// popen, for fontconfig on the desktop; the console never reaches it.
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include "font.h"
#include "engine.h"
#include "sjis.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
// The header offers a great deal more than glyph rasterising; the parts this
// engine never calls are not a defect in it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../vendor/stb_truetype.h"
#pragma GCC diagnostic pop

// Where a host keeps its fonts. Android puts them in /system/fonts and, since
// Android 10, ships downloadable ones under /data/fonts; desktop Linux uses the
// freedesktop locations. Missing directories are skipped in silence, so the same
// list works on both.
static const char* gFontDirectories[] = {
	"/system/fonts",
	"/data/fonts",
	"/usr/share/fonts",
	"/usr/local/share/fonts",
	NULL
};

#define FONT_MAX_FAMILIES 512
#define FONT_MAX_NAME 128
#define FONT_MAX_PATH 1024

// One family, and the file that answers for it when a script asks for it by name:
// the first face found with that family name, replaced by a later one only when the
// later one is the family's Regular and the first was not.
typedef struct FontFamily
{
	char name[FONT_MAX_NAME];
	char path[FONT_MAX_PATH];
	int  faceIndex;
	int  regular;
} FontFamily_t;

static FontFamily_t gFamilies[FONT_MAX_FAMILIES];
static uint32_t gFamilyCount = 0;
static int gInitialised = 0;

static uint16_t Font_ReadU16(const uint8_t* p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t Font_ReadU32(const uint8_t* p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void Font_AddFamily(const char* name, const char* path, int faceIndex, int regular)
{
	if(name[0] == 0)
		return;

	for(uint32_t i = 0; i < gFamilyCount; i++)
	{
		if(strcmp(gFamilies[i].name, name) == 0)
		{
			if(regular && !gFamilies[i].regular)
			{
				snprintf(gFamilies[i].path, FONT_MAX_PATH, "%s", path);
				gFamilies[i].faceIndex = faceIndex;
				gFamilies[i].regular = 1;
			}
			return;
		}
	}

	if(gFamilyCount >= FONT_MAX_FAMILIES)
		return;
	FontFamily_t* family = &gFamilies[gFamilyCount++];
	snprintf(family->name, FONT_MAX_NAME, "%s", name);
	snprintf(family->path, FONT_MAX_PATH, "%s", path);
	family->faceIndex = faceIndex;
	family->regular = regular;
}

// Copies one "name" table record out as bytes we can hand to a script. A record
// on platform 3 (Windows) is UTF-16BE, so anything outside ASCII is dropped
// rather than mangled; platform 1 (Macintosh) records are already single byte.
static int Font_DecodeName(const uint8_t* data, uint16_t length, int isUtf16, char* out, size_t outSize)
{
	size_t written = 0;

	if(isUtf16)
	{
		for(uint16_t i = 0; i + 1 < length; i += 2)
		{
			uint16_t ch = Font_ReadU16(data + i);
			if(ch == 0 || ch > 0x7F)
				return 0;
			if(written + 1 >= outSize)
				return 0;
			out[written++] = (char)ch;
		}
	}
	else
	{
		for(uint16_t i = 0; i < length; i++)
		{
			uint8_t ch = data[i];
			if(ch == 0 || ch > 0x7F)
				return 0;
			if(written + 1 >= outSize)
				return 0;
			out[written++] = (char)ch;
		}
	}

	out[written] = 0;
	return written > 0;
}

// Name `nameId` of one sfnt at `base` within the file, the Windows record winning
// over a Macintosh one. 0 when the font has none that decodes.
static int Font_ReadName(const uint8_t* file, size_t fileSize, uint32_t base, uint16_t nameId, char* out, size_t outSize)
{
	if(base + 12 > fileSize)
		return 0;

	uint16_t tableCount = Font_ReadU16(file + base + 4);
	uint32_t nameOffset = 0;
	uint32_t nameLength = 0;

	for(uint16_t i = 0; i < tableCount; i++)
	{
		uint32_t entry = base + 12 + i * 16;
		if(entry + 16 > fileSize)
			return 0;
		if(memcmp(file + entry, "name", 4) == 0)
		{
			nameOffset = Font_ReadU32(file + entry + 8);
			nameLength = Font_ReadU32(file + entry + 12);
			break;
		}
	}

	if(nameOffset == 0 || nameOffset + 6 > fileSize || nameLength < 6)
		return 0;

	const uint8_t* name = file + nameOffset;
	uint16_t recordCount = Font_ReadU16(name + 2);
	uint16_t stringBase = Font_ReadU16(name + 4);
	int have = 0;

	for(uint16_t i = 0; i < recordCount; i++)
	{
		uint32_t record = nameOffset + 6 + i * 12;
		if(record + 12 > fileSize)
			break;

		uint16_t platform = Font_ReadU16(file + record);
		uint16_t id = Font_ReadU16(file + record + 6);
		uint16_t length = Font_ReadU16(file + record + 8);
		uint16_t offset = Font_ReadU16(file + record + 10);

		if(id != nameId)
			continue;

		uint32_t stringAt = nameOffset + stringBase + offset;
		if(stringAt + length > fileSize)
			continue;

		char decoded[FONT_MAX_NAME];
		if(!Font_DecodeName(file + stringAt, length, platform == 3, decoded, sizeof(decoded)))
			continue;

		// A Windows record is what the original engine would have seen, so it wins
		// over a Macintosh one for the same font.
		if(!have || platform == 3)
		{
			snprintf(out, outSize, "%s", decoded);
			have = 1;
			if(platform == 3)
				break;
		}
	}
	return have;
}

static uint8_t* Font_LoadFile(const char* path, size_t* sizeOut)
{
	FILE* f = fopen(path, "rb");
	if(f == NULL)
		return NULL;

	if(fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return NULL;
	}
	long size = ftell(f);
	if(size <= 12 || size > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return NULL;
	}

	uint8_t* file = (uint8_t*)malloc((size_t)size);
	if(file == NULL)
	{
		fclose(f);
		return NULL;
	}
	if(fread(file, 1, (size_t)size, f) != (size_t)size)
	{
		free(file);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*sizeOut = (size_t)size;
	return file;
}

static void Font_ReadFile(const char* path)
{
	size_t size = 0;
	uint8_t* file = Font_LoadFile(path, &size);
	if(file == NULL)
		return;

	uint32_t fontCount = 1;
	int collection = memcmp(file, "ttcf", 4) == 0;
	if(collection)
		fontCount = Font_ReadU32(file + 8);

	for(uint32_t i = 0; i < fontCount; i++)
	{
		uint32_t base = 0;
		if(collection)
		{
			// A collection: every font in it has its own table directory.
			uint32_t at = 12 + i * 4;
			if(at + 4 > size)
				break;
			base = Font_ReadU32(file + at);
		}
		char family[FONT_MAX_NAME];
		char style[FONT_MAX_NAME];
		if(!Font_ReadName(file, size, base, 1, family, sizeof(family)))
			continue;
		int regular = Font_ReadName(file, size, base, 2, style, sizeof(style)) && strcmp(style, "Regular") == 0;
		Font_AddFamily(family, path, (int)i, regular);
	}

	free(file);
}

static int Font_HasFontExtension(const char* name)
{
	const char* dot = strrchr(name, '.');
	if(dot == NULL)
		return 0;
	return strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0 ||
	       strcasecmp(dot, ".ttc") == 0 || strcasecmp(dot, ".otc") == 0;
}

static void Font_ScanDirectory(const char* path, int depth)
{
	if(depth > 4)
		return;

	DIR* dir = opendir(path);
	if(dir == NULL)
		return;

	struct dirent* entry;
	while((entry = readdir(dir)) != NULL)
	{
		if(entry->d_name[0] == '.')
			continue;

		char full[1024];
		if(snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= (int)sizeof(full))
			continue;

		struct stat info;
		if(stat(full, &info) != 0)
			continue;

		if(S_ISDIR(info.st_mode))
			Font_ScanDirectory(full, depth + 1);
		else if(Font_HasFontExtension(entry->d_name))
			Font_ReadFile(full);
	}

	closedir(dir);
}

void Font_Init()
{
	if(gInitialised)
		return;
	gInitialised = 1;

	for(int i = 0; gFontDirectories[i] != NULL; i++)
		Font_ScanDirectory(gFontDirectories[i], 0);

	printf("[Font]: Found %d font families\n", gFamilyCount);
}

uint32_t Font_GetFamilyCount()
{
	return gFamilyCount;
}

const char* Font_GetFamilyName(uint32_t index)
{
	if(index >= gFamilyCount)
		return NULL;
	return gFamilies[index].name;
}

// ----------------------------------------------------------------------------
// Host faces
// ----------------------------------------------------------------------------

// A face loaded for drawing. Faces are kept for the life of the engine: fonts are
// made and remade (every tag in a line of text makes one) and a CJK collection is
// tens of megabytes.
typedef struct FontFace
{
	char path[FONT_MAX_PATH];
	int faceIndex;
	uint8_t* file;
	stbtt_fontinfo info;
	int unitsPerEm;
	// OS/2 usWinAscent, usWinDescent and xAvgCharWidth, which is what GDI's cell
	// height, tmAscent and tmAveCharWidth come from.
	int winAscent;
	int winDescent;
	int averageWidth;
	struct FontFace* next;
} FontFace_t;

static FontFace_t* gFaces = NULL;

static FontFace_t* Font_LoadFace(const char* path, int faceIndex)
{
	for(FontFace_t* face = gFaces; face != NULL; face = face->next)
		if(face->faceIndex == faceIndex && strcmp(face->path, path) == 0)
			return face;

	size_t size = 0;
	uint8_t* file = Font_LoadFile(path, &size);
	if(file == NULL)
		return NULL;
	int offset = stbtt_GetFontOffsetForIndex(file, faceIndex);
	FontFace_t* face = (FontFace_t*)calloc(1, sizeof(FontFace_t));
	if(face == NULL || offset < 0 || !stbtt_InitFont(&face->info, file, offset))
	{
		free(face);
		free(file);
		return NULL;
	}
	snprintf(face->path, FONT_MAX_PATH, "%s", path);
	face->faceIndex = faceIndex;
	face->file = file;
	face->unitsPerEm = ttUSHORT(file + face->info.head + 18);
	int ascent = 0, descent = 0, gap = 0;
	stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &gap);
	face->winAscent = ascent;
	face->winDescent = -descent;
	face->averageWidth = face->unitsPerEm / 2;
	uint32_t os2 = stbtt__find_table(file, (uint32_t)offset, "OS/2");
	if(os2 != 0)
	{
		face->averageWidth = ttSHORT(file + os2 + 2);
		face->winAscent = ttUSHORT(file + os2 + 74);
		face->winDescent = ttUSHORT(file + os2 + 76);
	}
	if(face->unitsPerEm <= 0)
		face->unitsPerEm = 1000;
	if(face->winAscent + face->winDescent <= 0)
	{
		face->winAscent = face->unitsPerEm;
		face->winDescent = 0;
	}
	face->next = gFaces;
	gFaces = face;
	return face;
}

// The console's own Japanese faces, newest naming first, and the paths desktop
// distributions put Noto CJK at. Android has carried a CJK face since Droid Sans
// Fallback and carries Noto Sans CJK today. These are paths on the host, not files
// in this repository.
static const char* gJapaneseFacePaths[] = {
	"/system/fonts/NotoSansCJK-Regular.ttc",
	"/system/fonts/NotoSansJP-Regular.otf",
	"/system/fonts/NotoSansCJKjp-Regular.otf",
	"/system/fonts/DroidSansJapanese.ttf",
	"/system/fonts/DroidSansFallback.ttf",
	"/system/fonts/DroidSansFallbackFull.ttf",
	"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
	"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
	NULL
};

// Whether a face can draw the text the original drew with MS Gothic: MS Gothic
// covers the kana and the Latin a line of Japanese text mixes in, so the face that
// stands in for it must have both.
static int Font_FaceCoversJapanese(FontFace_t* face)
{
	return stbtt_FindGlyphIndex(&face->info, 'A') != 0 &&
	       stbtt_FindGlyphIndex(&face->info, 0x3042) != 0 &&
	       stbtt_FindGlyphIndex(&face->info, 0x6F22) != 0;
}

#ifndef __ANDROID__
// fontconfig's answers, asked for through fc-match rather than linked against, so
// the Android build carries no library Android does not have. The console never
// reaches this: it finds its face in the list above.
static FontFace_t* Font_AskFontconfig(const char* pattern)
{
	char command[512];
	char line[1024];
	snprintf(command, sizeof(command), "fc-match --sort -f '%%{file}\\n' '%s' 2>/dev/null", pattern);
	FILE* pipe = popen(command, "r");
	if(pipe == NULL)
		return NULL;
	FontFace_t* found = NULL;
	int tried = 0;
	while(found == NULL && tried < 24 && fgets(line, sizeof(line), pipe) != NULL)
	{
		size_t n = strlen(line);
		while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if(n == 0)
			continue;
		tried++;
		FontFace_t* face = Font_LoadFace(line, 0);
		if(face != NULL && Font_FaceCoversJapanese(face))
			found = face;
	}
	pclose(pipe);
	return found;
}
#endif

static FontFace_t* gJapaneseFace = NULL;
static int gJapaneseFaceSearched = 0;

// The host face that stands in for MS Gothic. Face 0 of every CJK collection
// Android ships is its Japanese one.
static FontFace_t* Font_JapaneseFace(void)
{
	if(gJapaneseFaceSearched)
		return gJapaneseFace;
	gJapaneseFaceSearched = 1;
	for(int i = 0; gJapaneseFacePaths[i] != NULL && gJapaneseFace == NULL; i++)
	{
		FontFace_t* face = Font_LoadFace(gJapaneseFacePaths[i], 0);
		if(face != NULL && Font_FaceCoversJapanese(face))
			gJapaneseFace = face;
	}
#ifndef __ANDROID__
	if(gJapaneseFace == NULL)
		gJapaneseFace = Font_AskFontconfig("sans-serif:lang=ja");
#endif
	if(gJapaneseFace != NULL)
		printf("[Font]: Japanese text is drawn with %s\n", gJapaneseFace->path);
	else
		printf("[Font]: Warning: the host has no face that covers Japanese text\n");
	return gJapaneseFace;
}

// The faces a Windows machine answers a Japanese game with and the host does not
// have. Each is drawn with the host's Japanese face in the metrics of the face it
// stands in for, so that it takes the room on the screen the original's text took:
// MS Gothic's em is 256 units with an ascent of 220 and a descent of 36 - its cell
// is its em - and its average character width, which is what a width passed to
// CreateFontA is measured against, is half the em. The names are as the scripts
// spell them, in Shift-JIS, and in the English form Windows also accepts.
typedef struct FontStandIn
{
	const char* name;
	int unitsPerEm;
	int ascent;
	int descent;
	int averageWidth;
} FontStandIn_t;

static const FontStandIn_t gStandIns[] = {
	{ "\x82\x6C\x82\x72\x20\x83\x53\x83\x56\x83\x62\x83\x4E", 256, 220, 36, 128 }, // ＭＳ ゴシック
	{ "MS Gothic",                                         256, 220, 36, 128 },
	{ NULL, 0, 0, 0, 0 }
};

// What CreateFontA would have selected for a face name: the host's own face of
// that name, or the host's Japanese face standing in for a Windows one (with that
// face's metrics in *standIn), or nothing - the case in which the original, finding
// that the face it got is not the one it named, falls back (0x0042DF74).
static FontFace_t* Font_ResolveFace(const char* name, const FontStandIn_t** standIn)
{
	*standIn = NULL;
	for(int i = 0; gStandIns[i].name != NULL; i++)
	{
		if(strcmp(gStandIns[i].name, name) == 0)
		{
			FontFace_t* face = Font_JapaneseFace();
			if(face != NULL)
				*standIn = &gStandIns[i];
			return face;
		}
	}
	Font_Init();
	for(uint32_t i = 0; i < gFamilyCount; i++)
		if(strcmp(gFamilies[i].name, name) == 0)
			return Font_LoadFace(gFamilies[i].path, gFamilies[i].faceIndex);
	return NULL;
}

// ----------------------------------------------------------------------------
// Glyph quality (0x0042DD10 - 0x0042DE38)
// ----------------------------------------------------------------------------

// 0x00507694: 1 while characters are drawn with TextOut into the DIB, 0 once
// 0x0042DD40 has switched to GetGlyphOutline. The original switches only when a
// test character drawn through TextOut at start-up (0x00468A80) comes out with ink
// where an 'a' has none, i.e. on a system whose TextOut into the DIB is broken;
// this engine's TextOut is the rasteriser below, so the switch never happens here
// and the GetGlyphOutline arm is kept for completeness.
static int gTextOutGlyphs = 1;
// 0x00507698, the quality level, which the GetGlyphOutline arm reads.
static int32_t gQuality = 1;
// 0x0050769C, 0x005076A0 and 0x005076A4: the shift that brings a count of inked
// samples back to 0..0xFF, how many samples a side one cell pixel is drawn at, and
// that number's log2. Level 1 is what the data section starts with.
static int gCoverageShift = 4;
static int gSampleScale = 4;
static int gSampleShift = 2;
// 0x00565B64 and 0x00565B60, both in the zeroed data section.
static uint32_t gCoverageCurve = 0;
static uint32_t gPitchCheck = 0;

int Font_SetQuality(int32_t level)
{
	// 0x0042DD62: a level above 3 is refused and nothing is written.
	if(level > 3)
		return 0;
	if(gTextOutGlyphs && (uint32_t)level <= 3)
	{
		static const int shift[4] = { 2, 4, 6, 8 };
		static const int scale[4] = { 2, 4, 8, 16 };
		static const int log2[4] = { 1, 2, 3, 4 };
		gCoverageShift = shift[level];
		gSampleScale = scale[level];
		gSampleShift = log2[level];
	}
	else
	{
		// 0x0042DE1C: one sample a pixel, and the GetGlyphOutline arm picks its
		// format from the level.
		gSampleScale = 1;
		gSampleShift = 0;
		gCoverageShift = 0;
	}
	gQuality = level;
	return 1;
}

int Font_SetCoverageCurve(uint32_t curve)
{
	if(curve > 1)
		return 0;
	gCoverageCurve = curve;
	return 1;
}

void Font_SetPitchCheck(uint32_t value)
{
	gPitchCheck = value;
}

// ----------------------------------------------------------------------------
// The font object (0x0042D780, 0xAC bytes)
// ----------------------------------------------------------------------------

typedef struct FontCacheEntry
{
	FontGlyphInfo_t info;          // +0x00..+0x18
	struct FontCacheEntry* next;   // +0x1C
} FontCacheEntry_t;

struct FontObject
{
	int      created;              // +0x04
	int      size;                 // +0x08
	int      width;                // +0x0C
	int      bold;                 // +0x10
	int      italic;               // +0x14
	int32_t  scaleX;               // +0x18, 16.16
	int32_t  scaleY;               // +0x1C, 16.16
	int      offsetX;              // +0x20, cell pixels
	int      offsetY;              // +0x24
	int32_t  adjust[4];            // +0x28..+0x34
	int32_t  spacing[2];           // +0x38, +0x3C
	int      characterHeight;      // +0x40: 1 when the adjust values were all zero
	int      cellWidth;            // +0x44
	int      cellHeight;           // +0x48
	uint8_t* cache;                // +0x4C
	int      cellStride;           // +0x50
	int      cellBytes;            // +0x54
	int      surfaceWidth;         // +0x58
	int      surfaceHalfHeight;    // +0x5C
	int      surfaceStride;        // +0x60
	FontCacheEntry_t* recent;      // +0x84, most recently used first
	FontCacheEntry_t* entries;     // +0x88
	int      used;                 // +0x8C
	int      capacity;             // +0x90
	// +0x94..+0xA8: the DIB section, the memory DC and the HFONT. Here the DIB is
	// plain memory and the HFONT is the host face with the size CreateFontA was
	// asked for.
	uint8_t* surface;              // +0xA0
	FontFace_t* face;
	const FontStandIn_t* standIn;
	float    emPixels;             // the em, in DIB pixels
	float    stretch;              // horizontal scale from CreateFontA's width
	int      ascent;               // tmAscent, in DIB pixels
	int      emboldened;           // GDI's simulated bold
	// The sampling the DIB was sized for. The original reads the globals each time,
	// and its only caller that changes them remakes every font at once (Grp0 0x0D);
	// the font keeps them so its DIB is always read at the size it was made.
	int      sampleScale;
	int      sampleShift;
	int      coverageShift;
};

FontObject_t* Font_NewObject(void)
{
	FontObject_t* font = (FontObject_t*)calloc(1, sizeof(FontObject_t));
	// 0x0042D791: 0x0042EAC0 with 0, 0 - both spacing values start at nothing.
	return font;
}

// 0x0042DE50: what a font object holds, released; the object stays usable.
static void Font_Release(FontObject_t* font)
{
	free(font->surface);
	font->surface = NULL;
	font->created = 0;
}

void Font_DeleteObject(FontObject_t* font)
{
	if(font == NULL)
		return;
	Font_Release(font);
	free(font->cache);
	free(font->entries);
	free(font);
}

// 0x0042EB90 with edi 0: a name of 1..31 bytes, a size of 4..200, a width of
// 25..200 per cent.
static uint32_t Font_CheckRequest(const char* name, int size, int width)
{
	if(name == NULL || name[0] == 0 || strlen(name) >= 0x20)
		return 0x80000004;
	if(size < 4 || size > 200)
		return 0x80000002;
	if(width < 25 || width > 200)
		return 0x80000003;
	return 0;
}

// 0x0042EC10: the adjust values. Both scales 1.0..2.0 in 16.16, the first also 0
// for "the second"; each origin no further than 1.0 - 1/scale. All four zero is
// accepted.
static uint32_t Font_CheckAdjust(const int32_t* adjust)
{
	if(adjust[0] == 0 && adjust[1] == 0 && adjust[2] == 0 && adjust[3] == 0)
		return 0;
	if((adjust[0] < 0x10000 || adjust[0] > 0x20000) && adjust[0] != 0)
		return 0x80000005;
	if(adjust[1] < 0x10000 || adjust[1] > 0x20000)
		return 0x80000005;
	int32_t reference = adjust[0] != 0 ? adjust[0] : adjust[1];
	if(adjust[2] > (int32_t)(65536.0 - 4294967296.0 / (double)reference))
		return 0x80000006;
	if(adjust[3] > (int32_t)(65536.0 - 4294967296.0 / (double)adjust[1]))
		return 0x80000006;
	return 0;
}

// What CreateFontA does with its height and width, for the face it selects: a
// negative height is the character height (the em), a positive one the cell
// height (ascent plus descent); a width of 0 keeps the face's own proportions and
// any other is the average character width wanted.
static void Font_SetGeometry(FontObject_t* font, int height, int width)
{
	int unitsPerEm = font->standIn ? font->standIn->unitsPerEm : font->face->unitsPerEm;
	int ascent = font->standIn ? font->standIn->ascent : font->face->winAscent;
	int descent = font->standIn ? font->standIn->descent : font->face->winDescent;
	int average = font->standIn ? font->standIn->averageWidth : font->face->averageWidth;
	if(height < 0)
		font->emPixels = (float)-height;
	else if(height > 0)
		font->emPixels = (float)height * (float)unitsPerEm / (float)(ascent + descent);
	else
		font->emPixels = 12.0f;
	font->ascent = (int)(font->emPixels * (float)ascent / (float)unitsPerEm + 0.5f);
	font->stretch = 1.0f;
	if(width != 0 && average > 0)
		font->stretch = (float)width / (font->emPixels * (float)average / (float)unitsPerEm);
}

// 0x0042E020: the cell, the DIB and the font itself.
static uint32_t Font_Build(FontObject_t* font, const char* name, int size, int width, int bold,
                           int italic, int level, const int32_t* adjust, FontFace_t* face,
                           const FontStandIn_t* standIn)
{
	int32_t scaleX = 0x10000;
	int32_t scaleY = 0x10000;
	int32_t originX = 0;
	int32_t originY = 0;
	int useWidth = 1;
	int characterHeight = 0;
	if(adjust != NULL)
	{
		uint32_t bad = Font_CheckAdjust(adjust);
		if(bad != 0)
			return bad;
		if(adjust[0] == 0 && adjust[1] == 0 && adjust[2] == 0 && adjust[3] == 0)
		{
			// 0x0042E077: all zero asks for the character height and leaves
			// CreateFontA's width to the face.
			characterHeight = 1;
			useWidth = 0;
		}
		else
		{
			useWidth = adjust[0] != 0;
			scaleX = adjust[0] != 0 ? adjust[0] : adjust[1];
			scaleY = adjust[1];
			originX = adjust[2];
			originY = adjust[3];
		}
		memcpy(font->adjust, adjust, sizeof(font->adjust));
	}
	else
	{
		font->adjust[0] = 0x10000;
		font->adjust[1] = 0x10000;
		font->adjust[2] = 0;
		font->adjust[3] = 0;
	}

	// 0x0042E0CE: a cell two characters wide and one and a half high, one byte a
	// pixel, with `level` of them in the cache.
	int cellWidth = (2 * size * width) / 100;
	int cellHeight = (3 * size) / 2;
	font->cellWidth = cellWidth;
	font->cellStride = cellWidth;
	font->cellHeight = cellHeight;
	font->cellBytes = cellWidth * cellHeight;
	font->sampleScale = gSampleScale;
	font->sampleShift = gSampleShift;
	font->coverageShift = gCoverageShift;
	font->surfaceWidth = ((cellWidth * scaleX) >> 16) * font->sampleScale;
	font->surfaceHalfHeight = (characterHeight ? cellHeight + cellHeight : ((cellHeight * scaleY) >> 16)) * font->sampleScale;

	free(font->cache);
	free(font->entries);
	font->cache = (uint8_t*)malloc((size_t)font->cellBytes * (size_t)level);
	font->entries = (FontCacheEntry_t*)calloc((size_t)level, sizeof(FontCacheEntry_t));
	if(font->cache == NULL || font->entries == NULL)
		return 0x80000004;
	font->capacity = level;
	font->used = 0;
	font->recent = NULL;
	for(int i = 0; i < level; i++)
		font->entries[i].info.pixels = font->cache + (size_t)i * (size_t)font->cellBytes;

	// 0x0042E1A3: the height asked of CreateFontA, and the width - half the cell's
	// width, which is one character's, when the adjust values leave it to us.
	int height = ((scaleY * size) >> 16) * font->sampleScale;
	int wantedWidth = 0;
	if(useWidth)
		wantedWidth = (((((size * width) / 100) * scaleX) >> 16) * font->sampleScale) >> 1;
	// 0x0042E1F0: with the pitch check on, a face that 0x0042DC40 reports as
	// variable pitch (2) is left its own width. The query asks the host's font
	// enumeration for the face's lfPitchAndFamily, which no host font carries; the
	// stand-in for MS Gothic is fixed pitch, as MS Gothic is, and a host face is
	// taken at its word that it is proportional unless its post table says fixed.
	if(gPitchCheck != 0 && standIn == NULL && face != NULL)
	{
		uint32_t post = stbtt__find_table(face->file, (uint32_t)face->info.fontstart, "post");
		int fixed = post != 0 && ttULONG(face->file + post + 12) != 0;
		if(!fixed)
			wantedWidth = 0;
	}
	if(characterHeight)
		height = -height;

	font->face = face;
	font->standIn = standIn;
	Font_SetGeometry(font, height, wantedWidth);
	// CreateFontA's weight: 700 for bold, 100 otherwise (0x0042E24A). A face with no
	// bold of its own is emboldened by GDI.
	font->emboldened = bold != 0;

	free(font->surface);
	font->surfaceStride = (font->surfaceWidth + 3) & ~3;
	font->surface = (uint8_t*)calloc((size_t)font->surfaceStride, (size_t)font->surfaceHalfHeight * 2 + 1);
	if(font->surface == NULL)
		return 0x80000004;

	font->size = size;
	font->width = width;
	font->bold = bold;
	font->italic = italic;
	if(characterHeight)
	{
		// 0x0042E379: the origin from the outline metrics - the character's top
		// moved so its ascent sits seven eighths of a character down.
		font->scaleX = 0x10000;
		font->scaleY = 0x10000;
		font->offsetX = 0;
		int shift = (font->ascent - (7 * -height) / 8) >> font->sampleShift;
		font->offsetY = shift < 0 ? 0 : shift;
	}
	else
	{
		font->scaleX = scaleX;
		font->scaleY = scaleY;
		font->offsetX = ((((scaleX * size) >> 16) * originX) >> 16);
		font->offsetY = ((((scaleY * size) >> 16) * originY) >> 16);
	}
	font->characterHeight = characterHeight;
	font->created = 1;
	return 0;
}

uint32_t Font_CreateObject(FontObject_t* font, const char* name, int size, int width, int bold,
                           int italic, const int32_t* adjust, int level, int fallback)
{
	// 0x0042DEE0: whatever the object held goes first.
	Font_Release(font);
	if(level < 2)
		return 0x80000001;
	uint32_t bad = Font_CheckRequest(name, size, width);
	if(bad != 0)
		return bad;

	const FontStandIn_t* standIn = NULL;
	FontFace_t* face = Font_ResolveFace(name, &standIn);
	if(face == NULL && fallback)
	{
		// 0x0042DFA9: the face the font came out with is not the one asked for, so
		// the font is made again under the substitution Ext0 0xC7 set up (0x0042ECB0),
		// or under the same name when there is none, and without falling back again.
		const char* substitute = Engine_GetFontSubstitution(name);
		return Font_CreateObject(font, substitute != NULL ? substitute : name, size, width, bold,
		                         italic, adjust, level, 0);
	}
	if(face == NULL)
	{
		// CreateFontA always answers with some face. With nothing of the name on the
		// host, the host's Japanese face is the one that can draw the text.
		face = Font_JapaneseFace();
		if(face == NULL)
			return 0x80000004;
	}
	return Font_Build(font, name, size, width, bold, italic, level, adjust, face, standIn);
}

// The character a code names, as TextOutA / TextOutW would be handed it
// (0x0042E498): 0xB000 and up is a UTF-16 code unit plus 0xB000, 0x7F is drawn as
// U+2014, anything else is Shift-JIS.
static uint32_t Font_CodeToUnicode(uint32_t code)
{
	if(code >= 0xB000)
		return code - 0xB000;
	if(code == 0x7F)
		return 0x2014;
	uint8_t bytes[3] = { 0, 0, 0 };
	if(code > 0xFF)
	{
		bytes[0] = (uint8_t)(code >> 8);
		bytes[1] = (uint8_t)code;
	}
	else
		bytes[0] = (uint8_t)code;
	return SjisToUTF16(bytes);
}

// TextOut(dc, 0, 0, character) into the DIB: the character cell's top left at the
// DIB's, so the baseline is tmAscent down. GDI fills the pixels whose centres the
// outline covers, which is what a coverage of one half or more is taken to mean,
// in the text colour, which the DIB's inverted palette makes a non-zero index;
// simulated bold overstrikes one pixel to the right.
static void Font_TextOut(FontObject_t* font, uint32_t code)
{
	uint32_t unicode = Font_CodeToUnicode(code);
	FontFace_t* face = font->face;
	int glyph = stbtt_FindGlyphIndex(&face->info, (int)unicode);
	if(glyph == 0)
	{
		// Windows links a face that lacks a character to one that has it; the
		// host's Japanese face is the one to ask.
		FontFace_t* other = Font_JapaneseFace();
		if(other != NULL && other != face)
		{
			int otherGlyph = stbtt_FindGlyphIndex(&other->info, (int)unicode);
			if(otherGlyph != 0)
			{
				face = other;
				glyph = otherGlyph;
			}
		}
	}
	if(glyph == 0)
		return;

	int unitsPerEm = font->standIn && face == font->face ? font->standIn->unitsPerEm : face->unitsPerEm;
	float scaleY = font->emPixels / (float)unitsPerEm;
	// A stand-in's em is its own; drawing its outlines at the em of the face it
	// stands in for is what makes them take that face's room.
	if(font->standIn != NULL)
		scaleY = font->emPixels / (float)face->unitsPerEm;
	float scaleX = scaleY * font->stretch;
	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBox(&face->info, glyph, scaleX, scaleY, &x0, &y0, &x1, &y1);
	int w = x1 - x0;
	int h = y1 - y0;
	if(w <= 0 || h <= 0)
		return;
	uint8_t* ink = (uint8_t*)calloc((size_t)w, (size_t)h);
	if(ink == NULL)
		return;
	stbtt_MakeGlyphBitmap(&face->info, ink, w, h, w, scaleX, scaleY, glyph);

	int surfaceHeight = font->surfaceHalfHeight * 2;
	int top = font->ascent + y0;
	for(int row = 0; row < h; row++)
	{
		int y = top + row;
		if(y < 0 || y >= surfaceHeight)
			continue;
		uint8_t* line = font->surface + (size_t)y * (size_t)font->surfaceStride;
		for(int column = 0; column < w; column++)
		{
			if(ink[row * w + column] < 0x80)
				continue;
			int x = x0 + column;
			for(int extra = 0; extra <= font->emboldened; extra++)
				if(x + extra >= 0 && x + extra < font->surfaceWidth)
					line[x + extra] = 0xFF;
		}
	}
	free(ink);
}

// GetGlyphOutlineA with GGO_GRAY2/4/8_BITMAP: the glyph's black box in levels of
// 0..`levels`, rows padded to four bytes, and its metrics.
typedef struct FontGlyphMetrics
{
	int blackBoxX;          // gmBlackBoxX
	int blackBoxY;          // gmBlackBoxY
	int originX;            // gmptGlyphOrigin.x
	int originY;            // gmptGlyphOrigin.y
	int cellIncX;           // gmCellIncX
	int cellIncY;           // gmCellIncY
} FontGlyphMetrics_t;

static uint8_t* Font_GetGlyphOutline(FontObject_t* font, uint32_t code, int levels, FontGlyphMetrics_t* metrics, int* size)
{
	memset(metrics, 0, sizeof(*metrics));
	*size = 0;
	uint32_t unicode = Font_CodeToUnicode(code);
	FontFace_t* face = font->face;
	int glyph = stbtt_FindGlyphIndex(&face->info, (int)unicode);
	float scaleY = font->emPixels / (float)face->unitsPerEm;
	float scaleX = scaleY * font->stretch;
	int advance = 0, bearing = 0;
	stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &bearing);
	metrics->cellIncX = (int)((float)advance * scaleX + 0.5f);
	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBox(&face->info, glyph, scaleX, scaleY, &x0, &y0, &x1, &y1);
	if(glyph == 0 || x1 <= x0 || y1 <= y0)
	{
		// What GDI answers for a blank: a one-pixel black box and no bitmap.
		metrics->blackBoxX = 1;
		metrics->blackBoxY = 1;
		return NULL;
	}
	int w = x1 - x0, h = y1 - y0;
	int pitch = (w + 3) & ~3;
	uint8_t* ink = (uint8_t*)calloc((size_t)w, (size_t)h);
	uint8_t* out = (uint8_t*)calloc((size_t)pitch, (size_t)h);
	if(ink == NULL || out == NULL)
	{
		free(ink);
		free(out);
		return NULL;
	}
	stbtt_MakeGlyphBitmap(&face->info, ink, w, h, w, scaleX, scaleY, glyph);
	for(int row = 0; row < h; row++)
		for(int column = 0; column < w; column++)
			out[row * pitch + column] = (uint8_t)((ink[row * w + column] * levels + 127) / 255);
	free(ink);
	metrics->blackBoxX = w;
	metrics->blackBoxY = h;
	metrics->originX = x0;
	metrics->originY = -y0;
	*size = pitch * h;
	return out;
}

// 0x0042E450: one character into a cache cell, and the part of it the ink covers.
static void Font_Render(FontObject_t* font, uint32_t code, uint8_t* cell, FontGlyphInfo_t* info)
{
	int cellWidth = font->cellWidth;
	int cellHeight = font->cellHeight;
	if(gTextOutGlyphs)
	{
		// 0x0042E480: the top half of the DIB cleared and the character drawn.
		memset(font->surface, 0, (size_t)font->surfaceStride * (size_t)font->surfaceHalfHeight);
		Font_TextOut(font, code);
		if(code == 0x20 || code == 0x8140)
		{
			// 0x0042E4F7: a space leaves the cell empty.
			memset(cell, 0, (size_t)font->cellBytes);
		}
		else if(font->sampleScale > 1)
		{
			// 0x0042E559: each cell pixel counts the inked samples of its square of
			// the DIB, from the origin the adjust values moved it to.
			int n = font->sampleScale;
			const uint8_t* rowStart = font->surface
			                        + (size_t)(font->surfaceStride << font->sampleShift) * (size_t)font->offsetY
			                        + (size_t)(font->offsetX << font->sampleShift);
			uint8_t* out = cell;
			for(int row = 0; row < cellHeight; row++)
			{
				const uint8_t* block = rowStart;
				for(int column = 0; column < cellWidth; column++)
				{
					uint32_t count = 0;
					const uint8_t* line = block;
					for(int sy = 0; sy < n; sy++)
					{
						for(int sx = 0; sx < n; sx++)
							if(line[sx] != 0)
								count++;
						line += font->surfaceStride;
					}
					if(gCoverageCurve == 0)
						*out++ = (uint8_t)((count * 0xFF) >> font->coverageShift);
					else if(gCoverageCurve == 1)
					{
						// 0x0042E5EA: sin(count * pi/2 / n^2) * 255, truncated.
						double v = sin((double)count * 1.5707963267948966 / (double)(n * n)) * 255.0;
						*out++ = (uint8_t)(int32_t)v;
					}
					block += n;
				}
				rowStart += (size_t)(font->surfaceStride << font->sampleShift);
			}
		}
		else
		{
			// 0x0042E68A: one sample a pixel - the DIB copied as it is.
			const uint8_t* in = font->surface + (size_t)font->surfaceStride * (size_t)font->offsetY + font->offsetX;
			for(int row = 0; row < cellHeight; row++)
				memcpy(cell + (size_t)row * (size_t)cellWidth, in + (size_t)row * (size_t)font->surfaceStride, (size_t)cellWidth);
		}
	}
	else
	{
		// 0x0042E6DB: GetGlyphOutlineA at the quality the level asks for.
		int format = gQuality == 0 ? 4 : gQuality == 1 ? 5 : 6;
		int shift = gQuality == 0 ? 2 : gQuality == 1 ? 4 : 6;
		int levels = format == 4 ? 4 : format == 5 ? 16 : 64;
		FontGlyphMetrics_t metrics;
		int size = 0;
		uint8_t* outline = Font_GetGlyphOutline(font, code, levels, &metrics, &size);
		memset(cell, 0, (size_t)font->cellBytes);
		if((metrics.blackBoxX >= 2 || metrics.blackBoxY >= 2) && metrics.blackBoxX > 0 && metrics.blackBoxY > 0 && outline != NULL)
		{
			// 0x0042E7BB: rows start where tmAscent less the origin puts them, the
			// columns at the adjust origin; the pitch is the size over the rows.
			int top = font->ascent - font->offsetY - metrics.originY;
			if(top < 0)
				top = 0;
			uint8_t* out = cell + (size_t)top * (size_t)font->cellStride + font->offsetX;
			int pitch = (size / metrics.blackBoxY) & ~3;
			int w = metrics.blackBoxX < cellWidth ? metrics.blackBoxX : cellWidth;
			int h = metrics.blackBoxY < cellHeight ? metrics.blackBoxY : cellHeight;
			const uint8_t* in = outline;
			for(int row = 0; row < h; row++)
			{
				for(int column = 0; column < w && out + column < cell + font->cellBytes; column++)
					out[column] = (uint8_t)((in[column] * 0xFF) >> shift);
				out += font->cellStride;
				in += pitch;
			}
		}
		free(outline);
	}

	// 0x0042E86B: the extent of the ink across the cell.
	info->top = 0;
	info->bottom = cellHeight - 1;
	if(code == 0x20 || code == 0x8140)
	{
		info->left = 0;
		info->right = font->size / 2 - 1;
		return;
	}
	int left = -1;
	for(int column = 0; column < cellWidth && left < 0; column++)
		for(int row = 0; row < cellHeight; row++)
			if(cell[row * font->cellStride + column] != 0)
			{
				left = column;
				break;
			}
	if(left < 0)
	{
		// 0x0042E95F: nothing drawn at all is taken as half a character.
		info->left = 0;
		info->right = font->size / 2 - 1;
		return;
	}
	info->left = left;
	for(int column = cellWidth - 1; column >= 0; column--)
	{
		int inked = 0;
		for(int row = 0; row < cellHeight; row++)
			if(cell[row * font->cellStride + column] != 0)
			{
				inked = 1;
				break;
			}
		if(!inked)
			continue;
		info->right = column;
		if(code == 0x8141 || code == 0x8142)
		{
			// 0x0042E923: the ideographic comma and full stop are given as much room
			// right of their ink as there is left of it.
			int right = 2 * column - info->left + 1;
			if(right >= cellWidth)
				right = cellWidth - 1;
			info->right = right;
		}
		return;
	}
}

void Font_GetGlyph(FontObject_t* font, uint32_t code, FontGlyphInfo_t* out)
{
	// 0x0042E9A0: the recently used list, most recent first, walked no further than
	// the number of cells in use.
	FontCacheEntry_t* previous = NULL;
	FontCacheEntry_t* entry = font->recent;
	for(int i = 0; i < font->used && entry != NULL; i++)
	{
		if(entry->info.code == code)
		{
			if(previous != NULL)
			{
				previous->next = entry->next;
				entry->next = font->recent;
				font->recent = entry;
			}
			*out = entry->info;
			return;
		}
		if(entry->next == NULL)
			break;
		previous = entry;
		entry = entry->next;
	}

	// 0x0042E9C4: a free cell while there is one, else the least recently used.
	if(font->used < font->capacity)
	{
		entry = &font->entries[font->used++];
		entry->next = font->recent;
		font->recent = entry;
	}
	else
	{
		if(previous != NULL)
		{
			previous->next = entry->next;
			entry->next = font->recent;
			font->recent = entry;
		}
	}
	entry->info.code = code;
	entry->info.doubleByte = code >= 0x100;
	Font_Render(font, code, entry->info.pixels, &entry->info);
	*out = entry->info;
}

int Font_CellWidth(const FontObject_t* font)
{
	return font->cellWidth;
}

int Font_CellHeight(const FontObject_t* font)
{
	return font->cellHeight;
}

int Font_CellStride(const FontObject_t* font)
{
	return font->cellStride;
}

void Font_GetAdjust(const FontObject_t* font, int32_t out[4])
{
	memcpy(out, font->adjust, sizeof(font->adjust));
}

void Font_SetSpacing(FontObject_t* font, int32_t first, int32_t second)
{
	font->spacing[0] = first;
	font->spacing[1] = second;
}

int Font_GetSpacing(const FontObject_t* font, uint32_t index, int32_t* out)
{
	if(index > 1)
		return 0;
	*out = font->spacing[index];
	return 1;
}

// ----------------------------------------------------------------------------
// The font manager (0x0042EDB0)
// ----------------------------------------------------------------------------

typedef struct FontEntry
{
	uint32_t id;                   // +0x00
	FontEntryInfo_t info;          // +0x04..+0x34
	struct FontEntry* next;        // +0x38
} FontEntry_t;

static FontEntry_t* gFontEntries = NULL;   // +0x78
// +0x40: the id the first font is numbered after.
static uint32_t gFontLastId = 0;
// +0x38: how many characters a managed font caches. 0x0042F150 would answer a
// per-font count from the list at +0x3C, which 0x0042F040 fills; nothing reaches
// 0x0042F040 in this binary, so every font gets this.
#define FONT_DEFAULT_LEVEL 0x40

// 0x0042EFA0 on the list at +0xAC: the adjust values Grp1 0x0E attached to a name.
static const int32_t* Font_FindAdjust(const char* name, int32_t storage[4])
{
	for(FontAdjust_t* entry = gFontAdjusts; entry != NULL; entry = entry->next)
	{
		if(strcmp(entry->name, name) == 0)
		{
			storage[0] = (int32_t)entry->scaleX;
			storage[1] = (int32_t)entry->scaleY;
			storage[2] = entry->originX;
			storage[3] = entry->originY;
			return storage;
		}
	}
	return NULL;
}

uint32_t Font_Open(const char* name, int size, int width, int bold, uint32_t* id)
{
	*id = 0;
	if(name == NULL)
		return 0x80000004;
	FontEntry_t* last = NULL;
	for(FontEntry_t* entry = gFontEntries; entry != NULL; entry = entry->next)
	{
		if(strcmp(entry->info.name, name) == 0 && entry->info.size == size &&
		   entry->info.width == width && entry->info.bold == bold)
		{
			*id = entry->id;
			return 0;
		}
		last = entry;
	}

	FontObject_t* font = Font_NewObject();
	if(font == NULL)
		return 0x80000004;
	int32_t storage[4];
	uint32_t result = Font_CreateObject(font, name, size, width, bold, 0,
	                                    Font_FindAdjust(name, storage), FONT_DEFAULT_LEVEL, 1);
	if(result != 0)
	{
		Font_DeleteObject(font);
		return result;
	}

	FontEntry_t* entry = (FontEntry_t*)calloc(1, sizeof(FontEntry_t));
	if(entry == NULL)
	{
		Font_DeleteObject(font);
		return 0x80000004;
	}
	// 0x0042F303: the id after the last entry's, the name copied whole.
	entry->id = (last != NULL ? last->id : gFontLastId) + 1;
	snprintf(entry->info.name, sizeof(entry->info.name), "%s", name);
	entry->info.size = size;
	entry->info.width = width;
	entry->info.bold = bold;
	entry->info.italic = 0;
	entry->info.font = font;
	if(last != NULL)
		last->next = entry;
	else
		gFontEntries = entry;
	*id = entry->id;
	return 0;
}

int Font_GetInfo(uint32_t id, FontEntryInfo_t* out)
{
	for(FontEntry_t* entry = gFontEntries; entry != NULL; entry = entry->next)
	{
		if(entry->id == id)
		{
			*out = entry->info;
			return 1;
		}
	}
	return 0;
}

void Font_RecreateAll(void)
{
	for(FontEntry_t* entry = gFontEntries; entry != NULL; entry = entry->next)
	{
		Font_DeleteObject(entry->info.font);
		entry->info.font = Font_NewObject();
		if(entry->info.font == NULL)
			continue;
		int32_t storage[4];
		Font_CreateObject(entry->info.font, entry->info.name, entry->info.size, entry->info.width,
		                  entry->info.bold, 0, Font_FindAdjust(entry->info.name, storage),
		                  FONT_DEFAULT_LEVEL, 1);
	}
}

void Font_Free()
{
	while(gFontEntries != NULL)
	{
		FontEntry_t* next = gFontEntries->next;
		Font_DeleteObject(gFontEntries->info.font);
		free(gFontEntries);
		gFontEntries = next;
	}
	while(gFaces != NULL)
	{
		FontFace_t* next = gFaces->next;
		free(gFaces->file);
		free(gFaces);
		gFaces = next;
	}
	gJapaneseFace = NULL;
	gJapaneseFaceSearched = 0;
	gFamilyCount = 0;
	gInitialised = 0;
}
