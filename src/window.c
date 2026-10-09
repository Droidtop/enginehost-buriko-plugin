#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "window.h"
#include "font.h"
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

// The text surface's pixel mode: 0x0041C060 with no template asks the device's own
// mode (0x00407B10) and makes 24-bit into 32-bit, so the text keeps its alpha.
static int Window_TextMode(Renderer_t* renderer)
{
	int mode = Renderer_ScreenMode(renderer);
	return mode == BITMAP_MODE_24 ? BITMAP_MODE_32 : mode;
}

int Window_TextSurface(Renderer_t* renderer, DisplayObject_t* window, Bitmap_t* out)
{
	Bitmap_t pixels;
	if(window == NULL || !Renderer_WindowBitmap(renderer, window->handle, &pixels))
		return 0;
	int mode = Window_TextMode(renderer);
	int stride = pixels.width * Renderer_ModePixelBytes(mode);
	if(window->textPixels == NULL)
	{
		// 0x0042B320: made with the window's pixels, and cleared (0x0042BA90).
		window->textPixels = (uint8_t*)calloc(1, (size_t)stride * (size_t)pixels.height);
		if(window->textPixels == NULL)
			return 0;
	}
	*out = pixels;
	out->mode = mode;
	out->stride = stride;
	out->bitmap = window->textPixels;
	return 1;
}

// 0x0042CC29, the frame layer: the text surface over the window when it is shown -
// through a copy that has the eight parts erased out of it first when the window's
// +0x3BC asks for that - and then the eight parts, each with its own weight.
static void Window_ComposeTextLayer(Renderer_t* renderer, DisplayObject_t* window,
                                    const Bitmap_t* pixels, const Rect_t* area)
{
	Bitmap_t view = *pixels;
	if(!Renderer_ClipBitmap(&view, area))
		return;
	Bitmap_t text;
	if(window->textShown && Window_TextSurface(renderer, window, &text) && Renderer_ClipBitmap(&text, area))
	{
		if(window->field3BC != 0)
		{
			// 0x0042CC8A: a copy of that part of the text (0x00409080, 0x0040A9E0
			// mode 0x80), every part erased out of it (mode 0x40, weight 1), and the
			// copy blended on.
			Bitmap_t copy = text;
			copy.stride = text.width * Renderer_ModePixelBytes(text.mode);
			copy.bitmap = (uint8_t*)malloc((size_t)copy.stride * (size_t)copy.height);
			if(copy.bitmap != NULL)
			{
				Renderer_BlitView(&copy, &text, BITMAP_BLEND_COPY, 0);
				for(int i = 0; i < 8; i++)
					if(window->parts[i].enabled && window->parts[i].surface.bitmap != NULL)
						Renderer_BlitAt(&copy, window->parts[i].x - area->left, window->parts[i].y - area->top,
						                &window->parts[i].surface, BITMAP_BLEND_ERASE, 1);
				Renderer_BlitView(&view, &copy, BITMAP_BLEND_ALPHA_TRANS, (int)window->textTransparency);
				free(copy.bitmap);
			}
		}
		else
			Renderer_BlitView(&view, &text, BITMAP_BLEND_ALPHA_TRANS, (int)window->textTransparency);
	}
	// 0x0042CD33: the parts, at their places, with their own weight (mode 1).
	for(int i = 0; i < 8; i++)
		if(window->parts[i].enabled && window->parts[i].surface.bitmap != NULL)
			Renderer_BlitAt(&view, window->parts[i].x - area->left, window->parts[i].y - area->top,
			                &window->parts[i].surface, BITMAP_BLEND_ALPHA_TRANS, (int)window->parts[i].weight);
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
				Window_ComposeTextLayer(renderer, window, &pixels, &area);
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

uint32_t Window_SetFont(Renderer_t* renderer, DisplayObject_t* window, const char* name, int32_t size,
                        int32_t width, uint32_t bold, uint32_t proportional, uint32_t reserveColumn)
{
	// 0x00440AA0: the two plain values go in first, whatever the font does.
	window->proportional = proportional;     // 0x0042C5F0
	window->reserveColumn = reserveColumn;   // 0x0042C610

	// 0x0042C490 -> 0x00409290 -> 0x0042F1D0: the id goes straight into +0x350, and
	// only a font that was made moves the size and the advance.
	uint32_t id = 0;
	uint32_t result = Font_Open(name, size, width, (int)bold, &id);
	window->fontId = id;
	if(result != 0)
		return result;
	window->fontSize = size;
	window->fontAdvance = (size * width) / 100;

	// 0x0042C4E1: with a column reserved, the client area's right edge may come no
	// nearer the window's right edge than one advance; the area is set again through
	// 0x0042B9E0, which resets the cursor.
	Bitmap_t pixels;
	if(window->reserveColumn && Renderer_WindowBitmap(renderer, window->handle, &pixels))
	{
		int32_t limit = (pixels.width - 1) - window->fontAdvance;
		if(window->clientRect[2] > limit)
			Window_SetClientArea(renderer, window, window->clientRect[0], window->clientRect[1],
			                     limit, window->clientRect[3]);
	}
	return 0;
}

int Window_SetLineSpacing(DisplayObject_t* window, uint32_t spacing)
{
	if(spacing > 0x320)
		return 0;
	window->lineSpacing = spacing;
	return 1;
}

int Window_SetDirection(DisplayObject_t* window, uint32_t direction)
{
	if(direction > 1)
		return 0;
	window->textDirection = direction;
	Window_ResetTextCursor(window);
	return 1;
}

int Window_SetAlignment(DisplayObject_t* window, uint32_t align)
{
	if(align > 2)
		return 0;
	window->textAlign = align;
	return 1;
}

int32_t Window_LineHeight(const DisplayObject_t* window)
{
	// 0x0042C580 -> 0x004097B0: the spacing is that per cent of the size.
	return (window->fontSize * (int32_t)window->lineSpacing) / 100 + window->fontSize;
}

void Window_SetTextShown(DisplayObject_t* window, uint32_t shown)
{
	window->textShown = shown;
}

void Window_SetTextTransparency(DisplayObject_t* window, uint32_t transparency)
{
	window->textTransparency = transparency;
}

void Window_ClearText(Renderer_t* renderer, DisplayObject_t* window)
{
	Bitmap_t text;
	if(Window_TextSurface(renderer, window, &text))
		Renderer_ClearBitmap(&text);
	Window_RedrawAll(renderer, window);
}

void Window_TextArea(const DisplayObject_t* window, Rect_t* rect)
{
	Window_ClientRect(window, rect);
	if(window->reserveColumn)
		rect->right += window->fontSize;
}

uint32_t Window_DrawText(Renderer_t* renderer, DisplayObject_t* window, int32_t x, int32_t y,
                         Bitmap_t* image, int mode, int weight)
{
	Bitmap_t text;
	if(!Window_TextSurface(renderer, window, &text))
		return 0;
	Rect_t area;
	Window_TextArea(window, &area);
	Bitmap_t view = text;
	if(!Renderer_ClipBitmap(&view, &area))
		return 4;
	switch(Renderer_BlitAt(&view, x - area.left, y - area.top, image, mode, weight))
	{
		case 0:
		{
			Rect_t covered = { x, y, x + image->width - 1, y + image->height - 1 };
			Window_Redraw(renderer, window, &covered);
			return 0;
		}
		case 1: return 5;
		case 2: return 6;
		case 3: return 7;
		case 4: return 4;
	}
	return 5;
}

int Window_NewLine(DisplayObject_t* window)
{
	Rect_t client;
	Window_ClientRect(window, &client);
	int32_t line = Window_LineHeight(window);
	if(window->textDirection == 0)
	{
		// 0x0042C725: back to the left edge, and down a line when another one still
		// fits under it.
		window->textCursorX = client.left;
		if(window->textCursorY + line * 2 > client.bottom + 1)
			return 0;
		window->textCursorY += line;
		return 1;
	}
	if(window->textDirection == 1)
	{
		// 0x0042C70C: down, a column further left, back to the top.
		window->textCursorX -= line;
		window->textCursorY = client.top;
		return 1;
	}
	return 0;
}

int Window_Fits(const DisplayObject_t* window, int32_t extent)
{
	Rect_t client;
	Window_ClientRect(window, &client);
	if(window->textDirection == 0)
		return window->textCursorX + extent <= client.right + 1;
	if(window->textDirection == 1)
		return window->textCursorY + extent <= client.bottom + 1;
	// 0x0042C7B1: no direction but 0 and 1 can be set (0x0042C630).
	return 0;
}

void Window_AdvanceCursor(DisplayObject_t* window, int32_t amount)
{
	if(window->textDirection == 0)
		window->textCursorX += amount;
	else if(window->textDirection == 1)
		window->textCursorY += amount;
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

// ----------------------------------------------------------------------------------
// A content slot's sprite, changed in place (0x0042C140 to 0x0042C300). Each answers
// 9 for a slot the window does not have or that is empty.
// ----------------------------------------------------------------------------------
static DisplayObject_t* Window_Slot(DisplayObject_t* window, uint32_t slot)
{
	if(window == NULL || slot >= window->contentSlotCount)
		return NULL;
	return window->contentSlots[slot];
}

uint32_t Window_SlotSetEnabled(DisplayObject_t* window, uint32_t slot, uint32_t enabled)
{
	// 0x0042C140: vtable+0x0C's setter (0x0041AE30).
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 9;
	Object_SetEnabled(sprite, (int)enabled);
	return 0;
}

uint32_t Window_SlotSetBitmap(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot, int32_t bitmap)
{
	// 0x0042C170: the content setter (0x004273C0); 2 when it refuses.
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 9;
	return Object_SetContentBitmap(renderer, sprite, bitmap, NULL) != OBJECT_CONTENT_OK ? 2 : 0;
}

uint32_t Window_SlotSetPosition(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot,
                                int32_t x, int32_t y, int32_t z)
{
	// 0x0042C1B0: the 3D position about the content origin (vtable+0x3C), then the
	// sprite re-filed in the window's list (0x004308B0).
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 9;
	const char* unread = Sprite5_SetPosition3D(renderer, sprite,
	                                           (int32_t)((uint32_t)(window->contentOriginX + x) << 16),
	                                           (int32_t)((uint32_t)(window->contentOriginY + y) << 16),
	                                           (int32_t)((uint32_t)z << 16));
	if(unread != NULL)
		printf("[Engine]: Warning: a window content sprite reached unported work: %s\n", unread);
	Object_ListRemoveFrom(window->contentList, sprite);
	Object_ListInsertInto(window->contentList, sprite);
	return 0;
}

uint32_t Window_SlotSetAngle(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot, int32_t angle)
{
	// 0x0042C220: the sprite's angle (0x00428130, its parameter 0x41).
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 9;
	uint32_t result;
	const char* unread = NULL;
	Sprite5_SetParameter(renderer, sprite, 0x41, (uint32_t)angle, 0, &result, &unread);
	return 0;
}

uint32_t Window_SlotSetLayer(DisplayObject_t* window, uint32_t slot, uint32_t layer)
{
	// 0x0042C250: the layer (vtable+0x54, 0x0041B980, which leaves 0x10000 and up
	// alone), then the sprite re-filed in the window's list (0x004308B0).
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 9;
	if(layer < 0x10000)
		sprite->layer = layer;
	Object_ListRemoveFrom(window->contentList, sprite);
	Object_ListInsertInto(window->contentList, sprite);
	return 0;
}

int Window_SlotRedrawWithout(Renderer_t* renderer, DisplayObject_t* window, uint32_t slot)
{
	// 0x0042C300: the part of the window the sprite covers redrawn with the sprite
	// hidden, and the sprite shown again - what a sprite about to move leaves behind.
	DisplayObject_t* sprite = Window_Slot(window, slot);
	if(sprite == NULL)
		return 0;
	Object_SetVisible(sprite, 0);
	Window_RedrawContentSlot(renderer, window, slot);
	Object_SetVisible(sprite, 1);
	return 1;
}

// ----------------------------------------------------------------------------------
// The eight parts of the text layer (0x0042BB90 to 0x0042C410).
// ----------------------------------------------------------------------------------
void Window_ClientRect(const DisplayObject_t* window, Rect_t* rect)
{
	// 0x0042C380: +0x1A0..+0x1AC.
	rect->left = window->clientRect[0];
	rect->top = window->clientRect[1];
	rect->right = window->clientRect[2];
	rect->bottom = window->clientRect[3];
}

// 0x0042CE50: the part's area redrawn as if it were shown or not (`shown`), when it is
// on and has an image; answers whether the index is one of the eight.
static int Window_RedrawPart(Renderer_t* renderer, DisplayObject_t* window, int index, uint32_t shown)
{
	if(index < 0 || index >= 8)
		return 0;
	if(window->parts[index].enabled == 0 || window->parts[index].surface.bitmap == NULL)
		return 1;
	uint32_t was = window->parts[index].enabled;
	window->parts[index].enabled = shown;
	// 0x0042CE10: the image's rectangle at the part's place.
	Rect_t area = { window->parts[index].x, window->parts[index].y,
	                window->parts[index].x + window->parts[index].surface.width - 1,
	                window->parts[index].y + window->parts[index].surface.height - 1 };
	Window_Redraw(renderer, window, &area);
	window->parts[index].enabled = was;
	return 1;
}

void Window_EnablePart(Renderer_t* renderer, DisplayObject_t* window, int index, uint32_t enabled)
{
	// 0x0042BB90.
	if(window == NULL || index < 0 || index >= 8)
		return;
	Window_RedrawPart(renderer, window, index, 0);
	window->parts[index].enabled = enabled;
	Window_RedrawPart(renderer, window, index, 1);
}

void Window_SetPartPosition(Renderer_t* renderer, DisplayObject_t* window, int index,
                            int32_t x, int32_t y, uint32_t weight)
{
	// 0x0042BBC0.
	if(window == NULL || index < 0 || index >= 8)
		return;
	Window_RedrawPart(renderer, window, index, 0);
	window->parts[index].x = x;
	window->parts[index].y = y;
	window->parts[index].weight = weight;
	Window_RedrawPart(renderer, window, index, 1);
}

uint32_t Window_SetPartBitmap(Renderer_t* renderer, DisplayObject_t* window, int index, const Bitmap_t* bitmap)
{
	// 0x0042BC10: 4 for an image in a mode the display cannot take (0x0040A9D0);
	// otherwise the part's own surface is made the image's size (0x0041C060) and the
	// image copied into it (0x0040A9E0, mode 0x80).
	if(window == NULL || index < 0 || index >= 8 || bitmap == NULL)
		return 4;
	int bytes = Renderer_ModePixelBytes(bitmap->mode);
	if(bytes <= 0)
		return 4;
	Window_RedrawPart(renderer, window, index, 0);
	Bitmap_t* surface = &window->parts[index].surface;
	size_t size = (size_t)bitmap->stride * (size_t)bitmap->height;
	uint8_t* pixels = (uint8_t*)realloc(surface->bitmap, size > 0 ? size : 1);
	if(pixels != NULL)
	{
		memcpy(pixels, bitmap->bitmap, size);
		*surface = *bitmap;
		surface->bitmap = pixels;
	}
	Window_RedrawPart(renderer, window, index, 1);
	return 0;
}

int Window_PartScreenRect(Renderer_t* renderer, DisplayObject_t* window, int index, Rect_t* rect)
{
	// 0x0042C410: the part's image rectangle at the part's place, moved by the
	// window's screen position (vtable+0x34); 0 when the part is off or empty.
	(void)renderer;
	if(window == NULL || index < 0 || index >= 8)
		return 0;
	if(window->parts[index].enabled == 0 || window->parts[index].surface.bitmap == NULL)
		return 0;
	Rect_t where;
	Object_GetScreenBounds(window, &where);
	rect->left = where.left + window->parts[index].x;
	rect->top = where.top + window->parts[index].y;
	rect->right = rect->left + window->parts[index].surface.width - 1;
	rect->bottom = rect->top + window->parts[index].surface.height - 1;
	return 1;
}
