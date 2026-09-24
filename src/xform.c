#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xform.h"

/*
 * The original's transformed blits, 0x00417800 and 0x004169B0, with everything they
 * reach. The per-pixel work is MMX in the original; it is written out here word by
 * word, with the same wrap-arounds, shifts and saturations, so the pixels come out
 * identical. See xform.h for the arguments and the coordinate conventions.
 *
 * The walk's start and steps come from x87 code (0x00416820) running at the MSVC
 * runtime's default 53-bit precision, which is what C doubles are. Nothing here may be
 * fused into a multiply-add, or the truncations below stop matching.
 */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

static int xformWorkers = 0;

void Xform_SetWorkerCount(int workers)
{
	xformWorkers = workers;
}

// ---------------------------------------------------------------------------------
// The MMX word operations the inner loops are built from.
// ---------------------------------------------------------------------------------

// fistp / _ftol2 (0x004AD5F0) of a double, of which the callers keep the low dword.
static int32_t Xform_Truncate(double value)
{
	return (int32_t)(uint32_t)(uint64_t)(int64_t)value;
}

// packssdw of one dword: the whole-pixel part of a 16.16 coordinate, saturated.
static int32_t Xform_Saturate16(int32_t value)
{
	if(value > 0x7FFF)
		return 0x7FFF;
	if(value < -0x8000)
		return -0x8000;
	return value;
}

// pmulhw: the high word of the signed product.
static int16_t Xform_MulHigh(int16_t a, int16_t b)
{
	return (int16_t)(((int32_t)a * (int32_t)b) >> 16);
}

// packuswb of one word.
static uint32_t Xform_PackByte(int16_t value)
{
	if(value < 0)
		return 0;
	if(value > 0xFF)
		return 0xFF;
	return (uint32_t)value;
}

static void Xform_Unpack(uint32_t pixel, int16_t words[4])
{
	for(int channel = 0; channel < 4; channel++)
		words[channel] = (int16_t)((pixel >> (channel * 8)) & 0xFF);
}

static uint32_t Xform_Pack(const int16_t words[4])
{
	uint32_t pixel = 0;
	for(int channel = 0; channel < 4; channel++)
		pixel |= Xform_PackByte(words[channel]) << (channel * 8);
	return pixel;
}

// The four words of an MMX qword, lowest first.
static void Xform_QwordWords(uint64_t qword, uint16_t words[4])
{
	for(int word = 0; word < 4; word++)
		words[word] = (uint16_t)(qword >> (word * 16));
}

/*
 * The darkening multiplier 0x00417A70, 0x00418230 and 0x00418900 build the same way:
 * _allmul((uint32_t)(0x100 - weight), 0x100010001) + 0x0100000000000000, which is the
 * words { inv, inv, inv, 0x100 } for a weight up to 0x100 - the colours scaled by
 * inv / 256, the fourth byte kept. Each word of the pixel is multiplied by it with
 * pmullw (the low word), shifted down by 8 with psrlw and packed back.
 */
static void Xform_DarkenWords(uint32_t weight, uint16_t words[4])
{
	uint64_t multiplier = (uint64_t)(uint32_t)(0x100u - weight) * 0x100010001ull
	                    + 0x0100000000000000ull;
	Xform_QwordWords(multiplier, words);
}

static uint32_t Xform_Darken(uint32_t pixel, const uint16_t multiplier[4])
{
	uint32_t out = 0;
	for(int channel = 0; channel < 4; channel++)
	{
		uint16_t word = (uint16_t)((pixel >> (channel * 8)) & 0xFF);
		uint16_t product = (uint16_t)((uint32_t)word * multiplier[channel]);
		out |= Xform_PackByte((int16_t)(product >> 8)) << (channel * 8);
	}
	return out;
}

// ---------------------------------------------------------------------------------
// 0x00416820: the walk's start and its two steps.
// ---------------------------------------------------------------------------------

/*
 * Where the view's top left pixel reads the source, and how far one pixel to the
 * right (`column`) and one row down (`row`) move that, all 16.16 as { u, v }. The
 * expressions keep the original's order of operations exactly; the angle is negated
 * because this is the inverse mapping, and the row step is taken from the angle plus
 * pi / 2 rather than from the column step's sine and cosine, as the original does.
 */
static void Xform_Setup(int32_t pointX, int32_t pointY, int32_t anchorX, int32_t anchorY,
                        int32_t angle, uint32_t scaleX, uint32_t scaleY,
                        int32_t start[2], int32_t row[2], int32_t column[2])
{
	const double pi = 3.141592653589793;        // 0x004EC8F8
	const double halfPi = 1.5707963267948966;   // 0x004EC918
	const double toPixels = 1.52587890625e-05;  // 0x004EC930, 1 / 65536

	double theta = -(((double)angle * pi) / 11796480.0);   // 0x004EC908, 180 * 65536
	double c = cos(theta);   // 0x004D5850
	double s = sin(theta);   // 0x004D5980
	// fild of the scale, 2^32 added when its top bit is set: the scale is unsigned.
	double inverseX = 65536.0 / (double)scaleX;
	double inverseY = 65536.0 / (double)scaleY;
	double negativePointX = -((double)pointX * toPixels);
	double pointYPixels = (double)pointY * toPixels;
	double anchorYPixels = (double)anchorY * toPixels;
	double anchorXPixels = (double)anchorX * toPixels;

	start[1] = Xform_Truncate((anchorYPixels - (pointYPixels * c + negativePointX * s) * inverseY) * 65536.0);
	start[0] = Xform_Truncate(((negativePointX * c - pointYPixels * s) * inverseX + anchorXPixels) * 65536.0);
	column[1] = Xform_Truncate((s * inverseY) * -65536.0);   // 0x004EC968
	column[0] = Xform_Truncate((c * inverseX) * 65536.0);

	double phi = theta + halfPi;
	row[1] = Xform_Truncate((sin(phi) * inverseY) * 65536.0);
	row[0] = Xform_Truncate((cos(phi) * inverseX) * -65536.0);
}

// ---------------------------------------------------------------------------------
// The samplers.
// ---------------------------------------------------------------------------------

/*
 * Every walk turns a 16.16 position into the source pixel the same way: psrad by 16,
 * packssdw (so the whole part is saturated to 16 bits) and sign-extended back. The
 * byte offset is then pmaddwd with { 1, stride >> 2 } and shifted left by 2, which is
 * column * 4 + row * stride for every stride below 0x20000 bytes - all of this
 * engine's bitmaps. The bounds are checked unsigned, so a negative whole part is out.
 */
static const uint32_t* Xform_SourcePixel(const Bitmap_t* source, int32_t column, int32_t row)
{
	if((uint32_t)row >= (uint32_t)source->height || (uint32_t)column >= (uint32_t)source->width)
		return NULL;
	return (const uint32_t*)(source->bitmap + (ptrdiff_t)row * source->stride) + column;
}

/*
 * The nearest walks (0x00418350, 0x00418230, 0x00418440, 0x00417200, 0x00417630):
 * the pixel floor(u), floor(v), and whether there is one.
 */
static int Xform_Nearest(const Bitmap_t* source, int32_t u, int32_t v, uint32_t* pixel)
{
	int32_t column = Xform_Saturate16(u >> 16);
	int32_t row = Xform_Saturate16(v >> 16);
	const uint32_t* in = Xform_SourcePixel(source, column, row);
	if(in == NULL)
		return 0;
	*pixel = *in;
	return 1;
}

/*
 * The bilinear walks (0x00417D20, 0x00417A70, 0x00417F90, 0x00416C70, 0x00417330):
 * the four pixels around (u, v), each 0 where it falls outside the source and ORed
 * with `orMask` where it is read (0xFF000000 for the 24-onto-32 walk, which reads a
 * 24-bit source as opaque). The weights come from a sixteen-entry table of
 * (16 - f) << 8 per word, indexed by the top nibble f of each fraction. The walks
 * skip straight to their output when all four pixels are outside, and the several
 * orders of their bounds tests all agree with reading the four pixels one by one,
 * so they are read one by one here; the result is the same word for word.
 *
 * top    = p01 + pmulhw((p00 - p01) << 4, (16 - fu) << 8)
 * bottom = p11 + pmulhw((p10 - p11) << 4, (16 - fu) << 8)
 * out    = bottom + pmulhw((top - bottom) << 4, (16 - fv) << 8)
 */
static void Xform_Bilinear(const Bitmap_t* source, int32_t u, int32_t v, uint32_t orMask,
                           int16_t out[4])
{
	int32_t column = Xform_Saturate16(u >> 16);
	int32_t row = Xform_Saturate16(v >> 16);
	int16_t weightU = (int16_t)((16 - (int)(((uint32_t)u & 0xF000) >> 12)) << 8);
	int16_t weightV = (int16_t)((16 - (int)(((uint32_t)v & 0xF000) >> 12)) << 8);

	uint32_t corner[4] = { 0, 0, 0, 0 };
	for(int i = 0; i < 4; i++)
	{
		const uint32_t* in = Xform_SourcePixel(source, column + (i & 1), row + (i >> 1));
		if(in != NULL)
			corner[i] = *in | orMask;
	}

	int16_t p00[4], p01[4], p10[4], p11[4];
	Xform_Unpack(corner[0], p00);
	Xform_Unpack(corner[1], p01);
	Xform_Unpack(corner[2], p10);
	Xform_Unpack(corner[3], p11);
	for(int channel = 0; channel < 4; channel++)
	{
		int16_t top = (int16_t)(p01[channel]
		            + Xform_MulHigh((int16_t)((p00[channel] - p01[channel]) * 16), weightU));
		int16_t bottom = (int16_t)(p11[channel]
		               + Xform_MulHigh((int16_t)((p10[channel] - p11[channel]) * 16), weightU));
		out[channel] = (int16_t)(bottom + Xform_MulHigh((int16_t)((top - bottom) * 16), weightV));
	}
}

// ---------------------------------------------------------------------------------
// The walks.
// ---------------------------------------------------------------------------------

typedef enum
{
	XFORM_COPY_NEAREST,     // 0x00418350: the source pixel, or 0
	XFORM_COPY_SMOOTH,      // 0x00417D20
	XFORM_DARKEN_NEAREST,   // 0x00418230: the source pixel darkened, or 0
	XFORM_DARKEN_SMOOTH,    // 0x00417A70
	XFORM_OPAQUE_NEAREST,   // 0x00418440: a 24-bit source pixel made opaque, or 0
	XFORM_OPAQUE_SMOOTH,    // 0x00417F90
	XFORM_FADE_NEAREST,     // 0x00417200: the destination moved towards the sample
	XFORM_FADE_SMOOTH,      // 0x00416C70
	XFORM_ALPHA_NEAREST,    // 0x00417630: the sample over the destination by its alpha
	XFORM_ALPHA_SMOOTH      // 0x00417330
} XformWalk_t;

/*
 * One walk over the whole view. Every one of the ten routines has this frame: the
 * start and steps from 0x00416820, the next row's start computed before the row, the
 * position stepped by `column` after each pixel, and `dec / jne` loops - which would
 * run away on an empty view, so an empty view is left alone here instead.
 */
static void Xform_Walk(Bitmap_t* destination, const Bitmap_t* source,
                       int32_t pointX, int32_t pointY, int32_t anchorX, int32_t anchorY,
                       int32_t angle, uint32_t scaleX, uint32_t scaleY,
                       uint32_t weight, XformWalk_t walk)
{
	if(destination->width <= 0 || destination->height <= 0)
		return;

	int32_t start[2], row[2], column[2];
	Xform_Setup(pointX, pointY, anchorX, anchorY, angle, scaleX, scaleY, start, row, column);

	uint16_t darken[4];
	Xform_DarkenWords(weight, darken);

	/*
	 * 0x00416C70 and 0x00417200: sext(inv << 4) + _allmul(sext(inv), 0x0000001000100000),
	 * the words { inv * 16, inv * 16, inv * 16, 0 }, with inv = 0x100 - transparency.
	 * out = d + pmulhw((s - d) << 4, word): d + (s - d) * inv / 256, rounded down, and
	 * the fourth byte left as it was.
	 */
	uint16_t fade[4];
	{
		int64_t inv = (int32_t)(0x100u - weight);
		uint64_t qword = (uint64_t)inv * 0x0000001000100000ull + (uint64_t)(int64_t)(int32_t)((uint32_t)inv << 4);
		Xform_QwordWords(qword, fade);
	}

	/*
	 * 0x00417330 and 0x00417630: a 128-entry table indexed by the sample's alpha >> 1,
	 * entry i being (i * inv) >> 8 in the three colour words (0 in the fourth) - except
	 * the last, which is (inv << 7) >> 8, as if the index were 128. Built with the same
	 * _allmul(w, 0x100010001) as the darkening multiplier.
	 * out = d + (((s - d) * w) >> 7), pmullw then psraw.
	 */
	uint16_t alphaWeight[128];
	{
		uint32_t inv = 0x100u - weight;
		for(uint32_t i = 0; i < 0x7F; i++)
			alphaWeight[i] = (uint16_t)((i * inv) >> 8);
		alphaWeight[0x7F] = (uint16_t)((inv << 7) >> 8);
	}

	int32_t rowU = start[0];
	int32_t rowV = start[1];
	for(int y = 0; y < destination->height; y++)
	{
		uint32_t* out = (uint32_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		int32_t u = rowU;
		int32_t v = rowV;
		rowU = (int32_t)((uint32_t)rowU + (uint32_t)row[0]);
		rowV = (int32_t)((uint32_t)rowV + (uint32_t)row[1]);
		for(int x = 0; x < destination->width; x++)
		{
			int32_t sampleU = u;
			int32_t sampleV = v;
			u = (int32_t)((uint32_t)u + (uint32_t)column[0]);
			v = (int32_t)((uint32_t)v + (uint32_t)column[1]);

			int16_t sample[4];
			uint32_t pixel = 0;
			switch(walk)
			{
				case XFORM_COPY_NEAREST:
					if(!Xform_Nearest(source, sampleU, sampleV, &pixel))
						pixel = 0;
					out[x] = pixel;
					break;
				case XFORM_COPY_SMOOTH:
					Xform_Bilinear(source, sampleU, sampleV, 0, sample);
					out[x] = Xform_Pack(sample);
					break;
				case XFORM_DARKEN_NEAREST:
					out[x] = Xform_Nearest(source, sampleU, sampleV, &pixel)
					       ? Xform_Darken(pixel, darken) : 0;
					break;
				case XFORM_DARKEN_SMOOTH:
					// The words are already unpacked; pmullw / psrlw 8 / packuswb.
					Xform_Bilinear(source, sampleU, sampleV, 0, sample);
					out[x] = Xform_Darken(Xform_Pack(sample), darken);
					break;
				case XFORM_OPAQUE_NEAREST:
					out[x] = Xform_Nearest(source, sampleU, sampleV, &pixel)
					       ? (pixel | 0xFF000000u) : 0;
					break;
				case XFORM_OPAQUE_SMOOTH:
					Xform_Bilinear(source, sampleU, sampleV, 0xFF000000u, sample);
					out[x] = Xform_Pack(sample);
					break;
				case XFORM_FADE_NEAREST:
				case XFORM_FADE_SMOOTH:
				{
					// A sample outside the source is 0, and is still faded in: the
					// destination is darkened there by the same share.
					if(walk == XFORM_FADE_SMOOTH)
						Xform_Bilinear(source, sampleU, sampleV, 0, sample);
					else
					{
						if(!Xform_Nearest(source, sampleU, sampleV, &pixel))
							pixel = 0;
						Xform_Unpack(pixel, sample);
					}
					int16_t d[4];
					Xform_Unpack(out[x], d);
					for(int channel = 0; channel < 4; channel++)
						sample[channel] = (int16_t)(d[channel]
						    + Xform_MulHigh((int16_t)((sample[channel] - d[channel]) * 16),
						                    (int16_t)fade[channel]));
					out[x] = Xform_Pack(sample);
					break;
				}
				case XFORM_ALPHA_NEAREST:
				case XFORM_ALPHA_SMOOTH:
				{
					// A sample outside the source, or one whose alpha >> 1 is 0, leaves
					// the destination pixel alone.
					if(walk == XFORM_ALPHA_SMOOTH)
					{
						Xform_Bilinear(source, sampleU, sampleV, 0, sample);
						pixel = Xform_Pack(sample);
					}
					else
					{
						if(!Xform_Nearest(source, sampleU, sampleV, &pixel))
							break;
						Xform_Unpack(pixel, sample);
					}
					uint32_t index = pixel >> 25;
					if(index == 0)
						break;
					int16_t d[4];
					Xform_Unpack(out[x], d);
					for(int channel = 0; channel < 4; channel++)
					{
						uint16_t w = channel < 3 ? alphaWeight[index] : 0;
						int16_t product = (int16_t)(uint16_t)((uint32_t)(uint16_t)(sample[channel] - d[channel]) * w);
						sample[channel] = (int16_t)(d[channel] + (product >> 7));
					}
					out[x] = Xform_Pack(sample);
					break;
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------
// The fast path.
// ---------------------------------------------------------------------------------

/*
 * 0x004166E0: can the walk be skipped. Only when all four positions are whole pixels,
 * the angle is a whole number of turns and both scales are exactly 0x10000 - and then
 * only when the source, placed with its anchor on the point, covers the whole view
 * (0x004090B0 with the source's rectangle as the outer one). `view` is then the part
 * of the source that lands on the view, the same size as the view.
 */
static int Xform_FitsWhole(Bitmap_t* view, const Bitmap_t* destination,
                           int32_t pointX, int32_t pointY, const Bitmap_t* source,
                           int32_t anchorX, int32_t anchorY, int32_t angle,
                           uint32_t scaleX, uint32_t scaleY)
{
	if((pointX & 0xFFFF) || (pointY & 0xFFFF) || (anchorX & 0xFFFF) || (anchorY & 0xFFFF))
		return 0;
	if(angle % XFORM_FULL_TURN != 0)
		return 0;
	if(scaleX != 0x10000 || scaleY != 0x10000)
		return 0;

	Rect_t target = { 0, 0, destination->width - 1, destination->height - 1 };
	Renderer_OffsetRect(&target, -(pointX >> 16), -(pointY >> 16));
	Rect_t covered = { 0, 0, source->width - 1, source->height - 1 };
	Renderer_OffsetRect(&covered, -(anchorX >> 16), -(anchorY >> 16));
	if(!Renderer_RectContains(&covered, &target))
		return 0;

	Renderer_RectIntersect(&covered, &target);
	Renderer_OffsetRect(&covered, anchorX >> 16, anchorY >> 16);
	*view = *source;
	Renderer_ClipBitmap(view, &covered);
	return 1;
}

/*
 * 0x0040AF50: the view copied across as it is, by the pair of modes - the same mode
 * is a byte copy (0x0040ADF0), a 24-bit source onto anything else is made opaque
 * (0x0040AF80) and a 32-bit source onto anything else is flattened by its alpha
 * (0x0040AFF0). Any other pair is left alone. Both of the last two write four-byte
 * pixels whatever the destination's mode, which would run past the rows of a
 * destination with smaller pixels; that is refused here.
 */
static int Xform_CopyView(Bitmap_t* destination, const Bitmap_t* view)
{
	if(destination->mode == view->mode)
	{
		size_t bytes = (size_t)Renderer_ModePixelBytes(view->mode) * (size_t)view->width;
		for(int y = 0; y < view->height; y++)
			memcpy(destination->bitmap + (ptrdiff_t)y * destination->stride,
			       view->bitmap + (ptrdiff_t)y * view->stride, bytes);
		return XFORM_RESULT_OK;
	}
	if(view->mode != BITMAP_MODE_24 && view->mode != BITMAP_MODE_32)
		return XFORM_RESULT_OK;
	if(Renderer_ModePixelBytes(destination->mode) != 4)
	{
		printf("[Xform]: Warning: copying a %d-bit view onto pixel mode %d (0x%08X) would overrun the destination; refused\n",
		       view->mode == BITMAP_MODE_24 ? 24 : 32, destination->mode,
		       view->mode == BITMAP_MODE_24 ? 0x0040AF80u : 0x0040AFF0u);
		return XFORM_RESULT_REFUSED;
	}
	for(int y = 0; y < view->height; y++)
	{
		uint32_t* out = (uint32_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		const uint32_t* in = (const uint32_t*)(view->bitmap + (ptrdiff_t)y * view->stride);
		for(int x = 0; x < view->width; x++)
		{
			if(view->mode == BITMAP_MODE_24)
			{
				out[x] = in[x] | 0xFF000000u;
				continue;
			}
			/*
			 * 0x0040AFF0: each colour times the table at 0x0050B0F0, indexed by
			 * alpha >> 1, then >> 7. The table (built at 0x00407770) holds i in the
			 * three colour words and 0 in the fourth - except entry 127, which holds
			 * 0x80, so an opaque pixel comes through unchanged. The fourth byte is 0.
			 */
			uint32_t pixel = in[x];
			uint32_t index = pixel >> 25;
			uint32_t factor = index == 0x7F ? 0x80 : index;
			uint32_t result = 0;
			for(int channel = 0; channel < 3; channel++)
				result |= ((((pixel >> (channel * 8)) & 0xFF) * factor) >> 7) << (channel * 8);
			out[x] = result;
		}
	}
	return XFORM_RESULT_OK;
}

/*
 * 0x004188D0: 0x00417800's fast path. A weight of 0 is 0x0040AF50; any other weight
 * darkens with 0x00418900 when both views are 24-bit or both 32-bit, and does
 * nothing otherwise. 0x00418900 walks the destination's size, all four words through
 * the darkening multiplier - so a weight from 0x100 up does whatever pmullw makes of
 * a negative inverse, which is reproduced rather than clamped.
 */
static int Xform_FastCopy(Bitmap_t* destination, const Bitmap_t* view, uint32_t weight)
{
	if(weight == 0)
		return Xform_CopyView(destination, view);
	if(destination->mode != view->mode
	   || (destination->mode != BITMAP_MODE_24 && destination->mode != BITMAP_MODE_32))
		return XFORM_RESULT_OK;

	uint16_t darken[4];
	Xform_DarkenWords(weight, darken);
	for(int y = 0; y < destination->height; y++)
	{
		uint32_t* out = (uint32_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		const uint32_t* in = (const uint32_t*)(view->bitmap + (ptrdiff_t)y * view->stride);
		for(int x = 0; x < destination->width; x++)
			out[x] = Xform_Darken(in[x], darken);
	}
	return XFORM_RESULT_OK;
}

/*
 * 0x0040B0D0: a 16-bit view onto a destination read as 16-bit words, each word that
 * is not 0 copied - 0 is the colour key. Right to left, as the original goes. 0x0040B080
 * sends every 16-bit source here without looking at the destination's mode.
 */
static void Xform_KeyCopy16(Bitmap_t* destination, const Bitmap_t* view)
{
	for(int y = 0; y < view->height; y++)
	{
		uint16_t* out = (uint16_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		const uint16_t* in = (const uint16_t*)(view->bitmap + (ptrdiff_t)y * view->stride);
		for(int x = view->width - 1; x >= 0; x--)
			if(in[x] != 0)
				out[x] = in[x];
	}
}

/*
 * 0x0040B3B0: 16-bit onto 16-bit with a transparency, per RGB555 field, the source
 * weighted by 0x100 - transparency and the destination by the transparency, each
 * field shifted down by 8 and masked back into place (the blue field is not masked;
 * it cannot overflow). Key colour 0 skipped, right to left.
 */
static void Xform_Blend16(Bitmap_t* destination, const Bitmap_t* view, uint32_t transparency)
{
	uint32_t inv = 0x100 - transparency;
	for(int y = 0; y < view->height; y++)
	{
		uint16_t* out = (uint16_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		const uint16_t* in = (const uint16_t*)(view->bitmap + (ptrdiff_t)y * view->stride);
		for(int x = view->width - 1; x >= 0; x--)
		{
			uint32_t s = in[x];
			if(s == 0)
				continue;
			uint32_t d = out[x];
			uint32_t green = (((s & 0x3E0) * inv + (d & 0x3E0) * transparency) >> 8) & 0x3E0;
			uint32_t red = (((s & 0x7C00) * inv + (d & 0x7C00) * transparency) >> 8) & 0x7C00;
			uint32_t blue = ((s & 0x1F) * inv + (d & 0x1F) * transparency) >> 8;
			out[x] = (uint16_t)(green + red + blue);
		}
	}
}

/*
 * 0x0040B4B0: 24 onto 24 with a transparency. All four bytes, the fourth included:
 * s + ((d - s) * (transparency >> 1)) >> 7, the product's low word shifted
 * arithmetically, saturated.
 */
static void Xform_Blend24(Bitmap_t* destination, const Bitmap_t* view, uint32_t transparency)
{
	int16_t weight = (int16_t)(transparency >> 1);
	for(int y = 0; y < view->height; y++)
	{
		uint32_t* out = (uint32_t*)(destination->bitmap + (ptrdiff_t)y * destination->stride);
		const uint32_t* in = (const uint32_t*)(view->bitmap + (ptrdiff_t)y * view->stride);
		for(int x = 0; x < view->width; x++)
		{
			int16_t s[4], d[4];
			Xform_Unpack(in[x], s);
			Xform_Unpack(out[x], d);
			for(int channel = 0; channel < 4; channel++)
			{
				int16_t product = (int16_t)(uint16_t)((uint32_t)(uint16_t)(d[channel] - s[channel]) * (uint16_t)weight);
				s[channel] = (int16_t)(s[channel] + (product >> 7));
			}
			out[x] = Xform_Pack(s);
		}
	}
}

/*
 * 0x0040B5D0: a 24-bit source onto a 32-bit destination with a transparency. The
 * source is taken as opaque, S = 0xFF * (0x100 - transparency); per pixel
 * D = ((0x10000 - S) * da) >> 8, the colours are (s * (S << 16) / (D + S) +
 * d * (D << 16) / (D + S)) >> 16 and the new alpha (D + S) >> 8.
 */
static void Xform_Blend24Onto32(Bitmap_t* destination, const Bitmap_t* view, uint32_t transparency)
{
	uint32_t share = (0x100 - transparency) * 0xFF;
	for(int y = 0; y < view->height; y++)
	{
		uint8_t* out = destination->bitmap + (ptrdiff_t)y * destination->stride;
		const uint8_t* in = view->bitmap + (ptrdiff_t)y * view->stride;
		for(int x = 0; x < view->width; x++, out += 4, in += 4)
		{
			uint32_t destinationShare = ((0x10000 - share) * out[3]) >> 8;
			uint32_t denominator = destinationShare + share;
			uint32_t sourceWeight = (share << 16) / denominator;
			uint32_t destinationWeight = (destinationShare << 16) / denominator;
			for(int channel = 0; channel < 3; channel++)
				out[channel] = (uint8_t)((in[channel] * sourceWeight + out[channel] * destinationWeight) >> 16);
			out[3] = (uint8_t)(denominator >> 8);
		}
	}
}

/*
 * 0x0040B320: 0x004169B0's fast path, the view blended across by the pair of modes
 * and the transparency. No transparency is 0x0040B080: 16-bit key copy (0x0040B0D0),
 * 24 onto 24 a byte copy (0x0040ADF0), 24 onto 32 made opaque (0x0040AF80), 32 onto
 * 24 (0x0040B130) and 32 onto 32 (0x0040B200) by the source's alpha. Full
 * transparency (0x100 and up) draws nothing. What lies between is 0x0040B3B0 (16/16),
 * 0x0040B4B0 (24/24), 0x0040B5D0 (24 onto 32), 0x0040B6F0 (32 onto 24) and 0x0040B9B0
 * (32/32). The four arms for a 32-bit source are the renderer's own transcriptions,
 * reached through its blend mode 0x01, which is this very routine.
 */
static void Xform_BlendView(Bitmap_t* destination, const Bitmap_t* view, uint32_t transparency)
{
	if(transparency >= 0x100)
		return;
	if(view->mode == BITMAP_MODE_32)
	{
		if(destination->mode != BITMAP_MODE_24 && destination->mode != BITMAP_MODE_32)
			return;
		Bitmap_t source = *view;
		Renderer_BlitView(destination, &source, BITMAP_BLEND_ALPHA_TRANS, (int)transparency);
		return;
	}
	if(view->mode == BITMAP_MODE_16)
	{
		if(transparency == 0)
		{
			// Words written into one-byte pixels would run past the rows.
			if(Renderer_ModePixelBytes(destination->mode) < 2)
			{
				printf("[Xform]: Warning: a 16-bit key copy onto pixel mode %d (0x0040B0D0) would overrun the destination; refused\n",
				       destination->mode);
				return;
			}
			Xform_KeyCopy16(destination, view);
		}
		else if(destination->mode == BITMAP_MODE_16)
			Xform_Blend16(destination, view, transparency);
		return;
	}
	if(view->mode != BITMAP_MODE_24)
		return;
	if(transparency == 0)
	{
		if(destination->mode == BITMAP_MODE_24 || destination->mode == BITMAP_MODE_32)
			Xform_CopyView(destination, view);
		return;
	}
	if(destination->mode == BITMAP_MODE_24)
		Xform_Blend24(destination, view, transparency);
	else if(destination->mode == BITMAP_MODE_32)
		Xform_Blend24Onto32(destination, view, transparency);
}

// ---------------------------------------------------------------------------------
// The bands.
// ---------------------------------------------------------------------------------

typedef int (*XformDraw_t)(Bitmap_t*, int32_t, int32_t, const Bitmap_t*, int32_t, int32_t,
                           int32_t, uint32_t, uint32_t, uint32_t, int, int);

/*
 * 0x00419EB0 with 0x00419BE0 (how many bands), 0x00419C70 (the band views) and
 * 0x00419D90 (their points), then the worker (0x0041A2F0 / 0x0041A280) calling the
 * routine again per band with banding off. 0 when the original would not band, and
 * the caller then draws the view in one piece.
 *
 * No bands for a view under 0xA00 pixels or fewer than two workers. The step is
 * (height << 16) / count; while a band would be over 0xA000 pixels the count doubles
 * and the step halves; a step under one row means no bands. Band k takes the whole
 * rows the running 16.16 total has passed (the last band takes what is left), and its
 * point is moved up by the rows the bands before it took.
 */
static int Xform_DrawBands(XformDraw_t draw, Bitmap_t* destination, int32_t pointX, int32_t pointY,
                           const Bitmap_t* source, int32_t anchorX, int32_t anchorY,
                           int32_t angle, uint32_t scaleX, uint32_t scaleY,
                           uint32_t weight, int smooth)
{
	if(xformWorkers <= 0)
		return 0;
	uint32_t width = (uint32_t)destination->width;
	uint32_t height = (uint32_t)destination->height;
	if(width * height < 0xA00)
		return 0;
	uint32_t count = (uint32_t)xformWorkers;
	if(count < 2)
		return 0;
	uint32_t step = (height << 16) / count;
	uint32_t perBand = (height / count) * width;
	while(perBand > 0xA000)
	{
		perBand >>= 1;
		count += count;
		step >>= 1;
	}
	if(step < 0x10000)
		return 0;

	uint32_t total = 0;
	uint32_t rowsDone = 0;
	int32_t bandPointY = pointY;
	for(uint32_t band = 0; band < count; band++)
	{
		total += step;
		uint32_t rows = total >> 16;
		Bitmap_t view = *destination;
		view.bitmap = destination->bitmap + (ptrdiff_t)rowsDone * destination->stride;
		view.height = (int)(band + 1 < count ? rows : height - rowsDone);
		draw(&view, pointX, bandPointY, source, anchorX, anchorY, angle, scaleX, scaleY,
		     weight, smooth, 0);
		bandPointY = (int32_t)((uint32_t)bandPointY - (total & 0xFFFF0000u));
		rowsDone += rows;
		total &= 0xFFFF;
	}
	return 1;
}

// ---------------------------------------------------------------------------------
// The two routines.
// ---------------------------------------------------------------------------------

int Xform_DrawCopy(Bitmap_t* destination, int32_t pointX, int32_t pointY,
                   const Bitmap_t* source, int32_t anchorX, int32_t anchorY,
                   int32_t angle, uint32_t scaleX, uint32_t scaleY,
                   uint32_t weight, int smooth, int banded)
{
	if(scaleX == 0 || scaleY == 0)
		return XFORM_RESULT_ZERO_SCALE;

	Bitmap_t view;
	if(Xform_FitsWhole(&view, destination, pointX, pointY, source, anchorX, anchorY,
	                   angle, scaleX, scaleY))
		return Xform_FastCopy(destination, &view, weight);

	if(banded && Xform_DrawBands(Xform_DrawCopy, destination, pointX, pointY, source,
	                             anchorX, anchorY, angle, scaleX, scaleY, weight, smooth))
		return XFORM_RESULT_OK;

	// 0x004178F2: the pair of pixel modes picks the walk.
	XformWalk_t walk;
	if(destination->mode == source->mode)
	{
		if(source->mode != BITMAP_MODE_24 && source->mode != BITMAP_MODE_32)
			return XFORM_RESULT_OK;
		if(weight >= 0x100)
		{
			// 0x004179CF: 0x0040A620 over the whole view.
			Renderer_ClearBitmap(destination);
			return XFORM_RESULT_OK;
		}
		if(weight != 0)
			walk = smooth ? XFORM_DARKEN_SMOOTH : XFORM_DARKEN_NEAREST;
		else
			walk = smooth ? XFORM_COPY_SMOOTH : XFORM_COPY_NEAREST;
	}
	else if(source->mode == BITMAP_MODE_24 && destination->mode == BITMAP_MODE_32)
		walk = smooth ? XFORM_OPAQUE_SMOOTH : XFORM_OPAQUE_NEAREST;
	else
		return XFORM_RESULT_OK;

	Xform_Walk(destination, source, pointX, pointY, anchorX, anchorY, angle, scaleX, scaleY,
	           weight, walk);
	return XFORM_RESULT_OK;
}

int Xform_DrawBlend(Bitmap_t* destination, int32_t pointX, int32_t pointY,
                    const Bitmap_t* source, int32_t anchorX, int32_t anchorY,
                    int32_t angle, uint32_t scaleX, uint32_t scaleY,
                    uint32_t transparency, int smooth, int banded)
{
	if(scaleX == 0 || scaleY == 0)
		return XFORM_RESULT_ZERO_SCALE;

	Bitmap_t view;
	if(Xform_FitsWhole(&view, destination, pointX, pointY, source, anchorX, anchorY,
	                   angle, scaleX, scaleY))
	{
		Xform_BlendView(destination, &view, transparency);
		return XFORM_RESULT_OK;
	}

	if(banded && Xform_DrawBands(Xform_DrawBlend, destination, pointX, pointY, source,
	                             anchorX, anchorY, angle, scaleX, scaleY, transparency, smooth))
		return XFORM_RESULT_OK;

	// 0x00416AA4: only a 24-bit destination is drawn on.
	if(destination->mode != BITMAP_MODE_24)
		return XFORM_RESULT_OK;
	if(transparency >= 0x100)
		return XFORM_RESULT_OK;

	XformWalk_t walk;
	if(source->mode == BITMAP_MODE_24)
	{
		// 0x00416B50: no transparency is 0x00417800's own copy walks.
		if(transparency == 0)
			walk = smooth ? XFORM_COPY_SMOOTH : XFORM_COPY_NEAREST;
		else
			walk = smooth ? XFORM_FADE_SMOOTH : XFORM_FADE_NEAREST;
	}
	else if(source->mode == BITMAP_MODE_32)
		walk = smooth ? XFORM_ALPHA_SMOOTH : XFORM_ALPHA_NEAREST;
	else
		return XFORM_RESULT_OK;

	Xform_Walk(destination, source, pointX, pointY, anchorX, anchorY, angle, scaleX, scaleY,
	           transparency, walk);
	return XFORM_RESULT_OK;
}
