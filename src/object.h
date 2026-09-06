#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <stdint.h>

typedef struct Renderer Renderer_t;

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
//   0xB0  0x010  root+0x8F4  0x004407A0  a window   (built here; the renderer
//                                        holds its pixels, 0x0042AF00, type 3)
//   0xC0  0x008  root+0x93C  0x00441320
//   0xC1  0x008  root+0x964  0x00441AA0
//   0xF0  0x020  root+0x98C  0x00442300
//   0xF1  0x008  root+0xA14  0x004426F0  a group       (built here)
//
// Every kind is the same base class (0x0041A4D0), which is why the enable, visible
// and parameter opcodes take any of them; what differs is the class the constructor
// puts on top, and the type it passes the base: 2 for a sprite, 3 for a window,
// 9 for a group.
#define OBJECT_TAG_MASK     0xFF000000u
#define OBJECT_INDEX_MASK   0x00FFFFFFu

#define OBJECT_TAG_SPRITE   0x80000000u
#define OBJECT_TAG_WINDOW   0xB0000000u
#define OBJECT_TAG_GROUP    0xF1000000u
#define SPRITE_SLOT_COUNT   0x200
#define WINDOW_SLOT_COUNT   0x010
#define GROUP_SLOT_COUNT    0x008
// The screen object, which the display root builds at root+0x50 (0x0041E960, vtable
// 0x004E4854) and immediately puts at the head of its display list through
// 0x004307D0. It is a plain display object with the type 1 and no class of its own,
// and it is what the handle 0 names: 0x00443350 answers 0 with root+0x50 before it
// looks at any table.
#define OBJECT_HANDLE_SCREEN 0u
#define OBJECT_TYPE_SCREEN  1
#define OBJECT_TYPE_SPRITE  2
#define OBJECT_TYPE_WINDOW  3
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
	// +0x38 and +0x3C, the object's position, which vtable+0x38 (0x0041B3A0) writes
	// and then writes down every child through the child's own virtual.
	int32_t   x;
	int32_t   y;
	uint32_t  priority;           // +0x48
	uint32_t  unknownA8;          // +0xA8
	uint32_t  unknownAC;          // +0xAC
	// +0xB0, 0 opaque and 0x100 completely transparent: Grp0 0x34 sets it and
	// "would this be drawn" refuses the object once it reaches 0x100.
	uint32_t  transparency;
	uint32_t  opacity;            // +0xB4
	// +0xBC, which the parameter 0x7FFF0000 writes (0x0041C290) and 0x0041C2A0 reads
	// back. What consumes it is not read yet; scrdrv sets it on the screen object.
	uint32_t  fieldBC;
	// A sprite's own two, which its vtable+0x48 dispatches on. The sprite
	// constructor (0x00425790) leaves the content kind at -1 and the kind at 0,
	// and nothing here can give a sprite content yet, so they stay that way -
	// but the dispatch is written out, so the day content exists it is already
	// the original's dispatch and not a special case.
	int32_t   contentKind;        // +0x244
	uint32_t  kind;               // +0x134
	// A sprite's content, as the kind 0 arm (0x004274E0) leaves it: the bitmap
	// itself and the serial that bitmap slot had when it was handed over, so a slot
	// refilled behind the sprite's back can be told apart from the image it was
	// given. The constructor (0x00425790) leaves +0x150 at -1.
	int32_t   bitmapId;           // +0x150
	uint32_t  bitmapSerial;       // +0x158
	// The object's own surface, which vtable+0x74 (0x0041C090) sizes from whatever
	// content it was just given, in the screen's pixel mode. The original leaves the
	// pixels at +0x90 NULL until something draws into them, and so does this.
	uint8_t*  surfacePixels;      // +0x90
	int       surfaceStride;      // +0x94
	int       surfaceWidth;       // +0x98
	int       surfaceHeight;      // +0x9C
	int       surfaceMode;        // +0xA0
	int       surfacePixelBytes;  // +0xA4
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
// Free the object a handle names and give its slot back, so the next object of
// that kind takes it - which is what the original does and a counter cannot.
void Object_Destroy(uint32_t handle);
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

// The effect level (Grp0 0x32, the virtual at vtable+0x48). The original refuses
// anything above 0x100 before it even looks at the handle, in 0x00497F40.
#define OBJECT_EFFECT_LEVEL_MAX 0x100u
// The worker behind the opcode (0x00443540). NULL when the level was set;
// otherwise the name of the arm of the sprite's override this engine has not
// read yet, so it is refused by name rather than guessed at.
const char* Object_ApplyEffectLevel(DisplayObject_t* object, uint32_t level);
// The transparency (Grp0 0x34, 0x00443690), which takes the same range.
void Object_ApplyTransparency(DisplayObject_t* object, uint32_t transparency);
// The position (Grp0 0x37, the virtual at vtable+0x38), bracketed as 0x004437B0
// brackets it.
void Object_ApplyPosition(DisplayObject_t* object, int32_t x, int32_t y);
// ----------------------------------------------------------------------------------
// Sprite content: the virtual-free function 0x004273C0, which is both the sprite
// parameter 0x10 and the worker behind Grp0 0x57. It dispatches on the sprite's kind
// (+0x134) through the jump table at 0x004274BC, seven arms wide.
// ----------------------------------------------------------------------------------
// The original's own two results, and one of this engine's.
#define OBJECT_CONTENT_OK           0x00000000u
// 0x80000001: there is no such bitmap. The opcode turns it into result 1.
#define OBJECT_CONTENT_BAD_BITMAP   0x80000001u
// Not the original's: a kind whose arm this engine has not read out of the binary
// yet, refused by name in `*unread` rather than guessed at.
#define OBJECT_CONTENT_UNREAD       0x80FF0000u
uint32_t Object_SetContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread);
// The same, bracketed as 0x0043ED80 brackets it. 0xFF for a handle that is not a
// live sprite, which the opcode treats as fatal.
#define OBJECT_CONTENT_BAD_SPRITE   0x000000FFu
uint32_t Object_ApplyContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread);

void Object_FreeAll(void);

#endif // __OBJECT_H__
