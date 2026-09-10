#ifndef __REGION_H__
#define __REGION_H__

#include <stdint.h>

// ----------------------------------------------------------------------------------
// The two priority-keyed region lists, at 0x005667F0 and 0x0056680C.
//
// Sys0 0x18 (0x00488290) puts one region into both and Sys0 0x19 (0x004882D0) takes
// it out of both again; the script names them by a plain number and the engine turns
// that number into a key the same way it makes a draw-order key, the number shifted
// up sixteen places with the low half all ones, so the region sits behind everything
// that shares its number. 0x0046D7E0 keeps each list in descending key order.
//
// A node is 0x1C bytes: the key, a rectangle, an owner, and the next node. Sys0 0x18
// gives the first list the rectangle at 0x00506A4C, which is
// {0x80000000, 0x80000000, 0x7FFFFFFF, 0x7FFFFFFF} - the whole plane - and the second
// list an all-zero one, and leaves the owner 0. The lists are read by the frame's
// input pass at 0x0045C690, and there is a second remover, 0x0046D940, which matches
// on the owner instead of the key; neither is read yet, so nothing here consumes a
// region and the list only ever holds what the script put in it.
//
// The head at 0x005667F0 is a node used only for its next pointer, which is why
// 0x00566808 - that node's +0x18 - reads as a list of its own elsewhere.
// ----------------------------------------------------------------------------------
typedef struct Region Region_t;
struct Region
{
	uint32_t  key;      // +0x00
	int32_t   left;     // +0x04
	int32_t   top;      // +0x08
	int32_t   right;    // +0x0C
	int32_t   bottom;   // +0x10
	uint32_t  owner;    // +0x14
	Region_t* next;     // +0x18
};

// There are exactly two, and the pair is always addressed together.
#define REGION_LIST_COUNT 2

// The key the script's number becomes, in both opcodes.
#define REGION_KEY(number) (((number) << 16) | 0xFFFFu)

// 0x0046D7E0, which both adders share: insert in descending key order.
void Region_Add(int list, uint32_t key, int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t owner);
// 0x0046D8E0: drop the first region of that key. Answers whether one went.
int  Region_RemoveByKey(int list, uint32_t key);
// The head, so the input pass can walk it once it is read.
Region_t* Region_First(int list);

// 0x0046E080, which both opcodes call after they have changed the lists: when a
// region is currently held (0x00566824) and the key reaches its own, what is under
// the pointer is worked out again through 0x0046DE00 and 0x0046DFB0. Nothing builds
// that held region yet - it is written only by the input pass at 0x0045C690, which is
// unread - so this has nothing to recompute and says so rather than pretending. It
// answers the name of the unread work when there ever is any, and NULL otherwise.
const char* Region_Recompute(uint32_t key);

void Region_FreeAll(void);

#endif // __REGION_H__
