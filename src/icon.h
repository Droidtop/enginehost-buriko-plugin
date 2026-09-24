#ifndef __ICON_H__
#define __ICON_H__

#include <stdint.h>

#include "object.h"
#include "renderer.h"
#include "thread.h"

// ----------------------------------------------------------------------------------
// Icons: DCIPIcon and DCIPIconEx (the names are the executable's own, out of the RTTI
// at 0x004F2BBC and 0x004F2C08) - the engine's menus.
//
// An icon is a message receiver (0x004477B0, vtable 0x004E5B5C) that lives in a window:
// it registers itself as the window's receiver (window+0x130), keeps a queue of
// messages the script sends it (Sys0 0xAC), and holds a display object of its own
// (+0x2C, a CDspObjVirtual) attached to the window. 0x0046C7B0 is the factory (kind 0
// a DCIPIcon, 0x00447A70, vtable 0x004E5B6C; kind 1 a DCIPIconEx, 0x0044A8A0, vtable
// 0x004E5BBC) and files it on the list at 0x005667E8.
//
// Its content is a tree of entries, each a row of parts. A part of an Ex icon is a
// kind 5 sprite in the window's content slot for it, with a virtual display object in
// the window where the pointer can hit it. Once the content is built (+0x30) the
// icon follows the pointer and the keys every pass of the main loop (vtable+0x04,
// 0x00448680 -> 0x00448770): it swaps a part's image when the pointer is over it,
// selects and decides parts, animates frames and turns, and posts what happened to
// an event queue (+0xA0) that the script reads with Grp0 0xBF.
//
// This file ports the DCIPIconEx and the base code it shares. The plain DCIPIcon's
// own content (0x00447CF0) and its virtuals are not ported and name themselves.
// ----------------------------------------------------------------------------------
#define ICON_KIND_PLAIN 0
#define ICON_KIND_EX    1

// ----------------------------------------------------------------------------------
// An Ex icon's content descriptor (0x0046CB50)
//
// The script builds a tree in its own memory and hands over the address of its root;
// the engine deep-copies it before it uses any of it:
//
//   the root, 0x28 bytes: the entry count, the script address of the entry array, the
//     selected entry, then the words that become +0x40, +0x44, +0x48, +0x4C, +0x50
//     and (inverted) +0x88
//   an entry, 0x40 bytes: the part count, the columns, the script address of the part
//     array, the selected part, and the words the entry record carries
//   a part, 0xC4 bytes (see IconPart below)
//
// Both counts must be between 1 and 0x100 (0x0046CB9A / 0x0046CC23); the original
// answers 2 when the root's count or its array is bad and 3 when an entry's is.
// ----------------------------------------------------------------------------------
#define ICON_CONTENT_ROOT_WORDS  10   // 0x28
#define ICON_CONTENT_ENTRY_WORDS 16   // 0x40
#define ICON_CONTENT_PART_WORDS  49   // 0xC4
#define ICON_CONTENT_MAX_COUNT   0x100

// The words of a part (0xC4 bytes), as 0x0044A9E0 and the Ex virtuals read them.
enum
{
	ICON_PART_W0       = 0,   // +0x00: with +0x04, whether the part has a hit mask
	ICON_PART_USED     = 1,   // +0x04: 0 leaves the part out; with flag 0x10 its layer
	ICON_PART_X        = 2,   // +0x08
	ICON_PART_Y        = 3,   // +0x0C; with flag 0x02 its layer
	ICON_PART_ANCHOR_X = 4,   // +0x10: the image's anchor; the sprite sits at x + anchor
	ICON_PART_ANCHOR_Y = 5,   // +0x14
	ICON_PART_FRAMES   = 6,   // +0x18: frames of an animation (consecutive bitmaps)
	ICON_PART_INTERVAL = 7,   // +0x1C: milliseconds per frame
	ICON_PART_BITMAP   = 8,   // +0x20: the image
	ICON_PART_HOVER    = 9,   // +0x24: under the pointer, or -1
	ICON_PART_SELECTED = 10,  // +0x28: selected, or -1
	ICON_PART_SELHOVER = 11,  // +0x2C: selected and under the pointer, or -1
	ICON_PART_HITMASK  = 12,  // +0x30: the bitmap whose pixels the pointer must hit; -2 none
	ICON_PART_NODECIDE = 13,  // +0x34: a selected part cannot be decided again
	ICON_PART_TURNS    = 14,  // +0x38: how many turn keys follow
	ICON_PART_KEYS     = 15,  // +0x3C: {angle delta, duration, unused} per key
	ICON_PART_ANGLE    = 39,  // +0x9C: the angle a turn starts from
	ICON_PART_TURN_HOVER = 40,// +0xA0: turn only under the pointer
	ICON_PART_TURN_KEEP  = 41,// +0xA4: keep the angle when the pointer leaves
	ICON_PART_CURSOR   = 42,  // +0xA8..+0xBC: two cursors {x, y, bitmap} (window parts 2, 3)
	ICON_PART_FLAGS    = 48,  // +0xC0
};

typedef struct
{
	uint32_t  raw[ICON_CONTENT_ENTRY_WORDS];
	// The part array the original writes back over the entry's own +0x08. Kept
	// beside the raw words instead, so the copy of the script's bytes stays a copy.
	uint32_t  partCount;
	uint32_t* parts;   // partCount * ICON_CONTENT_PART_WORDS words
} IconContentEntry_t;

typedef struct IconContent
{
	uint32_t            raw[ICON_CONTENT_ROOT_WORDS];
	uint32_t            entryCount;
	IconContentEntry_t* entries;
} IconContent_t;

// 0x0046CB50. 0 and *out set on success; 2 or 3 and *out NULL on the two failures.
uint32_t Icon_ReadContent(Thread_t* thread, const uint32_t* root, IconContent_t** out);
// 0x0046CB00.
void Icon_FreeContent(IconContent_t* content);

// ----------------------------------------------------------------------------------
// The icon's own records
// ----------------------------------------------------------------------------------
// A part's place in the base class's bookkeeping (0x3C bytes, icon+0x38 -> entry +0x04).
typedef struct
{
	uint32_t w0;          // +0x00, the part's +0x00
	int32_t  x;           // +0x04
	int32_t  y;           // +0x08
	int32_t  bitmap;      // +0x0C
	int32_t  selected;    // +0x10
	int32_t  hover;       // +0x14
	int32_t  hitMask;     // +0x18
	int32_t  cursor[6];   // +0x1C: the part's two cursors
	uint32_t f34;         // +0x34
	uint32_t hotkey;      // +0x38: a key that decides the part (bit 31: when held)
} IconPartRecord_t;

// An entry (0x34 bytes at icon+0x38).
typedef struct
{
	int32_t           partCount;    // +0x00
	IconPartRecord_t* parts;        // +0x04
	int32_t           selectedPart; // +0x08, -1 for none
	// +0x0C..+0x30, the entry's words 5..14: [0] whether it has cursors, [1] the
	// pointer selects what it is over, [2] a held button counts as a click, [3] a
	// group (selecting a part clears the other entries of the group), [4..9] two
	// cursors {x, y, bitmap} in window parts 0 and 1.
	int32_t           words[10];
} IconEntry_t;

// A part the pointer can reach (0x14 bytes at icon+0x5C), one per part in order.
typedef struct
{
	uint32_t          hasImage;     // +0x00
	int32_t           entry;        // +0x04
	int32_t           part;         // +0x08
	IconPartRecord_t* record;       // +0x0C
	DisplayObject_t*  object;       // +0x10, the virtual object the pointer hits
} IconHit_t;

// An Ex part's animation (0x30 bytes, icon+0xCC[entry][part]).
typedef struct
{
	uint32_t  used;       // +0x00
	uint32_t* part;       // +0x04, the icon's own copy of the part's words
	uint32_t  slot;       // +0x08, the window content slot
	uint32_t  layer;      // +0x0C
	uint32_t  frame;      // +0x10
	uint32_t  frameAt;    // +0x14
	int32_t   angle;      // +0x18
	uint32_t  key;        // +0x1C
	int32_t   step;       // +0x20
	int32_t   steps;      // +0x24
	uint32_t  turnAt;     // +0x28
	int32_t   depth;      // +0x2C
} IconPartState_t;

// +0xA0: the event queue, three words and a link (0x0044A2B0 appends, 0x0044A300 pops).
typedef struct IconEvent IconEvent_t;
struct IconEvent
{
	uint32_t     words[3];
	IconEvent_t* next;
};

// The message queue at +0x18..+0x20 (0x004478C0 appends, 0x00447A10 pops).
typedef struct IconMessage IconMessage_t;
struct IconMessage
{
	uint32_t       count;
	uint32_t*      words;
	IconMessage_t* next;
};

typedef struct Icon Icon_t;
struct Icon
{
	uint32_t         handle;        // +0x04, the serial from 0x00565D74
	uint32_t         enabled;       // +0x10: the main loop runs only enabled icons
	uint32_t         dirty;         // +0x14 (0x004479B0)
	IconMessage_t*   messages;      // +0x20
	uint32_t         kind;          // +0x24: 0 DCIPIcon, 1 DCIPIconEx
	DisplayObject_t* window;        // +0x0C and +0x28
	DisplayObject_t* object;        // +0x2C, the CDspObjVirtual
	uint32_t         ready;         // +0x30: the content is built and live
	int32_t          entryCount;    // +0x34
	IconEntry_t*     entries;       // +0x38
	int32_t          currentEntry;  // +0x3C
	uint32_t         keyboard;      // +0x40: the icon takes the keyboard
	uint32_t         mouseRegion;   // +0x44: the window takes the pointer
	uint32_t         ignoreBits;    // +0x48: input bits the icon ignores
	uint32_t         keyMap;        // +0x4C: which key table the actions come from (0..7)
	uint32_t         decideSelects; // +0x50: deciding a part selects it
	int32_t*         grid;          // +0x54: {columns, rows} per entry
	int32_t          hitCount;      // +0x58
	IconHit_t*       hits;          // +0x5C
	int32_t          hitX, hitY;    // +0x60, +0x64
	int32_t          lastEntry;     // +0x68
	int32_t          lastPart;      // +0x6C
	uint32_t         lastValue;     // +0x70
	int32_t          hover;         // +0x74
	int32_t          mouseX, mouseY;// +0x78, +0x7C
	int32_t          under;         // +0x80: the hit under the pointer
	int32_t          underChecked;  // +0x84: the same, decidable and reachable
	uint32_t         active;        // +0x88
	uint32_t         input;         // +0x8C
	int32_t          pressed;       // +0x90: the hit a press waits to be released on
	IconEvent_t*     events;        // +0xA0
	// DCIPIconEx (+0xA4 to +0xD4).
	uint32_t         root[ICON_CONTENT_ROOT_WORDS];   // +0xA4
	IconContentEntry_t* copies;     // +0xA8: the entries and their parts, the icon's own
	IconPartState_t** states;       // +0xCC
	int32_t*         order;         // +0xD0: hits by layer, highest first
	int32_t          orderCount;    // +0xD4
	Icon_t*          next;          // the +0x08 link of the registration node
};

// 0x0046C7B0. The icon's handle, or 0.
uint32_t Icon_Create(uint32_t windowHandle, uint32_t kind);
// 0x0046C650 / 0x0046C790: the icon a handle names, or NULL.
Icon_t*  Icon_Resolve(uint32_t handle);
// 0x0046C670: out of the list and destroyed. Answers whether there was one.
int      Icon_Destroy(uint32_t handle);
void     Icon_FreeAll(void);

// 0x0044A9E0: an Ex icon's content from a descriptor (which it copies; the caller
// frees it). 0, 0x80000001 for a bad entry count, 0x80000002 for a bad part count.
#define ICON_CONTENT_SET_OK          0x00000000u
#define ICON_CONTENT_SET_BAD_COUNT   0x80000001u
#define ICON_CONTENT_SET_BAD_ENTRY   0x80000002u
uint32_t Icon_SetContent(Renderer_t* renderer, Icon_t* icon, const IconContent_t* content);

// 0x00448640: the oldest event's three words into `out`, 1; or zeros and 0.
int      Icon_TakeEvent(Icon_t* icon, uint32_t* out);
// 0x004478C0 (Sys0 0xAC): a message of `count` words (1..0x100) for the icon.
int      Icon_PostMessage(Icon_t* icon, const uint32_t* words, uint32_t count);
// 0x004485A0 (Grp0 0xBC): ready, the last decision's entry, part, value and point.
void     Icon_GetState(Icon_t* icon, uint32_t out[6]);
// 0x0044B540 (Grp1 0xBB): a part's sprite on or off. 0, 0x80000001, 0x80000002.
uint32_t Icon_SetPartEnabled(Renderer_t* renderer, Icon_t* icon, int32_t entry, int32_t part, uint32_t enabled);
// 0x00448610 (Grp0 0xBE): every entry's selected part into `out`; answers the count.
int32_t  Icon_SelectedParts(Icon_t* icon, uint32_t* out);
// 0x00447C90 (Grp1 0xBF): the key table for maps 4 to 7 (24 actions). 0 for another map.
int      Icon_SetKeyMap(uint32_t map, const uint32_t* actions);
// Sys0 0xAF (0x0046C5F0): 0 the icons run whole, 1 they only take messages. 0 for >1.
int      Icon_SetMode(uint32_t mode);
// The main loop's pass over the icons (0x0048CDDC).
void     Icon_FrameUpdate(Renderer_t* renderer);

#endif // __ICON_H__
