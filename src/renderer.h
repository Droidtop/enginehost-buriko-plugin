#ifndef _RENDERER_H_
#define _RENDERER_H_

#include <stdint.h>
#include <SDL2/SDL.h>

/*
 * The original's drawing device (0x00566750) is built at 0x004075E0 with a fixed
 * table of 0x4000 bitmap records, 0x48 bytes each, the pixel pointer at +0x04 and
 * zero meaning the slot is empty. Every id the script uses is an index into it.
 */
#define RENDERER_MAX_BITMAPS 0x4000
#define RENDERER_MAX_SCREENS 0x10

// The largest gap coefficient 0x0042C550 accepts.
#define SCREEN_MAX_GAP_COEFFICIENT 0x320
// The largest message swinging style 0x0042C660 accepts.
#define SCREEN_MAX_SWINGING_STYLE 2

typedef struct Screen
{
	int width;
	int height;
	int x;
	int y;
	// +0x360 of the original's 0x3CC-byte window object: the gap
	// coefficient its text layout reads at 0x0042B892. The constructor
	// (0x0042AF00) leaves it 0.
	int gapCoefficient;
	// The font selection 0x0042C490 writes into the window: the font
	// itself at +0x350, its size at +0x358 and size * width / 100 at
	// +0x35C, with the width and style the font was asked for. NULL font
	// family means no font has been chosen yet.
	const char* fontFamily;
	int fontSize;
	int fontWidth;
	int fontStyle;
	int fontScaledWidth;
	// +0x354 and +0x364, written by 0x0042C5F0 and 0x0042C610 from the
	// same opcode. What reads them back is not read yet.
	// +0x374, the message swinging style: 0, 1 or 2 only (0x0042C660).
	int swingingStyle;
	int field354;
	int field364;
	uint8_t* bitmap;
	SDL_Surface* surface;
} Screen_t;

/*
 * The original's pixel modes, from 0x00401C10: an 8-bit image is mode 3, a 24-bit one
 * mode 1, and a 32-bit one mode 2 unless its header carries one of the alternate
 * layouts, which CompressedBG files never do. Bytes per pixel comes from the table at
 * 0x004E41B0 and is NOT bits/8: a 24-bit image is held four bytes to the pixel.
 */
#define BITMAP_MODE_16      0
#define BITMAP_MODE_24      1
#define BITMAP_MODE_32      2
#define BITMAP_MODE_8       3
#define BITMAP_MODE_COUNT   8

typedef struct Bitmap
{
	int width;
	int height;
	int mode;
	int stride;
	uint8_t* bitmap;
	// The pair at +0x28 / +0x2C of the original's table entry: an offset carried
	// with the image, set by the loader from the image header (0x00401F80).
	int offsetX;
	int offsetY;
} Bitmap_t;

typedef struct Engine Engine_t;
typedef struct Renderer
{
	Engine_t* engine;
	Bitmap_t* bitmaps[RENDERER_MAX_BITMAPS];
	Screen_t* screens[RENDERER_MAX_SCREENS];
	int activeScreen;
	int allocatedScreens;
} Renderer_t;

Renderer_t* Renderer_Init(Engine_t* engine);
int Renderer_ModePixelBytes(int mode);
int Renderer_ModeForBits(int bits);
// NULL unless the id is in range and the slot holds a live bitmap (0x00407F20).
Bitmap_t* Renderer_ResolveBitmap(Renderer_t* renderer, int id);
// Replaces whatever is in the slot with a cleared bitmap (0x00407DA0); NULL on a bad
// id, an unusable mode or an allocation failure.
Bitmap_t* Renderer_CreateBitmap(Renderer_t* renderer, int id, int width, int height, int mode);
// Releases a slot and says whether there was anything in it (0x00407CF0).
int Renderer_DestroyBitmap(Renderer_t* renderer, int id);
// Writes one colour over every pixel of a bitmap (0x00408040); 0 when the slot
// is empty, 1 when it was filled.
int Renderer_FillBitmap(Renderer_t* renderer, int id, uint32_t colour);
// 0, or one of the 0x8000000X codes the original's loader returns.
uint32_t Renderer_LoadBitmap(Renderer_t* renderer, int slot, const char* filename, const char* archive);
// 0 on success, 1 invalid destination, 2 invalid source, 3 invalid range (0x004033A0).
int Renderer_CopyBitmap(Renderer_t* renderer, int destination, int source, int offsetX, int offsetY, int width, int height);
// The original's own blend modes (0x0040AC9C), of which these are written.
#define BITMAP_BLEND_ALPHA        0x00
// The same, with a uniform transparency on top (0x0040B320); 0x20 shares it.
#define BITMAP_BLEND_ALPHA_TRANS  0x01
#define BITMAP_BLEND_ALPHA_TRANS2 0x20
#define BITMAP_BLEND_COPY         0x80
// Draws the source onto the destination with its top left corner at (x, y)
// (0x00402720). 0 on success, 1 no destination, 2 no source, 3 incompatible
// pixel modes, 4 nothing left after clipping (success to the caller), 5 a blend
// mode that is not written yet, 6 a transparency that mode 0x01 does not take yet.
int Renderer_BlitBitmap(Renderer_t* renderer, int destination, int x, int y,
                        int source, int mode, int transparency);
// Scales the source into the destination by two 16.16 rates (0x00402B90).
// 0 on success, 1 the destination could not be made, 2 no source, 3 a source
// that is not 24- or 32-bit, 4 a rate that leaves nothing, 5 a filter that is
// not written yet.
int Renderer_ScaleBitmap(Renderer_t* renderer, int destination, int source,
                         int rateX, int rateY, int filter);
// The whole of one bitmap, its offset pair included, into another slot
// (0x00403450). 0 on success, 1 invalid destination, 2 invalid source.
int Renderer_DuplicateBitmap(Renderer_t* renderer, int destination, int source);
uint32_t Renderer_CreateScreen(Renderer_t* renderer, int width, int height);
// NULL unless the handle carries the screen tag and names a live slot,
// the way 0x004407A0 resolves a window handle against its sixteen slots.
Screen_t* Renderer_ResolveScreen(Renderer_t* renderer, uint32_t handle);
void Renderer_DestroyScreen(Renderer_t* renderer, uint32_t handle);
void Renderer_DrawBitmapToScreen(Renderer_t* renderer, uint32_t bitmapId, int screenId);
void Renderer_DrawScreen(Renderer_t* renderer);
void Renderer_SetScreenParams(Renderer_t* renderer, uint32_t handle, int x, int y);
void Renderer_Free(Renderer_t* renderer);

#endif