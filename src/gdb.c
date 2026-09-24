#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gdb.h"

// ============================================================================
// SDC
// ============================================================================

static const char kSdcMagic[16] = "SDC FORMAT 1.00";   // 0x004EC23C, NUL included
static const char kGdbMagic[16] = "BURIKO GDB 3.00";   // 0x004E70BC, NUL included

// The keystream 0x00492450 seeds and 0x00492460 steps: the byte used is the low
// eight bits of what it answers.
static uint8_t SDC_NextKey(uint32_t* state)
{
	uint32_t s = *state;
	uint32_t lo = (s & 0xFFFF) * 0x4E35u;
	uint32_t hi = ((s >> 16) * 0x4E35u + s * 0x15Au + (lo >> 16)) & 0xFFFF;
	*state = (hi << 16) + (lo & 0xFFFF) + 1;
	return (uint8_t)(hi & 0x7FFF);
}

static uint32_t Rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void Wr32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void Wr16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

uint32_t SDC_DecodedSize(const uint8_t* file, size_t fileSize)
{
	if(fileSize < 0x20 || memcmp(file, kSdcMagic, 16) != 0)
		return 0;
	return Rd32(file + 0x18);
}

// 0x00493A80: the magic (0x00493AD0), the deciphering and both checksums over
// the enciphered bytes (0x004939F0), then the LZ (0x004938E0). A control byte
// with its top bit clear is a run of that many plus one literal bytes; one with
// it set copies ((c >> 3) & 15) + 2 bytes from ((c & 7) << 8 | next) + 2 back.
uint32_t SDC_Decode(const uint8_t* file, size_t fileSize, uint8_t* out, uint32_t outSize)
{
	if(fileSize < 0x20 || memcmp(file, kSdcMagic, 16) != 0)
		return 0;
	uint32_t key = Rd32(file + 0x10);
	uint32_t csize = Rd32(file + 0x14);
	uint32_t dsize = Rd32(file + 0x18);
	if((size_t)csize + 0x20 > fileSize || dsize > outSize)
		return 0;
	uint8_t* plain = (uint8_t*)malloc(csize ? csize : 1);
	uint32_t sum = 0, x = 0;
	for(uint32_t i = 0; i < csize; i++)
	{
		uint8_t c = file[0x20 + i];
		plain[i] = (uint8_t)(c - SDC_NextKey(&key));
		sum += c;
		x ^= c;
	}
	if((uint16_t)sum != Rd16(file + 0x1C) || (uint16_t)x != Rd16(file + 0x1E))
	{
		free(plain);
		return 0;
	}
	uint32_t in = 0, o = 0;
	while(in < csize)
	{
		uint8_t c = plain[in];
		if(c & 0x80)
		{
			if(in + 1 >= csize) break;
			uint32_t n = ((c >> 3) & 0x0F) + 2;
			uint32_t d = (((uint32_t)(c & 7) << 8) | plain[in + 1]) + 2;
			if(d > o || o + n > outSize) { free(plain); return 0; }
			for(uint32_t k = 0; k < n; k++, o++)
				out[o] = out[o - d];
			in += 2;
		}
		else
		{
			uint32_t n = (uint32_t)c + 1;
			if(in + 1 + n > csize || o + n > outSize) { free(plain); return 0; }
			memcpy(out + o, plain + in + 1, n);
			o += n;
			in += 1 + n;
		}
	}
	free(plain);
	return o == dsize ? o : 0;
}

// 0x00493B10. The original's compressor (0x00493530) searches a 0x801-byte window
// through hash chains and keeps the longest match; any encoding the decoder above
// reads back is the same file to the game, so this one is a plain greedy match
// over the same window and the same length limit (2 to 17 bytes), and the result
// is decoded and compared before it is answered, as 0x00493B82 does.
uint32_t SDC_Encode(const uint8_t* data, uint32_t size, uint32_t key, uint8_t** outFile)
{
	enum { MAXD = 0x801, MINL = 2, MAXL = 17, HBITS = 16 };
	uint8_t* lz = (uint8_t*)malloc((size_t)size + size / 128 + 16);
	int32_t* head = (int32_t*)malloc(sizeof(int32_t) << HBITS);
	int32_t* prev = (int32_t*)malloc(sizeof(int32_t) * (size ? size : 1));
	uint32_t o = 0, i = 0, litStart = 0;
	for(uint32_t k = 0; k < (1u << HBITS); k++) head[k] = -1;

#define HASH2(p) ((uint32_t)(((p)[0] << 8) ^ ((p)[1] * 0x9E37u)) & ((1u << HBITS) - 1))
#define FLUSH_LITERALS(end) do { \
		uint32_t s_ = litStart; \
		while(s_ < (end)) { \
			uint32_t n_ = (end) - s_; if(n_ > 128) n_ = 128; \
			lz[o++] = (uint8_t)(n_ - 1); memcpy(lz + o, data + s_, n_); o += n_; s_ += n_; \
		} } while(0)

	while(i < size)
	{
		uint32_t bestLen = 0, bestDist = 0;
		if(i + MINL <= size)
		{
			uint32_t h = HASH2(data + i);
			int32_t cand = head[h];
			int chain = 64;
			while(cand >= 0 && chain-- > 0)
			{
				uint32_t d = i - (uint32_t)cand;
				if(d > MAXD) break;
				if(d >= 2)
				{
					uint32_t l = 0;
					while(l < MAXL && i + l < size && data[cand + l] == data[i + l]) l++;
					if(l > bestLen) { bestLen = l; bestDist = d; if(l == MAXL) break; }
				}
				cand = prev[cand];
			}
			// A distance of one is not expressible; a run is copied from two back.
			if(bestLen < MAXL && i >= 2)
			{
				uint32_t l = 0;
				while(l < MAXL && i + l < size && data[i - 2 + l] == data[i + l]) l++;
				if(l > bestLen) { bestLen = l; bestDist = 2; }
			}
		}
		if(bestLen >= MINL)
		{
			FLUSH_LITERALS(i);
			uint32_t d = bestDist - 2;
			lz[o++] = (uint8_t)(0x80 | ((bestLen - 2) << 3) | (d >> 8));
			lz[o++] = (uint8_t)(d & 0xFF);
			for(uint32_t k = 0; k < bestLen; k++, i++)
				if(i + MINL <= size) { uint32_t h = HASH2(data + i); prev[i] = head[h]; head[h] = (int32_t)i; }
			litStart = i;
		}
		else
		{
			if(i + MINL <= size) { uint32_t h = HASH2(data + i); prev[i] = head[h]; head[h] = (int32_t)i; }
			i++;
		}
	}
	FLUSH_LITERALS(size);
#undef HASH2
#undef FLUSH_LITERALS
	free(head);
	free(prev);

	uint8_t* file = (uint8_t*)malloc(0x20 + (size_t)o);
	memcpy(file, kSdcMagic, 16);
	Wr32(file + 0x10, key);
	Wr32(file + 0x14, o);
	Wr32(file + 0x18, size);
	// 0x00493970: encipher (add the keystream) and sum and xor the result.
	uint32_t state = key, sum = 0, x = 0;
	for(uint32_t k = 0; k < o; k++)
	{
		uint8_t c = (uint8_t)(lz[k] + SDC_NextKey(&state));
		file[0x20 + k] = c;
		sum += c;
		x ^= c;
	}
	Wr16(file + 0x1C, (uint16_t)sum);
	Wr16(file + 0x1E, (uint16_t)x);
	free(lz);

	uint8_t* check = (uint8_t*)malloc(size ? size : 1);
	uint32_t back = SDC_Decode(file, 0x20 + (size_t)o, check, size);
	int same = back == size && memcmp(check, data, size) == 0;
	free(check);
	if(!same)
	{
		free(file);
		return 0;
	}
	*outFile = file;
	return 0x20 + o;
}

// ============================================================================
// String tables
// ============================================================================

typedef struct StrEntry { uint32_t hash; uint32_t length; char* text; } StrEntry_t;
typedef struct StrTable
{
	uint32_t id;
	uint32_t capacity;
	uint32_t count;
	StrEntry_t* entries;
	struct StrTable* next;
} StrTable_t;
static StrTable_t* gTables = NULL;

// 0x00452BC0: h = h * 0xE9 + (signed char)c over the string.
static uint32_t Hash(const char* s)
{
	uint32_t h = 0;
	for(; *s; s++)
		h = h * 0xE9u + (uint32_t)(int32_t)(signed char)*s;
	return h;
}

static StrTable_t* StrTab_Find(uint32_t id)
{
	for(StrTable_t* t = gTables; t; t = t->next)
		if(t->id == id)
			return t;
	return NULL;
}

static char* DupString(const char* s, uint32_t length)
{
	char* d = (char*)malloc(length);
	memcpy(d, s, length);
	return d;
}

uint32_t StrTab_Add(uint32_t id, const char* s)
{
	StrTable_t* t = StrTab_Find(id);
	if(!t)
	{
		t = (StrTable_t*)calloc(1, sizeof(StrTable_t));
		t->id = id;
		t->capacity = 0x20;
		t->entries = (StrEntry_t*)calloc(t->capacity, sizeof(StrEntry_t));
		t->next = gTables;
		gTables = t;
	}
	uint32_t h = Hash(s);
	for(uint32_t i = 0; i < t->count; i++)
		if(t->entries[i].hash == h && strcmp(t->entries[i].text, s) == 0)
			return i;
	if(t->count == t->capacity)
	{
		t->capacity *= 2;
		t->entries = (StrEntry_t*)realloc(t->entries, t->capacity * sizeof(StrEntry_t));
	}
	StrEntry_t* e = &t->entries[t->count];
	e->hash = h;
	e->length = (uint32_t)strlen(s) + 1;
	e->text = DupString(s, e->length);
	return t->count++;
}

uint32_t StrTab_Count(uint32_t id)
{
	StrTable_t* t = StrTab_Find(id);
	return t ? t->count : 0;
}

uint32_t StrTab_Remove(uint32_t id)
{
	StrTable_t** link = &gTables;
	while(*link && (*link)->id != id)
		link = &(*link)->next;
	if(!*link)
		return 0;
	StrTable_t* t = *link;
	*link = t->next;
	for(uint32_t i = 0; i < t->count; i++)
		free(t->entries[i].text);
	free(t->entries);
	free(t);
	return 1;
}

void StrTab_RemoveAll(int keepGlobal)
{
	StrTable_t** link = &gTables;
	while(*link)
	{
		if(keepGlobal && (*link)->id == STRTAB_GLOBAL_ID)
			link = &(*link)->next;
		else
			StrTab_Remove((*link)->id);
	}
}

uint32_t StrTab_Load(uint32_t id, uint32_t count, const char* data)
{
	if(count == 0)
	{
		StrTab_Remove(id);
		return 1;
	}
	if((int32_t)count < 1 || data == NULL)
		return 0;
	StrTab_Remove(id);
	uint32_t capacity = 0x20;
	while(capacity < count)
		capacity *= 2;
	StrTable_t* t = (StrTable_t*)calloc(1, sizeof(StrTable_t));
	t->id = id;
	t->capacity = capacity;
	t->count = count;
	t->entries = (StrEntry_t*)calloc(capacity, sizeof(StrEntry_t));
	t->next = gTables;
	gTables = t;
	for(uint32_t i = 0; i < count; i++)
	{
		StrEntry_t* e = &t->entries[i];
		e->hash = Hash(data);
		e->length = (uint32_t)strlen(data) + 1;
		e->text = DupString(data, e->length);
		data += e->length;
	}
	return 1;
}

uint32_t StrTab_Serialize(uint32_t id, uint8_t* out)
{
	StrTable_t* t = StrTab_Find(id);
	uint32_t total = 0;
	if(!t)
		return 0;
	for(uint32_t i = 0; i < t->count; i++)
	{
		if(out)
		{
			memcpy(out, t->entries[i].text, t->entries[i].length);
			out += t->entries[i].length;
		}
		total += t->entries[i].length;
	}
	return total;
}

uint32_t StrTab_Read(uint32_t id, int32_t index, char* out, uint32_t* length)
{
	StrTable_t* t = StrTab_Find(id);
	if(!t)
		return 0x80000001u;
	if(index < 0 || (uint32_t)index >= t->count)
		return 0x80000002u;
	if(out)
		memcpy(out, t->entries[index].text, t->entries[index].length);
	if(length)
		*length = t->entries[index].length - 1;
	return 0;
}

uint32_t StrTab_GlobalHas(const char* s)
{
	StrTable_t* t = StrTab_Find(STRTAB_GLOBAL_ID);
	if(!t)
		return 0x80000001u;
	uint32_t h = Hash(s);
	for(uint32_t i = 0; i < t->count; i++)
		if(t->entries[i].hash == h && strcmp(t->entries[i].text, s) == 0)
			return 0;
	return 1;
}

// ============================================================================
// Flag store
// ============================================================================

typedef struct FlagEntry
{
	uint32_t hash;
	char* name;
	uint32_t bits;
	uint8_t* data;
	struct FlagEntry* next;
} FlagEntry_t;
static FlagEntry_t* gFlags = NULL;

static uint32_t FlagBytes(uint32_t bits) { return (bits + 7) >> 3; }   // 0x00446BF0

// 0x00447120: the entry found is moved to the front of the list.
static FlagEntry_t* Flags_Find(const char* name)
{
	uint32_t h = Hash(name);
	FlagEntry_t** link = &gFlags;
	for(FlagEntry_t* f = gFlags; f; link = &f->next, f = f->next)
	{
		if(f->hash == h && strcmp(f->name, name) == 0)
		{
			*link = f->next;
			f->next = gFlags;
			gFlags = f;
			return f;
		}
	}
	return NULL;
}

uint32_t Flags_Define(const char* name, uint32_t bits)
{
	uint32_t bytes = FlagBytes(bits);
	if(bytes == 0)
		return 0x80000001u;
	FlagEntry_t* f = Flags_Find(name);
	if(!f)
	{
		f = (FlagEntry_t*)calloc(1, sizeof(FlagEntry_t));
		f->hash = Hash(name);
		f->name = DupString(name, (uint32_t)strlen(name) + 1);
		f->bits = bits;
		f->data = (uint8_t*)calloc(1, bytes);
		f->next = gFlags;
		gFlags = f;
		return 0;
	}
	uint8_t* data = (uint8_t*)calloc(1, bytes);
	uint32_t keep = FlagBytes(f->bits);
	memcpy(data, f->data, bytes < keep ? bytes : keep);
	free(f->data);
	f->bits = bits;
	f->data = data;
	return 0;
}

uint32_t Flags_Set(const char* name, uint32_t index, uint32_t value)
{
	FlagEntry_t* f = Flags_Find(name);
	if(!f)
		return 0x80000002u;
	if(index >= f->bits)
		return 0x80000003u;
	uint8_t mask = (uint8_t)(0x80 >> (index & 7));   // 0x00446C10
	if(value)
		f->data[index >> 3] |= mask;
	else
		f->data[index >> 3] &= (uint8_t)~mask;
	return 0;
}

uint32_t Flags_SetRange(const char* name, uint32_t start, uint32_t count, uint32_t value)
{
	FlagEntry_t* f = Flags_Find(name);
	if(!f)
		return 0x80000002u;
	if(start >= f->bits)
		return 0x80000003u;
	if(count - 1 > 0xFFFF || start + count > f->bits)
		return 0x80000004u;
	for(uint32_t i = start; i < start + count; i++)
	{
		uint8_t mask = (uint8_t)(0x80 >> (i & 7));
		if(value)
			f->data[i >> 3] |= mask;
		else
			f->data[i >> 3] &= (uint8_t)~mask;
	}
	return 0;
}

uint32_t Flags_Get(const char* name, uint32_t index, uint32_t* value)
{
	FlagEntry_t* f = Flags_Find(name);
	if(!f)
		return 0x80000002u;
	if(index >= f->bits)
		return 0x80000003u;
	*value = (f->data[index >> 3] & (0x80 >> (index & 7))) ? 1 : 0;
	return 0;
}

void Flags_Clear(void)
{
	while(gFlags)
	{
		FlagEntry_t* f = gFlags;
		gFlags = f->next;
		free(f->name);
		free(f->data);
		free(f);
	}
}

// 0x00446F20: the entry count, then per entry in list order its name with the
// NUL, its bit count and its bytes. Answers the size; writes only when out is set.
static uint32_t Flags_Serialize(uint8_t* out)
{
	uint32_t size = 4, count = 0;
	uint8_t* p = out ? out + 4 : NULL;
	for(FlagEntry_t* f = gFlags; f; f = f->next, count++)
	{
		uint32_t nameLength = (uint32_t)strlen(f->name) + 1;
		uint32_t bytes = FlagBytes(f->bits);
		if(p)
		{
			memcpy(p, f->name, nameLength);
			p += nameLength;
			Wr32(p, f->bits);
			p += 4;
			memcpy(p, f->data, bytes);
			p += bytes;
		}
		size += nameLength + 4 + bytes;
	}
	if(out)
		Wr32(out, count);
	return size;
}

// 0x00446FE0: the entries are defined from the last to the first, each one
// prepended, so the list comes back in the order it was written in.
static void Flags_Deserialize(const uint8_t* in, const uint8_t* end)
{
	if(end - in < 4)
		return;
	uint32_t count = Rd32(in);
	const char** names = (const char**)calloc(count ? count : 1, sizeof(char*));
	const uint8_t** datas = (const uint8_t**)calloc(count ? count : 1, sizeof(uint8_t*));
	uint32_t* bits = (uint32_t*)calloc(count ? count : 1, sizeof(uint32_t));
	const uint8_t* p = in + 4;
	uint32_t parsed = 0;
	for(; parsed < count; parsed++)
	{
		const uint8_t* nul = memchr(p, 0, end - p);
		if(!nul || end - (nul + 1) < 4)
			break;
		names[parsed] = (const char*)p;
		p = nul + 1;
		bits[parsed] = Rd32(p);
		p += 4;
		datas[parsed] = p;
		if((size_t)(end - p) < FlagBytes(bits[parsed]))
			break;
		p += FlagBytes(bits[parsed]);
	}
	for(uint32_t i = parsed; i-- > 0; )
	{
		if(Flags_Define(names[i], bits[i]) == 0)
		{
			FlagEntry_t* f = Flags_Find(names[i]);
			memcpy(f->data, datas[i], FlagBytes(bits[i]));
		}
	}
	free(names);
	free(datas);
	free(bits);
}

// ============================================================================
// Persistent block
// ============================================================================

static uint8_t* gPersistent = NULL;

uint8_t* Persistent_Block(void)
{
	if(!gPersistent)
		gPersistent = (uint8_t*)calloc(1, PERSISTENT_SIZE);
	return gPersistent;
}

// ============================================================================
// The database
// ============================================================================

static const char kGdbFile[] = "BGI.gdb";   // 0x004E70F0, beside the game

uint32_t GDB_Load(uint8_t* globalMem, uint32_t globalSize, int32_t* left, int32_t* top)
{
	FILE* f = fopen(kGdbFile, "rb");
	if(!f)
		return 0x80000001u;
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* file = (uint8_t*)malloc(fileSize > 0 ? fileSize : 1);
	size_t got = fread(file, 1, fileSize > 0 ? fileSize : 0, f);
	fclose(f);

	uint32_t dsize = SDC_DecodedSize(file, got);
	uint8_t* d = dsize ? (uint8_t*)malloc(dsize) : NULL;
	if(!d || SDC_Decode(file, got, d, dsize) != dsize)
	{
		free(file);
		free(d);
		printf("[GDB]: %s is not an SDC file this engine can decode\n", kGdbFile);
		return 0x80000002u;
	}
	free(file);

	// Only the "BURIKO GDB 3.00" layout is read. The original also migrates an
	// older layout (0x0046B9A5, a file of at least 0x70408 bytes with no such
	// signature) which no game of this engine generation writes; it is refused
	// by name here rather than guessed.
	if(dsize < 0x20 || memcmp(d, kGdbMagic, 16) != 0)
	{
		printf("[GDB]: %s is not a \"BURIKO GDB 3.00\" database (the older layout 0x0046B9A5 reads is not implemented)\n", kGdbFile);
		free(d);
		return 0x80000002u;
	}
	const uint8_t* end = d + dsize;
	const uint8_t* h = d + 0x10;
	if(Rd32(h) != dsize)
	{
		free(d);
		return 0x80000002u;
	}
	if(left) *left = (int32_t)Rd32(h + 4);
	if(top) *top = (int32_t)Rd32(h + 8);

	// The first block goes over the start of global memory.
	uint32_t n = Rd32(h + 0xC);
	const uint8_t* p = h + 0x10;
	if(p + n > end) { free(d); return 0x80000002u; }
	if(globalMem)
		memcpy(globalMem, p, n < globalSize ? n : globalSize);
	p += n;

	// The persistent block: what the file holds, the rest of the megabyte cleared.
	if(p + 4 > end) { free(d); return 0x80000002u; }
	n = Rd32(p);
	p += 4;
	if(p + n > end || n > PERSISTENT_SIZE) { free(d); return 0x80000002u; }
	uint8_t* block = Persistent_Block();
	memcpy(block, p, n);
	if(n < PERSISTENT_SIZE)
		memset(block + n, 0, PERSISTENT_SIZE - n);
	p += n;

	// String table 0x80000000, then the flag store.
	if(p + 4 > end) { free(d); return 0x80000002u; }
	uint32_t count = Rd32(p);
	p += 4;
	StrTab_Load(STRTAB_GLOBAL_ID, count, (const char*)p);
	p += StrTab_Serialize(STRTAB_GLOBAL_ID, NULL);
	Flags_Clear();
	if(p <= end)
		Flags_Deserialize(p, end);
	free(d);
	printf("[GDB]: read %s: %u strings, %u bytes\n", kGdbFile, count, dsize);
	return 0;
}

uint32_t GDB_Save(const uint8_t* globalMem, uint32_t globalSize)
{
	uint32_t strings = StrTab_Serialize(STRTAB_GLOBAL_ID, NULL) + 4;
	uint32_t flags = Flags_Serialize(NULL);
	uint32_t total = 0x100424 + strings + flags;
	uint8_t* d = (uint8_t*)calloc(1, total);

	memcpy(d, kGdbMagic, 16);
	uint8_t* h = d + 0x10;
	Wr32(h, total);
	// GetWindowRect's left and top in the original (0x0046B6D3). The engine's
	// window is the whole display, whose corner is 0, 0.
	Wr32(h + 4, 0);
	Wr32(h + 8, 0);
	Wr32(h + 0xC, 0x400);
	if(globalMem)
		memcpy(h + 0x10, globalMem, globalSize < 0x400 ? globalSize : 0x400);
	Wr32(h + 0x410, PERSISTENT_SIZE);
	memcpy(h + 0x414, Persistent_Block(), PERSISTENT_SIZE);
	uint8_t* p = h + 0x414 + PERSISTENT_SIZE;
	Wr32(p, StrTab_Count(STRTAB_GLOBAL_ID));
	p += 4;
	p += StrTab_Serialize(STRTAB_GLOBAL_ID, p);
	Flags_Serialize(p);

	// The key is the millisecond of the clock (0x00493970 reads GetSystemTime's
	// wMilliseconds); any key reads back the same.
	uint8_t* file = NULL;
	uint32_t fileSize = SDC_Encode(d, total, (uint32_t)(clock() % 1000), &file);
	free(d);
	if(!fileSize)
	{
		printf("[GDB]: SDC encoding failed\n");
		return 0;
	}
	FILE* f = fopen(kGdbFile, "wb");
	uint32_t written = 0;
	if(f)
	{
		written = (uint32_t)fwrite(file, 1, fileSize, f);
		fclose(f);
	}
	free(file);
	printf("[GDB]: wrote %s, %u bytes (%u decoded)\n", kGdbFile, written, total);
	return written == fileSize;
}

void GDB_FreeAll(void)
{
	StrTab_RemoveAll(0);
	Flags_Clear();
	free(gPersistent);
	gPersistent = NULL;
}
