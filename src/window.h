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

#endif // __WINDOW_H__
