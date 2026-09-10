#ifndef __ICON_H__
#define __ICON_H__

#include <stdint.h>

#include "object.h"
#include "renderer.h"
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

// The content descriptor itself is described further down; the icon holds one.
typedef struct IconContent IconContent_t;

// One entry of an icon's content, as 0x0044A9E0 lands it: the 0x34-byte record at
// icon+0x38 and the pair at icon+0x54 are two arrays over the same entries in the
// original, and one record here.
typedef struct
{
	// icon+0x54, two dwords per entry: how many parts stand side by side, and how
	// many rows that makes. The columns are the entry's own second word unless it
	// is not positive or larger than the part count, and then they are the part
	// count (0x0044AADE); the rows are the part count divided by them, rounded up.
	uint32_t columns;
	uint32_t rows;
	// [0x00] of the 0x34-byte record: how many parts this entry has.
	uint32_t partCount;
	// [0x08]: which of this entry's parts is the selected one, or -1 when the
	// entry's own word is outside 0..partCount-1 (0x0044ACCC).
	int32_t  selectedPart;
	// [0x0C] to [0x30]: the entry's words 5 to 14, carried across as they are.
	// Word 4 is skipped, and words 0, 1, 2 and 3 are the four above.
	uint32_t carried[10];
} IconEntry_t;

// +0xA0 of an icon is the head of a queue of events, each a 16-byte node of
// three words and a link (0x0044A300 pops one and frees it). What an icon puts
// on it is its own input handling, which needs the hit regions its parts have
// not been given yet, so nothing fills it in this engine so far - but a script
// asks for the next event several times per frame, so the queue itself has to
// be real and has to be empty rather than imaginary and always full.
typedef struct IconEvent IconEvent_t;
struct IconEvent
{
	uint32_t     words[3];
	IconEvent_t* next;
};

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

	// ------------------------------------------------------------------------
	// What 0x0044A9E0 leaves on the icon. Everything here is the descriptor's,
	// read out of it once and named, so nothing after this has to walk the raw
	// words again.
	// ------------------------------------------------------------------------
	// +0xA4 and +0xA8: the original copies the whole descriptor a second time,
	// into memory of the icon's own. Icon_ReadContent has already made that copy
	// (it is what 0x0046CB50 does), so the icon takes it over rather than copying
	// a copy - one descriptor, one owner.
	IconContent_t*   content;
	uint32_t         entryCount;    // +0x34
	IconEntry_t*     entries;       // +0x38 and +0x54, one record per entry
	// +0x3C: which entry is the selected one, or -1 when the descriptor's own
	// word is outside 0..entryCount-1.
	int32_t          selectedEntry;
	uint32_t         carried[5];    // +0x40, +0x44, +0x48, +0x4C (masked to 3
	                                // bits) and +0x50
	uint32_t         field88;       // +0x88: 1 when the root's ninth word is zero
	uint32_t         partCount;     // +0x58: every entry's parts added up
	uint32_t         ready;         // +0x30: set once the content is built
	uint32_t         redraw;        // +0x14, which 0x004479B0 sets and 0x004478A0
	                                // consumes
	IconEvent_t*     events;        // +0xA0, oldest first
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

struct IconContent
{
	uint32_t            raw[ICON_CONTENT_ROOT_WORDS];
	uint32_t            entryCount;
	IconContentEntry_t* entries;
};

// 0x0046CB50. Copies the tree at the resolved address `root` into freshly allocated
// memory. 0 and *out set on success; 2 or 3 and *out NULL on the two failures above.
uint32_t Icon_ReadContent(Thread_t* thread, const uint32_t* root, IconContent_t** out);
// 0x0046CB00. Frees what Icon_ReadContent built, parts first.
void Icon_FreeContent(IconContent_t* content);

// 0x0044A9E0. Gives an icon the content a descriptor describes. The icon takes over
// `content`, whatever the answer: 0 when the content was built, 0x80000001 when the
// entry count is outside 1..0x100, 0x80000002 when an entry has more than 0x100
// parts. The results are the original's own.
#define ICON_CONTENT_SET_OK          0x00000000u
#define ICON_CONTENT_SET_BAD_COUNT   0x80000001u
#define ICON_CONTENT_SET_BAD_ENTRY   0x80000002u
uint32_t Icon_SetContent(Renderer_t* renderer, Icon_t* icon, IconContent_t* content);

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

// 0x00448640. Copies the oldest event's three words to `out` and frees it,
// answering 1; with an empty queue it writes three zeros and answers 0.
int Icon_TakeEvent(Icon_t* icon, uint32_t* out);
// 0x0044A330's half of the same queue: one event onto the end of it.
void Icon_PostEvent(Icon_t* icon, uint32_t word0, uint32_t word1, uint32_t word2);

#endif // __ICON_H__
