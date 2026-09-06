#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icon.h"
#include "object.h"
#include "thread.h"

// The list at 0x005667E8, its count at 0x005667E0 and the serial at 0x00565D74.
// 0x0046C620 pushes a new node onto the head, so the list is in reverse creation
// order; nothing walks it in order yet.
static Icon_t*  gIcons = NULL;
static uint32_t gIconCount = 0;
static uint32_t gIconSerial = 0;

uint32_t Icon_Count(void)
{
	return gIconCount;
}

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

Icon_t* Icon_Resolve(uint32_t handle)
{
	for(Icon_t* icon = gIcons; icon != NULL; icon = icon->next)
	{
		if(icon->handle == handle)
			return icon;
	}

	return NULL;
}

uint32_t Icon_Create(uint32_t windowHandle, uint32_t kind)
{
	if(kind != ICON_KIND_PLAIN && kind != ICON_KIND_EX)
	{
		// 0x0046C7B0 answers 0 for any other kind, before it allocates anything.
		printf("[Engine]: Error: icon kind %u is neither a DCIPIcon nor a DCIPIconEx\n", kind);
		return 0;
	}

	// 0x004407A0, the window resolver: the top byte has to be 0xB0. The original
	// then hands the result to the constructor without testing it, so a handle that
	// is not a window would dereference NULL there; refuse it by name instead.
	DisplayObject_t* window = Object_ResolveKind(windowHandle, OBJECT_TYPE_WINDOW);
	if(window == NULL)
	{
		printf("[Engine]: Error: an icon can only be made from a window handle (0x%08X is not one)\n",
		       windowHandle);
		return 0;
	}

	Icon_t* icon = (Icon_t*)malloc(sizeof(Icon_t));
	if(icon == NULL)
		return 0;

	memset(icon, 0, sizeof(Icon_t));
	icon->kind   = kind;
	icon->window = window;
	// 0x004477B0 pre-increments the counter, so the first icon's handle is 1.
	icon->handle = ++gIconSerial;

	// The display object the icon lives at. It is made with the window as its owner
	// and then attached to it, exactly as 0x00447A70 does it.
	icon->object = Object_CreateVirtual(window);
	if(icon->object == NULL)
	{
		free(icon);
		return 0;
	}

	// The window's own position, copied through vtable+0x38 rather than assigned -
	// the setter writes it down the children too. The original copies a second pair
	// (+0x40/+0x44, through 0x0041B3F0) as well; nothing in this engine writes that
	// pair, so both halves of it are zero.
	Object_SetPosition(icon->object, window->x, window->y);
	// 0x0042EA80 and 0x0041AEC0: the icon's object draws at the window's priority.
	icon->object->priority = window->priority;
	Object_Attach(window, icon->object, 0, 0);

	// 0x0046C620: the twelve-byte registration node, handle first.
	icon->next = gIcons;
	gIcons = icon;
	gIconCount++;

	return icon->handle;
}

void Icon_Destroy(uint32_t handle)
{
	Icon_t** link = &gIcons;
	while(*link != NULL && (*link)->handle != handle)
		link = &(*link)->next;

	if(*link == NULL)
		return;

	Icon_t* icon = *link;
	*link = icon->next;
	gIconCount--;
	// The display object belongs to the icon; the original's destructor takes it out
	// of the window before it frees it, which nothing here needs yet because nothing
	// destroys an icon.
	free(icon);
}

void Icon_FreeAll(void)
{
	while(gIcons != NULL)
		Icon_Destroy(gIcons->handle);

	gIconSerial = 0;
}
