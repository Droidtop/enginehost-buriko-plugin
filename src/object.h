#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <stdint.h>

// Display objects. The original keeps a table per kind on the display root at
// 0x0056674C and tells the kinds apart by the top byte of the handle; the index is
// the rest of it. Its resolver (0x00443350) walks all ten in this order:
//
//   tag   slots  table       resolver    what it is
//   0x80  0x200  root+0x05C  0x0043E6B0  a sprite      (built here)
//   0x90  0x008  root+0x864  0x0043F130
//   0x91  0x008  root+0x88C  0x0043F430
//   0xA0  0x008  root+0x8B4  0x0043F990
//   0xA1  0x004  root+0x8DC  0x0043FE10
//   0xB0  0x010  root+0x8F4  0x004407A0  a window, which the renderer holds instead
//   0xC0  0x008  root+0x93C  0x00441320
//   0xC1  0x008  root+0x964  0x00441AA0
//   0xF0  0x020  root+0x98C  0x00442300
//   0xF1  0x008  root+0xA14  0x004426F0  a group       (built here)
//
// Every kind is the same base class (0x0041A4D0), which is why the enable, visible
// and parameter opcodes take any of them; what differs is the class the constructor
// puts on top, and the type it passes the base: 2 for a sprite, 9 for a group.
#define OBJECT_TAG_MASK     0xFF000000u
#define OBJECT_INDEX_MASK   0x00FFFFFFu

#define OBJECT_TAG_SPRITE   0x80000000u
#define OBJECT_TAG_GROUP    0xF1000000u
#define SPRITE_SLOT_COUNT   0x200
#define GROUP_SLOT_COUNT    0x008
#define OBJECT_TYPE_SPRITE  2
#define OBJECT_TYPE_GROUP   9

typedef struct DisplayObject DisplayObject_t;
struct DisplayObject
{
	uint32_t  serial;             // +0x20, the value of the counter before this one
	uint32_t  type;               // +0x18, 2 for a sprite
	int       enabled;            // +0x04
	int       propagateEnabled;   // +0x08
	int       hidden;      // +0x0C
	int       propagateHidden; // +0x10
	int       visible;            // +0x14
	uint32_t  priority;           // +0x48
	uint32_t  unknownA8;          // +0xA8
	uint32_t  unknownAC;          // +0xAC
	uint32_t  unknownB0;          // +0xB0
	uint32_t  opacity;            // +0xB4
	DisplayObject_t* firstChild;         // +0x12C
	DisplayObject_t* nextSibling;
};

// The priority the screen object draws at, +0x48 of the object at root+0x14,
// held as the original holds it: the priority shifted up sixteen places, so the
// low half orders what shares a priority. Grp0 0x09 sets it.
#define SPRITE_MAX_PRIORITY 0x10000
extern uint32_t gDrawPriority;

extern uint32_t gObjectDamage;

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

uint32_t Object_SetParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// The worker behind Grp0 0x38 (0x00443990), the parameter write with its own
// bracket. Its results are the original's: 0 done, 5 unsupported number, 0xFE wrong
// arguments, and 0xFD for a number this engine has not read yet (`*unread` names
// it). The opcode answers 0xFF for a handle that did not resolve at all.
#define OBJECT_SET_OK            0x00u
#define OBJECT_SET_UNSUPPORTED   0x05u
#define OBJECT_SET_BAD_ARGUMENT  0xFEu
#define OBJECT_SET_BAD_HANDLE    0xFFu
#define OBJECT_SET_UNREAD        0xFDu
uint32_t Object_ApplyParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// A handle of the given kind, or 0 when every slot of it is taken (which the
// opcodes treat as fatal, as the original does).
uint32_t Object_Create(uint32_t tag);
// NULL for anything that is not a live object handle: a tag byte of no kind that is
// built, an index past that kind's table, or a slot that has been freed.
DisplayObject_t* Object_Resolve(uint32_t handle);
// The same, but only if the object is of that type.
DisplayObject_t* Object_ResolveKind(uint32_t handle, uint32_t type);
int  Object_IsDrawable(const DisplayObject_t* object);
// The flag and the walk down the children, as the original's setters do it.
void Object_SetVisible(DisplayObject_t* object, int visible);
void Object_SetEnabled(DisplayObject_t* object, int enabled);
void Object_SetHidden(DisplayObject_t* object, int hidden);
// The same, bracketed the way the opcodes' workers bracket them: would it be drawn,
// change it, would it be drawn now, and dirty the screen only when that answer moved.
void Object_ApplyVisible(DisplayObject_t* object, int visible);
void Object_ApplyEnabled(DisplayObject_t* object, int enabled);
void Object_ApplyHidden(DisplayObject_t* object, int hidden);
void Object_FreeAll(void);

#endif // __OBJECT_H__
