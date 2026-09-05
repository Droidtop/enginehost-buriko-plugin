#include "arc.h"
#include "dsc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARC20_MAGIC "BURIKO ARC20"
#define ARC20_MAGIC_LENGTH 12
#define ARC20_HEADER_SIZE 0x10
#define ARC20_ENTRY_SIZE 0x80
#define ARC20_NAME_LENGTH 0x60

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

/* "<archive>.arc" under dir, with whatever case the disk has, or NULL. */
static char* FindArchiveIn(const char* dir, const char* archive)
{
	char wanted[300];
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

static char* FindArchive(const char* archive)
{
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

// Opening the archive and walking its table is the same work whether the caller
// wants the file's bytes or only wants to know the file is there, so both go
// through here. outData NULL asks the second question, and answers it without
// reading or inflating anything.
static int Arc_Find(const char* archive, const char* filename, uint8_t** outData, size_t* outSize)
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

	uint8_t header[ARC20_HEADER_SIZE];
	if(fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, ARC20_MAGIC, ARC20_MAGIC_LENGTH) != 0)
	{
		printf("[Arc]: \"%s\" is not a BURIKO ARC20 archive\n", path);
		fclose(f);
		free(path);
		return 0;
	}
	uint32_t count = ReadU32(header + ARC20_MAGIC_LENGTH);
	size_t tableSize = (size_t)count * ARC20_ENTRY_SIZE;
	uint8_t* table = (uint8_t*)malloc(tableSize ? tableSize : 1);
	if(table == NULL || fread(table, 1, tableSize, f) != tableSize)
	{
		free(table);
		fclose(f);
		free(path);
		return 0;
	}
	long dataBase = ARC20_HEADER_SIZE + (long)tableSize;

	int present = 0;
	for(uint32_t i = 0; i < count; i++)
	{
		const uint8_t* entry = table + (size_t)i * ARC20_ENTRY_SIZE;
		char name[ARC20_NAME_LENGTH + 1];
		memcpy(name, entry, ARC20_NAME_LENGTH);
		name[ARC20_NAME_LENGTH] = 0;
		if(!EqualsIgnoringCase(name, filename))
			continue;

		present = 1;
		if(outData == NULL)
			break;

		uint32_t offset = ReadU32(entry + ARC20_NAME_LENGTH);
		uint32_t size = ReadU32(entry + ARC20_NAME_LENGTH + 4);
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

uint8_t* Arc_ReadFile(const char* archive, const char* filename, size_t* outSize)
{
	uint8_t* data = NULL;
	Arc_Find(archive, filename, &data, outSize);
	return data;
}

int Arc_FileExists(const char* archive, const char* filename)
{
	return Arc_Find(archive, filename, NULL, NULL);
}
