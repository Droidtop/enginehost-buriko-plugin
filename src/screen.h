#ifndef SCREEN_H_
#define SCREEN_H_

#include <stdint.h>
#include "object.h"
#include "renderer.h"

/*
 * The screen object (root+0x50): the bottom of the display list and what draws the
 * background. It is a plain display object of type 1 whose CLASS (+0x134) the
 * scripts replace (0x0043E270): class 1 draws nothing (0x0041E960), class 4 is a
 * background with a transition from a previous image or a colour (0x0041D2E0,
 * Grp0 0x43), class 12 is eight layered, positioned and scaled images (0x0041DBF0,
 * Grp1 0x40). Every class draws through the base's vtable+0x18 (0x0041C410): when
 * its content flag (+0x138) is set it offers the class's own draw (vtable+0x84)
 * first, and whatever is not drawn by that is cleared to black.
 */

// The screen's class (0x0043E5A0 -> 0x00420870).
uint32_t Screen_Class(void);
// 0x0043E270: the screen becomes an object of class `cls`, a new one unless it
// already is one. 0 on success.
uint32_t Screen_Replace(uint32_t cls);
// 0x0043E570 (Grp0 0x4C): whether the screen is visible, and whether its content
// is drawn (vtable+0x04, vtable+0x78).
void Screen_SetShown(uint32_t visible, uint32_t content);

// 0x0043D830 (Grp0 0x43): class 4. Returns 0, or the handler's error number: 1 the
// image, 2 the previous image, 3 the rule, 4 a rule that is not 8-bit, 5 other.
uint32_t Screen_SetTransition(int32_t x, int32_t y, int32_t bitmap,
                              int32_t fromX, int32_t fromY, int32_t fromBitmap,
                              int32_t rule, uint32_t ruleParameter, uint32_t level);
// 0x0043DD90 (Grp1 0x40): class 12, layer 0. Returns 0 or 3 (a bad image) or 4
// (a scale of zero).
uint32_t Screen_SetLayered(int32_t x, int32_t y, int32_t bitmap,
                           int32_t anchorX, int32_t anchorY, uint32_t angle,
                           uint32_t scaleX, uint32_t scaleY, uint32_t flag);

// Class overrides of the base virtuals, answered for the screen when it has them.
// Each returns 1 when the class handled it.
int Screen_SetEffectLevel(DisplayObject_t* object, uint32_t level);   // vtable+0x48
int Screen_SetPosition(DisplayObject_t* object, int32_t x, int32_t y); // vtable+0x2C
int Screen_SetParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, uint32_t* result); // vtable+0x5C
int Screen_GetPosition(DisplayObject_t* object, int32_t* x, int32_t* y);     // vtable+0x30
int Screen_GetEffectLevel(DisplayObject_t* object, uint32_t* level);        // vtable+0x4C

// 0x0041C410 for a screen object: 1 when anything was drawn.
void Screen_Draw(Renderer_t* renderer, DisplayObject_t* object, Bitmap_t* target, const Rect_t* rect);

void Screen_FreeData(DisplayObject_t* object);

#endif
