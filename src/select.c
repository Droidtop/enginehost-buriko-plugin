#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "select.h"
#include "engine.h"
#include "input.h"
#include "object.h"
#include "process.h"
#include "region.h"
#include "renderer.h"
#include "window.h"

#define SELECT_MAX 64

typedef struct
{
	int32_t          x, y;       // relative to the window's client origin
	int32_t          image;
	int32_t          lit;        // 0xB0's lit image; 0xB1 shows the image lit
	int32_t          mask;       // 0xB1's hit mask, or -1
	uint32_t         key;        // its hotkey, or 0
	DisplayObject_t* object;     // what the pointer hits
	int              drawn;
} SelectChoice_t;

typedef struct
{
	Thread_t*        thread;
	Process_t*       process;
	uint32_t         windowHandle;
	int              ex;
	uint32_t         pixel;
	uint32_t         focus;
	int32_t          count;
	SelectChoice_t   choices[SELECT_MAX];
	int32_t          lit;        // the choice under the pointer, or -1
	int32_t          litX, litY; // the point inside it
	ProcessEvent_t*  seen;       // the last event read
} Select_t;

static DisplayObject_t* Select_Window(Select_t* sel)
{
	return Object_ResolveKind(sel->windowHandle, OBJECT_TYPE_WINDOW);
}

static void Select_Draw(Select_t* sel, int32_t index, int lit)
{
	DisplayObject_t* window = Select_Window(sel);
	Renderer_t* renderer = gEngine != NULL ? gEngine->renderer : NULL;
	if(window == NULL || renderer == NULL || index < 0 || !sel->choices[index].drawn)
		return;
	SelectChoice_t* c = &sel->choices[index];
	int32_t bitmap = lit && c->lit != -1 ? c->lit : c->image;
	Window_SlotSetBitmap(renderer, window, (uint32_t)index, bitmap);
	Window_RedrawContentSlot(renderer, window, (uint32_t)index);
	gObjectDamage++;
}

// The choice under the pointer and the point inside it, or -1.
static int32_t Select_Hit(Select_t* sel, int32_t* outX, int32_t* outY)
{
	int32_t mx, my;
	Input_GetMouse(&mx, &my);
	for(int32_t i = sel->count - 1; i >= 0; i--)
	{
		SelectChoice_t* c = &sel->choices[i];
		if(!c->drawn || c->object == NULL)
			continue;
		Rect_t b;
		Object_GetScreenBounds(c->object, &b);
		if(mx < b.left || mx > b.right || my < b.top || my > b.bottom)
			continue;
		if(sel->pixel && !Object_HitTest(c->object, mx - b.left, my - b.top, 1))
			continue;
		*outX = mx - b.left;
		*outY = my - b.top;
		return i;
	}
	return -1;
}

static void Select_Free(void* context)
{
	Select_t* sel = (Select_t*)context;
	DisplayObject_t* window = Select_Window(sel);
	for(int32_t i = 0; i < sel->count; i++)
	{
		SelectChoice_t* c = &sel->choices[i];
		if(window != NULL && c->drawn)
			Window_SlotSetEnabled(window, (uint32_t)i, 0);
		if(c->object != NULL)
		{
			Region_RemoveByOwner(c->object);
			if(window != NULL)
				Object_Detach(window, c->object);
			Object_FreeDetached(c->object);
		}
	}
	if(window != NULL)
		Region_RemoveByOwner(window);
	gObjectDamage++;
	free(sel);
}

static int Select_Finish(Select_t* sel, int32_t index, int32_t x, int32_t y)
{
	Thread_PushStack(sel->thread, (uint32_t)x);
	Thread_PushStack(sel->thread, (uint32_t)y);
	Thread_PushStack(sel->thread, (uint32_t)index);
	return 1;
}

static int Select_Run(void* context)
{
	Select_t* sel = (Select_t*)context;
	DisplayObject_t* window = Select_Window(sel);
	if(window == NULL)
		return Select_Finish(sel, -1, 0, 0);
	// A message of type 0x200 posted to the process forces the choice.
	for(ProcessEvent_t* e = sel->seen != NULL ? sel->seen->next : sel->process->events; e != NULL; e = e->next)
	{
		sel->seen = e;
		if(e->value[0] == 0x200)
		{
			int32_t index = (int32_t)e->value[1];
			return Select_Finish(sel, index >= 0 && index < sel->count ? index : -1, 0, 0);
		}
	}
	uint32_t key = Object_DrawKey(window);
	uint32_t bits = Input_RegionStateOf(0, key);
	if(sel->focus == 3)
		bits |= Input_RegionStateOf(key, key);
	// Cancel: the right button or the cancel key.
	if(bits & 0x202)
		return Select_Finish(sel, -1, 0, 0);
	// A choice's hotkey picks it, at (0, 0).
	for(int32_t i = 0; i < sel->count; i++)
		if(sel->choices[i].key != 0 && Input_Take(sel->choices[i].key))
			return Select_Finish(sel, i, 0, 0);
	int32_t x = 0, y = 0;
	int32_t hit = Select_Hit(sel, &x, &y);
	if(hit != sel->lit)
	{
		// The original posts message 0x20000001 (hasValue << 16 | index) here; where it goes
		// is not traced yet (Droidtop/tracker#123), so the redraw is all this does.
		Select_Draw(sel, sel->lit, 0);
		Select_Draw(sel, hit, 1);
		sel->lit = hit;
	}
	sel->litX = x;
	sel->litY = y;
	// A left click on the choice under the pointer.
	if((bits & 1) && sel->lit != -1)
		return Select_Finish(sel, sel->lit, sel->litX, sel->litY);
	return 0;
}

uint32_t Select_Start(Thread_t* thread, int ex)
{
	uint32_t pixel = Thread_PopStack(thread);
	uint32_t keysAddress = Thread_PopStack(thread);
	uint32_t focus = Thread_PopStack(thread);
	uint32_t listAddress = Thread_PopStack(thread);
	Thread_PopStack(thread);
	int32_t count = (int32_t)Thread_PopStack(thread);
	uint32_t windowHandle = Thread_PopStack(thread);
	const char* fatal = NULL;
	DisplayObject_t* window = Object_ResolveKind(windowHandle, OBJECT_TYPE_WINDOW);
	uint32_t words = ex ? 16 : 4;
	uint32_t room = 0, keyRoom = 0;
	const uint32_t* list = NULL;
	const uint32_t* keys = NULL;
	if(window == NULL)
		fatal = "invalid window";
	else if(focus > 3)
		fatal = "invalid focus mode";
	else if(count < 1 || count > SELECT_MAX)
		fatal = "invalid icon count";
	else if((list = (const uint32_t*)Thread_ResolveSpan(thread, listAddress, &room)) == NULL
	        || (uint64_t)count * words * 4 > room)
		fatal = "the icon list is not addressable";
	else if(keysAddress != 0 && ((keys = (const uint32_t*)Thread_ResolveSpan(thread, keysAddress, &keyRoom)) == NULL
	                              || (uint64_t)count * 4 > keyRoom))
		fatal = "the hotkey list is not addressable";
	Renderer_t* renderer = thread->engine->renderer;
	for(int32_t i = 0; fatal == NULL && i < count; i++)
	{
		const uint32_t* w = list + (size_t)i * words;
		if(Renderer_ResolveBitmap(renderer, (int32_t)w[2]) == NULL || (!ex && Renderer_ResolveBitmap(renderer, (int32_t)w[3]) == NULL))
			fatal = "the specified bitmaps include an invalid one";
	}
	if(fatal != NULL)
	{
		printf("[Thread %d]: %sError: Grp0 0x%02X: %s\n", thread->threadId, TLevel[thread->level], ex ? 0xB1 : 0xB0, fatal);
		return 0xFFFFFFFC;
	}

	Select_t* sel = (Select_t*)calloc(1, sizeof(Select_t));
	if(sel == NULL)
		return 0xFFFFFFFF;
	sel->thread = thread;
	sel->windowHandle = windowHandle;
	sel->ex = ex;
	sel->pixel = pixel;
	sel->focus = focus;
	sel->count = count;
	sel->lit = -1;
	// One drawable per choice in the window's own content, and an object the pointer hits.
	Window_ReserveContent(renderer, window, (uint32_t)count);
	for(int32_t i = 0; i < count; i++)
	{
		const uint32_t* w = list + (size_t)i * words;
		SelectChoice_t* c = &sel->choices[i];
		c->x = (int32_t)w[0];
		c->y = (int32_t)w[1];
		c->image = (int32_t)w[2];
		c->lit = ex ? -1 : (int32_t)w[3];
		c->mask = ex ? (int32_t)w[15] : c->image;
		c->key = keys != NULL ? keys[i] : 0;
		Bitmap_t* image = Renderer_ResolveBitmap(renderer, c->image);
		Window_SetContentSlot(renderer, window, (uint32_t)i, c->image, c->x, c->y, 0, 0, (uint32_t)i);
		Window_RedrawContentSlot(renderer, window, (uint32_t)i);
		c->drawn = 1;
		c->object = Object_CreateVirtual(window);
		if(c->object != NULL)
		{
			Object_SetExtent(c->object, image->width, image->height);
			Object_SetVisible(c->object, Object_IsDrawable(window));
			Object_SetPosition(c->object, window->x, window->y);
			Object_SetOrigin(c->object, window->originX, window->originY);
			c->object->priority = window->priority;
			Object_Attach(window, c->object, c->x, c->y);
			Bitmap_t* mask = c->mask >= 0 ? Renderer_ResolveBitmap(renderer, c->mask) : NULL;
			if(pixel && mask != NULL)
				Object_SetHitMask(c->object, mask);
			Region_Add(0, Object_DrawKey(c->object), 0, 0, 0, 0, c->object);
		}
	}
	// The window takes the pointer; mode 3 also the keyboard (cursor-key movement over the
	// choices is not ported yet).
	Region_Add(0, Object_DrawKey(window), 0, 0, 0, 0, window);
	Input_RegionStateOf(0, Object_DrawKey(window));
	int32_t x, y;
	sel->lit = Select_Hit(sel, &x, &y);
	Select_Draw(sel, sel->lit, 1);

	Process_t* process = Process_CreateCallback(thread, Select_Run, Select_Free, sel);
	if(process == NULL)
	{
		Select_Free(sel);
		return 0xFFFFFFFF;
	}
	sel->process = process;
	Thread_SetProcess(thread, process);
	return 2;
}
