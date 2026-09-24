#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sprite5.h"
#include "process.h"
#include "xform.h"

/*
 * Sprites of kind 5 (fureraba.exe, the sprite class 0x00425790 / vtable 0x004E4F24).
 * See sprite5.h for the model and the units. Every function below names the
 * original routine it is, and keeps its order of operations; the x87 code is written
 * with doubles (the MSVC runtime's 53-bit precision control), the MMX/SSE code word by
 * word with the same wrap-arounds, shifts and saturations.
 */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

// root+0x40 / root+0x44 (0x00442F50). The root's constructor writes (-1, -1).
static int32_t gProjectionCentreX = -1;
static int32_t gProjectionCentreY = -1;

void Sprite5_SetProjectionCentre(int32_t x, int32_t y)
{
	gProjectionCentreX = x;
	gProjectionCentreY = y;
}

// ----------------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------------

// _ftol2 (0x004AD5F0): truncation to 64 bits, of which every caller here keeps the low
// dword. A NaN or a value out of range gives the integer indefinite 0x8000000000000000,
// whose low dword is 0.
static int32_t Sprite5_Ftol(double value)
{
	if(!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
		return 0;
	return (int32_t)(uint32_t)(uint64_t)(int64_t)value;
}

// pmulhw of one word.
static int16_t Sprite5_MulHigh(int16_t a, int16_t b)
{
	return (int16_t)(((int32_t)a * (int32_t)b) >> 16);
}

// packuswb of one word.
static uint32_t Sprite5_PackByte(int32_t value)
{
	int16_t word = (int16_t)value;
	if(word < 0)
		return 0;
	if(word > 0xFF)
		return 0xFF;
	return (uint32_t)word;
}

static uint32_t* Sprite5_Row(const Bitmap_t* bitmap, int row)
{
	return (uint32_t*)(bitmap->bitmap + (size_t)row * (size_t)bitmap->stride);
}

// 0x00409030: a descriptor for pixels of its own. The original leaves the pixels as
// malloc (0x004AB518) gives them; they are cleared here, which differs only where
// the original would read memory nothing wrote. 1 when there are pixels.
static int Sprite5_Allocate(Bitmap_t* bitmap, int width, int height, int mode)
{
	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->width  = width;
	bitmap->height = height;
	bitmap->mode   = mode;
	bitmap->stride = Renderer_ModePixelBytes(mode) * width;   // 0x00407B30
	if(width == 0 || height == 0 || bitmap->stride <= 0 || height < 0)
		return 0;
	bitmap->bitmap = (uint8_t*)calloc((size_t)bitmap->stride, (size_t)height);
	return bitmap->bitmap != NULL;
}

// 0x00429090 / 0x004291D0: free the pixels and clear the six dwords.
static void Sprite5_Release(Bitmap_t* bitmap)
{
	if(bitmap->bitmap != NULL)
		free(bitmap->bitmap);
	memset(bitmap, 0, sizeof(*bitmap));
}

// 0x00407F20: the six-dword descriptor of a table slot; 0 when the slot is empty
// (and `out` is left alone, which is why 0x0042A730 clears it itself).
static int Sprite5_Resolve(Renderer_t* renderer, int32_t id, Bitmap_t* out)
{
	Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, id);
	if(bitmap == NULL)
		return 0;
	*out = *bitmap;
	return 1;
}

static int Sprite5_IsKind5(const DisplayObject_t* sprite)
{
	return sprite->type == OBJECT_TYPE_SPRITE && sprite->kind == 5;
}

struct Sprite5* Sprite5_State(DisplayObject_t* sprite, int useProjectionCentre)
{
	if(sprite->sprite5 == NULL)
	{
		struct Sprite5* state = (struct Sprite5*)calloc(1, sizeof(struct Sprite5));
		if(state == NULL)
			return NULL;
		// 0x0041A4D0 ends with 0x0041C180(second argument): +0x100.
		state->useProjectionCentre = (uint32_t)useProjectionCentre;
		// 0x00425790: +0x154 = -1; +0x220, +0x2E4, +0x300, +0x308, +0x30C.. all 0.
		state->secondBitmap = -1;
		sprite->sprite5 = state;
	}
	return sprite->sprite5;
}

void Sprite5_Teardown(DisplayObject_t* sprite)
{
	if(sprite->sprite5 == NULL)
		return;
	Sprite5_Release(&sprite->sprite5->crossfade);   // 0x00429090
	Sprite5_Release(&sprite->sprite5->wave);        // 0x004291D0
}

// 0x0042AAA0: the eight mip levels freed and both blocks cleared.
static void Sprite5_FreeLevels(struct Sprite5* state)
{
	for(int level = 0; level < 4; level++)
	{
		free(state->levelsFirst[level].bitmap);
		free(state->levelsSecond[level].bitmap);
	}
	memset(state->levelsFirst, 0, sizeof(state->levelsFirst));
	memset(state->levelsSecond, 0, sizeof(state->levelsSecond));
}

void Sprite5_Free(DisplayObject_t* sprite)
{
	if(sprite == NULL || sprite->sprite5 == NULL)
		return;
	// 0x00425910: 0x0042AAA0, then the five teardown calls.
	Sprite5_FreeLevels(sprite->sprite5);
	Sprite5_Teardown(sprite);
	free(sprite->sprite5);
	sprite->sprite5 = NULL;
}

// ----------------------------------------------------------------------------------
// Pixel work
// ----------------------------------------------------------------------------------

/*
 * 0x00419070 -> 0x00419090: `source` halved into `destination` (whose size is
 * ((w + 1) >> 1, (h + 1) >> 1)). Both must be of one pixel mode, 24- or 32-bit. Each
 * 2x2 block is the per-byte sum >> 2; an odd last column averages its two rows, an odd
 * last row its two columns (>> 1), and the odd corner is copied. All four bytes.
 */
static void Sprite5_Halve(const Bitmap_t* source, Bitmap_t* destination)
{
	if(destination->mode != source->mode || (uint32_t)(source->mode - 1) > 1)
		return;
	uint32_t halfWidth = (uint32_t)source->width >> 1;
	uint32_t halfHeight = (uint32_t)source->height >> 1;
	uint32_t odd = (((uint32_t)source->height & 1) << 1) | ((uint32_t)source->width & 1);
	// Both loops are `dec; jne`: a zero count would run 2^32 times. The callers only
	// halve bitmaps of at least 2x2.
	if(halfWidth == 0 || halfHeight == 0 || destination->bitmap == NULL)
		return;

	uint32_t row = 0;
	for(; row < halfHeight; row++)
	{
		const uint32_t* top = Sprite5_Row(source, (int)(row * 2));
		const uint32_t* bottom = Sprite5_Row(source, (int)(row * 2 + 1));
		uint32_t* out = Sprite5_Row(destination, (int)row);
		for(uint32_t column = 0; column < halfWidth; column++)
		{
			uint32_t pixel = 0;
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				uint32_t sum = ((top[column * 2] >> shift) & 0xFF) + ((top[column * 2 + 1] >> shift) & 0xFF)
				             + ((bottom[column * 2] >> shift) & 0xFF) + ((bottom[column * 2 + 1] >> shift) & 0xFF);
				pixel |= Sprite5_PackByte((int32_t)(sum >> 2)) << shift;
			}
			out[column] = pixel;
		}
		if(odd & 1)
		{
			uint32_t pixel = 0;
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				uint32_t sum = ((top[halfWidth * 2] >> shift) & 0xFF) + ((bottom[halfWidth * 2] >> shift) & 0xFF);
				pixel |= Sprite5_PackByte((int32_t)(sum >> 1)) << shift;
			}
			out[halfWidth] = pixel;
		}
	}
	if(odd & 2)
	{
		const uint32_t* last = Sprite5_Row(source, (int)(row * 2));
		uint32_t* out = Sprite5_Row(destination, (int)row);
		for(uint32_t column = 0; column < halfWidth; column++)
		{
			uint32_t pixel = 0;
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				uint32_t sum = ((last[column * 2] >> shift) & 0xFF) + ((last[column * 2 + 1] >> shift) & 0xFF);
				pixel |= Sprite5_PackByte((int32_t)(sum >> 1)) << shift;
			}
			out[column] = pixel;
		}
		if(odd & 1)
			out[halfWidth] = last[halfWidth * 2];
	}
}

/*
 * 0x00418540 -> 0x00418570: every row of `source` moved sideways by a sine, into the
 * wider `destination`. For the row r:
 *   offset = (ftol(sin((r + phase) * 2pi / period) * (width * amplitude)) >> 1)
 *            + ((source width - destination width) << 15)
 * in 16.16 source pixels; the whole part (offset >> 16) is where the row's reading
 * starts, and the top nibble of the fraction ((offset >> 12) & 15 = f) mixes each
 * pixel with the next: out = next + (this - next) * (16 - f) / 16, per byte, all four.
 * The source pointer advances only when a column inside the source is read, so a
 * positive start does not skip pixels - it reads from the row's first one on - and
 * columns outside read 0. Both of the original's loops (pairs at 0x00418712 when
 * 0x00565AFC is set and the row is 8 or wider, singles at 0x0041880B) compute this.
 */
static void Sprite5_WaveRows(const Bitmap_t* source, Bitmap_t* destination,
                             uint32_t period, uint32_t phase, uint32_t amplitude)
{
	if(destination->mode != source->mode || (uint32_t)(source->mode - 1) > 1)
		return;
	if(destination->bitmap == NULL || destination->height <= 0)
		return;

	int32_t* offsets = (int32_t*)malloc(sizeof(int32_t) * (size_t)destination->height);
	if(offsets == NULL)
		return;
	int32_t base = (int32_t)((uint32_t)(source->width - destination->width) << 15);
	double periodRows = (double)period;
	double reach = (double)((uint32_t)source->width * amplitude);   // imul, low dword
	for(uint32_t row = 0; row < (uint32_t)destination->height; row++)
	{
		double angle = ((double)(uint32_t)(row + phase) * 6.283185307179586) / periodRows;   // 0x004EC960
		offsets[row] = (Sprite5_Ftol(sin(angle) * reach) >> 1) + base;
	}

	uint32_t sourceWidth = (uint32_t)source->width;
	for(int row = 0; row < destination->height; row++)
	{
		int32_t offset = offsets[row];
		int32_t weight = (int32_t)(16 - ((offset >> 12) & 0xF)) << 8;
		uint32_t column = (uint32_t)(offset >> 16);
		const uint32_t* in = Sprite5_Row(source, row);
		uint32_t* out = Sprite5_Row(destination, row);

		uint32_t previous = 0;
		if(column < sourceWidth)
			previous = *in++;
		column++;
		for(int x = 0; x < destination->width; x++)
		{
			uint32_t next = 0;
			if(column < sourceWidth)
				next = *in++;
			column++;
			uint32_t pixel = 0;
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				int16_t p = (int16_t)((previous >> shift) & 0xFF);
				int16_t n = (int16_t)((next >> shift) & 0xFF);
				int16_t difference = (int16_t)((uint16_t)(p - n) << 4);
				int16_t moved = Sprite5_MulHigh(difference, (int16_t)weight);
				pixel |= Sprite5_PackByte((int16_t)(moved + n)) << shift;
			}
			out[x] = pixel;
			previous = next;
		}
	}
	free(offsets);
}

/*
 * 0x0040C1B0: two 24-bit (held in four bytes) images crossfaded into a third,
 * per byte, all four: out = first + pmulhw((second - first) << 4, level << 4).
 * Level 0 is the first image, 0x100 the second. The size is the smaller of the two
 * sources'.
 */
static void Sprite5_Crossfade24(Bitmap_t* destination, const Bitmap_t* first,
                                const Bitmap_t* second, uint32_t level)
{
	int height = first->height < second->height ? first->height : second->height;
	int width = (uint32_t)first->width < (uint32_t)second->width ? first->width : second->width;
	int16_t weight = (int16_t)(uint16_t)(level << 4);   // shl 4, cwde, broadcast
	for(int row = 0; row < height; row++)
	{
		const uint32_t* a = Sprite5_Row(first, row);
		const uint32_t* b = Sprite5_Row(second, row);
		uint32_t* out = Sprite5_Row(destination, row);
		for(int column = 0; column < width; column++)
		{
			uint32_t pixel = 0;
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				int16_t x = (int16_t)((a[column] >> shift) & 0xFF);
				int16_t y = (int16_t)((b[column] >> shift) & 0xFF);
				int16_t difference = (int16_t)((uint16_t)(y - x) << 4);
				pixel |= Sprite5_PackByte((int16_t)(x + Sprite5_MulHigh(difference, weight))) << shift;
			}
			out[column] = pixel;
		}
	}
}

/*
 * 0x0040C430: two 32-bit images crossfaded into a third, alpha-weighted:
 *   S = alpha1 * (0x100 - level) + alpha2 * level              (pmaddwd)
 *   f = trunc((alpha1 * (0x100 - level) << 7) * rcp(S))        (0..128)
 *   colour = second + (((first - second) * f) >> 7)            (16-bit words)
 *   alpha = S >> 8
 * The original's rcpps is an approximation good to about 12 bits whose last bits
 * depend on the processor; this uses the exact single-precision reciprocal, so f
 * can differ from a given machine's by one where the product lands near a whole
 * number. Rows of an odd width go one pixel at a time and write 0 only when S is 0;
 * rows of an even width go in pairs and write 0 whenever S >> 8 is 0 - the original's
 * two loops really do differ there.
 */
static uint32_t Sprite5_Crossfade32Pixel(uint32_t a, uint32_t b, int16_t inverse, int16_t level, int pairs)
{
	int32_t alphaA = (int32_t)(a >> 24), alphaB = (int32_t)(b >> 24);
	int32_t partA = alphaA * inverse;
	int32_t sum = partA + alphaB * level;
	if(!pairs && ((uint32_t)sum & 0xFFFF) == 0)
		return 0;

	float numerator = (float)(int32_t)((uint32_t)partA << 7);   // pslld 7, cvtdq2ps
	float reciprocal = 1.0f / (float)sum;                         // rcpps (see above)
	float quotient = numerator * reciprocal;                      // mulps
	int32_t truncated;
	if(!(quotient > -2147483648.0f && quotient < 2147483648.0f))
		truncated = (int32_t)0x80000000u;                         // cvttps2dq's indefinite
	else
		truncated = (int32_t)quotient;
	uint32_t f = (uint32_t)truncated & 0xFFFF;                    // pextrw
	if(f > 0x80)
	{
		printf("[Sprite5]: Warning: 0x0040C430 indexes past its 0x81-entry table (%u)\n", f);
		f = 0;
	}

	uint32_t alphaWord = ((uint32_t)sum << 8) >> 16;               // pandn of S << 8
	uint32_t pixel = 0;
	for(int byte = 0; byte < 3; byte++)
	{
		int shift = byte * 8;
		int16_t x = (int16_t)((a >> shift) & 0xFF);
		int16_t y = (int16_t)((b >> shift) & 0xFF);
		int16_t product = (int16_t)(uint16_t)((uint32_t)(uint16_t)(int16_t)(x - y) * f);   // pmullw
		pixel |= Sprite5_PackByte((int16_t)((product >> 7) + y)) << shift;
	}
	pixel |= Sprite5_PackByte((int16_t)alphaWord) << 24;
	if(pairs && !((int32_t)((alphaWord & 0xFFFF) << 16) > 0))   // pcmpgtd, pand
		return 0;
	return pixel;
}

static void Sprite5_Crossfade32(Bitmap_t* destination, const Bitmap_t* first,
                                const Bitmap_t* second, uint32_t level)
{
	int height = (uint32_t)first->height < (uint32_t)second->height ? first->height : second->height;
	int width = (uint32_t)first->width < (uint32_t)second->width ? first->width : second->width;
	int16_t inverse = (int16_t)(uint16_t)(0x100 - level);
	int16_t weight = (int16_t)(uint16_t)level;
	int pairs = (width & 1) == 0;
	for(int row = 0; row < height; row++)
	{
		const uint32_t* a = Sprite5_Row(first, row);
		const uint32_t* b = Sprite5_Row(second, row);
		uint32_t* out = Sprite5_Row(destination, row);
		for(int column = 0; column < width; column++)
			out[column] = Sprite5_Crossfade32Pixel(a[column], b[column], inverse, weight, pairs);
	}
}

// 0x0040C0F0: the crossfade into `destination`, whose mode must be 24- or 32-bit
// and the sources' own. 0, 9 (modes differ) or 0xA (a mode it has no arm for). The
// worker pool (0x00419EB0) only splits it in bands; the result is the same.
static int Sprite5_CrossfadeInto(Bitmap_t* destination, const Bitmap_t* first,
                                 const Bitmap_t* second, uint32_t level)
{
	if(destination->mode != BITMAP_MODE_24 && destination->mode != BITMAP_MODE_32)
		return 0xA;
	if(first->mode != second->mode || destination->mode != first->mode)
		return 9;
	if(destination->bitmap == NULL)
		return 0;
	if(destination->mode == BITMAP_MODE_24)
		Sprite5_Crossfade24(destination, first, second, level);   // 0x0040C1B0
	else
		Sprite5_Crossfade32(destination, first, second, level);   // 0x0040C430
	return 0;
}

/*
 * 0x0040BD60 -> 0x0040BDB0: two 32-bit images crossfaded straight onto a 24-bit view,
 * with a transparency. Each source is premultiplied by its alpha and (0x100 -
 * transparency): colour words by T(alpha) = ((alpha * (0x100 - t)) >> 8) << 4 and the
 * alpha word by (0x100 - t) << 4, all through pmulhw of the byte << 4; the two are
 * mixed by level (x + pmulhw((y - x) << 4, level << 4)); where the mixed alpha m is 0
 * the view is left alone, elsewhere view = pmulhw(view << 4, (0x100 - m) << 4) + mix,
 * the fourth byte 0. 0, 9 (sources not 32-bit) or 0xA (the view not 24-bit); nothing
 * is drawn at a transparency of 0x100 or more. The size is the smaller source's.
 */
static int Sprite5_CrossfadeOnto24(Bitmap_t* target, const Bitmap_t* first, const Bitmap_t* second,
                                   uint32_t level, uint32_t transparency)
{
	if(target->mode != BITMAP_MODE_24)
		return 0xA;
	if(first->mode != BITMAP_MODE_32 || second->mode != BITMAP_MODE_32)
		return 9;
	if(transparency >= 0x100)
		return 0;

	uint16_t colourWeight[0x101];
	uint16_t keep[0x101];
	uint32_t step = 0x100 - transparency, accumulated = 0;
	for(int alpha = 0; alpha <= 0x100; alpha++)
	{
		colourWeight[alpha] = (uint16_t)((accumulated >> 8) << 4);
		keep[alpha] = (uint16_t)((uint32_t)(0x100 - alpha) << 4);
		accumulated += step;
	}
	int16_t alphaWeight = (int16_t)(uint16_t)(step << 4);
	int16_t mix = (int16_t)(uint16_t)(level << 4);   // shl 4, movsx

	int height = (uint32_t)first->height < (uint32_t)second->height ? first->height : second->height;
	int width = (uint32_t)first->width < (uint32_t)second->width ? first->width : second->width;
	for(int row = 0; row < height; row++)
	{
		const uint32_t* a = Sprite5_Row(first, row);
		const uint32_t* b = Sprite5_Row(second, row);
		uint32_t* out = Sprite5_Row(target, row);
		for(int column = 0; column < width; column++)
		{
			int16_t words[4];
			for(int byte = 0; byte < 4; byte++)
			{
				int shift = byte * 8;
				int16_t wa = byte < 3 ? (int16_t)colourWeight[a[column] >> 24] : alphaWeight;
				int16_t wb = byte < 3 ? (int16_t)colourWeight[b[column] >> 24] : alphaWeight;
				int16_t x = Sprite5_MulHigh((int16_t)(((a[column] >> shift) & 0xFF) << 4), wa);
				int16_t y = Sprite5_MulHigh((int16_t)(((b[column] >> shift) & 0xFF) << 4), wb);
				int16_t difference = (int16_t)((uint16_t)(y - x) << 4);
				words[byte] = (int16_t)(x + Sprite5_MulHigh(difference, mix));
			}
			uint32_t mixedAlpha = (uint16_t)words[3];
			if(mixedAlpha == 0)
				continue;
			if(mixedAlpha > 0x100)
				mixedAlpha = 0x100;   // not reachable: the mix of two premultiplied alphas
			uint32_t pixel = 0;
			for(int byte = 0; byte < 3; byte++)
			{
				int shift = byte * 8;
				int16_t kept = Sprite5_MulHigh((int16_t)(((out[column] >> shift) & 0xFF) << 4),
				                               (int16_t)keep[mixedAlpha]);
				pixel |= Sprite5_PackByte((int16_t)(kept + words[byte])) << shift;
			}
			out[column] = pixel;
		}
	}
	return 0;
}

// ----------------------------------------------------------------------------------
// Geometry
// ----------------------------------------------------------------------------------

// 0x0041B590: the accumulated position vector, (+0x4C) + (+0x5C) + (+0x6C), three
// components.
static void Sprite5_Position(const struct Sprite5* state, int32_t out[3])
{
	for(int i = 0; i < 3; i++)
		out[i] = (int32_t)((uint32_t)state->position[i] + (uint32_t)state->position2[i]
		                   + (uint32_t)state->position3[i]);
}

/*
 * 0x0041AB70: the scale a depth gives, 16.16. 0x10000 for no depth or no focal
 * distance; otherwise, with _alldiv (0x004C7220):
 *   z >= 0:  (focal << 32) / ((focal << 16) + z)
 *   z <  0:  ((focal << 16) - z) / focal
 */
static uint32_t Sprite5_DepthScale(int32_t z, uint32_t focal)
{
	if(z == 0 || focal == 0)
		return 0x10000;
	if(z >= 0)
		return (uint32_t)((int64_t)((uint64_t)focal << 32) / (int64_t)(((uint64_t)focal << 16) + (uint64_t)(int64_t)z));
	return (uint32_t)((int64_t)(((uint64_t)focal << 16) - (uint64_t)(int64_t)z) / (int64_t)(uint64_t)focal);
}

/*
 * 0x004297A0: the transform after the second effect parameter e (+0xB8, whole):
 *   anchor = anchor + (delta anchor * e) >> 24
 *   angle  = angle + (ease(+0x2D0, e) * delta angle) >> 16
 *   scale, with +0x274 set:   (((dScale + dScaleXY) * e >> 24) + ownScale) * depthScale >> 16
 *          with +0x274 clear: ((dScaleXY * e >> 24) + depthScale) * (ownScale + (dScale * e >> 24)) >> 16
 * where dScale is +0x2CC and dScaleXY +0x2C4 / +0x2C8. The products are _allmul
 * (0x004B7740) of a sign-extended delta and the zero-extended e, shifted as 64-bit
 * signed values; the scales are zero-extended.
 */
static void Sprite5_Transform(const DisplayObject_t* sprite, const struct Sprite5* state,
                              int32_t anchor[2], int32_t* angle, uint32_t* scaleX, uint32_t* scaleY,
                              int32_t baseAngle, uint32_t depthScaleX, uint32_t depthScaleY,
                              uint32_t ownScaleX, uint32_t ownScaleY, int32_t anchorX, int32_t anchorY)
{
	uint64_t e = (uint64_t)sprite->fieldB8;   // 0x0041B820, mode 1
	anchor[0] = (int32_t)((uint32_t)(((uint64_t)(int64_t)state->deltaAnchorX * e) >> 24) + (uint32_t)anchorX);
	anchor[1] = (int32_t)((uint32_t)(((uint64_t)(int64_t)state->deltaAnchorY * e) >> 24) + (uint32_t)anchorY);
	int32_t eased = Process_Ease(state->angleCurve, (int32_t)(uint32_t)e);   // 0x0041A760
	*angle = (int32_t)((uint32_t)(((uint64_t)((int64_t)eased * (int64_t)state->deltaAngle)) >> 16) + (uint32_t)baseAngle);

	if(state->scaleYGiven != 0)
	{
		int64_t sumX = (int64_t)state->deltaScaleX + (int64_t)state->deltaScale;
		int64_t sumY = (int64_t)state->deltaScaleY + (int64_t)state->deltaScale;
		int64_t mX = ((int64_t)((uint64_t)sumX * e) >> 24) + (int64_t)(uint64_t)ownScaleX;
		int64_t mY = ((int64_t)((uint64_t)sumY * e) >> 24) + (int64_t)(uint64_t)ownScaleY;
		*scaleX = (uint32_t)(((uint64_t)mX * (uint64_t)depthScaleX) >> 16);
		*scaleY = (uint32_t)(((uint64_t)mY * (uint64_t)depthScaleY) >> 16);
		return;
	}
	int64_t shared = (int64_t)((uint64_t)(int64_t)state->deltaScale * e) >> 24;
	int64_t pX = ((int64_t)((uint64_t)(int64_t)state->deltaScaleX * e) >> 24) + (int64_t)(uint64_t)depthScaleX;
	int64_t qX = (int64_t)(uint64_t)ownScaleX + shared;
	*scaleX = (uint32_t)(((uint64_t)pX * (uint64_t)qX) >> 16);
	int64_t pY = ((int64_t)((uint64_t)(int64_t)state->deltaScaleY * e) >> 24) + (int64_t)(uint64_t)depthScaleY;
	int64_t qY = (int64_t)(uint64_t)ownScaleY + shared;
	*scaleY = (uint32_t)(((uint64_t)pY * (uint64_t)qY) >> 16);
}

// 0x004296BC-style roundings of 0x00429300: the whole part towards minus infinity
// and towards plus infinity, deciding "is there a fraction" on the value's 16.16
// truncation, as the original does.
static int32_t Sprite5_Floor16(double value)
{
	if(0.0 > value || value != value)
	{
		int32_t fraction = ((Sprite5_Ftol(value * 65536.0) & 0xFFFF) + 0xFFFF) >> 16;
		return Sprite5_Ftol(value) - fraction;
	}
	return Sprite5_Ftol(value);
}

static int32_t Sprite5_Ceil16(double value)
{
	if(!(value < 0.0))
	{
		int32_t fraction = ((Sprite5_Ftol(value * 65536.0) & 0xFFFF) + 0xFFFF) >> 16;
		return fraction + Sprite5_Ftol(value);
	}
	return Sprite5_Ftol(value);
}

/*
 * 0x00429300: the box the transformed bitmap covers. The bitmap (stretched by the wave
 * amplitude, 1 + wave / 0x10000, and centred on it) is measured from its anchor with y
 * upwards, its four corners scaled and turned, and the extremes taken against the
 * sentinels 1e9 / -1e9 (0x004EC928 / 0x004EC920). The 16.16 fractions of position *
 * scale then widen the right edge and lower the bottom one. The answer: the width and
 * height, and where the anchor is inside the box (x from the left, y from the top).
 */
static void Sprite5_Box(uint32_t* width, uint32_t* height, int32_t offset[2],
                        uint32_t sourceWidth, uint32_t sourceHeight, uint32_t wave,
                        int32_t angle, uint32_t scaleX, uint32_t scaleY,
                        int32_t positionX, int32_t positionY, int32_t anchorX, int32_t anchorY)
{
	const double unit = 1.52587890625e-05;     // 0x004EC930
	const double pi = 3.141592653589793;       // 0x004EC8F8

	double stretched = (double)(uint32_t)((wave + 0x10000u) * sourceWidth) * unit;
	double sourceRows = (double)sourceHeight;
	double pointX = ((stretched - (double)sourceWidth) * 0.5) + (double)anchorX * unit;
	double pointY = unit * (double)anchorY;
	double theta = ((double)angle * pi) / 11796480.0;   // 0x004EC908
	double c = cos(theta);                              // 0x004D5850
	double s = sin(theta);                              // 0x004D5980
	double sx = (double)scaleX * unit;
	double sy = (double)scaleY * unit;

	double left = -pointX;
	double right = (stretched - pointX) - 1.0;
	double top = pointY;
	double bottom = 1.0 + (pointY - sourceRows);

	double x[4], y[4];
	x[0] = left * sx * c - top * sy * s;       y[0] = left * sx * s + top * sy * c;
	x[1] = right * sx * c - top * sy * s;      y[1] = top * sy * c + right * sx * s;
	x[2] = left * sx * c - bottom * sy * s;    y[2] = left * sx * s + bottom * sy * c;
	x[3] = right * sx * c - bottom * sy * s;   y[3] = right * sx * s + bottom * sy * c;

	double minX = 1000000000.0, maxX = -1000000000.0;
	double minY = 1000000000.0, maxY = -1000000000.0;
	for(int i = 0; i < 4; i++)
	{
		if(x[i] < minX) minX = x[i];
		if(x[i] > maxX) maxX = x[i];
		if(y[i] < minY) minY = y[i];
		if(y[i] > maxY) maxY = y[i];
	}

	uint32_t fractionX = (uint32_t)(((uint64_t)(int64_t)positionX * (uint64_t)scaleX) >> 16) & 0xFFFF;
	uint32_t fractionY = (uint32_t)(((uint64_t)(int64_t)positionY * (uint64_t)scaleY) >> 16) & 0xFFFF;
	maxX += (double)fractionX * unit;
	minY -= (double)fractionY * unit;

	int32_t first = Sprite5_Floor16(minX);
	int32_t last = Sprite5_Floor16(maxX);
	int32_t upper = Sprite5_Ceil16(maxY);
	int32_t lower = Sprite5_Ceil16(minY);
	*width = (uint32_t)(last - first + 1);
	*height = (uint32_t)(upper - lower + 1);
	offset[0] = -first;
	offset[1] = upper;
}

/*
 * 0x00429BD0: the projection. The centre is the drawing device's middle (0x0041C170,
 * width and height shifted right once) or, when +0x100 is set, the root's projection
 * centre if that is on the device (0x0041C1A0 -> 0x00442F70). With perspective the
 * position is scaled by its depth (0x0041AB70 with +0x278). The whole part becomes the
 * base position (vtable+0x28, 0x0041B2A0 with both flags clear), the fraction +0x2A4.
 */
static void Sprite5_Project(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	int32_t position[3];
	Sprite5_Position(state, position);
	Bitmap_t* device = Renderer_BackBuffer(renderer);
	uint32_t deviceWidth = device != NULL ? (uint32_t)device->width : 0;
	uint32_t deviceHeight = device != NULL ? (uint32_t)device->height : 0;
	uint32_t centreX = deviceWidth >> 1;
	uint32_t centreY = deviceHeight >> 1;
	if(state->useProjectionCentre != 0
	   && gProjectionCentreX >= 0 && gProjectionCentreX < (int32_t)deviceWidth
	   && gProjectionCentreY >= 0 && gProjectionCentreY < (int32_t)deviceHeight)
	{
		centreX = (uint32_t)gProjectionCentreX;
		centreY = (uint32_t)gProjectionCentreY;
	}

	int32_t x, y;
	if(state->perspective != 0)
	{
		uint32_t scale = Sprite5_DepthScale(position[2], state->focal);
		x = (int32_t)((centreX << 16) + (uint32_t)(((uint64_t)(int64_t)position[0] * (uint64_t)scale) >> 16));
		y = (int32_t)((centreY << 16) + (uint32_t)(((uint64_t)(int64_t)position[1] * (uint64_t)scale) >> 16));
	}
	else
	{
		x = (int32_t)((centreX << 16) + (uint32_t)position[0]);
		y = (int32_t)((centreY << 16) + (uint32_t)position[1]);
	}
	Object_SetBasePosition(sprite, x >> 16, y >> 16, 0, 0);
	state->fractionX = (uint32_t)x & 0xFFFF;
	state->fractionY = (uint32_t)y & 0xFFFF;
}

/*
 * 0x0042A730: the four mip levels of each bitmap rebuilt from scratch (0x0042AAA0
 * first): level i is the one before it halved (0x00419070), made only while the one
 * before has pixels and is at least 2x2.
 */
static void Sprite5_BuildLevels(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	Sprite5_FreeLevels(state);
	Bitmap_t first, second;
	if(!Sprite5_Resolve(renderer, sprite->bitmapId, &first))
		memset(&first, 0, sizeof(first));
	if(!Sprite5_Resolve(renderer, state->secondBitmap, &second))
		memset(&second, 0, sizeof(second));

	const Bitmap_t* fromFirst = &first;
	const Bitmap_t* fromSecond = &second;
	for(int level = 0; level < 4; level++)
	{
		if(fromFirst->bitmap != NULL && (uint32_t)fromFirst->width >= 2 && (uint32_t)fromFirst->height >= 2)
		{
			if(Sprite5_Allocate(&state->levelsFirst[level], (int)(((uint32_t)fromFirst->width + 1) >> 1),
			                    (int)(((uint32_t)fromFirst->height + 1) >> 1), fromFirst->mode))
				Sprite5_Halve(fromFirst, &state->levelsFirst[level]);
		}
		if(fromSecond->bitmap != NULL && (uint32_t)fromSecond->width >= 2 && (uint32_t)fromSecond->height >= 2)
		{
			if(Sprite5_Allocate(&state->levelsSecond[level], (int)(((uint32_t)fromSecond->width + 1) >> 1),
			                    (int)(((uint32_t)fromSecond->height + 1) >> 1), fromSecond->mode))
				Sprite5_Halve(fromSecond, &state->levelsSecond[level]);
		}
		fromFirst = &state->levelsFirst[level];
		fromSecond = &state->levelsSecond[level];
	}
}

/*
 * 0x0042AB00 for kind 5: how many times the drawn width is halved, from the drawn
 * X scale (+0x29C): none above 0x8000, then one more for every halving of 0x8000
 * that is still at least the scale. A zero scale never ends the original's loop; it
 * is stopped here when the step reaches 0.
 */
static uint32_t Sprite5_LevelCount(const struct Sprite5* state)
{
	uint32_t count = 0, step = 0x8000;
	if(state->drawScaleX > step)
		return 0;
	do
	{
		step >>= 1;
		count++;
		if(step == 0)
		{
			printf("[Sprite5]: Warning: 0x0042AB00 never returns for an X scale of 0\n");
			break;
		}
	}
	while(step >= state->drawScaleX);
	return count;
}

/*
 * 0x0042AB80: the descriptor to draw from - the bitmap (+0x150, or +0x154 for
 * `second`), replaced by the deepest mip level 0x0042AB00 asks for (at most 4) that
 * has pixels. `*levels` is how many levels that is. Answers whether the bitmap slot
 * resolved at all.
 */
static int Sprite5_LevelView(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state,
                             Bitmap_t* out, uint32_t* levels, int second)
{
	int found = Sprite5_Resolve(renderer, second ? state->secondBitmap : sprite->bitmapId, out);
	uint32_t count = Sprite5_LevelCount(state);
	if(found && count != 0)
	{
		const Bitmap_t* level = second ? state->levelsSecond : state->levelsFirst;
		if(count > 4)
			count = 4;
		for(uint32_t i = 0; i < count; i++)
		{
			if(level[i].bitmap == NULL)
			{
				count = i;
				break;
			}
			*out = level[i];
		}
	}
	if(levels != NULL)
		*levels = count;
	return found;
}

// The transform is the identity for the draw's dispatch (0x004259FC) and 0x00428F72:
// no angle, 1:1, no fraction, and not both of the wave's +0x300 and +0x308.
static int Sprite5_IsIdentity(const struct Sprite5* state)
{
	return state->drawAngle == 0 && state->drawScaleX == 0x10000 && state->drawScaleY == 0x10000
	    && state->fractionX == 0 && state->fractionY == 0
	    && (state->wavePeriod == 0 || state->waveAmplitude == 0);
}

/*
 * 0x00428F50: the crossfade cache (+0x220) of the two bitmaps at the draw's mip level.
 * Unforced, an identity transform needs none (arm 1 crossfades as it draws) and a
 * cache made at the current +0x240 is kept.
 */
static void Sprite5_Crossfade(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state, int force)
{
	if(sprite->kind != 5)
	{
		if(sprite->kind == 6)
			printf("[Sprite5]: Warning: 0x00428F50 for a sprite of kind 6 is not ported\n");
		return;
	}
	if(!force)
	{
		if(Sprite5_IsIdentity(state))
			return;
		if(state->crossfadeLevel == sprite->field240)
			return;
	}
	Sprite5_Release(&state->crossfade);   // 0x00429090
	if(state->secondBitmap == -1)
		return;
	Bitmap_t first, second;
	uint32_t levels = 0;
	if(!Sprite5_LevelView(renderer, sprite, state, &first, &levels, 0))
		return;
	if(!Sprite5_LevelView(renderer, sprite, state, &second, NULL, 1))
		return;
	if(Renderer_BitmapSerial(renderer, sprite->bitmapId) != sprite->bitmapSerial)
		return;
	if(Renderer_BitmapSerial(renderer, state->secondBitmap) != state->secondSerial)
		return;
	Sprite5_Allocate(&state->crossfade, first.width, first.height, first.mode);
	Sprite5_CrossfadeInto(&state->crossfade, &first, &second, sprite->field240);   // 0x0040C0F0
	state->crossfadeLevel = sprite->field240;
	state->crossfadeShift = levels;
}

/*
 * 0x004290E0: the wave image (+0x30C) of the bitmap's mip level, or of the crossfade
 * cache for two bitmaps, widened by an even number of columns to hold the swing:
 * width + ((((0x10000 + amplitude) * width) >> 16) - width + 1 & ~1). Period and phase
 * are shifted down by the mip level; the amplitude is not.
 */
static void Sprite5_Wave(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	if(sprite->kind != 5 && sprite->kind != 6)
		return;
	Sprite5_Release(&state->wave);   // 0x004291D0
	if(state->wavePeriod == 0 || state->waveAmplitude == 0)
		return;
	Bitmap_t view;
	uint32_t levels = 0;
	if(!Sprite5_LevelView(renderer, sprite, state, &view, &levels, 0))
		return;
	const Bitmap_t* source;
	if(state->secondBitmap == -1)
	{
		if(Renderer_BitmapSerial(renderer, sprite->bitmapId) != sprite->bitmapSerial)
			return;
		source = &view;
	}
	else
	{
		if(state->crossfade.bitmap == NULL)
			return;
		source = &state->crossfade;
	}
	uint32_t width = (uint32_t)source->width;
	uint32_t grown = (((state->waveAmplitude + 0x10000u) * width) >> 16);
	grown = ((grown - width + 1) & ~1u) + width;
	Sprite5_Allocate(&state->wave, (int)grown, source->height, source->mode);
	uint32_t shift = levels & 31;
	Sprite5_WaveRows(source, &state->wave, state->wavePeriod >> shift, state->wavePhase >> shift,
	                 state->waveAmplitude);
}

/*
 * 0x00429A80: the transform, the box and the projection made again from the current
 * fields (after a position, the angle, a scale, the wave or the second effect
 * parameter changed). The depth scale uses +0x278 unless +0x88 is set (0x0041BFD0).
 * A box narrower or lower than 2 leaves everything as it was.
 */
static void Sprite5_Rebuild(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	Bitmap_t bitmap;
	if(!Sprite5_Resolve(renderer, sprite->bitmapId, &bitmap))
		return;
	int32_t position[3];
	Sprite5_Position(state, position);
	uint32_t focal = sprite->field88 != 0 ? 0 : state->focal;
	uint32_t depth = Sprite5_DepthScale(position[2], focal);
	int32_t anchor[2], angle;
	uint32_t scaleX, scaleY;
	Sprite5_Transform(sprite, state, anchor, &angle, &scaleX, &scaleY, state->angle, depth, depth,
	                  state->scaleX, state->scaleY, state->anchorX, state->anchorY);
	uint32_t wave = state->wavePeriod != 0 ? state->waveAmplitude : 0;
	uint32_t width, height;
	int32_t offset[2];
	Sprite5_Box(&width, &height, offset, (uint32_t)bitmap.width, (uint32_t)bitmap.height, wave,
	            angle, scaleX, scaleY, position[0], position[1], anchor[0], anchor[1]);
	if(width < 2 || height < 2)
		return;
	state->drawAnchorX = anchor[0];
	state->drawAnchorY = anchor[1];
	state->drawAngle = angle;
	state->drawScaleX = scaleX;
	state->drawScaleY = scaleY;
	state->surfaceWidth = width;
	state->surfaceHeight = height;
	state->offsetX = offset[0];
	state->offsetY = offset[1];
	Sprite5_Project(renderer, sprite, state);
	Object_SetSurfaceSize(renderer, sprite, (int)width, (int)height);   // vtable+0x74
}

// The three calls every kind 5 change ends in: 0x00429A80, 0x00428F50(0), 0x004290E0.
static void Sprite5_Refresh(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	Sprite5_Rebuild(renderer, sprite, state);
	Sprite5_Crossfade(renderer, sprite, state, 0);
	Sprite5_Wave(renderer, sprite, state);
}

// ----------------------------------------------------------------------------------
// Content
// ----------------------------------------------------------------------------------

uint32_t Sprite5_SetContent(Renderer_t* renderer, DisplayObject_t* sprite,
                            int32_t bitmap, int32_t second, uint32_t level, int32_t contentKind,
                            int32_t anchorX, int32_t anchorY, int32_t angle, uint32_t focal,
                            uint32_t perspective, uint32_t smooth)
{
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return SPRITE5_BAD_BITMAP;

	// 0x00427B70.
	Bitmap_t first, other;
	if(!Sprite5_Resolve(renderer, bitmap, &first))
		return SPRITE5_BAD_BITMAP;
	int hasSecond = second != -1;
	if(hasSecond)
	{
		if(!Sprite5_Resolve(renderer, second, &other))
			return SPRITE5_BAD_SECOND;
		if(first.width != other.width || first.height != other.height || first.mode != other.mode)
			return SPRITE5_MISMATCH;
	}

	int32_t anchorX16 = (int32_t)((uint32_t)anchorX << 16);
	int32_t anchorY16 = (int32_t)((uint32_t)anchorY << 16);
	int32_t position[3];
	Sprite5_Position(state, position);
	uint32_t depth = Sprite5_DepthScale(position[2], focal);
	// The transform with the sprite's own scales reset to 1:1. It reads the deltas and
	// +0x274 as they are now, before they are written below.
	int32_t anchor[2], drawAngle;
	uint32_t scaleX, scaleY;
	Sprite5_Transform(sprite, state, anchor, &drawAngle, &scaleX, &scaleY, angle, depth, depth,
	                  0x10000, 0x10000, anchorX16, anchorY16);
	uint32_t wave = state->wavePeriod != 0 ? state->waveAmplitude : 0;
	uint32_t width, height;
	int32_t offset[2];
	Sprite5_Box(&width, &height, offset, (uint32_t)first.width, (uint32_t)first.height, wave,
	            drawAngle, scaleX, scaleY, position[0], position[1], anchor[0], anchor[1]);
	if(width < 2 || height < 2)
		return SPRITE5_TOO_SMALL;

	// 0x00429090, 0x004291D0, 0x00429220, 0x00429240, 0x00429290: of these only the
	// first two free anything this module keeps.
	Sprite5_Teardown(sprite);

	state->secondBitmap = second;
	sprite->kind = 5;
	sprite->bitmapId = bitmap;
	sprite->bitmapSerial = Renderer_BitmapSerial(renderer, bitmap);
	state->secondSerial = hasSecond ? Renderer_BitmapSerial(renderer, second) : 0;
	sprite->field240 = hasSecond ? level : 0;
	state->crossfadeLevel = sprite->field240;
	sprite->contentKind = hasSecond ? contentKind : -1;
	state->anchorX = anchorX16;
	state->scaleX = 0x10000;
	state->scaleY = 0x10000;
	state->anchorY = anchorY16;
	state->angle = angle;
	state->focal = focal;
	state->perspective = perspective;
	state->smooth = smooth;
	state->drawAnchorX = anchor[0];
	state->drawAnchorY = anchor[1];
	state->perspectiveScaleX = depth;
	state->perspectiveScaleY = depth;
	state->drawAngle = drawAngle;
	state->drawScaleX = scaleX;
	state->scaleYGiven = 0;
	state->drawScaleY = scaleY;
	state->surfaceWidth = width;
	state->surfaceHeight = height;
	state->offsetX = offset[0];
	state->offsetY = offset[1];

	Sprite5_BuildLevels(renderer, sprite, state);   // 0x0042A730
	Sprite5_Project(renderer, sprite, state);       // 0x00429BD0
	Sprite5_Crossfade(renderer, sprite, state, 1);  // 0x00428F50(1)
	Sprite5_Wave(renderer, sprite, state);          // 0x004290E0
	Object_SetSurfaceSize(renderer, sprite, (int)width, (int)height);   // vtable+0x74
	return 0;
}

uint32_t Sprite5_SetContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int32_t bitmap)
{
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return SPRITE5_BAD_BITMAP;
	// 0x00427424: movsx of the words at +0x24A and +0x24E - the whole parts.
	return Sprite5_SetContent(renderer, sprite, bitmap, -1, 0, -1,
	                          (int16_t)((uint32_t)state->anchorX >> 16),
	                          (int16_t)((uint32_t)state->anchorY >> 16),
	                          state->angle, state->focal, state->perspective, state->smooth);
}

uint32_t Sprite5_Setup(Renderer_t* renderer, DisplayObject_t* sprite, int useProjectionCentre,
                       int32_t x, int32_t y, int32_t z, int32_t bitmap, int32_t second,
                       uint32_t level, int32_t contentKind, int32_t anchorX, int32_t anchorY,
                       int32_t angle, uint32_t focal, uint32_t perspective, uint32_t smooth,
                       uint32_t blendMode, uint32_t effectLevel, uint32_t layer,
                       const char** unread)
{
	const char* ignored = NULL;
	if(unread == NULL)
		unread = &ignored;
	*unread = NULL;

	struct Sprite5* state = Sprite5_State(sprite, useProjectionCentre);
	if(state == NULL)
		return SPRITE5_BAD_BITMAP;

	// 0x00427240.
	state->deltaAnchorX = 0;
	state->deltaAnchorY = 0;
	state->deltaAngle = 0;
	state->deltaScaleX = 0;
	state->deltaScaleY = 0;
	state->deltaScale = 0;
	state->angleCurve = 0;
	sprite->contentKind = -1;
	sprite->depthFromSerial = 0;   // 0x0041BF70 with 0: +0x7C
	// vtable+0x3C with the sprite's kind as it still is.
	const char* name = Sprite5_SetPosition3D(renderer, sprite, x, y, z);
	if(name != NULL)
		*unread = name;
	sprite->unknownA8 = blendMode;   // 0x0041B6D0
	// vtable+0x48: with +0x244 now -1, 0x00428450 is the base (0x0041B6F0) unless the
	// kind is 4. Object_ApplyEffectLevel adds object.c's damage count around it.
	name = Object_ApplyEffectLevel(sprite, effectLevel);
	if(name != NULL)
		*unread = name;
	// vtable+0x54 (0x0041B980): refused at 0x10000 and above. The caller re-sorts
	// the list (0x0043EC40) or files the sprite afterwards (0x0042C0D2).
	if(layer < 0x10000)
		sprite->layer = layer;
	return Sprite5_SetContent(renderer, sprite, bitmap, second, level, contentKind,
	                          anchorX, anchorY, angle, focal, perspective, smooth);
}

// ----------------------------------------------------------------------------------
// Virtuals
// ----------------------------------------------------------------------------------

void Sprite5_ScreenPosition(DisplayObject_t* sprite, int32_t* x, int32_t* y)
{
	if(sprite->type == OBJECT_TYPE_SPRITE && sprite->kind == 6)
		printf("[Sprite5]: Warning: 0x004282C6, the kind 6 screen position, is not ported\n");
	// 0x0041B330: 0x0041B310 (+0x30) + 0x0041B3E0 (+0x38) + 0x0041B430 (+0x40). The
	// fourth term, 0x0041C1B0, adds 0x00565B34 / 0x00565B38 when +0x48 is set; what
	// writes that pair is not read, and it is taken as 0 as object.c takes it.
	*x = (int32_t)((uint32_t)sprite->baseX + (uint32_t)sprite->x + (uint32_t)sprite->originX);
	*y = (int32_t)((uint32_t)sprite->baseY + (uint32_t)sprite->y + (uint32_t)sprite->originY);
	if(sprite->type == OBJECT_TYPE_SPRITE && (sprite->kind == 2 || sprite->kind == 5)
	   && sprite->sprite5 != NULL)
	{
		*x -= sprite->sprite5->offsetX;
		*y -= sprite->sprite5->offsetY;
	}
}

void Sprite5_LocalBounds(const DisplayObject_t* sprite, Rect_t* rect)
{
	// 0x0041B200.
	rect->left = 0;
	rect->top = 0;
	rect->right = sprite->surfaceWidth - 1;
	rect->bottom = sprite->surfaceHeight - 1;
}

void Sprite5_ScreenBounds(DisplayObject_t* sprite, Rect_t* rect)
{
	// 0x0041B230.
	Sprite5_LocalBounds(sprite, rect);
	int32_t x, y;
	Sprite5_ScreenPosition(sprite, &x, &y);
	Renderer_OffsetRect(rect, x, y);
}

void Sprite5_GetPosition3D(DisplayObject_t* sprite, int32_t out[4])
{
	struct Sprite5* state = sprite->sprite5;
	for(int i = 0; i < 4; i++)
		out[i] = state != NULL ? state->position[i] : 0;
}

// The kind arms of vtable+0x3C and vtable+0x44 (0x004283A6, 0x00428406).
static const char* Sprite5_AfterMove(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state)
{
	if(sprite->type != OBJECT_TYPE_SPRITE)
		return NULL;
	if(sprite->kind == 5)
	{
		Sprite5_Refresh(renderer, sprite, state);
		return NULL;
	}
	if(sprite->kind == 6)
		return "0x00428F50(1) / 0x004290E0 / 0x0042A170, a kind 6 sprite moved";
	return NULL;
}

const char* Sprite5_SetPosition3D(Renderer_t* renderer, DisplayObject_t* sprite,
                                  int32_t x, int32_t y, int32_t z)
{
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return NULL;

	// 0x0041B440. Snapping (0x0041BFA0: +0x80, and +0x84 into the local) rounds x and
	// y to whole pixels when there is no depth or +0x84 is 1.
	if(sprite->snap != 0 && (z == 0 || sprite->snapValue == 1))
	{
		x = (int32_t)(((uint32_t)x + 0x8000u) & 0xFFFF0000u);
		y = (int32_t)(((uint32_t)y + 0x8000u) & 0xFFFF0000u);
	}
	int32_t old[3] = { state->position[0], state->position[1], state->position[2] };
	state->position[0] = x;
	state->position[1] = y;
	state->position[2] = z;
	if(sprite->depthFromSerial != 0)
		Object_SetBasePosition(sprite, x >> 16, y >> 16, 1, 1);   // vtable+0x28 (1, 1)

	// Every child keeps its offset from the parent: its own vtable+0x40, then its
	// vtable+0x3C with the parent's move added.
	const char* unread = NULL;
	for(ObjectChild_t* node = sprite->children; node != NULL; node = node->next)
	{
		int32_t v[4];
		Sprite5_GetPosition3D(node->child, v);
		const char* name = Sprite5_SetPosition3D(renderer, node->child,
		                                         (int32_t)((uint32_t)v[0] - (uint32_t)old[0] + (uint32_t)x),
		                                         (int32_t)((uint32_t)v[1] - (uint32_t)old[1] + (uint32_t)y),
		                                         (int32_t)((uint32_t)v[2] - (uint32_t)old[2] + (uint32_t)z));
		if(unread == NULL)
			unread = name;
	}

	const char* name = Sprite5_AfterMove(renderer, sprite, state);
	return unread != NULL ? unread : name;
}

// 0x0041B5F0 on the object and every child (through the child's own vtable+0x44).
static void Sprite5_Position2Walk(Renderer_t* renderer, DisplayObject_t* object,
                                  int32_t x, int32_t y, int32_t z, const char** unread)
{
	struct Sprite5* state = Sprite5_State(object, 1);
	if(state == NULL)
		return;
	state->position2[0] = x;
	state->position2[1] = y;
	state->position2[2] = z;
	for(ObjectChild_t* node = object->children; node != NULL; node = node->next)
	{
		Sprite5_Position2Walk(renderer, node->child, x, y, z, unread);
		const char* name = Sprite5_AfterMove(renderer, node->child, node->child->sprite5);
		if(*unread == NULL)
			*unread = name;
	}
}

const char* Sprite5_SetPosition2(Renderer_t* renderer, DisplayObject_t* sprite,
                                 int32_t x, int32_t y, int32_t z)
{
	const char* unread = NULL;
	Sprite5_Position2Walk(renderer, sprite, x, y, z, &unread);
	struct Sprite5* state = sprite->sprite5;
	if(state == NULL)
		return unread;
	const char* name = Sprite5_AfterMove(renderer, sprite, state);
	return unread != NULL ? unread : name;
}

int Sprite5_SetEffectLevel(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t level)
{
	// 0x00428450, content kinds 1 (0x00428472) and 2 (0x0042848E, after the base).
	if(sprite->contentKind != 1 && sprite->contentKind != 2)
		return 0;
	sprite->field240 = level;
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return 1;
	Sprite5_Crossfade(renderer, sprite, state, 0);
	if(sprite->kind == 5)
		Sprite5_Wave(renderer, sprite, state);
	else if(sprite->kind == 6)
		printf("[Sprite5]: Warning: 0x004290E0 for a sprite of kind 6 is not ported\n");
	return 1;
}

void Sprite5_Effect2Changed(Renderer_t* renderer, DisplayObject_t* sprite)
{
	// 0x00428599.
	if(!Sprite5_IsKind5(sprite))
		return;
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return;
	if(sprite->contentKind == 3)
		sprite->field240 = Object_GetEffect2(sprite);   // 0x0041B810
	Sprite5_Refresh(renderer, sprite, state);
}

int Sprite5_SetParameter(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t number,
                         uint32_t value1, uint32_t value2, uint32_t* result, const char** unread)
{
	const char* ignored = NULL;
	if(unread == NULL)
		unread = &ignored;
	*unread = NULL;
	if(sprite->type != OBJECT_TYPE_SPRITE)
		return 0;
	uint32_t kind = sprite->kind;
	int spatial = kind == 2 || kind == 5 || kind == 6;
	*result = OBJECT_PARAM_OK;

	switch(number)
	{
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x60:
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x8F:
		break;
	default:
		return 0;
	}
	struct Sprite5* state = Sprite5_State(sprite, 1);
	if(state == NULL)
		return 1;

	switch(number)
	{
	case 0x40:
		// 0x004280F0: the anchor, whole pixels, and nothing rebuilt.
		if(spatial)
		{
			state->anchorX = (int32_t)(value1 << 16);
			state->anchorY = (int32_t)(value2 << 16);
		}
		return 1;
	case 0x41:
		// 0x00428130: the angle.
		if(kind == 5)
		{
			state->angle = (int32_t)value1;
			Sprite5_Rebuild(renderer, sprite, state);
			Sprite5_Crossfade(renderer, sprite, state, 0);
		}
		else if(kind == 2)
		{
			state->angle = (int32_t)value1;
			*unread = "0x00429960, a kind 2 sprite's angle";
			*result = OBJECT_PARAM_UNREAD;
		}
		else if(kind == 6)
		{
			*unread = "+0x25C and 0x0042A170, a kind 6 sprite's angle";
			*result = OBJECT_PARAM_UNREAD;
		}
		return 1;
	case 0x42:
		// 0x00428200: the sprite's own scales; 0 is 1 for X, and a Y of 0 is X's.
		if(spatial)
		{
			if(value1 == 0)
				value1 = 1;
			if(value2 == 0)
			{
				value2 = value1;
				state->scaleYGiven = 0;
			}
			else
				state->scaleYGiven = 1;
			state->scaleX = value1;
			state->scaleY = value2;
			if(kind == 5)
				Sprite5_Refresh(renderer, sprite, state);
			else
			{
				*unread = kind == 2 ? "0x00429960, a kind 2 sprite's scale" : "0x0042A170, a kind 6 sprite's scale";
				*result = OBJECT_PARAM_UNREAD;
			}
		}
		return 1;
	case 0x60:
		// 0x00428610, whatever the kind: period (value1 >> 16), phase (value1 & 0xFFFF),
		// amplitude (value2).
		state->wavePeriod = value1 >> 16;
		state->wavePhase = value1 & 0xFFFF;
		state->waveAmplitude = value2;
		if(kind == 5)
		{
			Sprite5_Wave(renderer, sprite, state);
			Sprite5_Rebuild(renderer, sprite, state);
		}
		else if(kind == 6)
		{
			*unread = "0x004290E0 / 0x0042A170, a kind 6 sprite's wave";
			*result = OBJECT_PARAM_UNREAD;
		}
		return 1;
	case 0x80:
		if(spatial)
		{
			state->deltaAnchorX = (int32_t)value1;
			state->deltaAnchorY = (int32_t)value2;
		}
		return 1;
	case 0x81:
		if(kind == 2 || kind == 5)
		{
			state->deltaAngle = (int32_t)value1;
			state->deltaScale = (int32_t)value2;
		}
		else if(kind == 6)
		{
			state->deltaScale = (int32_t)value2;
			*unread = "+0x2C0, a kind 6 sprite's field";
			*result = OBJECT_PARAM_UNREAD;
		}
		return 1;
	case 0x82:
		if(spatial)
		{
			state->deltaScaleX = (int32_t)value1;
			state->deltaScaleY = (int32_t)value2;
		}
		return 1;
	case 0x8F:
		state->angleCurve = value1;
		return 1;
	}
	return 0;
}

int Sprite5_GetParameter(Renderer_t* renderer, DisplayObject_t* sprite, uint32_t code,
                         int32_t* out, uint32_t* result)
{
	if(sprite->type != OBJECT_TYPE_SPRITE)
		return 0;
	struct Sprite5* state = sprite->sprite5;
	uint32_t kind = sprite->kind;
	if(code == 0x41)
	{
		// 0x004281A0: the angle for kinds 2 and 5.
		if((kind == 2 || kind == 5) && state != NULL)
		{
			out[0] = state->angle;
			*result = 0;
		}
		else if(kind == 6)
		{
			printf("[Sprite5]: Warning: 0x004281B5, a kind 6 sprite's +0x25C, is not ported\n");
			*result = 0xFFFF0001u;
		}
		else
			*result = 0xFFFF0001u;
		return 1;
	}
	if(code == 0x10000000)
	{
		if(kind == 5 && state != NULL)
		{
			int32_t anchor[2], angle;
			uint32_t scaleX, scaleY;
			Sprite5_Transform(sprite, state, anchor, &angle, &scaleX, &scaleY, state->angle,
			                  state->perspectiveScaleX, state->perspectiveScaleY,
			                  state->scaleX, state->scaleY, state->anchorX, state->anchorY);
			out[0] = anchor[0];
			out[1] = anchor[1];
			out[2] = angle;
			out[3] = (int32_t)scaleX;
			out[4] = (int32_t)scaleY;
			*result = 0;
		}
		else if(kind == 6)
		{
			printf("[Sprite5]: Warning: 0x00429CD0, a kind 6 sprite's transform, is not ported\n");
			*result = 0xFFFF0001u;
		}
		else
			*result = 0xFFFF0001u;
		return 1;
	}
	if(code == 0x10000100)
	{
		Bitmap_t bitmap;
		if(Sprite5_Resolve(renderer, sprite->bitmapId, &bitmap))
		{
			out[0] = bitmap.width;
			out[1] = bitmap.height;
		}
		if((kind == 2 || kind == 5 || kind == 6) && state != NULL)
		{
			out[2] = (int32_t)state->surfaceWidth;
			out[3] = (int32_t)state->surfaceHeight;
		}
		else
		{
			out[2] = out[0];
			out[3] = out[1];
		}
		*result = 0;
		return 1;
	}
	return 0;
}

int Sprite5_HitPoint(DisplayObject_t* sprite, int32_t x, int32_t y, int32_t* u, int32_t* v)
{
	if(sprite->type != OBJECT_TYPE_SPRITE)
		return -1;
	if(sprite->kind == 2 || sprite->kind == 6)
		return 0;
	if(sprite->kind != 5 || sprite->sprite5 == NULL)
		return -1;
	struct Sprite5* state = sprite->sprite5;

	// 0x00428BD3.
	double theta = -((((double)state->drawAngle * 1.52587890625e-05) * 3.141592653589793) / 180.0);
	double c = cos(theta);
	double s = sin(theta);
	double across = (double)(int32_t)((uint32_t)x - (uint32_t)state->offsetX);
	double up = (double)(int32_t)((uint32_t)state->offsetY - (uint32_t)y);
	int32_t row = Sprite5_Ftol(((across * s + up * c) * 65536.0) / (double)state->drawScaleY);
	int32_t column = Sprite5_Ftol(((across * c - s * up) * -65536.0) / (double)state->drawScaleX);
	*v = (int32_t)(int16_t)((uint32_t)state->anchorY >> 16) - row;
	*u = (int32_t)(int16_t)((uint32_t)state->anchorX >> 16) - column;
	return 1;
}

int Sprite5_IsSpatial(const DisplayObject_t* sprite)
{
	return sprite->type == OBJECT_TYPE_SPRITE && (sprite->kind == 5 || sprite->kind == 6);
}

uint32_t Sprite5_DepthTerm(DisplayObject_t* sprite)
{
	int32_t position[3] = { 0, 0, 0 };
	if(sprite->sprite5 != NULL)
		Sprite5_Position(sprite->sprite5, position);
	return (0xFFFu - (uint32_t)(position[2] >> 19)) & 0x1FFF;
}

void Sprite5_BitmapChanged(Renderer_t* renderer, DisplayObject_t* sprite, const Rect_t* rect)
{
	struct Sprite5* state = sprite->sprite5;
	if(!Sprite5_IsKind5(sprite) || state == NULL)
		return;
	if(rect == NULL)
	{
		Sprite5_BuildLevels(renderer, sprite, state);   // 0x0042A730
		return;
	}

	// 0x0042A850: the rectangle made at least one pixel wide and high, rounded out to
	// even corners, halved into each level in turn.
	Bitmap_t first, second;
	if(!Sprite5_Resolve(renderer, sprite->bitmapId, &first))
		memset(&first, 0, sizeof(first));
	if(!Sprite5_Resolve(renderer, state->secondBitmap, &second))
		memset(&second, 0, sizeof(second));
	int32_t left = rect->left, top = rect->top, right = rect->right, bottom = rect->bottom;
	const Bitmap_t* fromFirst = &first;
	const Bitmap_t* fromSecond = &second;
	for(int level = 0; level < 4; level++)
	{
		if(right - left <= 0)
		{
			if(left >= 1)
				left--;
			else
				right++;
		}
		if(bottom - top <= 0)
		{
			if(top >= 1)
				top--;
			else
				bottom++;
		}
		Rect_t from = { left & ~1, top & ~1, right | 1, bottom | 1 };
		Rect_t to = { from.left >> 1, from.top >> 1, from.right >> 1, from.bottom >> 1 };
		const Bitmap_t* sources[2] = { fromFirst, fromSecond };
		Bitmap_t* levels[2] = { &state->levelsFirst[level], &state->levelsSecond[level] };
		for(int which = 0; which < 2; which++)
		{
			if(sources[which]->bitmap == NULL)
				continue;
			Bitmap_t view = *sources[which];
			if(!Renderer_ClipBitmap(&view, &from) || (uint32_t)view.width < 2 || (uint32_t)view.height < 2)
				continue;
			Bitmap_t into = *levels[which];
			if(into.bitmap == NULL)
				continue;   // the original would write through a null level here
			Renderer_ClipBitmap(&into, &to);   // its answer is not looked at
			Sprite5_Halve(&view, &into);
		}
		left = to.left;
		top = to.top;
		right = to.right;
		bottom = to.bottom;
		fromFirst = &state->levelsFirst[level];
		fromSecond = &state->levelsSecond[level];
	}
}

// ----------------------------------------------------------------------------------
// The draw (vtable+0x18, 0x004259A0)
// ----------------------------------------------------------------------------------

// 0x0042AC80: any of the sixteen filters at +0x358 (parameter 0x11, 0x0042AC30).
// Nothing in this engine sets one - object.c refuses parameter 0x11 by name - so the
// original's filter step (0x0042ACC0 -> 0x00419AA0) is never reached here.
static int Sprite5_HasFilters(const DisplayObject_t* sprite)
{
	(void)sprite;
	return 0;
}

// Arm 0 (0x00425A7B): one bitmap, identity transform - the kind 0 arm.
static int Sprite5_DrawOne(Renderer_t* renderer, DisplayObject_t* sprite, Bitmap_t* target, const Rect_t* rect)
{
	Bitmap_t view;
	if(!Sprite5_Resolve(renderer, sprite->bitmapId, &view))
		return 1;
	if(Renderer_BitmapSerial(renderer, sprite->bitmapId) != sprite->bitmapSerial)
		return 1;
	if(sprite->maskBitmapId != 0)
	{
		printf("[Sprite5]: Warning: a sprite with a mask draws through 0x00425ADC / 0x00425BB8, which is not ported\n");
		return 0;
	}
	// 0x00425D0D: the bitmap narrowed to the part being drawn (0x004091B0, whose
	// answer is not looked at) and blitted with the blend mode and weight (0x0040A9E0).
	Renderer_ClipBitmap(&view, rect);
	Renderer_BlitView(target, &view, (int)sprite->unknownA8, (int)Object_Weight(sprite));
	return 1;
}

// Arm 1 (0x00425D3D): two bitmaps, identity transform, crossfaded by +0x240.
static int Sprite5_DrawTwo(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state,
                           Bitmap_t* target, const Rect_t* rect)
{
	Bitmap_t first, second;
	if(!Sprite5_Resolve(renderer, sprite->bitmapId, &first))
		return 1;
	if(!Sprite5_Resolve(renderer, state->secondBitmap, &second))
		return 1;
	if(Renderer_BitmapSerial(renderer, sprite->bitmapId) != sprite->bitmapSerial)
		return 1;
	if(Renderer_BitmapSerial(renderer, state->secondBitmap) != state->secondSerial)
		return 1;
	if(sprite->maskBitmapId != 0)
	{
		printf("[Sprite5]: Warning: a two-bitmap sprite with a mask draws through 0x00425EA4, which is not ported\n");
		return 0;
	}
	Renderer_ClipBitmap(&first, rect);
	Renderer_ClipBitmap(&second, rect);
	uint32_t mode = sprite->unknownA8;
	uint32_t weight = Object_Weight(sprite);
	if((mode == 0 || mode == 1 || mode == 0x20) && !Sprite5_HasFilters(sprite))
	{
		// 0x00425DFB: straight onto the view, the weight only for modes 1 and 0x20.
		// Anything but two 32-bit bitmaps onto a 24-bit view draws nothing (9, 0xA).
		Sprite5_CrossfadeOnto24(target, &first, &second, sprite->field240, mode != 0 ? weight : 0);
		return 1;
	}
	// 0x00425E2B: into a buffer of the first's size and mode, then blitted.
	Bitmap_t buffer;
	Sprite5_Allocate(&buffer, first.width, first.height, first.mode);
	Sprite5_CrossfadeInto(&buffer, &first, &second, sprite->field240);
	if(buffer.bitmap != NULL)
		Renderer_BlitView(target, &buffer, (int)mode, (int)weight);
	free(buffer.bitmap);
	return 1;
}

// Arm 5 (0x0042679F): everything else - through the transformed blits.
static int Sprite5_DrawTransformed(Renderer_t* renderer, DisplayObject_t* sprite, struct Sprite5* state,
                                   Bitmap_t* target, const Rect_t* rect)
{
	int32_t pointX = (int32_t)(((uint32_t)(state->offsetX - rect->left) << 16) + state->fractionX);
	int32_t pointY = (int32_t)(((uint32_t)(state->offsetY - rect->top) << 16) + state->fractionY);
	int32_t anchorX = state->drawAnchorX;
	int32_t anchorY = state->drawAnchorY;
	uint32_t scaleY = state->drawScaleY;
	uint32_t scaleX = state->drawScaleX;
	uint32_t shift = 0;
	const Bitmap_t* source = NULL;
	Bitmap_t level;

	if(state->wavePeriod != 0 && state->waveAmplitude != 0)
	{
		// 0x00426812: the wave image, at the level the bitmap's own view is at.
		if(Sprite5_LevelView(renderer, sprite, state, &level, &shift, 0))
		{
			anchorY >>= shift & 31;
			scaleX <<= shift & 31;
			scaleY <<= shift & 31;
			anchorX >>= shift & 31;
			anchorX += (int32_t)(((uint32_t)(state->wave.width - level.width) << 15) & 0xFFFF0000u);
			source = &state->wave;
		}
	}
	else
	{
		if(state->secondBitmap == -1)
		{
			// 0x00426864: the bitmap at its mip level, if its slot is still its own.
			if(Sprite5_LevelView(renderer, sprite, state, &level, &shift, 0)
			   && Renderer_BitmapSerial(renderer, sprite->bitmapId) == sprite->bitmapSerial)
				source = &level;
		}
		else
		{
			// 0x004268B0: the crossfade cache, at the level it was made at.
			source = &state->crossfade;
			shift = state->crossfadeShift;
		}
		anchorY >>= shift & 31;
		scaleX <<= shift & 31;
		anchorX >>= shift & 31;
		scaleY <<= shift & 31;
	}

	// 0x004268DB: below 1:1 on X, the anchor moves half a pixel for the first halving
	// and a quarter, an eighth ... for each after it (0x8000 >> n below 0x8000, else
	// (0x10000 - scale) >> n), on both axes.
	uint32_t scale = scaleX;
	for(int n = 1; scale < 0x10000; n++)
	{
		int32_t move = scale < 0x8000 ? (int32_t)(0x8000 >> n) : (int32_t)((0x10000u - scale) >> n);
		anchorY += move;
		anchorX += move;
		scale += scale;
		if(n >= 32)
			break;   // the original's loop ends when the doubling reaches 0x10000
	}

	if(source == NULL || source->bitmap == NULL)
		return 1;
	if(sprite->maskBitmapId != 0)
	{
		printf("[Sprite5]: Warning: a transformed sprite with a mask draws through 0x00426A19, which is not ported\n");
		return 0;
	}

	uint32_t mode = sprite->unknownA8;
	uint32_t weight = Object_Weight(sprite);
	if((mode == 0 || mode == 1 || mode == 0x20) && target->mode == BITMAP_MODE_24
	   && source->mode == BITMAP_MODE_32 && !Sprite5_HasFilters(sprite))
	{
		// 0x00426975: blended straight on.
		Xform_DrawBlend(target, pointX, pointY, source, anchorX, anchorY, state->drawAngle,
		                scaleX, scaleY, weight, (int)state->smooth, 1);
		return 1;
	}
	// 0x004269C2: into a buffer the size of the part being drawn (a 24-bit source makes
	// a 32-bit buffer, so what it does not cover is transparent), then blitted.
	int bufferMode = source->mode == BITMAP_MODE_24 ? BITMAP_MODE_32 : source->mode;
	Bitmap_t buffer;
	Sprite5_Allocate(&buffer, rect->right - rect->left + 1, rect->bottom - rect->top + 1, bufferMode);
	if(buffer.bitmap == NULL)
		return 1;
	Xform_DrawCopy(&buffer, pointX, pointY, source, anchorX, anchorY, state->drawAngle,
	               scaleX, scaleY, 0, (int)state->smooth, 1);
	Renderer_BlitView(target, &buffer, (int)mode, (int)weight);
	free(buffer.bitmap);
	return 1;
}

int Sprite5_Draw(Renderer_t* renderer, DisplayObject_t* sprite, Bitmap_t* target, const Rect_t* rect)
{
	struct Sprite5* state = sprite->sprite5;
	if(!Sprite5_IsKind5(sprite) || state == NULL)
		return 0;
	// 0x004259AF: +0x148 set draws nothing; the sprite constructor clears it and
	// nothing in this engine sets it (0x00428E60).
	// 0x004259FC: the identity test.
	if(state->drawAngle == 0 && state->drawScaleX == 0x10000 && state->drawScaleY == 0x10000
	   && state->fractionX == 0 && state->fractionY == 0
	   && (state->wavePeriod == 0 || state->waveAmplitude == 0))
	{
		if(state->secondBitmap == -1)
			return Sprite5_DrawOne(renderer, sprite, target, rect);
		return Sprite5_DrawTwo(renderer, sprite, state, target, rect);
	}
	return Sprite5_DrawTransformed(renderer, sprite, state, target, rect);
}
