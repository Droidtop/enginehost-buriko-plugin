#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "gameid.h"
#include "engine.h"

// ----------------------------------------------------------------------------------
// Reading a game's identifier out of its own executable
//
// Sys0 0xE8 returns a 16-byte string that is a compile-time constant of each original
// executable ("FriendToLoverHD" for Fureraba HD). It is load-bearing: the boot script
// branches on it and the save files are named after it (UserData\FriendToLoverHD000
// .sud), so an empty or wrong one silently changes what the game does.
//
// It is not recorded in the game's data, nor in the executable's version resource;
// it is a plain NUL-terminated string in .rdata (0x004EBDAC in fureraba.exe) with
// nothing around it to mark it out. The handler that reads it is recognisable
// instead. At 0x0048ACE0 it is always the same shape - the sixteen bytes copied as
// four dword loads from four consecutive addresses, then the zero return:
//
//     8B 0D <a+0>     mov  ecx, [a+0]
//     89 08           mov  [eax], ecx
//     8B 15 <a+4>     mov  edx, [a+4]
//     89 50 04        mov  [eax+4], edx
//     8B 0D <a+8>     mov  ecx, [a+8]
//     89 48 08        mov  [eax+8], ecx
//     8B 15 <a+12>    mov  edx, [a+12]
//     89 50 0C        mov  [eax+0xC], edx
//     33 C0           xor  eax, eax
//
// So the scan searches the image for that instruction sequence with the address
// wildcarded, requires the four addresses to be consecutive, and reads the sixteen
// bytes they point at. On Fureraba's folder it accepts fureraba.exe and rejects
// BHVC.exe and Uninstaller.exe, which do not contain the sequence at all.
// ----------------------------------------------------------------------------------

#define GAMEID_PATTERN_LENGTH 37

typedef struct
{
	uint32_t virtualAddress;
	uint32_t virtualSize;
	uint32_t rawAddress;
	uint32_t rawSize;
} PeSection_t;

#define GAMEID_MAX_SECTIONS 96

static uint32_t ReadU32(const uint8_t* data, size_t offset)
{
	return (uint32_t)data[offset]
		| ((uint32_t)data[offset + 1] << 8)
		| ((uint32_t)data[offset + 2] << 16)
		| ((uint32_t)data[offset + 3] << 24);
}

static uint16_t ReadU16(const uint8_t* data, size_t offset)
{
	return (uint16_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8));
}

// Fills `sections` from the PE headers and returns how many there are, or -1 if this
// is not a 32-bit PE image. Only PE32 is accepted: the handler shape above is 32-bit
// code, so a 64-bit image could never match it anyway.
static int PeReadSections(const uint8_t* data, size_t size, PeSection_t* sections)
{
	if(size < 0x40)
		return -1;

	uint32_t peOffset = ReadU32(data, 0x3C);
	if(peOffset > size || size - peOffset < 24 + 28 + 4)
		return -1;
	if(ReadU32(data, peOffset) != 0x00004550) // "PE\0\0"
		return -1;

	uint16_t sectionCount = ReadU16(data, peOffset + 6);
	uint16_t optionalSize = ReadU16(data, peOffset + 20);
	if(optionalSize < 28 + 4 || ReadU16(data, peOffset + 24) != 0x010B) // PE32
		return -1;

	uint32_t imageBase = ReadU32(data, peOffset + 24 + 28);

	size_t table = (size_t)peOffset + 24 + optionalSize;
	if(sectionCount > GAMEID_MAX_SECTIONS)
		sectionCount = GAMEID_MAX_SECTIONS;
	if(table > size || (size - table) / 40 < sectionCount)
		return -1;

	for(uint16_t i = 0; i < sectionCount; i++)
	{
		size_t entry = table + (size_t)i * 40;
		sections[i].virtualSize   = ReadU32(data, entry + 8);
		sections[i].virtualAddress = ReadU32(data, entry + 12) + imageBase;
		sections[i].rawSize       = ReadU32(data, entry + 16);
		sections[i].rawAddress    = ReadU32(data, entry + 20);
	}

	return (int)sectionCount;
}

// Turns a virtual address into a file offset, or returns -1 when the address is not
// backed by bytes in the file (an uninitialised section, say).
static long PeOffsetOf(const PeSection_t* sections, int sectionCount, size_t size, uint32_t address)
{
	for(int i = 0; i < sectionCount; i++)
	{
		uint32_t base = sections[i].virtualAddress;
		if(address < base)
			continue;

		uint32_t delta = address - base;
		if(delta >= sections[i].virtualSize || delta >= sections[i].rawSize)
			continue;

		uint64_t offset = (uint64_t)sections[i].rawAddress + delta;
		if(offset >= size)
			continue;

		return (long)offset;
	}

	return -1;
}

// Matches the handler's instruction sequence at `p` and, if it matches, writes the
// four wildcarded addresses into `addresses`.
static int MatchHandler(const uint8_t* p, uint32_t* addresses)
{
	static const struct { size_t offset; size_t length; uint8_t bytes[3]; } fixed[] =
	{
		{  0, 2, { 0x8B, 0x0D } },
		{  6, 2, { 0x89, 0x08 } },
		{  8, 2, { 0x8B, 0x15 } },
		{ 14, 3, { 0x89, 0x50, 0x04 } },
		{ 17, 2, { 0x8B, 0x0D } },
		{ 23, 3, { 0x89, 0x48, 0x08 } },
		{ 26, 2, { 0x8B, 0x15 } },
		{ 32, 3, { 0x89, 0x50, 0x0C } },
		{ 35, 2, { 0x33, 0xC0 } },
	};

	for(size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
	{
		if(memcmp(p + fixed[i].offset, fixed[i].bytes, fixed[i].length) != 0)
			return 0;
	}

	addresses[0] = ReadU32(p, 2);
	addresses[1] = ReadU32(p, 10);
	addresses[2] = ReadU32(p, 19);
	addresses[3] = ReadU32(p, 28);

	// The four loads must read one string, not four unrelated globals.
	if(addresses[1] != addresses[0] + 4 || addresses[2] != addresses[0] + 8
		|| addresses[3] != addresses[0] + 12)
		return 0;

	return 1;
}

int GameId_ReadFromExecutable(const char* path, char* out)
{
	if(path == NULL || out == NULL)
		return 0;

	FILE* file = fopen(path, "rb");
	if(file == NULL)
		return 0;

	if(fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return 0;
	}

	long length = ftell(file);
	if(length <= GAMEID_PATTERN_LENGTH)
	{
		fclose(file);
		return 0;
	}
	rewind(file);

	uint8_t* data = (uint8_t*)malloc((size_t)length);
	if(data == NULL)
	{
		fclose(file);
		return 0;
	}

	size_t size = fread(data, 1, (size_t)length, file);
	fclose(file);
	if(size <= GAMEID_PATTERN_LENGTH)
	{
		free(data);
		return 0;
	}

	PeSection_t sections[GAMEID_MAX_SECTIONS];
	int sectionCount = PeReadSections(data, size, sections);
	if(sectionCount < 1)
	{
		free(data);
		return 0;
	}

	int found = 0;
	for(size_t i = 0; i + GAMEID_PATTERN_LENGTH <= size && !found; i++)
	{
		// Cheap first test so the whole image is not compared nine times over.
		if(data[i] != 0x8B || data[i + 1] != 0x0D)
			continue;

		uint32_t addresses[4];
		if(!MatchHandler(data + i, addresses))
			continue;

		long offset = PeOffsetOf(sections, sectionCount, size, addresses[0]);
		if(offset < 0 || (size_t)offset + ENGINE_GAME_ID_SIZE > size)
			continue;

		const uint8_t* raw = data + offset;

		// The original copies a fixed sixteen bytes. Every identifier seen so far is
		// shorter than that and terminated, and requiring the terminator is what
		// tells a real identifier from sixteen bytes of something else.
		if(raw[ENGINE_GAME_ID_SIZE - 1] != 0)
			continue;

		size_t idLength = strlen((const char*)raw);
		if(idLength == 0)
			continue;

		int printable = 1;
		for(size_t c = 0; c < idLength; c++)
		{
			if(raw[c] < 0x20 || raw[c] > 0x7E)
			{
				printable = 0;
				break;
			}
		}
		if(!printable)
			continue;

		memcpy(out, raw, idLength + 1);
		found = 1;
	}

	free(data);
	return found;
}

static int HasExeExtension(const char* name)
{
	size_t length = strlen(name);
	if(length < 5)
		return 0;

	const char* extension = name + length - 4;
	return extension[0] == '.'
		&& (extension[1] == 'e' || extension[1] == 'E')
		&& (extension[2] == 'x' || extension[2] == 'X')
		&& (extension[3] == 'e' || extension[3] == 'E');
}

static void AppendExamined(char* examined, size_t examinedSize, const char* name)
{
	if(examined == NULL || examinedSize == 0)
		return;

	size_t used = strlen(examined);
	size_t needed = strlen(name) + (used ? 2 : 0);
	if(used + needed + 1 > examinedSize)
		return;

	if(used)
		strcat(examined, ", ");
	strcat(examined, name);
}

int GameId_ScanFolder(const char* folder, char* out, char* exeName, size_t exeNameSize,
	char* examined, size_t examinedSize)
{
	if(examined != NULL && examinedSize > 0)
		examined[0] = 0;

	DIR* directory = opendir(folder != NULL ? folder : ".");
	if(directory == NULL)
		return 0;

	int found = 0;
	struct dirent* entry;
	while(!found && (entry = readdir(directory)) != NULL)
	{
		if(!HasExeExtension(entry->d_name))
			continue;

		char path[1024];
		if((size_t)snprintf(path, sizeof(path), "%s/%s", folder != NULL ? folder : ".",
			entry->d_name) >= sizeof(path))
			continue;

		if(GameId_ReadFromExecutable(path, out))
		{
			if(exeName != NULL && exeNameSize > 0)
			{
				strncpy(exeName, entry->d_name, exeNameSize - 1);
				exeName[exeNameSize - 1] = 0;
			}
			found = 1;
			break;
		}

		AppendExamined(examined, examinedSize, entry->d_name);
	}

	closedir(directory);
	return found;
}
