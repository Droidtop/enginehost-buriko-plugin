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

typedef struct Screen
{
	int width;
	int height;
	int x;
	int y;
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
void Renderer_DestroyBitmap(Renderer_t* renderer, int id);
// 0, or one of the 0x8000000X codes the original's loader returns.
uint32_t Renderer_LoadBitmap(Renderer_t* renderer, int slot, const char* filename, const char* archive);
// 0 on success, 1 invalid destination, 2 invalid source, 3 invalid range (0x004033A0).
int Renderer_CopyBitmap(Renderer_t* renderer, int destination, int source, int offsetX, int offsetY, int width, int height);
uint32_t Renderer_CreateScreen(Renderer_t* renderer, int width, int height);
void Renderer_DestroyScreen(Renderer_t* renderer, uint32_t handle);
void Renderer_DrawBitmapToScreen(Renderer_t* renderer, uint32_t bitmapId, int screenId);
void Renderer_DrawScreen(Renderer_t* renderer);
void Renderer_SetScreenParams(Renderer_t* renderer, uint32_t handle, int x, int y);
void Renderer_Free(Renderer_t* renderer);

#endif