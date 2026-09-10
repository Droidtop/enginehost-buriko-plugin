//
// The named-string table at 0x00565BB4, which Grp1 0x96 loads.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nametable.h"
#include "sjis.h"

// The original's root is a whole 0x28-byte node used only for its +0x24; nothing
// else of it is ever read, so only that field is kept.
static NameTableEntry_t* gNameTableHead = NULL;

NameTableEntry_t* NameTable_First(void)
{
	return gNameTableHead;
}

// 0x00434C30. Walks a string one Shift-JIS character at a time through 0x0042EAF0,
// counting the characters and, when `out` is given, writing each character's raw
// Shift-JIS code into it - the lead byte shifted up eight with the trail byte added,
// not a conversion to anything else.
static uint32_t NameTable_Characters(const char* text, uint32_t* out)
{
	uint32_t count = 0;
	const uint8_t* p = (const uint8_t*)text;
	while(*p != 0)
	{
		uint32_t code = *p;
		int wide = SjisIsTwobyte((uint8_t*)p) ? 1 : 0;
		if(wide)
			code = (code << 8) + p[1];
		if(out != NULL)
			out[count] = code;
		count++;
		p += wide ? 2 : 1;
	}
	return count;
}

// The value half of an entry: the bytes, their count, and the characters. Both the
// rewrite arm (0x00434689) and the create arm (0x0043476A) do exactly this.
static void NameTable_SetValue(NameTableEntry_t* entry, const char* value)
{
	entry->valueSize = (uint32_t)strlen(value) + 1;
	entry->valueLength = NameTable_Characters(value, NULL);
	entry->value = (char*)malloc(entry->valueSize);
	entry->valueChars = (uint32_t*)malloc(entry->valueLength * 4);
	if(entry->value == NULL || entry->valueChars == NULL)
	{
		printf("[NameTable]: Out of memory\n");
		exit(1);
	}
	memcpy(entry->value, value, entry->valueSize);
	NameTable_Characters(value, entry->valueChars);
}

// 0x00434600, with the root at 0x00565BB4 as 0x004345E0 passes it.
void NameTable_Set(const char* name, const char* value, uint32_t mode)
{
	// The lookup, which the original does only in the replace mode.
	if(mode == NAMETABLE_MODE_REPLACE)
	{
		for(NameTableEntry_t* entry = gNameTableHead; entry != NULL; entry = entry->next)
		{
			if(strcmp(entry->name, name) != 0)
				continue;
			free(entry->value);
			free(entry->valueChars);
			NameTable_SetValue(entry, value);
			return;
		}
	}

	// Where it goes. The original walks with the previous link in hand and inserts
	// there; `previous` NULL means the head.
	uint32_t nameSize = (uint32_t)strlen(name) + 1;
	NameTableEntry_t* previous = NULL;
	NameTableEntry_t* next = gNameTableHead;
	while(next != NULL)
	{
		if(mode == NAMETABLE_MODE_REPLACE)
		{
			// Descending name length, so a longest match comes first.
			if(nameSize >= next->nameSize)
				break;
		}
		else
		{
			// Before the first entry that was made in the replace mode.
			if(next->mode == NAMETABLE_MODE_REPLACE)
				break;
		}
		previous = next;
		next = next->next;
	}

	NameTableEntry_t* entry = (NameTableEntry_t*)malloc(sizeof(NameTableEntry_t));
	if(entry == NULL)
	{
		printf("[NameTable]: Out of memory\n");
		exit(1);
	}
	entry->nameSize = nameSize;
	entry->nameLength = NameTable_Characters(name, NULL);
	entry->name = (char*)malloc(nameSize);
	if(entry->name == NULL)
	{
		printf("[NameTable]: Out of memory\n");
		exit(1);
	}
	memcpy(entry->name, name, nameSize);
	NameTable_SetValue(entry, value);
	entry->mode = mode;
	entry->field20 = 0;
	entry->next = next;
	if(previous != NULL)
		previous->next = entry;
	else
		gNameTableHead = entry;
}

// 0x00434B20. The blob is a run of `name\value\n` records: the name reaches to the
// first backslash (0x004E5258), the value to the next newline (0x004E525C) or to the
// end of the blob. A record with no backslash left in it ends the walk without
// consuming anything, which is why the answer is "did this reach the end".
//
// One deliberate difference: an empty name or an empty value makes the original
// skip the record without advancing esi, so it spins on it for ever. This ends the
// walk instead and answers 0, which is what the original would mean if it could say
// it.
//
// Both halves land in a 0x100-byte stack buffer there, and a longer one would run
// off it; this refuses the record instead and says so.
#define NAMETABLE_FIELD_MAX 0x100

uint32_t NameTable_Load(const char* text)
{
	if(text == NULL)
		return 0;

	char name[NAMETABLE_FIELD_MAX];
	char value[NAMETABLE_FIELD_MAX];
	const char* at = text;
	while(*at != 0)
	{
		const char* separator = strchr(at, '\\');
		if(separator == NULL || separator == at)
			break;

		size_t nameLength = (size_t)(separator - at);
		const char* rest = separator + 1;
		const char* end = strchr(rest, '\n');
		size_t valueLength = end != NULL ? (size_t)(end - rest) : strlen(rest);
		if(valueLength == 0)
			break;
		if(nameLength >= NAMETABLE_FIELD_MAX || valueLength >= NAMETABLE_FIELD_MAX)
		{
			printf("[NameTable]: Refusing a record whose name or value is longer than %d bytes\n", NAMETABLE_FIELD_MAX - 1);
			break;
		}

		memcpy(name, at, nameLength);
		name[nameLength] = 0;
		memcpy(value, rest, valueLength);
		value[valueLength] = 0;
		NameTable_Set(name, value, NAMETABLE_MODE_REPLACE);

		at = rest + valueLength + (end != NULL ? 1 : 0);
	}
	return *at == 0 ? 1u : 0u;
}

void NameTable_FreeAll(void)
{
	NameTableEntry_t* entry = gNameTableHead;
	while(entry != NULL)
	{
		NameTableEntry_t* next = entry->next;
		free(entry->name);
		free(entry->value);
		free(entry->valueChars);
		free(entry);
		entry = next;
	}
	gNameTableHead = NULL;
}
