#ifndef DSC_H
#define DSC_H

#include <stdint.h>

/*
 * BGI's DSC compression: LZ77 over a Huffman-coded symbol stream. file_buf is
 * the whole compressed file, header included; out_buf must hold the size the
 * header gives at offset 0x14.
 */
void decompressDSC(uint8_t* out_buf, const uint8_t* file_buf);

#endif
