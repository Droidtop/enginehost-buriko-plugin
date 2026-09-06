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
	uint32_t  priority;           // +0x48
	uint32_t  unknownA8;          // +0xA8
	uint32_t  unknownAC;          // +0xAC
	uint32_t  unknownB0;          // +0xB0
	uint32_t  opacity;            // +0xB4
	Sprite_t* firstChild;         // +0x12C
	Sprite_t* nextSibling;
};

// The priority the screen object draws at, +0x48 of the object at root+0x14,
// held as the original holds it: the priority shifted up sixteen places, so the
// low half orders what shares a priority. Grp0 0x09 sets it.
#define SPRITE_MAX_PRIORITY 0x10000
extern uint32_t gDrawPriority;

extern Sprite_t* gSprites[SPRITE_SLOT_COUNT];
extern uint32_t  gSpriteCount;
extern uint32_t  gSpriteSerial;
extern uint32_t  gSpriteDamage;

// ----------------------------------------------------------------------------------
// Display object parameters: the virtual at vtable+0x5C, which Grp0 0x38 calls.
//
// A sprite answers it at 0x00428670 and hands everything it does not know to the
// display-object base at 0x0041B9B0. The three codes below are the base's own; the
// caller (0x00443990) turns them into the opcode's result.
// ----------------------------------------------------------------------------------
#define OBJECT_PARAM_OK            0x00000000u
#define OBJECT_PARAM_UNSUPPORTED   0xFFFF0001u
#define OBJECT_PARAM_BAD_ARGUMENT  0xFFFF0002u
// Not one of the original's codes. It means a parameter the original implements and
// this engine has not read out of the binary yet, so it is refused by name rather
// than guessed at; `*unread` then names it.
#define OBJECT_PARAM_UNREAD        0xFFFF00FFu

uint32_t Sprite_SetParameter(Sprite_t* sprite, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// The worker behind Grp0 0x38 (0x00443990). Its results are the original's:
// 0 done, 5 unsupported number, 0xFE wrong arguments, 0xFF invalid handle, and
// 0xFD for a number this engine has not read yet (`*unread` names it).
#define OBJECT_SET_OK            0x00u
#define OBJECT_SET_UNSUPPORTED   0x05u
#define OBJECT_SET_BAD_ARGUMENT  0xFEu
#define OBJECT_SET_BAD_HANDLE    0xFFu
#define OBJECT_SET_UNREAD        0xFDu
uint32_t Sprite_SetParameterByHandle(uint32_t handle, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// Returns a handle, or 0 when all 512 slots are taken (which the opcode treats as
// fatal, as the original does).
uint32_t  Sprite_Create(void);
// NULL for anything that is not a live sprite handle: a wrong tag byte, an index of
// 0x200 or more, or a slot that has been freed.
Sprite_t* Sprite_Resolve(uint32_t handle);
int       Sprite_IsDrawable(const Sprite_t* sprite);
void      Sprite_SetVisible(Sprite_t* sprite, int visible);
void      Sprite_SetEnabled(Sprite_t* sprite, int enabled);
// 1 when the handle resolved, 0 when it did not.
int       Sprite_SetVisibleByHandle(uint32_t handle, int visible);
int       Sprite_SetEnabledByHandle(uint32_t handle, int enabled);
void      Sprite_FreeAll(void);

#endif // __SPRITE_H__
