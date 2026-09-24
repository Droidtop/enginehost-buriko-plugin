#ifndef __WINDOW_H__
#define __WINDOW_H__

#include <stdint.h>

#include "object.h"
#include "renderer.h"

// ----------------------------------------------------------------------------------
// A window's own contents
//
// A window (CDspObjWindow, 0x0042AF00) is not one image. It holds a numbered array of
// content slots (+0x314, count at +0x310), a display list of its own (+0x340) built by
// the same constructor the display root's list is built by (0x0044C840 -> 0x00430650),
// and an origin (+0x344 / +0x348) that the content's coordinates are measured from.
// Its pixels are composed out of three layers by 0x0042CB10, in the order the three
// dwords at +0x3C0 give: the background image, the frame, and that list.
//
// This is what an icon's parts become. 0x0044A9E0 reserves one slot per part
// (0x0042BCB0) and then, per part, creates a sprite in that slot (0x0042BFB0) whose
// content is the part's bitmap. The window's redraw is what puts them on the window,
// and the window's own draw (0x0042B1D0) is what puts the window on the screen.
// ----------------------------------------------------------------------------------

// 0x0042BCB0. Throws away whatever contents the window had - every sprite out of the
// window's list and freed, the list itself and the slot array freed - and then, for a
// count above zero, makes a new empty array of that many slots, a new empty list, and
// the origin out of the current display mode. A count of zero leaves the window with
// none of the three, which is how the original empties it.
void Window_ReserveContent(Renderer_t* renderer, DisplayObject_t* window, uint32_t count);

// 0x0042CB10: compose `rect` of the window's own pixels out of its three layers, in
// the window's own order. `rect` is in the window's coordinates. Nothing happens when
// the window has no pixels yet, which is the original's +0x13C guard.
void Window_Redraw(Renderer_t* renderer, DisplayObject_t* window, const Rect_t* rect);
// 0x0042CAE0: the same over the whole of the window.
void Window_RedrawAll(Renderer_t* renderer, DisplayObject_t* window);

// 0x0042B9E0: the client area, where text goes, and the text cursor reset to it.
// 1 when it was inside the window and was taken.
int  Window_SetClientArea(Renderer_t* renderer, DisplayObject_t* window, int32_t left, int32_t top, int32_t right, int32_t bottom);
// 0x0042C690.
void Window_ResetTextCursor(DisplayObject_t* window);

// 0x0042B380 (Grp0 0x86): the window's background image, the bitmap copied onto a
// surface of the window's own at its corner; -1 for none. 0, or 1 (the window has no
// pixels) or 2 (no such bitmap).
uint32_t Window_SetBackground(Renderer_t* renderer, DisplayObject_t* window, int32_t bitmap);

// Grp2 0x88 (0x0042B490): whether the background surface is shown.
uint32_t Window_SetBackgroundShown(Renderer_t* renderer, DisplayObject_t* window, uint32_t shown);
// Grp2 0x8A (0x0042B4A0): the background surface filled with one colour.
uint32_t Window_FillBackground(Renderer_t* renderer, DisplayObject_t* window, uint32_t colour);
// Grp2 0x89 (0x0042B4E0): a bitmap blended onto the background surface at (x, y).
uint32_t Window_DrawOnBackground(Renderer_t* renderer, DisplayObject_t* window, int32_t bitmap,
                                 int32_t x, int32_t y, uint32_t mode, uint32_t level);

// 0x0042BFB0: a sprite of kind 5 in content slot `slot`, whose anchor (anchorX,
// anchorY of the bitmap) lands on (x, y) of the window. 0, 2 when the sprite could not
// be given the bitmap, 9 for a slot the window does not have.
uint32_t Window_SetContentSlot(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot,
                               int32_t bitmap, int32_t x, int32_t y,
                               int32_t anchorX, int32_t anchorY, uint32_t layer);
// 0x0042C2A0: the part of the window a slot's sprite covers, redrawn. 1 when there was one.
int Window_RedrawContentSlot(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot);
// 0x0044B340 (Grp0 0xB7): a window's content made from a descriptor (the tree
// 0x0046CB50 copies, as an Ex icon's): one slot per part. 0, or 0x80000001 for an
// entry count outside 1..0x100, 0x80000002 for an entry's part count outside it.
struct IconContent;
uint32_t Window_SetContent(Renderer_t* renderer, DisplayObject_t* window, const struct IconContent* content);

// A content slot's sprite changed in place; 9 for a slot that is missing or empty.
uint32_t Window_SlotSetEnabled(DisplayObject_t* window, uint32_t slot, uint32_t enabled);              // 0x0042C140
uint32_t Window_SlotSetBitmap(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot, int32_t bitmap); // 0x0042C170
uint32_t Window_SlotSetPosition(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot,
                                int32_t x, int32_t y, int32_t z);                                        // 0x0042C1B0
uint32_t Window_SlotSetAngle(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot, int32_t angle); // 0x0042C220
uint32_t Window_SlotSetLayer(DisplayObject_t* window, uint32_t slot, uint32_t layer);                  // 0x0042C250
int      Window_SlotRedrawWithout(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot);         // 0x0042C300

// The eight parts of the text layer.
void     Window_ClientRect(const DisplayObject_t* window, Rect_t* rect);                                  // 0x0042C380
void     Window_EnablePart(Renderer_t* renderer, DisplayObject_t* window, int index, uint32_t enabled);   // 0x0042BB90
void     Window_SetPartPosition(Renderer_t* renderer, DisplayObject_t* window, int index,
                                int32_t x, int32_t y, uint32_t weight);                                  // 0x0042BBC0
uint32_t Window_SetPartBitmap(Renderer_t* renderer, DisplayObject_t* window, int index, const Bitmap_t* bitmap); // 0x0042BC10
int      Window_PartScreenRect(Renderer_t* renderer, DisplayObject_t* window, int index, Rect_t* rect);  // 0x0042C410

#endif // __WINDOW_H__
