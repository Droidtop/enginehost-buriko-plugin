#include "arc.h"
#include "dsc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The two containers the engine itself reads, in the order 0x00406EC0 tries
// them. A header is sixteen bytes: twelve of magic and the entry count; the
// table follows it and the data follows the table. An entry is its name, then
// the offset and the size, both relative to the end of the table.
#define ARC_HEADER_SIZE 0x10
#define ARC_MAGIC_LENGTH 12
#define ARC_COUNT_OFFSET 12

typedef struct
{
	const char* magic;       // twelve bytes, padded with spaces where it is short
	size_t      entrySize;
	size_t      nameLength;
} ArcContainer_t;

static const ArcContainer_t gContainers[] =
{
	{ "PackFile    ", 0x20, 0x10 },   // 0x004E4168, the table at 0x00406F97
	{ "BURIKO ARC20", 0x80, 0x60 },   // 0x004E4178, the branch at 0x00407028
};
#define ARC_CONTAINER_COUNT ((int)(sizeof(gContainers) / sizeof(gContainers[0])))

#define DSC_MAGIC "DSC FORMAT 1.00"
#define DSC_MAGIC_LENGTH 15
#define DSC_HEADER_SIZE 0x20
#define DSC_SIZE_OFFSET 0x14

static int EqualsIgnoringCase(const char* a, const char* b)
{
	while(*a && *b)
	{
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

/* True when name ends with ".arc", whatever case the caller wrote it in. */
static int HasArcExtension(const char* name)
{
	size_t length = strlen(name);
	return length > 4 && EqualsIgnoringCase(name + length - 4, ".arc");
}

/* The archive under dir, with whatever case the disk has, or NULL. Scripts name
   archives both ways - Fureraba's ipl asks for "sysprg.arc" where the boot asks for
   "system" - so a name that already carries the extension is taken as it stands. */
static char* FindArchiveIn(const char* dir, const char* archive)
{
	char wanted[300];
	if(HasArcExtension(archive))
		snprintf(wanted, sizeof(wanted), "%s", archive);
	else
		snprintf(wanted, sizeof(wanted), "%s.arc", archive);

	DIR* d = opendir(dir);
	if(d == NULL)
		return NULL;
	char* result = NULL;
	struct dirent* entry;
	while((entry = readdir(d)) != NULL)
	{
		if(EqualsIgnoringCase(entry->d_name, wanted))
		{
			size_t length = strlen(dir) + 1 + strlen(entry->d_name) + 1;
			result = (char*)malloc(length);
			if(result != NULL)
				snprintf(result, length, "%s/%s", dir, entry->d_name);
			break;
		}
	}
	closedir(d);
	return result;
}

/* One path component of dir, whatever case the disk has, appended to it. */
static char* ResolveComponent(const char* dir, const char* name)
{
	DIR* d = opendir(dir);
	if(d == NULL)
		return NULL;
	char* result = NULL;
	struct dirent* entry;
	while((entry = readdir(d)) != NULL)
	{
		if(!EqualsIgnoringCase(entry->d_name, name))
			continue;
		size_t length = strlen(dir) + 1 + strlen(entry->d_name) + 1;
		result = (char*)malloc(length);
		if(result != NULL)
			snprintf(result, length, "%s/%s", dir, entry->d_name);
		break;
	}
	closedir(d);
	return result;
}

/* The directory a script named, resolved from the game folder without regard
   to case, or NULL when a component of it is not there. */
static char* ResolveDirectory(char* path)
{
	char* dir = (char*)malloc(2);
	if(dir == NULL)
		return NULL;
	strcpy(dir, ".");
	char* rest = path;
	while(rest != NULL && *rest != 0)
	{
		char* slash = strchr(rest, '/');
		if(slash != NULL)
			*slash = 0;
		if(*rest != 0)
		{
			char* next = ResolveComponent(dir, rest);
			free(dir);
			if(next == NULL)
				return NULL;
			dir = next;
		}
		rest = slash == NULL ? NULL : slash + 1;
	}
	return dir;
}

static char* FindArchive(const char* archive)
{
	// Scripts name archives by a path as well as by a bare name: the boot's
	// listing of "Archive\\data0????.arc" is handed straight to Sys0 0x38, so
	// its members arrive as "Archive\\data01800.arc". Search the directory
	// the script named rather than the game folder.
	char normalised[512];
	if(snprintf(normalised, sizeof(normalised), "%s", archive) >= (int)sizeof(normalised))
		return NULL;
	for(char* c = normalised; *c != 0; c++)
	{
		if(*c == '\\')
			*c = '/';
	}
	char* base = strrchr(normalised, '/');
	if(base != NULL)
	{
		*base++ = 0;
		char* dir = ResolveDirectory(normalised);
		if(dir == NULL)
			return NULL;
		char* path = FindArchiveIn(dir, base);
		free(dir);
		return path;
	}

	char* path = FindArchiveIn(".", archive);
	if(path == NULL)
		path = FindArchiveIn("Archive", archive);
	if(path == NULL)
		path = FindArchiveIn("archive", archive);
	return path;
}

static uint32_t ReadU32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A DSC-compressed file becomes its plain contents; anything else is returned as is. */
static uint8_t* Inflate(uint8_t* data, size_t size, size_t* outSize)
{
	if(size < DSC_HEADER_SIZE || memcmp(data, DSC_MAGIC, DSC_MAGIC_LENGTH) != 0)
	{
		*outSize = size;
		return data;
	}
	size_t plainSize = ReadU32(data + DSC_SIZE_OFFSET);
	uint8_t* plain = (uint8_t*)malloc(plainSize ? plainSize : 1);
	if(plain == NULL)
	{
		free(data);
		return NULL;
	}
	decompressDSC(plain, data);
	free(data);
	*outSize = plainSize;
	return plain;
}

// Complex archives, in the order they were registered. The original keeps its
// archives in one linked list rooted at 0x00566754 and walks it by name.
typedef struct ArcComplex ArcComplex_t;
struct ArcComplex
{
	char*         name;
	char**        members;
	int           count;
	ArcComplex_t* next;
};

static ArcComplex_t* gComplexArchives = NULL;

static ArcComplex_t* FindComplex(const char* name)
{
	for(ArcComplex_t* c = gComplexArchives; c != NULL; c = c->next)
	{
		if(EqualsIgnoringCase(c->name, name))
			return c;
	}
	return NULL;
}

int Arc_CreateComplex(const char* name, const char* const* members, int count)
{
	if(name == NULL || count < 0)
		return 0;
	// 0x00406BC0 compares the new archive's name against every archive already
	// in the list and refuses a repeat, returning 0 without registering it.
	if(FindComplex(name) != NULL)
		return 0;

	ArcComplex_t* c = (ArcComplex_t*)malloc(sizeof(ArcComplex_t));
	if(c == NULL)
		return 0;
	c->name = (char*)malloc(strlen(name) + 1);
	c->members = (char**)malloc(sizeof(char*) * (size_t)(count > 0 ? count : 1));
	if(c->name == NULL || c->members == NULL)
	{
		free(c->name);
		free(c->members);
		free(c);
		return 0;
	}
	strcpy(c->name, name);
	c->count = 0;
	for(int i = 0; i < count; i++)
	{
		const char* member = members[i];
		if(member == NULL)
			continue;
		char* copy = (char*)malloc(strlen(member) + 1);
		if(copy == NULL)
			continue;
		strcpy(copy, member);
		c->members[c->count++] = copy;
	}
	c->next = gComplexArchives;
	gComplexArchives = c;
	return 1;
}

// Opening the archive and walking its table is the same work whether the caller
// wants the file's bytes or only wants to know the file is there, so both go
// through here. outData NULL asks the second question, and answers it without
// reading or inflating anything.
static int Arc_FindInArchive(const char* archive, const char* filename, uint8_t** outData, size_t* outSize)
{
	char* path = FindArchive(archive);
	if(path == NULL)
		return 0;

	FILE* f = fopen(path, "rb");
	if(f == NULL)
	{
		free(path);
		return 0;
	}

	uint8_t header[ARC_HEADER_SIZE];
	const ArcContainer_t* container = NULL;
	if(fread(header, 1, sizeof(header), f) == sizeof(header))
	{
		for(int i = 0; i < ARC_CONTAINER_COUNT; i++)
		{
			if(memcmp(header, gContainers[i].magic, ARC_MAGIC_LENGTH) == 0)
			{
				container = &gContainers[i];
				break;
			}
		}
	}
	if(container == NULL)
	{
		printf("[Arc]: \"%s\" is neither a BURIKO ARC20 nor a PackFile archive\n", path);
		fclose(f);
		free(path);
		return 0;
	}
	uint32_t count = ReadU32(header + ARC_COUNT_OFFSET);
	size_t tableSize = (size_t)count * container->entrySize;
	uint8_t* table = (uint8_t*)malloc(tableSize ? tableSize : 1);
	if(table == NULL || fread(table, 1, tableSize, f) != tableSize)
	{
		free(table);
		fclose(f);
		free(path);
		return 0;
	}
	long dataBase = ARC_HEADER_SIZE + (long)tableSize;

	int present = 0;
	for(uint32_t i = 0; i < count; i++)
	{
		const uint8_t* entry = table + (size_t)i * container->entrySize;
		// The original copies the name out as a C string and stops at the
		// terminator, so a name that fills its field is taken whole.
		char name[0x61];
		memcpy(name, entry, container->nameLength);
		name[container->nameLength] = 0;
		if(!EqualsIgnoringCase(name, filename))
			continue;

		present = 1;
		if(outData == NULL)
			break;

		uint32_t offset = ReadU32(entry + container->nameLength);
		uint32_t size = ReadU32(entry + container->nameLength + 4);
		uint8_t* data = (uint8_t*)malloc(size ? size : 1);
		if(data == NULL)
			break;
		if(fseek(f, dataBase + (long)offset, SEEK_SET) != 0 || fread(data, 1, size, f) != size)
		{
			free(data);
			break;
		}
		*outData = Inflate(data, size, outSize);
		if(*outData != NULL)
			printf("[Arc]: Read \"%s\" from \"%s\" (%zu bytes)\n", name, path, *outSize);
		break;
	}

	free(table);
	fclose(f);
	free(path);
	return present;
}

// A name that belongs to a complex archive is answered by its members, in the
// order the script listed them. The depth limit is ours: the original's list is
// a flat one, but nothing stops a script from naming a group inside itself.
static int Arc_Find(const char* archive, const char* filename, uint8_t** outData, size_t* outSize, int depth)
{
	ArcComplex_t* group = FindComplex(archive);
	if(group == NULL)
		return Arc_FindInArchive(archive, filename, outData, outSize);
	if(depth > 8)
	{
		printf("[Arc]: complex archive \"%s\" is nested too deeply\n", archive);
		return 0;
	}
	for(int i = 0; i < group->count; i++)
	{
		if(Arc_Find(group->members[i], filename, outData, outSize, depth + 1))
			return 1;
	}
	return 0;
}

uint8_t* Arc_ReadFile(const char* archive, const char* filename, size_t* outSize)
{
	uint8_t* data = NULL;
	Arc_Find(archive, filename, &data, outSize, 0);
	return data;
}

int Arc_FileExists(const char* archive, const char* filename)
{
	return Arc_Find(archive, filename, NULL, NULL, 0);
}
