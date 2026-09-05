#ifndef __SPRITE_H__
#define __SPRITE_H__

#include <stdint.h>

// Sprite objects. The original keeps them in a fixed table on the display root at
// 0x0056674C: 512 slots at root+0x5C, the live count at root+0x85C and a serial
// counter at root+0x860. A handle is the slot index with 0x80 in its top byte.
#define SPRITE_SLOT_COUNT   0x200
#define SPRITE_HANDLE_TAG   0x80000000u
#define SPRITE_HANDLE_MASK  0x00FFFFFFu
#define SPRITE_OBJECT_TYPE  2

typedef struct Sprite Sprite_t;
struct Sprite
{
	uint32_t  serial;             // +0x20, the value of the counter before this one
	uint32_t  type;               // +0x18, 2 for a sprite
	int       enabled;            // +0x04
	int       propagateEnabled;   // +0x08
	int       flagUnknown0C;      // +0x0C
	int       propagateUnknown0C; // +0x10
	int       visible;            // +0x14
	uint32_t  unknownA8;          // +0xA8
	uint32_t  unknownAC;          // +0xAC
	uint32_t  unknownB0;          // +0xB0
	uint32_t  opacity;            // +0xB4
	Sprite_t* firstChild;         // +0x12C
	Sprite_t* nextSibling;
};

extern Sprite_t* gSprites[SPRITE_SLOT_COUNT];
extern uint32_t  gSpriteCount;
extern uint32_t  gSpriteSerial;
extern uint32_t  gSpriteDamage;

// Returns a handle, or 0 when all 512 slots are taken (which the opcode treats as
// fatal, as the original does).
uint32_t  Sprite_Create(void);
// NULL for anything that is not a live sprite handle: a wrong tag byte, an index of
// 0x200 or more, or a slot that has been freed.
Sprite_t* Sprite_Resolve(uint32_t handle);
int       Sprite_IsDrawable(const Sprite_t* sprite);
void      Sprite_SetVisible(Sprite_t* sprite, int visible);
// 1 when the handle resolved, 0 when it did not.
int       Sprite_SetVisibleByHandle(uint32_t handle, int visible);
void      Sprite_FreeAll(void);

#endif // __SPRITE_H__
