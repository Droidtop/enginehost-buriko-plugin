#ifndef __CBG_H__
#define __CBG_H__

#include <stddef.h>
#include <stdint.h>

/*
 * BGI's "CompressedBG___" image container, which is what every bitmap in an ARC20
 * archive actually is. The original recognises it at 0x00401EF0 by comparing the
 * first sixteen bytes against the literal at 0x004E4138 and then reads the same
 * width / height / bit-count triple out of it that it would read from a Windows BMP
 * header, which is why the loader's error messages all talk about BMP files.
 *
 * The file is a 0x30-byte header followed by an encrypted Huffman weight table and
 * then the Huffman-coded stream:
 *
 *   0x00  char     magic[16]            "CompressedBG___\0"
 *   0x10  uint16   width
 *   0x12  uint16   height
 *   0x14  uint32   bits                 8, 24 or 32
 *   0x18  uint32   reserved[2]
 *   0x20  uint32   intermediateLength   bytes the Huffman stream decodes to
 *   0x24  uint32   key                  seed for the weight table's cipher
 *   0x28  uint32   encLength            encrypted bytes of the weight table
 *   0x2C  uint8    checkSum             sum of the decrypted weight table
 *   0x2D  uint8    checkXor             xor of the decrypted weight table
 *   0x2E  uint16   version              1
 *
 * The decoded buffer is top-down, bits/8 bytes per pixel, blue first.
 */

int CBG_IsCompressedBG(const uint8_t* file, size_t size);

/*
 * The image's own offset pair, which the original's loader reads straight out of
 * the header at 0x00401F80: the u16 at 0x1A is a flag and the u16s at 0x1C and
 * 0x1E are the offset. Returns 0 and leaves the outputs alone when the flag is
 * not set, which is the usual case.
 */
int CBG_ReadOffset(const uint8_t* file, size_t size, int* outX, int* outY);

/*
 * Decodes into a freshly allocated buffer of width * height * (bits / 8) bytes, or
 * returns NULL and leaves the outputs alone when the file is not a well-formed
 * CompressedBG. Never partially succeeds.
 */
uint8_t* CBG_Decode(const uint8_t* file, size_t size, int* outWidth, int* outHeight, int* outBits);

#endif // __CBG_H__
