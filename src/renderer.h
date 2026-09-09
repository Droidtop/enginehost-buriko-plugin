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
	// +0x15C, written by 0x0042B490, after which the window is laid out
	// again. What reads it back is not read yet.
	int field15C;
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
	// +0x1C of the original's table entry, read back by 0x00408300: a number that
	// names this particular image, so a display object that was given the slot can
	// tell later that the slot has been filled with something else since.
	uint32_t serial;
	// The pair at +0x28 / +0x2C of the original's table entry: an offset carried
	// with the image, set by the loader from the image header (0x00401F80).
	int offsetX;
	int offsetY;
} Bitmap_t;

/*
 * The original's rectangle, four dwords with both edges INSIDE it: 0x00409190 builds
 * one over an image as { 0, 0, width - 1, height - 1 }, and 0x00409110 answers empty
 * when either edge crosses. Every clip in the drawing path is one of these.
 */
typedef struct Rect
{
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
} Rect_t;

typedef struct Engine Engine_t;
typedef struct Renderer
{
	Engine_t* engine;
	Bitmap_t* bitmaps[RENDERER_MAX_BITMAPS];
	Screen_t* screens[RENDERER_MAX_SCREENS];
	int activeScreen;
	int allocatedScreens;
	// The animated cursor's frames: the count at 0x00565B7C and an array of
	// 0x18-byte entries at 0x00565B80, each a bitmap of its own copied out of the
	// bitmap table (0x004333E0). 0x00434080 is what plays them: it counts frames at
	// +0x64 of the thing that owns it, waits the interval below between them and
	// indexes this array by that counter.
	Bitmap_t** animationFrames;
	int animationFrameCount;
	// 0x0050765C, the interval between two frames, which Grp0 0x99 sets (0x00433560)
	// and 0x00434080 waits out. Fureraba's message window asks for 2000 / 6.
	uint32_t animationInterval;
	// Where the animation is drawn (0x00433570, three stores). 0x0043412C reads the
	// first back: 1 means the offset below is the position itself, anything else
	// means it is measured from the text cursor of the window the animation belongs
	// to. 0x00565B84, 0x00565B88 and 0x00565B8C.
	uint32_t animationPlacement;
	int32_t animationX;
	int32_t animationY;
	// +0x10 of the drawing device, the counter 0x00407DA0 hands serials out of.
	uint32_t bitmapSerial;
	// +0x14. 0x00407DA0 keeps a recreated slot's old serial only when this is set;
	// the device constructor (0x004075E0) leaves it 0 and nothing this engine has
	// read sets it, so every created bitmap gets a fresh serial. If a slot ever has
	// to keep its serial across a recreation, this flag is where that comes from.
	int keepBitmapSerial;
	// What the frame is composed into before it reaches the window: the original's
	// drawing device holds it and the display list draws into it through the
	// descriptor at list+0x54. It is made at the size of the display mode the game
	// chose (Sys0 0x60) and remade when that size changes.
	Bitmap_t* backBuffer;
} Renderer_t;

Renderer_t* Renderer_Init(Engine_t* engine);
int Renderer_ModePixelBytes(int mode);
int Renderer_ModeForBits(int bits);
// NULL unless the id is in range and the slot holds a live bitmap (0x00407F20).
Bitmap_t* Renderer_ResolveBitmap(Renderer_t* renderer, int id);
// Replaces the animated cursor's frames with copies of `count` bitmaps named by
// `ids` (0x004333E0), each in the screen's pixel mode. An id of -1 leaves that frame
// empty. 1 on success; 0 when an id named no bitmap, with *badId set to it.
int Renderer_SetAnimationFrames(Renderer_t* renderer, int count, const uint32_t* ids, int32_t* badId);
// A bitmap slot's serial, or 0xFFFFFFFF when the slot is empty (0x00408300).
uint32_t Renderer_BitmapSerial(Renderer_t* renderer, int id);
// The device's own pixel mode (0x00565B14, read by 0x00407B10). Every display
// object's surface, the back buffer and the animated cursor's frames are built in
// it. It is the pixel mode the script chose with Sys0 0x60, which that opcode has
// already refused above 1: 16-bit (mode 0) or 24-bit held four bytes to the pixel
// (mode 1).
int Renderer_ScreenMode(Renderer_t* renderer);
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
// 0x00409110: narrow `rect` to what it and `other` have in common, and say whether
// anything is left. `rect` is written even when nothing is.
int Renderer_RectIntersect(Rect_t* rect, const Rect_t* other);
// 0x004090B0: is `inner` wholly inside `outer`.
int Renderer_RectContains(const Rect_t* outer, const Rect_t* inner);
// 0x00409170: move a rectangle.
void Renderer_OffsetRect(Rect_t* rect, int32_t x, int32_t y);
// 0x004091B0: narrow a bitmap to the part of itself that `rect` covers, by moving its
// pixel pointer and shrinking its size. The result shares the original's pixels and
// keeps its stride, so it is a view, not a copy - which is exactly what the original
// does with the six-dword descriptor it passes down the draw. 0 when nothing is left.
int Renderer_ClipBitmap(Bitmap_t* view, const Rect_t* rect);
// 0x0040A620 with no rectangle: every pixel of the view set to zero.
void Renderer_ClearBitmap(Bitmap_t* view);
// 0x0040E260: one colour blended over every pixel of a view, which is what a
// filter object's draw does. Per channel the result is
// ((pixel * (0x100 - weight)) >> 8) + ((colour * weight) >> 8), so a weight of
// 0x100 is the colour alone and a weight of 0 leaves the view alone. 1 when the
// fill was done; 0 for a pixel mode the original has no arm for, which are all
// of them but 16-bit and 24-bit-in-four-bytes.
int Renderer_FillView(Bitmap_t* view, uint32_t colour, uint32_t weight);
// 0x0040A9E0: the blend of one view onto another, corner to corner. The results are
// Renderer_BlitBitmap's.
int Renderer_BlitView(Bitmap_t* destination, Bitmap_t* source, int mode, int transparency);
// The frame buffer, made on first use at the display mode's size.
Bitmap_t* Renderer_BackBuffer(Renderer_t* renderer);
// Composes a frame and writes it to a PNG. 0 on success.
int Renderer_SaveScreenPng(Renderer_t* renderer, const char* path);

uint32_t Renderer_CreateScreen(Renderer_t* renderer, int width, int height);
// NULL unless the handle carries the screen tag and names a live slot,
// the way 0x004407A0 resolves a window handle against its sixteen slots.
Screen_t* Renderer_ResolveScreen(Renderer_t* renderer, uint32_t handle);
// The six-dword descriptor of a window's own pixels - the original keeps it at
// window+0x144 and hands it to the draw (0x0042B1D0) and to the window's own redraw
// (0x0042CB10) alike. 0 when that window has none.
int Renderer_WindowBitmap(Renderer_t* renderer, uint32_t handle, Bitmap_t* out);
void Renderer_DestroyScreen(Renderer_t* renderer, uint32_t handle);
void Renderer_DrawBitmapToScreen(Renderer_t* renderer, uint32_t bitmapId, int screenId);
void Renderer_DrawScreen(Renderer_t* renderer);
void Renderer_SetScreenParams(Renderer_t* renderer, uint32_t handle, int x, int y);
void Renderer_Free(Renderer_t* renderer);

#endif