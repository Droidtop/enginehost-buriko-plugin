#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "renderer.h"
#include "engine.h"
#include "object.h"
#include <string.h>
#include "spng.h"
#include "cbg.h"

Bitmap_t* Renderer_ParsePng(uint8_t* file, size_t fileSize);

Renderer_t* Renderer_Init(Engine_t* engine)
{
	Renderer_t* renderer = (Renderer_t*)malloc(sizeof(Renderer_t));
	if(renderer == NULL)
		return NULL;
	renderer->engine = engine;
	for(int i = 0; i < RENDERER_MAX_BITMAPS; i++)
		renderer->bitmaps[i] = NULL;
	for(int i = 0; i < RENDERER_MAX_SCREENS; i++)
		renderer->screens[i] = NULL;
	renderer->activeScreen = 0;
	renderer->allocatedScreens = 0;
	return renderer;
}

uint32_t Renderer_CreateScreen(Renderer_t* renderer, int width, int height)
{
	Screen_t* screen = (Screen_t*)malloc(sizeof(Screen_t));
    if(!screen)
    	return 0;
    uint8_t* bitmap = (uint8_t*)malloc(width * height * 4);
    if(!bitmap)
    {
    	free(screen);
    	return 0;
    }
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
	    bitmap,
	    width,
	    height,
	    32,
	    width * 4,
	    SDL_PIXELFORMAT_RGBA32
	);
    screen->width = width;
    screen->height = height;
    screen->x = 0;
    screen->y = 0;
    screen->gapCoefficient = 0;
    screen->fontFamily = NULL;
    screen->fontSize = 0;
    screen->fontWidth = 0;
    screen->fontStyle = 0;
    screen->fontScaledWidth = 0;
    screen->field15C = 0;
    screen->swingingStyle = 0;
    screen->field354 = 0;
    screen->field364 = 0;
    screen->bitmap = bitmap;
    screen->surface = surface;
    // The handle is the display object's: a window is one, and the object table
    // is what hands out and reuses the sixteen slots (0x00440690).
    uint32_t id = Object_Create(OBJECT_TAG_WINDOW);
    if(id == 0)
    {
        SDL_FreeSurface(surface);
        free(bitmap);
        free(screen);
        printf("[Renderer]: Warning: every window slot is taken\n");
        return 0;
    }
    uint32_t index = id & OBJECT_INDEX_MASK;
    renderer->screens[index] = screen;
    renderer->activeScreen = (int)index;
    renderer->allocatedScreens++;
    printf("[Renderer]: Created screen object (0x%08X) width size %dx%d\n", id, width, height);
    return id;
}

Screen_t* Renderer_ResolveScreen(Renderer_t* renderer, uint32_t handle)
{
	if((handle & OBJECT_TAG_MASK) != OBJECT_TAG_WINDOW)
		return NULL;
	uint32_t id = handle & OBJECT_INDEX_MASK;
	if(id >= RENDERER_MAX_SCREENS)
		return NULL;
	return renderer->screens[id];
}

void Renderer_DestroyScreen(Renderer_t* renderer, uint32_t handle)
{
	if((handle & OBJECT_TAG_MASK) != OBJECT_TAG_WINDOW)
		return;
	uint32_t id = handle & OBJECT_INDEX_MASK;
	if(id >= RENDERER_MAX_SCREENS || renderer->screens[id] == NULL)
		return;
	if(renderer->screens[id]->surface != NULL)
		SDL_FreeSurface(renderer->screens[id]->surface);
	if(renderer->screens[id]->bitmap != NULL)
		free(renderer->screens[id]->bitmap);
	free(renderer->screens[id]);
	renderer->screens[id] = NULL;
	renderer->allocatedScreens--;
	Object_Destroy(handle);
}

void Renderer_DrawBitmapToScreen(Renderer_t* renderer, uint32_t bitmapId, int screenId)
{
	printf("[Renderer]: Start draw\n");
	Screen_t* screen = renderer->screens[screenId];
	Bitmap_t* bitmap = renderer->bitmaps[bitmapId];
	if(!screen)
	{
		printf("[Renderer]: Warning: Attempting to draw to invalid screen object (%d)\n", screenId);
		return;
	}
	if(!bitmap)
	{
		printf("[Renderer]: Warning: Attempting to draw invalid bitmap (%d)\n", bitmapId);
		return;
	}
	if(Renderer_ModePixelBytes(bitmap->mode) != 4)
	{
		printf("[Renderer]: Warning: bitmap %d is pixel mode %d, which the screen blit does not take yet\n", bitmapId, bitmap->mode);
		return;
	}
	for(int y = 0; y < bitmap->height; y++)
	{
		if(y >= screen->height)
			break;
		for(int x = 0; x < bitmap->width; x++)
		{
			if(x >= screen->width)
				break;
			memcpy(&screen->bitmap[(y * screen->width * 4) + (x * 4)],
			       &bitmap->bitmap[(y * bitmap->stride) + (x * 4)],
			       4);
		}
	}
	printf("[Renderer]: End draw\n");
}

Bitmap_t* Renderer_ParsePng(uint8_t* file, size_t fileSize)
{
    spng_ctx *ctx = spng_ctx_new(0);
    if(!ctx)
    {
        printf("[Renderer]: Failed to create libspng context\n");
        return NULL;
    }

    int ret = spng_set_png_buffer(ctx, file, fileSize);
    if(ret)
    {
        printf("[Renderer]: Error setting PNG file: %s\n", spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    // Determine output buffer size
    size_t out_size;
    ret = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
    if(ret)
    {
        printf("Error calculating image size: %s\n", spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    unsigned char *out_buffer = malloc(out_size);
    if(!out_buffer)
    {
        printf("[Renderer]: Error: Memory allocation failed\n");
        spng_ctx_free(ctx);
        return NULL;
    }

    ret = spng_decode_image(ctx, out_buffer, out_size, SPNG_FMT_RGBA8, 0);
    if(ret)
    {
        printf("[Renderer]: Error decoding image: %s\n", spng_strerror(ret));
        free(out_buffer);
        spng_ctx_free(ctx);
        return NULL;
    }

    struct spng_ihdr ihdr;
    ret = spng_get_ihdr(ctx, &ihdr);
    if(ret)
    {
        printf("[Renderer]: Error getting image IHDR: %s\n", spng_strerror(ret));
        free(out_buffer);
        spng_ctx_free(ctx);
        return NULL;
    }

    printf("[Renderer]: Successfully decoded!\n");

    Bitmap_t* bitmap = (Bitmap_t*)malloc(sizeof(Bitmap_t));
    if(!bitmap)
    {
    	free(out_buffer);
    	return NULL;
    }
    bitmap->width = ihdr.width;
    bitmap->height = ihdr.height;
    bitmap->mode = BITMAP_MODE_32;
    bitmap->stride = ihdr.width * 4;
    bitmap->bitmap = out_buffer;

    spng_ctx_free(ctx);
    return bitmap;
}

// 0x004E41B0, indexed by pixel mode.
int Renderer_ModePixelBytes(int mode)
{
	static const int bytes[BITMAP_MODE_COUNT] = { 2, 4, 4, 1, 4, 4, 6, 4 };
	if(mode < 0 || mode >= BITMAP_MODE_COUNT)
		return 0;
	return bytes[mode];
}

// 0x00401C10, for the bit counts a CompressedBG file can carry.
int Renderer_ModeForBits(int bits)
{
	switch(bits)
	{
		case 8:  return BITMAP_MODE_8;
		case 24: return BITMAP_MODE_24;
		case 32: return BITMAP_MODE_32;
		default: return -1;
	}
}

Bitmap_t* Renderer_ResolveBitmap(Renderer_t* renderer, int id)
{
	if(renderer == NULL || id < 0 || id >= RENDERER_MAX_BITMAPS)
		return NULL;
	return renderer->bitmaps[id];
}

int Renderer_DestroyBitmap(Renderer_t* renderer, int id)
{
	if(renderer == NULL || id < 0 || id >= RENDERER_MAX_BITMAPS)
		return 0;
	if(renderer->bitmaps[id] == NULL)
		return 0;
	free(renderer->bitmaps[id]->bitmap);
	free(renderer->bitmaps[id]);
	renderer->bitmaps[id] = NULL;
	return 1;
}

int Renderer_FillBitmap(Renderer_t* renderer, int id, uint32_t colour)
{
	Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, id);
	if(bitmap == NULL)
		return 0;
	if(Renderer_ModePixelBytes(bitmap->mode) != 4)
		return 1;
	for(int row = 0; row < bitmap->height; row++)
	{
		uint32_t* out = (uint32_t*)(bitmap->bitmap + (size_t)row * bitmap->stride);
		for(int column = 0; column < bitmap->width; column++)
			out[column] = colour;
	}
	return 1;
}

Bitmap_t* Renderer_CreateBitmap(Renderer_t* renderer, int id, int width, int height, int mode)
{
	if(renderer == NULL || id < 0 || id >= RENDERER_MAX_BITMAPS)
		return NULL;
	if(width <= 0 || height <= 0)
		return NULL;

	int pixelBytes = Renderer_ModePixelBytes(mode);
	if(pixelBytes == 0)
		return NULL;

	Bitmap_t* bitmap = (Bitmap_t*)malloc(sizeof(Bitmap_t));
	if(bitmap == NULL)
		return NULL;
	bitmap->width  = width;
	bitmap->height = height;
	bitmap->mode   = mode;
	bitmap->stride = width * pixelBytes;
	// 0x00407DA0 rewrites only the first eight fields of the table entry, so a slot
	// that is recreated keeps the offset it already had.
	bitmap->offsetX = renderer->bitmaps[id] != NULL ? renderer->bitmaps[id]->offsetX : 0;
	bitmap->offsetY = renderer->bitmaps[id] != NULL ? renderer->bitmaps[id]->offsetY : 0;
	bitmap->bitmap = (uint8_t*)calloc(1, (size_t)bitmap->stride * (size_t)height);
	if(bitmap->bitmap == NULL)
	{
		free(bitmap);
		return NULL;
	}

	Renderer_DestroyBitmap(renderer, id);
	renderer->bitmaps[id] = bitmap;
	return bitmap;
}

/*
 * Grp0 0x10's immediate path, 0x00401E00 by way of 0x00401EF0: read the file, take
 * its size and bit count, and put its pixels in the slot. The 0x8000000X results are
 * the original's, and every one of them is fatal at the opcode.
 *
 * A 24-bit image is widened to four bytes a pixel because that is how the original
 * holds it (mode 1 is four bytes in the table at 0x004E41B0); the fourth byte is set
 * opaque, which is what a bitmap with no alpha channel means to the blitter.
 */
uint32_t Renderer_LoadBitmap(Renderer_t* renderer, int slot, const char* filename, const char* archive)
{
	size_t fileSize;
	uint8_t* file = Engine_ReadFile(renderer->engine, archive, filename, &fileSize);
	if(file == NULL)
		return 0x80000009; // the loader's "no such image" result

	int width = 0;
	int height = 0;
	int bits = 0;
	int offsetX = 0;
	int offsetY = 0;
	uint8_t* pixels = NULL;

	if(CBG_IsCompressedBG(file, fileSize))
	{
		pixels = CBG_Decode(file, fileSize, &width, &height, &bits);
		CBG_ReadOffset(file, fileSize, &offsetX, &offsetY);
	}
	else if(fileSize > 4 && file[0] == 0x89 && file[1] == 'P' && file[2] == 'N' && file[3] == 'G')
	{
		// Not a format the original knows; kept because OpenBGI's own test data uses it.
		Bitmap_t* png = Renderer_ParsePng(file, fileSize);
		if(png != NULL)
		{
			width  = png->width;
			height = png->height;
			bits   = 32;
			pixels = png->bitmap;
			free(png);
		}
	}
	free(file);

	if(pixels == NULL)
	{
		printf("[Renderer]: Could not decode bitmap [%s : %s]\n", filename, archive);
		return 0x80000002; // "not Windows bitmap data"
	}

	int mode = Renderer_ModeForBits(bits);
	if(mode < 0)
	{
		free(pixels);
		return 0x80000004; // "unsupported bit count"
	}

	Bitmap_t* bitmap = Renderer_CreateBitmap(renderer, slot, width, height, mode);
	if(bitmap == NULL)
	{
		free(pixels);
		return 0x80000008; // "not enough memory"
	}
	bitmap->offsetX = offsetX;
	bitmap->offsetY = offsetY;

	int pixelBytes = Renderer_ModePixelBytes(mode);
	if(bits / 8 == pixelBytes)
	{
		memcpy(bitmap->bitmap, pixels, (size_t)bitmap->stride * (size_t)height);
	}
	else
	{
		// 24 bits into four-byte pixels.
		int sourceBytes = bits / 8;
		for(int y = 0; y < height; y++)
		{
			uint8_t* dst = bitmap->bitmap + (size_t)y * bitmap->stride;
			uint8_t* src = pixels + (size_t)y * width * sourceBytes;
			for(int x = 0; x < width; x++)
			{
				memcpy(dst + x * pixelBytes, src + x * sourceBytes, sourceBytes);
				dst[x * pixelBytes + 3] = 0xFF;
			}
		}
	}
	free(pixels);

	printf("[Renderer]: Loaded bitmap %d from [%s : %s] (%dx%d, %d bits)\n",
	       slot, filename, archive, width, height, bits);
	return 0;
}

/*
 * 0x004033A0. The source must resolve, the size must be non-zero, and the destination
 * is then created at that size in the source's own pixel mode and the source blitted
 * into it at the negated offsets - so the destination ends up holding the source's
 * (offsetX, offsetY, width, height) rectangle. Source pixels outside the source stay
 * as the fresh bitmap's cleared bytes.
 */
/*
 * 0x0040A9A0: two pixel modes work together when they are the same, or when one is
 * 24-bit and the other 32-bit. Anything else is the original's fatal "the pixel modes
 * of destination [ %d ] and source [ %d ] are not compatible".
 */
static int Renderer_ModesCompatible(int destinationMode, int sourceMode)
{
	if(destinationMode == sourceMode)
		return 1;
	if(destinationMode == BITMAP_MODE_24 && sourceMode == BITMAP_MODE_32)
		return 1;
	if(destinationMode == BITMAP_MODE_32 && sourceMode == BITMAP_MODE_24)
		return 1;
	return 0;
}

// 0x0040B200: source over destination, both 32-bit, in the original's fixed point.
static uint32_t Renderer_BlendOver(uint32_t destination, uint32_t source)
{
	uint32_t sa = source >> 24;
	if(sa == 0)
		return destination;
	if(sa == 0xFF)
		return source;

	uint32_t da = destination >> 24;
	uint32_t inv = 0x100 - sa;
	uint32_t denominator = (sa << 8) + da * inv;
	uint32_t destinationWeight = ((da * inv) << 8) / denominator;
	uint32_t sourceWeight = (sa << 16) / denominator;

	uint32_t out = (denominator >> 8) << 24;
	for(int channel = 0; channel < 3; channel++)
	{
		uint32_t d = (destination >> (channel * 8)) & 0xFF;
		uint32_t s = (source >> (channel * 8)) & 0xFF;
		uint32_t v = (d * destinationWeight + s * sourceWeight) >> 8;
		if(v > 0xFF)
			v = 0xFF;
		out |= v << (channel * 8);
	}
	return out;
}

// 0x0040B130: source over an opaque destination, on a 7-bit alpha.
static uint32_t Renderer_BlendOverOpaque(uint32_t destination, uint32_t source)
{
	uint32_t a = (source >> 25) & 0x7F;
	if(a == 0)
		return destination;
	if(a == 0x7F)
		return source & 0x00FFFFFF;

	uint32_t out = destination & 0xFF000000;
	for(int channel = 0; channel < 3; channel++)
	{
		int d = (int)((destination >> (channel * 8)) & 0xFF);
		int s = (int)((source >> (channel * 8)) & 0xFF);
		int v = d + (((s - d) * (int)a) >> 7);
		if(v < 0)
			v = 0;
		if(v > 0xFF)
			v = 0xFF;
		out |= (uint32_t)v << (channel * 8);
	}
	return out;
}

// 0x0040B6F0: a 32-bit source over a 24-bit destination, with a transparency
// strictly between 0 and 0x100. `inverse` is the 0x100 - transparency the routine
// computes once; the weight is the original's table entry for this pixel's alpha,
// (alpha7 * inverse) >> 8, and the blend from there is 0x0040B130's.
static uint32_t Renderer_BlendOverOpaqueWeighted(uint32_t destination, uint32_t source,
                                                 uint32_t inverse)
{
	// The original's `test eax, 0xFE000000`: a source pixel whose seven-bit alpha
	// is zero is left alone, whatever the transparency.
	uint32_t a = (source >> 25) & 0x7F;
	if(a == 0)
		return destination;

	int weight = (int)((a * inverse) >> 8);

	// The destination is 24-bit, so its fourth byte is not a channel in the
	// original and is left as it is here, the same choice 0x0040B130's transcription
	// makes - this engine's bitmaps are handed to SDL as RGBA, where it would be one.
	uint32_t out = destination & 0xFF000000;
	for(int channel = 0; channel < 3; channel++)
	{
		int d = (int)((destination >> (channel * 8)) & 0xFF);
		int s = (int)((source >> (channel * 8)) & 0xFF);
		int v = d + (((s - d) * weight) >> 7);
		if(v < 0)
			v = 0;
		if(v > 0xFF)
			v = 0xFF;
		out |= (uint32_t)v << (channel * 8);
	}
	return out;
}

// 0x0040B4B0: a 24-bit source over a 24-bit destination, which has no per-pixel
// alpha to fold in, so it is the straight walk from the source towards the
// destination by transparency >> 1 out of 0x80.
static uint32_t Renderer_BlendTowardsDestination(uint32_t destination, uint32_t source,
                                                 int transparency)
{
	int weight = transparency >> 1;

	uint32_t out = destination & 0xFF000000;
	for(int channel = 0; channel < 3; channel++)
	{
		int d = (int)((destination >> (channel * 8)) & 0xFF);
		int s = (int)((source >> (channel * 8)) & 0xFF);
		int v = s + (((d - s) * weight) >> 7);
		if(v < 0)
			v = 0;
		if(v > 0xFF)
			v = 0xFF;
		out |= (uint32_t)v << (channel * 8);
	}
	return out;
}

// 0x0040B9B0: a 32-bit source over a 32-bit destination. This is 0x0040B200 with
// the transparency folded into the source's alpha first, on the scale above it:
// S = alpha * inverse, so 0x10000 is a wholly opaque source. Set the transparency
// to 0 and every term below collapses into 0x0040B200's.
static uint32_t Renderer_BlendOverWeighted(uint32_t destination, uint32_t source,
                                           uint32_t inverse)
{
	// The original's `test dword [src], 0xFF000000`.
	uint32_t sa = source >> 24;
	if(sa == 0)
		return destination;

	uint32_t da = destination >> 24;
	uint32_t sourceShare = sa * inverse;
	uint32_t destinationShare = ((0x10000u - sourceShare) * da) >> 8;
	uint32_t denominator = destinationShare + sourceShare;
	if(denominator == 0)
		return destination;

	uint32_t destinationWeight = (destinationShare << 8) / denominator;
	uint32_t sourceWeight = (sourceShare << 8) / denominator;

	// All four channels go through the multiply, the shift and the saturating pack,
	// the alpha included, and the denominator's own top byte is then ORed over it -
	// which is what the original does, odd as the OR looks.
	uint32_t out = 0;
	for(int channel = 0; channel < 4; channel++)
	{
		uint32_t d = (destination >> (channel * 8)) & 0xFF;
		uint32_t s = (source >> (channel * 8)) & 0xFF;
		uint32_t v = (d * destinationWeight + s * sourceWeight) >> 8;
		if(v > 0xFF)
			v = 0xFF;
		out |= v << (channel * 8);
	}
	return out | ((denominator >> 8) << 24);
}

int Renderer_BlitBitmap(Renderer_t* renderer, int destination, int x, int y,
                        int source, int mode, int transparency)
{
	Bitmap_t* dst = Renderer_ResolveBitmap(renderer, destination);
	if(dst == NULL)
		return 1;
	Bitmap_t* src = Renderer_ResolveBitmap(renderer, source);
	if(src == NULL)
		return 2;
	if(!Renderer_ModesCompatible(dst->mode, src->mode))
		return 3;
	// 0x100 - the transparency, which 0x0040B6F0 computes once before its loop.
	// 0x100 means no transparency is in play at all.
	uint32_t inverse = 0x100;
	if(mode == BITMAP_BLEND_ALPHA_TRANS || mode == BITMAP_BLEND_ALPHA_TRANS2)
	{
		// 0x0040B320: no transparency at all is the ordinary alpha row, and full
		// transparency draws nothing. What lies between picks a routine by the pair
		// of pixel modes; three of its five arms are read - 0x0040B4B0 (24 over 24),
		// 0x0040B6F0 (32 over 24) and 0x0040B9B0 (32 over 32). 0x0040B3B0 (16 over
		// 16) and 0x0040B5D0 (24 over 32) are refused by name.
		if(transparency >= 0x100)
			return 4;
		if(transparency != 0)
		{
			int written = (dst->mode == BITMAP_MODE_24
			                && (src->mode == BITMAP_MODE_32 || src->mode == BITMAP_MODE_24))
			           || (dst->mode == BITMAP_MODE_32 && src->mode == BITMAP_MODE_32);
			if(!written)
				return 6;
			inverse = 0x100 - (uint32_t)transparency;
		}
		mode = BITMAP_BLEND_ALPHA;
	}
	if(mode != BITMAP_BLEND_ALPHA && mode != BITMAP_BLEND_COPY)
		return 5;
	if(Renderer_ModePixelBytes(dst->mode) != 4)
		return 5;

	// The intersection 0x0040A530 takes, in the destination's coordinates.
	int left   = x < 0 ? 0 : x;
	int top    = y < 0 ? 0 : y;
	int right  = x + src->width;
	int bottom = y + src->height;
	if(right > dst->width)
		right = dst->width;
	if(bottom > dst->height)
		bottom = dst->height;
	if(left >= right || top >= bottom)
		return 4;

	// 0x0040B080 and 0x0040AF50: what happens per pixel is decided once, by the mode
	// and the two pixel modes, not per pixel.
	int blend = (mode == BITMAP_BLEND_ALPHA && src->mode == BITMAP_MODE_32);
	int destinationIsOpaque = (dst->mode == BITMAP_MODE_24);

	for(int row = top; row < bottom; row++)
	{
		uint32_t* dstRow = (uint32_t*)(dst->bitmap + (size_t)row * dst->stride);
		uint32_t* srcRow = (uint32_t*)(src->bitmap + (size_t)(row - y) * src->stride);
		for(int column = left; column < right; column++)
		{
			uint32_t pixel = srcRow[column - x];
			if(inverse != 0x100)
				// One of the three pairs let through above.
				pixel = (src->mode != BITMAP_MODE_32)
				      ? Renderer_BlendTowardsDestination(dstRow[column], pixel, transparency)
				      : (destinationIsOpaque
				         ? Renderer_BlendOverOpaqueWeighted(dstRow[column], pixel, inverse)
				         : Renderer_BlendOverWeighted(dstRow[column], pixel, inverse));
			else if(blend)
				pixel = destinationIsOpaque
				      ? Renderer_BlendOverOpaque(dstRow[column], pixel)
				      : Renderer_BlendOver(dstRow[column], pixel);
			else if(destinationIsOpaque)
				pixel |= 0xFF000000;
			dstRow[column] = pixel;
		}
	}
	return 0;
}

/*
 * 0x00495200: the average of one box of the source. Every pixel of the box counts
 * towards the alpha; only the pixels that are not fully transparent - or all of them,
 * when the source has no alpha at all - count towards the three colours.
 */
static uint32_t Renderer_AverageBox(const uint8_t* pixels, int width, int height,
                                    int stride, int mode,
                                    int left, int top, int boxWidth, int boxHeight)
{
	int right  = left + boxWidth;
	int bottom = top + boxHeight;
	if(left < 0)
		left = 0;
	if(top < 0)
		top = 0;
	if(right > width)
		right = width;
	if(bottom > height)
		bottom = height;

	uint32_t sumB = 0, sumG = 0, sumR = 0, sumA = 0;
	uint32_t colourCount = 0;
	uint32_t alphaCount = 0;
	for(int row = top; row < bottom; row++)
	{
		const uint8_t* in = pixels + (size_t)row * stride + (size_t)left * 4;
		alphaCount += (uint32_t)(right - left);
		for(int column = left; column < right; column++, in += 4)
		{
			if(in[3] == 0 && mode != BITMAP_MODE_24)
				continue;
			sumB += in[0];
			sumG += in[1];
			sumR += in[2];
			sumA += in[3];
			colourCount++;
		}
	}
	if(alphaCount == 0)
		return 0;
	if(colourCount == 0)
		colourCount = 1;
	return ((sumA / alphaCount) << 24) | ((sumR / colourCount) << 16)
	     | ((sumG / colourCount) << 8) | (sumB / colourCount);
}

int Renderer_ScaleBitmap(Renderer_t* renderer, int destination, int source,
                         int rateX, int rateY, int filter)
{
	Bitmap_t* src = Renderer_ResolveBitmap(renderer, source);
	if(src == NULL)
		return 2;
	if(src->mode != BITMAP_MODE_24 && src->mode != BITMAP_MODE_32)
		return 3;
	if(filter != 0)
	{
		// 0x00494F20 picks its sampler from the two rates; only the shrinking one
		// (0x00495200) is written. 0x00495340 and 0x004954B0 are not read yet.
		int mildX = (rateX >= 0x8000 && rateX <= 0xFFFF);
		int mildY = (rateY >= 0x8000 && rateY <= 0xFFFF);
		if(mildX || mildY || (rateX >= 0x8000 && rateY >= 0x8000))
			return 5;
	}

	int width  = (int)(((int64_t)src->width  * (int64_t)rateX) >> 16);
	int height = (int)(((int64_t)src->height * (int64_t)rateY) >> 16);
	if(width <= 0 || height <= 0)
		return 4;

	// The source is copied out first, because the destination may be the same slot.
	int sourceWidth  = src->width;
	int sourceHeight = src->height;
	int sourceStride = src->stride;
	size_t sourceBytes = (size_t)sourceStride * (size_t)sourceHeight;
	uint8_t* pixels = (uint8_t*)malloc(sourceBytes);
	if(pixels == NULL)
		return 1;
	memcpy(pixels, src->bitmap, sourceBytes);

	Bitmap_t* dst = Renderer_CreateBitmap(renderer, destination, width, height, src->mode);
	if(dst == NULL)
	{
		free(pixels);
		return 1;
	}

	// 0x00494D20 rounds the size a second time and centres it in what it was given.
	int scaledWidth  = (int)((((int64_t)sourceWidth  * (int64_t)rateX) + 0x8000) >> 16);
	int scaledHeight = (int)((((int64_t)sourceHeight * (int64_t)rateY) + 0x8000) >> 16);
	if(scaledWidth > 0 && scaledHeight > 0)
	{
		int originX = width / 2 - scaledWidth / 2;
		int originY = height / 2 - scaledHeight / 2;
		int stepX = (int)(((int64_t)sourceWidth  << 16) / scaledWidth);
		int stepY = (int)(((int64_t)sourceHeight << 16) / scaledHeight);

		int left   = originX < 0 ? 0 : originX;
		int top    = originY < 0 ? 0 : originY;
		int right  = originX + scaledWidth;
		int bottom = originY + scaledHeight;
		if(right > width)
			right = width;
		if(bottom > height)
			bottom = height;

		if(filter == 0)
		{
			for(int row = top; row < bottom; row++)
			{
				int sourceRow = (int)(((int64_t)(row - originY) * stepY) >> 16);
				if(sourceRow >= sourceHeight)
					sourceRow = sourceHeight - 1;
				uint32_t* in  = (uint32_t*)(pixels + (size_t)sourceRow * sourceStride);
				uint32_t* out = (uint32_t*)(dst->bitmap + (size_t)row * dst->stride);
				for(int column = left; column < right; column++)
				{
					int sourceColumn = (int)(((int64_t)(column - originX) * stepX) >> 16);
					if(sourceColumn >= sourceWidth)
						sourceColumn = sourceWidth - 1;
					out[column] = in[sourceColumn];
				}
			}
		}
		else
		{
			// 0x00494F20: the box is 0x10000 / (rate >> 8) wide in 8.8, and the corner
			// advances so that the last box ends on the source's far edge.
			int boxStepX = rateX >= 0x100 ? 0x10000 / (rateX >> 8) : sourceWidth  << 8;
			int boxStepY = rateY >= 0x100 ? 0x10000 / (rateY >> 8) : sourceHeight << 8;
			int boxWidth  = (boxStepX + 0xFF) >> 8;
			int boxHeight = (boxStepY + 0xFF) >> 8;
			int walkX = (int)((((int64_t)(sourceWidth  - (boxStepX >> 8))) << 16) / scaledWidth);
			int walkY = (int)((((int64_t)(sourceHeight - (boxStepY >> 8))) << 16) / scaledHeight);

			for(int row = top; row < bottom; row++)
			{
				int boxTop = (int)(((int64_t)(row - originY) * walkY) >> 16);
				uint32_t* out = (uint32_t*)(dst->bitmap + (size_t)row * dst->stride);
				for(int column = left; column < right; column++)
				{
					int boxLeft = (int)(((int64_t)(column - originX) * walkX) >> 16);
					out[column] = Renderer_AverageBox(pixels, sourceWidth, sourceHeight,
					                                  sourceStride, src->mode,
					                                  boxLeft, boxTop, boxWidth, boxHeight);
				}
			}
		}
	}
	free(pixels);
	return 0;
}

int Renderer_DuplicateBitmap(Renderer_t* renderer, int destination, int source)
{
	Bitmap_t* src = Renderer_ResolveBitmap(renderer, source);
	if(src == NULL)
		return 2;

	int offsetX = src->offsetX;
	int offsetY = src->offsetY;
	Bitmap_t* dst = Renderer_CreateBitmap(renderer, destination, src->width, src->height, src->mode);
	if(dst == NULL)
		return 1;

	// Same size, same mode, so the whole surface goes across in one piece; the
	// source is re-resolved because the destination may have been the same slot.
	src = Renderer_ResolveBitmap(renderer, source);
	if(src != NULL && src != dst)
		memcpy(dst->bitmap, src->bitmap, (size_t)dst->stride * (size_t)dst->height);
	dst->offsetX = offsetX;
	dst->offsetY = offsetY;
	return 0;
}

int Renderer_CopyBitmap(Renderer_t* renderer, int destination, int source, int offsetX, int offsetY, int width, int height)
{
	Bitmap_t* src = Renderer_ResolveBitmap(renderer, source);
	if(src == NULL)
		return 2;
	if(width == 0 || height == 0)
		return 3;

	Bitmap_t* dst = Renderer_CreateBitmap(renderer, destination, width, height, src->mode);
	if(dst == NULL)
		return 1;

	int pixelBytes = Renderer_ModePixelBytes(src->mode);
	for(int y = 0; y < height; y++)
	{
		int sourceY = y + offsetY;
		if(sourceY < 0 || sourceY >= src->height)
			continue;
		for(int x = 0; x < width; x++)
		{
			int sourceX = x + offsetX;
			if(sourceX < 0 || sourceX >= src->width)
				continue;
			memcpy(dst->bitmap + (size_t)y * dst->stride + (size_t)x * pixelBytes,
			       src->bitmap + (size_t)sourceY * src->stride + (size_t)sourceX * pixelBytes,
			       (size_t)pixelBytes);
		}
	}
	return 0;
}

void Renderer_DrawScreen(Renderer_t* renderer)
{
	if(renderer->engine->window == NULL)
		return;
	//if(renderer->screens[renderer->activeScreen] == NULL)
	//	return;
	SDL_Surface* windowSurface = SDL_GetWindowSurface(renderer->engine->window);
	SDL_Rect* sourceRect = NULL;
	SDL_Rect destRect;
	destRect.w = 0;
	destRect.h = 0;
	uint32_t colour = SDL_MapRGB(windowSurface->format, 0, 0, 0);
	SDL_FillRect(windowSurface, NULL, colour);
	// Over the slots, not over the count: a freed slot is reused, so the live
	// windows are not the first `allocatedScreens` of them.
	for(int i = 0; i < RENDERER_MAX_SCREENS; i++)
	{
		Screen_t* screen = renderer->screens[i];
		if(screen == NULL)
			continue;
		destRect.x = screen->x;
		destRect.y = screen->y;
		SDL_BlitSurface(screen->surface, sourceRect, windowSurface, &destRect);
	}
	SDL_UpdateWindowSurface(renderer->engine->window);
}

void Renderer_SetScreenParams(Renderer_t* renderer, uint32_t handle, int x, int y)
{
	uint32_t id = handle & OBJECT_INDEX_MASK;
	if(id >= RENDERER_MAX_SCREENS || renderer->screens[id] == NULL)
	{
		printf("[Renderer]: Warning: Attempting to set params on invalid screen object (%d)\n", id);
		return;
	}
	Screen_t* screen = renderer->screens[id];
	screen->x = x;
	screen->y = y;
}

void Renderer_Free(Renderer_t* renderer)
{
	if(renderer == NULL)
		return;
	for(int i = 0; i < RENDERER_MAX_BITMAPS; i++)
		Renderer_DestroyBitmap(renderer, i);
	for(int i = 0; i < RENDERER_MAX_SCREENS; i++)
	{
		if(renderer->screens[i] == NULL)
			continue;
		SDL_FreeSurface(renderer->screens[i]->surface);
		free(renderer->screens[i]->bitmap);
		free(renderer->screens[i]);
		renderer->screens[i] = NULL;
	}
	free(renderer);
}
