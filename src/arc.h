#ifndef ARC_H
#define ARC_H

#include <stdint.h>
#include <stddef.h>

/*
 * BGI archives on disk.
 *
 * The engine addresses every file as (archive, name). Until now the only
 * archive it could read was a directory of that name, which is how a game
 * looks after someone unpacks it. A game as shipped keeps its files in
 * "BURIKO ARC20" containers: a count, a table of 128-byte entries (96 bytes
 * of name, then offset and size relative to the end of the table), and the
 * data. Individual files inside may additionally be DSC-compressed.
 *
 * Arc_ReadFile finds "<archive>.arc" in the game folder or its Archive/
 * subfolder, case-insensitively as the engine does elsewhere, and returns
 * the file's bytes, decompressed if they were DSC. The caller frees them.
 * NULL when the archive or the file is not there.
 */
uint8_t* Arc_ReadFile(const char* archive, const char* filename, size_t* outSize);

/* Arc_FileExists answers the same lookup without reading the file. */
int Arc_FileExists(const char* archive, const char* filename);

#endif
