#ifndef ARC_H
#define ARC_H

#include <stdint.h>
#include <stddef.h>

/*
 * BGI archives on disk.
 *
 * The engine addresses every file as (archive, name). Until now the only
 * archive it could read was a directory of that name, which is how a game
 * looks after someone unpacks it. A game as shipped keeps its files in one of
 * the two containers the engine recognises at 0x00406EC0, both a sixteen-byte
 * header (twelve of magic, then the entry count), a table, and the data that
 * follows it, each entry naming an offset and a size relative to the end of
 * the table:
 *
 *   "BURIKO ARC20"  128-byte entries, 96 bytes of name
 *   "PackFile    "   32-byte entries, 16 bytes of name (the older one; no
 *                    game seen here ships it, but the engine reads it and so
 *                    must this one)
 *
 * Individual files inside either may additionally be DSC-compressed.
 *
 * Arc_ReadFile finds "<archive>.arc" in the game folder or its Archive/
 * subfolder, case-insensitively as the engine does elsewhere, and returns
 * the file's bytes, decompressed if they were DSC. The caller frees them.
 * NULL when the archive or the file is not there.
 */
uint8_t* Arc_ReadFile(const char* archive, const char* filename, size_t* outSize);

/* Arc_FileExists answers the same lookup without reading the file. */
int Arc_FileExists(const char* archive, const char* filename);

/*
 * A complex archive is several archives addressed under one name, which is
 * how a shipped game keeps its bulk data: Fureraba lists its forty
 * data0????.arc files and hands them to Sys0 0x38 as a single archive.
 * The original builds a DCArchiveComplex and links it into the engine's
 * archive list; a name already in that list is refused.
 *
 * Arc_CreateComplex returns 1 when the group was registered and 0 when the
 * name was taken, as the original's 0x00406BC0 does. Lookups on the group's
 * name then try each member in the order the script gave them.
 */
int Arc_CreateComplex(const char* name, const char* const* members, int count);

#endif
