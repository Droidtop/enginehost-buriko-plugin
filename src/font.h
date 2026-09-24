//
// Fonts: the installed families, and the glyph layer the text code draws with
//
// The engine's font opcodes ask the host which font families exist: on Windows
// that is EnumFontFamiliesEx, here it is a scan of the host's font directories
// with each file's own family name read out of its sfnt "name" table, which is
// the same string Windows reports.
//
// The original draws text through GDI. Its font object (0x0042D780, 0xAC bytes)
// holds an HFONT selected into a memory DC over an 8-bit DIB section, renders one
// character at a time into that DIB with TextOutA / TextOutW - or, on a system
// where that is found not to work, fetches it with GetGlyphOutlineA - and turns
// what it gets into a coverage cell of its own, which it caches. Everything above
// that cell - layout, colour, the effects - is the engine's own code and is ported
// as such in text.c. What GDI itself did is done here with stb_truetype from the
// host's fonts, standing in for the DIB and the calls the original makes on it.
//

#ifndef _FONT_H_
#define _FONT_H_

#include <stdint.h>

// Scans the host's font directories. Safe to call more than once; the second
// call does nothing.
void Font_Init();
void Font_Free();

// Number of distinct families found, and family i's name. Names are in the
// order they were found and are unique.
uint32_t Font_GetFamilyCount();
const char* Font_GetFamilyName(uint32_t index);

// ----------------------------------------------------------------------------
// The glyph layer (0x0042D700 - 0x0042F7FF)
// ----------------------------------------------------------------------------

// What 0x0042E990 hands back for one character: the seven dwords of its cache
// entry. The cell is FontObject's cell width by cell height, one coverage byte a
// pixel (0 nothing, 0xFF full), `stride` bytes a row. The rectangle is the part of
// the cell the character's ink covers across (left..right, both inside); top and
// bottom are always the whole cell (0x0042E873, 0x0042E87A).
typedef struct FontGlyphInfo
{
	uint32_t code;          // +0x00
	uint32_t doubleByte;    // +0x04: 1 for a code of 0x100 and up
	uint8_t* pixels;        // +0x08
	int32_t  left;          // +0x0C
	int32_t  top;           // +0x10
	int32_t  right;         // +0x14
	int32_t  bottom;        // +0x18
} FontGlyphInfo_t;

typedef struct FontObject FontObject_t;

// 0x0042D780: an empty font object. NULL when memory runs out.
FontObject_t* Font_NewObject(void);
// The object's vtable+0x00 with 1 (0x0042D7A0): everything it holds, and itself.
void Font_DeleteObject(FontObject_t* font);
// 0x0042DED0. `adjust` is the four 16.16 values Grp1 0x0E attached to the name
// (0x0042EFA0), or NULL for none; `level` is how many characters the cache keeps
// and must be 2 or more; `fallback` lets a face the host does not have fall back to
// the substitution Ext0 0xC7 set up. 0 on success, else 0x80000001 (a level below
// 2), 0x80000002 (size), 0x80000003 (width), 0x80000004 (name or no font at all),
// 0x80000005 / 0x80000006 (the adjust values, 0x0042EC10).
uint32_t Font_CreateObject(FontObject_t* font, const char* name, int size, int width, int bold,
                           int italic, const int32_t* adjust, int level, int fallback);
// 0x0042E990 (through 0x0042EA50): one character, from the cache or rendered into
// it. `code` is a Shift-JIS code as the text code decodes it - one byte below 0x100,
// lead byte in the high half above - or 0xB000 plus a UTF-16 code unit.
void Font_GetGlyph(FontObject_t* font, uint32_t code, FontGlyphInfo_t* out);
// 0x0042EA70, 0x0042EA80, 0x0042EA90: the cell's width, height and stride.
int Font_CellWidth(const FontObject_t* font);
int Font_CellHeight(const FontObject_t* font);
int Font_CellStride(const FontObject_t* font);
// 0x0042EAA0: the four adjust values the object was made with (+0x28..+0x34).
void Font_GetAdjust(const FontObject_t* font, int32_t out[4]);
// 0x0042EAC0 and 0x0042EAD0: the pair of spacing values at +0x38 / +0x3C that the
// layout adds after each character (0x004370D0). 0x0042EAD0 answers 0 for an index
// above 1 and leaves *out alone.
void Font_SetSpacing(FontObject_t* font, int32_t first, int32_t second);
int Font_GetSpacing(const FontObject_t* font, uint32_t index, int32_t* out);

// 0x0042DD60 (Grp0 0x0D): the quality of the glyph cells. With TextOut in use
// (0x00507694, which starts at 1) levels 0..3 render at 2, 4, 8 and 16 times the
// cell and count coverage out of that; with GetGlyphOutline in use they choose
// GGO_GRAY2, GGO_GRAY4 or GGO_GRAY8. Above 3 nothing changes and 0 is answered;
// otherwise 1. The fonts already made keep their old cells until
// Font_RecreateAll, which is what the opcode does next (0x00461F90).
int Font_SetQuality(int32_t level);
// 0x0042DD20 (Grp1 0x0C): 0 counts coverage linearly, 1 through a sine curve
// (0x0042E5E7). Anything above 1 is refused with 0.
int Font_SetCoverageCurve(uint32_t curve);
// 0x0042DD10 (Grp1 0x0D): the flag 0x0042E1F0 reads before it picks a width for
// CreateFontA.
void Font_SetPitchCheck(uint32_t value);

// The font manager the drawing device keeps at +0x04 (0x0042EDB0): fonts by name,
// size, width and weight, each with an id the text code names it by.
// What 0x0042F3B0 copies out of an entry: thirteen dwords from its +0x04.
typedef struct FontEntryInfo
{
	char           name[0x20];  // +0x00
	int32_t        size;        // +0x20
	int32_t        width;       // +0x24
	int32_t        bold;        // +0x28
	int32_t        italic;      // +0x2C, always 0 for a managed font
	FontObject_t*  font;        // +0x30
} FontEntryInfo_t;

// 0x0042F1D0: the id of the font with this name, size, width and weight, made if
// there is none yet. 0 on success; the failures are Font_CreateObject's.
uint32_t Font_Open(const char* name, int size, int width, int bold, uint32_t* id);
// 0x0042F3B0: an entry's details; 0 when no entry has that id.
int Font_GetInfo(uint32_t id, FontEntryInfo_t* out);
// 0x0042F3F0: every managed font made again, which is how a change of quality
// reaches the fonts that already exist.
void Font_RecreateAll(void);

#endif
