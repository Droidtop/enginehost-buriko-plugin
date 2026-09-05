#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sprite.h"

// ----------------------------------------------------------------------------------
// Sprite objects
//
// The engine used to hand out 0x80000000, 0x80000001, ... from a counter with nothing
// behind them, which is wrong twice over: nothing can be asked about a sprite, and the
// original reuses freed indices instead of counting upwards.
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

Sprite_t* gSprites[SPRITE_SLOT_COUNT] = { NULL };
uint32_t  gSpriteCount = 0;
uint32_t  gSpriteSerial = 0;

// The original unions the sprite's rectangle into the global dirty region at
// 0x00565B2C (vtable +0x0C, 0x0041AF30, through the rectangle accessors at vtable
// +0x24 and +0x1C). A sprite here has no rectangle yet, because nothing can give it
// an image, so the damage is only counted for now - the count is what a renderer
// would consume, and it is not a made-up answer to anybody's question.
uint32_t  gSpriteDamage = 0;

uint32_t Sprite_Create(void)
{
	if(gSpriteCount >= SPRITE_SLOT_COUNT)
		return 0;

	// The first free slot, not the next index: freed slots are reused.
	uint32_t index = 0;
	while(index < SPRITE_SLOT_COUNT && gSprites[index] != NULL)
		index++;
	if(index >= SPRITE_SLOT_COUNT)
		return 0;

	Sprite_t* sprite = (Sprite_t*)malloc(sizeof(Sprite_t));
	if(sprite == NULL)
		return 0;

	memset(sprite, 0, sizeof(Sprite_t));
	sprite->serial = gSpriteSerial++;
	sprite->type = SPRITE_OBJECT_TYPE;
	sprite->enabled = 1;
	sprite->unknownA8 = 0x80;
	sprite->opacity = 0x100;

	gSprites[index] = sprite;
	gSpriteCount++;

	return index | SPRITE_HANDLE_TAG;
}

Sprite_t* Sprite_Resolve(uint32_t handle)
{
	if((handle & 0xFF000000u) != SPRITE_HANDLE_TAG)
		return NULL;

	uint32_t index = handle & SPRITE_HANDLE_MASK;
	if(index >= SPRITE_SLOT_COUNT)
		return NULL;

	return gSprites[index];
}

int Sprite_IsDrawable(const Sprite_t* sprite)
{
	if(sprite == NULL)
		return 0;

	return sprite->visible != 0
		&& sprite->enabled != 0
		&& sprite->flagUnknown0C == 0
		&& sprite->unknownB0 < 0x100
		&& sprite->opacity > 0;
}

void Sprite_SetVisible(Sprite_t* sprite, int visible)
{
	if(sprite == NULL)
		return;

	// 0x0041AED0 stores the flag and passes the same call down every child, so a
	// hidden parent hides its whole subtree. Nothing builds children yet; the walk is
	// here because it is the operation, not because it has anything to do today.
	sprite->visible = visible;
	for(Sprite_t* child = sprite->firstChild; child != NULL; child = child->nextSibling)
		Sprite_SetVisible(child, visible);
}

int Sprite_SetVisibleByHandle(uint32_t handle, int visible)
{
	Sprite_t* sprite = Sprite_Resolve(handle);
	if(sprite == NULL)
		return 0;

	// 0x0043EF50 asks whether the sprite would be drawn, sets the flag, asks again,
	// and dirties the screen only when the answer changed - not merely when the flag
	// did, which are different things once opacity or a parent is involved.
	int before = Sprite_IsDrawable(sprite);
	Sprite_SetVisible(sprite, visible);
	int after = Sprite_IsDrawable(sprite);

	if(before != after)
		gSpriteDamage++;

	return 1;
}

void Sprite_FreeAll(void)
{
	for(uint32_t i = 0; i < SPRITE_SLOT_COUNT; i++)
	{
		free(gSprites[i]);
		gSprites[i] = NULL;
	}
	gSpriteCount = 0;
}
