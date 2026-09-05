#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbg.h"

#define CBG_HEADER_SIZE 0x30

/*
 * 0x00469120. Note what comes out: the HIGH half of the new key, masked to 15 bits,
 * not the key itself. Getting that wrong still produces a plausible-looking stream
 * whose checksum byte happens to match, so it is worth stating.
 */
static uint32_t CBG_UpdateKey(uint32_t* key)
{
	uint32_t low  = 20021 * (*key & 0xFFFF);
	uint32_t high = 20021 * (*key >> 16) + 346 * *key;
	high = (high + (low >> 16)) & 0xFFFF;
	*key = (high << 16) + (low & 0xFFFF) + 1;
	return high & 0x7FFF;
}

// Little-endian base-128, low group first, 0x80 marking "another group follows".
static int CBG_ReadVarint(const uint8_t* buffer, size_t size, size_t* pos, uint32_t* out)
{
	uint32_t value = 0;
	int shift = 0;
	while(*pos < size)
	{
		uint8_t code = buffer[(*pos)++];
		value |= (uint32_t)(code & 0x7F) << shift;
		shift += 7;
		if(!(code & 0x80))
		{
			*out = value;
			return 1;
		}
		if(shift > 28)
			return 0;
	}
	return 0;
}

typedef struct CbgNode
{
	int      valid;
	uint32_t weight;
	int      left;
	int      right;
} CbgNode_t;

// The canonical "join the two lightest nodes" tree: 256 leaves, at most 255 joins.
static int CBG_BuildTree(CbgNode_t* nodes, const uint32_t* weights)
{
	for(int i = 0; i < 256; i++)
	{
		nodes[i].valid  = weights[i] != 0;
		nodes[i].weight = weights[i];
		nodes[i].left   = i;
		nodes[i].right  = 0;
	}

	int next = 256;
	int root = 0;
	for(;;)
	{
		int first = -1;
		int second = -1;
		for(int i = 0; i < next; i++)
		{
			if(!nodes[i].valid)
				continue;
			if(first < 0 || nodes[i].weight < nodes[first].weight)
			{
				second = first;
				first = i;
			}
			else if(second < 0 || nodes[i].weight < nodes[second].weight)
			{
				second = i;
			}
		}
		if(second < 0)
		{
			// One node left: it is the root. A file using a single byte value has a
			// root that is itself a leaf, and the decoder below then reads no bits.
			return first < 0 ? -1 : first;
		}
		nodes[next].valid  = 1;
		nodes[next].weight = nodes[first].weight + nodes[second].weight;
		nodes[next].left   = first;
		nodes[next].right  = second;
		nodes[first].valid  = 0;
		nodes[second].valid = 0;
		root = next;
		next++;
	}
	return root;
}

// Most significant bit first.
static int CBG_HuffmanDecode(const uint8_t* data, size_t size, size_t pos,
                             const CbgNode_t* nodes, int root,
                             uint8_t* out, size_t outSize)
{
	int bit = 0;
	for(size_t i = 0; i < outSize; i++)
	{
		int node = root;
		while(node >= 256)
		{
			if(pos >= size)
				return 0;
			int value = (data[pos] >> (7 - bit)) & 1;
			if(++bit == 8)
			{
				bit = 0;
				pos++;
			}
			node = value ? nodes[node].right : nodes[node].left;
		}
		out[i] = (uint8_t)node;
	}
	return 1;
}

/*
 * The intermediate buffer is runs of literal bytes and runs of zeroes, alternating
 * and starting with literals, each run introduced by its length as a varint.
 */
static void CBG_UnpackZeros(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize)
{
	size_t src = 0;
	size_t dst = 0;
	int zeroes = 0;
	while(dst < outSize)
	{
		uint32_t count;
		if(!CBG_ReadVarint(in, inSize, &src, &count))
			break;
		if(count > outSize - dst)
			count = (uint32_t)(outSize - dst);
		if(zeroes)
		{
			memset(out + dst, 0, count);
		}
		else
		{
			if(count > inSize - src)
				count = (uint32_t)(inSize - src);
			memcpy(out + dst, in + src, count);
			src += count;
		}
		dst += count;
		zeroes = !zeroes;
	}
}

/*
 * Each byte is stored as its difference from the average of the byte to its left and
 * the byte above it, per channel; undoing that in place is the last step.
 */
static void CBG_ReverseAverageSampling(uint8_t* out, int width, int height, int pixelBytes)
{
	int stride = width * pixelBytes;
	for(int y = 0; y < height; y++)
	{
		for(int x = 0; x < width; x++)
		{
			int base = y * stride + x * pixelBytes;
			for(int c = 0; c < pixelBytes; c++)
			{
				int average = 0;
				if(x > 0)
					average += out[base - pixelBytes + c];
				if(y > 0)
					average += out[base - stride + c];
				if(x > 0 && y > 0)
					average /= 2;
				out[base + c] = (uint8_t)(out[base + c] + average);
			}
		}
	}
}

int CBG_IsCompressedBG(const uint8_t* file, size_t size)
{
	return file != NULL && size >= CBG_HEADER_SIZE && memcmp(file, "CompressedBG___", 16) == 0;
}

uint8_t* CBG_Decode(const uint8_t* file, size_t size, int* outWidth, int* outHeight, int* outBits)
{
	if(!CBG_IsCompressedBG(file, size))
		return NULL;

	int width  = file[0x10] | (file[0x11] << 8);
	int height = file[0x12] | (file[0x13] << 8);
	uint32_t bits       = (uint32_t)file[0x14] | ((uint32_t)file[0x15] << 8) | ((uint32_t)file[0x16] << 16) | ((uint32_t)file[0x17] << 24);
	uint32_t interLen   = (uint32_t)file[0x20] | ((uint32_t)file[0x21] << 8) | ((uint32_t)file[0x22] << 16) | ((uint32_t)file[0x23] << 24);
	uint32_t key        = (uint32_t)file[0x24] | ((uint32_t)file[0x25] << 8) | ((uint32_t)file[0x26] << 16) | ((uint32_t)file[0x27] << 24);
	uint32_t encLen     = (uint32_t)file[0x28] | ((uint32_t)file[0x29] << 8) | ((uint32_t)file[0x2A] << 16) | ((uint32_t)file[0x2B] << 24);
	uint8_t  checkSum   = file[0x2C];
	uint8_t  checkXor   = file[0x2D];

	if(width <= 0 || height <= 0 || (bits != 8 && bits != 24 && bits != 32))
	{
		printf("[CBG]: Unsupported image: %dx%d, %u bits\n", width, height, bits);
		return NULL;
	}
	if(encLen == 0 || (size_t)CBG_HEADER_SIZE + encLen > size || interLen == 0)
	{
		printf("[CBG]: Truncated image (%zu bytes, weight table %u)\n", size, encLen);
		return NULL;
	}

	uint8_t* weightTable = (uint8_t*)malloc(encLen);
	if(weightTable == NULL)
		return NULL;
	memcpy(weightTable, file + CBG_HEADER_SIZE, encLen);

	uint8_t sum = 0;
	uint8_t xorAll = 0;
	for(uint32_t i = 0; i < encLen; i++)
	{
		weightTable[i] = (uint8_t)(weightTable[i] - (uint8_t)CBG_UpdateKey(&key));
		sum = (uint8_t)(sum + weightTable[i]);
		xorAll ^= weightTable[i];
	}
	if(sum != checkSum || xorAll != checkXor)
	{
		printf("[CBG]: Weight table failed its check (%02X/%02X, expected %02X/%02X)\n",
		       sum, xorAll, checkSum, checkXor);
		free(weightTable);
		return NULL;
	}

	uint32_t weights[256];
	size_t pos = 0;
	for(int i = 0; i < 256; i++)
	{
		if(!CBG_ReadVarint(weightTable, encLen, &pos, &weights[i]))
		{
			printf("[CBG]: Weight table ended after %d of 256 entries\n", i);
			free(weightTable);
			return NULL;
		}
	}
	free(weightTable);

	CbgNode_t nodes[511];
	int root = CBG_BuildTree(nodes, weights);
	if(root < 0)
	{
		printf("[CBG]: Weight table is empty\n");
		return NULL;
	}

	uint8_t* intermediate = (uint8_t*)malloc(interLen);
	if(intermediate == NULL)
		return NULL;
	if(!CBG_HuffmanDecode(file, size, CBG_HEADER_SIZE + encLen, nodes, root, intermediate, interLen))
	{
		printf("[CBG]: Huffman stream ended early\n");
		free(intermediate);
		return NULL;
	}

	int pixelBytes = (int)(bits / 8);
	size_t outSize = (size_t)width * (size_t)height * (size_t)pixelBytes;
	uint8_t* out = (uint8_t*)calloc(1, outSize);
	if(out == NULL)
	{
		free(intermediate);
		return NULL;
	}
	CBG_UnpackZeros(intermediate, interLen, out, outSize);
	free(intermediate);
	CBG_ReverseAverageSampling(out, width, height, pixelBytes);

	*outWidth  = width;
	*outHeight = height;
	*outBits   = (int)bits;
	return out;
}
