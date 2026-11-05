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
	// The most a byte can cost is two: a run of one literal is its control byte and the
	// byte. (This was size + size / 128 + 16, which short matches between single literals
	// overrun: Droidtop/tracker#123, found by the DCFS round trip under ASan.)
	uint8_t* lz = (uint8_t*)malloc((size_t)size * 2 + 16);
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
// DCFS (0x00447220 encodes, 0x00447410 decodes)
// ============================================================================
static const char kDcfsMagic[16] = { 'D','C','F','S',' ','F','O','R','M','A','T',' ','1','.','0','0' };

static size_t PutLeb(uint8_t* p, uint32_t v)
{
	size_t n = 0;
	do
	{
		uint8_t b = (uint8_t)(v & 0x7F);
		v >>= 7;
		p[n++] = (uint8_t)(b | (v != 0 ? 0x80 : 0));
	} while(v != 0);
	return n;
}

// 1 and the value, or 0 when the stream ends inside it or it does not fit 32 bits.
static int GetLeb(const uint8_t* p, size_t end, size_t* at, uint32_t* value)
{
	uint32_t v = 0;
	for(int shift = 0; shift < 35; shift += 7)
	{
		if(*at >= end)
			return 0;
		uint8_t b = p[(*at)++];
		if(shift == 28 && (b & 0x70) != 0)
			return 0;
		v |= (uint32_t)(b & 0x7F) << shift;
		if((b & 0x80) == 0)
		{
			*value = v;
			return 1;
		}
	}
	return 0;
}

uint32_t DCFS_Decode(const uint8_t* data, size_t dataSize, uint8_t* out, size_t outSize)
{
	if(dataSize < DCFS_HEADER_SIZE || memcmp(data, kDcfsMagic, 16) != 0)
		return DCFS_NOT_DCFS;
	uint32_t size = Rd32(data + 0x10);
	uint32_t count = Rd32(data + 0x14);
	if(size == 0 || count == 0 || (uint64_t)size * count > outSize)
		return DCFS_BAD_RUNS;
	size_t at = DCFS_HEADER_SIZE;
	if(dataSize - at < size)
		return DCFS_BAD_RUNS;
	memcpy(out, data + at, size);
	at += size;
	for(uint32_t r = 1; r < count; r++)
	{
		const uint8_t* prev = out + (size_t)(r - 1) * size;
		uint8_t* cur = out + (size_t)r * size;
		uint32_t filled = 0;
		int same = 1;
		while(filled < size)
		{
			uint32_t n;
			if(!GetLeb(data, dataSize, &at, &n) || n > size - filled)
				return DCFS_BAD_RUNS;
			if(same)
				memcpy(cur + filled, prev + filled, n);
			else
			{
				if(dataSize - at < n)
					return DCFS_BAD_RUNS;
				memcpy(cur + filled, data + at, n);
				at += n;
			}
			filled += n;
			same = !same;
		}
	}
	return 0;
}

uint32_t DCFS_Encode(const uint8_t* records, uint32_t size, uint32_t count, uint8_t** out, size_t* outSize)
{
	*out = NULL;
	*outSize = 0;
	if(count == 0)
		return DCFS_ERR_COUNT;
	if(size == 0)
		return DCFS_ERR_SIZE;
	// Worst case: every later record all literal, one length of at most 5 bytes per run.
	uint64_t worst = (uint64_t)DCFS_HEADER_SIZE + (uint64_t)size * count + (uint64_t)(count - 1) * 10 + 16;
	if(worst > 0x40000000u)
		return DCFS_ERR_SIZE;
	uint8_t* buf = (uint8_t*)malloc((size_t)worst);
	if(buf == NULL)
		return 0xFFFFFFFFu;
	memcpy(buf, kDcfsMagic, 16);
	Wr32(buf + 0x10, size);
	Wr32(buf + 0x14, count);
	size_t at = DCFS_HEADER_SIZE;
	memcpy(buf + at, records, size);
	at += size;
	for(uint32_t r = 1; r < count; r++)
	{
		const uint8_t* prev = records + (size_t)(r - 1) * size;
		const uint8_t* cur = records + (size_t)r * size;
		uint32_t i = 0;
		while(i < size)
		{
			uint32_t start = i;
			while(i < size && cur[i] == prev[i])
				i++;
			at += PutLeb(buf + at, i - start);
			if(i == size)
				break;
			start = i;
			while(i < size && cur[i] != prev[i])
				i++;
			at += PutLeb(buf + at, i - start);
			memcpy(buf + at, cur + start, i - start);
			at += i - start;
		}
	}
	// The original decodes what it wrote and compares before answering.
	uint8_t* check = (uint8_t*)malloc((size_t)size * count);
	int same = check != NULL && DCFS_Decode(buf, at, check, (size_t)size * count) == 0
	           && memcmp(check, records, (size_t)size * count) == 0;
	free(check);
	if(!same)
	{
		free(buf);
		return 0xFFFFFFFFu;
	}
	*out = buf;
	*outSize = at;
	return 0;
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

// Set when BGI.gdb is there but this engine could not read it (a layout it does not
// read yet, or damage). The original would read it - or migrate the older layout -
// and write it back, so the player's global data survives. Writing this engine's own
// database over a file it could not read would erase that data (Houkago's
// clear data, RE lane on Droidtop/tracker#123), so GDB_Save keeps such a file.
static int gGdbUnreadable = 0;

static uint32_t GDB_Read(uint8_t* globalMem, uint32_t globalSize, int32_t* left, int32_t* top);

// Which layout BGI.gdb was read in, so a save writes it back the same way: a game's own
// runtime reads only its own layout (1.580 rejects a 3.00 file, RE lane on #123). The
// older layout also keeps the window position it was read with, since this engine's
// window has none of its own to write.
static int gGdbOldLayout = 0;
static int32_t gGdbOldLeft = 0;
static int32_t gGdbOldTop = 0;

uint32_t GDB_Load(uint8_t* globalMem, uint32_t globalSize, int32_t* left, int32_t* top)
{
	gGdbOldLayout = 0;
	uint32_t status = GDB_Read(globalMem, globalSize, left, top);
	gGdbUnreadable = status == 0x80000002u;
	return status;
}

// The older BGI.gdb layout, which 0x0046B800's else-branch reads when the decoded file
// has no "BURIKO GDB 3.00" signature (Houkago Shippo Days, BGI 1.580; layout and
// rules from the RE lane on Droidtop/tracker#123). Fixed offsets, no signature:
//   +0x00 s32 window x, +0x04 s32 window y;
//   +0x08 0x400 bytes of global memory;
//   +0x408 0x40000 bytes of the persistent block (the rest of its megabyte zeroed);
//   +0x40408 string slots of 32 bytes, 0x1000 of them (short form) or 0x4000 (long);
//   then 0x800 flag records of 32 bytes - a 24-byte name, +0x18 u32 bit count (0 =
//   unused), +0x1C u32 BYTE offset of its data from the bit data - and the bit data.
// Each record's data is (bits + 7) >> 3 whole bytes copied as they are (0x00446D60 ->
// 0x00446C60), so the flag store keeps the same bytes the 3.00 layout writes.
// Writing is always the 3.00 layout. Anything that does not fit answers 0x80000002,
// so GDB_Save keeps the file rather than replace it.
#define GDB_OLD_SLOTS        0x40408u
#define GDB_OLD_SLOT_SIZE    32u
#define GDB_OLD_SHORT_SLOTS  0x1000u
#define GDB_OLD_LONG_SLOTS   0x4000u
#define GDB_OLD_RECORDS      0x800u
#define GDB_OLD_RECORD_SIZE  32u
#define GDB_OLD_MIN_SIZE     0x70408u

static uint32_t GDB_ReadOldLayout(const uint8_t* d, uint32_t dsize, uint8_t* globalMem, uint32_t globalSize,
                                  int32_t* left, int32_t* top)
{
	if(dsize < GDB_OLD_MIN_SIZE)
	{
		printf("[GDB]: %s has neither layout: %u bytes, the older one needs 0x%X\n", kGdbFile, dsize, GDB_OLD_MIN_SIZE);
		return 0x80000002u;
	}

	// Short or long: the place where the short form's flag records would start is
	// read as if it were one; any of these says it is still string slots instead.
	const uint8_t* probe = d + GDB_OLD_SLOTS + GDB_OLD_SHORT_SLOTS * GDB_OLD_SLOT_SIZE;   // +0x60408
	size_t probeLength = strnlen((const char*)probe, GDB_OLD_SLOT_SIZE);
	int longForm = probe[0] == 0 || probeLength >= 24 || Rd32(probe + 0x18) == 0 || Rd32(probe + 0x1C) != 0;
	uint32_t slots = longForm ? GDB_OLD_LONG_SLOTS : GDB_OLD_SHORT_SLOTS;
	uint32_t records = GDB_OLD_SLOTS + slots * GDB_OLD_SLOT_SIZE;          // +0x60408 or +0xC0408
	uint32_t bitData = records + GDB_OLD_RECORDS * GDB_OLD_RECORD_SIZE;     // +0x70408 or +0xD0408
	if(dsize < bitData)
	{
		printf("[GDB]: %s looks like the older layout's long form but ends at 0x%X, before 0x%X\n",
		       kGdbFile, dsize, bitData);
		return 0x80000002u;
	}
	// Every record's data has to be inside the file before anything is taken over.
	for(uint32_t i = 0; i < GDB_OLD_RECORDS; i++)
	{
		const uint8_t* r = d + records + i * GDB_OLD_RECORD_SIZE;
		uint32_t bits = Rd32(r + 0x18);
		if(bits == 0)
			continue;
		uint64_t start = (uint64_t)bitData + Rd32(r + 0x1C);
		if(start + FlagBytes(bits) > dsize)
		{
			printf("[GDB]: %s: flag record %u runs past the end of the file\n", kGdbFile, i);
			return 0x80000002u;
		}
	}

	gGdbOldLeft = (int32_t)Rd32(d + 0x00);
	gGdbOldTop = (int32_t)Rd32(d + 0x04);
	gGdbOldLayout = 1;
	if(left) *left = gGdbOldLeft;
	if(top) *top = gGdbOldTop;
	if(globalMem)
		memcpy(globalMem, d + 0x08, globalSize < 0x400 ? globalSize : 0x400);
	uint8_t* block = Persistent_Block();
	memcpy(block, d + 0x408, 0x40000);
	memset(block + 0x40000, 0, PERSISTENT_SIZE - 0x40000);

	// The non-empty slots, in order, become table 0x80000000.
	char* packed = (char*)malloc((size_t)slots * GDB_OLD_SLOT_SIZE);
	if(packed == NULL)
		return 0x80000002u;
	size_t used = 0;
	uint32_t count = 0;
	for(uint32_t i = 0; i < slots; i++)
	{
		const char* slot = (const char*)(d + GDB_OLD_SLOTS + i * GDB_OLD_SLOT_SIZE);
		size_t n = strnlen(slot, GDB_OLD_SLOT_SIZE - 1);
		if(n == 0)
			continue;
		memcpy(packed + used, slot, n);
		packed[used + n] = 0;
		used += n + 1;
		count++;
	}
	StrTab_Load(STRTAB_GLOBAL_ID, count, packed);
	free(packed);

	Flags_Clear();
	uint32_t defined = 0;
	for(uint32_t i = 0; i < GDB_OLD_RECORDS; i++)
	{
		const uint8_t* r = d + records + i * GDB_OLD_RECORD_SIZE;
		uint32_t bits = Rd32(r + 0x18);
		if(bits == 0)
			continue;
		// A name runs to its NUL within 0x18 bytes: one of more than 0x17 characters is
		// invalid to 1.580's reader (RE lane on #123) and is skipped here too.
		char name[24];
		size_t n = strnlen((const char*)r, 24);
		if(n > 0x17)
			continue;
		memcpy(name, r, n);
		name[n] = 0;
		if(Flags_Define(name, bits) != 0)
			continue;
		FlagEntry_t* f = Flags_Find(name);
		memcpy(f->data, d + bitData + Rd32(r + 0x1C), FlagBytes(bits));
		defined++;
	}
	printf("[GDB]: read %s in the older layout (%s form): %u strings, %u flag arrays, %u bytes\n",
	       kGdbFile, longForm ? "long" : "short", count, defined, dsize);
	return 0;
}

static uint32_t GDB_Read(uint8_t* globalMem, uint32_t globalSize, int32_t* left, int32_t* top)
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

	// "BURIKO GDB 3.00", or the older layout without a signature (0x0046B9A5).
	if(dsize < 0x20 || memcmp(d, kGdbMagic, 16) != 0)
	{
		uint32_t status = GDB_ReadOldLayout(d, dsize, globalMem, globalSize, left, top);
		free(d);
		return status;
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

/*
 * The older layout written back, as 1.580's writer (0x00455710) writes it: always the
 * long form, one flat image wrapped in SDC, no 3.00 header (RE lane on #123):
 *   +0 the window position; +8 0x400 bytes of global memory; +0x408 0x40000 bytes of
 *   the persistent block; +0x40408 0x4000 string slots of 32 bytes, table 0x80000000's
 *   strings in order; +0xC0408 0x800 flag slots of 32 bytes (name up to 0x17 characters,
 *   +0x18 bit count, +0x1C byte offset), the rest zero; +0xD0408 each flag array's
 *   (bits + 7) >> 3 bytes, back to back in slot order.
 * What does not fit those fixed slots is logged and left out - a string of 32 bytes or
 * more is cut to 31, strings past 0x4000 and flag arrays past 0x800 or with a longer
 * name are dropped - which is all the original's own file can hold.
 */
static uint32_t GDB_WriteOldLayout(const uint8_t* globalMem, uint32_t globalSize)
{
	uint32_t data = 0;
	for(FlagEntry_t* f = gFlags; f; f = f->next)
		data += FlagBytes(f->bits);
	uint32_t total = 0xD0408u + data;
	uint8_t* d = (uint8_t*)calloc(1, total);
	if(d == NULL)
		return 0;
	Wr32(d + 0x00, (uint32_t)gGdbOldLeft);
	Wr32(d + 0x04, (uint32_t)gGdbOldTop);
	if(globalMem)
		memcpy(d + 0x08, globalMem, globalSize < 0x400 ? globalSize : 0x400);
	memcpy(d + 0x408, Persistent_Block(), 0x40000);

	uint32_t strings = StrTab_Count(STRTAB_GLOBAL_ID), cut = 0;
	if(strings > GDB_OLD_LONG_SLOTS)
	{
		printf("[GDB]: %u strings, the older layout holds 0x%X: the rest are left out\n", strings, GDB_OLD_LONG_SLOTS);
		strings = GDB_OLD_LONG_SLOTS;
	}
	for(uint32_t i = 0; i < strings; i++)
	{
		uint32_t length = 0;
		if(StrTab_Read(STRTAB_GLOBAL_ID, (int32_t)i, NULL, &length) != 0)
			continue;
		char* text = (char*)malloc(length + 1);
		if(text == NULL)
			continue;
		StrTab_Read(STRTAB_GLOBAL_ID, (int32_t)i, text, NULL);
		if(length >= GDB_OLD_SLOT_SIZE)
		{
			length = GDB_OLD_SLOT_SIZE - 1;
			cut++;
		}
		memcpy(d + GDB_OLD_SLOTS + i * GDB_OLD_SLOT_SIZE, text, length);
		free(text);
	}
	if(cut)
		printf("[GDB]: %u strings were longer than a slot and were cut to 31 bytes\n", cut);

	uint32_t slot = 0, offset = 0, dropped = 0;
	for(FlagEntry_t* f = gFlags; f; f = f->next)
	{
		size_t nameLength = strlen(f->name);
		if(slot >= GDB_OLD_RECORDS || nameLength > 0x17 || f->bits == 0)
		{
			dropped++;
			continue;
		}
		uint8_t* r = d + 0xC0408u + slot * GDB_OLD_RECORD_SIZE;
		memcpy(r, f->name, nameLength);
		Wr32(r + 0x18, f->bits);
		Wr32(r + 0x1C, offset);
		memcpy(d + 0xD0408u + offset, f->data, FlagBytes(f->bits));
		offset += FlagBytes(f->bits);
		slot++;
	}
	if(dropped)
		printf("[GDB]: %u flag arrays do not fit the older layout and were left out\n", dropped);
	total = 0xD0408u + offset;

	uint8_t* file = NULL;
	uint32_t fileSize = SDC_Encode(d, total, (uint32_t)(clock() % 1000), &file);
	free(d);
	if(!fileSize)
	{
		printf("[GDB]: SDC encoding failed\n");
		return 0;
	}
	FILE* out = fopen(kGdbFile, "wb");
	uint32_t written = 0;
	if(out)
	{
		written = (uint32_t)fwrite(file, 1, fileSize, out);
		fclose(out);
	}
	free(file);
	printf("[GDB]: wrote %s in the older layout (long form), %u bytes (%u decoded)\n", kGdbFile, written, total);
	return written == fileSize;
}

uint32_t GDB_Save(const uint8_t* globalMem, uint32_t globalSize)
{
	if(gGdbUnreadable)
	{
		printf("[GDB]: %s is kept as it is: it is there but could not be read, and writing would replace"
		       " the player's global data\n", kGdbFile);
		return 0;
	}
	if(gGdbOldLayout)
		return GDB_WriteOldLayout(globalMem, globalSize);
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
