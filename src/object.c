#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "object.h"
#include "renderer.h"

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
// non-zero, +0x0C is zero, the transparency at +0xB0 is below 0x100 and +0xB4 is
// above 0. So a fresh
// sprite fails on exactly one condition, its visible flag, and every other test
// already passes - which is why showing a new sprite is what changes the answer.
// ----------------------------------------------------------------------------------

// One table per kind, each with the live count and the serial counter the original
// keeps beside it (a sprite's at root+0x85C and root+0x860, a group's at root+0xA34
// and root+0xA38).
static DisplayObject_t* gSprites[SPRITE_SLOT_COUNT] = { NULL };
static uint32_t gSpriteCount = 0;
static uint32_t gSpriteSerial = 0;
static DisplayObject_t* gWindows[WINDOW_SLOT_COUNT] = { NULL };
static uint32_t gWindowCount = 0;
static uint32_t gWindowSerial = 0;
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
	{ OBJECT_TAG_WINDOW, OBJECT_TYPE_WINDOW, WINDOW_SLOT_COUNT, gWindows, &gWindowCount, &gWindowSerial },
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

// What the base constructor (0x0041A4D0) leaves behind, whatever the kind: the type
// at +0x18 and the serial at +0x20 from its two arguments, then +0x04 = 1, +0x08 = 0,
// +0x0C = 0, +0x10 = 0, +0x14 = 0, +0x48 = 1, +0xA8 = 0x80, +0xAC = 0, +0xB0 = 0 and
// +0xB4 = 0x100. Everything below that is a field this engine keeps for content a
// sprite may be given later.
static void Object_ConstructBase(DisplayObject_t* object, uint32_t type, uint32_t serial)
{
	memset(object, 0, sizeof(DisplayObject_t));
	object->serial = serial;
	object->type = type;
	object->enabled = 1;
	object->priority = 1;
	object->unknownA8 = 0x80;
	object->opacity = 0x100;
	// And, on a sprite, what its own constructor (0x00425790) adds on top:
	// +0x244 = -1 and +0x134 = 0. A group's class (0x00420EB0) has neither
	// field, and its vtable+0x48 is the base's, so zero is right for it.
	object->contentKind = (type == OBJECT_TYPE_SPRITE) ? -1 : 0;
	// +0x150 = -1, as the sprite constructor leaves it. +0x158 is not written by any
	// constructor read so far; -1 here so that it can never accidentally equal a
	// real bitmap serial before something has actually given this sprite content.
	object->bitmapId = -1;
	object->bitmapSerial = 0xFFFFFFFFu;
	object->maskBitmapId = 0;
}

// ----------------------------------------------------------------------------------
// The display list
// ----------------------------------------------------------------------------------
typedef struct ObjectNode
{
	uint32_t           key;
	DisplayObject_t*   object;
	struct ObjectNode* next;
} ObjectNode_t;

static ObjectNode_t* gDisplayList = NULL;

uint32_t Object_DrawKey(const DisplayObject_t* object)
{
	uint32_t type = object->type;
	if(type >= 8)
		type = 7;

	// 0x0041B190 takes the depth out of the accumulated position vector's third
	// component, and nothing in this engine has a Z, so it is the constant the
	// original computes for a flat scene. The serial is used instead when the
	// object asks for it.
	uint32_t depth = object->depthFromSerial != 0 ? object->serial : 0xFFF;

	return (((object->layer * 8) + type) << 13) + object->orderBase + (depth & 0x1FFF);
}

void Object_ListInsert(DisplayObject_t* object)
{
	if(object == NULL)
		return;

	uint32_t key = Object_DrawKey(object);
	ObjectNode_t** link = &gDisplayList;
	// Before the first node whose key is strictly greater, so equal keys keep the
	// order they arrived in.
	while(*link != NULL && (*link)->key <= key)
		link = &(*link)->next;

	ObjectNode_t* node = (ObjectNode_t*)malloc(sizeof(ObjectNode_t));
	if(node == NULL)
		return;
	node->key = key;
	node->object = object;
	node->next = *link;
	*link = node;
}

void Object_ListRemove(DisplayObject_t* object)
{
	for(ObjectNode_t** link = &gDisplayList; *link != NULL; link = &(*link)->next)
	{
		if((*link)->object != object)
			continue;
		ObjectNode_t* node = *link;
		*link = node->next;
		free(node);
		return;
	}
}

void Object_ListResort(DisplayObject_t* object)
{
	Object_ListRemove(object);
	Object_ListInsert(object);
}

// root+0x50, built once with the display root and never freed while the engine runs.
static DisplayObject_t* gScreenObject = NULL;

static DisplayObject_t* Object_Screen(void)
{
	if(gScreenObject == NULL)
	{
		gScreenObject = (DisplayObject_t*)malloc(sizeof(DisplayObject_t));
		if(gScreenObject == NULL)
			return NULL;
		// 0x0041E960 passes the base the type 1 and no serial of its own: it is not
		// in any of the ten tables, so nothing hands it one.
		Object_ConstructBase(gScreenObject, OBJECT_TYPE_SCREEN, 0);
		gScreenObject->handle = OBJECT_HANDLE_SCREEN;
		// The display root puts it into the list the moment it has built it
		// (0x004429F9), before any other object exists.
		Object_ListInsert(gScreenObject);
	}

	return gScreenObject;
}

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

	Object_ConstructBase(object, kind->type, (*kind->serial)++);
	object->handle = index | kind->tag;

	kind->slots[index] = object;
	(*kind->count)++;
	// Every kind puts itself into the display list as it is built: the sprite table
	// at 0x0043E4F6, the windows at 0x00440737 and the groups at 0x00441A38 all end
	// in the same 0x004307D0.
	Object_ListInsert(object);

	return object->handle;
}
DisplayObject_t* Object_Resolve(uint32_t handle)
{
	// 0x00443350 answers the handle 0 with the screen object before it looks at a
	// single table, which is how a script moves or hides the whole screen at once.
	if(handle == OBJECT_HANDLE_SCREEN)
		return Object_Screen();

	const ObjectKind_t* kind = Object_KindForTag(handle & OBJECT_TAG_MASK);
	if(kind == NULL)
		return NULL;

	uint32_t index = handle & OBJECT_INDEX_MASK;
	if(index >= kind->slotCount)
		return NULL;

	return kind->slots[index];
}

void Object_Destroy(uint32_t handle)
{
	// The screen object belongs to the display root, not to a table; nothing that
	// destroys objects can reach it.
	if(handle == OBJECT_HANDLE_SCREEN)
		return;

	const ObjectKind_t* kind = Object_KindForTag(handle & OBJECT_TAG_MASK);
	if(kind == NULL)
		return;

	uint32_t index = handle & OBJECT_INDEX_MASK;
	if(index >= kind->slotCount || kind->slots[index] == NULL)
		return;

	Object_ListRemove(kind->slots[index]);
	free(kind->slots[index]);
	kind->slots[index] = NULL;
	if(*kind->count > 0)
		(*kind->count)--;
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
		&& object->hidden == 0
		&& object->transparency < 0x100
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
		object->propagateHidden = (int)value1;
		return OBJECT_PARAM_OK;
	case 0xC4:                              // 0x0041AEC0
		object->priority = value1;
		return OBJECT_PARAM_OK;
	case 0x7FFF0000:                        // 0x0041C290
		object->fieldBC = value1;
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
void Object_SetHidden(DisplayObject_t* object, int hidden)
{
	if(object == NULL)
		return;

	// 0x0041AE70, the same shape as the enabled setter one field along: it stores
	// the flag and walks the children only when +0x10 is set.
	object->hidden = hidden;
	if(object->propagateHidden == 0)
		return;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
		Object_SetHidden(child, hidden);
}

void Object_ApplyHidden(DisplayObject_t* object, int hidden)
{
	if(object == NULL)
		return;

	// 0x004434D0, the same bracket as the other two.
	int before = Object_IsDrawable(object);
	Object_SetHidden(object, hidden);
	int after = Object_IsDrawable(object);

	if(before != after)
		gObjectDamage++;
}

// ----------------------------------------------------------------------------------
// The effect level, the virtual at vtable+0x48 (Grp0 0x32)
//
// The base (0x0041B6F0) writes +0xAC and then calls each child's own vtable+0x48,
// so a child that overrides it is asked its own way. Three opcodes sit beside this
// one with the same shape and the same range error, reaching the object through
// 0x004620D0, 0x00462100 and 0x00462120 instead: Grp0 0x33, 0x34 and 0x35. They are
// not written until the boot asks for them.
// ----------------------------------------------------------------------------------
static const char* Object_SetEffectLevel(DisplayObject_t* object, uint32_t level);

// 0x0041B6F0, the base's own.
static const char* Object_SetEffectLevelBase(DisplayObject_t* object, uint32_t level)
{
	object->unknownAC = level;

	const char* unread = NULL;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
	{
		const char* childUnread = Object_SetEffectLevel(child, level);
		if(unread == NULL)
			unread = childUnread;
	}

	return unread;
}

// The virtual call itself: a sprite answers at 0x00428450 and dispatches on its
// content kind (+0x244); everything else is the base.
static const char* Object_SetEffectLevel(DisplayObject_t* object, uint32_t level)
{
	if(object == NULL)
		return NULL;

	if(object->type != OBJECT_TYPE_SPRITE)
		return Object_SetEffectLevelBase(object, level);

	switch(object->contentKind)
	{
	case 0:
	case 3:
		// Straight to the base.
		return Object_SetEffectLevelBase(object, level);
	case -1:
		// The base, and 0x00409F40 over the four values at +0x32C..+0x338 first,
		// but only for kind 4. A sprite with no content is kind 0, so this is the
		// arm a fresh sprite takes.
		if(object->kind == 4)
			return "0x00409F40, on a sprite of kind 4";
		return Object_SetEffectLevelBase(object, level);
	case 1:
		return "+0x240 and 0x00428F50/0x004290E0, on a sprite of content kind 1";
	case 2:
		return "+0x240 and 0x00428F50/0x004290E0, on a sprite of content kind 2";
	default:
		// Content kind 4 and up: the original falls off the end of the switch and
		// does nothing at all, not even the base. Faithfully nothing.
		return NULL;
	}
}

// 0x0041B730. Unlike the effect level's setter one field along, this one calls
// itself on every child rather than the child's own virtual: the original hard
// calls 0x0041B730, so a child that overrides the transparency does not get its
// override here.
static void Object_SetTransparency(DisplayObject_t* object, uint32_t transparency)
{
	object->transparency = transparency;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
		Object_SetTransparency(child, transparency);
}

// 0x0041B3A0: the two fields, and then the same virtual down every child, which is
// how a group moves everything under it.
static void Object_SetPosition(DisplayObject_t* object, int32_t x, int32_t y)
{
	object->x = x;
	object->y = y;
	for(DisplayObject_t* child = object->firstChild; child != NULL; child = child->nextSibling)
		Object_SetPosition(child, x, y);
}

void Object_ApplyPosition(DisplayObject_t* object, int32_t x, int32_t y)
{
	if(object == NULL)
		return;

	// 0x004437B0's bracket, the same one the content opcode uses: it asks once,
	// before the move, whether the object would be drawn, and dirties the screen
	// then and again afterwards on that same answer.
	int drawable = Object_IsDrawable(object);
	if(drawable)
		gObjectDamage++;

	Object_SetPosition(object, x, y);

	if(drawable)
		gObjectDamage++;
}

void Object_ApplyTransparency(DisplayObject_t* object, uint32_t transparency)
{
	if(object == NULL)
		return;

	// 0x00443690's bracket is 0x00443540's: drawn before OR after, not "it moved".
	// Here it can actually be one and not the other, since the transparency is one
	// of the things that decides whether the object is drawn at all.
	int before = Object_IsDrawable(object);
	Object_SetTransparency(object, transparency);
	int after = Object_IsDrawable(object);

	if(before || after)
		gObjectDamage++;
}

const char* Object_ApplyEffectLevel(DisplayObject_t* object, uint32_t level)
{
	if(object == NULL)
		return NULL;

	// 0x00443540's bracket, which is not the one the visible and enabled setters
	// use: it dirties the screen when the object would be drawn before OR after,
	// not when that answer moved. An effect level changes how a thing looks
	// without changing whether it is drawn, so "it moved" would never fire.
	int before = Object_IsDrawable(object);
	const char* unread = Object_SetEffectLevel(object, level);
	int after = Object_IsDrawable(object);

	if(before || after)
		gObjectDamage++;

	return unread;
}

// ----------------------------------------------------------------------------------
// Sprite content (0x004273C0)
// ----------------------------------------------------------------------------------

// vtable+0x74, 0x0041C090: size the object's own surface from its content, in the
// screen's pixel mode. A zero width or height is refused and nothing is written; the
// pixels at +0x90 are cleared to NULL, which is the original's own last act here.
int Object_SetSurfaceSize(Renderer_t* renderer, DisplayObject_t* object, int width, int height)
{
	if(width == 0 || height == 0)
		return 0;

	object->surfaceWidth      = width;
	object->surfaceHeight     = height;
	object->surfaceMode       = Renderer_ScreenMode(renderer);
	object->surfacePixelBytes = Renderer_ModePixelBytes(object->surfaceMode);
	object->surfaceStride     = object->surfacePixelBytes * width;
	object->surfacePixels     = NULL;
	return 1;
}

// 0x004274E0, the arm for a sprite of kind 0, which is what a fresh sprite is: its
// content is one whole bitmap.
static uint32_t Object_SetContentBitmapKind0(Renderer_t* renderer, DisplayObject_t* sprite, int number)
{
	// 0x00407F20 fills a six-dword descriptor - pixels, stride, width, height, mode
	// and bytes per pixel - out of the bitmap table entry, and answers 0 when the
	// slot is empty. That descriptor is this engine's Bitmap_t, field for field.
	Bitmap_t* bitmap = Renderer_ResolveBitmap(renderer, number);
	if(bitmap == NULL)
		return OBJECT_CONTENT_BAD_BITMAP;

	// The five teardown calls (0x00429090, 0x004291D0, 0x00429220, 0x00429240 and
	// 0x00429290) each free one buffer of the content the sprite used to hold and
	// clear the fields beside it (+0x220, +0x30C, +0x330, +0x340 and +0x2E4). No
	// content this engine can build owns any of those, so there is nothing to free.

	sprite->kind         = 0;
	sprite->bitmapId     = number;
	sprite->bitmapSerial = Renderer_BitmapSerial(renderer, number);
	sprite->contentKind  = -1;
	Object_SetSurfaceSize(renderer, sprite, bitmap->width, bitmap->height);
	return OBJECT_CONTENT_OK;
}

uint32_t Object_SetContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread)
{
	const char* ignored = NULL;
	if(unread == NULL)
		unread = &ignored;
	*unread = NULL;

	if(sprite == NULL)
		return OBJECT_CONTENT_BAD_SPRITE;

	switch(sprite->kind)
	{
	case 0:
		return Object_SetContentBitmapKind0(renderer, sprite, number);
	case 2:
		*unread = "0x00427680, the content of a sprite of kind 2";
		return OBJECT_CONTENT_UNREAD;
	case 5:
		*unread = "0x00427B70, the content of a sprite of kind 5";
		return OBJECT_CONTENT_UNREAD;
	case 6:
		*unread = "0x00427E60, the content of a sprite of kind 6";
		return OBJECT_CONTENT_UNREAD;
	}

	// Kinds 1, 3 and 4 share the jump table's default arm (0x004274B3), and so does
	// anything above 6, which does not reach the table at all: it returns 0 and does
	// nothing whatever. Faithfully nothing.
	return OBJECT_CONTENT_OK;
}

uint32_t Object_ApplyContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread)
{
	if(sprite == NULL)
		return OBJECT_CONTENT_BAD_SPRITE;

	// 0x0043ED80's bracket, which is neither of the two the other display-object
	// opcodes use: it asks once, before the change, whether the sprite would be
	// drawn, and dirties the screen then; afterwards it dirties it a second time on
	// that same answer, and only when the content was actually set.
	int drawable = Object_IsDrawable(sprite);
	if(drawable)
		gObjectDamage++;

	uint32_t result = Object_SetContentBitmap(renderer, sprite, number, unread);

	if(result == OBJECT_CONTENT_OK && drawable)
		gObjectDamage++;

	return result;
}

// ----------------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------------
// vtable+0x20 (0x0041B200): where the object's own surface starts and ends, in its
// own coordinates. The surface is what vtable+0x74 sized from the object's content;
// an object that has none is empty and nothing of it is drawn.
static void Object_LocalBounds(const DisplayObject_t* object, Rect_t* rect)
{
	rect->left   = 0;
	rect->top    = 0;
	rect->right  = object->surfaceWidth - 1;
	rect->bottom = object->surfaceHeight - 1;
}

// vtable+0x34 (0x0041B330): where the object sits on the screen, which the original
// accumulates through the parents. Every position this engine sets is written down
// the children by 0x0041B3A0 already, so an object's own +0x38 / +0x3C is that sum.
static void Object_ScreenPosition(const DisplayObject_t* object, int32_t* x, int32_t* y)
{
	*x = object->x;
	*y = object->y;
}

// vtable+0x24: the object's surface in screen coordinates. The base (0x0041C450)
// hands straight through to vtable+0x20 without moving it, which is why the screen
// object is not affected by a position; every other kind uses 0x0041B230, which adds
// the position on.
static void Object_ScreenBounds(const DisplayObject_t* object, Rect_t* rect)
{
	Object_LocalBounds(object, rect);
	if(object->type == OBJECT_TYPE_SCREEN)
		return;

	int32_t x, y;
	Object_ScreenPosition(object, &x, &y);
	Renderer_OffsetRect(rect, x, y);
}

// 0x0041B840: the transparency the blit is given, which the blend mode decides.
// Three arms are reachable from the table at 0x0041B8B8: the modes that carry a
// transparency of their own combine all three fields, 0x02 and its family weight
// them the other way round, and everything else - 0x00, 0x80, 0x40 and any mode
// below 1 - is just +0xAC.
static uint32_t Object_DrawTransparency(const DisplayObject_t* object)
{
	switch(object->unknownA8)
	{
		case BITMAP_BLEND_ALPHA_TRANS:
		case BITMAP_BLEND_ALPHA_TRANS2:
		case 0x21:
		{
			uint32_t weight = (0x100 - object->transparency)
			                * (0x100 - object->unknownAC)
			                * object->opacity;
			return 0x100 - (weight >> 16);
		}
		case 0x02:
		case 0xC0:
			return ((0x100 - object->transparency) * object->opacity * object->unknownAC) >> 16;
		default:
			return object->unknownAC;
	}
}

// vtable+0x18. The base's (0x0041C410) fills its area with zero; a group's is
// 0x0041BF50, which does nothing at all; a sprite's is 0x004259A0 and a window's is
// 0x0042B1D0.
//
// 0x00565B44, the flag a window's draw tests before it draws anything, is set by
// 0x00440650 - which the display root's own constructor calls with 0, so windows are
// invisible until the opcode behind 0x00462AA0 turns them on. That opcode is not
// wired up yet, so the flag stays where the constructor leaves it.
uint32_t gWindowsVisible = 0;
uint32_t gWindowTransparency = 0;

static void Object_Draw(Renderer_t* renderer, DisplayObject_t* object,
                        Bitmap_t* target, const Rect_t* rect)
{
	uint32_t transparency = Object_DrawTransparency(object);

	switch(object->type)
	{
		case OBJECT_TYPE_GROUP:
			// 0x0041BF50: a group has no pixels of its own.
			return;

		case OBJECT_TYPE_SPRITE:
		{
			// 0x004259A0. The sprite dispatches on its kind (+0x134) through the
			// table at 0x00427000; a sprite that has only been given a bitmap is
			// kind 0, the arm at 0x00425A7B.
			if(object->kind != 0)
			{
				printf("[Renderer]: Warning: the draw of a sprite of kind %u"
				       " (0x004259A0 arm %u) is not written yet\n",
				       object->kind, object->kind);
				return;
			}
			Bitmap_t* source = Renderer_ResolveBitmap(renderer, object->bitmapId);
			if(source == NULL)
				return;
			// The slot may have been refilled behind the sprite's back since it was
			// handed over, and then it is not this sprite's image any more.
			if(Renderer_BitmapSerial(renderer, object->bitmapId) != object->bitmapSerial)
				return;
			if(object->maskBitmapId != 0)
			{
				printf("[Renderer]: Warning: a sprite with a second bitmap draws"
				       " through 0x00425ADC / 0x00425BB8, which is not written yet\n");
				return;
			}
			// 0x0042AC80 asks whether any of the four transform blocks at +0x358 is
			// set; nothing in this engine sets one, so the draw is the plain one at
			// 0x00425D0D.
			Bitmap_t view = *source;
			if(!Renderer_ClipBitmap(&view, rect))
				return;
			Renderer_BlitView(target, &view, (int)object->unknownA8, (int)transparency);
			return;
		}

		case OBJECT_TYPE_WINDOW:
		{
			// 0x0042B1D0.
			if(gWindowsVisible == 0)
				return;
			Screen_t* screen = Renderer_ResolveScreen(renderer, object->handle);
			if(screen == NULL || screen->bitmap == NULL)
				return;
			Bitmap_t view;
			view.width  = screen->width;
			view.height = screen->height;
			view.mode   = Renderer_ScreenMode(renderer);
			view.stride = screen->width * Renderer_ModePixelBytes(view.mode);
			view.bitmap = screen->bitmap;
			view.serial = 0xFFFFFFFFu;
			view.offsetX = 0;
			view.offsetY = 0;
			if(!Renderer_ClipBitmap(&view, rect))
				return;
			// The window's own transparency and the global one, folded together the
			// way 0x0042B23F folds them.
			uint32_t combined = 0x100 - (((0x100 - transparency)
			                            * (0x100 - gWindowTransparency)) >> 8);
			Renderer_BlitView(target, &view, (int)object->unknownA8, (int)combined);
			return;
		}

		default:
			// 0x0041C410, the base. It offers vtable+0x84 first when +0x138 is set;
			// nothing sets it, so what is left is the fill.
			Renderer_ClearBitmap(target);
			return;
	}
}

// 0x0041B0A0: one object onto the target, if any of it is inside both the target and
// the part being redrawn. The answer is whether the object covered the whole of it.
static int Object_DrawTo(Renderer_t* renderer, DisplayObject_t* object,
                         Bitmap_t* target, const Rect_t* targetBounds, const Rect_t* clip)
{
	if(!Object_IsDrawable(object))
		return 1;

	Rect_t bounds;
	Object_ScreenBounds(object, &bounds);
	if(!Renderer_RectIntersect(&bounds, targetBounds))
		return 0;

	int covered = Renderer_RectContains(clip, &bounds);
	if(!Renderer_RectIntersect(&bounds, clip))
		return covered;

	// The destination is the target narrowed to what is being drawn, in screen
	// coordinates; the object then works in its own, so the rectangle goes with it.
	Bitmap_t view = *target;
	if(!Renderer_ClipBitmap(&view, &bounds))
		return covered;

	int32_t x = 0, y = 0;
	if(object->type != OBJECT_TYPE_SCREEN)
		Object_ScreenPosition(object, &x, &y);
	Rect_t local = bounds;
	Renderer_OffsetRect(&local, -x, -y);

	Object_Draw(renderer, object, &view, &local);
	return covered;
}

void Object_DrawList(Renderer_t* renderer, Bitmap_t* target, const Rect_t* clip)
{
	Rect_t bounds = { 0, 0, target->width - 1, target->height - 1 };

	for(ObjectNode_t* node = gDisplayList; node != NULL; node = node->next)
	{
		// The draw priority (Grp0 0x09, which reaches the list's own +0x48 through
		// 0x00462020) holds back everything below it that is not the screen itself;
		// the original then draws those from the second list at list+0x20, which
		// nothing builds yet - list+0x24, the flag that would enable it, is never
		// set, and 0x00431530 returns on that flag before it looks at anything.
		if(gDrawPriority > node->key && node->object->type != 0)
			continue;

		Object_DrawTo(renderer, node->object, target, &bounds, clip);
	}
}

void Object_FreeAll(void)
{
	while(gDisplayList != NULL)
	{
		ObjectNode_t* node = gDisplayList;
		gDisplayList = node->next;
		free(node);
	}
	free(gScreenObject);
	gScreenObject = NULL;

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