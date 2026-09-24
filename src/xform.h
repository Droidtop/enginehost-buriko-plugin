#ifndef _XFORM_H_
#define _XFORM_H_

#include <stdint.h>
#include "renderer.h"

/*
 * The original's two transformed blits: a source bitmap turned, zoomed and moved onto
 * a destination view. 0x00417800 replaces what it covers (a copy, optionally darkened
 * towards black); 0x004169B0 blends onto a 24-bit destination (a source's own alpha,
 * and a uniform transparency on top). Both take the same twelve cdecl arguments, in
 * this order, and every caller passes them straight through: the screen's layered
 * draw (0x0041E260, which pushes them at 0x0041E4CB and 0x0041E5AA), the sprite draw
 * (0x004259A0), 0x0042013E, the two script entries 0x00402F30 / 0x00402FD0, and the
 * banded workers 0x0041A280 / 0x0041A2F0.
 *
 * Coordinates. Everything positional is 16.16 fixed point (0x10000 is one pixel),
 * signed. The mapping is inverse: for every destination pixel (x, y) of the view -
 * its top left corner, no half-pixel centring anywhere - the source is read at
 *
 *   u = anchorX + ((x - pointX) * cos(a) - (y - pointY) * sin(a)) * 0x10000 / scaleX
 *   v = anchorY + ((x - pointX) * sin(a) + (y - pointY) * cos(a)) * 0x10000 / scaleY
 *
 * (all in pixels here; 0x00416820 does it in doubles once and then walks the view
 * with two 16.16 steps). So the source point (anchorX, anchorY) lands on the view's
 * point (pointX, pointY), the source is scaled about that anchor and turned about it.
 * The anchor is in the source's own pixels, measured from its top left corner; the
 * point is in the destination view's pixels, measured from the view's top left
 * corner - a caller that draws into part of a bitmap narrows the view first and
 * subtracts the view's corner from the point, which is what 0x0041E260 does with
 * the screen's clip (layer position - (clip corner << 16)).
 *
 * The angle is degrees in 16.16: a full turn is 360 << 16 = 0x01680000 (0x004166E0
 * tests `angle % 0x1680000`, 0x00416820 divides by 180 * 65536 = 11796480.0 and
 * multiplies by pi). Positive angles turn the image counter-clockwise on screen
 * (the source's +x axis goes to the screen's -y). The scales are unsigned 16.16
 * (0x10000 = 1:1, 0x20000 = twice the size); either one 0 is refused with 0x13.
 *
 * Sampling. `smooth` 0 is nearest: the source pixel floor(u), floor(v). `smooth`
 * non-zero is bilinear on four fraction bits per axis (the fraction's top nibble),
 * and a neighbour outside the source counts as 0x00000000, so edges fade out through
 * transparent black. Destination pixels whose sample is wholly outside the source
 * are written as 0 by 0x00417800 (and by 0x004169B0 for a 24-bit source at no
 * transparency); see each function for the rest.
 *
 * Fast path (0x004166E0): when the positions are whole pixels, the angle a whole
 * number of turns, both scales exactly 0x10000 and the source covers the whole view,
 * both routines skip the walk and blit the covering part of the source straight
 * across (0x004188D0 / 0x0040B320).
 *
 * Banding. `banded` non-zero lets the original hand the view to its worker pool
 * (0x00419EB0) in horizontal bands, each drawn by a call of the same routine with
 * `banded` 0, the band's own view and the point moved up by the band's first row.
 * Only when a pool is installed (0x00565B1C) and it has at least two workers; the
 * worker count is the machine's processor count. Each band recomputes its start in
 * doubles instead of accumulating the row step from the top, so a banded draw can
 * differ from an unbanded one by one 16.16 step, and the original's output therefore
 * depends on the player's processor count. Xform_SetWorkerCount picks the count that
 * is reproduced here; 0 (the default) is "no pool installed" and every draw is one
 * piece. The bands are drawn one after another - they do not overlap, so the order
 * does not matter.
 *
 * Results: 0 for everything the original returns 0 for (including the many cases
 * where it draws nothing at all), XFORM_RESULT_ZERO_SCALE (0x13, the original's own)
 * for a zero scale, and XFORM_RESULT_REFUSED for a case this port declines, always
 * with a printf naming the routine.
 */

#define XFORM_RESULT_OK          0
#define XFORM_RESULT_ZERO_SCALE  0x13
#define XFORM_RESULT_REFUSED     (-1)

// A full turn in the angle unit both routines take.
#define XFORM_FULL_TURN          0x01680000

// How many workers the original's pool would have (see Banding above). 0 or 1
// means no banding at all.
void Xform_SetWorkerCount(int workers);

/*
 * 0x00417800: the source turned / zoomed onto the destination view, replacing every
 * pixel of the view.
 *
 *   destination  the view drawn into (its pixels, size, stride and mode are used)
 *   pointX/Y     16.16, where in the view the source's anchor lands
 *   source       the source view
 *   anchorX/Y    16.16, the point of the source that lands on (pointX, pointY)
 *   angle        16.16 degrees, full turn 0x01680000, counter-clockwise on screen
 *   scaleX/Y     unsigned 16.16 zoom, 0x10000 = 1:1; 0 refused with 0x13
 *   weight       darkening towards black, 0 = none .. 0x100 = black; compared
 *                unsigned, so anything from 0x100 up clears the view to 0
 *   smooth       0 nearest, non-zero bilinear
 *   banded       non-zero allows the pool's bands (0x0041E260 and the sprite draw
 *                pass 1; the banded worker passes 0)
 *
 * Pixel modes: the destination and source both 24-bit or both 32-bit (all four
 * bytes of each pixel are carried, the fourth included); a 24-bit source onto a
 * 32-bit destination (fourth byte set to 0xFF per source pixel read, weight
 * ignored). Any other pair draws nothing. The fast path copies any pair
 * 0x0040AF50 has an arm for (same mode, 24 onto anything 32-bit, 32 flattened onto
 * 24) when weight is 0, and darkens only same-mode 24/32 pairs otherwise.
 */
int Xform_DrawCopy(Bitmap_t* destination, int32_t pointX, int32_t pointY,
                   const Bitmap_t* source, int32_t anchorX, int32_t anchorY,
                   int32_t angle, uint32_t scaleX, uint32_t scaleY,
                   uint32_t weight, int smooth, int banded);

/*
 * 0x004169B0: the source turned / zoomed and blended onto a 24-bit destination view.
 * The arguments are Xform_DrawCopy's, with `weight` now a uniform transparency:
 * 0 = opaque .. 0x100 = invisible (compared unsigned, so 0x100 and up draw nothing).
 *
 * Pixel modes: the destination must be 24-bit (mode 1) or nothing is drawn.
 *  - 32-bit source: per-pixel alpha blend with the transparency folded in; source
 *    pixels whose alpha is below 2, and destination pixels the source does not
 *    reach, are left alone. The destination's fourth byte is left alone.
 *  - 24-bit source at transparency 0: the same walk as Xform_DrawCopy, so the whole
 *    view is replaced and what the source does not reach becomes 0.
 *  - 24-bit source at any other transparency: every view pixel is moved from the
 *    destination towards the source by (0x100 - transparency) / 0x100 - including
 *    the pixels the source does not reach, which are moved towards 0 (black). The
 *    destination's fourth byte is left alone.
 * The fast path hands the covering part of the source to the 0x0040B320 family,
 * which has arms for every pair of 16/24/32-bit modes it knows.
 */
int Xform_DrawBlend(Bitmap_t* destination, int32_t pointX, int32_t pointY,
                    const Bitmap_t* source, int32_t anchorX, int32_t anchorY,
                    int32_t angle, uint32_t scaleX, uint32_t scaleY,
                    uint32_t transparency, int smooth, int banded);

#endif
