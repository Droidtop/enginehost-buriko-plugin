#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icon.h"
#include "engine.h"
#include "input.h"
#include "object.h"
#include "os.h"
#include "region.h"
#include "thread.h"
#include "window.h"

extern Engine_t* gEngine;

// 0x0046CB00, which is called on every path out of 0x0046CB50 and again by the
// opcode once the icon has taken what it wants: the parts of every entry, then the
// entry array, then the root. It steps the entry array itself rather than the count
// it was given, so a half-built copy frees cleanly.
void Icon_FreeContent(IconContent_t* content)
{
	if(content == NULL)
		return;
	if(content->entries != NULL)
	{
		for(uint32_t i = 0; i < content->entryCount; i++)
			free(content->entries[i].parts);
		free(content->entries);
	}
	free(content);
}

// 0x0046CB50. `root` is already resolved (the opcode's own 0x0048E0E0 did that); the
// two addresses inside the tree are resolved here, as the original does with
// 0x0048DF60 and the thread it was handed.
uint32_t Icon_ReadContent(Thread_t* thread, const uint32_t* root, IconContent_t** out)
{
	*out = NULL;
	if(root == NULL)
		return 2;

	IconContent_t* content = (IconContent_t*)malloc(sizeof(IconContent_t));
	if(content == NULL)
		return 2;
	memcpy(content->raw, root, sizeof(content->raw));
	content->entryCount = 0;
	content->entries = NULL;

	uint32_t count = root[0];
	const uint32_t* entries = (const uint32_t*)Thread_ResolveAddr(thread, root[1]);
	if(entries == NULL || count == 0 || count > ICON_CONTENT_MAX_COUNT)
	{
		Icon_FreeContent(content);
		return 2;
	}

	content->entries = (IconContentEntry_t*)calloc(count, sizeof(IconContentEntry_t));
	if(content->entries == NULL)
	{
		Icon_FreeContent(content);
		return 2;
	}
	content->entryCount = count;

	uint32_t status = 0;
	for(uint32_t i = 0; i < count; i++)
	{
		// Once an entry has failed the original stops resolving and leaves the
		// remaining entries' part pointers null, which calloc has already done.
		if(status != 0)
			continue;

		const uint32_t* source = entries + i * ICON_CONTENT_ENTRY_WORDS;
		IconContentEntry_t* entry = &content->entries[i];
		memcpy(entry->raw, source, sizeof(entry->raw));

		uint32_t parts = source[0] & 0xFFFF;
		const uint32_t* partSource = (const uint32_t*)Thread_ResolveAddr(thread, source[2]);
		if(partSource == NULL || parts == 0 || parts > ICON_CONTENT_MAX_COUNT)
		{
			status = 3;
			continue;
		}

		entry->parts = (uint32_t*)malloc((size_t)parts * ICON_CONTENT_PART_WORDS * sizeof(uint32_t));
		if(entry->parts == NULL)
		{
			status = 3;
			continue;
		}
		entry->partCount = parts;
		memcpy(entry->parts, partSource, (size_t)parts * ICON_CONTENT_PART_WORDS * sizeof(uint32_t));
	}

	if(status != 0)
	{
		Icon_FreeContent(content);
		return status;
	}

	*out = content;
	return 0;
}

// ----------------------------------------------------------------------------------
// The registry (0x005667E8, the count at 0x005667E0, the serial at 0x00565D74) and the
// mode the main loop runs the icons in (0x005667DC, Sys0 0xAF).
// ----------------------------------------------------------------------------------
static Icon_t*  gIcons = NULL;
static uint32_t gIconCount = 0;
static uint32_t gIconSerial = 0;
static uint32_t gIconMode = 0;

// 0x0050EA58: the four key tables of maps 4 to 7, which 0x00447C70 clears the first
// time an icon is made (0x00507224 starts at 1) and Grp1 0xBF fills.
static uint32_t gKeyMaps[4][24];
static int      gKeyMapsCleared = 0;

// 0x00565D84, which Grp0 0xAF writes (0x00447CC0): 0 lets the pointer reach icons
// always; otherwise only while the game window is active (0x0049A3C0).
static uint32_t gIconPointerGate = 0;
extern int gAppActive;

static Renderer_t* Icon_Renderer(void)
{
	return gEngine != NULL ? gEngine->renderer : NULL;
}

// 0x004479B0 and 0x004479C0: the icon marks itself changed, and at the end of its
// pass asks for a frame when its window would be drawn (0x004479E0 -> 0x00461F00).
static void Icon_MarkDirty(Icon_t* icon)
{
	icon->dirty = 1;
}

static void Icon_FlushDirty(Icon_t* icon)
{
	if(icon->dirty && Object_IsDrawable(icon->window))
		gObjectDamage++;
	icon->dirty = 0;
}

// 0x00443320: a rectangle of the window damaged on the screen. The screen here is
// recomposed whole, so any damage is a frame.
static void Icon_Damage(Icon_t* icon)
{
	if(Object_IsDrawable(icon->window))
		gObjectDamage++;
}

// ----------------------------------------------------------------------------------
// Events (0x0044A2B0, 0x0044A300, 0x00448640)
// ----------------------------------------------------------------------------------
static void Icon_PostEvent(Icon_t* icon, uint32_t word0, uint32_t word1, uint32_t word2)
{
	IconEvent_t* event = (IconEvent_t*)malloc(sizeof(IconEvent_t));
	if(event == NULL)
		return;
	event->words[0] = word0;
	event->words[1] = word1;
	event->words[2] = word2;
	event->next = NULL;
	IconEvent_t** link = &icon->events;
	while(*link != NULL)
		link = &(*link)->next;
	*link = event;
}

static int Icon_DropEvent(Icon_t* icon)
{
	IconEvent_t* event = icon->events;
	if(event == NULL)
		return 0;
	icon->events = event->next;
	free(event);
	return 1;
}

int Icon_TakeEvent(Icon_t* icon, uint32_t* out)
{
	if(icon == NULL || icon->events == NULL)
	{
		out[0] = out[1] = out[2] = 0;
		return 0;
	}
	out[0] = icon->events->words[0];
	out[1] = icon->events->words[1];
	out[2] = icon->events->words[2];
	Icon_DropEvent(icon);
	return 1;
}

// ----------------------------------------------------------------------------------
// Messages (0x004478C0, 0x00447A10, 0x00447940)
// ----------------------------------------------------------------------------------
int Icon_PostMessage(Icon_t* icon, const uint32_t* words, uint32_t count)
{
	// 0x004478C0: 1..0x100 words, copied, onto the end of the queue.
	if(icon == NULL || words == NULL || count - 1 > 0xFF)
		return 0;
	IconMessage_t* message = (IconMessage_t*)malloc(sizeof(IconMessage_t));
	if(message == NULL)
		return 0;
	message->words = (uint32_t*)malloc(count * sizeof(uint32_t));
	if(message->words == NULL)
	{
		free(message);
		return 0;
	}
	memcpy(message->words, words, count * sizeof(uint32_t));
	message->count = count;
	message->next = NULL;
	IconMessage_t** link = &icon->messages;
	while(*link != NULL)
		link = &(*link)->next;
	*link = message;
	return 1;
}

static uint32_t Icon_TakeMessage(Icon_t* icon, uint32_t* out)
{
	// 0x00447A10: the oldest message's words and its count, or 0.
	IconMessage_t* message = icon->messages;
	if(message == NULL)
		return 0;
	uint32_t count = message->count;
	memcpy(out, message->words, count * sizeof(uint32_t));
	icon->messages = message->next;
	free(message->words);
	free(message);
	return count;
}

static uint32_t Icon_HandleMessage(Renderer_t* renderer, Icon_t* icon, uint32_t count, const uint32_t* words);

static void Icon_DrainMessages(Renderer_t* renderer, Icon_t* icon)
{
	// 0x00447940: every message; one whose first word is not 0 goes to vtable+0x08,
	// and {0, value} sets +0x10.
	uint32_t words[0x100];
	uint32_t count;
	while((count = Icon_TakeMessage(icon, words)) > 0)
	{
		if(words[0] != 0)
			Icon_HandleMessage(renderer, icon, count, words);
		else if(count == 2)
			icon->enabled = words[1];
	}
}

// ----------------------------------------------------------------------------------
// Lookups
// ----------------------------------------------------------------------------------
static int Icon_ValidEntry(const Icon_t* icon, int32_t entry)
{
	return entry >= 0 && entry < icon->entryCount;
}

static int Icon_ValidPart(const Icon_t* icon, int32_t entry, int32_t part)
{
	return Icon_ValidEntry(icon, entry) && part >= 0 && part < icon->entries[entry].partCount;
}

// The Ex's own copy of a part's words.
static uint32_t* Icon_PartWords(const Icon_t* icon, int32_t entry, int32_t part)
{
	return icon->copies[entry].parts + (size_t)part * ICON_CONTENT_PART_WORDS;
}

static uint32_t Icon_WindowKey(const Icon_t* icon)
{
	return Object_DrawKey(icon->window);
}

static int Icon_HoverIs(const Icon_t* icon, int32_t entry, int32_t part)
{
	if(icon->hover == -1)
		return 0;
	return icon->hits[icon->hover].entry == entry && icon->hits[icon->hover].part == part;
}

// 0x0044A780: the virtual object of the part at (entry, part) that has an image.
static DisplayObject_t* Icon_PartObject(const Icon_t* icon, int32_t entry, int32_t part)
{
	for(int32_t n = 0; n < icon->hitCount; n++)
	{
		const IconHit_t* hit = &icon->hits[n];
		if(hit->hasImage && hit->entry == entry && hit->part == part)
			return hit->object;
	}
	return NULL;
}

// 0x0044A7D0: where the pointer is on a part, in the part's own coordinates. The
// point is the pointer itself when the entry takes a held button as a click and the
// left button is held (0x0046E610), and otherwise where the left button was last
// pressed (0x0048E720 with 0). 1 when that point is on the part's object.
static int Icon_PartPoint(Icon_t* icon, int32_t entry, int32_t part, int32_t* x, int32_t* y)
{
	DisplayObject_t* object = Icon_PartObject(icon, entry, part);
	if(object == NULL)
		return 0;
	int32_t px, py;
	if(icon->entries[entry].words[2] != 0 && (Input_MouseHeld(Object_DrawKey(icon->object)) & 1))
		Input_GetMouse(&px, &py);
	else
		Input_ClickPosition(0, &px, &py);
	Rect_t bounds;
	Object_GetScreenBounds(object, &bounds);
	if(px < bounds.left || px > bounds.right || py < bounds.top || py > bounds.bottom)
		return 0;
	*x = px - bounds.left;
	*y = py - bounds.top;
	return 1;
}

// 0x00447CD0.
static int Icon_PointerAllowed(void)
{
	return gIconPointerGate == 0 ? 1 : gAppActive;
}

// ----------------------------------------------------------------------------------
// The pointer (0x004496A0, 0x00449840)
// ----------------------------------------------------------------------------------
static int Icon_Decidable(Icon_t* icon, int32_t entry, int32_t part);

// 0x004496A0: the hit under the pointer, walking the parts from the highest layer
// down (vtable+0x44). With `check` it must also be decidable (vtable+0x28), reachable
// (0x0046D9B0 with its object's key) and hit at that pixel (vtable+0x64); without
// `force` a pointer the system has hidden (0x0048EFB0) finds nothing.
static int32_t Icon_HitUnderPointer(Icon_t* icon, int32_t* outX, int32_t* outY, int check, int force)
{
	if(!icon->active)
		return -1;
	if(!Input_AboveModal(Icon_WindowKey(icon)))
		return -1;
	if(!Icon_PointerAllowed())
		return -1;
	// 0x0048EFB0 asks GetCursorInfo whether the cursor is suppressed (a touch or pen
	// has taken over it). This host's pointer is always shown: SDL turns touches
	// into pointer motion.
	(void)force;
	int32_t mx, my;
	Input_GetMouse(&mx, &my);
	for(int32_t n = 0; n < icon->orderCount; n++)
	{
		int32_t index = icon->order[n];
		IconHit_t* hit = &icon->hits[index];
		if(!hit->hasImage)
			continue;
		Rect_t bounds;
		Object_GetScreenBounds(hit->object, &bounds);
		if(mx < bounds.left || mx > bounds.right || my < bounds.top || my > bounds.bottom)
			continue;
		if(check)
		{
			if(!Icon_Decidable(icon, hit->entry, hit->part))
				continue;
			if(!Input_HasMouse(Object_DrawKey(hit->object)))
				continue;
			if(!Object_HitTest(hit->object, mx - bounds.left, my - bounds.top, 1))
				continue;
		}
		if(outX != NULL)
		{
			*outX = mx - bounds.left;
			*outY = my - bounds.top;
		}
		return index;
	}
	return -1;
}

// 0x00449840: the same with the check, keeping the point in +0x60/+0x64.
static int32_t Icon_HoverTarget(Icon_t* icon)
{
	icon->hitX = icon->hitY = -1;
	int32_t x, y;
	int32_t index = Icon_HitUnderPointer(icon, &x, &y, 1, 0);
	if(index == -1)
		return -1;
	icon->hitX = x;
	icon->hitY = y;
	return index;
}

// ----------------------------------------------------------------------------------
// The Ex virtuals
// ----------------------------------------------------------------------------------
// vtable+0x20 (0x0044BB20): the bitmap a part shows. The four images are the part's
// own (+0x20), under the pointer (+0x24), selected (+0x28) and both (+0x2C); an
// animation adds its frame, and flag 0x01 (animate only under the pointer) and 0x08
// (animate under the pointer or while selected) decide when the frame counts.
static int Icon_PartBitmap(Icon_t* icon, int32_t* out, int32_t entry, int32_t part, int hovered, int useSelection)
{
	if(entry >= (int32_t)icon->root[0])
		return 0;
	IconContentEntry_t* copy = &icon->copies[entry];
	if(part >= (int32_t)copy->raw[0])
		return 0;
	IconPartState_t* state = &icon->states[entry][part];
	if(!state->used)
		return 0;
	const uint32_t* p = state->part;
	*out = (int32_t)p[ICON_PART_BITMAP];
	int always = (p[ICON_PART_FLAGS] & 1) != 0;
	int eitherway = ((p[ICON_PART_FLAGS] >> 3) & 1) != 0;
	int selected = useSelection && part == (int32_t)copy->raw[3];

	int animate;
	if(!hovered)
		animate = selected ? !always : (!always && !eitherway);
	else
		animate = 1;

	if(hovered)
	{
		if((int32_t)p[ICON_PART_HOVER] != -1)
			*out = (int32_t)p[ICON_PART_HOVER];
		else if(selected)
			animate = !always;
		else
			animate = !always && !eitherway;
	}
	if(selected)
	{
		if((int32_t)p[ICON_PART_SELECTED] != -1)
			*out = (int32_t)p[ICON_PART_SELECTED];
		else
			always = 0;
		if(hovered)
		{
			if((int32_t)p[ICON_PART_SELHOVER] != -1)
				*out = (int32_t)p[ICON_PART_SELHOVER];
			else
			{
				// 0x0044BC37: with no image of its own the frame counts only when
				// the part does not animate "only under the pointer".
				if(animate && !always)
					*out += (int32_t)state->frame;
				return 1;
			}
		}
	}
	if(animate)
		*out += (int32_t)state->frame;
	return 1;
}

// Set a part's sprite to the image it should show now, and redraw it.
static void Icon_RefreshPart(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part, int hovered, int useSelection)
{
	int32_t bitmap = 0;
	Icon_PartBitmap(icon, &bitmap, entry, part, hovered, useSelection);
	uint32_t slot = icon->states[entry][part].slot;
	Window_SlotSetBitmap(renderer, icon->window, slot, bitmap);
	Window_RedrawContentSlot(renderer, icon->window, slot);
	Icon_Damage(icon);
}

// vtable+0x28 (0x0044C1F0): whether a part can be decided. A part with +0x34 set
// cannot while it is the entry's selected part.
static int Icon_Decidable(Icon_t* icon, int32_t entry, int32_t part)
{
	if(!Icon_ValidPart(icon, entry, part))
		return 0;
	if(Icon_PartWords(icon, entry, part)[ICON_PART_NODECIDE] != 0 && part == icon->entries[entry].selectedPart)
		return 0;
	return 1;
}

// vtable+0x48 (0x0044C7D0): whether a press decides at once (1) or waits for the
// release over the same part (0): entry flag 0x02 or part flag 0x20 make it wait.
static int Icon_DecidesOnPress(Icon_t* icon, int32_t index)
{
	if(index == -1)
		return 1;
	IconHit_t* hit = &icon->hits[index];
	if(icon->copies[hit->entry].raw[15] & 2)
		return 0;
	if(Icon_PartWords(icon, hit->entry, hit->part)[ICON_PART_FLAGS] & 0x20)
		return 0;
	return 1;
}

// vtable+0x10 (0x0044BA50): the pointer's new part, posted as 0x10000002 unless the
// part has flag 0x04.
static int Icon_NotifyHover(Icon_t* icon, uint32_t value, uint32_t flag)
{
	int32_t entry = (int32_t)value >> 16;
	int32_t part = (int16_t)(value & 0xFFFF);
	if(entry >= 0 && entry < (int32_t)icon->root[0] && part >= 0 && part < (int32_t)icon->copies[entry].raw[0])
	{
		if(Icon_PartWords(icon, entry, part)[ICON_PART_FLAGS] & 4)
			return 0;
	}
	Icon_PostEvent(icon, 0x10000002u, value, flag);
	return 1;
}

// vtable+0x18 (0x0044BAC0): a click on nothing, posted as 0x10000007 with where the
// left button was pressed, in the window's coordinates unless the icon has the
// keyboard.
static void Icon_ClickOnNothing(Icon_t* icon)
{
	int32_t x = 0, y = 0;
	Input_ClickPosition(0, &x, &y);
	if(!icon->keyboard)
	{
		int32_t wx, wy;
		Object_GetPosition(icon->window, &wx, &wy);
		x -= wx;
		y -= wy;
	}
	Icon_PostEvent(icon, 0x10000007u, 0xFFFFFFFFu, ((uint32_t)y << 16) | ((uint32_t)x & 0xFFFF));
}

// The decision's point, posted as 0x10000007 before the decision itself.
static void Icon_PostPoint(Icon_t* icon, int32_t entry, int32_t part)
{
	int32_t x = 0, y = 0;
	Icon_PartPoint(icon, entry, part, &x, &y);
	Icon_PostEvent(icon, 0x10000007u, ((uint32_t)entry << 16) | (uint32_t)part,
	               ((uint32_t)y << 16) | ((uint32_t)x & 0xFFFF));
}

// vtable+0x2C (0x0044C310, over the base's 0x0044A0E0): (entry, part) decided with a
// value. -1 for either is "none". Posts 0x10000007 (with a value) and 0x10000006.
static int Icon_DecidePart(Icon_t* icon, int32_t entry, int32_t part, uint32_t value)
{
	if(entry != -1 && !Icon_ValidEntry(icon, entry))
		return 0;
	if(part != -1 && (part < 0 || part >= icon->entries[entry == -1 ? 0 : entry].partCount))
		return 0;
	icon->lastValue = value;
	icon->lastEntry = entry;
	icon->lastPart = part;
	if(entry != -1 && part != -1)
	{
		if(value != 0)
			Icon_PostPoint(icon, entry, part);
		Icon_PostEvent(icon, 0x10000006u, ((uint32_t)entry << 16) | (uint32_t)part, value);
	}
	else
		Icon_PostEvent(icon, 0x10000006u, 0xFFFFFFFFu, value);
	return 1;
}

// vtable+0x30 (0x0044C250, over the base's 0x0044A080): the same for a hit index.
static int Icon_DecideHit(Icon_t* icon, int32_t index, uint32_t value)
{
	if(index != -1 && (index < 0 || index >= icon->hitCount))
		return 0;
	if(index == -1)
	{
		icon->lastEntry = icon->lastPart = -1;
		icon->lastValue = value;
		Icon_PostEvent(icon, 0x10000006u, 0xFFFFFFFFu, value);
		return 1;
	}
	IconHit_t* hit = &icon->hits[index];
	icon->lastEntry = hit->entry;
	icon->lastPart = hit->part;
	icon->lastValue = value;
	if(value != 0)
		Icon_PostPoint(icon, hit->entry, hit->part);
	Icon_PostEvent(icon, 0x10000006u, ((uint32_t)hit->entry << 16) | (uint32_t)hit->part, value);
	return 1;
}

// 0x00449E40: an entry of a group (+0x18) that has a selected part clears the
// selection of every other entry of the group (vtable+0x24 with -1).
static int Icon_SelectPart(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part);

static int Icon_ClearGroup(Renderer_t* renderer, Icon_t* icon, int32_t entry)
{
	if(!Icon_ValidEntry(icon, entry))
		return 0;
	if(icon->entries[entry].selectedPart == -1)
		return 1;
	int32_t group = icon->entries[entry].words[3];
	if(group == -1)
		return 1;
	for(int32_t other = 0; other < icon->entryCount; other++)
	{
		if(other != entry && icon->entries[other].words[3] == group)
			Icon_SelectPart(renderer, icon, other, -1);
	}
	return 1;
}

// vtable+0x24 (0x0044BFA0): an entry's selected part (-1 for none). Refuses a part
// that is not used, and answers 0 when nothing changes.
static int Icon_SelectPart(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part)
{
	if(!Icon_ValidEntry(icon, entry))
		return 0;
	IconEntry_t* record = &icon->entries[entry];
	int32_t old = record->selectedPart;
	if(part >= 0 && part < record->partCount)
	{
		if(Icon_PartWords(icon, entry, part)[ICON_PART_W0] == 0)
			return 0;
	}
	else if(part != -1)
		return 0;
	if(part == old)
		return 0;

	if(old != -1)
	{
		IconPartState_t* state = &icon->states[entry][old];
		if(state->part[ICON_PART_FLAGS] & 8)
			state->frame = state->frameAt = 0;
		int32_t bitmap;
		if(Icon_PartBitmap(icon, &bitmap, entry, old, Icon_HoverIs(icon, entry, old), 0))
		{
			Window_SlotSetBitmap(renderer, icon->window, state->slot, bitmap);
			Window_RedrawContentSlot(renderer, icon->window, state->slot);
			Icon_Damage(icon);
			Icon_MarkDirty(icon);
		}
	}
	record->selectedPart = part;
	icon->copies[entry].raw[3] = (uint32_t)part;
	if(part != -1)
	{
		Icon_RefreshPart(renderer, icon, entry, part, Icon_HoverIs(icon, entry, part), 1);
		Icon_ClearGroup(renderer, icon, entry);
		Icon_MarkDirty(icon);
	}
	return 1;
}

// Two cursors in window parts `first` and `first + 1`, from {x, y, bitmap} triples in
// the window's client area; a cursor whose bitmap does not exist turns its part off.
static void Icon_PlaceCursors(Renderer_t* renderer, Icon_t* icon, int first, const int32_t* triples)
{
	Rect_t client;
	Window_ClientRect(icon->window, &client);
	for(int k = 0; k < 2; k++)
	{
		const int32_t* cursor = triples + k * 3;
		Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, cursor[2]);
		if(bitmap != NULL)
		{
			Window_SetPartBitmap(renderer, icon->window, first + k, bitmap);
			Window_SetPartPosition(renderer, icon->window, first + k, cursor[0] + client.left, cursor[1] + client.top, 0);
			Window_EnablePart(renderer, icon->window, first + k, 1);
			Rect_t area;
			Window_PartScreenRect(renderer, icon->window, first + k, &area);
			Icon_Damage(icon);
		}
		else
			Window_EnablePart(renderer, icon->window, first + k, 0);
		Icon_MarkDirty(icon);
	}
}

// 0x00449B40: the current entry. An entry with +0x0C set has cursors (window parts
// 0 and 1); the group of the entry is then cleared (0x00449E40).
static int Icon_SelectEntry(Renderer_t* renderer, Icon_t* icon, int32_t entry)
{
	if(!Icon_ValidEntry(icon, entry))
		return 0;
	IconEntry_t* record = &icon->entries[entry];
	if(record->words[0] == 0)
		return 0;
	for(int k = 0; k < 2; k++)
	{
		Rect_t area;
		if(Window_PartScreenRect(renderer, icon->window, k, &area))
		{
			Icon_Damage(icon);
			Icon_MarkDirty(icon);
		}
	}
	Icon_PlaceCursors(renderer, icon, 0, &record->words[4]);
	icon->currentEntry = entry;
	Icon_ClearGroup(renderer, icon, entry);
	return 1;
}

// vtable+0x1C (0x0044BC50): the part under the pointer (a hit index or -1). The old
// one goes back to its image (and its layer, for an entry with flag 0x01), the new one
// shows its hover image, one layer band higher, with the part's cursors in window
// parts 2 and 3. 0 when nothing changes and `force` is clear.
static int Icon_SetHover(Renderer_t* renderer, Icon_t* icon, int32_t index, int force)
{
	if(icon->hover == index && !force)
		return 0;
	for(int k = 2; k < 4; k++)
	{
		Rect_t area;
		if(Window_PartScreenRect(renderer, icon->window, k, &area))
		{
			Icon_Damage(icon);
			Icon_MarkDirty(icon);
		}
	}
	if(icon->hover != -1)
	{
		IconHit_t* hit = &icon->hits[icon->hover];
		int32_t entry = hit->entry, part = hit->part;
		IconPartState_t* state = &icon->states[entry][part];
		uint32_t flags = state->part[ICON_PART_FLAGS];
		if((flags & 1) || ((flags & 8) && part != (int32_t)icon->copies[entry].raw[3]))
			state->frame = state->frameAt = 0;
		if(icon->copies[entry].raw[15] & 1)
			Window_SlotSetLayer(icon->window, state->slot, state->layer);
		Icon_RefreshPart(renderer, icon, entry, part, 0, 1);
		Icon_MarkDirty(icon);
	}
	if(index != -1)
	{
		IconHit_t* hit = &icon->hits[index];
		int32_t entry = hit->entry, part = hit->part;
		IconPartState_t* state = &icon->states[entry][part];
		if(icon->copies[entry].raw[15] & 1)
			Window_SlotSetLayer(icon->window, state->slot, state->layer + 0x100);
		Icon_RefreshPart(renderer, icon, entry, part, 1, 1);
		Icon_MarkDirty(icon);
		Icon_PlaceCursors(renderer, icon, 2, hit->record->cursor);
	}
	else
	{
		Window_EnablePart(renderer, icon->window, 2, 0);
		Window_EnablePart(renderer, icon->window, 3, 0);
		Icon_MarkDirty(icon);
	}
	icon->hover = index;
	return 1;
}

// The notification after a hover change: vtable+0x10 with the part and whether it has
// a hover image.
static void Icon_AfterHover(Icon_t* icon)
{
	uint32_t value = 0xFFFFFFFFu, flag = 0;
	if(icon->hover != -1)
	{
		IconHit_t* hit = &icon->hits[icon->hover];
		value = ((uint32_t)hit->entry << 16) | (uint32_t)hit->part;
		flag = hit->record->hover != -1;
	}
	Icon_NotifyHover(icon, value, flag);
}

// vtable+0x38 (0x0044C440): one of the part's four images (0..3) replaced; 4 is the
// base's (0x0044A4C0): the hit mask bitmap (-2 none, -1 the whole rectangle).
static int Icon_SetPartImage(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part, uint32_t which, int32_t value)
{
	if(!Icon_ValidEntry(icon, entry) || part < 0 || part >= (int32_t)icon->copies[entry].raw[0])
		return 0;
	if(which < 4)
	{
		Icon_PartWords(icon, entry, part)[ICON_PART_BITMAP + which] = (uint32_t)value;
		Icon_RefreshPart(renderer, icon, entry, part, Icon_HoverIs(icon, entry, part), 1);
		Icon_MarkDirty(icon);
		return 1;
	}
	if(which == 4)
	{
		icon->entries[entry].parts[part].hitMask = value;
		DisplayObject_t* object = Icon_PartObject(icon, entry, part);
		if(object == NULL)
			return 0;
		if(value == -2)
			Object_DisableHit(object);
		else if(value == -1)
			Object_SetHitMask(object, NULL);
		else
		{
			Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, value);
			if(bitmap == NULL)
				return 0;
			Object_SetHitMask(object, bitmap);
		}
		return 1;
	}
	return 0;
}

// vtable+0x3C (0x0044C580): a part moved to (x, y) at depth z: its words, its record,
// its virtual object (at the window's position plus the point) and its sprite (at the
// point plus its anchor).
static int Icon_MovePart(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part, int32_t x, int32_t y, int32_t z)
{
	if(!Icon_ValidEntry(icon, entry) || part < 0 || part >= (int32_t)icon->copies[entry].raw[0])
		return 0;
	IconPartState_t* state = &icon->states[entry][part];
	state->part[ICON_PART_X] = (uint32_t)x;
	state->part[ICON_PART_Y] = (uint32_t)y;
	state->depth = z;
	icon->entries[entry].parts[part].x = x;
	icon->entries[entry].parts[part].y = y;
	for(int32_t n = 0; n < icon->orderCount; n++)
	{
		IconHit_t* hit = &icon->hits[icon->order[n]];
		if(hit->entry == entry && hit->part == part)
		{
			int32_t wx, wy;
			Object_GetPosition(icon->window, &wx, &wy);
			Object_Move(hit->object, wx + x, wy + y);
			break;
		}
	}
	Window_SlotRedrawWithout(renderer, icon->window, state->slot);
	Icon_Damage(icon);
	Window_SlotSetPosition(renderer, icon->window, state->slot,
	                       (int32_t)state->part[ICON_PART_ANCHOR_X] + x,
	                       (int32_t)state->part[ICON_PART_ANCHOR_Y] + y, z);
	Window_RedrawContentSlot(renderer, icon->window, state->slot);
	Icon_Damage(icon);
	Icon_MarkDirty(icon);
	return 1;
}

// vtable+0x40 (0x0044C740): a part's animation (frames, interval), restarted.
static int Icon_SetPartAnimation(Icon_t* icon, int32_t entry, int32_t part, uint32_t frames, uint32_t interval)
{
	if(!Icon_ValidPart(icon, entry, part))
		return 0;
	IconPartState_t* state = &icon->states[entry][part];
	state->part[ICON_PART_FRAMES] = frames;
	state->part[ICON_PART_INTERVAL] = interval;
	state->frame = state->frameAt = 0;
	return 1;
}

// vtable+0x0C (0x0044B620): the animations' step - frames on their interval, and
// turns through their keys in 4 ms steps.
static void Icon_Animate(Renderer_t* renderer, Icon_t* icon)
{
	int32_t hoverEntry = -1, hoverPart = -1;
	if(icon->hover != -1)
	{
		hoverEntry = icon->hits[icon->hover].entry;
		hoverPart = icon->hits[icon->hover].part;
	}
	for(int32_t entry = 0; entry < (int32_t)icon->root[0]; entry++)
	{
		IconContentEntry_t* copy = &icon->copies[entry];
		for(int32_t part = 0; part < (int32_t)copy->raw[0]; part++)
		{
			IconPartState_t* state = &icon->states[entry][part];
			if(!state->used)
				continue;
			uint32_t* p = state->part;
			if((int32_t)p[ICON_PART_FRAMES] > 1 && (int32_t)p[ICON_PART_INTERVAL] > 0)
			{
				int hovered = entry == hoverEntry && part == hoverPart;
				uint32_t flags = p[ICON_PART_FLAGS];
				int allowed;
				if(flags & 1)
					allowed = hovered;
				else if(flags & 8)
					allowed = hovered || part == (int32_t)copy->raw[3];
				else
					allowed = 1;
				if(allowed)
				{
					if(state->frameAt != 0)
					{
						uint32_t at = state->frameAt;
						if(at <= OS_GetTicks())
						{
							state->frame++;
							if(state->frame == p[ICON_PART_FRAMES])
								state->frame = 0;
							state->frameAt = at + p[ICON_PART_INTERVAL];
							Icon_RefreshPart(renderer, icon, entry, part, hovered, 1);
							Icon_MarkDirty(icon);
						}
					}
					else
					{
						state->frame = 0;
						state->frameAt = OS_GetTicks() + p[ICON_PART_INTERVAL];
					}
				}
			}
			if((int32_t)p[ICON_PART_TURNS] <= 0)
				continue;
			int turning = 1;
			if(p[ICON_PART_TURN_HOVER] != 0)
			{
				turning = icon->hover != -1 && Icon_Decidable(icon, entry, part) && Icon_HoverIs(icon, entry, part);
			}
			if(!turning)
			{
				// 0x0044B823: the pointer has gone - back to angle 0 unless the part
				// keeps its angle.
				if(state->turnAt == 0)
					continue;
				if(p[ICON_PART_TURN_KEEP] == 0)
				{
					Window_SlotRedrawWithout(renderer, icon->window, state->slot);
					Icon_Damage(icon);
					Window_SlotSetAngle(renderer, icon->window, state->slot, 0);
					Window_RedrawContentSlot(renderer, icon->window, state->slot);
					Icon_Damage(icon);
					Icon_MarkDirty(icon);
					state->turnAt = 0;
				}
				else
					state->turnAt = OS_GetTicks() + 4;
				continue;
			}
			if(state->turnAt == 0)
			{
				state->angle = (int32_t)p[ICON_PART_ANGLE];
				state->key = 0;
				state->step = 0;
				state->turnAt = OS_GetTicks() + 4;
				continue;
			}
			if(state->turnAt > OS_GetTicks())
				continue;
			const uint32_t* key = &p[ICON_PART_KEYS + 3 * state->key];
			if(state->step == 0)
			{
				int32_t steps = (int32_t)key[1] / 4;
				state->steps = steps > 0 ? steps : 1;
			}
			state->step++;
			Window_SlotRedrawWithout(renderer, icon->window, state->slot);
			Icon_Damage(icon);
			int32_t angle = (int32_t)(((int64_t)(int32_t)key[0] * state->step) / state->steps) + state->angle;
			Window_SlotSetAngle(renderer, icon->window, state->slot, angle);
			Window_RedrawContentSlot(renderer, icon->window, state->slot);
			Icon_Damage(icon);
			Icon_MarkDirty(icon);
			if(state->step == state->steps)
			{
				state->angle = (state->angle + (int32_t)key[0]) % (360 << 16);
				state->key++;
				if(state->key == p[ICON_PART_TURNS])
					state->key = 0;
				state->step = 0;
			}
			state->turnAt += 4;
		}
	}
}

// ----------------------------------------------------------------------------------
// The messages a script sends (vtable+0x08, 0x0044A330)
// ----------------------------------------------------------------------------------
static uint32_t Icon_HandleMessage(Renderer_t* renderer, Icon_t* icon, uint32_t count, const uint32_t* words)
{
	switch(words[0])
	{
		case 0xF0000000u:
			// Decide (entry, part) = the two halves of word 1, with word 2.
			if(count == 3)
			{
				int32_t entry = (int16_t)(words[1] >> 16);
				int32_t part = (int16_t)(words[1] & 0xFFFF);
				if(Icon_DecidePart(icon, entry, part, words[2]))
				{
					// vtable+0x14 is 0 for an Ex icon: deciding does not end it.
				}
				return 1;
			}
			return 0;
		case 0xF0000001u:
			return count == 2 ? (uint32_t)Icon_SelectEntry(renderer, icon, (int32_t)words[1]) : 0;
		case 0xF0000002u:
			return count == 3 ? (uint32_t)Icon_SelectPart(renderer, icon, (int32_t)words[1], (int32_t)words[2]) : 0;
		case 0xF0000003u:
			if(count == 2)
			{
				icon->active = words[1];
				return 1;
			}
			return 0;
		case 0xF0000004u:
			return count == 5 ? (uint32_t)Icon_SetPartImage(renderer, icon, (int32_t)words[1], (int32_t)words[2], words[3], (int32_t)words[4]) : 0;
		case 0xF0000005u:
		{
			// 0x0044A6E0: back to its place at depth word 3. The original reads the
			// place from the entry's FIRST part record, whatever part is named.
			if(count != 4)
				return 0;
			int32_t entry = (int32_t)words[1], part = (int32_t)words[2];
			if(!Icon_ValidPart(icon, entry, part))
				return 0;
			IconPartRecord_t* first = &icon->entries[entry].parts[0];
			return (uint32_t)Icon_MovePart(renderer, icon, entry, part, first->x, first->y, (int32_t)words[3]);
		}
		case 0xF0000006u:
			return count == 5 ? (uint32_t)Icon_SetPartAnimation(icon, (int32_t)words[1], (int32_t)words[2], words[3], words[4]) : 0;
		case 0xF0000007u:
			return count == 6 ? (uint32_t)Icon_MovePart(renderer, icon, (int32_t)words[1], (int32_t)words[2], (int32_t)words[3], (int32_t)words[4], (int32_t)words[5]) : 0;
	}
	return 0;
}

// ----------------------------------------------------------------------------------
// The pass (0x00448770)
// ----------------------------------------------------------------------------------
// The logical input bits in the order the key tables list them.
static const uint32_t kIconBits[24] = {
	0x1, 0x2, 0x4, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000,
	0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000,
	0x1000000, 0x2000000, 0x40000000,
};
// The four built-in tables (on the stack of 0x00448770): left click decides (1), right
// cancels (2), Enter decides the current part (3), Space cancels without a value (4),
// Tab switches entries by the Shift key (0x13); the arrows differ by map: 0 moves parts
// left/right and entries up/down, 1 the other way round, 2 walks a grid, 3 a grid on
// its side.
static const uint32_t kIconMaps[4][24] = {
	{ 1, 2, 0, 0, 0, 0, 0, 3, 4, 7, 8, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x13 },
	{ 1, 2, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x13 },
	{ 1, 2, 0, 0, 0, 0, 0, 3, 4, 0xB, 0xC, 9, 0xA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x13 },
	{ 1, 2, 0, 0, 0, 0, 0, 3, 4, 9, 0xA, 0xB, 0xC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x13 },
};

// 0x00449FB0: a part whose record names a key (+0x38) that was pressed - or held,
// with bit 31 - while the icon has the keyboard.
static int32_t Icon_HotkeyHit(Icon_t* icon)
{
	if(!icon->keyboard || !Input_AboveModal(Icon_WindowKey(icon)))
		return -1;
	for(int32_t n = 0; n < icon->hitCount; n++)
	{
		if(!Input_HasKeyboard(Icon_WindowKey(icon)))
			continue;
		IconHit_t* hit = &icon->hits[n];
		if(!hit->hasImage)
			continue;
		uint32_t key = hit->record->hotkey;
		uint32_t vk = key & 0x7FFFFFFF;
		if(vk == 0)
			continue;
		int held = (key & 0x80000000u) ? Input_HeldLong(vk) : 0;
		if(Input_Take(vk) || Input_BitForKey(icon->input, vk) || held)
			return n;
	}
	return -1;
}

static void Icon_PostPartMove(Icon_t* icon, int32_t entry, int32_t part, uint32_t direction)
{
	Icon_PostEvent(icon, 0x10000004u, ((uint32_t)entry << 16) | (uint32_t)part, direction);
}

static void Icon_PostEdge(Icon_t* icon, int32_t entry, int32_t part, uint32_t direction)
{
	Icon_PostEvent(icon, 0x10000005u, ((uint32_t)entry << 16) | ((uint32_t)part & 0xFFFF), direction);
}

// Returns whether the icon is finished (vtable+0x14 after a decision; always 0 for an
// Ex icon, which keeps running until it is destroyed).
static int Icon_Interact(Renderer_t* renderer, Icon_t* icon)
{
	int finished = 0;
	Icon_Animate(renderer, icon);

	int32_t entry = -1, part = -1;
	IconEntry_t* record = NULL;
	if(Icon_ValidEntry(icon, icon->currentEntry))
	{
		entry = icon->currentEntry;
		record = &icon->entries[entry];
		part = record->selectedPart;
	}

	// 0x004487C8: what is under the pointer at all, posted as 0x10000001 when it
	// changes.
	int32_t under = Icon_HitUnderPointer(icon, NULL, NULL, 0, 0);
	if(under != icon->under)
	{
		icon->under = under;
		Icon_PostEvent(icon, 0x10000001u,
		               under != -1 ? (uint32_t)icon->hits[under].entry : 0xFFFFFFFFu,
		               under != -1 ? (uint32_t)icon->hits[under].part : 0xFFFFFFFFu);
	}
	if(Icon_SetHover(renderer, icon, Icon_HoverTarget(icon), 0))
		Icon_AfterHover(icon);

	int32_t px, py;
	int32_t checked = Icon_HitUnderPointer(icon, &px, &py, 1, 1);
	if(checked != icon->underChecked)
	{
		icon->underChecked = checked;
		icon->hitX = px;
		icon->hitY = py;
	}

	if(icon->active)
	{
		// 0x0044889A: an entry that follows the pointer selects the part under it
		// when the pointer has moved.
		if(icon->hover != -1 && icon->entries[icon->hits[icon->hover].entry].words[1] != 0)
		{
			int32_t mx, my;
			Input_GetMouse(&mx, &my);
			if(icon->mouseX != mx || icon->mouseY != my)
			{
				IconHit_t* hit = &icon->hits[icon->hover];
				Icon_SelectEntry(renderer, icon, hit->entry);
				int changed = Icon_SelectPart(renderer, icon, hit->entry, hit->part);
				if(record != NULL)
					part = record->selectedPart;
				icon->mouseX = mx;
				icon->mouseY = my;
				if(changed)
					Icon_PostPartMove(icon, icon->hits[icon->hover].entry, icon->hits[icon->hover].part, 0);
			}
		}
		// 0x00448958: an entry that takes a held button as a click.
		if(icon->hover != -1 && icon->entries[icon->hits[icon->hover].entry].words[2] != 0)
		{
			if(Input_MouseHeld(Object_DrawKey(icon->object)) & 1)
				icon->input |= 1;
		}
	}

	// 0x00448C63: the first input bit the icon does not ignore, through the key map.
	uint32_t bits = icon->input & ~icon->ignoreBits;
	uint32_t action = 0;
	for(int k = 0; k < 24; k++)
	{
		if(bits & kIconBits[k])
		{
			action = icon->keyMap < 4 ? kIconMaps[icon->keyMap][k] : gKeyMaps[icon->keyMap - 4][k];
			if(action == 0x13)
				action = OS_IsKeyDown(0x10) ? 7 : 8;
			break;
		}
	}

	// 0x00448E18: a press that waits for its release decides when the button is up
	// over the same part; anything else in between forgets it.
	if(icon->pressed != -1)
	{
		if(action != 0)
			icon->pressed = -1;
		else if(!OS_IsKeyDown(0x01))
		{
			if(icon->underChecked == icon->pressed)
				action = 1;
			else
				icon->pressed = -1;
		}
	}

	int32_t cols = entry != -1 ? icon->grid[entry * 2] : 0;
	int32_t rows = entry != -1 ? icon->grid[entry * 2 + 1] : 0;
	switch(action)
	{
		case 1:
		{
			int32_t h = icon->underChecked;
			if(h == -1)
			{
				Icon_ClickOnNothing(icon);
				return 0;
			}
			IconHit_t* hit = &icon->hits[h];
			if(!Icon_Decidable(icon, hit->entry, hit->part))
				return 0;
			if(!Icon_DecidesOnPress(icon, h) && icon->pressed == -1)
			{
				icon->pressed = h;
				return 0;
			}
			Icon_DecideHit(icon, h, 1);
			if(icon->decideSelects)
				Icon_SelectPart(renderer, icon, hit->entry, hit->part);
			finished = 0;   // vtable+0x14
			if(!finished)
				Icon_SelectEntry(renderer, icon, hit->entry);
			icon->pressed = -1;
			return finished;
		}
		case 2:
			Icon_DecideHit(icon, -1, 1);
			return 0;
		case 3:
			if(Icon_Decidable(icon, entry, part) && entry != -1 && part != -1)
				Icon_DecidePart(icon, entry, part, 0);
			return 0;
		case 4:
			Icon_DecideHit(icon, -1, 0);
			return 0;
		case 5:
		case 6:
			if(record == NULL || record->partCount <= 0)
				return 0;
			for(int32_t n = 0; n < record->partCount; n++)
			{
				part += action == 5 ? -1 : 1;
				if(part < 0 || part >= record->partCount)
					part = action == 5 ? record->partCount - 1 : 0;
				if(Icon_SelectPart(renderer, icon, entry, part))
				{
					Icon_PostPartMove(icon, entry, part, action == 5 ? 0xFFFFFFFFu : 1);
					return 0;
				}
			}
			return 0;
		case 7:
		case 8:
			if(icon->entryCount <= 0)
				return 0;
			for(int32_t n = 0; n < icon->entryCount; n++)
			{
				entry += action == 7 ? -1 : 1;
				if(entry < 0 || entry >= icon->entryCount)
					entry = action == 7 ? icon->entryCount - 1 : 0;
				if(Icon_SelectEntry(renderer, icon, entry))
				{
					Icon_PostEvent(icon, 0x10000003u, (uint32_t)entry, action == 7 ? 0xFFFFFFFFu : 1);
					return 0;
				}
			}
			return 0;
		case 9:
		case 10:
			if(record == NULL || cols <= 0)
				return 0;
			for(int32_t n = 0; n < cols; n++)
			{
				int32_t row = part / cols;
				int32_t col = part % cols + (action == 9 ? -1 : 1);
				if(action == 9 && col < 0)
					col = cols - 1;
				if(action == 10 && col >= cols)
					col = 0;
				part = cols * row + col;
				if(Icon_SelectPart(renderer, icon, entry, part))
				{
					Icon_PostPartMove(icon, entry, part, action == 9 ? 0xFFFFu : 1);
					return 0;
				}
			}
			return 0;
		case 11:
		case 12:
			if(record == NULL || rows <= 0)
				return 0;
			for(int32_t n = 0; n < rows; n++)
			{
				int32_t row = part / cols + (action == 11 ? -1 : 1);
				if(action == 11 && row < 0)
					row = rows - 1;
				if(action == 12 && row >= rows)
					row = 0;
				part = cols * row + part % cols;
				if(Icon_SelectPart(renderer, icon, entry, part))
				{
					Icon_PostPartMove(icon, entry, part, action == 11 ? 0xFFFF0000u : 0x10000u);
					return 0;
				}
			}
			return 0;
		case 13:
			if(record == NULL)
				return 0;
			if(part > 0 && Icon_SelectPart(renderer, icon, entry, part - 1))
				Icon_PostPartMove(icon, entry, part - 1, 0xFFFFFFFFu);
			else
				Icon_PostEdge(icon, entry, part, 0xFFFFFFFFu);
			return 0;
		case 14:
			if(record == NULL)
				return 0;
			if(Icon_SelectPart(renderer, icon, entry, part + 1))
				Icon_PostPartMove(icon, entry, part + 1, 1);
			else
				Icon_PostEdge(icon, entry, part, 1);
			return 0;
		case 15:
		case 16:
		case 17:
		case 18:
		{
			// 0x00449462..0x004495AA: the next usable part towards one edge of the
			// grid, or the edge itself.
			if(record == NULL || cols <= 0)
				return 0;
			int32_t row = part / cols, col = part % cols;
			static const uint32_t kDirections[4] = { 0xFFFFu, 1, 0xFFFF0000u, 0x10000u };
			uint32_t direction = kDirections[action - 15];
			for(;;)
			{
				if(action == 15) { if(col <= 0) break; col--; }
				else if(action == 16) { if(col + 1 >= cols) break; col++; }
				else if(action == 17) { if(row <= 0) break; row--; }
				else { if(row + 1 >= rows) break; row++; }
				int32_t candidate = cols * row + col;
				if(Icon_SelectPart(renderer, icon, entry, candidate))
				{
					Icon_PostPartMove(icon, entry, candidate, direction);
					return 0;
				}
			}
			Icon_PostEdge(icon, entry, part, direction);
			return 0;
		}
		default:
		{
			// 0x00449612: a part's own key.
			int32_t h = Icon_HotkeyHit(icon);
			if(h != -1)
				Icon_DecideHit(icon, h, 0);
			return 0;   // vtable+0x14
		}
	}
}

// vtable+0x04 (0x00448680): one pass of a live icon: its messages, its input (the
// window's keys with the keyboard, the window's pointer region, the icon's own object)
// and the interaction. Without the keyboard only the pointer bits 0x2C3 count.
static void Icon_Update(Renderer_t* renderer, Icon_t* icon)
{
	Icon_DrainMessages(renderer, icon);
	if(!icon->ready)
		return;
	if(icon->active)
	{
		uint32_t bits = 0;
		if(icon->keyboard)
			bits = Input_RegionStateOf(Icon_WindowKey(icon), Icon_WindowKey(icon));
		else if(icon->mouseRegion)
			bits = Input_RegionStateOf(0, Icon_WindowKey(icon));
		bits |= Input_RegionStateOf(0, Object_DrawKey(icon->object));
		icon->input = bits;
		if(!icon->keyboard)
			icon->input &= 0x2C3;
	}
	else
		icon->input = 0;
	int finished = Icon_Interact(renderer, icon);
	icon->ready = !finished;
	Icon_FlushDirty(icon);
}

void Icon_FrameUpdate(Renderer_t* renderer)
{
	// 0x0048CDDC: mode 0 runs every enabled icon (0x0046C6F0), mode 1 only their
	// messages (0x0046C730 -> 0x004478A0).
	for(Icon_t* icon = gIcons; icon != NULL; icon = icon->next)
	{
		if(!icon->enabled)
			continue;
		if(gIconMode == 0)
			Icon_Update(renderer, icon);
		else if(gIconMode == 1)
		{
			Icon_DrainMessages(renderer, icon);
			Icon_FlushDirty(icon);
		}
	}
}

int Icon_SetMode(uint32_t mode)
{
	if(mode > 1)
		return 0;
	gIconMode = mode;
	return 1;
}

int Icon_SetKeyMap(uint32_t map, const uint32_t* actions)
{
	// 0x00447C90 (after 0x00447C70).
	if(!gKeyMapsCleared)
	{
		memset(gKeyMaps, 0, sizeof(gKeyMaps));
		gKeyMapsCleared = 1;
	}
	if(map - 4 >= 4)
		return 0;
	memcpy(gKeyMaps[map - 4], actions, sizeof(gKeyMaps[0]));
	return 1;
}

// ----------------------------------------------------------------------------------
// Content (0x0044A130 / 0x0044C3B0 teardown, 0x0044A9E0 build)
// ----------------------------------------------------------------------------------
static void Icon_Teardown(Icon_t* icon)
{
	// 0x0044C3B0: the Ex's copies, then the base (0x0044A130).
	for(uint32_t i = 0; i < icon->root[0] && icon->copies != NULL; i++)
	{
		free(icon->copies[i].parts);
		if(icon->states != NULL)
			free(icon->states[i]);
	}
	free(icon->copies);
	free(icon->states);
	free(icon->order);
	icon->copies = NULL;
	icon->states = NULL;
	icon->order = NULL;
	icon->orderCount = 0;
	memset(icon->root, 0, sizeof(icon->root));

	// 0x0044A130: not ready; the input the window and the icon's object have waiting
	// is taken; every part's object leaves the pointer list and the window, and goes.
	icon->ready = 0;
	Input_RegionStateOf(Icon_WindowKey(icon), Icon_WindowKey(icon));
	Input_RegionStateOf(0, Object_DrawKey(icon->object));
	for(int32_t n = 0; n < icon->hitCount && icon->hits != NULL; n++)
	{
		IconHit_t* hit = &icon->hits[n];
		if(!hit->hasImage || hit->object == NULL)
			continue;
		Region_RemoveByOwner(hit->object);
		Object_Detach(icon->window, hit->object);
		Object_FreeDetached(hit->object);
	}
	for(int32_t i = 0; i < icon->entryCount && icon->entries != NULL; i++)
		free(icon->entries[i].parts);
	free(icon->entries);
	free(icon->hits);
	free(icon->grid);
	if(icon->keyboard)
	{
		Region_RemoveByKey(0, Icon_WindowKey(icon));
		Region_RemoveByKey(1, Icon_WindowKey(icon));
		Input_RemoveModal(icon->handle | 0x80000000u);
	}
	else if(icon->mouseRegion)
		Region_RemoveByOwner(icon->window);
	icon->entryCount = 0;
	icon->entries = NULL;
	icon->currentEntry = -1;
	icon->keyboard = icon->mouseRegion = icon->ignoreBits = icon->keyMap = icon->decideSelects = 0;
	icon->grid = NULL;
	icon->hitCount = 0;
	icon->hits = NULL;
	icon->hover = -1;
	while(Icon_DropEvent(icon))
		;
}

uint32_t Icon_SetContent(Renderer_t* renderer, Icon_t* icon, const IconContent_t* content)
{
	if(icon == NULL || content == NULL)
		return ICON_CONTENT_SET_BAD_COUNT;
	DisplayObject_t* window = icon->window;

	// 0x0044AA35: the old content, then the window emptied - the eight text parts
	// off (0x0042BC90), no content slots (0x0042BCB0), the text surface cleared and
	// its weight and flag reset (0x0042BA90, 0x0042B630, 0x0042B620), and the whole
	// window redrawn (0x0042CAE0).
	Icon_Teardown(icon);
	for(int k = 0; k < 8; k++)
		Window_EnablePart(renderer, window, k, 0);
	Window_ReserveContent(renderer, window, 0);
	Window_RedrawAll(renderer, window);

	uint32_t result = ICON_CONTENT_SET_BAD_COUNT;
	int32_t count = (int32_t)content->raw[0];
	if(count <= 0 || count > ICON_CONTENT_MAX_COUNT)
		goto done;

	// 0x0044AA8B: {columns, rows} per entry; a part count outside 1..0x100 stops the
	// walk at the next entry and answers 0x80000002.
	icon->grid = (int32_t*)calloc((size_t)count, 2 * sizeof(int32_t));
	int32_t total = 0;
	for(int32_t i = 0; i < count; i++)
	{
		int32_t parts = (int32_t)content->entries[i].raw[0];
		int32_t cols = (int32_t)content->entries[i].raw[1];
		if(cols > parts || cols <= 0)
			cols = parts;
		icon->grid[i * 2] = cols;
		icon->grid[i * 2 + 1] = cols != 0 ? (parts + cols - 1) / cols : 0;
		total += parts;
		if(parts - 1 > 0xFF || parts <= 0)
		{
			free(icon->grid);
			icon->grid = NULL;
			result = ICON_CONTENT_SET_BAD_ENTRY;
			goto done;
		}
	}

	// 0x0044AB21: one content slot per part; the root and the entries are the icon's
	// own from here.
	Window_ReserveContent(renderer, window, (uint32_t)total);
	memcpy(icon->root, content->raw, sizeof(icon->root));
	icon->copies = (IconContentEntry_t*)calloc((size_t)count, sizeof(IconContentEntry_t));
	icon->entryCount = count;
	icon->entries = (IconEntry_t*)calloc((size_t)count, sizeof(IconEntry_t));
	icon->currentEntry = (int32_t)content->raw[2] >= 0 && (int32_t)content->raw[2] < count ? (int32_t)content->raw[2] : -1;
	icon->keyboard = content->raw[3];
	icon->mouseRegion = content->raw[4];
	icon->ignoreBits = content->raw[5];
	icon->keyMap = content->raw[6] & 7;
	icon->decideSelects = content->raw[7];
	icon->hitCount = total;
	icon->active = content->raw[8] == 0;
	icon->hits = (IconHit_t*)calloc((size_t)total, sizeof(IconHit_t));
	icon->states = (IconPartState_t**)calloc((size_t)count, sizeof(IconPartState_t*));

	// A list of {layer, slot} kept highest layer first; a part goes in before the
	// first one of the same layer or lower (0x0044B05B).
	int32_t* orderSlot = (int32_t*)malloc((size_t)total * sizeof(int32_t));
	uint32_t* orderLayer = (uint32_t*)malloc((size_t)total * sizeof(uint32_t));
	int32_t ordered = 0;

	int32_t slot = 0;
	for(int32_t i = 0; i < count; i++)
	{
		const IconContentEntry_t* source = &content->entries[i];
		IconContentEntry_t* copy = &icon->copies[i];
		IconEntry_t* record = &icon->entries[i];
		int32_t parts = (int32_t)source->raw[0];
		memcpy(copy->raw, source->raw, sizeof(copy->raw));
		copy->partCount = (uint32_t)parts;
		copy->parts = (uint32_t*)calloc((size_t)parts, ICON_CONTENT_PART_WORDS * sizeof(uint32_t));
		record->partCount = parts;
		record->parts = (IconPartRecord_t*)calloc((size_t)parts, sizeof(IconPartRecord_t));
		record->selectedPart = (int32_t)source->raw[3] >= 0 && (int32_t)source->raw[3] < parts ? (int32_t)source->raw[3] : -1;
		for(int w = 0; w < 10; w++)
			record->words[w] = (int32_t)source->raw[5 + w];
		icon->states[i] = (IconPartState_t*)calloc((size_t)parts, sizeof(IconPartState_t));

		for(int32_t j = 0; j < parts; j++, slot++)
		{
			const uint32_t* p = source->parts + (size_t)j * ICON_CONTENT_PART_WORDS;
			// 0x0044AD74: the selected part must be used.
			if(j == record->selectedPart && p[ICON_PART_USED] == 0)
			{
				record->selectedPart = -1;
				copy->raw[3] = 0xFFFFFFFFu;
			}
			IconPartState_t* state = &icon->states[i][j];
			memset(state, 0, sizeof(*state));
			uint32_t* own = copy->parts + (size_t)j * ICON_CONTENT_PART_WORDS;
			memcpy(own, p, ICON_CONTENT_PART_WORDS * sizeof(uint32_t));

			int32_t bitmap = (int32_t)p[ICON_PART_BITMAP];
			if(bitmap != -1 && j == record->selectedPart && (int32_t)p[ICON_PART_SELECTED] != -1)
				bitmap = (int32_t)p[ICON_PART_SELECTED];
			Bitmap_t* image = p[ICON_PART_USED] != 0 ? Renderer_ResolveBitmap(renderer, bitmap) : NULL;

			IconHit_t* hit = &icon->hits[slot];
			IconPartRecord_t* rec = &record->parts[j];
			hit->hasImage = image != NULL;
			hit->entry = i;
			hit->part = j;
			hit->record = rec;
			hit->object = NULL;
			if(image == NULL)
			{
				// 0x0044B0AA: a record with nothing in it.
				rec->x = rec->y = 0;
				rec->bitmap = rec->selected = rec->hover = rec->hitMask = -1;
				rec->cursor[0] = rec->cursor[1] = 0;
				rec->cursor[2] = -1;
				rec->cursor[3] = rec->cursor[4] = 0;
				rec->cursor[5] = -1;
				rec->hotkey = 0;
				continue;
			}

			state->used = 1;
			state->part = own;
			state->slot = (uint32_t)slot;
			uint32_t flags = p[ICON_PART_FLAGS];
			state->layer = (flags & 0x10) ? p[ICON_PART_USED] : (flags & 0x02) ? p[ICON_PART_Y] : (uint32_t)slot;
			rec->w0 = p[ICON_PART_W0];
			rec->x = (int32_t)p[ICON_PART_X];
			rec->y = (int32_t)p[ICON_PART_Y];
			rec->bitmap = (int32_t)p[ICON_PART_BITMAP];
			rec->selected = (int32_t)p[ICON_PART_SELECTED];
			rec->hover = (int32_t)p[ICON_PART_HOVER];
			rec->hitMask = (int32_t)p[ICON_PART_HITMASK];
			for(int k = 0; k < 6; k++)
				rec->cursor[k] = (int32_t)p[ICON_PART_CURSOR + k];
			rec->f34 = 0;
			rec->hotkey = 0;

			// 0x0042BFB0: the sprite, its anchor on (x + anchor, y + anchor).
			Window_SetContentSlot(renderer, window, (uint32_t)slot, bitmap,
			                      (int32_t)(p[ICON_PART_X] + p[ICON_PART_ANCHOR_X]),
			                      (int32_t)(p[ICON_PART_Y] + p[ICON_PART_ANCHOR_Y]),
			                      (int32_t)p[ICON_PART_ANCHOR_X], (int32_t)p[ICON_PART_ANCHOR_Y], state->layer);
			Window_RedrawContentSlot(renderer, window, (uint32_t)slot);

			// 0x0044AF35: the part's virtual object - the image's size, shown as the
			// window is, at the window's position and origin and priority, attached
			// to the window at the part's place.
			DisplayObject_t* object = Object_CreateVirtual(window);
			hit->object = object;
			if(object != NULL)
			{
				Object_SetExtent(object, image->width, image->height);
				Object_SetVisible(object, Object_IsDrawable(window));
				Object_SetPosition(object, window->x, window->y);
				Object_SetOrigin(object, window->originX, window->originY);
				object->priority = window->priority;
				Object_Attach(window, object, (int32_t)p[ICON_PART_X], (int32_t)p[ICON_PART_Y]);
				// 0x0044B012: its hit mask.
				if(p[ICON_PART_W0] != 0 && (int32_t)p[ICON_PART_HITMASK] != -2)
				{
					Bitmap_t* mask = Renderer_ResolveBitmap(renderer, (int32_t)p[ICON_PART_HITMASK]);
					if(mask != NULL)
						Object_SetHitMask(object, mask);
				}
				else
					Object_DisableHit(object);
				// 0x0046D860: a pointer region the object owns.
				Region_Add(0, Object_DrawKey(object), 0, 0, 0, 0, object);
			}

			int32_t at = 0;
			while(at < ordered && orderLayer[at] > state->layer)
				at++;
			memmove(&orderLayer[at + 1], &orderLayer[at], (size_t)(ordered - at) * sizeof(uint32_t));
			memmove(&orderSlot[at + 1], &orderSlot[at], (size_t)(ordered - at) * sizeof(int32_t));
			orderLayer[at] = state->layer;
			orderSlot[at] = slot;
			ordered++;
		}
	}
	icon->order = orderSlot;
	icon->orderCount = ordered;
	free(orderLayer);

	// 0x0044B188: the window moved to where it is (vtable+0x30, vtable+0x2C), which
	// takes the new objects with it.
	{
		int32_t wx, wy;
		Object_GetPosition(window, &wx, &wy);
		Object_Move(window, wx, wy);
	}
	// 0x0044B1AB: the keyboard - the window's key in both lists and on the pointer
	// shut-out list, and the current entry selected - or the window's pointer region.
	if(icon->keyboard)
	{
		uint32_t key = Icon_WindowKey(icon);
		Region_Add(0, key, (int32_t)0x80000000, (int32_t)0x80000000, (int32_t)0x7FFFFFFF, (int32_t)0x7FFFFFFF, NULL);
		Region_Add(1, key, 0, 0, 0, 0, NULL);
		Input_AddModal(icon->handle | 0x80000000u, key);
		Icon_SelectEntry(renderer, icon, icon->currentEntry);
	}
	else if(icon->mouseRegion)
		Region_Add(0, Icon_WindowKey(icon), 0, 0, 0, 0, window);
	// 0x0044B20D: waiting input taken.
	Input_RegionStateOf(Icon_WindowKey(icon), Icon_WindowKey(icon));
	Input_RegionStateOf(0, Object_DrawKey(icon->object));

	// 0x0044B242: the part under the pointer now.
	if(Icon_SetHover(renderer, icon, Icon_HoverTarget(icon), 1))
		Icon_AfterHover(icon);
	// 0x0044B296: an entry and part selected from the start keep the pointer where it
	// is from following them.
	if((int32_t)content->raw[2] != -1 && Icon_ValidEntry(icon, (int32_t)content->raw[2])
	   && (int32_t)content->entries[content->raw[2]].raw[3] != -1)
		Input_GetMouse(&icon->mouseX, &icon->mouseY);
	else
		icon->mouseX = icon->mouseY = 0x7FFFFFFF;
	icon->under = Icon_HitUnderPointer(icon, NULL, NULL, 0, 0);
	icon->underChecked = -1;
	icon->ready = 1;
	result = ICON_CONTENT_SET_OK;

done:
	// 0x0044B2FB: the window damaged where it is, and the icon marked.
	Icon_Damage(icon);
	Icon_MarkDirty(icon);
	return result;
}

// ----------------------------------------------------------------------------------
// The script's other calls
// ----------------------------------------------------------------------------------
void Icon_GetState(Icon_t* icon, uint32_t out[6])
{
	// 0x004485A0.
	int32_t x = 0, y = 0;
	if(icon->lastValue != 0)
		Icon_PartPoint(icon, icon->lastEntry, icon->lastPart, &x, &y);
	out[0] = icon->ready;
	out[1] = (uint32_t)icon->lastEntry;
	out[2] = (uint32_t)icon->lastPart;
	out[3] = icon->lastValue;
	out[4] = (uint32_t)x;
	out[5] = (uint32_t)y;
}

uint32_t Icon_SetPartEnabled(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part, uint32_t enabled)
{
	// 0x0044B540.
	if(entry >= (int32_t)icon->root[0])
		return 0x80000001u;
	if(part >= (int32_t)icon->copies[entry].raw[0])
		return 0x80000002u;
	IconPartState_t* state = &icon->states[entry][part];
	if(state->used)
	{
		Window_SlotSetEnabled(icon->window, state->slot, enabled);
		Window_RedrawContentSlot(renderer, icon->window, state->slot);
		Icon_Damage(icon);
		Icon_MarkDirty(icon);
	}
	return 0;
}

int32_t Icon_SelectedParts(Icon_t* icon, uint32_t* out)
{
	// 0x00448610.
	for(int32_t i = 0; i < icon->entryCount; i++)
		out[i] = (uint32_t)icon->entries[i].selectedPart;
	return icon->entryCount;
}

// ----------------------------------------------------------------------------------
// Creation and destruction (0x0046C7B0, 0x00447A70, 0x0044A8A0, 0x0046C670)
// ----------------------------------------------------------------------------------
Icon_t* Icon_Resolve(uint32_t handle)
{
	for(Icon_t* icon = gIcons; icon != NULL; icon = icon->next)
		if(icon->handle == handle)
			return icon;
	return NULL;
}

uint32_t Icon_Create(uint32_t windowHandle, uint32_t kind)
{
	// 0x0046C7B0: 0 for a kind other than 0 or 1. The window resolver (0x004407A0)
	// answers NULL for a handle that is not a window, which the constructor would
	// then use; refuse it by name instead.
	if(kind != ICON_KIND_PLAIN && kind != ICON_KIND_EX)
		return 0;
	DisplayObject_t* window = Object_ResolveKind(windowHandle, OBJECT_TYPE_WINDOW);
	if(window == NULL)
	{
		printf("[Engine]: Error: an icon can only be made from a window handle (0x%08X is not one)\n", windowHandle);
		return 0;
	}
	Icon_t* icon = (Icon_t*)calloc(1, sizeof(Icon_t));
	if(icon == NULL)
		return 0;
	// 0x004477B0: the serial, enabled, and the window's receiver when it has none.
	icon->handle = ++gIconSerial;
	icon->enabled = 1;
	icon->kind = kind;
	icon->window = window;
	if(window->receiver == NULL)
		window->receiver = icon;
	// 0x00447A70: the key tables cleared on first use (0x00447C70), and the icon's own
	// object at the window's position, origin and priority, attached at 0, 0.
	if(!gKeyMapsCleared)
	{
		memset(gKeyMaps, 0, sizeof(gKeyMaps));
		gKeyMapsCleared = 1;
	}
	icon->object = Object_CreateVirtual(window);
	if(icon->object == NULL)
	{
		free(icon);
		return 0;
	}
	Object_SetPosition(icon->object, window->x, window->y);
	Object_SetOrigin(icon->object, window->originX, window->originY);
	icon->object->priority = window->priority;
	Object_Attach(window, icon->object, 0, 0);
	icon->currentEntry = -1;
	icon->hover = -1;
	icon->active = 1;
	icon->pressed = -1;
	icon->under = icon->underChecked = -1;
	// 0x0046C620.
	icon->next = gIcons;
	gIcons = icon;
	gIconCount++;
	return icon->handle;
}

int Icon_Destroy(uint32_t handle)
{
	Icon_t** link = &gIcons;
	while(*link != NULL && (*link)->handle != handle)
		link = &(*link)->next;
	if(*link == NULL)
		return 0;
	Icon_t* icon = *link;
	*link = icon->next;
	gIconCount--;
	// 0x0044A980 -> 0x0044C3B0, then 0x00447BF0: the icon's object out of the window
	// and gone, the events and messages dropped, and the window's receiver cleared
	// when it was this icon (0x0041ADE0).
	Icon_Teardown(icon);
	if(icon->object != NULL)
	{
		Object_Detach(icon->window, icon->object);
		Object_FreeDetached(icon->object);
	}
	while(Icon_DropEvent(icon))
		;
	uint32_t words[0x100];
	while(Icon_TakeMessage(icon, words) > 0)
		;
	if(icon->window->receiver == icon)
		icon->window->receiver = NULL;
	free(icon);
	return 1;
}

void Icon_FreeAll(void)
{
	while(gIcons != NULL)
		Icon_Destroy(gIcons->handle);
	gIconSerial = 0;
	gIconMode = 0;
}
