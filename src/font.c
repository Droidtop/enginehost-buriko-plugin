#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "font.h"

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

static char gFamilies[FONT_MAX_FAMILIES][FONT_MAX_NAME];
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

static void Font_AddFamily(const char* name)
{
	if(name[0] == 0 || gFamilyCount >= FONT_MAX_FAMILIES)
		return;

	for(uint32_t i = 0; i < gFamilyCount; i++)
	{
		if(strcmp(gFamilies[i], name) == 0)
			return;
	}

	strncpy(gFamilies[gFamilyCount], name, FONT_MAX_NAME - 1);
	gFamilies[gFamilyCount][FONT_MAX_NAME - 1] = 0;
	gFamilyCount++;
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

// Reads the family name (name ID 1) out of one sfnt at `base` within the file.
static void Font_ReadSfnt(const uint8_t* file, size_t fileSize, uint32_t base)
{
	if(base + 12 > fileSize)
		return;

	uint16_t tableCount = Font_ReadU16(file + base + 4);
	uint32_t nameOffset = 0;
	uint32_t nameLength = 0;

	for(uint16_t i = 0; i < tableCount; i++)
	{
		uint32_t entry = base + 12 + i * 16;
		if(entry + 16 > fileSize)
			return;
		if(memcmp(file + entry, "name", 4) == 0)
		{
			nameOffset = Font_ReadU32(file + entry + 8);
			nameLength = Font_ReadU32(file + entry + 12);
			break;
		}
	}

	if(nameOffset == 0 || nameOffset + 6 > fileSize || nameLength < 6)
		return;

	const uint8_t* name = file + nameOffset;
	uint16_t recordCount = Font_ReadU16(name + 2);
	uint16_t stringBase = Font_ReadU16(name + 4);

	char best[FONT_MAX_NAME];
	int haveBest = 0;

	for(uint16_t i = 0; i < recordCount; i++)
	{
		uint32_t record = nameOffset + 6 + i * 12;
		if(record + 12 > fileSize)
			return;

		uint16_t platform = Font_ReadU16(file + record);
		uint16_t nameId = Font_ReadU16(file + record + 6);
		uint16_t length = Font_ReadU16(file + record + 8);
		uint16_t offset = Font_ReadU16(file + record + 10);

		if(nameId != 1)
			continue;

		uint32_t stringAt = nameOffset + stringBase + offset;
		if(stringAt + length > fileSize)
			continue;

		char decoded[FONT_MAX_NAME];
		if(!Font_DecodeName(file + stringAt, length, platform == 3, decoded, sizeof(decoded)))
			continue;

		// A Windows record is what the original engine would have seen, so it wins
		// over a Macintosh one for the same font.
		if(!haveBest || platform == 3)
		{
			strcpy(best, decoded);
			haveBest = 1;
			if(platform == 3)
				break;
		}
	}

	if(haveBest)
		Font_AddFamily(best);
}

static void Font_ReadFile(const char* path)
{
	FILE* f = fopen(path, "rb");
	if(f == NULL)
		return;

	if(fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return;
	}
	long size = ftell(f);
	if(size <= 12 || size > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return;
	}

	uint8_t* file = (uint8_t*)malloc((size_t)size);
	if(file == NULL)
	{
		fclose(f);
		return;
	}
	if(fread(file, 1, (size_t)size, f) != (size_t)size)
	{
		free(file);
		fclose(f);
		return;
	}
	fclose(f);

	if(memcmp(file, "ttcf", 4) == 0)
	{
		// A collection: every font in it has its own table directory.
		uint32_t fontCount = Font_ReadU32(file + 8);
		for(uint32_t i = 0; i < fontCount; i++)
		{
			uint32_t at = 12 + i * 4;
			if(at + 4 > (uint32_t)size)
				break;
			Font_ReadSfnt(file, (size_t)size, Font_ReadU32(file + at));
		}
	}
	else
	{
		Font_ReadSfnt(file, (size_t)size, 0);
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

void Font_Free()
{
	gFamilyCount = 0;
	gInitialised = 0;
}

uint32_t Font_GetFamilyCount()
{
	return gFamilyCount;
}

const char* Font_GetFamilyName(uint32_t index)
{
	if(index >= gFamilyCount)
		return NULL;
	return gFamilies[index];
}
