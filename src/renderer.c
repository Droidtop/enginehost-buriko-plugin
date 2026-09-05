#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "renderer.h"
#include "engine.h"
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
    screen->bitmap = bitmap;
    screen->surface = surface;
    renderer->screens[renderer->allocatedScreens] = screen;
    uint32_t id = 0xC0000000 + renderer->allocatedScreens;
    renderer->activeScreen = renderer->allocatedScreens;
    renderer->allocatedScreens++;
    printf("[Renderer]: Created screen object (0x%08X) width size %dx%d\n", id, width, height);
    return id;
}

void Renderer_DestroyScreen(Renderer_t* renderer, uint32_t handle)
{
	uint32_t id = 0x0000001F & handle;
	if(renderer->screens[id] == NULL)
		return;
	if(renderer->screens[id]->surface != NULL)
		SDL_FreeSurface(renderer->screens[id]->surface);
	if(renderer->screens[id]->bitmap != NULL)
		free(renderer->screens[id]->bitmap);
	free(renderer->screens[id]);
	renderer->screens[id] = NULL;
	renderer->allocatedScreens--;
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

void Renderer_DestroyBitmap(Renderer_t* renderer, int id)
{
	if(renderer == NULL || id < 0 || id >= RENDERER_MAX_BITMAPS)
		return;
	if(renderer->bitmaps[id] == NULL)
		return;
	free(renderer->bitmaps[id]->bitmap);
	free(renderer->bitmaps[id]);
	renderer->bitmaps[id] = NULL;
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
	uint8_t* pixels = NULL;

	if(CBG_IsCompressedBG(file, fileSize))
	{
		pixels = CBG_Decode(file, fileSize, &width, &height, &bits);
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
	for(int i = 0; i < renderer->allocatedScreens; i++)
	{
		Screen_t* screen = renderer->screens[i];
		destRect.x = screen->x;
		destRect.y = screen->y;
		SDL_BlitSurface(screen->surface, sourceRect, windowSurface, &destRect);
	}
	SDL_UpdateWindowSurface(renderer->engine->window);
}

void Renderer_SetScreenParams(Renderer_t* renderer, uint32_t handle, int x, int y)
{
	uint32_t id = 0x0000001F & handle;
	if(renderer->screens[id] == NULL)
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
