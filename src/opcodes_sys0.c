#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "engine.h"
#include "arc.h"
#include "region.h"
#include "process.h"
#include "opcodes.h"
#include "opcodes_sys0.h"
#include "object.h"
#include "os.h"
#include "thread.h"
#include "gdb.h"
#include "input.h"

static uint32_t Sys0_FlagResult(uint32_t r);

char* OpcodesSys0Mnemonics[256] = {
	/* 0x00   0 */ "Srand",
	/* 0x01   1 */ "Unknown_1",
	/* 0x02   2 */ "Random",
	/* 0x03   3 */ "--Unknown--",
	/* 0x04   4 */ "GetSysTime",
	/* 0x05   5 */ "--Unknown--",
	/* 0x06   6 */ "SetClockBase",
	/* 0x07   7 */ "--Unknown--",
	/* 0x08   8 */ "Unknown_8",
	/* 0x09   9 */ "--Unknown--",
	/* 0x0A  10 */ "Unknown_10",
	/* 0x0B  11 */ "Unknown_11",
	/* 0x0C  12 */ "GetLocalTime",
	/* 0x0D  13 */ "GetPhysicalMemory",
	/* 0x0E  14 */ "Unknown_14",
	/* 0x0F  15 */ "IsWindowActive",
	/* 0x10  16 */ "SetInputEnabled",
	/* 0x11  17 */ "IsKeyDown",
	/* 0x12  18 */ "CountKeyPresses",
	/* 0x13  19 */ "InputActivity",
	/* 0x14  20 */ "SetSkipAllowed",
	/* 0x15  21 */ "SetSkipLatch",
	/* 0x16  22 */ "SkipToggle",
	/* 0x17  23 */ "IsSkipping",
	/* 0x18  24 */ "AddRegion",
	/* 0x19  25 */ "RemoveRegion",
	/* 0x1A  26 */ "TakeRegionInput",
	/* 0x1B  27 */ "SetButtonKeys",
	/* 0x1C  28 */ "CountButtonPresses",
	/* 0x1D  29 */ "TakeRegionKey",
	/* 0x1E  30 */ "SetSwapButtons",
	/* 0x1F  31 */ "Unknown_31",
	/* 0x20  32 */ "AllocAuxMem",
	/* 0x21  33 */ "FreeAuxMem",
	/* 0x22  34 */ "--Unknown--",
	/* 0x23  35 */ "--Unknown--",
	/* 0x24  36 */ "Unknown_36",
	/* 0x25  37 */ "ListFiles",
	/* 0x26  38 */ "--Unknown--",
	/* 0x27  39 */ "--Unknown--",
	/* 0x28  40 */ "CreateDirectory",
	/* 0x29  41 */ "RemoveDirectory",
	/* 0x2A  42 */ "IsDirectory",
	/* 0x2B  43 */ "--Unknown--",
	/* 0x2C  44 */ "Unknown_44",
	/* 0x2D  45 */ "Unknown_45",
	/* 0x2E  46 */ "--Unknown--",
	/* 0x2F  47 */ "Unknown_47",
	/* 0x30  48 */ "ReadFile",
	/* 0x31  49 */ "LoadFile",
	/* 0x32  50 */ "SaveFile",
	/* 0x33  51 */ "DeleteFile",
	/* 0x34  52 */ "FindFile",
	/* 0x35  53 */ "GetFileSize",
	/* 0x36  54 */ "EnableSearchPaths",
	/* 0x37  55 */ "AddSearchPath",
	/* 0x38  56 */ "CreateComplexArchive",
	/* 0x39  57 */ "SetUserDirectory",
	/* 0x3A  58 */ "Unknown_58",
	/* 0x3B  59 */ "Unknown_59",
	/* 0x3C  60 */ "Unknown_60",
	/* 0x3D  61 */ "GetDirectory",
	/* 0x3E  62 */ "Unknown_62",
	/* 0x3F  63 */ "Unknown_63",
	/* 0x40  64 */ "LoadProgram",
	/* 0x41  65 */ "DeleteProgram",
	/* 0x42  66 */ "--Unknown--",
	/* 0x43  67 */ "--Unknown--",
	/* 0x44  68 */ "CreateThread",
	/* 0x45  69 */ "Unknown_69",
	/* 0x46  70 */ "GetThreadID",
	/* 0x47  71 */ "ThreadExists",
	/* 0x48  72 */ "PostMessage",
	/* 0x49  73 */ "TakeMessage",
	/* 0x4A  74 */ "PostMessages",
	/* 0x4B  75 */ "TakeMessages",
	/* 0x4C  76 */ "Unknown_76",
	/* 0x4D  77 */ "--Unknown--",
	/* 0x4E  78 */ "--Unknown--",
	/* 0x4F  79 */ "--Unknown--",
	/* 0x50  80 */ "SetProcessesWait",
	/* 0x51  81 */ "--Unknown--",
	/* 0x52  82 */ "SetIdleWaitTime",
	/* 0x53  83 */ "--Unknown--",
	/* 0x54  84 */ "Unknown_84",
	/* 0x55  85 */ "--Unknown--",
	/* 0x56  86 */ "--Unknown--",
	/* 0x57  87 */ "--Unknown--",
	/* 0x58  88 */ "SetTimer",
	/* 0x59  89 */ "Unknown_89",
	/* 0x5A  90 */ "Unknown_90",
	/* 0x5B  91 */ "--Unknown--",
	/* 0x5C  92 */ "WaitTiming",
	/* 0x5D  93 */ "Unknown_93",
	/* 0x5E  94 */ "SwitchToThread",
	/* 0x5F  95 */ "Yield",
	/* 0x60  96 */ "SetDisplayMode",
	/* 0x61  97 */ "Unknown_97",
	/* 0x62  98 */ "SetKeySlots",
	/* 0x63  99 */ "SetScreenMappingModeFlag",
	/* 0x64 100 */ "SetWindowVisible",
	/* 0x65 101 */ "Unknown_101",
	/* 0x66 102 */ "SetWindowTitle",
	/* 0x67 103 */ "SetCursorShape",
	/* 0x68 104 */ "SetGlobalUnknownVal001",
	/* 0x69 105 */ "Unknown_105",
	/* 0x6A 106 */ "YieldAndEndPass",
	/* 0x6B 107 */ "Unknown_107",
	/* 0x6C 108 */ "Unknown_108",
	/* 0x6D 109 */ "Unknown_109",
	/* 0x6E 110 */ "--Unknown--",
	/* 0x6F 111 */ "Unknown_111",
	/* 0x70 112 */ "InitGlobalMem",
	/* 0x71 113 */ "Unknown_113",
	/* 0x72 114 */ "--Unknown--",
	/* 0x73 115 */ "--Unknown--",
	/* 0x74 116 */ "SetFlagUnknown10",
	/* 0x75 117 */ "--Unknown--",
	/* 0x76 118 */ "--Unknown--",
	/* 0x77 119 */ "--Unknown--",
	/* 0x78 120 */ "Unknown_120",
	/* 0x79 121 */ "Unknown_121",
	/* 0x7A 122 */ "Unknown_122",
	/* 0x7B 123 */ "Unknown_123",
	/* 0x7C 124 */ "--Unknown--",
	/* 0x7D 125 */ "--Unknown--",
	/* 0x7E 126 */ "--Unknown--",
	/* 0x7F 127 */ "--Unknown--",
	/* 0x80 128 */ "LoadGlobalDatabase",
	/* 0x81 129 */ "SaveGlobalDatabase",
	/* 0x82 130 */ "WritePersistent",
	/* 0x83 131 */ "ReadPersistent",
	/* 0x84 132 */ "AddGlobalString",
	/* 0x85 133 */ "HasGlobalString",
	/* 0x86 134 */ "--Unknown--",
	/* 0x87 135 */ "--Unknown--",
	/* 0x88 136 */ "DefineFlags",
	/* 0x89 137 */ "SetFlag",
	/* 0x8A 138 */ "SetFlagRange",
	/* 0x8B 139 */ "GetFlag",
	/* 0x8C 140 */ "--Unknown--",
	/* 0x8D 141 */ "--Unknown--",
	/* 0x8E 142 */ "--Unknown--",
	/* 0x8F 143 */ "--Unknown--",
	/* 0x90 144 */ "Unknown_144",
	/* 0x91 145 */ "Unknown_145",
	/* 0x92 146 */ "--Unknown--",
	/* 0x93 147 */ "--Unknown--",
	/* 0x94 148 */ "Unknown_148",
	/* 0x95 149 */ "Unknown_149",
	/* 0x96 150 */ "Unknown_150",
	/* 0x97 151 */ "Unknown_151",
	/* 0x98 152 */ "CreateRecordList",
	/* 0x99 153 */ "DestroyRecordList",
	/* 0x9A 154 */ "RecordListCount",
	/* 0x9B 155 */ "--Unknown--",
	/* 0x9C 156 */ "AddToRecordList",
	/* 0x9D 157 */ "ReadRecordList",
	/* 0x9E 158 */ "DropFromRecordList",
	/* 0x9F 159 */ "--Unknown--",
	/* 0xA0 160 */ "PopGlobalList",
	/* 0xA1 161 */ "PushGlobalList",
	/* 0xA2 162 */ "--Unknown--",
	/* 0xA3 163 */ "--Unknown--",
	/* 0xA4 164 */ "--Unknown--",
	/* 0xA5 165 */ "--Unknown--",
	/* 0xA6 166 */ "--Unknown--",
	/* 0xA7 167 */ "--Unknown--",
	/* 0xA8 168 */ "Unknown_168",
	/* 0xA9 169 */ "Unknown_169",
	/* 0xAA 170 */ "--Unknown--",
	/* 0xAB 171 */ "--Unknown--",
	/* 0xAC 172 */ "Unknown_172",
	/* 0xAD 173 */ "--Unknown--",
	/* 0xAE 174 */ "--Unknown--",
	/* 0xAF 175 */ "--Unknown--",
	/* 0xB0 176 */ "Unknown_176",
	/* 0xB1 177 */ "Unknown_177",
	/* 0xB2 178 */ "--Unknown--",
	/* 0xB3 179 */ "--Unknown--",
	/* 0xB4 180 */ "Unknown_180",
	/* 0xB5 181 */ "Unknown_181",
	/* 0xB6 182 */ "Unknown_182",
	/* 0xB7 183 */ "--Unknown--",
	/* 0xB8 184 */ "--Unknown--",
	/* 0xB9 185 */ "--Unknown--",
	/* 0xBA 186 */ "--Unknown--",
	/* 0xBB 187 */ "--Unknown--",
	/* 0xBC 188 */ "--Unknown--",
	/* 0xBD 189 */ "--Unknown--",
	/* 0xBE 190 */ "--Unknown--",
	/* 0xBF 191 */ "--Unknown--",
	/* 0xC0 192 */ "Unknown_192",
	/* 0xC1 193 */ "Unknown_193",
	/* 0xC2 194 */ "--Unknown--",
	/* 0xC3 195 */ "--Unknown--",
	/* 0xC4 196 */ "Unknown_196",
	/* 0xC5 197 */ "Unknown_197",
	/* 0xC6 198 */ "--Unknown--",
	/* 0xC7 199 */ "--Unknown--",
	/* 0xC8 200 */ "--Unknown--",
	/* 0xC9 201 */ "--Unknown--",
	/* 0xCA 202 */ "--Unknown--",
	/* 0xCB 203 */ "--Unknown--",
	/* 0xCC 204 */ "--Unknown--",
	/* 0xCD 205 */ "--Unknown--",
	/* 0xCE 206 */ "--Unknown--",
	/* 0xCF 207 */ "--Unknown--",
	/* 0xD0 208 */ "CreateRecordTable",
	/* 0xD1 209 */ "DestroyRecordTable",
	/* 0xD2 210 */ "SetRecord",
	/* 0xD3 211 */ "DeleteRecord",
	/* 0xD4 212 */ "ReadRecord",
	/* 0xD5 213 */ "--Unknown--",
	/* 0xD6 214 */ "--Unknown--",
	/* 0xD7 215 */ "--Unknown--",
	/* 0xD8 216 */ "ClearStringTables",
	/* 0xD9 217 */ "StringTableCount",
	/* 0xDA 218 */ "LoadStringTable",
	/* 0xDB 219 */ "SerializeStringTable",
	/* 0xDC 220 */ "AddString",
	/* 0xDD 221 */ "ReadString",
	/* 0xDE 222 */ "StringLength",
	/* 0xDF 223 */ "--Unknown--",
	/* 0xE0 224 */ "Unknown_224",
	/* 0xE1 225 */ "Unknown_225",
	/* 0xE2 226 */ "Unknown_226",
	/* 0xE3 227 */ "Unknown_227",
	/* 0xE4 228 */ "--Unknown--",
	/* 0xE5 229 */ "--Unknown--",
	/* 0xE6 230 */ "--Unknown--",
	/* 0xE7 231 */ "--Unknown--",
	/* 0xE8 232 */ "GetGameId",
	/* 0xE9 233 */ "--Unknown--",
	/* 0xEA 234 */ "--Unknown--",
	/* 0xEB 235 */ "--Unknown--",
	/* 0xEC 236 */ "Unknown_236",
	/* 0xED 237 */ "Unknown_237",
	/* 0xEE 238 */ "Unknown_238",
	/* 0xEF 239 */ "Unknown_239",
	/* 0xF0 240 */ "Unknown_240",
	/* 0xF1 241 */ "Unknown_241",
	/* 0xF2 242 */ "Unknown_242",
	/* 0xF3 243 */ "Unknown_243",
	/* 0xF4 244 */ "Unknown_244",
	/* 0xF5 245 */ "Unknown_245",
	/* 0xF6 246 */ "Unknown_246",
	/* 0xF7 247 */ "Unknown_247",
	/* 0xF8 248 */ "Unknown_248",
	/* 0xF9 249 */ "Unknown_249",
	/* 0xFA 250 */ "Unknown_250",
	/* 0xFB 251 */ "Unknown_251",
	/* 0xFC 252 */ "Unknown_252",
	/* 0xFD 253 */ "IsLauncher",
	/* 0xFE 254 */ "Unknown_254",
	/* 0xFF 255 */ "--Unknown--"
};

OpcodePtr_t OpcodesSys0[256] = {
	/* 0x00   0 */ Opcode_Sys0_Srand,
	/* 0x01   1 */ Opcode_Sys0_Unknown_1,
	/* 0x02   2 */ Opcode_Sys0_Random,
	/* 0x03   3 */ NULL,
	/* 0x04   4 */ Opcode_Sys0_GetSysTime,
	/* 0x05   5 */ NULL,
	/* 0x06   6 */ Opcode_Sys0_SetClockBase,
	/* 0x07   7 */ NULL,
	/* 0x08   8 */ Opcode_Sys0_Unknown_8,
	/* 0x09   9 */ NULL,
	/* 0x0A  10 */ Opcode_Sys0_Unknown_10,
	/* 0x0B  11 */ Opcode_Sys0_Unknown_11,
	/* 0x0C  12 */ Opcode_Sys0_GetLocalTime,
	/* 0x0D  13 */ Opcode_Sys0_GetPhysicalMemory,
	/* 0x0E  14 */ Opcode_Sys0_Unknown_14,
	/* 0x0F  15 */ Opcode_Sys0_IsWindowActive,
	/* 0x10  16 */ Opcode_Sys0_SetInputEnabled,
	/* 0x11  17 */ Opcode_Sys0_IsKeyDown,
	/* 0x12  18 */ Opcode_Sys0_CountKeyPresses,
	/* 0x13  19 */ Opcode_Sys0_InputActivity,
	/* 0x14  20 */ Opcode_Sys0_SetSkipAllowed,
	/* 0x15  21 */ Opcode_Sys0_SetSkipLatch,
	/* 0x16  22 */ Opcode_Sys0_SkipToggle,
	/* 0x17  23 */ Opcode_Sys0_IsSkipping,
	/* 0x18  24 */ Opcode_Sys0_AddRegion,
	/* 0x19  25 */ Opcode_Sys0_RemoveRegion,
	/* 0x1A  26 */ Opcode_Sys0_TakeRegionInput,
	/* 0x1B  27 */ Opcode_Sys0_SetButtonKeys,
	/* 0x1C  28 */ Opcode_Sys0_CountButtonPresses,
	/* 0x1D  29 */ Opcode_Sys0_TakeRegionKey,
	/* 0x1E  30 */ Opcode_Sys0_SetSwapButtons,
	/* 0x1F  31 */ Opcode_Sys0_Unknown_31,
	/* 0x20  32 */ Opcode_Sys0_AllocAuxMem,
	/* 0x21  33 */ Opcode_Sys0_FreeAuxMem,
	/* 0x22  34 */ NULL,
	/* 0x23  35 */ NULL,
	/* 0x24  36 */ Opcode_Sys0_Unknown_36,
	/* 0x25  37 */ Opcode_Sys0_ListFiles,
	/* 0x26  38 */ NULL,
	/* 0x27  39 */ NULL,
	/* 0x28  40 */ Opcode_Sys0_CreateDirectory,
	/* 0x29  41 */ Opcode_Sys0_RemoveDirectory,
	/* 0x2A  42 */ Opcode_Sys0_IsDirectory,
	/* 0x2B  43 */ NULL,
	/* 0x2C  44 */ Opcode_Sys0_Unknown_44,
	/* 0x2D  45 */ Opcode_Sys0_Unknown_45,
	/* 0x2E  46 */ NULL,
	/* 0x2F  47 */ Opcode_Sys0_Unknown_47,
	/* 0x30  48 */ Opcode_Sys0_ReadFile,
	/* 0x31  49 */ Opcode_Sys0_LoadFile,
	/* 0x32  50 */ Opcode_Sys0_SaveFile,
	/* 0x33  51 */ Opcode_Sys0_DeleteFile,
	/* 0x34  52 */ Opcode_Sys0_FindFile,
	/* 0x35  53 */ Opcode_Sys0_GetFileSize,
	/* 0x36  54 */ Opcode_Sys0_EnableSearchPaths,
	/* 0x37  55 */ Opcode_Sys0_AddSearchPath,
	/* 0x38  56 */ Opcode_Sys0_CreateComplexArchive,
	/* 0x39  57 */ Opcode_Sys0_SetUserDirectory,
	/* 0x3A  58 */ Opcode_Sys0_Unknown_58,
	/* 0x3B  59 */ Opcode_Sys0_Unknown_59,
	/* 0x3C  60 */ Opcode_Sys0_Unknown_60,
	/* 0x3D  61 */ Opcode_Sys0_GetDirectory,
	/* 0x3E  62 */ Opcode_Sys0_Unknown_62,
	/* 0x3F  63 */ Opcode_Sys0_Unknown_63,
	/* 0x40  64 */ Opcode_Sys0_LoadProgram,
	/* 0x41  65 */ Opcode_Sys0_DeleteProgram,
	/* 0x42  66 */ NULL,
	/* 0x43  67 */ NULL,
	/* 0x44  68 */ Opcode_Sys0_CreateThread,
	/* 0x45  69 */ Opcode_Sys0_Unknown_69,
	/* 0x46  70 */ Opcode_Sys0_GetThreadID,
	/* 0x47  71 */ Opcode_Sys0_ThreadExists,
	/* 0x48  72 */ Opcode_Sys0_PostMessage,
	/* 0x49  73 */ Opcode_Sys0_TakeMessage,
	/* 0x4A  74 */ Opcode_Sys0_PostMessages,
	/* 0x4B  75 */ Opcode_Sys0_TakeMessages,
	/* 0x4C  76 */ Opcode_Sys0_Unknown_76,
	/* 0x4D  77 */ NULL,
	/* 0x4E  78 */ NULL,
	/* 0x4F  79 */ NULL,
	/* 0x50  80 */ Opcode_Sys0_SetProcessesWait,
	/* 0x51  81 */ NULL,
	/* 0x52  82 */ Opcode_Sys0_SetIdleWaitTime,
	/* 0x53  83 */ NULL,
	/* 0x54  84 */ Opcode_Sys0_Unknown_84,
	/* 0x55  85 */ NULL,
	/* 0x56  86 */ NULL,
	/* 0x57  87 */ NULL,
	/* 0x58  88 */ Opcode_Sys0_SetTimer,
	/* 0x59  89 */ Opcode_Sys0_Unknown_89,
	/* 0x5A  90 */ Opcode_Sys0_Unknown_90,
	/* 0x5B  91 */ NULL,
	/* 0x5C  92 */ Opcode_Sys0_WaitTiming,
	/* 0x5D  93 */ Opcode_Sys0_Unknown_93,
	/* 0x5E  94 */ Opcode_Sys0_SwitchToThread,
	/* 0x5F  95 */ Opcode_Sys0_Yield,
	/* 0x60  96 */ Opcode_Sys0_SetDisplayMode,
	/* 0x61  97 */ Opcode_Sys0_Unknown_97,
	/* 0x62  98 */ Opcode_Sys0_SetKeySlots,
	/* 0x63  99 */ Opcode_Sys0_SetScreenMappingModeFlag,
	/* 0x64 100 */ Opcode_Sys0_SetWindowVisible,
	/* 0x65 101 */ Opcode_Sys0_Unknown_101,
	/* 0x66 102 */ Opcode_Sys0_SetWindowTitle,
	/* 0x67 103 */ Opcode_Sys0_SetCursorShape,
	/* 0x68 104 */ Opcode_Sys0_SetGlobalUnknownVal001,
	/* 0x69 105 */ Opcode_Sys0_Unknown_105,
	/* 0x6A 106 */ Opcode_Sys0_YieldAndEndPass,
	/* 0x6B 107 */ Opcode_Sys0_Unknown_107,
	/* 0x6C 108 */ Opcode_Sys0_Unknown_108,
	/* 0x6D 109 */ Opcode_Sys0_Unknown_109,
	/* 0x6E 110 */ NULL,
	/* 0x6F 111 */ Opcode_Sys0_Unknown_111,
	/* 0x70 112 */ Opcode_Sys0_InitGlobalMem,
	/* 0x71 113 */ Opcode_Sys0_Unknown_113,
	/* 0x72 114 */ NULL,
	/* 0x73 115 */ NULL,
	/* 0x74 116 */ Opcode_Sys0_SetFlagUnknown10,
	/* 0x75 117 */ NULL,
	/* 0x76 118 */ NULL,
	/* 0x77 119 */ NULL,
	/* 0x78 120 */ Opcode_Sys0_Unknown_120,
	/* 0x79 121 */ Opcode_Sys0_Unknown_121,
	/* 0x7A 122 */ Opcode_Sys0_Unknown_122,
	/* 0x7B 123 */ Opcode_Sys0_Unknown_123,
	/* 0x7C 124 */ NULL,
	/* 0x7D 125 */ NULL,
	/* 0x7E 126 */ NULL,
	/* 0x7F 127 */ NULL,
	/* 0x80 128 */ Opcode_Sys0_LoadGlobalDatabase,
	/* 0x81 129 */ Opcode_Sys0_SaveGlobalDatabase,
	/* 0x82 130 */ Opcode_Sys0_WritePersistent,
	/* 0x83 131 */ Opcode_Sys0_ReadPersistent,
	/* 0x84 132 */ Opcode_Sys0_AddGlobalString,
	/* 0x85 133 */ Opcode_Sys0_HasGlobalString,
	/* 0x86 134 */ NULL,
	/* 0x87 135 */ NULL,
	/* 0x88 136 */ Opcode_Sys0_DefineFlags,
	/* 0x89 137 */ Opcode_Sys0_SetFlag,
	/* 0x8A 138 */ Opcode_Sys0_SetFlagRange,
	/* 0x8B 139 */ Opcode_Sys0_GetFlag,
	/* 0x8C 140 */ NULL,
	/* 0x8D 141 */ NULL,
	/* 0x8E 142 */ NULL,
	/* 0x8F 143 */ NULL,
	/* 0x90 144 */ Opcode_Sys0_Unknown_144,
	/* 0x91 145 */ Opcode_Sys0_Unknown_145,
	/* 0x92 146 */ NULL,
	/* 0x93 147 */ NULL,
	/* 0x94 148 */ Opcode_Sys0_Unknown_148,
	/* 0x95 149 */ Opcode_Sys0_Unknown_149,
	/* 0x96 150 */ Opcode_Sys0_Unknown_150,
	/* 0x97 151 */ Opcode_Sys0_Unknown_151,
	/* 0x98 152 */ Opcode_Sys0_CreateRecordList,
	/* 0x99 153 */ Opcode_Sys0_DestroyRecordList,
	/* 0x9A 154 */ Opcode_Sys0_RecordListCount,
	/* 0x9B 155 */ NULL,
	/* 0x9C 156 */ Opcode_Sys0_AddToRecordList,
	/* 0x9D 157 */ Opcode_Sys0_ReadRecordList,
	/* 0x9E 158 */ Opcode_Sys0_DropFromRecordList,
	/* 0x9F 159 */ NULL,
	/* 0xA0 160 */ Opcode_Sys0_PopGlobalList,
	/* 0xA1 161 */ Opcode_Sys0_PushGlobalList,
	/* 0xA2 162 */ NULL,
	/* 0xA3 163 */ NULL,
	/* 0xA4 164 */ NULL,
	/* 0xA5 165 */ NULL,
	/* 0xA6 166 */ NULL,
	/* 0xA7 167 */ NULL,
	/* 0xA8 168 */ Opcode_Sys0_Unknown_168,
	/* 0xA9 169 */ Opcode_Sys0_Unknown_169,
	/* 0xAA 170 */ NULL,
	/* 0xAB 171 */ NULL,
	/* 0xAC 172 */ Opcode_Sys0_Unknown_172,
	/* 0xAD 173 */ NULL,
	/* 0xAE 174 */ NULL,
	/* 0xAF 175 */ NULL,
	/* 0xB0 176 */ Opcode_Sys0_Unknown_176,
	/* 0xB1 177 */ Opcode_Sys0_Unknown_177,
	/* 0xB2 178 */ NULL,
	/* 0xB3 179 */ NULL,
	/* 0xB4 180 */ Opcode_Sys0_Unknown_180,
	/* 0xB5 181 */ Opcode_Sys0_Unknown_181,
	/* 0xB6 182 */ Opcode_Sys0_Unknown_182,
	/* 0xB7 183 */ NULL,
	/* 0xB8 184 */ NULL,
	/* 0xB9 185 */ NULL,
	/* 0xBA 186 */ NULL,
	/* 0xBB 187 */ NULL,
	/* 0xBC 188 */ NULL,
	/* 0xBD 189 */ NULL,
	/* 0xBE 190 */ NULL,
	/* 0xBF 191 */ NULL,
	/* 0xC0 192 */ Opcode_Sys0_Unknown_192,
	/* 0xC1 193 */ Opcode_Sys0_Unknown_193,
	/* 0xC2 194 */ NULL,
	/* 0xC3 195 */ NULL,
	/* 0xC4 196 */ Opcode_Sys0_Unknown_196,
	/* 0xC5 197 */ Opcode_Sys0_Unknown_197,
	/* 0xC6 198 */ NULL,
	/* 0xC7 199 */ NULL,
	/* 0xC8 200 */ NULL,
	/* 0xC9 201 */ NULL,
	/* 0xCA 202 */ NULL,
	/* 0xCB 203 */ NULL,
	/* 0xCC 204 */ NULL,
	/* 0xCD 205 */ NULL,
	/* 0xCE 206 */ NULL,
	/* 0xCF 207 */ NULL,
	/* 0xD0 208 */ Opcode_Sys0_CreateRecordTable,
	/* 0xD1 209 */ Opcode_Sys0_DestroyRecordTable,
	/* 0xD2 210 */ Opcode_Sys0_SetRecord,
	/* 0xD3 211 */ Opcode_Sys0_DeleteRecord,
	/* 0xD4 212 */ Opcode_Sys0_ReadRecord,
	/* 0xD5 213 */ NULL,
	/* 0xD6 214 */ NULL,
	/* 0xD7 215 */ NULL,
	/* 0xD8 216 */ Opcode_Sys0_ClearStringTables,
	/* 0xD9 217 */ Opcode_Sys0_StringTableCount,
	/* 0xDA 218 */ Opcode_Sys0_LoadStringTable,
	/* 0xDB 219 */ Opcode_Sys0_SerializeStringTable,
	/* 0xDC 220 */ Opcode_Sys0_AddString,
	/* 0xDD 221 */ Opcode_Sys0_ReadString,
	/* 0xDE 222 */ Opcode_Sys0_StringLength,
	/* 0xDF 223 */ NULL,
	/* 0xE0 224 */ Opcode_Sys0_Unknown_224,
	/* 0xE1 225 */ Opcode_Sys0_Unknown_225,
	/* 0xE2 226 */ Opcode_Sys0_Unknown_226,
	/* 0xE3 227 */ Opcode_Sys0_Unknown_227,
	/* 0xE4 228 */ NULL,
	/* 0xE5 229 */ NULL,
	/* 0xE6 230 */ NULL,
	/* 0xE7 231 */ NULL,
	/* 0xE8 232 */ Opcode_Sys0_GetGameId,
	/* 0xE9 233 */ NULL,
	/* 0xEA 234 */ NULL,
	/* 0xEB 235 */ NULL,
	/* 0xEC 236 */ Opcode_Sys0_Unknown_236,
	/* 0xED 237 */ Opcode_Sys0_Unknown_237,
	/* 0xEE 238 */ Opcode_Sys0_Unknown_238,
	/* 0xEF 239 */ Opcode_Sys0_Unknown_239,
	/* 0xF0 240 */ Opcode_Sys0_Unknown_240,
	/* 0xF1 241 */ Opcode_Sys0_Unknown_241,
	/* 0xF2 242 */ Opcode_Sys0_Unknown_242,
	/* 0xF3 243 */ Opcode_Sys0_Unknown_243,
	/* 0xF4 244 */ Opcode_Sys0_Unknown_244,
	/* 0xF5 245 */ Opcode_Sys0_Unknown_245,
	/* 0xF6 246 */ Opcode_Sys0_Unknown_246,
	/* 0xF7 247 */ Opcode_Sys0_Unknown_247,
	/* 0xF8 248 */ Opcode_Sys0_Unknown_248,
	/* 0xF9 249 */ Opcode_Sys0_Unknown_249,
	/* 0xFA 250 */ Opcode_Sys0_Unknown_250,
	/* 0xFB 251 */ Opcode_Sys0_Unknown_251,
	/* 0xFC 252 */ Opcode_Sys0_Unknown_252,
	/* 0xFD 253 */ Opcode_Sys0_IsLauncher,
	/* 0xFE 254 */ Opcode_Sys0_Unknown_254,
	/* 0xFF 255 */ NULL
};

// The C runtime's generator the original links (srand 0x004AB739, rand 0x004AB74B):
// seed = seed * 0x343FD + 0x269EC3, and rand is bits 16 to 30 of the new seed. The
// scripts all run on the one Windows thread, so there is one seed; it starts at 1.
static uint32_t gRandSeed = 1;

static uint32_t Sys0_CrtRand(void)
{
	gRandSeed = gRandSeed * 0x343FDu + 0x269EC3u;
	return (gRandSeed >> 16) & 0x7FFF;
}

// Sys0 0x00 (0x00487EA0): the seed.
uint32_t Opcode_Sys0_Srand(Thread_t* thread)
{
	gRandSeed = Thread_PopStack(thread);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_1(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * Sys0 0x02 (0x00487EE0): pops n and pushes a number from 0 to n - 1, built from
 * three draws as ((r1 << 8 ^ r2) << 8 ^ r3) and taken modulo n with a signed
 * division; for n of 0 or below it pushes 0 and draws nothing.
 */
uint32_t Opcode_Sys0_Random(Thread_t* thread)
{
	int32_t n = (int32_t)Thread_PopStack(thread);
	int32_t result = 0;
	if(n > 0)
	{
		int32_t a = (int32_t)(Sys0_CrtRand() << 8);
		int32_t b = (int32_t)((Sys0_CrtRand() ^ (uint32_t)a) << 8);
		int32_t c = (int32_t)(Sys0_CrtRand() ^ (uint32_t)b);
		result = c % n;
	}
	Thread_PushStack(thread, (uint32_t)result);
	return 0;
}


// Sys0 0x04 (0x00487F30) pushes the tick count from 0x004988B0, the clock
// every deadline in the engine is measured against.
uint32_t Opcode_Sys0_GetSysTime(Thread_t* thread)
{
	Thread_PushStack(thread, OS_GetTicks());
	return 0;
}

uint32_t Opcode_Sys0_Unknown_8(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_10(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_11(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

// Sys0 0x0C (fureraba.exe 0x00488080) pops one script address, resolves it with the
// engine's own address resolver at 0x0048DF60 and hands the host pointer straight to
// GetLocalTime. So the script buffer receives a SYSTEMTIME: eight 16-bit fields in
// the order year, month, day of week, day, hour, minute, second, millisecond. The
// original writes local time, not UTC.
uint32_t Opcode_Sys0_GetLocalTime(Thread_t* thread)
{
	uint16_t* out = (uint16_t*)Thread_PopAndResolveAddress(thread);
	if(out == NULL)
		return 1;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm local;
	localtime_r(&now.tv_sec, &local);

	out[0] = (uint16_t)(local.tm_year + 1900);
	out[1] = (uint16_t)(local.tm_mon + 1);
	out[2] = (uint16_t)local.tm_wday;
	out[3] = (uint16_t)local.tm_mday;
	out[4] = (uint16_t)local.tm_hour;
	out[5] = (uint16_t)local.tm_min;
	out[6] = (uint16_t)local.tm_sec;
	out[7] = (uint16_t)(now.tv_nsec / 1000000);

	return 0;
}

// Sys0 0x0D (fureraba.exe 0x004880A0) fills a MEMORYSTATUS with GlobalMemoryStatus
// and pushes two of its fields: dwTotalPhys first, then dwAvailPhys, so the script
// pops the available figure first. Both are clamped to 0x7FFFFFFF when they do not
// fit in a signed 32-bit value, which is how the original copes with machines that
// have 2 GB or more.
uint32_t Opcode_Sys0_GetPhysicalMemory(Thread_t* thread)
{
	uint64_t total = 0;
	uint64_t available = 0;
	OS_GetPhysicalMemory(&total, &available);

	Thread_PushStack(thread, total >= 0x80000000ULL ? 0x7FFFFFFF : (uint32_t)total);
	Thread_PushStack(thread, available >= 0x80000000ULL ? 0x7FFFFFFF : (uint32_t)available);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_14(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_IsWindowActive(Thread_t* thread)
{
	// Should read from a global var; Fake it for now
	Thread_PushStack(thread, 1);
	return 0;
}

uint32_t Opcode_Sys0_SetInputEnabled(Thread_t* thread)
{
	// 0x00488170: whether input reaches the scripts at all (0x0046DAF0), and every
	// key's presses forgotten (0x0046DBA0).
	Input_SetEnabled(Thread_PopStack(thread));
	return 0;
}

/*
 * Sys0 0x11 (0x00488190) pops a Windows virtual-key code and pushes whether it
 * is held down: the original takes bit 15 of GetAsyncKeyState, the "down right
 * now" bit, and drops bit 0, the "pressed since last asked" bit.
 */
uint32_t Opcode_Sys0_IsKeyDown(Thread_t* thread)
{
	// 0x00488190 -> 0x0046D6E0: bit 15 of the key's state, 1 while it is held.
	uint32_t vk = Thread_PopStack(thread);
	Thread_PushStack(thread, OS_IsKeyDown(vk) ? 1 : 0);
	return 0;
}

uint32_t Opcode_Sys0_CountKeyPresses(Thread_t* thread)
{
	// 0x004881C0: every press there has been of the keys in a zero-terminated
	// list (0x0046DDF0 each), added up.
	const uint32_t* keys = (const uint32_t*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, keys ? Input_ListTotals(keys) : 0);
	return 0;
}

uint32_t Opcode_Sys0_InputActivity(Thread_t* thread)
{
	// 0x00488200 -> 0x0046E740: a counter every key, button and wheel message
	// the window receives steps (0x0046E730).
	Thread_PushStack(thread, Input_Activity());
	return 0;
}

uint32_t Opcode_Sys0_SetSkipAllowed(Thread_t* thread)
{
	// 0x00488220 -> 0x0046DB00: whether the skip keys may skip (0x00506A48).
	Input_SetSkipAllowed(Thread_PopStack(thread));
	return 0;
}

uint32_t Opcode_Sys0_SetSkipLatch(Thread_t* thread)
{
	// 0x00488240 -> 0x0046DB10: skip as if the keys were held (0x00566828).
	Input_SetSkipLatch(Thread_PopStack(thread));
	return 0;
}

uint32_t Opcode_Sys0_SkipToggle(Thread_t* thread)
{
	// 0x00488260 -> 0x0046DB20: skip keys toggle from now until they are let go.
	Input_SkipToggle();
	return 0;
}

uint32_t Opcode_Sys0_IsSkipping(Thread_t* thread)
{
	// 0x00488270 -> 0x0046DFB0: 1 while skipping.
	Thread_PushStack(thread, Input_SkipQuery());
	return 0;
}

uint32_t Opcode_Sys0_AddRegion(Thread_t* thread)
{
	// 0x00488290: the region goes into the pointer list with the whole plane
	// (0x00506A4C) and into the keyboard list, and 0x0046E080 is run for it,
	// which takes whatever the new region can now see.
	uint32_t number = Thread_PopStack(thread);
	uint32_t key = REGION_KEY(number);
	Region_Add(0, key, (int32_t)0x80000000, (int32_t)0x80000000, (int32_t)0x7FFFFFFF, (int32_t)0x7FFFFFFF, 0);
	Region_Add(1, key, 0, 0, 0, 0, 0);
	Input_RegionState(key);
	return 0;
}

uint32_t Opcode_Sys0_RemoveRegion(Thread_t* thread)
{
	// 0x004882D0: 0x0046E080 for the region first, then out of both lists.
	uint32_t number = Thread_PopStack(thread);
	uint32_t key = REGION_KEY(number);
	Input_RegionState(key);
	Region_RemoveByKey(0, key);
	Region_RemoveByKey(1, key);
	return 0;
}

uint32_t Opcode_Sys0_TakeRegionInput(Thread_t* thread)
{
	// 0x00488310 -> 0x0046E080: the logical buttons pressed since the last take,
	// when the region has the keyboard (bit 31 while skipping), and the mouse
	// bits when it has the pointer.
	uint32_t number = Thread_PopStack(thread);
	Thread_PushStack(thread, Input_RegionState(REGION_KEY(number)));
	return 0;
}


uint32_t Opcode_Sys0_SetButtonKeys(Thread_t* thread)
{
	// 0x00488340 -> 0x0046E120: the keys (zero-terminated, fewer than 16), then
	// the logical button they become. Only ten buttons can be replaced; any other
	// is fatal (0x004EB81C), as is a list of 16 or more (0x004EB850).
	const uint32_t* keys = (const uint32_t*)Thread_PopAndResolveAddress(thread);
	uint32_t bit = Thread_PopStack(thread);
	uint32_t r = keys ? Input_SetButtonKeys(keys, bit) : 0x80000001u;
	if(r == 0x80000001u)
	{
		printf("[Thread %d]: %sError: 0x%X is not a button whose keys can be set\n", thread->threadId, TLevel[thread->level], bit);
		return 0xFFFFFFFC;
	}
	if(r == 0x80000002u)
	{
		printf("[Thread %d]: %sError: a button takes at most 15 keys\n", thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFC;
	}
	return 0;
}

uint32_t Opcode_Sys0_CountButtonPresses(Thread_t* thread)
{
	// 0x004883E0 -> 0x0046E1F0: every press there has been of every key of every
	// logical button (and mouse bit) in the mask, added up.
	uint32_t mask = Thread_PopStack(thread);
	Thread_PushStack(thread, Input_ButtonTotals(mask));
	return 0;
}

uint32_t Opcode_Sys0_TakeRegionKey(Thread_t* thread)
{
	// 0x00488410: a virtual key, then the region. The presses of that key since
	// the last take (0x0046DCC0) when the region has the pointer (for the left and
	// right buttons) or the keyboard (for anything else); 0 otherwise.
	uint32_t vk = Thread_PopStack(thread);
	uint32_t number = Thread_PopStack(thread);
	Thread_PushStack(thread, Input_RegionTake(vk, REGION_KEY(number)));
	return 0;
}

uint32_t Opcode_Sys0_SetSwapButtons(Thread_t* thread)
{
	// 0x00488470 -> 0x0048EFE0: exchange the left and right buttons (0 or 1);
	// pushes 1 when the value was taken.
	uint32_t v = Thread_PopStack(thread);
	Thread_PushStack(thread, Input_SetSwapButtons(v));
	return 0;
}

uint32_t Opcode_Sys0_Unknown_31(Thread_t* thread)
{
	uint32_t value1 = Thread_PopStack(thread);
	uint32_t value2 = Thread_PopStack(thread);
	uint32_t value3 = Thread_PopStack(thread);
	uint32_t value4 = Thread_PopStack(thread);
	uint32_t value5 = Thread_PopStack(thread);
	uint32_t value6 = Thread_PopStack(thread);
	return 0;
}

uint32_t Opcode_Sys0_AllocAuxMem(Thread_t* thread)
{
	uint32_t size = Thread_PopStack(thread);
	uint32_t res = Engine_AllocAuxMemory(thread->engine, size);
	Thread_PushStack(thread, res);
	return 0;
}

uint32_t Opcode_Sys0_FreeAuxMem(Thread_t* thread)
{
	uint32_t address = Thread_PopStack(thread);
	uint32_t result = Engine_FreeAuxMemory(thread->engine, address);
	if(result == 0)
	{
		printf("[Thread %d]: %sError: 0x%.8X is not the start of an allocated memory area\n", thread->threadId, TLevel[thread->level], address);
		return 0xFFFFFFFF;
	}
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_36(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * The matching FindFirstFileA does: '*' for any run and '?' for any one character,
 * without regard to case.
 */
static int Sys0_WildcardMatch(const char* pattern, const char* name)
{
	const char* star = NULL;
	const char* retry = name;
	while(*name != '\0')
	{
		if(*pattern == '?' ||
		   (*pattern != '\0' && *pattern != '*' &&
		    tolower((unsigned char)*pattern) == tolower((unsigned char)*name)))
		{
			pattern++;
			name++;
		}
		else if(*pattern == '*')
		{
			star = pattern++;
			retry = name;
		}
		else if(star != NULL)
		{
			pattern = star + 1;
			name = ++retry;
		}
		else
		{
			return 0;
		}
	}
	while(*pattern == '*')
		pattern++;
	return *pattern == '\0';
}

/*
 * Sys0 0x25 (0x00488610 -> 0x00466D30) lists the files matching a pattern. The script
 * pushes the buffer, its size, the pattern, a recurse flag and the maximum number of
 * names, so they pop back to front. Directories are skipped, each name is copied bare
 * and NUL-terminated one after another, and a name that does not fit ends the walk
 * with a count of zero and no byte total written. With no buffer the opcode pushes
 * the number of bytes the names would have taken; with one it pushes how many there
 * were.
 */
uint32_t Opcode_Sys0_ListFiles(Thread_t* thread)
{
	int maximum = (int)Thread_PopStack(thread);
	int recurse = (int)Thread_PopStack(thread);
	const char* pattern = (const char*)Thread_PopAndResolveAddress(thread);
	int size = (int)Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);

	if(pattern == NULL)
		return 0xFFFFFFFF;
	if(recurse != 0)
	{
		printf("[Thread %d]: %sError: the recursive file listing (0x00466E7B) is not written yet\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}

	// The pattern is a Windows path; split it into a directory and a mask.
	char path[512];
	if(snprintf(path, sizeof(path), "%s", pattern) >= (int)sizeof(path))
		return 0xFFFFFFFF;
	for(char* c = path; *c != '\0'; c++)
	{
		if(*c == '\\')
			*c = '/';
	}
	char* mask = strrchr(path, '/');
	const char* directory = ".";
	if(mask != NULL)
	{
		*mask++ = '\0';
		directory = path[0] == '\0' ? "/" : path;
	}
	else
	{
		mask = path;
	}

	printf("[Thread %d]: %sListing \"%s\" in \"%s\"\n",
	       thread->threadId, TLevel[thread->level], mask, directory);

	int count = 0;
	int written = 0;
	int overflowed = 0;
	DIR* dir = opendir(directory);
	if(dir != NULL)
	{
		struct dirent* entry;
		while((entry = readdir(dir)) != NULL)
		{
			if(maximum != 0 && count >= maximum)
				break;
			if(!Sys0_WildcardMatch(mask, entry->d_name))
				continue;

			char full[1024];
			if(snprintf(full, sizeof(full), "%s/%s", directory, entry->d_name) >= (int)sizeof(full))
				continue;
			struct stat info;
			if(stat(full, &info) != 0 || S_ISDIR(info.st_mode))
				continue;

			int length = (int)strlen(entry->d_name) + 1;
			if(out != NULL)
			{
				if(length > size - written)
				{
					overflowed = 1;
					break;
				}
				memcpy(out + written, entry->d_name, (size_t)length);
			}
			written += length;
			count++;
		}
		closedir(dir);
	}

	if(overflowed)
	{
		printf("[Thread %d]: %sThe listing buffer (%d bytes) was too small\n",
		       thread->threadId, TLevel[thread->level], size);
		Thread_PushStack(thread, 0);
		return 0;
	}

	printf("[Thread %d]: %sListed %d file%s (%d bytes)\n",
	       thread->threadId, TLevel[thread->level], count, count == 1 ? "" : "s", written);
	Thread_PushStack(thread, (uint32_t)(out != NULL ? count : written));
	return 0;
}


/*
 * Sys0 0x28 (0x00488710): CreateDirectoryA on the path the script gives; pushes its
 * answer (non-zero when the directory was made).
 */
uint32_t Opcode_Sys0_CreateDirectory(Thread_t* thread)
{
	const char* path = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t result = 0;
	if(path != NULL)
	{
		char local[512];
		snprintf(local, sizeof(local), "%s", path);
		for(char* c = local; *c != 0; c++)
			if(*c == '\\')
				*c = '/';
		result = mkdir(local, 0777) == 0 ? 1 : 0;
	}
	Thread_PushStack(thread, result);
	return 0;
}

/*
 * Sys0 0x29 (0x00488740): RemoveDirectoryA; pushes its answer. Only an empty
 * directory can be removed, as on Windows.
 */
uint32_t Opcode_Sys0_RemoveDirectory(Thread_t* thread)
{
	const char* path = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t result = 0;
	if(path != NULL)
	{
		char local[512];
		snprintf(local, sizeof(local), "%s", path);
		for(char* c = local; *c != 0; c++)
			if(*c == '\\')
				*c = '/';
		result = rmdir(local) == 0 ? 1 : 0;
	}
	Thread_PushStack(thread, result);
	return 0;
}


/*
 * Sys0 0x2A (0x00488770): GetFileAttributesA; pushes 1 when the path exists and has
 * the directory attribute (bit 4), else 0.
 */
uint32_t Opcode_Sys0_IsDirectory(Thread_t* thread)
{
	const char* path = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t result = 0;
	if(path != NULL)
	{
		char local[512];
		snprintf(local, sizeof(local), "%s", path);
		for(char* c = local; *c != 0; c++)
			if(*c == '\\')
				*c = '/';
		struct stat info;
		result = stat(local, &info) == 0 && S_ISDIR(info.st_mode) ? 1 : 0;
	}
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_44(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_45(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_47(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_ReadFile(Thread_t* thread)
{
	uint8_t* filename = Thread_PopAndResolveAddress(thread);
	uint8_t* archive = Thread_PopAndResolveAddress(thread);
	uint8_t* buffer = Thread_PopAndResolveAddress(thread);
	uint32_t size = Engine_ReadFileToMemory(thread->engine, archive, filename, buffer);
	Thread_PushStack(thread, size);
	return 0;
}

/*
 * Sys0 0x31 (0x00488910 -> 0x00465DB0): read a file, or part of it, into script
 * memory. Popped: a length and an offset (both 0 for the whole file), then the name,
 * an archive name (0 for none) and the destination. The name is looked for loose
 * first (0x00465A50: as given when it is absolute, else in the game's folder, then
 * each registered search path); when that fails the archive is tried (0x00465840),
 * or with no archive the per-user folder 0x00517C08. The contents are then prepared
 * by 0x004654A0: a DSC file is unpacked (0x00464030); a CompressedBG or other image
 * would be decoded to pixels (0x004A0EF0, 0x00402030 - refused here by name); and
 * the range [offset, offset + length) copied to the destination. Pushed: 0 done,
 * 1 not found, 2 the range runs past the end, 3 a length of 0 or past the size,
 * 5 a read or decode failed, 6 larger than 64 MB.
 */
uint32_t Opcode_Sys0_LoadFile(Thread_t* thread)
{
	uint32_t length = Thread_PopStack(thread);
	uint32_t offset = Thread_PopStack(thread);
	uint32_t nameAddress = Thread_PopStack(thread);
	uint32_t archiveAddress = Thread_PopStack(thread);
	uint8_t* destination = Thread_PopAndResolveAddress(thread);
	const char* name = nameAddress ? (const char*)Thread_ResolveAddr(thread, nameAddress) : NULL;
	const char* archive = archiveAddress ? (const char*)Thread_ResolveAddr(thread, archiveAddress) : NULL;
	uint32_t result = 1;
	size_t size = 0;
	uint8_t* data = name != NULL ? Engine_ReadLooseFile(name, &size) : NULL;
	if(data == NULL && name != NULL && archive != NULL)
		data = Arc_ReadFile(archive, name, &size);
	if(data != NULL)
	{
		if(size > 0x4000000)
			result = 6;
		else if(size >= 16 && memcmp(data, "CompressedBG___", 15) == 0)
		{
			printf("[Thread %d]: %sError: reading a CompressedBG image into memory (0x004A0EF0) is not written yet\n",
			       thread->threadId, TLevel[thread->level]);
			result = 5;
		}
		else
		{
			data = Arc_Inflate(data, size, &size);
			if(data == NULL)
				result = 5;
			else
			{
				// 0x004655FC: both 0 means the whole file.
				if(offset == 0 && length == 0)
					length = (uint32_t)size;
				if(length == 0 || length > size)
					result = 3;
				else if((uint64_t)offset + length > size)
					result = 2;
				else
				{
					if(destination != NULL)
						memcpy(destination, data + offset, length);
					result = 0;
				}
			}
		}
		free(data);
	}
	Thread_PushStack(thread, result);
	return 0;
}

/*
 * Sys0 0x32 (0x00488970 -> 0x00465FB0): write script memory to a file. Popped: the
 * size, the data and the name. The name is used as it is when absolute (0x00464B00:
 * a leading '\' or a drive letter), else joined to the game's folder ("%s%s",
 * 0x00464B70); the file is created or truncated (CREATE_ALWAYS) and written in one
 * go. Pushes 1 when every byte was written, else 0. The engine runs inside the game's
 * folder, so a relative name is written there, beside the game, as the original
 * writes it.
 */
uint32_t Opcode_Sys0_SaveFile(Thread_t* thread)
{
	uint32_t size = Thread_PopStack(thread);
	const uint8_t* data = Thread_PopAndResolveAddress(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t result = 0;
	if(name != NULL && data != NULL)
	{
		char path[1024];
		Engine_ResolveWritePath(name, path, sizeof(path));
		FILE* f = fopen(path, "wb");
		if(f != NULL)
		{
			size_t written = size ? fwrite(data, 1, size, f) : 0;
			fclose(f);
			result = written == size ? 1 : 0;
			printf("[Thread %d]: %sWrote \"%s\" (%u bytes)\n", thread->threadId, TLevel[thread->level], path, size);
		}
		else
			printf("[Thread %d]: %sCould not write \"%s\"\n", thread->threadId, TLevel[thread->level], path);
	}
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_DeleteFile(Thread_t* thread)
{
	uint8_t* ptr1 = Thread_PopAndResolveAddress(thread);
	uint8_t* ptr2 = Thread_PopAndResolveAddress(thread);
	printf("[Thread %d]: %sDeleting file (\"%s\", \"%s\")\n", thread->threadId, TLevel[thread->level], ptr1, ptr2);
	Thread_PushStack(thread, 0);
	return 0;
}

uint32_t Opcode_Sys0_FindFile(Thread_t* thread)
{
	const char* filename = (const char*)Thread_PopAndResolveAddress(thread);
	const char* archive = (const char*)Thread_PopAndResolveAddress(thread);
	printf("[Thread %d]: %sFinding file (\"%s\", \"%s\")\n", thread->threadId, TLevel[thread->level], filename, archive);
	int found = Engine_FileExists(archive, filename);
	printf("[Thread %d]: %s%s\n", thread->threadId, TLevel[thread->level], found ? "Found it" : "Not there");
	Thread_PushStack(thread, found);
	return 0;
}

// Sys0 0x35 (0x00488A80 -> 0x00466460) pushes back the size of a file, or 0 when it
// is not there. Both arguments are addresses in the script's own memory, popped and
// resolved (0x0048E0E0): the name first, then the archive, whose pushed value may be
// 0 to mean no archive at all. The original reads the file into a 0x4000000-byte
// buffer and frees it again without looking at the bytes; it tries the archive
// registry at 0x00517F18 and then the one at 0x00517C08, which is the same order this
// engine's own lookup takes.
uint32_t Opcode_Sys0_GetFileSize(Thread_t* thread)
{
	Engine_t* engine = thread->engine;
	const char* filename = (const char*)Thread_PopAndResolveAddress(thread);
	const char* archive  = (const char*)Thread_PopAndResolveAddress(thread);

	size_t size = 0;
	uint8_t* file = Engine_ReadFile(engine, archive, filename, &size);
	if(file == NULL)
	{
		Thread_PushStack(thread, 0);
		return 0;
	}

	free(file);
	Thread_PushStack(thread, (uint32_t)size);
	return 0;
}


uint32_t Opcode_Sys0_EnableSearchPaths(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Engine_SetEnableSearchPaths(value);
	return 0;
}

uint32_t Opcode_Sys0_AddSearchPath(Thread_t* thread)
{
	uint8_t* ptr = Thread_PopAndResolveAddress(thread);
	Engine_AddSearchPath(ptr);
	return 0;
}

uint32_t Opcode_Sys0_CreateComplexArchive(Thread_t* thread)
{
	// 0x00488B00: the array of member names first, then the group's own name.
	uint32_t listAddress = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t* list = (uint32_t*)Thread_ResolveAddr(thread, listAddress);
	if(list == NULL || name == NULL)
		return 0xFFFFFFFF;

	// The array ends at the first null entry, as 0x00488B1F counts it.
	int count = 0;
	while(list[count] != 0)
		count++;

	const char** members = (const char**)malloc(sizeof(char*) * (size_t)(count > 0 ? count : 1));
	if(members == NULL)
		return 0xFFFFFFFF;
	int silenced = thread->silenceBasicOpcodeLog;
	thread->silenceBasicOpcodeLog = 1;
	int inBasic = thread->inBasicOpcode;
	thread->inBasicOpcode = 1;
	for(int i = 0; i < count; i++)
		members[i] = (const char*)Thread_ResolveAddr(thread, list[i]);
	thread->silenceBasicOpcodeLog = silenced;
	thread->inBasicOpcode = inBasic;

	printf("[Thread %d]: %sComplex archive \"%s\" of %d archive%s\n",
	       thread->threadId, TLevel[thread->level], name, count, count == 1 ? "" : "s");
	for(int i = 0; i < count; i++)
	{
		printf("[Thread %d]: %s  %s\n", thread->threadId, TLevel[thread->level],
		       members[i] == NULL ? "(unresolved)" : members[i]);
	}

	uint32_t result = (uint32_t)Arc_CreateComplex(name, members, count);
	free(members);
	if(result == 0)
	{
		printf("[Thread %d]: %sAn archive named \"%s\" is already registered\n",
		       thread->threadId, TLevel[thread->level], name);
	}
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_58(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_59(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_60(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


/*
 * Sys0 0x3D (0x00488CA0 -> 0x00464B20): pops which directory and a buffer in script
 * memory, copies the directory's path there and pushes 1; 0 for a directory that is
 * not known. Directory 0 is the game's own (0x00517F18: the executable's folder with
 * a trailing separator, 0x00465180); 1 is the per-user folder at 0x00517C08, which
 * only 0x004650C0 fills and which is empty until then (then 0 is pushed). The engine
 * runs from inside the game's folder (main.c changes to it), so the game's folder is
 * "./" here, the same prefix Sys0 0x39 is given by the scripts.
 */
char gUserSaveDirectory[512] = { 0 };   // 0x00517C08

uint32_t Opcode_Sys0_GetDirectory(Thread_t* thread)
{
	uint32_t which = Thread_PopStack(thread);
	char* buffer = (char*)Thread_PopAndResolveAddress(thread);
	uint32_t result = 0;
	if(buffer != NULL)
	{
		if(which == 0)
		{
			strcpy(buffer, "./");
			result = 1;
		}
		else if(which == 1 && gUserSaveDirectory[0] != 0)
		{
			strcpy(buffer, gUserSaveDirectory);
			result = 1;
		}
	}
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_62(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_63(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_LoadProgram(Thread_t* thread)
{
	uint8_t* filename = Thread_PopAndResolveAddress(thread);
	uint8_t* archive = Thread_PopAndResolveAddress(thread);
	printf("[Thread %d]: %sAttempting to load program [%s : %s]\n", thread->threadId, TLevel[thread->level], archive, filename);

	size_t fileSize;
	uint8_t* code = Engine_ReadFile(gEngine, archive, filename, &fileSize);
	if(code == NULL)
		return 1;
	uint32_t location = Thread_LoadCode(thread, code, fileSize, filename);
	free(code);
	if(location == THREAD_LOAD_FAILED)
		return 1;

	Thread_PushStack(thread, location);

	return 0;
}

uint32_t Opcode_Sys0_DeleteProgram(Thread_t* thread)
{
	uint32_t res = Thread_DeleteProgram(thread);
	Thread_PushStack(thread, res);
	return 0;
}

uint32_t Opcode_Sys0_CreateThread(Thread_t* thread)
{
	uint32_t memorySize = Thread_PopStack(thread);
	uint32_t codeSize = Thread_PopStack(thread);
	uint32_t stackSize = Thread_PopStack(thread);
	uint8_t* programFilename = Thread_PopAndResolveAddress(thread);
	uint8_t* archiveFilename = Thread_PopAndResolveAddress(thread);
	printf("[Thread %d]: %sAttempting create thread for program [%s : %s]\n", thread->threadId, TLevel[thread->level], archiveFilename, programFilename);
	uint32_t res = Engine_LoadProgram(gEngine, archiveFilename, programFilename, stackSize, codeSize, memorySize);
	Thread_PushStack(thread, res);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_69(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_GetThreadID(Thread_t* thread)
{
	uint32_t threadId = Thread_GetThreadID(thread);
	Thread_PushStack(thread, threadId);
	return 0;
}

/*
 * Sys0 0x47 (0x00488F20) pops a thread handle, looks it up in the thread tree
 * from the root down (0x00444B40 to the root, 0x00444C70 the search) and pushes
 * whether it is there. A handle that is not there is not an error here: the
 * question the opcode asks is exactly that.
 */
uint32_t Opcode_Sys0_ThreadExists(Thread_t* thread)
{
	uint32_t handle = Thread_PopStack(thread);
	Thread_t* target = Engine_GetLiveThreadById(thread->engine, handle);
	Thread_PushStack(thread, target != NULL ? 1 : 0);
	return 0;
}

/*
 * Sys0 0x48 (0x00488F60) posts one value to another thread's queue: the handle
 * is pushed first and the value on top of it, and nothing is pushed back. Here
 * an unknown handle IS an error - the original reports
 * "an invalid thread handle was specified" (0x004EB9CC) through the fatal
 * reporter at 0x00464870, which ends in int3 and does not return - so the
 * engine stops the thread rather than dropping the message silently.
 */
uint32_t Opcode_Sys0_PostMessage(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	Thread_t* target = Engine_GetLiveThreadById(thread->engine, handle);
	if(target == NULL)
	{
		printf("[Thread %d]: %sError: an invalid thread handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}

	Thread_PostMessage(target, value);
	printf("[Thread %d]: %sPosted 1 message to thread %d\n",
	       thread->threadId, TLevel[thread->level], handle);
	return 0;
}

/*
 * Sys0 0x49 (0x00488FB0) takes one message for the running thread. It pushes
 * whether there was one, and writes to the address only when there was.
 */
uint32_t Opcode_Sys0_TakeMessage(Thread_t* thread)
{
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	uint32_t value = 0;
	int taken = Thread_TakeMessage(thread, &value);
	if(taken && out != NULL)
		Thread_WriteIntToMemory(thread, out, BGI_SIZE_DWORD, value);
	Thread_PushStack(thread, (uint32_t)taken);
	return 0;
}

/*
 * Sys0 0x4A (0x00488FE0) posts a run of values to another thread's queue. The
 * array of values pops first, then how many of them, then the thread's handle.
 * An unknown handle and a count below 1 are both fatal, as in the original.
 */
uint32_t Opcode_Sys0_PostMessages(Thread_t* thread)
{
	uint32_t* values = (uint32_t*)Thread_PopAndResolveAddress(thread);
	int count = (int)Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	Thread_t* target = Engine_GetLiveThreadById(thread->engine, handle);
	if(target == NULL)
	{
		printf("[Thread %d]: %sError: an invalid thread handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}
	if(count < 1)
	{
		printf("[Thread %d]: %sError: an invalid thread message count [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], count);
		return 0xFFFFFFFF;
	}
	if(values == NULL)
		return 0xFFFFFFFF;

	for(int i = 0; i < count; i++)
		Thread_PostMessage(target, values[i]);
	printf("[Thread %d]: %sPosted %d message%s to thread %d\n",
	       thread->threadId, TLevel[thread->level], count, count == 1 ? "" : "s", handle);
	return 0;
}

/*
 * Sys0 0x4B (0x00489090) takes a run of messages into an array and pushes how
 * many there were. The loop tests the previous take before taking again, so an
 * empty queue ends the run; nothing is written for the take that failed.
 */
uint32_t Opcode_Sys0_TakeMessages(Thread_t* thread)
{
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	int count = (int)Thread_PopStack(thread);
	if(count < 1)
	{
		printf("[Thread %d]: %sError: an invalid thread message count [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], count);
		return 0xFFFFFFFF;
	}

	int taken = 0;
	int more = 1;
	for(int i = 0; i < count && more; i++)
	{
		uint32_t value = 0;
		more = Thread_TakeMessage(thread, &value);
		if(more && out != NULL)
			Thread_WriteIntToMemory(thread, out + (size_t)i * 4, BGI_SIZE_DWORD, value);
		taken += more;
	}
	Thread_PushStack(thread, (uint32_t)taken);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_76(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * Sys0 0x50 (0x004891B0 -> 0x00431A90) pops one value into the global at 0x00507688.
 * 0x00431AA0 reads it for a PROCESS (its +0x10 is the process's aborted flag): while
 * it is 0 every wait and animation a thread is held by ends at its next pass. The
 * scripts clear it around work that must not wait and set it again after.
 */
uint32_t Opcode_Sys0_SetProcessesWait(Thread_t* thread)
{
	gProcessesWait = Thread_PopStack(thread);
	// 0x004891C0: the handler answers 1, the thread gives up the rest of its turn
	// (the reference trace shows r=1 at every call).
	return 1;
}

uint32_t Opcode_Sys0_Unknown_84(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_SetTimer(Thread_t* thread)
{
	uint32_t data = Thread_PopStack(thread);
	Thread_SetUnknownTimestamp(thread, data);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_89(Thread_t* thread)
{
	uint32_t value1 = Thread_PopStack(thread);
	if(thread->ticks == 19946 || thread->ticks == 1192)
		Thread_PushStack(thread, 1);
	else
		Thread_PushStack(thread, 0); // 0 @ 24926, 1 @ 24686, 24746, 24806, 24866
	printf("[Thread %d]: %sWarning: dummy opcode\n", thread->threadId, TLevel[thread->level]);
	return 2;
}

uint32_t Opcode_Sys0_Unknown_90(Thread_t* thread)
{
	Thread_PushStack(thread, 0);
	printf("[Thread %d]: %sWarning: dummy opcode\n", thread->threadId, TLevel[thread->level]);
	return 2;
}

/*
 * Sys0 0x5C (0x00489370) makes the thread wait. The key mask pops first, then
 * whether a key may cut the wait short, then the delay; the process is joined
 * to the thread (0x004452A0) and the handler returns 2, which is the result
 * that means the thread is waiting. Nothing is pushed here: the process pushes
 * its own result when it finishes, 1 if a key cut it short and 0 otherwise.
 */
uint32_t Opcode_Sys0_WaitTiming(Thread_t* thread)
{
	uint32_t keyMask = Thread_PopStack(thread);
	uint32_t allowKey = Thread_PopStack(thread);
	uint32_t delay = Thread_PopStack(thread);

	Process_t* process = Process_CreateWaitTiming(thread, delay, allowKey, keyMask);
	if(process == NULL)
		return 0xFFFFFFFF;
	Thread_SetProcess(thread, process);
	printf("[Thread %d]: %sWaiting %d ms\n", thread->threadId, TLevel[thread->level], delay);
	return 2;
}

uint32_t Opcode_Sys0_Unknown_93(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_SwitchToThread(Thread_t* thread)
{
	uint32_t threadId = Thread_PopStack(thread);
	thread->engine->nextThreadRequest = threadId;
	return 3;
}

uint32_t Opcode_Sys0_Yield(Thread_t* thread)
{
	return 1;
}


// Sys0 0x60 (0x00489460). The script pushes the size index, then the pixel mode,
// then a third value, so they come off the stack the other way round. Both of the
// original's range checks are fatal and name themselves; it pushes nothing back.
uint32_t Opcode_Sys0_SetDisplayMode(Thread_t* thread)
{
	uint32_t third = Thread_PopStack(thread);
	uint32_t pixelMode = Thread_PopStack(thread);
	uint32_t sizeIndex = Thread_PopStack(thread);

	if(pixelMode >= 2)
	{
		printf("[Thread %d]: %sError: an invalid pixel mode was set (%u)\n",
		       thread->threadId, TLevel[thread->level], pixelMode);
		return 0xFFFFFFFF;
	}
	if(sizeIndex >= 8)
	{
		printf("[Thread %d]: %sError: an invalid screen size was set (%u)\n",
		       thread->threadId, TLevel[thread->level], sizeIndex);
		return 0xFFFFFFFF;
	}

	Engine_SetDisplayMode(thread->engine, sizeIndex, pixelMode, third);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_97(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_SetKeySlots(Thread_t* thread)
{
	uint32_t* keys = (uint32_t*)Thread_PopAndResolveAddress(thread);
	uint32_t value = Thread_PopStack(thread);
	Engine_SetKeySlots(value, keys);
	return 0;
}

// The boolean form of Sys1 0x63: a true value selects mapping mode 0 and a false
// value selects mode 1. The original (0x00489520) discards the setter result and
// pushes nothing back.
uint32_t Opcode_Sys0_SetScreenMappingModeFlag(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Engine_SetScreenMappingMode(value != 0 ? 0 : 1);
	return 0;
}

uint32_t Opcode_Sys0_SetWindowVisible(Thread_t* thread)
{
	uint32_t visible = Thread_PopStack(thread);
	Engine_SetWindowVisible(visible);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_101(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_SetWindowTitle(Thread_t* thread)
{
	const char* title = (const char*)Thread_PopAndResolveAddress(thread);
	Engine_SetWindowTitle(title);
	return 0;
}

uint32_t Opcode_Sys0_SetCursorShape(Thread_t* thread)
{
	uint32_t shapeType = Thread_PopStack(thread);
	if(shapeType > 4)
	{
		printf("[Thread %d]: %sError: %d is not a valid cursor shape\n", thread->threadId, TLevel[thread->level], shapeType);
		return 0xFFFFFFFF;
	}
	gCursorShape = shapeType;
	return 0;
}


uint32_t Opcode_Sys0_SetIdleWaitTime(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Engine_SetIdleWaitTime(value);
	return 0;
}

uint32_t Opcode_Sys0_SetGlobalUnknownVal001(Thread_t* thread)
{
	uint32_t data = Thread_PopStack(thread);
	SetGlobalUnknownVal001(data);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_105(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

// Sys0 0x6A (fureraba.exe 0x00489650) is "mov eax, 6; ret": no operands, no work,
// only a control code for the scheduler at 0x0048CF00.
//
// That scheduler runs each thread in turn and switches on the code a handler
// returns: 0 and 1 move to the next thread, 2 stays on this one, 3 switches to the
// thread named by 0x00566894, 4 stays after a call with 0x80000000, and 5 and 6 move
// on like 1 but also raise a flag. Code 6 raises the one at [ebp-0x118], which makes
// every remaining thread be skipped at 0x0048CFD5, so nothing else runs in this pass;
// the pass then reports 1 to the frame loop at 0x0048CD5F, which notes that it ran
// and goes on to the frame's own work. Code 5 raises the other flag and the pass
// reports 2, which the frame loop records at [ebp-0x634] as well.
//
// So this is the yield that also ends the pass, against Sys0 0x5F (0x00489450, "mov
// eax, 1") which is the plain one. OpenBGI runs one thread's slice at a time and has
// no pass to end, so both stop the slice and return 1; the difference will only start
// to matter once more than one thread is scheduled.
uint32_t Opcode_Sys0_YieldAndEndPass(Thread_t* thread)
{
	return 1;
}

uint32_t Opcode_Sys0_Unknown_107(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_108(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_109(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_111(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_InitGlobalMem(Thread_t* thread)
{
	uint32_t level = Thread_PopStack(thread);
	uint32_t res = Engine_InitGlobalMemory(thread->engine, level);
	Thread_PushStack(thread, res);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_113(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_SetFlagUnknown10(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Engine_SetFlagUnknown10(value);
	return 0;
}

// 0x00487F80: pop one value and hand it to 0x00401600, which stores it at
// 0x00565AC4 and, only when it is not zero, derives a 64-bit period from it
// (value * 96 / 100 through the runtime's own 64-bit divide) into 0x00565AC8 and
// clears the five words after it and the pair at 0x0050A858. Those are the state
// of the clock the four-arm query at 0x00401670 reads back; nothing in this
// engine has that clock yet, so a non-zero base is refused by name rather than
// stored as a number nothing would honour.
//
// scrdrv's boot calls it with 0 as the very last thing it does before it returns,
// which is the original's own way of leaving the clock stopped.
uint32_t Opcode_Sys0_SetClockBase(Thread_t* thread)
{
	uint32_t base = Thread_PopStack(thread);
	Engine_SetClockBase(base);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_120(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_121(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_122(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_123(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_LoadGlobalDatabase(Thread_t* thread)
{
	// 0x00489920 -> 0x0046B800: read BGI.gdb, then push the window's left and
	// top it keeps and the status: 0 read, 1 no file (0x80000001), 2 not a
	// database this engine reads (0x80000002). The position is pushed as it was
	// saved; the original first checks it against the monitors (0x0046FAB0) and
	// centres the window otherwise (0x00461760), which a display that is the
	// whole screen has no use for.
	int32_t left = 0, top = 0;
	uint32_t status = GDB_Load(thread->engine->globalMem, thread->engine->globalBufferSize, &left, &top);
	uint32_t pushed = status == 0 ? 0 : status == 0x80000001u ? 1 : status == 0x80000002u ? 2 : status;
	Thread_PushStack(thread, (uint32_t)left);
	Thread_PushStack(thread, (uint32_t)top);
	Thread_PushStack(thread, pushed);
	return 0;
}

uint32_t Opcode_Sys0_SaveGlobalDatabase(Thread_t* thread)
{
	// 0x00489990 -> 0x0046B640: write BGI.gdb and push 1 when it was written.
	Thread_PushStack(thread, GDB_Save(thread->engine->globalMem, thread->engine->globalBufferSize));
	return 0;
}

uint32_t Opcode_Sys0_WritePersistent(Thread_t* thread)
{
	// 0x004899B0: the size, the source, then the offset into the persistent
	// block. An offset past the block, an empty copy or one that runs off its
	// end are fatal in the original (0x004EBB6C / 0x004EBB98).
	uint32_t size = Thread_PopStack(thread);
	uint8_t* source = Thread_PopAndResolveAddress(thread);
	uint32_t offset = Thread_PopStack(thread);
	if(offset >= PERSISTENT_SIZE || size == 0 || size > PERSISTENT_SIZE || offset + size > PERSISTENT_SIZE || source == NULL)
	{
		printf("[Thread %d]: %sError: persistent block write of 0x%X bytes at 0x%X is out of its 1 MB\n", thread->threadId, TLevel[thread->level], size, offset);
		return 0xFFFFFFFC;
	}
	memcpy(Persistent_Block() + offset, source, size);
	return 0;
}

uint32_t Opcode_Sys0_ReadPersistent(Thread_t* thread)
{
	// 0x00489A70: the size, the offset into the persistent block, then where to
	// copy it; the same bounds as Sys0 0x82.
	uint32_t size = Thread_PopStack(thread);
	uint32_t offset = Thread_PopStack(thread);
	uint8_t* destination = Thread_PopAndResolveAddress(thread);
	if(offset >= PERSISTENT_SIZE || size == 0 || size > PERSISTENT_SIZE || offset + size > PERSISTENT_SIZE || destination == NULL)
	{
		printf("[Thread %d]: %sError: persistent block read of 0x%X bytes at 0x%X is out of its 1 MB\n", thread->threadId, TLevel[thread->level], size, offset);
		return 0xFFFFFFFC;
	}
	memcpy(destination, Persistent_Block() + offset, size);
	return 0;
}

uint32_t Opcode_Sys0_AddGlobalString(Thread_t* thread)
{
	// 0x00489B30 -> 0x0046B5B0: add a string to table 0x80000000 and push 1.
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);
	if(text)
		StrTab_Add(STRTAB_GLOBAL_ID, text);
	Thread_PushStack(thread, 1);
	return 0;
}

uint32_t Opcode_Sys0_HasGlobalString(Thread_t* thread)
{
	// 0x00489B60 -> 0x0046B5D0: 1 when table 0x80000000 holds the string.
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, text && StrTab_GlobalHas(text) == 0 ? 1 : 0);
	return 0;
}

uint32_t Opcode_Sys0_DefineFlags(Thread_t* thread)
{
	// 0x00489B90 -> 0x0046B5E0: the bit count, then the name; defines the named
	// bit array or resizes it keeping what fits, and pushes 1 when it could.
	uint32_t bits = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, name && Flags_Define(name, bits) == 0 ? 1 : 0);
	return 0;
}

uint32_t Opcode_Sys0_SetFlag(Thread_t* thread)
{
	// 0x00489BC0 -> 0x0046B600 -> 0x00446DB0: the value, the bit, then the name.
	uint32_t value = Thread_PopStack(thread);
	uint32_t index = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, Sys0_FlagResult(name ? Flags_Set(name, index, value) : 0x80000002u));
	return 0;
}

uint32_t Opcode_Sys0_SetFlagRange(Thread_t* thread)
{
	// 0x00489C40 -> 0x0046B610 -> 0x00446E20: the count, the value, the first
	// bit, then the name.
	uint32_t count = Thread_PopStack(thread);
	uint32_t value = Thread_PopStack(thread);
	uint32_t start = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, Sys0_FlagResult(name ? Flags_SetRange(name, start, count, value) : 0x80000002u));
	return 0;
}

uint32_t Opcode_Sys0_GetFlag(Thread_t* thread)
{
	// 0x00489D00 -> 0x0046B630 -> 0x00446EC0: the bit, the name, then where to
	// put it (a dword, 0 or 1).
	uint32_t index = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	uint32_t value = 0;
	uint32_t r = name ? Flags_Get(name, index, &value) : 0x80000002u;
	if(r == 0 && out)
		Thread_WriteIntToMemory(thread, out, BGI_SIZE_DWORD, value);
	Thread_PushStack(thread, Sys0_FlagResult(r));
	return 0;
}

uint32_t Opcode_Sys0_Unknown_144(Thread_t* thread)
{
	uint32_t data = Thread_PopStack(thread);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_145(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_148(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_149(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_150(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_151(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_CreateRecordList(Thread_t* thread)
{
	// 0x0048A160: record size, then capacity, then where to put the id.
	uint32_t recordSize = Thread_PopStack(thread);
	uint32_t capacity = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	uint32_t id = 0;
	uint32_t result = Engine_CreateRing(capacity, recordSize, &id);
	if(result == 0 && out != NULL)
		Thread_WriteIntToMemory(thread, out, BGI_SIZE_DWORD, id);
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_DestroyRecordList(Thread_t* thread)
{
	// 0x0048A1A0.
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_DestroyRing(id));
	return 0;
}

uint32_t Opcode_Sys0_RecordListCount(Thread_t* thread)
{
	// 0x0048A1D0: the id, then where to put the count.
	uint32_t id = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	uint32_t count = 0;
	uint32_t result = Engine_RingCount(id, &count);
	if(result == 0 && out != NULL)
		Thread_WriteIntToMemory(thread, out, BGI_SIZE_DWORD, count);
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_AddToRecordList(Thread_t* thread)
{
	// 0x0048A210: the record, then the id.
	uint8_t* record = Thread_PopAndResolveAddress(thread);
	uint32_t id = Thread_PopStack(thread);
	if(record == NULL)
		return 0xFFFFFFFF;
	Thread_PushStack(thread, Engine_RingAdd(id, record));
	return 0;
}

uint32_t Opcode_Sys0_DropFromRecordList(Thread_t* thread)
{
	// 0x0048A290: how many, then from where, then the id.
	uint32_t count = Thread_PopStack(thread);
	uint32_t index = Thread_PopStack(thread);
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_RingDrop(id, index, count));
	return 0;
}

uint32_t Opcode_Sys0_ReadRecordList(Thread_t* thread)
{
	// 0x0048A250: the index, then the id, then where to put the record.
	uint32_t index = Thread_PopStack(thread);
	uint32_t id = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, Engine_RingRead(id, index, out));
	return 0;
}

/*
 * Sys0 0xA0 (0x0048A2D0 -> 0x00496710) takes the next event off the engine's own
 * global queue - the head at 0x005669C8 with its tail pointer at 0x00503DEC,
 * appended to by 0x004966D0 from thirty-seven places: the window procedure, the
 * display objects, the interpreter's own failure paths. It writes the event's
 * three words to the address it is given and pushes whether there was one, and
 * when the queue is empty it writes NOTHING and pushes 0.
 *
 * It used to write three words of its own over the caller's buffer on every
 * single call - 3 and 1, or one of two other triples chosen by the thread's tick
 * count. Those tick numbers were a recording of one particular run, so the two
 * special cases had long stopped happening and every call handed the script an
 * event that never occurred.
 */
uint32_t Opcode_Sys0_PopGlobalList(Thread_t* thread)
{
	uint32_t* out = (uint32_t*)Thread_PopAndResolveAddress(thread);
	if(out == NULL)
	{
		Thread_PushStack(thread, 0);
		return 0;
	}
	Thread_PushStack(thread, (uint32_t)Engine_PopGlobalList(out));
	return 0;
}

uint32_t Opcode_Sys0_PushGlobalList(Thread_t* thread)
{
	uint32_t value1 = Thread_PopStack(thread);
	uint32_t value2 = Thread_PopStack(thread);
	Engine_PushGlobalList(0, value2, value1);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_168(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_169(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_172(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_176(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_177(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_180(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_181(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_182(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_192(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_193(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_196(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_197(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_CreateRecordTable(Thread_t* thread)
{
	uint32_t recordSize = Thread_PopStack(thread);
	uint8_t* idOut = Thread_PopAndResolveAddress(thread);
	uint32_t id = 0;
	uint32_t result = Engine_CreateRecordTable(recordSize, &id);
	if(result == 0)
		memcpy(idOut, &id, sizeof(id));
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_DestroyRecordTable(Thread_t* thread)
{
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_DestroyRecordTable(id));
	return 0;
}

uint32_t Opcode_Sys0_SetRecord(Thread_t* thread)
{
	const uint8_t* value = Thread_PopAndResolveAddress(thread);
	const char* key = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_SetRecord(id, key, value));
	return 0;
}

uint32_t Opcode_Sys0_DeleteRecord(Thread_t* thread)
{
	// 0x0048A910: the key, then the id.
	const char* key = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_DeleteRecord(id, key));
	return 0;
}

uint32_t Opcode_Sys0_ReadRecord(Thread_t* thread)
{
	// 0x0048A940: an index, then a key, then the id, then where to put the
	// record. 0x004963B0 chooses the form on the key alone: a key that resolved
	// to nothing means the index names the record instead.
	uint32_t index = Thread_PopStack(thread);
	const char* key = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t id = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);

	uint32_t result = key != NULL ? Engine_ReadRecordByKey(id, key, out)
	                              : Engine_ReadRecordByIndex(id, index, out);
	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Sys0_ClearStringTables(Thread_t* thread)
{
	// 0x0048A990 -> 0x00495630: every string table but 0x80000000.
	StrTab_RemoveAll(1);
	return 0;
}

uint32_t Opcode_Sys0_StringTableCount(Thread_t* thread)
{
	// 0x0048A9A0 -> 0x00495850: how many strings a table holds (0 for none).
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, StrTab_Count(id));
	return 0;
}

uint32_t Opcode_Sys0_AddString(Thread_t* thread)
{
	// 0x0048AA50 -> 0x00495870: the string, then the table id; pushes its index,
	// the old one when the table already has it.
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, text ? StrTab_Add(id, text) : 0);
	return 0;
}

uint32_t Opcode_Sys0_ReadString(Thread_t* thread)
{
	// 0x0048AA80 -> 0x004959E0: the index, the table id, then where to copy the
	// string. Pushes 0, 0x80000001 (no table) or 0x80000002 (no such index).
	uint32_t index = Thread_PopStack(thread);
	uint32_t id = Thread_PopStack(thread);
	char* out = (char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, StrTab_Read(id, (int32_t)index, out, NULL));
	return 0;
}

uint32_t Opcode_Sys0_Unknown_224(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_225(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_226(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_227(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_GetGameId(Thread_t* thread)
{
	uint8_t* destination = Thread_PopAndResolveAddress(thread);
	memcpy(destination, gGameId, ENGINE_GAME_ID_SIZE);
	printf("[Thread %d]: %sGame id is \"%.*s\"\n", thread->threadId, TLevel[thread->level], ENGINE_GAME_ID_SIZE, gGameId);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_236(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_237(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_238(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_239(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_240(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_241(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_242(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_243(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_244(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_245(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_246(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_247(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_248(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_249(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_250(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_251(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_Unknown_252(Thread_t* thread)
{
	return 0xFFFFFFFF;
}


uint32_t Opcode_Sys0_IsLauncher(Thread_t* thread)
{
	// Dummy
	uint32_t data = 0;
	Thread_PushStack(thread, data);
	return 0;
}

uint32_t Opcode_Sys0_Unknown_254(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys0_SetUserDirectory(Thread_t* thread)
{
	const char* path = (const char*)Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, Engine_SetUserDirectory(path));
	return 0;
}

static uint32_t Sys0_FlagResult(uint32_t r)
{
	switch(r)
	{
		case 0:           return 0;
		case 0x80000002u: return 1;
		case 0x80000003u: return 2;
		case 0x80000004u: return 3;
		default:          return r;
	}
}

uint32_t Opcode_Sys0_LoadStringTable(Thread_t* thread)
{
	// 0x0048A9D0 -> 0x004956E0: the strings (NUL-terminated, one after another),
	// their count, then the table id. Replaces the table; a count of 0 removes it.
	const char* data = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t count = Thread_PopStack(thread);
	uint32_t id = Thread_PopStack(thread);
	Thread_PushStack(thread, StrTab_Load(id, count, data));
	return 0;
}

uint32_t Opcode_Sys0_SerializeStringTable(Thread_t* thread)
{
	// 0x0048AA10 -> 0x004957D0: the table id, then where to copy its strings (or
	// nothing, to ask the size). Pushes the byte count.
	uint32_t id = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	Thread_PushStack(thread, StrTab_Serialize(id, out));
	return 0;
}

uint32_t Opcode_Sys0_StringLength(Thread_t* thread)
{
	// 0x0048AAC0 -> 0x004959E0: the index, the table id, then where to put the
	// string's length (a dword). Pushes the same status as Sys0 0xDD.
	uint32_t index = Thread_PopStack(thread);
	uint32_t id = Thread_PopStack(thread);
	uint8_t* out = Thread_PopAndResolveAddress(thread);
	uint32_t length = 0;
	uint32_t r = StrTab_Read(id, (int32_t)index, NULL, &length);
	if(r == 0 && out)
		Thread_WriteIntToMemory(thread, out, BGI_SIZE_DWORD, length);
	Thread_PushStack(thread, r);
	return 0;
}
