//
// Text layout (0x00433xxx - 0x00438xxx)
//
// The original lays a string out into a list of records, one per character (and
// per ruby character, and per event marker), each holding the character already
// drawn into a small bitmap of its own with its effect, and the place it goes.
// Drawing text into a bitmap (Grp2 0x9C, Grp1 0x9C/0x9D through 0x00403B10) blits
// every record at once; a window's message text (0x0042B7F0) keeps the records and
// reveals them over time. This is that layout, ported from 0x00434D30 and the
// functions it calls.
//

#ifndef _TEXT_H_
#define _TEXT_H_

#include <stdint.h>
#include "renderer.h"
#include "font.h"

// The five dwords 0x00434F10 builds: the character's effect. Kind 0 is none, 1 a
// shadow (a copy of the character in `colour`, offset by a, b per cent of the size
// and drawn under it with 0x100 - weight of opacity), 2 an edge (the character
// grown by a, b per cent of the size in `colour`, drawn the same way).
typedef struct TextStyle
{
	uint32_t kind;          // +0x00
	int32_t  a;             // +0x04
	int32_t  b;             // +0x08
	uint32_t colour;        // +0x0C
	uint32_t weight;        // +0x10
} TextStyle_t;

// One laid-out thing (0x48 bytes).
typedef struct TextRecord TextRecord_t;
struct TextRecord
{
	uint32_t shown;         // +0x00: the window has finished revealing it
	uint32_t delay;         // +0x04: when it starts to appear
	uint32_t fade;          // +0x08: how far into its fade it is
	uint32_t fadeLength;    // +0x0C: 0x0050763C when laid out
	int32_t  x;             // +0x10
	int32_t  y;             // +0x14
	int32_t  x0;            // +0x18, where it was first placed
	int32_t  y0;            // +0x1C
	Bitmap_t bitmap;        // +0x20: the character, drawn
	char*    rubyKey;       // +0x38: the dictionary word this character starts
	int32_t  rubyWidth;     // +0x3C: that word's width (0x00434FE0's second answer)
	uint32_t kind;          // +0x40: 0 a character, 2 ruby, 0x80000000 an event
	TextRecord_t* next;     // +0x44
};

// 0x00434F10: a style out of its parts. A kind of 0 is all zero; kinds 1 and 2 need
// both offsets at or below 100 and the weight at or below 0x100. 0 when refused, in
// which case *style is left alone.
int Text_MakeStyle(TextStyle_t* style, uint32_t kind, int32_t a, int32_t b, uint32_t colour, uint32_t weight);
// 0x00433650: the default style Grp0 0x9C / 0x9D set.
void Text_DefaultStyle(TextStyle_t* style);
// Grp0 0x9C (0x00433600): the default style's kind, unchecked.
void Text_SetDefaultStyleKind(uint32_t kind);
// Grp0 0x9D (0x00433610): the default style's offsets and weight, its colour back
// to 0. 0 when refused (a or b above 100, the weight above 0x100).
int Text_SetDefaultStyleEdge(int32_t a, int32_t b, uint32_t weight);
// Grp0 0x95 (0x004335B0) and Grp0 0x96 (0x004335D0): two pairs of values the
// window text reads (0x00433A92, 0x00433985). Nothing is written unless the count
// is above 0; the answer is whether it was.
int Text_SetSplitPair1(int32_t count, int32_t value);
int Text_SetSplitPair2(int32_t count, int32_t value);

// 0x00403B10 -> 0x00434C80: `text` drawn into bitmap `bitmapId` from (x, y) to the
// bitmap's own edges, with font number `fontNumber` (Ext0 0xC1's numbering) at
// `size` and `width` per cent. `lineSpacing` is extra per cent of the size between
// lines, `proportional` sets characters by their ink rather than their cells,
// `kinsoku` keeps the characters Japanese typesetting will not start a line with
// off the start of one, and `ruby` turns on ruby: the dictionary `rubyDictionary`
// (lines of "word\reading") and the <ruby>/<r> tags, drawn in `rubyColour`.
// *lines is set to the number of lines the text took. 0 on success, else
// 0x80000001 (size), 0x80000002 (width), 0x80000003 (font number), 0x80000004 (no
// such bitmap) as the opcodes report them.
uint32_t Text_DrawIntoBitmap(Renderer_t* renderer, int bitmapId, uint32_t* lines, int32_t x, int32_t y,
                             const char* text, uint32_t ruby, const char* rubyDictionary,
                             uint32_t fontNumber, int32_t size, int32_t width, uint32_t bold,
                             uint32_t proportional, uint32_t kinsoku, int32_t lineSpacing,
                             uint32_t colour, uint32_t rubyColour, const TextStyle_t* style);

#endif
