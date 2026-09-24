#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "text.h"
#include "engine.h"

// ----------------------------------------------------------------------------
// The text globals, by the address the original keeps them at
// ----------------------------------------------------------------------------

// 0x00507638: how much later each character starts to appear than the one before
// it, and what a <t> tag's number is multiplied by. 0x0050763C: how long a
// character takes to fade in. Both in the units the window's reveal counts in.
#define TEXT_DELAY_STEP     0x19
#define TEXT_FADE_LENGTH    0x96
// 0x00507640: the ruby's size, per cent of the text's, when 0x00565CE0 is not set.
#define TEXT_RUBY_PERCENT   0x28
// 0x005076B0: a proportionally set character's gap, 16.16 of the cell width
// (0x00409790): one sixth.
#define TEXT_GAP_FACTOR     0x2AAA

// 0x00565BB0: added to every character's advance in a monospaced layout (0x004370D0).
static int32_t gTextExtraSpacing = 0;
// 0x00565BDC: how far ruby text pushes the first line in (0x004345B0).
static int32_t gTextRubyIndent = 0;
// 0x00565BE0, 0x00565CE0..0x00565CEC, 0x00507644, 0x00507648: the ruby's font
// name, size, width, offset and colours (0x00434520). An empty name, a size or
// width of 0 and a colour of -1 mean "the text's own".
static char     gTextRubyFont[0x100];
static int32_t  gTextRubySize = 0;
static int32_t  gTextRubyWidth = 0;
static int32_t  gTextRubyOffsetX = 0;
static int32_t  gTextRubyOffsetY = 0;
static uint32_t gTextRubyColour = 0xFFFFFFFF;
static uint32_t gTextRubyEdgeColour = 0xFFFFFFFF;
// 0x00565CF0: whether a text that starts with an opening bracket indents its
// following lines to after the bracket (0x004355D8). Zero in the data section.
static uint32_t gTextHangingIndent = 0;
// 0x00565CFC..0x00565D28 (0x00438080) and 0x00507650 (0x00438130): the font and
// colour a <l> link is drawn in.
static char     gTextLinkFont[0x20];
static int32_t  gTextLinkSize = 0;
static int32_t  gTextLinkWidth = 0;
static uint32_t gTextLinkBold = 0;
static uint32_t gTextLinkItalic = 0;
static uint32_t gTextLinkColour = 0xFFFFFFFF;
// 0x00565CF8 and 0x0050E240: the links laid out by the last layout, up to sixteen,
// 0x80 bytes each - the link's text (0x60 bytes) and where it starts.
typedef struct TextLink
{
	char     text[0x60];
	uint8_t  unused[0x18];
	int32_t  x;             // +0x78
	int32_t  y;             // +0x7C
} TextLink_t;
static TextLink_t gTextLinks[16];
static uint32_t gTextLinkCount = 0;
// 0x00507674..0x00507684: the default style.
static TextStyle_t gTextDefaultStyle = { 0, 0x11, 0x11, 0, 0xC0 };
// 0x00507664..0x00507670: Grp0 0x95 and 0x96.
static int32_t gTextSplit1Count = 6;
static int32_t gTextSplit1Value = 0x1E;
static int32_t gTextSplit2Count = 0x10;
static int32_t gTextSplit2Value = 0x3C;

// 0x0050CA40: the external characters, codes 0xFF01..0xFFFF, each a bitmap of its
// own. 0x00432F20 fills them; no opcode this engine has reached calls it, so every
// entry is empty and such a code is skipped by the layout (0x00436799).
typedef struct TextGaiji
{
	Bitmap_t bitmap;
} TextGaiji_t;
static TextGaiji_t gTextGaiji[256];

int Text_MakeStyle(TextStyle_t* style, uint32_t kind, int32_t a, int32_t b, uint32_t colour, uint32_t weight)
{
	if(kind == 0)
	{
		memset(style, 0, sizeof(*style));
		return 1;
	}
	if(kind > 2 || (uint32_t)a > 100 || (uint32_t)b > 100 || weight > 0x100)
		return 0;
	style->kind = kind;
	style->a = a;
	style->b = b;
	style->colour = colour;
	style->weight = weight;
	return 1;
}

void Text_DefaultStyle(TextStyle_t* style)
{
	*style = gTextDefaultStyle;
}

void Text_SetDefaultStyleKind(uint32_t kind)
{
	gTextDefaultStyle.kind = kind;
}

int Text_SetDefaultStyleEdge(int32_t a, int32_t b, uint32_t weight)
{
	if((uint32_t)a > 100 || (uint32_t)b > 100 || weight > 0x100)
		return 0;
	gTextDefaultStyle.a = a;
	gTextDefaultStyle.b = b;
	gTextDefaultStyle.colour = 0;
	gTextDefaultStyle.weight = weight;
	return 1;
}

int Text_SetSplitPair1(int32_t count, int32_t value)
{
	if(count > 0)
	{
		gTextSplit1Count = count;
		gTextSplit1Value = value;
	}
	return count > 0;
}

int Text_SetSplitPair2(int32_t count, int32_t value)
{
	if(count > 0)
	{
		gTextSplit2Count = count;
		gTextSplit2Value = value;
	}
	return count > 0;
}

// ----------------------------------------------------------------------------
// Characters
// ----------------------------------------------------------------------------

// 0x0042FB60: whether a byte starts a two-byte character. This build answers yes
// for every byte from 0x80 up: its compare against 0xA0 is jumped over, so the
// half-width katakana range is taken as lead bytes too.
static int Text_IsLeadByte(uint8_t c)
{
	return c >= 0x80;
}

// 0x0042EAF0: the character at `p` as a code (the lead byte in the high half for a
// two-byte one), and whether it took two bytes.
static int Text_Decode(const char* p, uint32_t* code)
{
	uint8_t c = (uint8_t)p[0];
	if(Text_IsLeadByte(c))
	{
		*code = ((uint32_t)c << 8) + (uint8_t)p[1];
		return 1;
	}
	*code = c;
	return 0;
}

// 0x00434C30: how many characters a string holds, their codes into `codes` when
// it is not NULL.
static uint32_t Text_CountCharacters(const char* p, uint32_t* codes)
{
	uint32_t n = 0;
	while(*p != 0)
	{
		uint32_t code;
		p += Text_Decode(p, &code) ? 2 : 1;
		if(codes != NULL)
			codes[n] = code;
		n++;
	}
	return n;
}

// Whether the character at `p` is one of those in `list` (0x00437B90, 0x00437BF0,
// 0x00437C50 on their three lists).
static int Text_InList(const char* p, const char* list)
{
	uint32_t code, other;
	Text_Decode(p, &code);
	while(*list != 0)
	{
		int wide = Text_Decode(list, &other);
		if(other == code)
			return 1;
		list += wide ? 2 : 1;
	}
	return 0;
}

// 0x004E5328: the characters 0x004355F0 takes to open a line of speech.
static const char gTextOpeners[] = "\x81\x75\x81\x77\x81\x69\x81\x67";
// 0x004E52D0: the characters a line must not start with (0x00437AF0).
static const char gTextNoLineStart[] =
	"\"':;?!\xDE\xDF\xA5\x81\x43\x81\x44\x81\x41\x81\x42\x81\x46\x81\x47\x81\x48\x81\x49\x81\x68"
	"\x81\x4A\x81\x4B\x81\x5D]})\x81\x6A\x81\x6C\x81\x6E\x81\x70\x81\x72\x81\xE1\x81\x74\x81\x76\x81\x78"
	"\x81\x7A\x81\x52\x81\x53\x81\x54\x81\x55\x81\x58\x81\x5B\x81\x60\x82\xC1\x82\xE1\x82\xE3\x82\xE5"
	"\x83\x62";
// 0x004E5338: of those, the ones that may hang past the margin instead (0x00437BF0).
static const char gTextHanging[] =
	"?!\xDE\xDF\x81\x43\x81\x44\x81\x41\x81\x42\x81\x48\x81\x49\x81\x68\x81\x4A\x81\x4B]})"
	"\x81\x6A\x81\x6C\x81\x6E\x81\x70\x81\x72\x81\xE1\x81\x74\x81\x76\x81\x78\x81\x7A\x81\x52\x81\x53"
	"\x81\x54\x81\x55\x81\x58\x81\x5B\x81\x60\x82\xC1\x82\xE1\x82\xE3\x82\xE5\x83\x62\x83\x83\x83\x85"
	"\x83\x87";
// 0x004E5384: the opening brackets a line must not end with (0x00437C50).
static const char gTextNoLineEnd[] =
	"[{(\x81\x69\x81\x6B\x81\x6D\x81\x6F\x81\x71\x81\xE1\x81\x73\x81\x75\x81\x77\x81\x79\x81\x67";

// 0x00437AF0: how many characters from `p` on may not start a line, copied into
// `out`.
static int Text_CountNoLineStart(const char* p, char* out)
{
	int n = 0;
	while(*p != 0 && Text_InList(p, gTextNoLineStart))
	{
		uint32_t code;
		int wide = Text_Decode(p, &code);
		for(int i = 0; i < (wide ? 2 : 1); i++)
			*out++ = *p++;
		n++;
	}
	*out = 0;
	return n;
}

// 0x00437CB0: the run of printable ASCII starting at `p`, copied into `out`, and
// its length.
static int Text_AsciiWord(const char* p, char* out)
{
	int n = 0;
	while((uint8_t)p[n] >= 0x21 && (uint8_t)p[n] <= 0x7E)
	{
		out[n] = p[n];
		n++;
	}
	out[n] = 0;
	return n;
}

// 0x00437CE0: character number `index` of `p` into `out` (one or two bytes and a
// terminator); 0 when the string is shorter.
static int Text_CharacterAt(const char* p, int index, char* out)
{
	int found = 0;
	for(int i = 0; i <= index && *p != 0; i++)
	{
		uint32_t code;
		int wide = Text_Decode(p, &code);
		if(i < index)
		{
			p += wide ? 2 : 1;
			continue;
		}
		out[0] = p[0];
		out[1] = wide ? p[1] : 0;
		if(wide)
			out[2] = 0;
		found = 1;
	}
	return found;
}

// ----------------------------------------------------------------------------
// Bitmaps
// ----------------------------------------------------------------------------

// 0x00409080 -> 0x00409030: a bitmap of its own, in the device's pixel mode - or,
// asked for alpha when that mode is 24-bit, 32-bit. Its pixels are left as they
// are; nothing when either side is 0.
static void Text_NewBitmap(Renderer_t* renderer, Bitmap_t* bitmap, int width, int height, int alpha)
{
	int mode = Renderer_ScreenMode(renderer);
	if(alpha && mode == BITMAP_MODE_24)
		mode = BITMAP_MODE_32;
	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->width = width;
	bitmap->height = height;
	bitmap->mode = mode;
	bitmap->stride = width * Renderer_ModePixelBytes(mode);
	bitmap->serial = 0xFFFFFFFFu;
	if(width != 0 && height != 0 && width > 0 && height > 0)
		bitmap->bitmap = (uint8_t*)malloc((size_t)bitmap->stride * (size_t)height);
}

static void Text_ClearBitmap(Bitmap_t* bitmap)
{
	if(bitmap->bitmap != NULL)
		Renderer_ClearBitmap(bitmap);
}

static void Text_FreeBitmap(Bitmap_t* bitmap)
{
	free(bitmap->bitmap);
	bitmap->bitmap = NULL;
}

static void Text_BlitAt(Bitmap_t* destination, int x, int y, Bitmap_t* source, int mode, int weight)
{
	if(destination->bitmap == NULL || source->bitmap == NULL || source->width <= 0 || source->height <= 0)
		return;
	Renderer_BlitAt(destination, x, y, source, mode, weight);
}

// 0x004092E0: a character's cell drawn into a bitmap in one colour, as much of it
// as fits: a 32-bit bitmap takes the coverage as its alpha, a 24-bit one 1 where
// any channel would be lit, a 16-bit one the colour scaled by the coverage.
static void Text_DrawCharacter(Bitmap_t* view, FontObject_t* font, uint32_t code, FontGlyphInfo_t* info, uint32_t colour)
{
	Font_GetGlyph(font, code, info);
	if(view->bitmap == NULL)
		return;
	int w = Font_CellWidth(font);
	int h = Font_CellHeight(font);
	int stride = Font_CellStride(font);
	if(h > view->height)
		h = view->height;
	if(w > view->width)
		w = view->width;
	uint8_t red = (uint8_t)(colour >> 16);
	uint8_t green = (uint8_t)(colour >> 8);
	uint8_t blue = (uint8_t)colour;
	for(int row = 0; row < h; row++)
	{
		const uint8_t* in = info->pixels + (size_t)row * (size_t)stride;
		uint8_t* line = view->bitmap + (size_t)row * (size_t)view->stride;
		for(int column = 0; column < w; column++)
		{
			uint32_t coverage = in[column];
			if(view->mode == BITMAP_MODE_32)
				((uint32_t*)line)[column] = (coverage << 24) | (colour & 0xFFFFFF);
			else if(view->mode == BITMAP_MODE_24)
				((uint32_t*)line)[column] = ((red * coverage) & 0xFFFF00) || ((green * coverage) & 0xFFFFFF00) ||
				                            ((blue * coverage) & 0xFFFFFF00);
			else if(view->mode == BITMAP_MODE_16)
				((uint16_t*)line)[column] = (uint16_t)(((((red * coverage) >> 1) & 0xFC1F) | ((green * coverage) >> 6)) & 0xFFE0) |
				                            (uint16_t)((blue * coverage) >> 11);
		}
	}
}

// 0x00407900: the weights 0x004094C0 grows an edge by for radii 1..5 when both
// radii are the same - per offset, 1.0 inside the circle of the radius, nothing a
// pixel or more outside it and, between, the distance past the radius (16.16).
static int32_t* gTextEdgeWeights[5];

static const int32_t* Text_EdgeWeights(int radius)
{
	int32_t** table = &gTextEdgeWeights[radius - 1];
	if(*table != NULL)
		return *table;
	int side = 2 * radius + 1;
	*table = (int32_t*)malloc(sizeof(int32_t) * (size_t)side * (size_t)side);
	if(*table == NULL)
		return NULL;
	for(int dy = -radius; dy <= radius; dy++)
	{
		for(int dx = -radius; dx <= radius; dx++)
		{
			double v = sqrt((double)(dx * dx + dy * dy)) - (double)radius;
			int32_t weight;
			if(v <= 0.0)
				weight = 0x10000;
			else if(v >= 1.0)
				weight = 0;
			else
				weight = (int32_t)(v * 65536.0);
			(*table)[(dy + radius) * side + (dx + radius)] = weight;
		}
	}
	return *table;
}

// 0x004094C0: a character's edge - its coverage summed over an ellipse of radii
// rx, ry about each pixel, the pixel (rx, ry) into the view being the cell's
// origin - drawn in `colour` into a 32-bit view.
static void Text_DrawCharacterEdge(Bitmap_t* view, FontObject_t* font, uint32_t code, int rx, int ry, uint32_t colour)
{
	FontGlyphInfo_t info;
	Font_GetGlyph(font, code, &info);
	if(view->mode != BITMAP_MODE_32 || view->bitmap == NULL)
		return;
	int cellWidth = Font_CellWidth(font);
	int cellHeight = Font_CellHeight(font);
	int stride = Font_CellStride(font);
	const int32_t* table = (rx == ry && ry <= 5 && rx <= 5 && rx > 0) ? Text_EdgeWeights(rx) : NULL;
	for(int j = 0; j < view->height; j++)
	{
		uint32_t* line = (uint32_t*)(view->bitmap + (size_t)j * (size_t)view->stride);
		for(int i = 0; i < view->width; i++)
		{
			uint32_t sum = 0;
			for(int dy = -ry; dy <= ry; dy++)
			{
				int sy = j - ry + dy;
				for(int dx = -rx; dx <= rx; dx++)
				{
					int sx = i - rx + dx;
					if(sx < 0 || sx >= cellWidth || sy < 0 || sy >= cellHeight)
						continue;
					uint32_t coverage = info.pixels[sy * stride + sx];
					if(table != NULL)
					{
						sum += (coverage * (uint32_t)table[(2 * ry + 1) * (dy + ry) + dx + rx]) >> 16;
						continue;
					}
					// 0x004095D2: the offset scaled onto a circle of radius ry.
					double sxr = (double)dx * (double)ry / (double)rx;
					double v = sqrt((double)(dy * dy) + sxr * sxr) - (double)ry;
					if(v <= 0.0)
						sum += coverage;
					else if(v < 1.0)
						sum += (uint32_t)(int64_t)((double)coverage * v);
				}
			}
			if(sum >= 0x100)
				sum = 0xFF;
			line[i] = (sum << 24) | colour;
		}
	}
}

// 0x004189A0: a 32-bit copy of a 32-bit view with every pixel's colour replaced.
static void Text_Recolour(Bitmap_t* destination, const Bitmap_t* source, uint32_t colour)
{
	if(source->mode != BITMAP_MODE_32 || destination->mode != BITMAP_MODE_32 || source->bitmap == NULL || destination->bitmap == NULL)
		return;
	for(int row = 0; row < source->height; row++)
	{
		const uint32_t* in = (const uint32_t*)(source->bitmap + (size_t)row * (size_t)source->stride);
		uint32_t* out = (uint32_t*)(destination->bitmap + (size_t)row * (size_t)destination->stride);
		for(int column = 0; column < source->width; column++)
			out[column] = (in[column] & 0xFF000000) | (colour & 0xFFFFFF);
	}
}

// 0x00409A80: a view narrowed to the columns a character's ink covers (its right
// edge moved by any effect first).
static void Text_NarrowToInk(Bitmap_t* view, const FontGlyphInfo_t* info)
{
	int width = info->right - info->left + 1;
	if((uint32_t)width > (uint32_t)view->width)
		width = view->width;
	view->width = width;
	view->bitmap += (size_t)Renderer_ModePixelBytes(view->mode) * (size_t)info->left;
}

// ----------------------------------------------------------------------------
// Fonts, as the layout sees them
// ----------------------------------------------------------------------------

// 0x00437160: a character cell's advance - size times width per cent.
static int32_t Text_CellAdvance(const FontEntryInfo_t* font)
{
	return (font->width * font->size) / 100;
}

// 0x00437180: the ruby's size for text of this size.
static int32_t Text_RubySize(int32_t size)
{
	int32_t ruby = gTextRubySize;
	if(ruby <= 0)
		ruby = (TEXT_RUBY_PERCENT * size) / 100;
	if(ruby < 4)
		ruby = 4;
	return ruby;
}

// 0x004370D0: what follows each character in a monospaced layout - the font's own
// spacing (the second of its pair for an italic font) and 0x00565BB0. A
// proportional layout adds nothing.
static int32_t Text_Spacing(const FontEntryInfo_t* font, uint32_t proportional)
{
	if(proportional)
		return 0;
	int32_t spacing = 0;
	if(font != NULL && font->font != NULL)
		Font_GetSpacing(font->font, font->italic != 0, &spacing);
	return gTextExtraSpacing + spacing;
}

// 0x00432F10 and 0x004330E0 / 0x00433110: an external character and its size.
static int Text_IsGaiji(uint32_t code)
{
	return code - 0xFF01 < 0xFF;
}

static int Text_GaijiWidth(uint32_t code)
{
	return Text_IsGaiji(code) ? gTextGaiji[code & 0xFF].bitmap.width : 0;
}

static int Text_GaijiHeight(uint32_t code)
{
	return Text_IsGaiji(code) ? gTextGaiji[code & 0xFF].bitmap.height : 0;
}

// 0x00433140: a character into a view - an external character copied, anything
// else drawn from its font - and what 0x0042E990 said of it.
static void Text_PutCharacter(Bitmap_t* view, uint32_t code, FontObject_t* font, uint32_t colour, FontGlyphInfo_t* info)
{
	if(!Text_IsGaiji(code))
	{
		Text_DrawCharacter(view, font, code, info, colour);
		return;
	}
	TextGaiji_t* gaiji = &gTextGaiji[code & 0xFF];
	if(gaiji->bitmap.bitmap != NULL && view->bitmap != NULL)
		Text_BlitAt(view, 0, 0, &gaiji->bitmap, gaiji->bitmap.mode == BITMAP_MODE_32 ? BITMAP_BLEND_ALPHA : BITMAP_BLEND_COPY, 0);
	info->code = code;
	info->doubleByte = 1;
	info->pixels = NULL;
	info->left = 0;
	info->top = 0;
	info->right = Text_GaijiWidth(code) - 1;
	info->bottom = Text_GaijiHeight(code) - 1;
}

// ----------------------------------------------------------------------------
// The ruby dictionary (0x004345E0 - 0x004349F1)
// ----------------------------------------------------------------------------

typedef struct TextWord TextWord_t;
struct TextWord
{
	char*     key;          // +0x00
	uint32_t  keySize;      // +0x04, bytes with the terminator
	uint32_t  keyCount;     // +0x08, characters
	char*     value;        // +0x0C
	uint32_t  valueSize;    // +0x10
	uint32_t* valueCodes;   // +0x14
	uint32_t  valueCount;   // +0x18
	uint32_t  local;        // +0x1C: from a tag or this call's own dictionary
	uint32_t  used;         // +0x20
	TextWord_t* next;       // +0x24
};

typedef struct TextDictionary
{
	TextWord_t* words;      // +0x24
} TextDictionary_t;

static char* Text_Duplicate(const char* s)
{
	size_t n = strlen(s) + 1;
	char* copy = (char*)malloc(n);
	if(copy != NULL)
		memcpy(copy, s, n);
	return copy;
}

static void Text_SetWordValue(TextWord_t* word, const char* value)
{
	word->valueSize = (uint32_t)strlen(value) + 1;
	word->valueCount = Text_CountCharacters(value, NULL);
	word->value = Text_Duplicate(value);
	word->valueCodes = (uint32_t*)malloc(sizeof(uint32_t) * (word->valueCount + 1));
	if(word->valueCodes != NULL)
		Text_CountCharacters(value, word->valueCodes);
}

// 0x00434600: a word and its reading. A dictionary word replaces the reading of
// the same word already there; a local one (a tag's, or this call's) goes after
// the other local ones, ahead of the dictionary's, which are kept longest first.
static void Text_AddWord(TextDictionary_t* dictionary, const char* key, const char* value, uint32_t local)
{
	if(!local)
	{
		for(TextWord_t* word = dictionary->words; word != NULL; word = word->next)
		{
			if(strcmp(word->key, key) == 0)
			{
				free(word->value);
				free(word->valueCodes);
				Text_SetWordValue(word, value);
				return;
			}
		}
	}
	uint32_t size = (uint32_t)strlen(key) + 1;
	TextWord_t** link = &dictionary->words;
	while(*link != NULL)
	{
		if(local ? (*link)->local == 0 : size >= (*link)->keySize)
			break;
		link = &(*link)->next;
	}
	TextWord_t* word = (TextWord_t*)calloc(1, sizeof(TextWord_t));
	if(word == NULL)
		return;
	word->keySize = size;
	word->keyCount = Text_CountCharacters(key, NULL);
	word->key = Text_Duplicate(key);
	Text_SetWordValue(word, value);
	word->local = local;
	word->used = 0;
	word->next = *link;
	*link = word;
}

// 0x00434B20: "word\reading" lines, each added as a local word. 1 when the whole
// string was taken.
static int Text_ParseDictionary(TextDictionary_t* dictionary, const char* s)
{
	if(s == NULL)
		return 0;
	while(*s != 0)
	{
		const char* slash = strstr(s, "\\");
		if(slash == NULL || slash - s <= 0)
			break;
		char key[0x100];
		char value[0x100];
		size_t keyLength = (size_t)(slash - s);
		if(keyLength >= sizeof(key))
			keyLength = sizeof(key) - 1;
		memcpy(key, s, keyLength);
		key[keyLength] = 0;
		s = slash + 1;
		const char* newline = strstr(s, "\n");
		size_t valueLength = newline != NULL ? (size_t)(newline - s) : strlen(s);
		if(valueLength == 0)
			break;
		if(valueLength >= sizeof(value))
			valueLength = sizeof(value) - 1;
		memcpy(value, s, valueLength);
		value[valueLength] = 0;
		s += valueLength + (newline != NULL ? 1 : 0);
		// 0x00434C01 -> 0x004345E0: added as a dictionary word, not a local one, so
		// it gives its reading to every place the word appears.
		Text_AddWord(dictionary, key, value, 0);
	}
	return *s == 0;
}

static void Text_FreeWord(TextWord_t* word)
{
	free(word->key);
	free(word->value);
	free(word->valueCodes);
	free(word);
}

// 0x00434810: the word `key` taken out - only a local one when `localOnly`.
static int Text_RemoveWord(TextDictionary_t* dictionary, const char* key, int localOnly)
{
	for(TextWord_t** link = &dictionary->words; *link != NULL; link = &(*link)->next)
	{
		TextWord_t* word = *link;
		if(localOnly && word->local == 0)
			continue;
		if(strcmp(word->key, key) != 0)
			continue;
		*link = word->next;
		Text_FreeWord(word);
		return 1;
	}
	return 0;
}

// 0x004348B0: every local word taken out.
static void Text_RemoveLocalWords(TextDictionary_t* dictionary)
{
	TextWord_t** link = &dictionary->words;
	while(*link != NULL)
	{
		if((*link)->local)
		{
			TextWord_t* word = *link;
			*link = word->next;
			Text_FreeWord(word);
		}
		else
			link = &(*link)->next;
	}
}

// 0x004348F0: the dictionary emptied.
static void Text_ClearDictionary(TextDictionary_t* dictionary)
{
	while(dictionary->words != NULL)
	{
		TextWord_t* word = dictionary->words;
		dictionary->words = word->next;
		Text_FreeWord(word);
	}
}

// 0x004349A0: the first unused word the text at `p` starts with, into `out`; a
// local word is used up by being found.
static int Text_FindWordAt(TextDictionary_t* dictionary, const char* p, char* out)
{
	for(TextWord_t* word = dictionary->words; word != NULL; word = word->next)
	{
		if(strncmp(p, word->key, word->keySize - 1) != 0 || word->used != 0)
			continue;
		strcpy(out, word->key);
		if(word->local)
			word->used++;
		return 1;
	}
	return 0;
}

// 0x00434920: the word `key`.
static TextWord_t* Text_FindWord(TextDictionary_t* dictionary, const char* key)
{
	for(TextWord_t* word = dictionary->words; word != NULL; word = word->next)
		if(strcmp(word->key, key) == 0)
			return word;
	return NULL;
}

// ----------------------------------------------------------------------------
// Measuring (0x00434FE0)
// ----------------------------------------------------------------------------

// A string's width set as the layout would set it: the whole width, and the two
// the ruby places itself by.
static void Text_Measure(Renderer_t* renderer, int32_t out[3], const char* s, const FontEntryInfo_t* font, uint32_t proportional)
{
	Bitmap_t scratch;
	Text_NewBitmap(renderer, &scratch, font->size * 2, font->size, 1);
	int32_t cell = Text_CellAdvance(font);
	int32_t total = 0;
	int32_t gap = 0;
	int32_t firstGap = 0;
	int first = 1;
	while(*s != 0)
	{
		Bitmap_t view = scratch;
		uint32_t code;
		int wide = Text_Decode(s, &code);
		FontGlyphInfo_t info;
		Text_PutCharacter(&view, code, font->font, 0xFFFFFF, &info);
		if(proportional)
		{
			Text_NarrowToInk(&view, &info);
			int gaiji = Text_IsGaiji(code);
			gap = gaiji ? 0 : (int32_t)((cell * TEXT_GAP_FACTOR) >> 16);
			int32_t width = gaiji ? Text_GaijiWidth(code) : view.width;
			total += width + gap;
			if(first)
			{
				firstGap = gap >> 1;
				first = 0;
			}
		}
		else
		{
			gap = 0;
			int32_t width = Text_IsGaiji(code) ? Text_GaijiWidth(code) : wide ? cell : cell / 2;
			total += Text_Spacing(font, 0) + width;
		}
		s += wide ? 2 : 1;
	}
	Text_FreeBitmap(&scratch);
	int32_t width = total - Text_Spacing(font, proportional);
	out[0] = width;
	out[1] = (gap >> 1) - firstGap - gap + width;
	out[2] = (gap >> 1) - gap + width;
}

// ----------------------------------------------------------------------------
// The layout (0x00435370)
// ----------------------------------------------------------------------------

typedef struct TextLayout
{
	TextRecord_t* records;  // +0x44
} TextLayout_t;

static void Text_Append(TextRecord_t** tail, TextRecord_t* record)
{
	(*tail)->next = record;
	*tail = record;
}

// 0x00437F20: a link's text and where it started, while there is room.
static int Text_AddLink(const char* text, int32_t x, int32_t y)
{
	if(gTextLinkCount >= 16)
		return 0;
	TextLink_t* link = &gTextLinks[gTextLinkCount];
	memset(link->text, 0, sizeof(link->text));
	size_t n = strlen(text);
	if(n >= 0x60)
		n = 0x5F;
	memcpy(link->text, text, n);
	gTextLinkCount++;
	link->x = x;
	link->y = y;
	return 1;
}

// The font the layout is currently drawing with: the call's own, or a copy of it
// that a tag has given a font object of its own.
typedef struct TextFontState
{
	FontEntryInfo_t base;
	FontEntryInfo_t copy;
	FontEntryInfo_t* current;
	int32_t spacing[2];
	int32_t adjust[4];
} TextFontState_t;

static void Text_ReplaceCopyFont(TextFontState_t* fonts, FontObject_t* font)
{
	if(fonts->copy.font != NULL)
		Font_DeleteObject(fonts->copy.font);
	fonts->copy.font = font;
	if(font != NULL)
		Font_SetSpacing(font, fonts->spacing[0], fonts->spacing[1]);
}

// The copy's font made again with a different weight or slant (the <b>, </b>,
// <i> and </i> arms). 0 when the font could not be made, which leaves everything
// as it was.
static int Text_RemakeCopy(TextFontState_t* fonts, int bold, int italic)
{
	FontObject_t* font = Font_NewObject();
	if(font == NULL)
		return 0;
	if(Font_CreateObject(font, fonts->copy.name, fonts->copy.size, fonts->copy.width, bold, italic,
	                     fonts->adjust, 0x40, 1) != 0)
	{
		Font_DeleteObject(font);
		return 0;
	}
	Text_ReplaceCopyFont(fonts, font);
	return 1;
}

static int Text_CopyIsBase(const TextFontState_t* fonts, int compareBold)
{
	return strcmp(fonts->copy.name, fonts->base.name) == 0 &&
	       fonts->copy.size == fonts->base.size &&
	       fonts->copy.width == fonts->base.width &&
	       (compareBold ? fonts->copy.bold == fonts->base.bold : fonts->copy.italic == fonts->base.italic);
}

// Skips the spaces a tag's arguments start with.
static const char* Text_SkipSpaces(const char* p)
{
	while(*p == ' ')
		p++;
	return p;
}

static uint32_t Text_Layout(Renderer_t* renderer, TextLayout_t* layout, const char* text, uint32_t ruby,
                            int32_t* cursor, const Rect_t* rect, int32_t lineHeight, uint32_t fontId,
                            uint32_t proportional, uint32_t kinsoku, uint32_t colour, const TextStyle_t* style,
                            uint32_t* lines, TextDictionary_t* dictionary)
{
	TextFontState_t fonts;
	memset(&fonts, 0, sizeof(fonts));
	if(!Font_GetInfo(fontId, &fonts.base))
		return 0;
	// 0x004353FE: the links of the last layout forgotten.
	gTextLinkCount = 0;
	uint32_t linkColour = gTextLinkColour;

	// 0x0043540F: a control character in front of the text: 0x02 underlines it,
	// 0x03 turns off the hanging indent, 0x04..0x08 name the opening character the
	// indent is measured from - 「, the ideographic space, （, “ and 『.
	int underline = 0;
	int noIndent = 0;
	int haveOpener = 0;
	uint32_t opener = 0;
	uint8_t first = (uint8_t)text[0];
	if(first < 0x20 && first >= 2 && first <= 8)
	{
		static const char* openers[5] = { "\x81\x75", "\x81\x40", "\x81\x69", "\x81\x67", "\x81\x77" };
		if(first == 2)
			underline = 1;
		else if(first == 3)
			noIndent = 1;
		else
		{
			Text_Decode(openers[first - 4], &opener);
			haveOpener = 1;
		}
		text++;
	}

	int32_t size = fonts.base.size;
	int32_t cell = Text_CellAdvance(&fonts.base);
	// 0x00435491: with ruby on, every line is pushed down to make room for it.
	int32_t rubyDrop = 0;
	if(ruby)
		rubyDrop = Text_RubySize(size) - (gFunctionParameters[3] != 0 ? gTextRubyOffsetY : 0);
	int32_t effectX = (style->a * size) / 100;
	if(effectX <= 0)
		effectX = 1;
	int32_t effectY = (style->b * size) / 100;
	if(effectY <= 0)
		effectY = 1;
	int32_t extraX = 0, extraY = 0;
	if(style->kind == 1)
	{
		extraX = effectX;
		extraY = effectY;
	}
	else if(style->kind == 2)
	{
		extraX = effectX * 2;
		extraY = effectY * 2;
		if(gFunctionParameters[3] != 0)
			rubyDrop += effectY;
	}

	FontObject_t* baseFont = fonts.base.font;
	Bitmap_t scratch;
	Text_NewBitmap(renderer, &scratch, Font_CellWidth(baseFont), Font_CellHeight(baseFont), 1);
	char* wordBuffer = (char*)malloc(strlen(text) + 1);
	if(wordBuffer == NULL)
	{
		Text_FreeBitmap(&scratch);
		return 0;
	}

	// 0x004355C1: kinsoku keeps a character's width in hand at the right margin,
	// and a text that opens with a bracket may indent its following lines past it.
	int32_t indent = 0;
	int32_t reserve = 0;
	if(kinsoku)
	{
		reserve = size;
		if(gTextHangingIndent != 0 && !noIndent && (Text_InList(text, gTextOpeners) || haveOpener))
		{
			char openerText[3] = { 0, 0, 0 };
			if(haveOpener)
			{
				openerText[0] = (char)(opener >> 8);
				openerText[1] = (char)opener;
			}
			else
			{
				uint32_t code;
				int wide = Text_Decode(text, &code);
				openerText[0] = text[0];
				openerText[1] = wide ? text[1] : 0;
			}
			int32_t measured[3];
			Text_Measure(renderer, measured, openerText, &fonts.base, proportional);
			indent = Text_Spacing(&fonts.base, proportional) + measured[0];
		}
	}
	if(ruby)
	{
		if(cursor[0] == rect->left)
			cursor[0] += gTextRubyIndent;
		indent += gTextRubyIndent;
	}

	fonts.copy = fonts.base;
	fonts.copy.font = NULL;
	fonts.current = &fonts.base;
	Font_GetAdjust(baseFont, fonts.adjust);
	Font_GetSpacing(baseFont, 0, &fonts.spacing[0]);
	Font_GetSpacing(baseFont, 1, &fonts.spacing[1]);

	int32_t index = 0;
	int32_t delay = 0;
	int tagsOff = 0;
	int lineStart = 1;
	uint32_t* colourStack = NULL;
	uint32_t colourDepth = 0;
	const char* linkStart = NULL;
	const char* linkBreak = NULL;
	int32_t linkX = 0, linkY = 0;
	uint32_t eventIndex = 0;
	int32_t wordSkip = 0;
	int32_t rubySkip = 0;
	int32_t kinsokuSkip = 0;
	uint32_t hangCode = 0;
	*lines = 1;

	TextRecord_t** append = &layout->records;

	while(text[index] != 0)
	{
		const char* here = text + index;
		uint8_t c = (uint8_t)*here;
		if(c < 0x20)
		{
			// 0x00435777: a newline starts a line; any other control character is
			// passed over.
			if(c == '\n')
			{
				cursor[0] = rect->left + indent;
				cursor[1] += lineHeight;
				(*lines)++;
				lineStart = 1;
			}
			index++;
			continue;
		}

		if(c == '<' && here[1] != 0)
		{
			int length = 1;
			while(here[length] != '>' && here[length] != 0)
				length++;
			if(length > 1 && here[length] == '>')
			{
				if(tagsOff)
					// 0x00436625: after </> the next tag is printed as it stands, and
					// tags are on again from there.
					tagsOff = 0;
				else
				{
					const char* after = here + length + 1;
					char tag[0x200];
					int n = length - 1;
					if(n >= (int)sizeof(tag))
						n = (int)sizeof(tag) - 1;
					memcpy(tag, here + 1, (size_t)n);
					tag[n] = 0;
					// 0x0042EB60: ASCII letters lowered, two-byte characters left alone.
					for(char* p = tag; *p != 0; p++)
					{
						if(Text_IsLeadByte((uint8_t)*p))
						{
							if(p[1] == 0)
								break;
							p++;
							continue;
						}
						if(*p >= 'A' && *p <= 'Z')
							*p += 0x20;
					}
					static const char* names[15] = { "/", "b", "/b", "i", "/i", "ruby", "r", "/r", "cr", "c", "/c", "l", "/l", "t", "ev" };
					int which = -1;
					for(int i = 0; i < 15 && which < 0; i++)
					{
						if(strcmp(tag, names[i]) == 0 || (i != 0 && strncmp(tag, names[i], strlen(names[i])) == 0))
							which = i;
					}
					const char* args = which >= 0 ? tag + strlen(names[which]) : tag;
					char first[0x200];
					char second[0x200];
					switch(which)
					{
						case 0:
							tagsOff = 1;
							break;
						case 1:
							if(!fonts.copy.bold && Text_RemakeCopy(&fonts, 1, fonts.copy.italic))
							{
								fonts.current = &fonts.copy;
								fonts.copy.bold = 1;
							}
							break;
						case 2:
							if(fonts.copy.bold)
							{
								if(Text_CopyIsBase(&fonts, 0))
								{
									Text_ReplaceCopyFont(&fonts, NULL);
									fonts.current = &fonts.base;
									fonts.copy.bold = 0;
								}
								else if(Text_RemakeCopy(&fonts, 0, fonts.copy.italic))
									fonts.copy.bold = 0;
							}
							break;
						case 3:
							if(!fonts.copy.italic && Text_RemakeCopy(&fonts, fonts.copy.bold, 1))
							{
								fonts.current = &fonts.copy;
								fonts.copy.italic = 1;
							}
							break;
						case 4:
							if(fonts.copy.italic)
							{
								if(Text_CopyIsBase(&fonts, 1))
								{
									Text_ReplaceCopyFont(&fonts, NULL);
									fonts.current = &fonts.base;
									fonts.copy.italic = 0;
								}
								else if(Text_RemakeCopy(&fonts, fonts.copy.bold, 0))
									fonts.copy.italic = 0;
							}
							break;
						case 5:
						{
							// 0x00435F0A: <ruby word,reading> - the reading given to the
							// next time the word appears.
							const char* p = Text_SkipSpaces(args);
							char* out = first;
							int ok = 0;
							while(*p != 0)
							{
								uint32_t code;
								int wide = Text_Decode(p, &code);
								if(!wide && *p == ',')
								{
									ok = 1;
									p++;
									break;
								}
								*out++ = *p++;
								if(wide)
									*out++ = *p++;
							}
							*out = 0;
							if(!ok)
								break;
							out = second;
							while(*p != 0)
								*out++ = *p++;
							*out = 0;
							Text_AddWord(dictionary, first, second, 1);
							break;
						}
						case 6:
						{
							// 0x00435FF7: <r reading>word</r> - the reading given to the
							// text up to the next tag.
							const char* p = Text_SkipSpaces(args);
							strcpy(second, p);
							if(second[0] == 0)
								break;
							char* out = first;
							const char* q = after;
							int ended = 0;
							while(*q != 0)
							{
								uint32_t code;
								int wide = Text_Decode(q, &code);
								if(!wide && *q == '<')
								{
									ended = 1;
									break;
								}
								*out++ = *q++;
								if(wide)
									*out++ = *q++;
							}
							*out = 0;
							if(ended && first[0] != 0)
								Text_AddWord(dictionary, first, second, 1);
							break;
						}
						case 8:
							// 0x0043610E: back to the left edge, on the same line.
							cursor[0] = rect->left;
							break;
						case 9:
						{
							// 0x0043612E: <c rrggbb> - exactly six hex digits.
							const char* p = Text_SkipSpaces(args);
							uint32_t value = 0;
							int digits = 0;
							while(digits < 6)
							{
								const char* at = strchr("0123456789abcdef", p[digits]);
								if(at == NULL || p[digits] == 0)
									break;
								value = (value << 4) | (uint32_t)(at - "0123456789abcdef");
								digits++;
							}
							if(digits != 6)
								break;
							uint32_t* grown = (uint32_t*)realloc(colourStack, sizeof(uint32_t) * (colourDepth + 1));
							if(grown == NULL)
								break;
							colourStack = grown;
							colourStack[colourDepth++] = colour;
							colour = value;
							break;
						}
						case 10:
							if(colourDepth != 0)
								colour = colourStack[--colourDepth];
							break;
						case 11:
						{
							// 0x00436214: <l> - a link: its colour and font until </l>.
							if(linkStart != NULL)
								break;
							linkX = cursor[0];
							linkY = cursor[1];
							linkStart = after;
							uint32_t* grown = (uint32_t*)realloc(colourStack, sizeof(uint32_t) * (colourDepth + 1));
							if(grown != NULL)
							{
								colourStack = grown;
								colourStack[colourDepth++] = colour;
							}
							if(linkColour != 0xFFFFFFFF)
								colour = linkColour;
							if(fonts.copy.bold || fonts.copy.italic)
								break;
							const char* name = gTextLinkFont[0] != 0 ? gTextLinkFont : fonts.copy.name;
							int32_t linkSize = gTextLinkSize > 0 ? gTextLinkSize : fonts.copy.size;
							int32_t linkWidth = gTextLinkWidth > 0 ? gTextLinkWidth : fonts.copy.width;
							FontObject_t* font = Font_NewObject();
							if(font == NULL)
								break;
							// The adjust values are the link font's own (0x00407BE0).
							int32_t storage[4];
							const int32_t* adjust = NULL;
							for(FontAdjust_t* entry = gFontAdjusts; entry != NULL; entry = entry->next)
							{
								if(strcmp(entry->name, name) == 0)
								{
									storage[0] = (int32_t)entry->scaleX;
									storage[1] = (int32_t)entry->scaleY;
									storage[2] = entry->originX;
									storage[3] = entry->originY;
									adjust = storage;
									break;
								}
							}
							if(Font_CreateObject(font, name, linkSize, linkWidth, (int)gTextLinkBold, (int)gTextLinkItalic, adjust, 0x40, 1) != 0)
							{
								Font_DeleteObject(font);
								break;
							}
							Text_ReplaceCopyFont(&fonts, font);
							fonts.current = &fonts.copy;
							break;
						}
						case 12:
						{
							// 0x004363C0: </l> - the link recorded, its colour and font
							// dropped.
							if(linkStart == NULL)
								break;
							int32_t n;
							if(linkBreak != NULL)
							{
								n = (int32_t)(linkBreak - linkStart);
								linkBreak = NULL;
							}
							else
								n = (int32_t)(here - linkStart);
							if(n != 0)
							{
								char linkText[0x60];
								int32_t copy = (uint32_t)n < 0x60 ? n : 0x5F;
								memcpy(linkText, linkStart, (size_t)copy);
								linkText[copy] = 0;
								linkStart = NULL;
								Text_AddLink(linkText, linkX, linkY);
							}
							if(colourDepth != 0)
								colour = colourStack[--colourDepth];
							if(fonts.copy.bold || fonts.copy.italic)
								break;
							Text_ReplaceCopyFont(&fonts, NULL);
							fonts.current = &fonts.base;
							break;
						}
						case 13:
						{
							// 0x004364C0: <t n> - the characters that follow wait n steps.
							const char* p = Text_SkipSpaces(args);
							char digits[0x40];
							int n = 0;
							while(p[n] >= '0' && p[n] <= '9' && n < (int)sizeof(digits) - 1)
							{
								digits[n] = p[n];
								n++;
							}
							digits[n] = 0;
							delay = atoi(digits) * TEXT_DELAY_STEP;
							break;
						}
						case 14:
						{
							// 0x00436541: <ev n> - an event record, fired when the reveal
							// reaches it.
							const char* p = Text_SkipSpaces(args);
							char digits[0x40];
							int n = 0;
							while(p[n] >= '0' && p[n] <= '9' && n < (int)sizeof(digits) - 1)
							{
								digits[n] = p[n];
								n++;
							}
							digits[n] = 0;
							TextRecord_t* record = (TextRecord_t*)calloc(1, sizeof(TextRecord_t));
							if(record == NULL)
								break;
							record->x = (int32_t)eventIndex++;
							record->delay = (uint32_t)delay;
							record->kind = 0x80000000u;
							record->y = n >= 1 ? atoi(digits) : -1;
							*append = record;
							append = &record->next;
							break;
						}
						default:
							break;
					}
					index += length + 1;
					continue;
				}
			}
		}

		// 0x0043662C: a character.
		if(kinsoku)
		{
			// A space at the right margin, after something that is not a space,
			// ends the line instead of being set.
			uint32_t code;
			int wide = Text_Decode(here, &code);
			const char* space = wide ? "\x81\x40" : " ";
			int bytes = wide ? 2 : 1;
			int32_t width = wide ? Text_CellAdvance(fonts.current) : Text_CellAdvance(fonts.current) >> 1;
			if(memcmp(here, space, (size_t)bytes) == 0 && index >= bytes &&
			   memcmp(here - bytes, space, (size_t)bytes) != 0)
			{
				if(cursor[0] + width > rect->right - fonts.base.size + 1)
				{
					cursor[0] = rect->left + indent;
					cursor[1] += lineHeight;
					(*lines)++;
					lineStart = 1;
					index += bytes;
					continue;
				}
			}
		}

		uint32_t code;
		int wide = Text_Decode(here, &code);
		int gaiji = Text_IsGaiji(code);
		Bitmap_t gaijiBitmap;
		memset(&gaijiBitmap, 0, sizeof(gaijiBitmap));
		Bitmap_t view;
		if(gaiji)
		{
			if(Text_GaijiWidth(code) == 0)
			{
				index += wide ? 2 : 1;
				continue;
			}
			Text_NewBitmap(renderer, &gaijiBitmap, Text_GaijiWidth(code) + extraX, Text_GaijiHeight(code) + extraY, 1);
			view = gaijiBitmap;
		}
		else
			view = scratch;
		Text_ClearBitmap(&view);
		FontGlyphInfo_t info;
		Text_PutCharacter(&view, code, fonts.current->font, colour, &info);

		if(underline || linkStart != NULL)
		{
			// 0x004368BB: a line along the bottom of the size, the width of the
			// character, in the text's colour.
			Rect_t line;
			line.left = 0;
			if(proportional)
				line.right = view.width - 1;
			else if(wide)
				line.right = cell;
			else
				line.right = cell / 2;
			line.top = size - 1;
			line.bottom = size - 1;
			Bitmap_t part = view;
			if(Renderer_ClipBitmap(&part, &line))
				Renderer_FillSolid(&part, colour | 0xFF000000u);
		}

		int32_t advance, gap;
		if(proportional)
		{
			info.right += extraX;
			Text_NarrowToInk(&view, &info);
			advance = gaiji ? Text_GaijiWidth(code) : view.width - extraX;
			gap = gaiji ? 0 : (int32_t)((cell * TEXT_GAP_FACTOR) >> 16);
		}
		else
		{
			advance = gaiji ? Text_GaijiWidth(code) : wide ? cell : cell / 2;
			gap = 0;
		}

		TextRecord_t* record = (TextRecord_t*)calloc(1, sizeof(TextRecord_t));
		if(record == NULL)
			break;
		record->delay = (uint32_t)delay;
		record->fadeLength = TEXT_FADE_LENGTH;
		Text_NewBitmap(renderer, &record->bitmap, view.width, view.height, 1);
		Text_ClearBitmap(&record->bitmap);
		if(style->kind == 2)
		{
			// 0x00436A2B: the edge, grown by the effect's offsets, under the
			// character, which sits those offsets in.
			Bitmap_t edge;
			Text_NewBitmap(renderer, &edge, scratch.width + extraX, scratch.height + extraY, 1);
			Text_ClearBitmap(&edge);
			Text_DrawCharacterEdge(&edge, fonts.current->font, code, effectX, effectY, style->colour);
			Bitmap_t edgeView = edge;
			if(proportional)
				Text_NarrowToInk(&edgeView, &info);
			Text_BlitAt(&record->bitmap, 0, 0, &edgeView, BITMAP_BLEND_ALPHA_TRANS, 0x100 - (int)style->weight);
			Text_BlitAt(&record->bitmap, effectX, effectY, &view, BITMAP_BLEND_ALPHA, 0);
			Text_FreeBitmap(&edge);
		}
		else if(style->kind == 1)
		{
			// 0x00436B4C: the shadow, the character's own shape in the effect's
			// colour, the offsets down and right of it.
			Bitmap_t shadow;
			Text_NewBitmap(renderer, &shadow, view.width, view.height, 1);
			Text_ClearBitmap(&shadow);
			Text_Recolour(&shadow, &view, style->colour);
			Text_BlitAt(&record->bitmap, effectX, effectY, &shadow, BITMAP_BLEND_ALPHA_TRANS, 0x100 - (int)style->weight);
			Text_FreeBitmap(&shadow);
			Text_BlitAt(&record->bitmap, 0, 0, &view, BITMAP_BLEND_ALPHA, 0);
		}
		else if(style->kind == 0)
			Text_BlitAt(&record->bitmap, 0, 0, &view, BITMAP_BLEND_ALPHA, 0);
		Text_FreeBitmap(&gaijiBitmap);

		// 0x00436C03: what this character takes across, for the margin test: its
		// advance, its gap and the spacing after it.
		int32_t step = advance + gap;
		int32_t halfGap = gap >> 1;
		int32_t reach = Text_Spacing(fonts.current, proportional) + step - halfGap;
		int hang = 0;
		const char* next = here + (wide ? 2 : 1);
		const char* skipEnd = next;
		int32_t skipCount = 0;

		// 0x00436C71: a word of ASCII is kept whole: at its first character the
		// margin test is made for all of it.
		int word = Text_AsciiWord(here, wordBuffer);
		if(word >= 1)
		{
			if(wordSkip > 0)
				wordSkip--;
			else if(!lineStart && word >= 2)
			{
				int32_t measured[3];
				Text_Measure(renderer, measured, wordBuffer, fonts.current, proportional);
				if(reach < measured[0])
					reach = measured[0];
				wordSkip = word - 1;
				skipCount = word - 1;
				skipEnd = here + word;
			}
		}
		else
			lineStart = 0;

		if(ruby)
		{
			// 0x00436CEB: a dictionary word starting here is kept whole too, and
			// remembered on the record for the ruby pass.
			if(rubySkip > 0)
				rubySkip--;
			else
			{
				char key[0x200];
				if(Text_FindWordAt(dictionary, here, key))
				{
					int32_t measured[3];
					Text_Measure(renderer, measured, key, fonts.current, proportional);
					if(reach < measured[0])
						reach = measured[0];
					record->rubyKey = Text_Duplicate(key);
					record->rubyWidth = measured[1];
					int32_t count = (int32_t)Text_CountCharacters(key, NULL) - 1;
					rubySkip = count;
					wordSkip = count;
					skipCount = count;
					skipEnd = here + strlen(key);
				}
			}
		}

		if(kinsoku)
		{
			// 0x00436DE7: the characters that may not start a line are kept with
			// the one before them; an opening bracket with the one after it.
			if(kinsokuSkip > 0)
				kinsokuSkip--;
			else
			{
				char held[0x200];
				int n = Text_CountNoLineStart(skipEnd, held);
				if(n > 0)
				{
					int32_t measured[3];
					Text_Measure(renderer, measured, held, fonts.current, proportional);
					reach += measured[0];
					char last[3];
					Text_CharacterAt(held, n - 1, last);
					kinsokuSkip = n + skipCount;
					if(gFunctionParameters[0] != 0)
					{
						hang = Text_InList(last, gTextHanging);
						if(hang)
							Text_Decode(last, &hangCode);
					}
					else
						hang = 0;
				}
				else if(Text_InList(here, gTextNoLineEnd) && *next != 0)
				{
					char following[3];
					uint32_t nextCode;
					int nextWide = Text_Decode(next, &nextCode);
					following[0] = next[0];
					following[1] = nextWide ? next[1] : 0;
					following[2] = 0;
					int32_t measured[3];
					Text_Measure(renderer, measured, following, fonts.current, proportional);
					reach += measured[0];
				}
			}
		}

		// 0x00436F2B: past the right margin - less the kinsoku reserve unless the
		// held characters may hang - the character goes to the next line. A space
		// never does, nor the hanging character the test above named (0x004371B3).
		int32_t margin = rect->right - (hang ? 0 : reserve) + 1;
		if(cursor[0] + reach > margin)
		{
			if(code == 0x20)
				;
			else if(code == hangCode)
				hangCode = 0;
			else
			{
				if(linkStart != NULL)
					linkBreak = here;
				cursor[0] = rect->left + indent;
				cursor[1] += lineHeight;
				(*lines)++;
				lineStart = 1;
			}
		}

		record->x = cursor[0] + halfGap;
		record->y = cursor[1] + rubyDrop;
		record->x0 = record->x;
		record->y0 = record->y;
		cursor[0] += Text_Spacing(fonts.current, proportional) + step;
		delay += TEXT_DELAY_STEP;
		index += wide ? 2 : 1;
		*append = record;
		append = &record->next;
	}

	free(colourStack);
	Text_ReplaceCopyFont(&fonts, NULL);
	free(wordBuffer);
	Text_FreeBitmap(&scratch);
	return 1;
}

// 0x00437380: one word's ruby, a record per character of the reading, spread over
// the word's width and put in the list after the word's first character.
static void Text_LayoutRubyWord(Renderer_t* renderer, TextRecord_t* record, TextWord_t* word, int32_t rubySize,
                                const FontEntryInfo_t* rubyFont, uint32_t colour, const TextStyle_t* style,
                                int32_t x, int32_t y, int32_t baseWidth, int32_t delay)
{
	int32_t advance = (rubyFont->width * rubySize) / 100;
	int32_t effectX = (style->a * rubySize) / 100;
	if(effectX <= 0)
		effectX = 1;
	int32_t effectY = (style->b * rubySize) / 100;
	if(effectY <= 0)
		effectY = 1;
	int32_t extraX = 0, extraY = 0;
	if(style->kind == 1)
	{
		extraX = effectX;
		extraY = effectY;
	}
	else if(style->kind == 2)
	{
		extraX = effectX * 2;
		extraY = effectY * 2;
	}
	int32_t count = (int32_t)word->valueCount;
	if(count <= 0)
		return;
	int32_t pitch = baseWidth / count;
	if(pitch < advance)
		pitch = advance;
	x += (rubySize >> 3) + ((baseWidth - (count - 1) * pitch - advance) >> 1);
	if(style->kind == 2)
	{
		x -= effectX;
		y -= effectY;
	}
	uint32_t step = (word->keyCount * TEXT_DELAY_STEP) / (uint32_t)count;
	int32_t at = (int32_t)(step >> 1) + delay;

	Bitmap_t cell;
	Text_NewBitmap(renderer, &cell, rubySize * 3, rubySize * 2, 1);
	int32_t height = extraY + rubySize;
	TextRecord_t* first = NULL;
	TextRecord_t* last = NULL;
	for(int32_t i = 0; i < count; i++)
	{
		TextRecord_t* ruby = (TextRecord_t*)calloc(1, sizeof(TextRecord_t));
		if(ruby == NULL)
			break;
		ruby->fadeLength = TEXT_FADE_LENGTH;
		ruby->delay = (uint32_t)at;
		ruby->y = y;
		ruby->x = x;
		ruby->kind = 2;
		Text_ClearBitmap(&cell);
		uint32_t code = word->valueCodes[i];
		FontGlyphInfo_t info;
		Text_DrawCharacter(&cell, rubyFont->font, code, &info, colour);
		int32_t width = extraX + (code < 0x100 ? advance / 2 : advance);
		Text_NewBitmap(renderer, &ruby->bitmap, width, height, 1);
		Text_ClearBitmap(&ruby->bitmap);
		if(style->kind == 2)
		{
			Bitmap_t edge;
			Text_NewBitmap(renderer, &edge, width, height, 1);
			Text_ClearBitmap(&edge);
			Text_DrawCharacterEdge(&edge, rubyFont->font, code, effectX, effectY, style->colour);
			Text_BlitAt(&ruby->bitmap, 0, 0, &edge, BITMAP_BLEND_ALPHA_TRANS, 0x100 - (int)style->weight);
			Text_FreeBitmap(&edge);
			Text_BlitAt(&ruby->bitmap, effectX, effectY, &cell, BITMAP_BLEND_ALPHA, 0);
		}
		else if(style->kind == 1)
		{
			Bitmap_t shadow;
			Text_NewBitmap(renderer, &shadow, cell.width, cell.height, 1);
			Text_ClearBitmap(&shadow);
			if(style->colour == 0)
				// 0x00437611: a black shadow is the character darkened all the way.
				Renderer_BlitView(&shadow, &cell, BITMAP_BLEND_FADE_BLACK, 0x100);
			else
			{
				FontGlyphInfo_t other;
				Text_DrawCharacter(&shadow, rubyFont->font, code, &other, style->colour);
			}
			Text_BlitAt(&ruby->bitmap, effectX, effectY, &shadow, BITMAP_BLEND_ALPHA_TRANS, 0x100 - (int)style->weight);
			Text_FreeBitmap(&shadow);
			Text_BlitAt(&ruby->bitmap, 0, 0, &cell, BITMAP_BLEND_ALPHA, 0);
		}
		else if(style->kind == 0)
			Text_BlitAt(&ruby->bitmap, 0, 0, &cell, BITMAP_BLEND_ALPHA, 0);
		at += (int32_t)step;
		x += pitch;
		if(last != NULL)
			last->next = ruby;
		else
			first = ruby;
		last = ruby;
	}
	if(first != NULL)
	{
		last->next = record->next;
		record->next = first;
	}
	Text_FreeBitmap(&cell);
}

// 0x004371F0: the ruby of every record that starts a dictionary word.
static int Text_LayoutRuby(Renderer_t* renderer, TextLayout_t* layout, uint32_t fontId, const TextStyle_t* style,
                           uint32_t rubyColour, TextDictionary_t* dictionary)
{
	FontEntryInfo_t font;
	if(!Font_GetInfo(fontId, &font))
		return 0;
	int32_t rubySize = Text_RubySize(font.size);
	const char* name = gTextRubyFont[0] != 0 ? gTextRubyFont : font.name;
	int32_t width = gTextRubyWidth > 0 ? gTextRubyWidth : font.width;
	uint32_t colour = gTextRubyColour != 0xFFFFFFFF ? gTextRubyColour : rubyColour;
	TextStyle_t rubyStyle = *style;
	if(gTextRubyEdgeColour != 0xFFFFFFFF)
		rubyStyle.colour = gTextRubyEdgeColour;
	uint32_t rubyId = 0;
	if(Font_Open(name, rubySize, width, font.bold, &rubyId) != 0)
		return 0;
	FontEntryInfo_t rubyFont;
	Font_GetInfo(rubyId, &rubyFont);
	for(TextRecord_t* record = layout->records; record != NULL; record = record->next)
	{
		if(record->rubyKey == NULL)
			continue;
		TextWord_t* word = Text_FindWord(dictionary, record->rubyKey);
		if(word != NULL)
			Text_LayoutRubyWord(renderer, record, word, rubySize, &rubyFont, colour, &rubyStyle,
			                    record->x0 + gTextRubyOffsetX, record->y0 - rubySize + gTextRubyOffsetY,
			                    record->rubyWidth, (int32_t)record->delay);
		Text_RemoveWord(dictionary, record->rubyKey, 1);
	}
	Text_RemoveLocalWords(dictionary);
	return 1;
}

// 0x00437110: the records freed.
static void Text_FreeLayout(TextLayout_t* layout)
{
	TextRecord_t* record = layout->records;
	while(record != NULL)
	{
		TextRecord_t* next = record->next;
		free(record->rubyKey);
		Text_FreeBitmap(&record->bitmap);
		free(record);
		record = next;
	}
	layout->records = NULL;
}

// 0x00437A20: every record blitted onto the target at its place. The number of
// records drawn.
static uint32_t Text_BlitLayout(TextLayout_t* layout, Bitmap_t* target)
{
	uint32_t count = 0;
	for(TextRecord_t* record = layout->records; record != NULL; record = record->next)
	{
		Text_BlitAt(target, record->x, record->y, &record->bitmap, BITMAP_BLEND_ALPHA, 0);
		count++;
	}
	return count;
}

// 0x00434D30: lay the text out and draw it.
static uint32_t Text_Print(Renderer_t* renderer, Bitmap_t* target, int32_t* cursor, uint32_t* lines,
                           const Rect_t* rect, const char* text, uint32_t ruby, const char* rubyDictionary,
                           uint32_t fontId, uint32_t proportional, uint32_t kinsoku, int32_t lineSpacing,
                           uint32_t colour, uint32_t rubyColour, const TextStyle_t* style)
{
	FontEntryInfo_t font;
	if(!Font_GetInfo(fontId, &font))
		return 0;
	TextLayout_t layout;
	memset(&layout, 0, sizeof(layout));
	TextDictionary_t dictionary;
	memset(&dictionary, 0, sizeof(dictionary));
	Text_ParseDictionary(&dictionary, rubyDictionary);
	int32_t lineHeight = (font.size * lineSpacing) / 100 + font.size;
	Text_Layout(renderer, &layout, text, ruby, cursor, rect, lineHeight, fontId, proportional, kinsoku,
	            colour, style, lines, &dictionary);
	if(ruby)
		Text_LayoutRuby(renderer, &layout, fontId, style, rubyColour, &dictionary);
	// 0x00437D90 with an alignment of 0 does nothing.
	Text_BlitLayout(&layout, target);
	Text_FreeLayout(&layout);
	Text_ClearDictionary(&dictionary);
	return 1;
}

uint32_t Text_DrawIntoBitmap(Renderer_t* renderer, int bitmapId, uint32_t* lines, int32_t x, int32_t y,
                             const char* text, uint32_t ruby, const char* rubyDictionary,
                             uint32_t fontNumber, int32_t size, int32_t width, uint32_t bold,
                             uint32_t proportional, uint32_t kinsoku, int32_t lineSpacing,
                             uint32_t colour, uint32_t rubyColour, const TextStyle_t* style)
{
	Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, bitmapId);
	if(bitmap == NULL)
		return 0x80000004;
	// 0x004035A0: the font number's name, and the managed font of that name, size,
	// width and weight. Its failures come back one lower than the font code's.
	const char* name = Engine_FontNameById(fontNumber);
	uint32_t fontId = 0;
	uint32_t result = Font_Open(name, size, width, bold, &fontId);
	if(result == 0x80000002)
		return 0x80000001;
	if(result == 0x80000003)
		return 0x80000002;
	if(result == 0x80000004)
		return 0x80000003;
	if(result != 0)
		// 0x004035F9: what was in ecx - the width - comes back for anything else.
		return (uint32_t)width;

	// 0x00434CC0: the whole bitmap is the rectangle, and the start is remembered
	// in 0x00565D34 / 0x00565D38.
	Bitmap_t view = *bitmap;
	Rect_t rect = { 0, 0, bitmap->width - 1, bitmap->height - 1 };
	int32_t cursor[2] = { x, y };
	Text_Print(renderer, &view, cursor, lines, &rect, text != NULL ? text : "", ruby, rubyDictionary,
	           fontId, proportional, kinsoku, lineSpacing, colour, rubyColour, style);
	return 0;
}
