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

// Grp1 0x88 (0x00440AA0): the window's proportional setting (+0x354), its reserved
// column (+0x364), and its font (0x0042C490): the managed font of that name, size,
// width and weight, the size, and the cell advance. With the column reserved the
// client area's right edge is pulled in to leave one advance free. 0, or the font
// manager's failure (0x80000002 a size, 0x80000003 a width, 0x80000004 a name), in
// which case the font fields are left as they were.
uint32_t Window_SetFont(Renderer_t* renderer, DisplayObject_t* window, const char* name, int32_t size,
                        int32_t width, uint32_t bold, uint32_t proportional, uint32_t reserveColumn);
// Grp1 0x89 (0x0042C550): the line spacing, per cent of the size. 0 for one above 800.
int Window_SetLineSpacing(DisplayObject_t* window, uint32_t spacing);
// Grp1 0x8A (0x0042C630): the text direction, 0 across and 1 down, and the cursor
// back to where a line starts. 0 for anything else.
int Window_SetDirection(DisplayObject_t* window, uint32_t direction);
// Grp1 0x8B (0x0042C660): the line alignment, 0 to 2. 0 for anything else.
int Window_SetAlignment(DisplayObject_t* window, uint32_t align);
// 0x0042C5A0: the size plus the line spacing, how far one line is below the last.
int32_t Window_LineHeight(const DisplayObject_t* window);

// The text surface (+0x184), the window's size; 0 when the window has no pixels yet.
int Window_TextSurface(Renderer_t* renderer, DisplayObject_t* window, Bitmap_t* out);
// 0x0042B620 and 0x0042B630: whether the text surface is shown (+0x17C) and its
// transparency (+0x19C). Neither redraws the window.
void Window_SetTextShown(DisplayObject_t* window, uint32_t shown);
void Window_SetTextTransparency(DisplayObject_t* window, uint32_t transparency);
// 0x0042BA90: the text surface cleared and the window redrawn.
void Window_ClearText(Renderer_t* renderer, DisplayObject_t* window);
// 0x0042C3B0: the area text is drawn into, the client area with one font size more on
// the right when the window keeps a column free (0x0042C360).
void Window_TextArea(const DisplayObject_t* window, Rect_t* rect);
// 0x0042B690: `image` drawn onto the text surface with its corner at (x, y) of the
// window, clipped to the text area, with a blend mode and a weight (0x0040A530); the
// part it covered is redrawn (0x0042CE10). 0, or the blit's failure as 0x0042B7DC
// maps it: 5, 6, 7 for 1, 2, 3 and 4 for nothing left.
uint32_t Window_DrawText(Renderer_t* renderer, DisplayObject_t* window, int32_t x, int32_t y,
                         Bitmap_t* image, int mode, int weight);
// 0x0042C6E0: the text cursor to the start of the next line (across) or column
// (down). 1 when the next one still fits in the client area.
int Window_NewLine(DisplayObject_t* window);
// 0x0042C760: whether something `extent` wide (across) or high (down) still fits
// on the line from the cursor.
int Window_Fits(const DisplayObject_t* window, int32_t extent);
// 0x0042C7C0: the cursor moved along the line by `amount`.
void Window_AdvanceCursor(DisplayObject_t* window, int32_t amount);

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
