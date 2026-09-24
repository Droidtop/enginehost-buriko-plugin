#ifndef _SPRITE5_H_
#define _SPRITE5_H_

#include <stdint.h>

#include "object.h"
#include "renderer.h"

/*
 * Sprites of kind 5 (+0x134 == 5): a bitmap, or two crossfaded, placed in a 3D
 * position, projected onto the screen and drawn through a 2D transform (anchor,
 * angle, two scales) whose deltas follow the second effect parameter.
 *
 * The sprite class is 0x00425790 (vtable 0x004E4F24). Kind 5 is given its content by
 * 0x00427B70, which is reached from 0x00427240 (a window's content slot, 0x0042BFB0,
 * and the script's sprite opcode behind 0x0043EB90) and from the content setter
 * 0x004273C0 (its kind 5 arm, 0x00427424). 0x004272F0 -> 0x00427E60 is its sibling
 * for kind 6 and is not ported.
 *
 * Units. Positions, anchors and the deltas of the anchor are 16.16 pixels; angles are
 * 16.16 degrees (a full turn is 360 << 16, positive turns counter-clockwise on the
 * screen, as xform.h says); scales are unsigned 16.16 (0x10000 = 1:1). The second
 * effect parameter (+0xB8, whole, as 0x0041B820 in mode 1 reads it) is the
 * interpolation variable: 0 to 1 << 24 is "from the settings to settings + deltas".
 *
 * Where it lands (0x00429BD0). The 3D position (+0x4C/+0x50/+0x54, plus the two
 * vectors at +0x5C and +0x6C) is projected about a centre - the drawing device's
 * middle (width >> 1, height >> 1), or, for a sprite whose base constructor was given
 * 1 in +0x100 (a sprite from the table, 0x0043E5F0; a window's content is given 0),
 * the display root's projection centre (root+0x40/+0x44, Grp0 0x06) when that lies
 * on the device. Without perspective (+0x27C == 0) the base position (+0x30/+0x34,
 * vtable+0x28) is centre + position; with it, centre + position * scale(z). The
 * transformed image's bounding box (0x00429300) is the object's surface, and its
 * corner sits at base + (+0x38) + origin (+0x40) - (+0x2DC, +0x2E0), which is where
 * the anchor of the source bitmap ends up offset from: the anchor lands on the base
 * position (plus the 16.16 fraction +0x2A4/+0x2A8).
 */

// The state the original keeps in the sprite from +0x154 to +0x320, plus the base
// display object's fields that DisplayObject_t has no member for (the 3D position
// vectors and +0x100). Offsets are the original's.
struct Sprite5
{
	// --- base display object (0x0041A4D0) fields that object.h does not carry ---
	int32_t  position[4];         // +0x4C..+0x58: vtable+0x3C (0x0041B440) writes the first three
	int32_t  position2[4];        // +0x5C..+0x68: vtable+0x44 (0x0041B5F0)
	int32_t  position3[4];        // +0x6C..+0x78: 0x0041B650
	uint32_t useProjectionCentre; // +0x100: the base constructor's second argument
	// --- the sprite (0x00425790 and 0x00427B70) ---
	int32_t  secondBitmap;        // +0x154, -1 for none
	uint32_t secondSerial;        // +0x15C
	Bitmap_t levelsFirst[4];      // +0x160: four half-size mip levels of +0x150 (0x0042A730)
	Bitmap_t levelsSecond[4];     // +0x1C0: the same for +0x154
	Bitmap_t crossfade;           // +0x220: +0x150 and +0x154 crossfaded at a mip level (0x00428F50)
	uint32_t crossfadeLevel;      // +0x238: the +0x240 that cache was made with
	uint32_t crossfadeShift;      // +0x23C: its mip level
	// +0x240 is DisplayObject_t.field240 (the crossfade level) and +0x244 its
	// contentKind (the arm selector of vtable+0x48, 0x00428450).
	int32_t  anchorX;             // +0x248, 16.16, the point of the bitmap that is placed
	int32_t  anchorY;             // +0x24C
	int32_t  angle;               // +0x250, 16.16 degrees
	uint32_t perspectiveScaleX;   // +0x264, 0x0041AB70's answer when the content was set
	uint32_t perspectiveScaleY;   // +0x268
	uint32_t scaleX;              // +0x26C, 16.16 (parameter 0x42)
	uint32_t scaleY;              // +0x270
	uint32_t scaleYGiven;         // +0x274: parameter 0x42 had a second value
	uint32_t focal;               // +0x278: the distance 0x0041AB70 divides by, pixels
	uint32_t perspective;         // +0x27C: position scaled by depth (0x00429BD0)
	uint32_t smooth;              // +0x280: bilinear (the transformed blits' `smooth`)
	// What 0x004297A0 made of the above and the second effect parameter; the draw
	// uses these.
	int32_t  drawAnchorX;         // +0x284, 16.16
	int32_t  drawAnchorY;         // +0x288
	int32_t  drawAngle;           // +0x28C
	uint32_t drawScaleX;          // +0x29C
	uint32_t drawScaleY;          // +0x2A0
	uint32_t fractionX;           // +0x2A4: the projected position's 16.16 fraction
	uint32_t fractionY;           // +0x2A8
	// The deltas the second effect parameter drives (parameters 0x80, 0x81, 0x82, 0x8F).
	int32_t  deltaAnchorX;        // +0x2AC, 16.16 at e = 1 << 24
	int32_t  deltaAnchorY;        // +0x2B0
	int32_t  deltaAngle;          // +0x2B4, 16.16 degrees at curve value 0x10000
	int32_t  deltaScaleX;         // +0x2C4, 16.16 at e = 1 << 24
	int32_t  deltaScaleY;         // +0x2C8
	int32_t  deltaScale;          // +0x2CC
	uint32_t angleCurve;          // +0x2D0, a Process_Ease curve
	uint32_t surfaceWidth;        // +0x2D4: the bounding box 0x00429300 answered
	uint32_t surfaceHeight;       // +0x2D8
	int32_t  offsetX;             // +0x2DC: where the anchor is in that box, pixels
	int32_t  offsetY;             // +0x2E0
	// A horizontal sine wave (parameter 0x60, 0x00428610) and the image it makes.
	uint32_t wavePeriod;          // +0x300, rows per cycle; 0 is off
	uint32_t wavePhase;           // +0x304, rows
	uint32_t waveAmplitude;       // +0x308, 16.16 times the width; 0 is off
	Bitmap_t wave;                // +0x30C (0x004290E0)
};

// The state of a sprite, made on first use with the base constructor's values
// (everything 0, +0x154 = -1). `useProjectionCentre` is what the base constructor was
// given as its second argument, +0x100: 1 for a sprite from the table (0x0043E5F0),
// 0 for a window's content (0x0042BFB0). It is only used when the state is made.
struct Sprite5* Sprite5_State(DisplayObject_t* sprite, int useProjectionCentre);
// The sprite destructor's share (0x00425910: 0x0042AAA0, 0x00429090, 0x004291D0,
// 0x00429240) and the state itself.
void Sprite5_Free(DisplayObject_t* sprite);
// The two of the five teardown calls every content arm makes (0x004274E0, 0x00427570,
// 0x00427B70: 0x00429090 frees +0x220, 0x004291D0 frees +0x30C) that free something
// this module owns. The mip levels (+0x160, +0x1C0) are NOT freed by them; only
// 0x0042A730 and the destructor free those.
void Sprite5_Teardown(DisplayObject_t* sprite);

// The display root's projection centre, root+0x40/+0x44, which 0x00442F50 writes: the
// root's constructor with (-1, -1) and Grp0 0x06 (0x004796B0 -> 0x00461FB0) with the
// script's (x, y). 0x00442F70 uses it only while 0 <= x < width and 0 <= y < height of
// the drawing device. NOTE: this engine names Grp0 0x06 "SetMousePosition"; the
// original's handler writes this pair and nothing else.
void Sprite5_SetProjectionCentre(int32_t x, int32_t y);

// 0x00427240: a sprite becomes kind 5. The arguments, in the original's order:
//   x, y, z        16.16, the 3D position (vtable+0x3C)
//   bitmap         +0x150, must be a live bitmap (else 0x80000001)
//   second         +0x154, -1 for none; a live bitmap of the same width, height and
//                  mode (else 0x80000002 / 0x80000003)
//   level          +0x240 with a second bitmap (the crossfade, 0 = first, 0x100 = second)
//   contentKind    +0x244 with a second bitmap (what vtable+0x48 / +0x4C / +0x50 do)
//   anchorX, anchorY  whole pixels in the bitmap
//   angle          16.16 degrees
//   focal          +0x278, pixels (0x0042BFB0 passes window+0x34C, half the display width)
//   perspective    +0x27C
//   smooth         +0x280
//   blendMode      +0xA8 (0x0041B6D0)
//   effectLevel    vtable+0x48
//   layer          vtable+0x54 (0x0041B980; 0x10000 and above leaves the layer alone)
// 0 on success; 0x80000004 when the transformed box is narrower or lower than 2.
// `*unread` names work this module has not read that the call reached.
#define SPRITE5_BAD_BITMAP  0x80000001u
#define SPRITE5_BAD_SECOND  0x80000002u
#define SPRITE5_MISMATCH    0x80000003u
#define SPRITE5_TOO_SMALL   0x80000004u
uint32_t Sprite5_Setup(Renderer_t* renderer, DisplayObject_t* sprite, int useProjectionCentre,
                       int32_t x, int32_t y, int32_t z, int32_t bitmap, int32_t second,
                       uint32_t level, int32_t contentKind, int32_t anchorX, int32_t anchorY,
                       int32_t angle, uint32_t focal, uint32_t perspective, uint32_t smooth,
                       uint32_t blendMode, uint32_t effectLevel, uint32_t layer,
                       const char** unread);
// 0x00427B70 itself (the ten arguments of the setup from `bitmap` on).
uint32_t Sprite5_SetContent(Renderer_t* renderer, DisplayObject_t* sprite,
                            int32_t bitmap, int32_t second, uint32_t level, int32_t contentKind,
                            int32_t anchorX, int32_t anchorY, int32_t angle, uint32_t focal,
                            uint32_t perspective, uint32_t smooth);
// 0x004273C0's kind 5 arm (0x00427424): a new bitmap with the sprite's own anchor
// (the whole parts of +0x248/+0x24C), angle, focal, perspective and smooth, no second.
uint32_t Sprite5_SetContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int32_t bitmap);

// ----------------------------------------------------------------------------------
// The sprite's virtuals as they behave for kind 5.
// ----------------------------------------------------------------------------------
// vtable+0x34 (0x004282B0): the screen position - 0x0041B330 (base + position + origin,
// and the scroll when +0x48 is set) less +0x2DC/+0x2E0 for kinds 2 and 5. Kind 6 is
// not ported and answers 0x0041B330 with a printf.
void Sprite5_ScreenPosition(DisplayObject_t* sprite, int32_t* x, int32_t* y);
// vtable+0x20 (0x0041B200) and vtable+0x24 (0x0041B230): the surface in the object's
// own coordinates, and moved by vtable+0x34.
void Sprite5_LocalBounds(const DisplayObject_t* sprite, Rect_t* rect);
void Sprite5_ScreenBounds(DisplayObject_t* sprite, Rect_t* rect);
// vtable+0x3C (0x00428380): the 3D position (0x0041B440), then kind 5 rebuilds its
// transform and caches. Answers the name of unported work (kind 6), or NULL.
const char* Sprite5_SetPosition3D(Renderer_t* renderer, DisplayObject_t* sprite,
                                  int32_t x, int32_t y, int32_t z);
// vtable+0x40 (0x0041B560): the 3D position, four dwords.
void Sprite5_GetPosition3D(DisplayObject_t* sprite, int32_t out[4]);
// vtable+0x44 (0x004283E0): the second position vector (0x0041B5F0), then the same.
const char* Sprite5_SetPosition2(Renderer_t* renderer, DisplayObject_t* sprite,
                                 int32_t x, int32_t y, int32_t z);
// vtable+0x48 (0x00428450), the arms for content kinds 1 and 2 that object.c names:
// kind 1 writes +0x240 and rebuilds (no base); kind 2 is the base (0x0041B6F0, which
// the CALLER does first - it is object.c's static Object_SetEffectLevelBase) and then
// the same. Answers 1 when it handled the arm.
int Sprite5_SetEffectLevel(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t level);
// vtable+0x50 (0x00428540), the kind 5 arm after the base (0x0041B7C0): +0x240 takes
// the second effect parameter for content kind 3, then 0x00429A80, 0x00428F50(0),
// 0x004290E0.
void Sprite5_Effect2Changed(Renderer_t* renderer, DisplayObject_t* sprite);
// vtable+0x5C (0x00428670), the sprite's own arms 0x40, 0x41, 0x42, 0x60, 0x80, 0x81,
// 0x82 and 0x8F. Answers 1 when `number` is one of them (and *result the parameter
// result, OBJECT_PARAM_OK or OBJECT_PARAM_UNREAD with *unread set for kinds 2 and 6's
// unported work), 0 when the caller must go on to its own table.
int Sprite5_SetParameter(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t number,
                         uint32_t value1, uint32_t value2, uint32_t* result, const char** unread);
// vtable+0x60 (0x004289C0): the query codes 0x41 (angle), 0x10000000 (the transform
// after deltas: anchor x, y, angle, scale x, y) and 0x10000100 (bitmap and surface
// sizes). Answers 1 when it answered (*result 0 or 0xFFFF0001), 0 for "the base
// 0x0041BBB0".
int Sprite5_GetParameter(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t code,
                         int32_t* out, uint32_t* result);
// vtable+0x64 (0x00428B90) for kind 5: the screen point (x, y) taken back into the
// bitmap; the original then asks the base 0x0041BEC0 about (*u, *v) with a third
// argument of 0. Answers 1 for "ask the base with (*u, *v, 0)", 0 for "the answer is
// 0" (kinds 2 and 6), -1 for "not kind 5: ask the base with the caller's own".
int Sprite5_HitPoint(DisplayObject_t* sprite, int32_t x, int32_t y, int32_t* u, int32_t* v);
// vtable+0x70 (0x00428CB0): 1 for kinds 5 and 6.
int Sprite5_IsSpatial(const DisplayObject_t* sprite);
// 0x0041B190's depth term for an object whose +0x7C is clear: (0xFFF - (z >> 19)) &
// 0x1FFF with z the third component of 0x0041B590. For a sprite without state, z is
// 0 and this is 0xFFF.
uint32_t Sprite5_DepthTerm(DisplayObject_t* sprite);
// Grp0 0x53's kind 5 work (0x00428CD0): the mip levels rebuilt inside `rect` of the
// bitmaps (0x0042A850), or all of them for NULL (0x0042A730).
void Sprite5_BitmapChanged(Renderer_t* renderer, DisplayObject_t* sprite, const Rect_t* rect);

// vtable+0x18 (0x004259A0) for kind 5: `target` is the view Object_DrawTo narrowed to
// what is drawn and `rect` the same area in the object's own coordinates. Arm 0
// (0x00425A7B) for an identity transform and one bitmap, arm 1 (0x00425D3D) for an
// identity transform and two, arm 5 (0x0042679F) for everything else. Answers 1 when
// something was drawn or the original draws nothing, 0 when it refused by name.
int Sprite5_Draw(Renderer_t* renderer, DisplayObject_t* sprite, Bitmap_t* target, const Rect_t* rect);

#endif
