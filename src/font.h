//
// Installed font families
//
// The engine's font opcodes ask the host which font families exist: on Windows
// that is EnumFontFamiliesEx, here it is a scan of the host's font directories
// with each file's own family name read out of its sfnt "name" table, which is
// the same string Windows reports.
//

#ifndef _FONT_H_
#define _FONT_H_

#include <stdint.h>

// Scans the host's font directories. Safe to call more than once; the second
// call does nothing.
void Font_Init();
void Font_Free();

// Number of distinct families found, and family i's name. Names are in the
// order they were found and are unique.
uint32_t Font_GetFamilyCount();
const char* Font_GetFamilyName(uint32_t index);

#endif
