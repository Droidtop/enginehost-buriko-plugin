#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "window.h"
#include "sprite5.h"
#include "icon.h"

void Window_ReserveContent(Renderer_t* renderer, DisplayObject_t* window, uint32_t count)
{
	if(window == NULL)
		return;

	// 0x0042BCD8: every slot out of the window's list and destroyed, then the array,
	// then the list itself. The sprites are the window's own - they are in no handle
	// table, so nothing else can be holding one.
	for(uint32_t i = 0; i < window->contentSlotCount; i++)
	{
		DisplayObject_t* sprite = window->contentSlots[i];
		if(sprite == NULL)
			continue;
		Object_ListRemoveFrom(window->contentList, sprite);
		Object_FreeDetached(sprite);
	}
	free(window->contentSlots);
	if(window->contentList != NULL)
	{
		Object_ListClear(window->contentList);
		free(window->contentList);
	}
	window->contentSlots     = NULL;
	window->contentSlotCount = 0;
	window->contentList      = NULL;

	// 0x0042BD43: a count of zero leaves it with nothing, and that is the whole of
	// the call - no array, no list, no origin.
	if(count == 0)
		return;

	window->contentSlots = (DisplayObject_t**)calloc(count, sizeof(DisplayObject_t*));
	window->contentList  = (ObjectList_t*)calloc(1, sizeof(ObjectList_t));
	if(window->contentSlots == NULL || window->contentList == NULL)
	{
		free(window->contentSlots);
		free(window->contentList);
		window->contentSlots = NULL;
		window->contentList  = NULL;
		return;
	}
	window->contentSlotCount = count;

	// 0x0042BDBF copies the window's own six-dword bitmap descriptor to +0x318 and
	// its rectangle to +0x330, and hands both to the list's constructor: the list's
	// target is the window's pixels. This engine's walk is given its target at draw
	// time instead (Object_DrawListOf), so there is nothing to copy here - but the
	// list is still the window's, and it is still the window's pixels it reaches.

	// 0x0042BE55: the origin, out of the display mode's own size.
	int width  = (int)gDisplayModeWidth[gDisplaySizeIndex & 7];
	int height = (int)gDisplayModeHeight[gDisplaySizeIndex & 7];
	(void)renderer;
	window->contentOriginX   = -(width / 2);
	window->contentOriginY   = -(height / 2);
	window->contentHalfWidth = width / 2;
}

void Window_Redraw(Renderer_t* renderer, DisplayObject_t* window, const Rect_t* rect)
{
	if(window == NULL || rect == NULL)
		return;

	// 0x0042CB1A: the window's +0x13C, which is whatever its own sizing answered -
	// a window with no pixels composes nothing.
	Bitmap_t pixels;
	if(!Renderer_WindowBitmap(renderer, window->handle, &pixels))
		return;

	// 0x0042CB40: the rectangle asked for, against the whole of the window. The
	// original answers its own +0x13C when the two do not meet.
	Rect_t area = *rect;
	Rect_t whole = { 0, 0, pixels.width - 1, pixels.height - 1 };
	if(!Renderer_RectIntersect(&area, &whole))
		return;

	for(int i = 0; i < 3; i++)
	{
		switch(window->layerOrder[i])
		{
			case WINDOW_LAYER_BACKGROUND:
			{
				// 0x0042CD75. The window's background image lives at +0x164,
				// guarded by +0x15C, and Grp0 0x86 (0x0042B380) is what puts one
				// there: that part of it is copied (mode 0x80) into the same
				// part of the window. With none, the area is cleared (0x0042CDDD
				// into 0x0040A620).
				Bitmap_t view = pixels;
				if(!Renderer_ClipBitmap(&view, &area))
					break;
				if(window->backgroundSet && window->backgroundPixels != NULL)
				{
					Bitmap_t background = pixels;
					background.bitmap = window->backgroundPixels;
					if(Renderer_ClipBitmap(&background, &area))
						Renderer_BlitView(&view, &background, BITMAP_BLEND_COPY, 0);
				}
				else
					Renderer_ClearBitmap(&view);
				break;
			}

			case WINDOW_LAYER_FRAME:
				// 0x0042CC29: the frame image at +0x184 behind +0x17C, and the
				// eight 0x2C-byte parts at +0x1B8 the constructor builds. Nothing
				// in this engine gives a window either yet, and the original's
				// own arm does nothing at all when both are empty, so this is
				// silent rather than refused.
				break;

			case WINDOW_LAYER_CONTENT:
				// 0x0042CBD0: with a content list (+0x340), the part being
				// redrawn is damaged in it (0x00430910) and the list composed
				// (0x00430FD0) onto its target, which 0x0042BDBF made the
				// window's own pixels and rectangle. A content sprite sits at
				// ((contentOriginX + x) << 16) about the device centre, and the
				// origin is minus half the display, so its anchor lands on (x, y)
				// of the window: the window's pixels are the list's coordinates.
				if(window->contentList != NULL)
					Object_DrawListOf(window->contentList, renderer, &pixels, &area);
				break;
		}
	}
}

void Window_RedrawAll(Renderer_t* renderer, DisplayObject_t* window)
{
	// 0x0042CAE0: the window's own rectangle, straight out of its pixels.
	Bitmap_t pixels;
	if(window == NULL || !Renderer_WindowBitmap(renderer, window->handle, &pixels))
		return;

	Rect_t whole = { 0, 0, pixels.width - 1, pixels.height - 1 };
	Window_Redraw(renderer, window, &whole);
}

int Window_SetClientArea(Renderer_t* renderer, DisplayObject_t* window, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	// 0x0042B9E0: all four inside the window (+0x18C wide, +0x190 high), else nothing.
	Bitmap_t pixels;
	if(window == NULL || !Renderer_WindowBitmap(renderer, window->handle, &pixels))
		return 0;
	if(left < 0 || left >= pixels.width || right < 0 || right >= pixels.width
	   || top < 0 || top >= pixels.height || bottom < 0 || bottom >= pixels.height)
		return 0;
	window->clientRect[0] = left;
	window->clientRect[1] = top;
	window->clientRect[2] = right;
	window->clientRect[3] = bottom;
	Window_ResetTextCursor(window);
	return 1;
}

void Window_ResetTextCursor(DisplayObject_t* window)
{
	// 0x0042C690: across, the top-left of the client area; down, its top-right.
	if(window->textDirection == 0)
	{
		window->textCursorX = window->clientRect[0];
		window->textCursorY = window->clientRect[1];
	}
	else if(window->textDirection == 1)
	{
		window->textCursorX = window->clientRect[2];
		window->textCursorY = window->clientRect[1];
	}
}


// The background surface (+0x160 / +0x164), made the window's size on first use; the
// window's sizing (0x0042B280) builds it cleared.
static int Window_Background(Renderer_t* renderer, DisplayObject_t* window, Bitmap_t* out)
{
	Bitmap_t pixels;
	if(window == NULL || !Renderer_WindowBitmap(renderer, window->handle, &pixels))
		return 0;
	if(window->backgroundPixels == NULL)
	{
		window->backgroundPixels = (uint8_t*)calloc(1, (size_t)pixels.stride * (size_t)pixels.height);
		if(window->backgroundPixels == NULL)
			return 0;
	}
	*out = pixels;
	out->bitmap = window->backgroundPixels;
	return 1;
}

uint32_t Window_SetBackgroundShown(Renderer_t* renderer, DisplayObject_t* window, uint32_t shown)
{
	// 0x0042B490: +0x15C, then the whole window redrawn.
	window->backgroundSet = shown;
	Window_RedrawAll(renderer, window);
	return 0;
}

uint32_t Window_FillBackground(Renderer_t* renderer, DisplayObject_t* window, uint32_t colour)
{
	// 0x0042B4A0: with pixels (+0x13C), the background surface filled with the
	// colour (0x0040A710) and the window redrawn; without, 1.
	Bitmap_t background;
	if(!Window_Background(renderer, window, &background))
		return 1;
	Renderer_FillSolid(&background, colour);
	Window_RedrawAll(renderer, window);
	return 0;
}

uint32_t Window_DrawOnBackground(Renderer_t* renderer, DisplayObject_t* window, int32_t bitmap,
                                 int32_t x, int32_t y, uint32_t mode, uint32_t level)
{
	// 0x0042B4E0: 1 without pixels, 2 for a bitmap that does not exist; otherwise the
	// bitmap blended onto the background surface at (x, y) with the mode and level
	// (0x0040A530), whose answers 1-4 become 5, 6, 7 and 4; on 0 the part it covered
	// is redrawn (0x0042CE10).
	Bitmap_t background;
	if(!Window_Background(renderer, window, &background))
		return 1;
	Bitmap_t* image = Renderer_ResolveBitmap(renderer, bitmap);
	if(image == NULL)
		return 2;
	int r = Renderer_BlitAt(&background, x, y, image, (int)mode, (int)level);
	switch(r)
	{
		case 0:
		{
			Rect_t area = { x, y, x + image->width - 1, y + image->height - 1 };
			Window_Redraw(renderer, window, &area);
			return 0;
		}
		case 1: return 5;
		case 2: return 6;
		case 3: return 7;
		case 4: return 4;
	}
	return (uint32_t)r;
}

uint32_t Window_SetBackground(Renderer_t* renderer, DisplayObject_t* window, int32_t bitmap)
{
	// 0x0042B380. A window with no pixels yet (+0x13C) answers 1.
	Bitmap_t background;
	if(!Window_Background(renderer, window, &background))
		return 1;
	if(bitmap != -1)
	{
		// A bitmap that does not exist answers 2 and changes nothing (0x0042B3E6).
		Bitmap_t* image = Renderer_ResolveBitmap(renderer, bitmap);
		if(image == NULL)
			return 2;
		// Cleared, then the image copied onto it at its corner (0x0040A530, 0x80).
		Renderer_ClearBitmap(&background);
		Renderer_BlitAt(&background, 0, 0, image, BITMAP_BLEND_COPY, 0);
	}
	else
		Renderer_ClearBitmap(&background);
	// 0x0042B490: +0x15C, then the whole window redrawn (0x0042CAE0).
	return Window_SetBackgroundShown(renderer, window, bitmap != -1);
}

uint32_t Window_SetContentSlot(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot,
                               int32_t bitmap, int32_t x, int32_t y,
                               int32_t anchorX, int32_t anchorY, uint32_t layer)
{
	// 0x0042BFB0. A slot outside +0x310 is 9.
	if(window == NULL || slot >= window->contentSlotCount)
		return 9;

	// 0x0042BFEA: whatever the slot held is taken out of the list (0x00430850) and
	// destroyed.
	DisplayObject_t* old = window->contentSlots[slot];
	if(old != NULL)
	{
		Object_ListRemoveFrom(window->contentList, old);
		Object_FreeDetached(old);
		window->contentSlots[slot] = NULL;
	}

	// 0x0042C015: a new sprite (0x00425790 with the slot as its serial and 0 for
	// +0x100), its +0x48 cleared (0x0041AEC0).
	DisplayObject_t* sprite = Object_CreateDetachedSprite(slot);
	if(sprite == NULL)
		return 2;
	window->contentSlots[slot] = sprite;
	sprite->priority = 0;

	// 0x0042C05E: 0x00427240 with the position about the content origin (16.16, z 0),
	// the bitmap and no second, level 0, content kind 0, the anchor, angle 0, the
	// focal distance +0x34C, no perspective, smooth, blend mode 0x20, effect level 0
	// and the layer.
	const char* unread = NULL;
	uint32_t result = Sprite5_Setup(renderer, sprite, 0,
	                                (int32_t)((uint32_t)(window->contentOriginX + x) << 16),
	                                (int32_t)((uint32_t)(window->contentOriginY + y) << 16), 0,
	                                bitmap, -1, 0, 0, anchorX, anchorY, 0,
	                                (uint32_t)window->contentHalfWidth, 0, 1, 0x20, 0, layer, &unread);
	if(unread != NULL)
		printf("[Engine]: Warning: a window content sprite reached unported work: %s\n", unread);
	if(result != 0)
	{
		// 0x0042C0EC: the sprite destroyed, the slot emptied, 2.
		Object_FreeDetached(sprite);
		window->contentSlots[slot] = NULL;
		return 2;
	}

	// 0x0042C0B9: shown (vtable+0x04 with 1) and into the window's list (0x004307D0).
	Object_SetVisible(sprite, 1);
	Object_ListInsertInto(window->contentList, sprite);
	return 0;
}

int Window_RedrawContentSlot(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot)
{
	// 0x0042C2A0: nothing for an empty or missing slot; otherwise the part of the
	// window the sprite covers (vtable+0x24) is redrawn (0x0042CB10). The rectangle it
	// then moves by the window's screen position (0x00409170) is its caller's to use,
	// and 0x0044B340 drops it.
	if(window == NULL || slot >= window->contentSlotCount || window->contentSlots[slot] == NULL)
		return 0;
	Rect_t area;
	Sprite5_ScreenBounds(window->contentSlots[slot], &area);
	Window_Redraw(renderer, window, &area);
	return 1;
}

uint32_t Window_SetContent(Renderer_t* renderer, DisplayObject_t* window, const IconContent_t* content)
{
	// 0x0044B340. First the window is emptied: the eight text parts (0x0042BC90 ->
	// 0x0042BB90 with 0 each), the content slots (0x0042BCB0 with 0), the text
	// surface cleared (0x0042BA90 -> 0x0040A620, then 0x0042CAE0), its weight +0x19C
	// and flag +0x17C set to 0 (0x0042B630, 0x0042B620), and the whole window redrawn
	// (0x0042CAE0). This engine's windows carry no text layer yet, so of those the
	// slots and the redraws are what there is to do.
	Window_ReserveContent(renderer, window, 0);
	Window_RedrawAll(renderer, window);

	// 0x0044B38D: the entry count in 1..0x100, else 0x80000001.
	uint32_t count = content->raw[0];
	if((int32_t)count <= 0 || count > ICON_CONTENT_MAX_COUNT)
		return 0x80000001u;

	// 0x0044B3B8: every entry's part count (the whole dword +0x00) in 1..0x100, else
	// 0x80000002; their sum is the number of slots.
	uint32_t total = 0;
	for(uint32_t i = 0; i < count; i++)
	{
		int32_t parts = (int32_t)content->entries[i].raw[0];
		total += (uint32_t)parts;
		if(parts <= 0 || parts > ICON_CONTENT_MAX_COUNT)
			return 0x80000002u;
	}
	Window_ReserveContent(renderer, window, total);

	// 0x0044B409: one slot per part, in order, whether or not the part is used.
	uint32_t slot = 0;
	for(uint32_t i = 0; i < count; i++)
	{
		const IconContentEntry_t* entry = &content->entries[i];
		uint32_t parts = entry->raw[0];
		for(uint32_t j = 0; j < parts; j++, slot++)
		{
			const uint32_t* part = entry->parts + (size_t)j * ICON_CONTENT_PART_WORDS;
			// +0x04: a part with 0 there is skipped.
			if(part[1] == 0)
				continue;
			// +0x20 is the bitmap; for the entry's selected part (+0x0C) it is +0x28
			// when that is not -1 (and +0x20 is not -1 either).
			int32_t bitmap = (int32_t)part[8];
			if(bitmap != -1 && j == entry->raw[3] && (int32_t)part[10] != -1)
				bitmap = (int32_t)part[10];
			// 0x00407F20: a bitmap that does not exist leaves the slot empty.
			if(Renderer_ResolveBitmap(renderer, bitmap) == NULL)
				continue;
			// +0xC0's flags choose the layer: bit 4 takes +0x04, bit 1 takes +0x0C
			// (the part's y), and otherwise it is the slot number.
			uint32_t flags = part[48];
			uint32_t layer = (flags & 0x10) ? part[1] : (flags & 0x02) ? part[3] : slot;
			// The position is +0x08/+0x0C plus the anchor +0x10/+0x14, so the
			// bitmap's corner lands on +0x08/+0x0C.
			Window_SetContentSlot(renderer, window, slot, bitmap,
			                      (int32_t)(part[2] + part[4]), (int32_t)(part[3] + part[5]),
			                      (int32_t)part[4], (int32_t)part[5], layer);
			Window_RedrawContentSlot(renderer, window, slot);
		}
	}
	return 0;
}
