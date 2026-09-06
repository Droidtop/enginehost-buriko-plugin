#ifndef __ICON_H__
#define __ICON_H__

#include <stdint.h>

#include "object.h"
#include "thread.h"

// ----------------------------------------------------------------------------------
// Icons: DCIPIcon and DCIPIconEx (the names are the executable's own, out of the RTTI
// at 0x004F2BBC and 0x004F2C08).
//
// This is a second object family, nothing to do with the display objects' ten handle
// tables. 0x0046C7B0 is its factory: it resolves a WINDOW handle, allocates 0xA4
// bytes for kind 0 or 0xD8 for kind 1, constructs it - 0x00447A70 for the base,
// 0x0044A8A0 for the Ex, which calls the base first - and registers it with
// 0x0046C620 on the list at 0x005667E8, counting at 0x005667E0. An icon's handle is
// its own serial (0x00490C70 reads +0x04, which 0x004477B0 filled from the counter at
// 0x00565D74), so handles are never reused and 0 is never one.
//
// What the base constructor really builds is the interesting part: the icon holds the
// window it was made from (+0x28) and a display object of its own (+0x2C) - a
// CDspObjVirtual of type 8 - which it positions where the window is, gives the
// window's draw priority, and then attaches to the window as a child at 0, 0. The
// icon is therefore a place inside a window that something can later be drawn at.
// ----------------------------------------------------------------------------------
#define ICON_KIND_PLAIN 0
#define ICON_KIND_EX    1

typedef struct Icon Icon_t;
struct Icon
{
	uint32_t         handle;   // +0x04, the serial from 0x00565D74
	// +0x24: 0 for a DCIPIcon and 1 for a DCIPIconEx, which is the only field that
	// tells the two apart until an opcode reaches one of the Ex's own.
	uint32_t         kind;
	DisplayObject_t* window;   // +0x0C and +0x28, both the window it was made from
	DisplayObject_t* object;   // +0x2C, the CDspObjVirtual inside that window
	Icon_t*          next;     // the +0x08 link of the 12-byte registration node
};
// The rest of what 0x00447A70 and 0x0044A8A0 initialise is not carried here, because
// nothing reads it yet and a field nobody reads is a field nobody maintains: the base
// zeroes +0x14 through +0x20, +0x30 through +0x5C and +0x94 through +0xA0, sets +0x3C,
// +0x74 and +0x90 to -1 and +0x88 to 1, and the Ex zeroes +0xA4 through +0xD4. The
// opcode that first reads one of them should add it here with its offset, as the
// display object's own fields are named.

// ----------------------------------------------------------------------------------
// An icon's content descriptor (0x0046CB50)
//
// The script does not hand an Ex icon its content a field at a time: it builds a tree
// in its own memory and hands over the address of its root, and the engine deep-copies
// that tree into its own before it uses any of it. The three levels are:
//
//   the root, 0x28 bytes: the entry count at +0x00 and the script address of the
//     entry array at +0x04, then eight more dwords that are carried along
//   an entry, 0x40 bytes: the part count in the low half of +0x00 and the script
//     address of the part array at +0x08, then thirteen more dwords
//   a part, 0xC4 bytes, copied whole
//
// Both counts must be between 1 and 0x100 (0x0046CB9A / 0x0046CC23); the original
// answers 2 when the root's count or its array is bad and 3 when an entry's is, and
// in the second case it fills the rest of the copy with nothing rather than stopping
// - the failure is only reported once the whole tree has been walked, and then the
// copy is freed and nothing is returned. What the fields inside an entry and a part
// mean is 0x0044A9E0's business and is not decided here: the copy is faithful to the
// byte, so naming them can wait for the code that reads them.
// ----------------------------------------------------------------------------------
#define ICON_CONTENT_ROOT_WORDS  10   // 0x28
#define ICON_CONTENT_ENTRY_WORDS 16   // 0x40
#define ICON_CONTENT_PART_WORDS  49   // 0xC4
#define ICON_CONTENT_MAX_COUNT   0x100

typedef struct
{
	uint32_t  raw[ICON_CONTENT_ENTRY_WORDS];
	// The part array the original writes back over the entry's own +0x08, replacing
	// the script address it copied there. Kept beside the raw words instead, so the
	// copy of the script's own bytes stays a copy.
	uint32_t  partCount;
	uint32_t* parts;   // partCount * ICON_CONTENT_PART_WORDS words
} IconContentEntry_t;

typedef struct
{
	uint32_t            raw[ICON_CONTENT_ROOT_WORDS];
	uint32_t            entryCount;
	IconContentEntry_t* entries;
} IconContent_t;

// 0x0046CB50. Copies the tree at the resolved address `root` into freshly allocated
// memory. 0 and *out set on success; 2 or 3 and *out NULL on the two failures above.
uint32_t Icon_ReadContent(Thread_t* thread, const uint32_t* root, IconContent_t** out);
// 0x0046CB00. Frees what Icon_ReadContent built, parts first.
void Icon_FreeContent(IconContent_t* content);

// 0x0046C7B0. Returns the icon's handle, or 0 when the window handle is not a window
// or the kind is neither 0 nor 1.
uint32_t Icon_Create(uint32_t windowHandle, uint32_t kind);
// 0x0046C650: the icon a handle names, or NULL.
Icon_t* Icon_Resolve(uint32_t handle);
// 0x0046C610: how many icons exist, which the frame loop asks twice a frame.
uint32_t Icon_Count(void);
// 0x0046C670: take one out of the list and free it.
void Icon_Destroy(uint32_t handle);
void Icon_FreeAll(void);

#endif // __ICON_H__
