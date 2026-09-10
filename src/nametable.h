#ifndef __NAMETABLE_H__
#define __NAMETABLE_H__

#include <stdint.h>

// ----------------------------------------------------------------------------------
// The named-string table at 0x00565BB4.
//
// A table of name -> string, where both sides are kept as bytes AND as a count of
// Shift-JIS characters, and the string additionally as an array of its raw Shift-JIS
// character codes. Grp1 0x96 (0x004848F0) fills it from a single blob of text;
// 0x00434A70 writes one back out in the same shape, through the format "%s\\%s\n"
// at 0x004E5250. It is consulted from the text-layout call at 0x0043528D, which
// hands the root straight to a virtual - what the layout does with a match is not
// read yet, so nothing here interprets an entry.
//
// The engine's own node is 0x28 bytes (0x00434721 allocates it), and the root at
// 0x00565BB4 is a node of the same shape used only for its +0x24, so the list head
// is root+0x24. This keeps the fields in the original's order:
//
//   +0x00 name           +0x04 name size (strlen + 1)   +0x08 name characters
//   +0x0C value          +0x10 value size (strlen + 1)  +0x14 value characters
//   +0x18 value character count                         +0x1C the mode it was made
//   +0x20 always 0       +0x24 the next entry
// ----------------------------------------------------------------------------------
typedef struct NameTableEntry NameTableEntry_t;
struct NameTableEntry
{
	char*             name;          // +0x00
	uint32_t          nameSize;      // +0x04, strlen(name) + 1
	uint32_t          nameLength;    // +0x08, Shift-JIS characters in the name
	char*             value;         // +0x0C
	uint32_t          valueSize;     // +0x10, strlen(value) + 1
	uint32_t*         valueChars;    // +0x14, valueLength raw Shift-JIS codes
	uint32_t          valueLength;   // +0x18
	uint32_t          mode;          // +0x1C
	uint32_t          field20;       // +0x20, the original zeroes it and this is
	                                 //        the only place it is ever written
	NameTableEntry_t* next;          // +0x24
};

// The mode 0x00434600 takes. With 0 an existing entry of the same name is rewritten
// in place and a new one is inserted before the first entry whose name is no longer
// than this one, so the list stays in descending order of name length - a longest
// match first order. With anything else the name is not looked up at all and the
// entry goes before the first one whose own mode is 0.
#define NAMETABLE_MODE_REPLACE 0u

// 0x00434600 with the root at 0x00565BB4.
void NameTable_Set(const char* name, const char* value, uint32_t mode);

// 0x00434B20: read `name\value\n` records out of one string, in order, into the
// table. Answers 1 when the whole string was consumed, which is what Grp1 0x96
// pushes back. A record with no `\` in it ends the walk, as it does there.
uint32_t NameTable_Load(const char* text);

// The head of the list, so the layout side can walk it once it is read.
NameTableEntry_t* NameTable_First(void);

void NameTable_FreeAll(void);

#endif // __NAMETABLE_H__
