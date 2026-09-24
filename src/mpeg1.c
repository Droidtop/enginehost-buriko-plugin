#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpeg1.h"

/*
 * The audio decoder is minimp3 (CC0, vendor/minimp3.h), built here with all
 * three layers: BGI's movies carry MPEG-1 layer II. Its own warnings are not
 * ours to fix, so they are silenced around the include, as audio.c does for
 * stb_vorbis.
 */
#define MINIMP3_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../vendor/minimp3.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------------- */
/* Stream queues                                                             */
/* ------------------------------------------------------------------------- */

/*
 * One elementary stream as the demuxer found it: the payloads of its PES
 * packets, in file order, each with its PTS (90 kHz, -1 when the packet has
 * none). The payloads are not copied at open; they are appended to the
 * stream's buffer as the decoder asks for bytes, and dropped again once read,
 * so only a little more than one picture (or a few audio frames) is ever held.
 */
typedef struct
{
	uint32_t offset;
	uint32_t length;
	int64_t  pts;
} Packet_t;

/* A PTS and the stream position (bytes of this stream from its start) of the
   first payload byte of the packet that carried it. */
typedef struct
{
	uint64_t position;
	int64_t  pts;
} Mark_t;

/* Zeroed bytes kept after the buffered data, so the bit reader may load eight
   bytes at any position up to a coefficient past the end without checking. */
#define QUEUE_PAD 64

typedef struct
{
	Packet_t* packets;
	int       count;
	int       capacity;
	int       next;     /* the next packet to append */

	uint8_t*  buf;
	size_t    len;
	size_t    cap;
	uint64_t  base;     /* stream position of buf[0] */

	Mark_t*   marks;
	int       markHead;
	int       markCount;
	int       markCap;
} Queue_t;

static int Queue_AddPacket(Queue_t* q, uint32_t offset, uint32_t length, int64_t pts)
{
	if(q->count == q->capacity)
	{
		int n = q->capacity ? q->capacity * 2 : 1024;
		Packet_t* p = realloc(q->packets, n * sizeof(Packet_t));
		if(!p)
			return 0;
		q->packets = p;
		q->capacity = n;
	}
	q->packets[q->count].offset = offset;
	q->packets[q->count].length = length;
	q->packets[q->count].pts = pts;
	q->count++;
	return 1;
}

/* Appends the next packet's payload. Returns 0 when there is none left (or
   memory ran out, which ends the stream the same way). */
static int Queue_Append(Queue_t* q, const uint8_t* data)
{
	const Packet_t* p;
	if(q->next >= q->count)
		return 0;
	p = &q->packets[q->next++];
	if(q->len + p->length + QUEUE_PAD > q->cap)
	{
		size_t n = q->cap ? q->cap : 65536;
		uint8_t* b;
		while(n < q->len + p->length + QUEUE_PAD)
			n *= 2;
		b = realloc(q->buf, n);
		if(!b)
			return 0;
		q->buf = b;
		q->cap = n;
	}
	if(p->pts >= 0)
	{
		if(q->markHead + q->markCount == q->markCap)
		{
			if(q->markHead > 0)
			{
				memmove(q->marks, q->marks + q->markHead, q->markCount * sizeof(Mark_t));
				q->markHead = 0;
			}
			if(q->markCount == q->markCap)
			{
				int n = q->markCap ? q->markCap * 2 : 64;
				Mark_t* mk = realloc(q->marks, n * sizeof(Mark_t));
				if(!mk)
					return 0;
				q->marks = mk;
				q->markCap = n;
			}
		}
		q->marks[q->markHead + q->markCount].position = q->base + q->len;
		q->marks[q->markHead + q->markCount].pts = p->pts;
		q->markCount++;
	}
	memcpy(q->buf + q->len, data + p->offset, p->length);
	q->len += p->length;
	memset(q->buf + q->len, 0, QUEUE_PAD);
	return 1;
}

/* Drops the first n buffered bytes. */
static void Queue_Discard(Queue_t* q, size_t n)
{
	if(n == 0)
		return;
	memmove(q->buf, q->buf + n, q->len - n + QUEUE_PAD);
	q->len -= n;
	q->base += n;
}

/*
 * The PTS that belongs to an access unit starting at stream position pos: in
 * ISO/IEC 11172-1 a packet's PTS is that of the first access unit that
 * commences in it, so it is the latest mark at or before pos, unless an
 * earlier access unit already took it. -1 when there is none.
 */
static int64_t Queue_TakePts(Queue_t* q, uint64_t pos)
{
	int64_t pts = -1;
	while(q->markCount > 0 && q->marks[q->markHead].position <= pos)
	{
		pts = q->marks[q->markHead].pts;
		q->markHead++;
		q->markCount--;
	}
	if(q->markCount == 0)
		q->markHead = 0;
	return pts;
}

/* ------------------------------------------------------------------------- */
/* Bit reader                                                                */
/* ------------------------------------------------------------------------- */

typedef struct
{
	const uint8_t* p;
	size_t pos;    /* in bits */
	size_t end;    /* in bits: where the current slice or header stops */
} Bits_t;

/* The next n bits (1..32) without consuming them. Loads eight bytes big-endian
   at the byte holding pos; the buffer's padding makes that safe. */
static inline uint32_t Bits_Peek(const Bits_t* b, int n)
{
	uint64_t w;
	memcpy(&w, b->p + (b->pos >> 3), 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#else
	w = __builtin_bswap64(w);
#endif
	return (uint32_t)((w << (b->pos & 7)) >> (64 - n));
}

static inline uint32_t Bits_Get(Bits_t* b, int n)
{
	uint32_t v = Bits_Peek(b, n);
	b->pos += n;
	return v;
}

static inline void Bits_Skip(Bits_t* b, int n)
{
	b->pos += n;
}

/* ------------------------------------------------------------------------- */
/* Variable-length codes                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Every table below is transcribed from ISO/IEC 11172-2 Annex B as the code
 * strings the standard prints, and turned into lookup tables when a decoder is
 * opened. A lookup peeks as many bits as the table's longest code: the first
 * eight index a 256-entry table whose entries either decode directly or point
 * at a second table indexed by the remaining bits. The builder refuses a table
 * in which two codes overlap, so a transcription slip that broke the prefix
 * property could not go unnoticed.
 */
typedef struct
{
	const char* code;
	int value;
} VlcCode_t;

typedef struct
{
	int16_t value;
	uint8_t len;     /* 0: no code; 0xFF: value is a second-level table */
} Vlc_t;

typedef struct
{
	Vlc_t* t;
	int bits;        /* the longest code, at least 8 */
} VlcTable_t;

#define VLC_ERROR (-32768)

static int Vlc_Build(VlcTable_t* t, const VlcCode_t* codes, int count)
{
	int16_t subIndex[256];
	int i, subs = 0, maxLen = 8, subBits;
	for(i = 0; i < count; i++)
	{
		int len = (int)strlen(codes[i].code);
		if(len > maxLen)
			maxLen = len;
	}
	subBits = maxLen - 8;
	for(i = 0; i < 256; i++)
		subIndex[i] = -1;
	for(i = 0; i < count; i++)
	{
		const char* s = codes[i].code;
		int len = (int)strlen(s), k;
		uint32_t code = 0;
		for(k = 0; k < len; k++)
			code = (code << 1) | (uint32_t)(s[k] == '1');
		if(len > 8 && subIndex[code >> (len - 8)] < 0)
			subIndex[code >> (len - 8)] = (int16_t)subs++;
	}
	t->bits = maxLen;
	t->t = calloc(256 + ((size_t)subs << subBits), sizeof(Vlc_t));
	if(!t->t)
		return 0;
	for(i = 0; i < count; i++)
	{
		const char* s = codes[i].code;
		int len = (int)strlen(s), k, first, n;
		uint32_t code = 0;
		Vlc_t* dst;
		for(k = 0; k < len; k++)
			code = (code << 1) | (uint32_t)(s[k] == '1');
		if(len <= 8)
		{
			dst = t->t;
			first = (int)(code << (8 - len));
			n = 1 << (8 - len);
		}
		else
		{
			Vlc_t* e = &t->t[code >> (len - 8)];
			if(e->len != 0 && e->len != 0xFF)
				return 0;
			e->len = 0xFF;
			e->value = subIndex[code >> (len - 8)];
			dst = t->t + 256 + ((size_t)e->value << subBits);
			first = (int)((code & ((1u << (len - 8)) - 1)) << (maxLen - len));
			n = 1 << (maxLen - len);
		}
		for(k = first; k < first + n; k++)
		{
			if(dst[k].len != 0)
				return 0;
			dst[k].value = (int16_t)codes[i].value;
			dst[k].len = (uint8_t)len;
		}
	}
	return 1;
}

static inline int Vlc_Get(Bits_t* b, const VlcTable_t* t)
{
	uint32_t w = Bits_Peek(b, t->bits);
	Vlc_t e = t->t[w >> (t->bits - 8)];
	if(e.len == 0xFF)
		e = t->t[256 + ((size_t)e.value << (t->bits - 8)) + (w & ((1u << (t->bits - 8)) - 1))];
	if(e.len == 0)
		return VLC_ERROR;
	b->pos += e.len;
	return e.value;
}

/* B.1: macroblock_address_increment, with macroblock_stuffing and
   macroblock_escape. */
#define MBAI_STUFFING 34
#define MBAI_ESCAPE   35
static const VlcCode_t kMbAddressIncrement[] = {
	{ "1", 1 },              { "011", 2 },            { "010", 3 },
	{ "0011", 4 },           { "0010", 5 },           { "00011", 6 },
	{ "00010", 7 },          { "0000111", 8 },        { "0000110", 9 },
	{ "00001011", 10 },      { "00001010", 11 },      { "00001001", 12 },
	{ "00001000", 13 },      { "00000111", 14 },      { "00000110", 15 },
	{ "0000010111", 16 },    { "0000010110", 17 },    { "0000010101", 18 },
	{ "0000010100", 19 },    { "0000010011", 20 },    { "0000010010", 21 },
	{ "00000100011", 22 },   { "00000100010", 23 },   { "00000100001", 24 },
	{ "00000100000", 25 },   { "00000011111", 26 },   { "00000011110", 27 },
	{ "00000011101", 28 },   { "00000011100", 29 },   { "00000011011", 30 },
	{ "00000011010", 31 },   { "00000011001", 32 },   { "00000011000", 33 },
	{ "00000001111", MBAI_STUFFING },
	{ "00000001000", MBAI_ESCAPE },
};

/* B.2: macroblock_type, as flags. */
#define MB_QUANT   0x01
#define MB_FORWARD 0x02
#define MB_BACKWARD 0x04
#define MB_PATTERN 0x08
#define MB_INTRA   0x10
static const VlcCode_t kMbTypeI[] = {
	{ "1",  MB_INTRA },
	{ "01", MB_QUANT | MB_INTRA },
};
static const VlcCode_t kMbTypeP[] = {
	{ "1",      MB_FORWARD | MB_PATTERN },
	{ "01",     MB_PATTERN },
	{ "001",    MB_FORWARD },
	{ "00011",  MB_INTRA },
	{ "00010",  MB_QUANT | MB_FORWARD | MB_PATTERN },
	{ "00001",  MB_QUANT | MB_PATTERN },
	{ "000001", MB_QUANT | MB_INTRA },
};
static const VlcCode_t kMbTypeB[] = {
	{ "10",     MB_FORWARD | MB_BACKWARD },
	{ "11",     MB_FORWARD | MB_BACKWARD | MB_PATTERN },
	{ "010",    MB_BACKWARD },
	{ "011",    MB_BACKWARD | MB_PATTERN },
	{ "0010",   MB_FORWARD },
	{ "0011",   MB_FORWARD | MB_PATTERN },
	{ "00011",  MB_INTRA },
	{ "00010",  MB_QUANT | MB_FORWARD | MB_BACKWARD | MB_PATTERN },
	{ "000011", MB_QUANT | MB_FORWARD | MB_PATTERN },
	{ "000010", MB_QUANT | MB_BACKWARD | MB_PATTERN },
	{ "000001", MB_QUANT | MB_INTRA },
};

/* B.3: coded_block_pattern. */
static const VlcCode_t kCodedBlockPattern[] = {
	{ "111", 60 },       { "1101", 4 },       { "1100", 8 },       { "1011", 16 },
	{ "1010", 32 },      { "10011", 12 },     { "10010", 48 },     { "10001", 20 },
	{ "10000", 40 },     { "01111", 28 },     { "01110", 44 },     { "01101", 52 },
	{ "01100", 56 },     { "01011", 1 },      { "01010", 61 },     { "01001", 2 },
	{ "01000", 62 },     { "001111", 24 },    { "001110", 36 },    { "001101", 3 },
	{ "001100", 63 },    { "0010111", 5 },    { "0010110", 9 },    { "0010101", 17 },
	{ "0010100", 33 },   { "0010011", 6 },    { "0010010", 10 },   { "0010001", 18 },
	{ "0010000", 34 },   { "00011111", 7 },   { "00011110", 11 },  { "00011101", 19 },
	{ "00011100", 35 },  { "00011011", 13 },  { "00011010", 49 },  { "00011001", 21 },
	{ "00011000", 41 },  { "00010111", 14 },  { "00010110", 50 },  { "00010101", 22 },
	{ "00010100", 42 },  { "00010011", 15 },  { "00010010", 51 },  { "00010001", 23 },
	{ "00010000", 43 },  { "00001111", 25 },  { "00001110", 37 },  { "00001101", 26 },
	{ "00001100", 38 },  { "00001011", 29 },  { "00001010", 45 },  { "00001001", 53 },
	{ "00001000", 57 },  { "00000111", 30 },  { "00000110", 46 },  { "00000101", 54 },
	{ "00000100", 58 },  { "000000111", 31 }, { "000000110", 47 }, { "000000101", 55 },
	{ "000000100", 59 }, { "000000011", 27 }, { "000000010", 39 },
};

/* B.4: motion_horizontal/vertical_*_code 1..16; the table below is built with
   the sign bit that follows every non-zero code (0 positive, 1 negative). */
static const char* const kMotionCode[17] = {
	"1", "01", "001", "0001", "000011", "0000101", "0000100", "0000011",
	"000001011", "000001010", "000001001", "0000010001", "0000010000",
	"0000001111", "0000001110", "0000001101", "0000001100",
};

/* B.5a and B.5b: dct_dc_size_luminance and dct_dc_size_chrominance. */
static const VlcCode_t kDcSizeLuminance[] = {
	{ "100", 0 }, { "00", 1 }, { "01", 2 }, { "101", 3 }, { "110", 4 },
	{ "1110", 5 }, { "11110", 6 }, { "111110", 7 }, { "1111110", 8 },
};
static const VlcCode_t kDcSizeChrominance[] = {
	{ "00", 0 }, { "01", 1 }, { "10", 2 }, { "110", 3 }, { "1110", 4 },
	{ "11110", 5 }, { "111110", 6 }, { "1111110", 7 }, { "11111110", 8 },
};

/*
 * B.5c: dct_coeff_next, without the sign bit that follows every run/level
 * code. The value is run * 64 + level. dct_coeff_first differs only in that
 * "1s" is run 0 level 1 (there is no end of block first), which the block
 * decoder checks before this table.
 */
#define DCT_EOB    (-1)
#define DCT_ESCAPE (-2)
#define RL(run, level) ((run) * 64 + (level))
static const VlcCode_t kDctCoefficients[] = {
	{ "10", DCT_EOB },
	{ "000001", DCT_ESCAPE },
	{ "11", RL(0, 1) },
	{ "011", RL(1, 1) },
	{ "0100", RL(0, 2) },
	{ "0101", RL(2, 1) },
	{ "00101", RL(0, 3) },
	{ "00111", RL(3, 1) },
	{ "00110", RL(4, 1) },
	{ "000110", RL(1, 2) },
	{ "000111", RL(5, 1) },
	{ "000101", RL(6, 1) },
	{ "000100", RL(7, 1) },
	{ "0000110", RL(0, 4) },
	{ "0000100", RL(2, 2) },
	{ "0000111", RL(8, 1) },
	{ "0000101", RL(9, 1) },
	{ "00100110", RL(0, 5) },
	{ "00100001", RL(0, 6) },
	{ "00100101", RL(1, 3) },
	{ "00100100", RL(3, 2) },
	{ "00100111", RL(10, 1) },
	{ "00100011", RL(11, 1) },
	{ "00100010", RL(12, 1) },
	{ "00100000", RL(13, 1) },
	{ "0000001010", RL(0, 7) },
	{ "0000001100", RL(1, 4) },
	{ "0000001011", RL(2, 3) },
	{ "0000001111", RL(4, 2) },
	{ "0000001001", RL(5, 2) },
	{ "0000001110", RL(14, 1) },
	{ "0000001101", RL(15, 1) },
	{ "0000001000", RL(16, 1) },
	{ "000000011101", RL(0, 8) },
	{ "000000011000", RL(0, 9) },
	{ "000000010011", RL(0, 10) },
	{ "000000010000", RL(0, 11) },
	{ "000000011011", RL(1, 5) },
	{ "000000010100", RL(2, 4) },
	{ "000000011100", RL(3, 3) },
	{ "000000010010", RL(4, 3) },
	{ "000000011110", RL(6, 2) },
	{ "000000010101", RL(7, 2) },
	{ "000000010001", RL(8, 2) },
	{ "000000011111", RL(17, 1) },
	{ "000000011010", RL(18, 1) },
	{ "000000011001", RL(19, 1) },
	{ "000000010111", RL(20, 1) },
	{ "000000010110", RL(21, 1) },
	{ "0000000011010", RL(0, 12) },
	{ "0000000011001", RL(0, 13) },
	{ "0000000011000", RL(0, 14) },
	{ "0000000010111", RL(0, 15) },
	{ "0000000010110", RL(1, 6) },
	{ "0000000010101", RL(1, 7) },
	{ "0000000010100", RL(2, 5) },
	{ "0000000010011", RL(3, 4) },
	{ "0000000010010", RL(5, 3) },
	{ "0000000010001", RL(9, 2) },
	{ "0000000010000", RL(10, 2) },
	{ "0000000011111", RL(22, 1) },
	{ "0000000011110", RL(23, 1) },
	{ "0000000011101", RL(24, 1) },
	{ "0000000011100", RL(25, 1) },
	{ "0000000011011", RL(26, 1) },
	{ "00000000011111", RL(0, 16) },
	{ "00000000011110", RL(0, 17) },
	{ "00000000011101", RL(0, 18) },
	{ "00000000011100", RL(0, 19) },
	{ "00000000011011", RL(0, 20) },
	{ "00000000011010", RL(0, 21) },
	{ "00000000011001", RL(0, 22) },
	{ "00000000011000", RL(0, 23) },
	{ "00000000010111", RL(0, 24) },
	{ "00000000010110", RL(0, 25) },
	{ "00000000010101", RL(0, 26) },
	{ "00000000010100", RL(0, 27) },
	{ "00000000010011", RL(0, 28) },
	{ "00000000010010", RL(0, 29) },
	{ "00000000010001", RL(0, 30) },
	{ "00000000010000", RL(0, 31) },
	{ "000000000011000", RL(0, 32) },
	{ "000000000010111", RL(0, 33) },
	{ "000000000010110", RL(0, 34) },
	{ "000000000010101", RL(0, 35) },
	{ "000000000010100", RL(0, 36) },
	{ "000000000010011", RL(0, 37) },
	{ "000000000010010", RL(0, 38) },
	{ "000000000010001", RL(0, 39) },
	{ "000000000010000", RL(0, 40) },
	{ "000000000011111", RL(1, 8) },
	{ "000000000011110", RL(1, 9) },
	{ "000000000011101", RL(1, 10) },
	{ "000000000011100", RL(1, 11) },
	{ "000000000011011", RL(1, 12) },
	{ "000000000011010", RL(1, 13) },
	{ "000000000011001", RL(1, 14) },
	{ "0000000000010011", RL(1, 15) },
	{ "0000000000010010", RL(1, 16) },
	{ "0000000000010001", RL(1, 17) },
	{ "0000000000010000", RL(1, 18) },
	{ "0000000000010100", RL(6, 3) },
	{ "0000000000011010", RL(11, 2) },
	{ "0000000000011001", RL(12, 2) },
	{ "0000000000011000", RL(13, 2) },
	{ "0000000000010111", RL(14, 2) },
	{ "0000000000010110", RL(15, 2) },
	{ "0000000000010101", RL(16, 2) },
	{ "0000000000011111", RL(27, 1) },
	{ "0000000000011110", RL(28, 1) },
	{ "0000000000011101", RL(29, 1) },
	{ "0000000000011100", RL(30, 1) },
	{ "0000000000011011", RL(31, 1) },
};

/* 2.4.4.1 / Figure D.30: the zigzag order, scan index to raster index. */
static const uint8_t kZigzag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* 2.4.3.2: the default intra_quantizer_matrix, in raster order. The default
   non-intra matrix is 16 throughout. */
static const uint8_t kDefaultIntraMatrix[64] = {
	 8, 16, 19, 22, 26, 27, 29, 34,
	16, 16, 22, 24, 27, 29, 34, 37,
	19, 22, 26, 27, 29, 34, 34, 38,
	22, 22, 26, 27, 29, 34, 37, 40,
	22, 26, 27, 29, 32, 35, 40, 48,
	26, 27, 29, 32, 35, 40, 48, 58,
	26, 27, 29, 34, 38, 46, 56, 69,
	27, 29, 35, 38, 46, 56, 69, 83,
};

/* 2.4.3.2 picture_rate: 1..8; 0 is forbidden and 9..15 reserved. */
static const double kPictureRate[9] = {
	0.0, 24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 50.0, 60000.0 / 1001.0, 60.0,
};

/* ------------------------------------------------------------------------- */
/* The decoder                                                               */
/* ------------------------------------------------------------------------- */

#define PICTURE_I 1
#define PICTURE_P 2
#define PICTURE_B 3
#define PICTURE_D 4

typedef struct
{
	uint8_t* y;
	uint8_t* cb;
	uint8_t* cr;
	double   pts;    /* seconds on the movie clock, or -1 when the packet had none */
} Frame_t;

struct Mpeg1
{
	const uint8_t* data;
	size_t size;

	Queue_t video;
	Queue_t audio;
	int64_t basePts;
	double  duration;

	/* The video parse position in video.buf, and where the last start code
	   found by NextStartCode began. */
	size_t rd;
	size_t codePos;

	/* Sequence header. */
	int width, height;
	int mbWidth, mbHeight;
	int lumaStride, chromaStride;
	double frameRate;
	uint8_t intraMatrix[64];     /* raster order */
	uint8_t nonIntraMatrix[64];

	/* Picture header. */
	int pictureType;
	int fullPelForward, forwardRSize;
	int fullPelBackward, backwardRSize;
	int brokenLink;

	/* Slice and macroblock state. */
	int quantizerScale;
	int intraQ[64];              /* quantizer_scale * matrix, scan order */
	int nonIntraQ[64];
	int dcPredictor[3];
	int dcReset;
	int predForward[2], predBackward[2];   /* recon_*_prev, in the picture's own units */
	int vecForward[2], vecBackward[2];     /* the last vectors, in half pels, for skipped B macroblocks */
	int lastMode;                          /* MB_FORWARD | MB_BACKWARD of the last B macroblock */

	/* Frames: two references (forward is the older) and the B-picture frame. */
	Frame_t frames[3];
	Frame_t* forward;
	Frame_t* backward;
	Frame_t* bframe;
	Frame_t* target;
	int references;          /* reference pictures decoded so far, up to 2 */
	int skipB;               /* references still to decode before B-pictures are usable again */
	int holding;             /* the backward reference has not been shown yet */
	int videoEnded;
	double lastPts;

	int coeff[64];

	VlcTable_t vlcMbai, vlcMbTypeI, vlcMbTypeP, vlcMbTypeB, vlcCbp, vlcMotion;
	VlcTable_t vlcDcLum, vlcDcChrom, vlcDct;

	/* Audio. */
	int audioRate, audioChannels;
	mp3dec_t mp3;
	size_t ard;
	int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	int pcmFrames, pcmPos;
	int64_t leadFrames;      /* silence still to put in front of the first frame */
	int audioEnded;
};

static void SetQuantizer(Mpeg1_t* m, int q)
{
	int i;
	m->quantizerScale = q;
	for(i = 0; i < 64; i++)
	{
		m->intraQ[i] = q * m->intraMatrix[kZigzag[i]];
		m->nonIntraQ[i] = q * m->nonIntraMatrix[kZigzag[i]];
	}
}

/* ------------------------------------------------------------------------- */
/* Start codes                                                               */
/* ------------------------------------------------------------------------- */

/* The index of the next 00 00 01 in buf[from, to - 3), or to. */
static size_t FindStartCode(const uint8_t* buf, size_t from, size_t to)
{
	size_t i = from;
	while(i + 3 < to + 1 && i + 2 < to)
	{
		uint8_t c = buf[i + 2];
		if(c > 1)
			i += 3;
		else if(c == 1 && buf[i] == 0 && buf[i + 1] == 0)
			return i;
		else
			i++;
	}
	return to;
}

/* Finds the next start code from m->rd, appending packets as needed. Returns
   its value with m->rd just past it, or -1 at the end of the stream. */
static int NextStartCode(Mpeg1_t* m)
{
	Queue_t* q = &m->video;
	/* Everything before rd has been read. */
	if(m->rd > 1024 * 1024 || (m->rd > 0 && m->rd == q->len))
	{
		Queue_Discard(q, m->rd);
		m->rd = 0;
	}
	for(;;)
	{
		size_t i = FindStartCode(q->buf, m->rd, q->len);
		if(i + 4 <= q->len)
		{
			m->codePos = i;
			m->rd = i + 4;
			return q->buf[i + 3];
		}
		/* Keep the last three bytes: a start code may straddle packets. */
		if(q->len >= 3 && m->rd < q->len - 3)
			m->rd = q->len - 3;
		if(!Queue_Append(q, m->data))
			return -1;
	}
}

/*
 * The end of the syntax element that starts at `from`: the index of the next
 * start code that does not belong to it, appending packets until one is found
 * or the stream ends (then the buffer's end). A picture owns the slices and
 * the extension and user data between them (0x01..0xAF, 0xB2, 0xB5); a header
 * owns nothing after itself.
 */
static size_t ElementEnd(Mpeg1_t* m, size_t from, int picture)
{
	Queue_t* q = &m->video;
	size_t i = from;
	for(;;)
	{
		i = FindStartCode(q->buf, i, q->len);
		if(i + 4 <= q->len)
		{
			int c = q->buf[i + 3];
			if(!picture || c == 0x00 || c >= 0xB3)
			{
				if(!picture || (c != 0xB5))
					return i;
			}
			i += 4;
			continue;
		}
		if(q->len >= 3 && i < q->len - 3)
			i = q->len - 3;
		if(i < from)
			i = from;
		if(!Queue_Append(q, m->data))
			return q->len;
	}
}

/* ------------------------------------------------------------------------- */
/* Headers                                                                   */
/* ------------------------------------------------------------------------- */

static int AllocFrames(Mpeg1_t* m)
{
	size_t luma = (size_t)m->lumaStride * m->mbHeight * 16;
	size_t chroma = (size_t)m->chromaStride * m->mbHeight * 8;
	int i;
	for(i = 0; i < 3; i++)
	{
		uint8_t* p = malloc(luma + 2 * chroma);
		if(!p)
			return 0;
		memset(p, 16, luma);
		memset(p + luma, 128, 2 * chroma);
		m->frames[i].y = p;
		m->frames[i].cb = p + luma;
		m->frames[i].cr = p + luma + chroma;
		m->frames[i].pts = -1.0;
	}
	m->forward = &m->frames[0];
	m->backward = &m->frames[1];
	m->bframe = &m->frames[2];
	return 1;
}

/* 2.4.2.3 sequence_header, from m->rd (just past its start code). */
static int ParseSequenceHeader(Mpeg1_t* m)
{
	size_t end = ElementEnd(m, m->rd, 0);
	Bits_t b;
	int w, h, rate, i;
	b.p = m->video.buf;
	b.pos = m->rd * 8;
	b.end = end * 8;
	if(end - m->rd < 8)
		return 0;
	w = (int)Bits_Get(&b, 12);
	h = (int)Bits_Get(&b, 12);
	Bits_Skip(&b, 4);                  /* pel_aspect_ratio */
	rate = (int)Bits_Get(&b, 4);
	Bits_Skip(&b, 18 + 1 + 10 + 1);    /* bit_rate, marker, vbv_buffer_size, constrained_parameters_flag */
	if(Bits_Get(&b, 1))
	{
		if(b.pos + 64 * 8 > b.end)
			return 0;
		for(i = 0; i < 64; i++)
			m->intraMatrix[kZigzag[i]] = (uint8_t)Bits_Get(&b, 8);
	}
	else
		memcpy(m->intraMatrix, kDefaultIntraMatrix, 64);
	if(Bits_Get(&b, 1))
	{
		if(b.pos + 64 * 8 > b.end)
			return 0;
		for(i = 0; i < 64; i++)
			m->nonIntraMatrix[kZigzag[i]] = (uint8_t)Bits_Get(&b, 8);
	}
	else
		memset(m->nonIntraMatrix, 16, 64);
	for(i = 0; i < 64; i++)
	{
		/* A zero weight is forbidden; one keeps the arithmetic defined. */
		if(!m->intraMatrix[i])
			m->intraMatrix[i] = 1;
		if(!m->nonIntraMatrix[i])
			m->nonIntraMatrix[i] = 1;
	}
	m->rd = end;

	/* An MPEG-2 stream follows its sequence header with a sequence_extension. */
	if(end + 4 <= m->video.len && m->video.buf[end + 3] == 0xB5)
	{
		printf("[Mpeg1]: Refused: the video is MPEG-2 (a sequence_extension follows the sequence header); only MPEG-1 video is written\n");
		return 0;
	}

	if(m->width == 0)
	{
		if(w == 0 || h == 0)
		{
			printf("[Mpeg1]: Refused: a sequence header of size %dx%d\n", w, h);
			return 0;
		}
		if(rate < 1 || rate > 8)
		{
			printf("[Mpeg1]: Refused: picture_rate %d is forbidden or reserved\n", rate);
			return 0;
		}
		m->width = w;
		m->height = h;
		m->mbWidth = (w + 15) / 16;
		m->mbHeight = (h + 15) / 16;
		m->lumaStride = m->mbWidth * 16;
		m->chromaStride = m->mbWidth * 8;
		m->frameRate = kPictureRate[rate];
		if(!AllocFrames(m))
			return 0;
	}
	else if(w != m->width || h != m->height)
	{
		printf("[Mpeg1]: Refused: the picture size changes from %dx%d to %dx%d mid-stream\n",
		       m->width, m->height, w, h);
		return 0;
	}
	return 1;
}

/* 2.4.2.4 group_of_pictures_header. Only broken_link matters: the B-pictures
   right after the group's first I-picture then predict from a picture that
   was never decoded (after an edit, or at the start of the file). */
static void ParseGopHeader(Mpeg1_t* m)
{
	size_t end = ElementEnd(m, m->rd, 0);
	Bits_t b;
	b.p = m->video.buf;
	b.pos = m->rd * 8;
	b.end = end * 8;
	if(end - m->rd >= 4)
	{
		Bits_Skip(&b, 25 + 1);         /* time_code, closed_gop */
		if(Bits_Get(&b, 1))
			m->brokenLink = 1;
	}
	m->rd = end;
}

/* ------------------------------------------------------------------------- */
/* IDCT                                                                      */
/* ------------------------------------------------------------------------- */

/*
 * The 8x8 inverse DCT of 2.4.4.2 / Annex A, in the separable Loeffler-
 * Ligtenberg-Moschytz factorisation with 13-bit fixed-point constants and two
 * extra bits of precision between the passes (the well-known "islow" form):
 * accurate to the IEEE 1180 limits, 12 multiplies per 1-D transform, all in
 * 32-bit integers. Rows or columns with only a DC term take a shortcut.
 */
#define IDCT_CONST_BITS 13
#define IDCT_PASS1_BITS 2
#define FIX_0_298631336 2446
#define FIX_0_390180644 3196
#define FIX_0_541196100 4433
#define FIX_0_765366865 6270
#define FIX_0_899976223 7373
#define FIX_1_175875602 9633
#define FIX_1_501321110 12299
#define FIX_1_847759065 15137
#define FIX_1_961570560 16069
#define FIX_2_053119869 16819
#define FIX_2_562915447 20995
#define FIX_3_072711026 25172
#define DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

/* Transforms in[64] (raster order) into out[64]. */
static void Idct(const int* in, int* out)
{
	int ws[64];
	int i;
	for(i = 0; i < 8; i++)
	{
		const int* c = in + i;
		int* w = ws + i;
		int tmp0, tmp1, tmp2, tmp3, tmp10, tmp11, tmp12, tmp13, z1, z2, z3, z4, z5;
		if(!(c[8] | c[16] | c[24] | c[32] | c[40] | c[48] | c[56]))
		{
			int dc = c[0] * (1 << IDCT_PASS1_BITS);
			w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc;
			continue;
		}
		/* Even part. */
		z2 = c[16];
		z3 = c[48];
		z1 = (z2 + z3) * FIX_0_541196100;
		tmp2 = z1 - z3 * FIX_1_847759065;
		tmp3 = z1 + z2 * FIX_0_765366865;
		tmp0 = (c[0] + c[32]) * (1 << IDCT_CONST_BITS);
		tmp1 = (c[0] - c[32]) * (1 << IDCT_CONST_BITS);
		tmp10 = tmp0 + tmp3;
		tmp13 = tmp0 - tmp3;
		tmp11 = tmp1 + tmp2;
		tmp12 = tmp1 - tmp2;
		/* Odd part. */
		tmp0 = c[56];
		tmp1 = c[40];
		tmp2 = c[24];
		tmp3 = c[8];
		z1 = tmp0 + tmp3;
		z2 = tmp1 + tmp2;
		z3 = tmp0 + tmp2;
		z4 = tmp1 + tmp3;
		z5 = (z3 + z4) * FIX_1_175875602;
		tmp0 *= FIX_0_298631336;
		tmp1 *= FIX_2_053119869;
		tmp2 *= FIX_3_072711026;
		tmp3 *= FIX_1_501321110;
		z1 *= -FIX_0_899976223;
		z2 *= -FIX_2_562915447;
		z3 = z3 * -FIX_1_961570560 + z5;
		z4 = z4 * -FIX_0_390180644 + z5;
		tmp0 += z1 + z3;
		tmp1 += z2 + z4;
		tmp2 += z2 + z3;
		tmp3 += z1 + z4;
		w[0]  = DESCALE(tmp10 + tmp3, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[56] = DESCALE(tmp10 - tmp3, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[8]  = DESCALE(tmp11 + tmp2, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[48] = DESCALE(tmp11 - tmp2, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[16] = DESCALE(tmp12 + tmp1, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[40] = DESCALE(tmp12 - tmp1, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[24] = DESCALE(tmp13 + tmp0, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		w[32] = DESCALE(tmp13 - tmp0, IDCT_CONST_BITS - IDCT_PASS1_BITS);
	}
	for(i = 0; i < 8; i++)
	{
		const int* r = ws + i * 8;
		int* o = out + i * 8;
		int tmp0, tmp1, tmp2, tmp3, tmp10, tmp11, tmp12, tmp13, z1, z2, z3, z4, z5;
		if(!(r[1] | r[2] | r[3] | r[4] | r[5] | r[6] | r[7]))
		{
			int dc = DESCALE(r[0], IDCT_PASS1_BITS + 3);
			o[0] = o[1] = o[2] = o[3] = o[4] = o[5] = o[6] = o[7] = dc;
			continue;
		}
		z2 = r[2];
		z3 = r[6];
		z1 = (z2 + z3) * FIX_0_541196100;
		tmp2 = z1 - z3 * FIX_1_847759065;
		tmp3 = z1 + z2 * FIX_0_765366865;
		tmp0 = (r[0] + r[4]) * (1 << IDCT_CONST_BITS);
		tmp1 = (r[0] - r[4]) * (1 << IDCT_CONST_BITS);
		tmp10 = tmp0 + tmp3;
		tmp13 = tmp0 - tmp3;
		tmp11 = tmp1 + tmp2;
		tmp12 = tmp1 - tmp2;
		tmp0 = r[7];
		tmp1 = r[5];
		tmp2 = r[3];
		tmp3 = r[1];
		z1 = tmp0 + tmp3;
		z2 = tmp1 + tmp2;
		z3 = tmp0 + tmp2;
		z4 = tmp1 + tmp3;
		z5 = (z3 + z4) * FIX_1_175875602;
		tmp0 *= FIX_0_298631336;
		tmp1 *= FIX_2_053119869;
		tmp2 *= FIX_3_072711026;
		tmp3 *= FIX_1_501321110;
		z1 *= -FIX_0_899976223;
		z2 *= -FIX_2_562915447;
		z3 = z3 * -FIX_1_961570560 + z5;
		z4 = z4 * -FIX_0_390180644 + z5;
		tmp0 += z1 + z3;
		tmp1 += z2 + z4;
		tmp2 += z2 + z3;
		tmp3 += z1 + z4;
		o[0] = DESCALE(tmp10 + tmp3, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[7] = DESCALE(tmp10 - tmp3, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[1] = DESCALE(tmp11 + tmp2, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[6] = DESCALE(tmp11 - tmp2, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[2] = DESCALE(tmp12 + tmp1, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[5] = DESCALE(tmp12 - tmp1, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[3] = DESCALE(tmp13 + tmp0, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
		o[4] = DESCALE(tmp13 - tmp0, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3);
	}
}

#define CLAMP255(v) ((unsigned)(v) <= 255u ? (uint8_t)(v) : (v) < 0 ? (uint8_t)0 : (uint8_t)255)

/* Writes (intra) or adds (predicted) the transformed block to 8x8 pixels and
   clears the coefficients for the next block. `last` is the highest scan
   index that was coded; a block with only its DC term is one value. */
static void PutBlock(Mpeg1_t* m, uint8_t* dst, int stride, int last, int add)
{
	int* c = m->coeff;
	int x, y;
	if(last == 0)
	{
		int v = DESCALE(c[0] * (1 << IDCT_PASS1_BITS), IDCT_PASS1_BITS + 3);
		c[0] = 0;
		if(add)
		{
			for(y = 0; y < 8; y++, dst += stride)
				for(x = 0; x < 8; x++)
				{
					int p = dst[x] + v;
					dst[x] = CLAMP255(p);
				}
		}
		else
		{
			uint8_t p = CLAMP255(v);
			for(y = 0; y < 8; y++, dst += stride)
				memset(dst, p, 8);
		}
		return;
	}
	{
		int out[64];
		const int* o = out;
		Idct(c, out);
		memset(c, 0, sizeof m->coeff);
		if(add)
		{
			for(y = 0; y < 8; y++, dst += stride, o += 8)
				for(x = 0; x < 8; x++)
				{
					int p = dst[x] + o[x];
					dst[x] = CLAMP255(p);
				}
		}
		else
		{
			for(y = 0; y < 8; y++, dst += stride, o += 8)
				for(x = 0; x < 8; x++)
					dst[x] = CLAMP255(o[x]);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Blocks                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * One run/level pair from dct_coeff_next (or dct_coeff_first when `first`).
 * Returns 0 at end of block, 1 for a coefficient, -1 on an invalid code. The
 * escape (2.4.3.7) is a 6-bit run and an 8-bit level, or 16 bits for
 * |level| >= 128: 0x00 then 128..255, or 0x80 then the low 8 bits of -255..-129.
 */
static inline int ReadCoefficient(Mpeg1_t* m, Bits_t* b, int first, int* run, int* level)
{
	int v;
	if(first && Bits_Peek(b, 1))
	{
		Bits_Skip(b, 1);
		*run = 0;
		*level = Bits_Get(b, 1) ? -1 : 1;
		return 1;
	}
	v = Vlc_Get(b, &m->vlcDct);
	if(v == DCT_EOB)
		return first ? -1 : 0;
	if(v == DCT_ESCAPE)
	{
		int l;
		*run = (int)Bits_Get(b, 6);
		l = (int)Bits_Get(b, 8);
		if(l == 0)
			l = (int)Bits_Get(b, 8);
		else if(l == 128)
			l = (int)Bits_Get(b, 8) - 256;
		else if(l > 128)
			l -= 256;
		*level = l;
		return 1;
	}
	if(v == VLC_ERROR)
		return -1;
	*run = v >> 6;
	*level = Bits_Get(b, 1) ? -(v & 63) : (v & 63);
	return 1;
}

/*
 * The inverse quantisation of 2.4.4.1 and 2.4.4.2: the product is truncated
 * toward zero, then made odd by stepping toward zero (the MPEG-1 mismatch
 * control), then saturated to -2048..2047. `mag` is the magnitude before
 * oddification.
 */
static inline int Oddify(int mag, int negative)
{
	if(mag == 0)
		return 0;
	mag = (mag - 1) | 1;
	if(negative)
		return mag > 2048 ? -2048 : -mag;
	return mag > 2047 ? 2047 : mag;
}

/* An intra block (2.4.2.8 with dct_dc_*_past): DC from its own predictor
   (comp 0 luminance, 1 Cb, 2 Cr), then the AC coefficients. */
static int DecodeIntraBlock(Mpeg1_t* m, Bits_t* b, int comp, uint8_t* dst, int stride)
{
	int size, i = 0, last = 0, run, level, r;
	size = Vlc_Get(b, comp ? &m->vlcDcChrom : &m->vlcDcLum);
	if(size == VLC_ERROR)
		return 0;
	if(size)
	{
		int v = (int)Bits_Get(b, size);
		if(!(v & (1 << (size - 1))))
			v = v - (1 << size) + 1;
		m->dcPredictor[comp] += v * 8;
	}
	m->coeff[0] = m->dcPredictor[comp];
	for(;;)
	{
		r = ReadCoefficient(m, b, 0, &run, &level);
		if(r == 0)
			break;
		i += run + 1;
		if(r < 0 || i > 63 || b->pos > b->end)
		{
			memset(m->coeff, 0, sizeof m->coeff);
			return 0;
		}
		m->coeff[kZigzag[i]] = level < 0 ? Oddify((-level * m->intraQ[i]) >> 3, 1)
		                                 : Oddify((level * m->intraQ[i]) >> 3, 0);
		last = i;
	}
	PutBlock(m, dst, stride, last, 0);
	return 1;
}

/* A non-intra block: the residual added to the prediction already in dst. */
static int DecodeNonIntraBlock(Mpeg1_t* m, Bits_t* b, uint8_t* dst, int stride)
{
	int i = -1, last = 0, run, level, r, first = 1;
	for(;;)
	{
		r = ReadCoefficient(m, b, first, &run, &level);
		if(r == 0)
			break;
		first = 0;
		i += run + 1;
		if(r < 0 || i > 63 || b->pos > b->end)
		{
			memset(m->coeff, 0, sizeof m->coeff);
			return 0;
		}
		m->coeff[kZigzag[i]] = level < 0 ? Oddify(((-2 * level + 1) * m->nonIntraQ[i]) >> 4, 1)
		                                 : Oddify(((2 * level + 1) * m->nonIntraQ[i]) >> 4, 0);
		last = i;
	}
	/* The DC-only shortcut needs the one coefficient to be the DC one. */
	if(last == 0 && i > 0)
		last = 1;
	PutBlock(m, dst, stride, i < 0 ? 0 : last, 1);
	return 1;
}

/* ------------------------------------------------------------------------- */
/* Motion compensation                                                       */
/* ------------------------------------------------------------------------- */

/*
 * One size x size block (16 luminance, 8 chrominance) of prediction from src,
 * at half-pel offsets hx, hy (2.4.4.2: the half-pel samples are the rounded
 * averages of the two or four neighbours). With avg the result is averaged,
 * rounding up, into what dst already holds: the second half of a bidirectional
 * prediction (2.4.4.3).
 */
#define MC_LOOP(expr) \
	do { \
		int x_, y_; \
		if(avg) \
			for(y_ = 0; y_ < size; y_++, dst += stride, src += stride) \
				for(x_ = 0; x_ < size; x_++) \
					dst[x_] = (uint8_t)((dst[x_] + (expr) + 1) >> 1); \
		else \
			for(y_ = 0; y_ < size; y_++, dst += stride, src += stride) \
				for(x_ = 0; x_ < size; x_++) \
					dst[x_] = (uint8_t)(expr); \
	} while(0)

static void MotionBlock(uint8_t* dst, const uint8_t* src, int stride, int size, int hx, int hy, int avg)
{
	switch((hy << 1) | hx)
	{
	case 0:
		MC_LOOP(src[x_]);
		break;
	case 1:
		MC_LOOP((src[x_] + src[x_ + 1] + 1) >> 1);
		break;
	case 2:
		MC_LOOP((src[x_] + src[x_ + stride] + 1) >> 1);
		break;
	default:
		MC_LOOP((src[x_] + src[x_ + 1] + src[x_ + stride] + src[x_ + stride + 1] + 2) >> 2);
		break;
	}
}

/* Predicts macroblock (mbx, mby) of the target from ref with the vector
   (mvx, mvy) in half luminance pels. The chrominance vector is half of it,
   divided with truncation toward zero as 2.4.4.2 prescribes. A vector that
   would reach outside the reference (which a valid stream never sends) is
   clamped so the read stays inside it. */
static void Predict(Mpeg1_t* m, const Frame_t* ref, int mbx, int mby, int mvx, int mvy, int avg)
{
	int ls = m->lumaStride, cs = m->chromaStride;
	int lw = ls, lh = m->mbHeight * 16, cw = cs, ch = m->mbHeight * 8;
	int hx = mvx & 1, hy = mvy & 1;
	int x = mbx * 16 + (mvx >> 1), y = mby * 16 + (mvy >> 1);
	int cx, cy;
	Frame_t* t = m->target;
	if(x < 0) x = 0; else if(x > lw - 16 - hx) x = lw - 16 - hx;
	if(y < 0) y = 0; else if(y > lh - 16 - hy) y = lh - 16 - hy;
	MotionBlock(t->y + (mby * 16) * ls + mbx * 16, ref->y + y * ls + x, ls, 16, hx, hy, avg);

	cx = mvx / 2;
	cy = mvy / 2;
	hx = cx & 1;
	hy = cy & 1;
	x = mbx * 8 + (cx >> 1);
	y = mby * 8 + (cy >> 1);
	if(x < 0) x = 0; else if(x > cw - 8 - hx) x = cw - 8 - hx;
	if(y < 0) y = 0; else if(y > ch - 8 - hy) y = ch - 8 - hy;
	MotionBlock(t->cb + (mby * 8) * cs + mbx * 8, ref->cb + y * cs + x, cs, 8, hx, hy, avg);
	MotionBlock(t->cr + (mby * 8) * cs + mbx * 8, ref->cr + y * cs + x, cs, 8, hx, hy, avg);
}

/* The B-picture prediction for the modes in `mode` with the stored vectors. */
static void PredictB(Mpeg1_t* m, int mbx, int mby, int mode)
{
	if(mode & MB_FORWARD)
		Predict(m, m->forward, mbx, mby, m->vecForward[0], m->vecForward[1], 0);
	if(mode & MB_BACKWARD)
		Predict(m, m->backward, mbx, mby, m->vecBackward[0], m->vecBackward[1], (mode & MB_FORWARD) != 0);
}

/*
 * One motion vector component (2.4.4.2): motion_code, then motion_r when the
 * f_code allows residuals, added to the predictor and wrapped into
 * [-16f, 16f - 1]. The predictor keeps the result in the picture's own units;
 * a full-pel vector is doubled only for use.
 */
static int DecodeMotion(Mpeg1_t* m, Bits_t* b, int rsize, int* pred, int* ok)
{
	int code = Vlc_Get(b, &m->vlcMotion), delta = 0, v;
	int range = 16 << rsize;
	if(code == VLC_ERROR)
	{
		*ok = 0;
		return 0;
	}
	if(code != 0)
	{
		int r = rsize ? (int)Bits_Get(b, rsize) : 0;
		delta = (((code < 0 ? -code : code) - 1) << rsize) + r + 1;
		if(code < 0)
			delta = -delta;
	}
	v = *pred + delta;
	if(v > range - 1)
		v -= 2 * range;
	else if(v < -range)
		v += 2 * range;
	*pred = v;
	return v;
}

/* ------------------------------------------------------------------------- */
/* Macroblocks and slices                                                    */
/* ------------------------------------------------------------------------- */

/* A skipped macroblock (2.4.4.4): in a P-picture a copy of the forward
   reference with a zero vector, which also zeroes the vector predictor; in a
   B-picture the previous macroblock's prediction with its vectors. */
static void SkippedMacroblock(Mpeg1_t* m, int address)
{
	int mbx = address % m->mbWidth, mby = address / m->mbWidth;
	m->dcReset = 1;
	if(m->pictureType == PICTURE_P)
	{
		m->predForward[0] = m->predForward[1] = 0;
		Predict(m, m->backward, mbx, mby, 0, 0, 0);
	}
	else
		PredictB(m, mbx, mby, m->lastMode);
}

static int DecodeMacroblock(Mpeg1_t* m, Bits_t* b, int address)
{
	int mbx = address % m->mbWidth, mby = address / m->mbWidth;
	int ls = m->lumaStride, cs = m->chromaStride;
	Frame_t* t = m->target;
	uint8_t* y = t->y + (mby * 16) * ls + mbx * 16;
	uint8_t* cb = t->cb + (mby * 8) * cs + mbx * 8;
	uint8_t* cr = t->cr + (mby * 8) * cs + mbx * 8;
	const VlcTable_t* types = m->pictureType == PICTURE_I ? &m->vlcMbTypeI
	                        : m->pictureType == PICTURE_P ? &m->vlcMbTypeP : &m->vlcMbTypeB;
	int type = Vlc_Get(b, types), cbp, ok = 1;
	if(type == VLC_ERROR)
		return 0;
	if(type & MB_QUANT)
	{
		int q = (int)Bits_Get(b, 5);
		if(q == 0)
			return 0;
		SetQuantizer(m, q);
	}

	if(type & MB_INTRA)
	{
		/* The DC predictors restart after anything but an intra macroblock
		   (2.4.4.1); the vector predictors restart after an intra one. */
		if(m->dcReset)
		{
			m->dcPredictor[0] = m->dcPredictor[1] = m->dcPredictor[2] = 1024;
			m->dcReset = 0;
		}
		m->predForward[0] = m->predForward[1] = 0;
		m->predBackward[0] = m->predBackward[1] = 0;
		return DecodeIntraBlock(m, b, 0, y, ls)
		    && DecodeIntraBlock(m, b, 0, y + 8, ls)
		    && DecodeIntraBlock(m, b, 0, y + 8 * ls, ls)
		    && DecodeIntraBlock(m, b, 0, y + 8 * ls + 8, ls)
		    && DecodeIntraBlock(m, b, 1, cb, cs)
		    && DecodeIntraBlock(m, b, 2, cr, cs);
	}

	m->dcReset = 1;
	if(type & MB_FORWARD)
	{
		int vx = DecodeMotion(m, b, m->forwardRSize, &m->predForward[0], &ok);
		int vy = DecodeMotion(m, b, m->forwardRSize, &m->predForward[1], &ok);
		m->vecForward[0] = m->fullPelForward ? vx * 2 : vx;
		m->vecForward[1] = m->fullPelForward ? vy * 2 : vy;
	}
	if(type & MB_BACKWARD)
	{
		int vx = DecodeMotion(m, b, m->backwardRSize, &m->predBackward[0], &ok);
		int vy = DecodeMotion(m, b, m->backwardRSize, &m->predBackward[1], &ok);
		m->vecBackward[0] = m->fullPelBackward ? vx * 2 : vx;
		m->vecBackward[1] = m->fullPelBackward ? vy * 2 : vy;
	}
	if(!ok)
		return 0;

	if(m->pictureType == PICTURE_P)
	{
		/* A P macroblock without motion compensation has a zero vector and
		   restarts the predictor. */
		if(!(type & MB_FORWARD))
		{
			m->predForward[0] = m->predForward[1] = 0;
			m->vecForward[0] = m->vecForward[1] = 0;
		}
		Predict(m, m->backward, mbx, mby, m->vecForward[0], m->vecForward[1], 0);
	}
	else
	{
		m->lastMode = type & (MB_FORWARD | MB_BACKWARD);
		PredictB(m, mbx, mby, m->lastMode);
	}

	if(!(type & MB_PATTERN))
		return 1;
	cbp = Vlc_Get(b, &m->vlcCbp);
	if(cbp == VLC_ERROR)
		return 0;
	if((cbp & 32) && !DecodeNonIntraBlock(m, b, y, ls)) return 0;
	if((cbp & 16) && !DecodeNonIntraBlock(m, b, y + 8, ls)) return 0;
	if((cbp & 8) && !DecodeNonIntraBlock(m, b, y + 8 * ls, ls)) return 0;
	if((cbp & 4) && !DecodeNonIntraBlock(m, b, y + 8 * ls + 8, ls)) return 0;
	if((cbp & 2) && !DecodeNonIntraBlock(m, b, cb, cs)) return 0;
	if((cbp & 1) && !DecodeNonIntraBlock(m, b, cr, cs)) return 0;
	return 1;
}

/* 2.4.2.6 slice: from just past its start code to the next one. A slice that
   breaks off on an invalid code leaves the rest of its macroblocks as they
   were; the next slice starts clean. */
static void DecodeSlice(Mpeg1_t* m, Bits_t* b, int row)
{
	int count = m->mbWidth * m->mbHeight;
	int address = row * m->mbWidth - 1;
	int first = 1, q;
	q = (int)Bits_Get(b, 5);
	if(q == 0)
		return;
	SetQuantizer(m, q);
	while(Bits_Get(b, 1))              /* extra_bit_slice, extra_information_slice */
		Bits_Skip(b, 8);
	m->dcReset = 1;
	m->predForward[0] = m->predForward[1] = 0;
	m->predBackward[0] = m->predBackward[1] = 0;
	m->vecForward[0] = m->vecForward[1] = 0;
	m->vecBackward[0] = m->vecBackward[1] = 0;
	m->lastMode = 0;
	for(;;)
	{
		int increment = 0, v;
		for(;;)
		{
			v = Vlc_Get(b, &m->vlcMbai);
			if(v == MBAI_STUFFING)
				continue;
			if(v == MBAI_ESCAPE)
			{
				increment += 33;
				continue;
			}
			break;
		}
		if(v == VLC_ERROR || b->pos > b->end)
			return;
		increment += v;
		if(first)
		{
			/* The first increment is from the slice's start, not a skip. */
			address += increment;
			first = 0;
		}
		else
		{
			int k;
			if(address + increment >= count)
				return;
			for(k = 1; k < increment; k++)
				SkippedMacroblock(m, address + k);
			address += increment;
		}
		if(address < 0 || address >= count)
			return;
		if(!DecodeMacroblock(m, b, address))
			return;
		/* The slice ends where the next start code's 23 zeros begin. */
		if(b->pos >= b->end || Bits_Peek(b, 23) == 0)
			return;
	}
}

/* ------------------------------------------------------------------------- */
/* Pictures                                                                  */
/* ------------------------------------------------------------------------- */

#define DECODED_NONE      0   /* the end of the stream */
#define DECODED_REFERENCE 1   /* an I- or P-picture, now m->backward */
#define DECODED_B         2   /* a B-picture, in m->bframe */
#define DECODED_SKIPPED   3   /* a picture that cannot be decoded here (no references yet) */

static int DecodePicture(Mpeg1_t* m)
{
	int code, type, temporal;
	size_t end, i;
	Bits_t b;
	double pts = -1.0;
	int64_t pts90;
	for(;;)
	{
		code = NextStartCode(m);
		if(code < 0)
			return DECODED_NONE;
		if(code == 0xB3)
		{
			if(!ParseSequenceHeader(m))
				return DECODED_NONE;
		}
		else if(code == 0xB8)
			ParseGopHeader(m);
		else if(code == 0x00)
			break;
	}

	pts90 = Queue_TakePts(&m->video, m->video.base + m->codePos);
	if(pts90 >= 0)
		pts = (double)(pts90 - m->basePts) / 90000.0;
	end = ElementEnd(m, m->rd, 1);
	b.p = m->video.buf;
	b.pos = m->rd * 8;
	b.end = end * 8;
	if(end - m->rd < 4)
	{
		m->rd = end;
		return DECODED_SKIPPED;
	}

	/* 2.4.2.5 picture_header. */
	temporal = (int)Bits_Get(&b, 10);
	(void)temporal;                    /* display order follows from the picture types */
	type = (int)Bits_Get(&b, 3);
	Bits_Skip(&b, 16);                 /* vbv_delay */
	if(type == PICTURE_P || type == PICTURE_B)
	{
		m->fullPelForward = (int)Bits_Get(&b, 1);
		m->forwardRSize = (int)Bits_Get(&b, 3) - 1;
	}
	if(type == PICTURE_B)
	{
		m->fullPelBackward = (int)Bits_Get(&b, 1);
		m->backwardRSize = (int)Bits_Get(&b, 3) - 1;
	}
	if(type == PICTURE_D)
	{
		printf("[Mpeg1]: Refused: a D-picture (picture_coding_type 4, DC-only intra); the video ends here\n");
		m->rd = end;
		return DECODED_NONE;
	}
	if(type < PICTURE_I || type > PICTURE_D
	   || ((type == PICTURE_P || type == PICTURE_B) && m->forwardRSize < 0)
	   || (type == PICTURE_B && m->backwardRSize < 0))
	{
		/* A forbidden coding type or f_code 0: nothing in it can be read. */
		m->rd = end;
		return DECODED_SKIPPED;
	}
	if((type == PICTURE_P && m->references < 1)
	   || (type == PICTURE_B && (m->references < 2 || m->skipB > 0)))
	{
		m->rd = end;
		return DECODED_SKIPPED;
	}

	m->pictureType = type;
	if(type == PICTURE_B)
		m->target = m->bframe;
	else
	{
		/* The new reference goes into the older one's frame. */
		if(m->brokenLink)
		{
			m->skipB = 2;
			m->brokenLink = 0;
		}
		m->target = m->forward;
	}
	m->target->pts = pts;

	/* The slices, skipping the extra_information_picture bits, extension and
	   user data before them. */
	i = m->rd;
	for(;;)
	{
		size_t s = FindStartCode(m->video.buf, i, end);
		size_t next;
		int c;
		if(s + 4 > end)
			break;
		c = m->video.buf[s + 3];
		next = FindStartCode(m->video.buf, s + 4, end);
		if(c >= 0x01 && c <= 0xAF && c <= m->mbHeight)
		{
			b.pos = (s + 4) * 8;
			b.end = next * 8;
			DecodeSlice(m, &b, c - 1);
		}
		i = next;
	}
	m->rd = end;

	if(type == PICTURE_B)
		return DECODED_B;
	{
		Frame_t* t = m->forward;
		m->forward = m->backward;
		m->backward = t;
	}
	if(m->references < 2)
		m->references++;
	if(m->skipB > 0)
		m->skipB--;
	return DECODED_REFERENCE;
}

/* 4:2:0 studio-range BT.601 to B, G, R, x, 16.16 fixed point: two luminance
   rows share each chrominance row. */
static void ConvertFrame(const Mpeg1_t* m, const Frame_t* f, uint8_t* out, int stride)
{
	int w = m->width, h = m->height, ls = m->lumaStride, cs = m->chromaStride;
	int x, y;
	for(y = 0; y < h; y += 2)
	{
		const uint8_t* y0 = f->y + y * ls;
		const uint8_t* y1 = y0 + ls;
		const uint8_t* cb = f->cb + (y >> 1) * cs;
		const uint8_t* cr = f->cr + (y >> 1) * cs;
		uint8_t* o0 = out + (size_t)y * stride;
		uint8_t* o1 = o0 + stride;
		int second = y + 1 < h;
		for(x = 0; x < w; x += 2)
		{
			int u = cb[x >> 1] - 128, v = cr[x >> 1] - 128;
			int rr = 104597 * v + 32768;
			int gg = -25675 * u - 53279 * v + 32768;
			int bb = 132201 * u + 32768;
			int l, k, n = x + 1 < w ? 2 : 1;
			for(k = 0; k < n; k++)
			{
				uint8_t* p = o0 + (x + k) * 4;
				l = (y0[x + k] - 16) * 76309;
				p[0] = CLAMP255((l + bb) >> 16);
				p[1] = CLAMP255((l + gg) >> 16);
				p[2] = CLAMP255((l + rr) >> 16);
				p[3] = 0xFF;
				if(second)
				{
					p = o1 + (x + k) * 4;
					l = (y1[x + k] - 16) * 76309;
					p[0] = CLAMP255((l + bb) >> 16);
					p[1] = CLAMP255((l + gg) >> 16);
					p[2] = CLAMP255((l + rr) >> 16);
					p[3] = 0xFF;
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* The system stream                                                         */
/* ------------------------------------------------------------------------- */

static int64_t ReadPts(const uint8_t* p)
{
	return ((int64_t)((p[0] >> 1) & 7) << 30) | ((int64_t)p[1] << 22) | ((int64_t)(p[2] >> 1) << 15)
	     | ((int64_t)p[3] << 7) | (int64_t)(p[4] >> 1);
}

/*
 * Walks every pack of an ISO/IEC 11172-1 stream and files the payload of each
 * packet of the first video stream and the first audio stream met. The packet
 * header (2.4.3.3) is up to 16 stuffing bytes, an optional STD buffer field
 * ('01'), then '0010' + PTS, '0011' + PTS + DTS, or 0x0F.
 */
static int Demux(Mpeg1_t* m)
{
	const uint8_t* d = m->data;
	size_t size = m->size, pos = 0;
	int videoId = -1, audioId = -1;
	while(pos + 4 <= size)
	{
		int code;
		if(d[pos] != 0 || d[pos + 1] != 0 || d[pos + 2] != 1)
		{
			pos++;
			continue;
		}
		code = d[pos + 3];
		if(code == 0xBA)
		{
			if(pos + 5 <= size && (d[pos + 4] & 0xC0) == 0x40)
			{
				printf("[Mpeg1]: Refused: an MPEG-2 program stream pack; only MPEG-1 system streams are written\n");
				return 0;
			}
			pos += 12;
		}
		else if(code == 0xB9)
			pos += 4;
		else if(code >= 0xBB)
		{
			size_t len, start, end, p;
			int64_t pts = -1;
			if(pos + 6 > size)
				break;
			len = ((size_t)d[pos + 4] << 8) | d[pos + 5];
			start = pos + 6;
			end = start + len;
			if(end > size)
				end = size;
			pos = end;
			if(code == 0xBB || code == 0xBE || code == 0xBF || code == 0xBC)
				continue;       /* system header, padding, private 2, reserved */
			if(!((code >= 0xC0 && code <= 0xDF) || (code >= 0xE0 && code <= 0xEF)))
				continue;
			p = start;
			while(p < end && d[p] == 0xFF)
				p++;
			if(p < end && (d[p] & 0xC0) == 0x40)
				p += 2;
			if(p >= end)
				continue;
			if((d[p] & 0xF0) == 0x20)
			{
				if(p + 5 > end)
					continue;
				pts = ReadPts(d + p);
				p += 5;
			}
			else if((d[p] & 0xF0) == 0x30)
			{
				if(p + 10 > end)
					continue;
				pts = ReadPts(d + p);
				p += 10;
			}
			else if(d[p] == 0x0F)
				p++;
			else if((d[p] & 0xC0) == 0x80)
			{
				printf("[Mpeg1]: Refused: an MPEG-2 PES header; only MPEG-1 system streams are written\n");
				return 0;
			}
			else
				continue;
			if(code >= 0xE0)
			{
				if(videoId < 0)
					videoId = code;
				if(code == videoId && !Queue_AddPacket(&m->video, (uint32_t)p, (uint32_t)(end - p), pts))
					return 0;
			}
			else
			{
				if(audioId < 0)
					audioId = code;
				if(code == audioId && !Queue_AddPacket(&m->audio, (uint32_t)p, (uint32_t)(end - p), pts))
					return 0;
			}
		}
		else
			pos += 4;
	}
	return 1;
}

/* ------------------------------------------------------------------------- */
/* Audio                                                                     */
/* ------------------------------------------------------------------------- */

/* Appends audio packets until `want` bytes are buffered past ard (a few
   frames, so minimp3 can confirm a sync by the frames that follow) or there
   are no more. */
static void AudioFill(Mpeg1_t* m, size_t want)
{
	Queue_t* q = &m->audio;
	if(m->ard > 65536)
	{
		Queue_Discard(q, m->ard);
		m->ard = 0;
	}
	while(q->len - m->ard < want)
		if(!Queue_Append(q, m->data))
			break;
}

/* Decodes the next audio frame into m->pcm, converted to the stream's channel
   count as Open found it. Returns 0 at the end. */
static int AudioDecodeFrame(Mpeg1_t* m)
{
	for(;;)
	{
		mp3dec_frame_info_t info;
		int16_t tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];
		int n, avail, i;
		AudioFill(m, 16384);
		avail = (int)(m->audio.len - m->ard);
		if(avail <= 0)
			return 0;
		n = mp3dec_decode_frame(&m->mp3, m->audio.buf + m->ard, avail, tmp, &info);
		if(info.frame_bytes == 0)
		{
			/* No frame in what is buffered: drop it and read on, if there is more. */
			m->ard = m->audio.len;
			if(m->audio.next >= m->audio.count)
				return 0;
			continue;
		}
		m->ard += info.frame_bytes;
		if(n <= 0)
			continue;
		if(info.channels == m->audioChannels)
			memcpy(m->pcm, tmp, (size_t)n * info.channels * sizeof(int16_t));
		else if(info.channels == 1)
			for(i = 0; i < n; i++)
				m->pcm[2 * i] = m->pcm[2 * i + 1] = tmp[i];
		else
			for(i = 0; i < n; i++)
				m->pcm[i] = (int16_t)((tmp[2 * i] + tmp[2 * i + 1]) / 2);
		m->pcmFrames = n;
		m->pcmPos = 0;
		return 1;
	}
}

int Mpeg1_ReadAudio(Mpeg1_t* m, int16_t* out, int maxFrames)
{
	int written = 0, ch;
	if(!m || m->audioChannels == 0)
		return 0;
	ch = m->audioChannels;
	while(written < maxFrames)
	{
		if(m->leadFrames > 0)
		{
			int n = maxFrames - written;
			if(n > m->leadFrames)
				n = (int)m->leadFrames;
			memset(out + (size_t)written * ch, 0, (size_t)n * ch * sizeof(int16_t));
			written += n;
			m->leadFrames -= n;
			continue;
		}
		if(m->pcmPos >= m->pcmFrames)
		{
			if(m->audioEnded || !AudioDecodeFrame(m))
			{
				m->audioEnded = 1;
				break;
			}
		}
		{
			int n = m->pcmFrames - m->pcmPos;
			if(n > maxFrames - written)
				n = maxFrames - written;
			memcpy(out + (size_t)written * ch, m->pcm + (size_t)m->pcmPos * ch, (size_t)n * ch * sizeof(int16_t));
			written += n;
			m->pcmPos += n;
		}
	}
	return written;
}

/* ------------------------------------------------------------------------- */
/* Open, close, frames                                                       */
/* ------------------------------------------------------------------------- */

static int BuildTables(Mpeg1_t* m)
{
	VlcCode_t motion[33];
	char strings[33][16];
	int i, n = 0;
	motion[n].code = kMotionCode[0];
	motion[n].value = 0;
	n++;
	for(i = 1; i <= 16; i++)
	{
		snprintf(strings[2 * i - 1], sizeof strings[0], "%s0", kMotionCode[i]);
		snprintf(strings[2 * i], sizeof strings[0], "%s1", kMotionCode[i]);
		motion[n].code = strings[2 * i - 1];
		motion[n++].value = i;
		motion[n].code = strings[2 * i];
		motion[n++].value = -i;
	}
#define BUILD(t, codes) Vlc_Build(&m->t, codes, (int)(sizeof codes / sizeof codes[0]))
	return BUILD(vlcMbai, kMbAddressIncrement)
	    && BUILD(vlcMbTypeI, kMbTypeI)
	    && BUILD(vlcMbTypeP, kMbTypeP)
	    && BUILD(vlcMbTypeB, kMbTypeB)
	    && BUILD(vlcCbp, kCodedBlockPattern)
	    && Vlc_Build(&m->vlcMotion, motion, n)
	    && BUILD(vlcDcLum, kDcSizeLuminance)
	    && BUILD(vlcDcChrom, kDcSizeChrominance)
	    && BUILD(vlcDct, kDctCoefficients);
#undef BUILD
}

void Mpeg1_Close(Mpeg1_t* m)
{
	if(!m)
		return;
	free(m->frames[0].y);
	free(m->frames[1].y);
	free(m->frames[2].y);
	free(m->video.packets);
	free(m->video.buf);
	free(m->video.marks);
	free(m->audio.packets);
	free(m->audio.buf);
	free(m->audio.marks);
	free(m->vlcMbai.t);
	free(m->vlcMbTypeI.t);
	free(m->vlcMbTypeP.t);
	free(m->vlcMbTypeB.t);
	free(m->vlcCbp.t);
	free(m->vlcMotion.t);
	free(m->vlcDcLum.t);
	free(m->vlcDcChrom.t);
	free(m->vlcDct.t);
	free(m);
}

Mpeg1_t* Mpeg1_Open(const uint8_t* data, size_t size)
{
	Mpeg1_t* m;
	int64_t firstVideo = -1, firstAudio = -1, lastVideo = -1;
	int i, code;
	if(!data || size < 4 || size > 0xFFFFFFFFu)
		return NULL;
	m = calloc(1, sizeof *m);
	if(!m)
		return NULL;
	m->data = data;
	m->size = size;
	if(!BuildTables(m))
	{
		printf("[Mpeg1]: A VLC table does not build (overlapping codes)\n");
		Mpeg1_Close(m);
		return NULL;
	}

	if(data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xB3)
	{
		/* A bare video elementary stream: one packet, no timestamps. */
		if(!Queue_AddPacket(&m->video, 0, (uint32_t)size, -1))
		{
			Mpeg1_Close(m);
			return NULL;
		}
	}
	else if(!Demux(m))
	{
		Mpeg1_Close(m);
		return NULL;
	}
	if(m->video.count == 0)
	{
		printf("[Mpeg1]: No MPEG video stream in the file\n");
		Mpeg1_Close(m);
		return NULL;
	}

	/* The clock's zero: the earlier of the two streams' first PTS. */
	for(i = 0; i < m->video.count; i++)
		if(m->video.packets[i].pts >= 0)
		{
			if(firstVideo < 0)
				firstVideo = m->video.packets[i].pts;
			if(m->video.packets[i].pts > lastVideo)
				lastVideo = m->video.packets[i].pts;
		}
	for(i = 0; i < m->audio.count && firstAudio < 0; i++)
		firstAudio = m->audio.packets[i].pts;
	m->basePts = firstVideo;
	if(firstAudio >= 0 && (m->basePts < 0 || firstAudio < m->basePts))
		m->basePts = firstAudio;
	if(m->basePts < 0)
		m->basePts = 0;

	/* The first sequence header gives the size and the rate. */
	do
		code = NextStartCode(m);
	while(code >= 0 && code != 0xB3);
	if(code < 0 || !ParseSequenceHeader(m))
	{
		if(code < 0)
			printf("[Mpeg1]: No sequence header in the video stream\n");
		Mpeg1_Close(m);
		return NULL;
	}
	/* Parse it again when pictures are decoded, so the stream reads from its start. */
	m->rd = m->codePos;
	m->lastPts = -1.0 / m->frameRate;
	if(lastVideo >= 0)
		m->duration = (double)(lastVideo - m->basePts) / 90000.0 + 1.0 / m->frameRate;

	/* The audio format, from its first frame. */
	if(m->audio.count > 0)
	{
		mp3dec_frame_info_t info;
		mp3dec_init(&m->mp3);
		AudioFill(m, 16384);
		memset(&info, 0, sizeof info);
		if(m->audio.len > 0)
			mp3dec_decode_frame(&m->mp3, m->audio.buf, (int)m->audio.len, NULL, &info);
		if(info.frame_bytes > 0 && (info.channels == 1 || info.channels == 2) && info.hz > 0)
		{
			m->audioRate = info.hz;
			m->audioChannels = info.channels;
			if(firstAudio > m->basePts)
				m->leadFrames = ((firstAudio - m->basePts) * info.hz + 45000) / 90000;
		}
		else
			printf("[Mpeg1]: The audio stream has no MPEG audio frame minimp3 reads; the movie plays silent\n");
		mp3dec_init(&m->mp3);
	}
	return m;
}

int Mpeg1_Width(const Mpeg1_t* m)         { return m ? m->width : 0; }
int Mpeg1_Height(const Mpeg1_t* m)        { return m ? m->height : 0; }
double Mpeg1_FrameRate(const Mpeg1_t* m)  { return m ? m->frameRate : 0.0; }
double Mpeg1_Duration(const Mpeg1_t* m)   { return m ? m->duration : 0.0; }
int Mpeg1_AudioRate(const Mpeg1_t* m)     { return m ? m->audioRate : 0; }
int Mpeg1_AudioChannels(const Mpeg1_t* m) { return m ? m->audioChannels : 0; }

/* Converts a frame and gives it a time: its own PTS, or the one after the
   last shown frame when its packet carried none. */
static void Show(Mpeg1_t* m, const Frame_t* f, uint8_t* bgrx, int stride, double* pts)
{
	double t = f->pts >= 0.0 ? f->pts : m->lastPts + 1.0 / m->frameRate;
	m->lastPts = t;
	if(pts)
		*pts = t;
	ConvertFrame(m, f, bgrx, stride);
}

/*
 * Display order (2.4.1): a B-picture is shown as soon as it is decoded; an I-
 * or P-picture is held until the next one arrives, since the B-pictures sent
 * after it are shown before it. At the end the last one held is shown.
 */
int Mpeg1_NextFrame(Mpeg1_t* m, uint8_t* bgrx, int stride, double* pts)
{
	if(!m)
		return 0;
	for(;;)
	{
		int r;
		if(m->videoEnded)
		{
			if(m->holding)
			{
				m->holding = 0;
				Show(m, m->backward, bgrx, stride, pts);
				return 1;
			}
			return 0;
		}
		r = DecodePicture(m);
		if(r == DECODED_NONE)
			m->videoEnded = 1;
		else if(r == DECODED_B)
		{
			Show(m, m->bframe, bgrx, stride, pts);
			return 1;
		}
		else if(r == DECODED_REFERENCE)
		{
			/* The previous reference is now m->forward. */
			if(m->holding)
			{
				Show(m, m->forward, bgrx, stride, pts);
				return 1;
			}
			m->holding = 1;
		}
	}
}
