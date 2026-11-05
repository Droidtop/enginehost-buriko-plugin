#ifndef GDB_H_
#define GDB_H_

#include <stdint.h>
#include <stddef.h>

/*
 * The global database and the three stores it is made of, as fureraba.exe keeps
 * them. BGI.gdb is what survives between runs of the game: the first 0x400
 * bytes of global memory, a 1 MB persistent block, the string table 0x80000000
 * and the named bit-flag store. Sys0 0x80 reads it (0x0046B800) and Sys0 0x81
 * writes it (0x0046B640), SDC-encoded (0x00493B10) into the game's own folder,
 * as the original does.
 */

// ---------------------------------------------------------------- SDC
// "SDC FORMAT 1.00": a 0x20-byte header (magic, key, payload size, decoded size,
// 16-bit sum and xor of the payload) and an LZ payload enciphered with the same
// keystream as DSC. Decode answers the decoded size, or 0 when the file is not
// SDC, fails its checksums or does not decode to the size it names (0x00493A80).
uint32_t SDC_DecodedSize(const uint8_t* file, size_t fileSize);
uint32_t SDC_Decode(const uint8_t* file, size_t fileSize, uint8_t* out, uint32_t outSize);
// Encodes into a buffer the caller frees; answers the file size or 0.
uint32_t SDC_Encode(const uint8_t* data, uint32_t size, uint32_t key, uint8_t** outFile);

// ---------------------------------------------------------------- DCFS
// User data (Sys0 0xC4 / 0xC5, 0x00447220 / 0x00447410; Droidtop/tracker#123, the local
// lane's trace, checked against two real saves): a 0x18-byte header (the 16 characters
// "DCFS FORMAT 1.00" with no NUL, the record size, the record count), the first record raw,
// and every later record as runs against the one before it, alternating "the same for N
// bytes" and "N bytes follow", always starting with "same", lengths in unsigned LEB128,
// summing exactly to the record size, with nothing after the last record.
#define DCFS_HEADER_SIZE 0x18
#define DCFS_ERR_COUNT   0x80000001u
#define DCFS_ERR_SIZE    0x80000002u
#define DCFS_NOT_DCFS    0x80000003u
#define DCFS_BAD_RUNS    0x80000004u
// Encodes `count` records of `size` bytes into a buffer the caller frees. 0 and *out, *outSize
// on success; DCFS_ERR_COUNT or DCFS_ERR_SIZE; 0xFFFFFFFF when the result does not decode back.
uint32_t DCFS_Encode(const uint8_t* records, uint32_t size, uint32_t count, uint8_t** out, size_t* outSize);
// Decodes into `out` (outSize bytes): 0, DCFS_NOT_DCFS, or DCFS_BAD_RUNS (a run past the record,
// runs that do not sum to it, a stream that ends early, or records that do not fit `out`).
uint32_t DCFS_Decode(const uint8_t* data, size_t dataSize, uint8_t* out, size_t outSize);

// ---------------------------------------------------------------- string tables
// The registry at 0x00503E00: tables of strings keyed by a number, each string
// kept with its hash (0x00452BC0) and length. Table 0x80000000 is the one the
// global database keeps.
#define STRTAB_GLOBAL_ID 0x80000000u
uint32_t StrTab_Add(uint32_t id, const char* s);                       // 0x00495870
uint32_t StrTab_Count(uint32_t id);                                    // 0x00495850
uint32_t StrTab_Remove(uint32_t id);                                   // 0x00495670
void     StrTab_RemoveAll(int keepGlobal);                             // 0x00495630
uint32_t StrTab_Load(uint32_t id, uint32_t count, const char* data);   // 0x004956E0
uint32_t StrTab_Serialize(uint32_t id, uint8_t* out);                  // 0x004957D0
uint32_t StrTab_Read(uint32_t id, int32_t index, char* out, uint32_t* length); // 0x004959E0
uint32_t StrTab_GlobalHas(const char* s);                              // 0x00495A50

// ---------------------------------------------------------------- flag store
// The named bit arrays at 0x0056676C. Every lookup moves the entry it finds to
// the front of the list (0x00447120), which is the order the database keeps.
uint32_t Flags_Define(const char* name, uint32_t bits);                           // 0x00446C60
uint32_t Flags_Set(const char* name, uint32_t index, uint32_t value);             // 0x00446DB0
uint32_t Flags_SetRange(const char* name, uint32_t start, uint32_t count, uint32_t value); // 0x00446E20
uint32_t Flags_Get(const char* name, uint32_t index, uint32_t* value);            // 0x00446EC0
void     Flags_Clear(void);                                                       // 0x00446C20

// ---------------------------------------------------------------- persistent block
// 0x00566760: 1 MB the scripts copy in and out of with Sys0 0x82 and 0x83, kept
// in the database (allocated at 0x0046D340, cleared at 0x0046D5A3).
#define PERSISTENT_SIZE 0x100000u
uint8_t* Persistent_Block(void);

// ---------------------------------------------------------------- the database
// 0 on success, 0x80000001 when there is no BGI.gdb, 0x80000002 when it is not
// one this engine reads. *left and *top are the window position it keeps.
uint32_t GDB_Load(uint8_t* globalMem, uint32_t globalSize, int32_t* left, int32_t* top);
// 1 when the file was written, 0 when it was not.
uint32_t GDB_Save(const uint8_t* globalMem, uint32_t globalSize);
void     GDB_FreeAll(void);

// The layout a save writes, from the game's own runtime version (gameid.h reads it from
// the executable): GDB_LayoutForRuntime maps a version to the layout that runtime writes,
// GDB_LAYOUT_UNKNOWN for a version not checked. Unknown keeps the layout the file was
// read in, and 3.00 for a game that has no BGI.gdb yet.
#define GDB_LAYOUT_UNKNOWN (-1)
#define GDB_LAYOUT_300     0
#define GDB_LAYOUT_OLDER   1
int  GDB_LayoutForRuntime(int major, int minor, int minorDigits);
void GDB_SetRuntimeLayout(int layout);

#endif
