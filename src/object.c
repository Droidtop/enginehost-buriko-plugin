#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "object.h"

// ----------------------------------------------------------------------------------
// Display objects
//
// The engine used to hand out 0x80000000, 0x80000001, ... from a counter with nothing
// behind them, which is wrong twice over: nothing can be asked about a sprite, and the
// original reuses freed indices instead of counting upwards. Groups had it worse: the
// same constant handle every time, and the wrong tag byte for a group at that.
//
// The original's model (fureraba.exe):
//   The display root is the object at 0x0056674C. Its slot array is root+0x5C with
//   512 entries, the live count is root+0x85C and a serial counter is root+0x860.
//   A handle is index | 0x80000000.
//   Resolve (0x0043E6B0) requires the top byte to be 0x80, pulls the index out with
//   `and eax, 0xFFFFFF` (0x00443340), refuses an index of 0x200 or more, and returns
//   root[index] - which is NULL for a freed slot, so callers test the object and not
//   the handle.
//   Allocate (0x0043E5F0) returns 0 once the count reaches 0x200. Otherwise it takes
//   the FIRST free slot, allocates 0x418 bytes, constructs the object with 0x00425790
//   (vtable 0x004E4F24) passing the serial before incrementing it, attaches it to the
//   display list through root+0x14 (0x004307D0), increments the count and returns the
//   handle.
//
// What construction leaves behind, which decides whether a fresh sprite is drawable:
// the sprite constructor (0x00425790) calls the display-object constructor
// (0x0041A4D0) with the type 2, and that one sets +0x04 = 1 (0x0041AE30),
// +0x0C = 0 (0x0041AE70), +0x10 = 0, +0x14 = 0 (0x0041AED0), +0x11C = 0, +0x130 = 0,
// +0x48 = 1, +0xA8 = 0x80 (0x0041B6D0), +0xAC = 0, +0xB0 = 0 (0x0041B730) and
// +0xB4 = 0x100 (0x0041B770).
//
// "Would this be drawn" (0x0041AF00) answers 1 only when +0x14 is non-zero, +0x04 is
// non-zero, +0x0C is zero, +0xB0 is below 0x100 and +0xB4 is above 0. So a fresh
// sprite fails on exactly one condition, its visible flag, and every other test
// already passes - which is why showing a new sprite is what changes the answer.
// ----------------------------------------------------------------------------------

// One table per kind, each with the live count and the serial counter the original
// keeps beside it (a sprite's at root+0x85C and root+0x860, a group's at root+0xA34
// and root+0xA38).
static DisplayObject_t* gSprites[SPRITE_SLOT_COUNT] = { NULL };
static uint32_t gSpriteCount = 0;
static uint32_t gSpriteSerial = 0;
static DisplayObject_t* gGroups[GROUP_SLOT_COUNT] = { NULL };
static uint32_t gGroupCount = 0;
static uint32_t gGroupSerial = 0;

typedef struct ObjectKind
{
	uint32_t          tag;
	uint32_t          type;
	uint32_t          slotCount;
	DisplayObject_t** slots;
	uint32_t*         count;
	uint32_t*         serial;
} ObjectKind_t;

static const ObjectKind_t gKinds[] = {
	{ OBJECT_TAG_SPRITE, OBJECT_TYPE_SPRITE, SPRITE_SLOT_COUNT, gSprites, &gSpriteCount, &gSpriteSerial },
	{ OBJECT_TAG_GROUP,  OBJECT_TYPE_GROUP,  GROUP_SLOT_COUNT,  gGroups,  &gGroupCount,  &gGroupSerial  },
};
#define OBJECT_KIND_COUNT (sizeof(gKinds) / sizeof(gKinds[0]))

static const ObjectKind_t* Object_KindForTag(uint32_t tag)
{
	for(size_t i = 0; i < OBJECT_KIND_COUNT; i++)
		if(gKinds[i].tag == tag)
			return &gKinds[i];
	return NULL;
}

// The original unions the sprite's rectangle into the global dirty region at
// 0x00565B2C (vtable +0x0C, 0x0041AF30, through the rectangle accessors at vtable
// +0x24 and +0x1C). A sprite here has no rectangle yet, because nothing can give it
// an image, so the damage is only counted for now - the count is what a renderer
// would consume, and it is not a made-up answer to anybody's question.
uint32_t  gObjectDamage = 0;
uint32_t  gDrawPriority = 0;

uint32_t Object_Create(uint32_t tag)
{
	const ObjectKind_t* kind = Object_KindForTag(tag);
	if(kind == NULL)
		return 0;

	if(*kind->count >= kind->slotCount)
		return 0;

	// The first free slot, not the next index: freed slots are reused.
	uint32_t index = 0;
	while(index < kind->slotCount && kind->slots[index] != NULL)
		index++;
	if(index >= kind->slotCount)
		return 0;

	DisplayObject_t* object = (DisplayObject_t*)malloc(sizeof(DisplayObject_t));
	if(object == NULL)
		return 0;

	// What the base constructor (0x0041A4D0) leaves behind, whatever the kind: the
	// type at +0x18 and the serial at +0x20 from its two arguments, then +0x04 = 1,
	// +0x08 = 0, +0x0C = 0, +0x10 = 0, +0x14 = 0, +0x48 = 1, +0xA8 = 0x80,
	// +0xAC = 0, +0xB0 = 0 and +0xB4 = 0x100.
	memset(object, 0, sizeof(DisplayObject_t));
	object->serial = (*kind->serial)++;
	object->type = kind->type;
	object->enabled = 1;
	object->priority = 1;
	object->unknownA8 = 0x80;
	object->opacity = 0x100;

	kind->slots[index] = object;
	(*kind->count)++;

	return index | kind->tag;
}
DisplayObject_t* Object_Resolve(uint32_t handle)
{
	const ObjectKind_t* kind = Object_KindForTag(handle & OBJECT_TAG_MASK);
	if(kind == NULL)
		return NULL;

	uint32_t index = handle & OBJECT_INDEX_MASK;
	if(index >= kind->slotCount)
		return NULL;

	return kind->slots[index];
}

DisplayObject_t* Object_ResolveKind(uint32_t handle, uint32_t type)
{
	DisplayObject_t* object = Object_Resolve(handle);
	if(object == NULL || object->type != type)
		return NULL;

	return object;
}
int Object_IsDrawable(const DisplayObject_t* object)
{
	if(object == NULL)
		return 0;

	return object->visible != 0
		&& object->enabled != 0
		&& object->flagUnknown0C == 0
		&& object->unknownB0 < 0x100
		&& object->opacity > 0;
}
void Object_SetVisible(DisplayObject_t* object, int visible)
{
	if(object == NULL)
		return;

	// 0x0041AED0 stores the flag and passes the same call down every child, so a
	// hidden parent hides its whole subtree. Nothing builds children yet; the walk is
	// here because it is the operation, not because it has anything to do today.
	object->visible = visible;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
		Object_SetVisible(child, visible);
}
void Object_ApplyVisible(DisplayObject_t* object, int visible)
{
	if(object == NULL)
		return;

	// 0x0043EF50 asks whether the object would be drawn, sets the flag, asks again,
	// and dirties the screen only when the answer changed - not merely when the flag
	// did, which are different things once opacity or a parent is involved.
	int before = Object_IsDrawable(object);
	Object_SetVisible(object, visible);
	int after = Object_IsDrawable(object);

	if(before != after)
		gObjectDamage++;
}
void Object_SetEnabled(DisplayObject_t* object, int enabled)
{
	if(object == NULL)
		return;

	// 0x0041AE30 stores the flag and walks the children only when +0x08 is
	// set, unlike the visible flag, which always walks them.
	object->enabled = enabled;
	if(object->propagateEnabled == 0)
		return;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
		Object_SetEnabled(child, enabled);
}
void Object_ApplyEnabled(DisplayObject_t* object, int enabled)
{
	if(object == NULL)
		return;

	// 0x00443460: would it be drawn, set the flag, would it be drawn now,
	// and dirty the screen only when that answer changed.
	int before = Object_IsDrawable(object);
	Object_SetEnabled(object, enabled);
	int after = Object_IsDrawable(object);

	if(before != after)
		gObjectDamage++;
}
// ----------------------------------------------------------------------------------
// Parameters (vtable+0x5C)
//
// The sprite's own table is at 0x00428670: a byte index table at 0x004288C0 covering
// the numbers 0x10 to 0x100 and a jump table of fourteen arms at 0x00428888. Its arms
// are, in the original's order:
//   0x10  0x004273C0(value1)
//   0x11  0x0042AC30(value1 & 0xFF, (value1 >> 8) & 0xFF, value1 >> 16, value2)
//   0x40  0x004280F0(value1, value2)   only when the object's kind (+0x134) is 2, 5 or 6
//   0x41  0x00428130(value1)           same kinds
//   0x42  0x00428200(value1, value2)   same kinds
//   0x43  0x004281D0(value1, value2)   only kind 6
//   0x60  0x00428610(value1 & 0xFFFF, value1 >> 16, value2)
//   0x80  kinds 2/5/6: +0x2AC = value1, +0x2B0 = value2
//   0x81  kinds 2/5: +0x2B4 = value1, +0x2CC = value2; kind 6: +0x2C0, +0x2CC
//   0x82  kinds 2/5/6: +0x2C4 = value1, +0x2C8 = value2
//   0x83  kind 6 only: +0x2B8 = value1, +0x2BC = value2
//   0x8F  +0x2D0 = value1, whatever the kind
//   0x100 0x00428EB0(value1, value2)
// Everything else falls through to the base at 0x0041B9B0, whose own byte table at
// 0x0041BAE4 covers 0x00 to 0xC1 and which then tests 0xC4, 0x8000, 0x8001, 0x8100,
// 0x7FFF0000 and 0x7FFFFFFF one at a time before answering 0xFFFF0001:
//   0x00        vtable+0x2C (0x0041B280 for a sprite) with (value1, value2)
//   0x01        0x0041B6D0: +0xA8 = value1
//   0x02        vtable+0x48 (0x00428450 for a sprite) with value1
//   0xC0        0x004932C0: +0x08 = value1
//   0xC1        0x0041AEB0: +0x10 = value1
//   0xC4        0x0041AEC0: +0x48 = value1
//   0x8000      0x0041BF80(value1, value2)
//   0x8001      0x0041BFC0(value1)
//   0x8100      0x0041BFE0(value1)
//   0x7FFF0000  0x0041C290(value1)
//   0x7FFFFFFF  0x0041C2B0(value1, value2), the one arm that can answer "wrong
//               arguments" (0xFFFF0002)
//
// The plain field writes are here. The arms that call a function this engine has not
// read, and the ones that write fields of a sprite's content (+0x2AC upwards) which
// nothing here has yet - a sprite cannot be given an image, so its kind at +0x134 is
// not modelled either - say so by name instead of being invented.
// ----------------------------------------------------------------------------------
static uint32_t Object_SetParameterBase(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread)
{
	(void)value2;

	switch(number)
	{
	case 0x01:                              // 0x0041B6D0
		object->unknownA8 = value1;
		return OBJECT_PARAM_OK;
	case 0xC0:                              // 0x004932C0
		object->propagateEnabled = (int)value1;
		return OBJECT_PARAM_OK;
	case 0xC1:                              // 0x0041AEB0
		object->propagateUnknown0C = (int)value1;
		return OBJECT_PARAM_OK;
	case 0xC4:                              // 0x0041AEC0
		object->priority = value1;
		return OBJECT_PARAM_OK;

	case 0x00:
		*unread = "the pair set through vtable+0x2C (0x0041B280)";
		return OBJECT_PARAM_UNREAD;
	case 0x02:
		*unread = "the value set through vtable+0x48 (0x00428450)";
		return OBJECT_PARAM_UNREAD;
	case 0x8000:
		*unread = "0x0041BF80";
		return OBJECT_PARAM_UNREAD;
	case 0x8001:
		*unread = "0x0041BFC0";
		return OBJECT_PARAM_UNREAD;
	case 0x8100:
		*unread = "0x0041BFE0";
		return OBJECT_PARAM_UNREAD;
	case 0x7FFF0000:
		*unread = "0x0041C290";
		return OBJECT_PARAM_UNREAD;
	case 0x7FFFFFFF:
		*unread = "0x0041C2B0";
		return OBJECT_PARAM_UNREAD;
	}

	return OBJECT_PARAM_UNSUPPORTED;
}

uint32_t Object_SetParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread)
{
	const char* ignored = NULL;
	if(unread == NULL)
		unread = &ignored;
	*unread = NULL;

	if(object == NULL)
		return OBJECT_PARAM_UNSUPPORTED;

	// The sprite's own arms, which only a sprite has: a group's vtable+0x5C is the
	// base itself (0x0041B9B0), so a group answers none of these. None can be
	// honoured until a sprite can hold an image anyway: they either call a function
	// that is still unread or write a field of the content its kind (+0x134) selects.
	switch(object->type == OBJECT_TYPE_SPRITE ? number : 0xFFFFFFFFu)
	{
	case 0x10:
		*unread = "0x004273C0";
		return OBJECT_PARAM_UNREAD;
	case 0x11:
		*unread = "0x0042AC30";
		return OBJECT_PARAM_UNREAD;
	case 0x40:
		*unread = "0x004280F0, on a sprite of kind 2, 5 or 6";
		return OBJECT_PARAM_UNREAD;
	case 0x41:
		*unread = "0x00428130, on a sprite of kind 2, 5 or 6";
		return OBJECT_PARAM_UNREAD;
	case 0x42:
		*unread = "0x00428200, on a sprite of kind 2, 5 or 6";
		return OBJECT_PARAM_UNREAD;
	case 0x43:
		*unread = "0x004281D0, on a sprite of kind 6";
		return OBJECT_PARAM_UNREAD;
	case 0x60:
		*unread = "0x00428610";
		return OBJECT_PARAM_UNREAD;
	case 0x80:
		*unread = "the content fields +0x2AC and +0x2B0";
		return OBJECT_PARAM_UNREAD;
	case 0x81:
		*unread = "the content fields +0x2B4/+0x2C0 and +0x2CC";
		return OBJECT_PARAM_UNREAD;
	case 0x82:
		*unread = "the content fields +0x2C4 and +0x2C8";
		return OBJECT_PARAM_UNREAD;
	case 0x83:
		*unread = "the content fields +0x2B8 and +0x2BC";
		return OBJECT_PARAM_UNREAD;
	case 0x8F:
		*unread = "the content field +0x2D0";
		return OBJECT_PARAM_UNREAD;
	case 0x100:
		*unread = "0x00428EB0";
		return OBJECT_PARAM_UNREAD;
	}

	return Object_SetParameterBase(object, number, value1, value2, unread);
}

uint32_t Object_ApplyParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread)
{
	if(object == NULL)
		return OBJECT_SET_BAD_HANDLE;

	// 0x00443990 brackets the write: it dirties the screen before the change when the
	// object would be drawn, sets the parameter, and dirties it again afterwards. In
	// between it also compares the object's draw-order key (vtable+0x1C, 0x0041B190)
	// from before and after and re-sorts the display list through 0x004433E0 when it
	// moved. There is no display list order here yet, and the key is built from the
	// object's rectangle, which nothing here can have until an object can hold an
	// image, so that comparison is left out rather than faked.
	if(Object_IsDrawable(object))
		gObjectDamage++;

	uint32_t result = Object_SetParameter(object, number, value1, value2, unread);

	if(result == OBJECT_PARAM_OK && Object_IsDrawable(object))
		gObjectDamage++;

	switch(result)
	{
	case OBJECT_PARAM_OK:           return OBJECT_SET_OK;
	case OBJECT_PARAM_BAD_ARGUMENT: return OBJECT_SET_BAD_ARGUMENT;
	case OBJECT_PARAM_UNREAD:       return OBJECT_SET_UNREAD;
	default:                        return OBJECT_SET_UNSUPPORTED;
	}
}
void Object_FreeAll(void)
{
	for(size_t i = 0; i < OBJECT_KIND_COUNT; i++)
	{
		for(uint32_t slot = 0; slot < gKinds[i].slotCount; slot++)
		{
			free(gKinds[i].slots[slot]);
			gKinds[i].slots[slot] = NULL;
		}
		*gKinds[i].count = 0;
	}
}