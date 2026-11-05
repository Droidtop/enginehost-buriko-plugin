#ifndef __GAMEID_H__
#define __GAMEID_H__

#include <stddef.h>

// Recovers a BGI game's identifier - the constant Sys0 0xE8 returns - from an
// original executable: 1.622's code pattern first, then the "Buriko General Interpreter for
// <ID> is executing." text older runtimes carry. Writes a NUL-terminated string into `out`,
// which must have room for ENGINE_GAME_ID_SIZE bytes. Returns 1 on success.
int GameId_ReadFromExecutable(const char* path, char* out);

// Tries every *.exe in `folder` in turn. On success `out` holds the identifier and
// `exeName` the file it came from; on failure the names of the files that were
// examined are written into `examined` (comma separated, truncated to fit), so the
// caller can say which executables it looked at.
int GameId_ScanFolder(const char* folder, char* out, char* exeName, size_t exeNameSize,
	char* examined, size_t examinedSize);

// The interpreter's own version, from its executable's version resource: FileVersion's
// first "<major>.<minor>" (1.580 for "Version : 1.580.1 / Compatibility : 1.71").
// minorDigits tells 1.69 (two digits, the oldest style) from 1.690.
typedef struct
{
	int major;
	int minor;
	int minorDigits;
	char text[96];
} GameRuntimeVersion_t;

// 1 when `path` is a BGI interpreter (its FileDescription says "General Interpreter")
// with a version that parses.
int GameId_ReadRuntimeVersion(const char* path, GameRuntimeVersion_t* version);

// The first *.exe in `folder` that GameId_ReadRuntimeVersion accepts.
int GameId_ScanRuntimeVersion(const char* folder, GameRuntimeVersion_t* version, char* exeName, size_t exeNameSize);

#endif // __GAMEID_H__
