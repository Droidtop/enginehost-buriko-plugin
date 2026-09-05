#ifndef __GAMEID_H__
#define __GAMEID_H__

#include <stddef.h>

// Recovers a BGI game's identifier - the constant Sys0 0xE8 returns - from an
// original executable. Writes a NUL-terminated string of at most 15 characters into
// `out`, which must have room for ENGINE_GAME_ID_SIZE bytes. Returns 1 on success.
int GameId_ReadFromExecutable(const char* path, char* out);

// Tries every *.exe in `folder` in turn. On success `out` holds the identifier and
// `exeName` the file it came from; on failure the names of the files that were
// examined are written into `examined` (comma separated, truncated to fit), so the
// caller can say which executables it looked at.
int GameId_ScanFolder(const char* folder, char* out, char* exeName, size_t exeNameSize,
	char* examined, size_t examinedSize);

#endif // __GAMEID_H__
