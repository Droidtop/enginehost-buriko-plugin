//
// SJIS to UTF16 conversion
//

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _SJIS_H_
#define _SJIS_H_

uint16_t SjisToUTF16(uint8_t* in);
bool SjisIsTwobyte(uint8_t* in);
uint16_t SjisGetChar(uint8_t* in, bool* isTwoByte);
int Utf16ToSjis(uint16_t ch, uint8_t out[2]);

#endif
