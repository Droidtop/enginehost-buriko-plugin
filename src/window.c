#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "window.h"

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
		free(sprite);
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
				// guarded by +0x15C, and 0x0042B380 is what puts one there -
				// nothing in this engine does yet. With none, the original
				// clears the area (0x0042CDDD into 0x0040A620), which is what
				// makes the window's pixels start from nothing every redraw.
				Bitmap_t view = pixels;
				if(Renderer_ClipBitmap(&view, &area))
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
				// 0x0042CBD0: the window's own display list, drawn into the
				// window's pixels. Not written: 0x0042BFB0 places a content
				// sprite at contentOriginX + x, an origin measured from the
				// middle of the SCREEN, and what maps that onto the window's own
				// pixels is 0x00430FD0's target, which is not read yet. Writing
				// the draw before that is written is guessing where the pixels
				// go.
				if(window->contentList != NULL && window->contentList->head != NULL)
				{
					printf("[Engine]: Error: a window's content layer (0x0042CBD0"
					       " through 0x00430FD0) is not written yet\n");
				}
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
