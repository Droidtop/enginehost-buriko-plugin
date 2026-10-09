#ifndef __NAMETABLE_H__
#define __NAMETABLE_H__

#include <stdint.h>

// ----------------------------------------------------------------------------------
// The ruby dictionary (0x004345E0 - 0x004349F1): word -> reading.
//
// One table shape serves two owners. The global table at 0x00565BB4 is what Grp1 0x94
// adds to (0x004345C0), Grp1 0x96 loads (0x004345D0 -> 0x00434B20) and the window's
// message layout is handed (0x0043527D); a bitmap's text draw (0x00434D30) builds a
// table of its own on the stack from the dictionary string it is given. The layout
// adds the words of <ruby> tags to whichever table it has, marked local, and takes
// them out again once their ruby is laid out (0x00434810, 0x004348B0).
//
// Both the word and its reading are kept as bytes AND as a count of Shift-JIS
// characters, and the reading additionally as an array of its raw Shift-JIS codes.
// The engine's own node is 0x28 bytes (0x00434721 allocates it), and the table itself
// is a node of the same shape used only for its +0x24, so the list head is +0x24:
//
//   +0x00 name           +0x04 name size (strlen + 1)   +0x08 name characters
//   +0x0C value          +0x10 value size (strlen + 1)  +0x14 value characters
//   +0x18 value character count                         +0x1C the mode it was made
//   +0x20 times a local word was matched                +0x24 the next entry
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
	uint32_t          mode;          // +0x1C, 0 a dictionary word, else a local one
	uint32_t          used;          // +0x20, 0x004349A0 counts a local word's matches
	NameTableEntry_t* next;          // +0x24
};

typedef struct NameTable
{
	NameTableEntry_t* head;          // +0x24 of the root node
} NameTable_t;

// The mode 0x00434600 takes. With 0 an existing entry of the same name is rewritten
// in place and a new one is inserted before the first entry whose name is no longer
// than this one, so the list stays in descending order of name length - a longest
// match first order. With anything else (a local word) the name is not looked up at
// all and the entry goes before the first one whose own mode is 0.
#define NAMETABLE_MODE_REPLACE 0u

// The table at 0x00565BB4.
NameTable_t* NameTable_Global(void);

// 0x00434600.
void NameTable_Set(NameTable_t* table, const char* name, const char* value, uint32_t mode);

// 0x00434B20: read `name\value\n` records out of one string, in order, into the
// table as dictionary words. Answers 1 when the whole string was consumed, which is
// what Grp1 0x96 pushes back. A record with no `\` in it ends the walk, as it does
// there.
uint32_t NameTable_Load(NameTable_t* table, const char* text);

// 0x00434920: the entry of that name, or NULL.
NameTableEntry_t* NameTable_Find(NameTable_t* table, const char* name);
// 0x004349A0: the first unused entry the text at `p` starts with, its name copied to
// `out`; a local word is used up by being found. 0 when none matches.
int NameTable_FindAt(NameTable_t* table, const char* p, char* out);
// 0x00434810: the entry of that name taken out - only a local one when `localOnly`.
int NameTable_Remove(NameTable_t* table, const char* name, int localOnly);
// 0x004348B0: every local entry taken out.
void NameTable_RemoveLocal(NameTable_t* table);
// 0x004348F0: the table emptied.
void NameTable_Clear(NameTable_t* table);

#endif // __NAMETABLE_H__
