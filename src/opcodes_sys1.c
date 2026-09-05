#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "opcodes.h"
#include "opcodes_sys1.h"
#include "thread.h"

// The 0x81 group is the engine's second system-control group. Its dispatch table
// lives at 0x00503F00 in fureraba.exe and is reached from 0x0048CAB0; an entry the
// engine does not define reports "undefined system control instruction $81xx".
// The NULL entries below are exactly the ones NULL in that table, so the group's
// shape is the original's, not a guess. Everything the engine does define starts
// out unimplemented and is filled in as the game reaches it.

char* OpcodesSys1Mnemonics[256] = {
	/* 0x00   0 */ "--Unknown--",
	/* 0x01   1 */ "--Unknown--",
	/* 0x02   2 */ "--Unknown--",
	/* 0x03   3 */ "--Unknown--",
	/* 0x04   4 */ "Unknown_4",
	/* 0x05   5 */ "--Unknown--",
	/* 0x06   6 */ "--Unknown--",
	/* 0x07   7 */ "Unknown_7",
	/* 0x08   8 */ "Unknown_8",
	/* 0x09   9 */ "Unknown_9",
	/* 0x0A  10 */ "Unknown_10",
	/* 0x0B  11 */ "Unknown_11",
	/* 0x0C  12 */ "Unknown_12",
	/* 0x0D  13 */ "Unknown_13",
	/* 0x0E  14 */ "Unknown_14",
	/* 0x0F  15 */ "Unknown_15",
	/* 0x10  16 */ "Unknown_16",
	/* 0x11  17 */ "Unknown_17",
	/* 0x12  18 */ "--Unknown--",
	/* 0x13  19 */ "--Unknown--",
	/* 0x14  20 */ "Unknown_20",
	/* 0x15  21 */ "--Unknown--",
	/* 0x16  22 */ "Unknown_22",
	/* 0x17  23 */ "Unknown_23",
	/* 0x18  24 */ "Unknown_24",
	/* 0x19  25 */ "Unknown_25",
	/* 0x1A  26 */ "--Unknown--",
	/* 0x1B  27 */ "Unknown_27",
	/* 0x1C  28 */ "--Unknown--",
	/* 0x1D  29 */ "Unknown_29",
	/* 0x1E  30 */ "Unknown_30",
	/* 0x1F  31 */ "Unknown_31",
	/* 0x20  32 */ "--Unknown--",
	/* 0x21  33 */ "--Unknown--",
	/* 0x22  34 */ "--Unknown--",
	/* 0x23  35 */ "--Unknown--",
	/* 0x24  36 */ "--Unknown--",
	/* 0x25  37 */ "--Unknown--",
	/* 0x26  38 */ "--Unknown--",
	/* 0x27  39 */ "--Unknown--",
	/* 0x28  40 */ "Unknown_40",
	/* 0x29  41 */ "Unknown_41",
	/* 0x2A  42 */ "Unknown_42",
	/* 0x2B  43 */ "Unknown_43",
	/* 0x2C  44 */ "Unknown_44",
	/* 0x2D  45 */ "Unknown_45",
	/* 0x2E  46 */ "--Unknown--",
	/* 0x2F  47 */ "Unknown_47",
	/* 0x30  48 */ "Unknown_48",
	/* 0x31  49 */ "Unknown_49",
	/* 0x32  50 */ "Unknown_50",
	/* 0x33  51 */ "--Unknown--",
	/* 0x34  52 */ "--Unknown--",
	/* 0x35  53 */ "Unknown_53",
	/* 0x36  54 */ "Unknown_54",
	/* 0x37  55 */ "Unknown_55",
	/* 0x38  56 */ "Unknown_56",
	/* 0x39  57 */ "Unknown_57",
	/* 0x3A  58 */ "Unknown_58",
	/* 0x3B  59 */ "Unknown_59",
	/* 0x3C  60 */ "Unknown_60",
	/* 0x3D  61 */ "Unknown_61",
	/* 0x3E  62 */ "Unknown_62",
	/* 0x3F  63 */ "--Unknown--",
	/* 0x40  64 */ "--Unknown--",
	/* 0x41  65 */ "--Unknown--",
	/* 0x42  66 */ "--Unknown--",
	/* 0x43  67 */ "--Unknown--",
	/* 0x44  68 */ "Unknown_68",
	/* 0x45  69 */ "--Unknown--",
	/* 0x46  70 */ "--Unknown--",
	/* 0x47  71 */ "--Unknown--",
	/* 0x48  72 */ "--Unknown--",
	/* 0x49  73 */ "--Unknown--",
	/* 0x4A  74 */ "--Unknown--",
	/* 0x4B  75 */ "--Unknown--",
	/* 0x4C  76 */ "--Unknown--",
	/* 0x4D  77 */ "--Unknown--",
	/* 0x4E  78 */ "--Unknown--",
	/* 0x4F  79 */ "--Unknown--",
	/* 0x50  80 */ "--Unknown--",
	/* 0x51  81 */ "--Unknown--",
	/* 0x52  82 */ "--Unknown--",
	/* 0x53  83 */ "--Unknown--",
	/* 0x54  84 */ "--Unknown--",
	/* 0x55  85 */ "--Unknown--",
	/* 0x56  86 */ "--Unknown--",
	/* 0x57  87 */ "--Unknown--",
	/* 0x58  88 */ "--Unknown--",
	/* 0x59  89 */ "--Unknown--",
	/* 0x5A  90 */ "--Unknown--",
	/* 0x5B  91 */ "--Unknown--",
	/* 0x5C  92 */ "--Unknown--",
	/* 0x5D  93 */ "--Unknown--",
	/* 0x5E  94 */ "--Unknown--",
	/* 0x5F  95 */ "--Unknown--",
	/* 0x60  96 */ "SetDisplayModeSize",
	/* 0x61  97 */ "Unknown_97",
	/* 0x62  98 */ "Unknown_98",
	/* 0x63  99 */ "SetScreenMappingMode",
	/* 0x64 100 */ "Unknown_100",
	/* 0x65 101 */ "Unknown_101",
	/* 0x66 102 */ "--Unknown--",
	/* 0x67 103 */ "--Unknown--",
	/* 0x68 104 */ "SetAudioResumeOnActivate",
	/* 0x69 105 */ "Unknown_105",
	/* 0x6A 106 */ "Unknown_106",
	/* 0x6B 107 */ "Unknown_107",
	/* 0x6C 108 */ "--Unknown--",
	/* 0x6D 109 */ "Unknown_109",
	/* 0x6E 110 */ "Unknown_110",
	/* 0x6F 111 */ "Unknown_111",
	/* 0x70 112 */ "--Unknown--",
	/* 0x71 113 */ "--Unknown--",
	/* 0x72 114 */ "--Unknown--",
	/* 0x73 115 */ "--Unknown--",
	/* 0x74 116 */ "--Unknown--",
	/* 0x75 117 */ "--Unknown--",
	/* 0x76 118 */ "--Unknown--",
	/* 0x77 119 */ "--Unknown--",
	/* 0x78 120 */ "--Unknown--",
	/* 0x79 121 */ "--Unknown--",
	/* 0x7A 122 */ "--Unknown--",
	/* 0x7B 123 */ "--Unknown--",
	/* 0x7C 124 */ "--Unknown--",
	/* 0x7D 125 */ "--Unknown--",
	/* 0x7E 126 */ "--Unknown--",
	/* 0x7F 127 */ "--Unknown--",
	/* 0x80 128 */ "--Unknown--",
	/* 0x81 129 */ "--Unknown--",
	/* 0x82 130 */ "--Unknown--",
	/* 0x83 131 */ "--Unknown--",
	/* 0x84 132 */ "--Unknown--",
	/* 0x85 133 */ "--Unknown--",
	/* 0x86 134 */ "--Unknown--",
	/* 0x87 135 */ "--Unknown--",
	/* 0x88 136 */ "--Unknown--",
	/* 0x89 137 */ "--Unknown--",
	/* 0x8A 138 */ "--Unknown--",
	/* 0x8B 139 */ "--Unknown--",
	/* 0x8C 140 */ "--Unknown--",
	/* 0x8D 141 */ "--Unknown--",
	/* 0x8E 142 */ "--Unknown--",
	/* 0x8F 143 */ "--Unknown--",
	/* 0x90 144 */ "--Unknown--",
	/* 0x91 145 */ "--Unknown--",
	/* 0x92 146 */ "--Unknown--",
	/* 0x93 147 */ "--Unknown--",
	/* 0x94 148 */ "--Unknown--",
	/* 0x95 149 */ "--Unknown--",
	/* 0x96 150 */ "--Unknown--",
	/* 0x97 151 */ "--Unknown--",
	/* 0x98 152 */ "--Unknown--",
	/* 0x99 153 */ "--Unknown--",
	/* 0x9A 154 */ "--Unknown--",
	/* 0x9B 155 */ "--Unknown--",
	/* 0x9C 156 */ "--Unknown--",
	/* 0x9D 157 */ "--Unknown--",
	/* 0x9E 158 */ "--Unknown--",
	/* 0x9F 159 */ "--Unknown--",
	/* 0xA0 160 */ "--Unknown--",
	/* 0xA1 161 */ "--Unknown--",
	/* 0xA2 162 */ "--Unknown--",
	/* 0xA3 163 */ "--Unknown--",
	/* 0xA4 164 */ "--Unknown--",
	/* 0xA5 165 */ "--Unknown--",
	/* 0xA6 166 */ "--Unknown--",
	/* 0xA7 167 */ "--Unknown--",
	/* 0xA8 168 */ "--Unknown--",
	/* 0xA9 169 */ "--Unknown--",
	/* 0xAA 170 */ "--Unknown--",
	/* 0xAB 171 */ "--Unknown--",
	/* 0xAC 172 */ "--Unknown--",
	/* 0xAD 173 */ "--Unknown--",
	/* 0xAE 174 */ "--Unknown--",
	/* 0xAF 175 */ "--Unknown--",
	/* 0xB0 176 */ "Unknown_176",
	/* 0xB1 177 */ "--Unknown--",
	/* 0xB2 178 */ "--Unknown--",
	/* 0xB3 179 */ "--Unknown--",
	/* 0xB4 180 */ "--Unknown--",
	/* 0xB5 181 */ "--Unknown--",
	/* 0xB6 182 */ "--Unknown--",
	/* 0xB7 183 */ "Unknown_183",
	/* 0xB8 184 */ "--Unknown--",
	/* 0xB9 185 */ "--Unknown--",
	/* 0xBA 186 */ "--Unknown--",
	/* 0xBB 187 */ "--Unknown--",
	/* 0xBC 188 */ "--Unknown--",
	/* 0xBD 189 */ "--Unknown--",
	/* 0xBE 190 */ "--Unknown--",
	/* 0xBF 191 */ "--Unknown--",
	/* 0xC0 192 */ "--Unknown--",
	/* 0xC1 193 */ "--Unknown--",
	/* 0xC2 194 */ "--Unknown--",
	/* 0xC3 195 */ "--Unknown--",
	/* 0xC4 196 */ "--Unknown--",
	/* 0xC5 197 */ "--Unknown--",
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
	/* 0xD0 208 */ "Unknown_208",
	/* 0xD1 209 */ "Unknown_209",
	/* 0xD2 210 */ "Unknown_210",
	/* 0xD3 211 */ "Unknown_211",
	/* 0xD4 212 */ "Unknown_212",
	/* 0xD5 213 */ "Unknown_213",
	/* 0xD6 214 */ "--Unknown--",
	/* 0xD7 215 */ "--Unknown--",
	/* 0xD8 216 */ "--Unknown--",
	/* 0xD9 217 */ "--Unknown--",
	/* 0xDA 218 */ "--Unknown--",
	/* 0xDB 219 */ "--Unknown--",
	/* 0xDC 220 */ "--Unknown--",
	/* 0xDD 221 */ "--Unknown--",
	/* 0xDE 222 */ "--Unknown--",
	/* 0xDF 223 */ "--Unknown--",
	/* 0xE0 224 */ "Unknown_224",
	/* 0xE1 225 */ "--Unknown--",
	/* 0xE2 226 */ "--Unknown--",
	/* 0xE3 227 */ "--Unknown--",
	/* 0xE4 228 */ "--Unknown--",
	/* 0xE5 229 */ "--Unknown--",
	/* 0xE6 230 */ "--Unknown--",
	/* 0xE7 231 */ "--Unknown--",
	/* 0xE8 232 */ "--Unknown--",
	/* 0xE9 233 */ "Unknown_233",
	/* 0xEA 234 */ "Unknown_234",
	/* 0xEB 235 */ "--Unknown--",
	/* 0xEC 236 */ "Unknown_236",
	/* 0xED 237 */ "Unknown_237",
	/* 0xEE 238 */ "--Unknown--",
	/* 0xEF 239 */ "--Unknown--",
	/* 0xF0 240 */ "--Unknown--",
	/* 0xF1 241 */ "--Unknown--",
	/* 0xF2 242 */ "Unknown_242",
	/* 0xF3 243 */ "--Unknown--",
	/* 0xF4 244 */ "--Unknown--",
	/* 0xF5 245 */ "--Unknown--",
	/* 0xF6 246 */ "--Unknown--",
	/* 0xF7 247 */ "Unknown_247",
	/* 0xF8 248 */ "--Unknown--",
	/* 0xF9 249 */ "--Unknown--",
	/* 0xFA 250 */ "--Unknown--",
	/* 0xFB 251 */ "--Unknown--",
	/* 0xFC 252 */ "--Unknown--",
	/* 0xFD 253 */ "--Unknown--",
	/* 0xFE 254 */ "--Unknown--",
	/* 0xFF 255 */ "--Unknown--",
};

OpcodePtr_t OpcodesSys1[256] = {
	/* 0x00   0 */ NULL,
	/* 0x01   1 */ NULL,
	/* 0x02   2 */ NULL,
	/* 0x03   3 */ NULL,
	/* 0x04   4 */ Opcode_Sys1_Unknown_4,
	/* 0x05   5 */ NULL,
	/* 0x06   6 */ NULL,
	/* 0x07   7 */ Opcode_Sys1_Unknown_7,
	/* 0x08   8 */ Opcode_Sys1_Unknown_8,
	/* 0x09   9 */ Opcode_Sys1_Unknown_9,
	/* 0x0A  10 */ Opcode_Sys1_Unknown_10,
	/* 0x0B  11 */ Opcode_Sys1_Unknown_11,
	/* 0x0C  12 */ Opcode_Sys1_Unknown_12,
	/* 0x0D  13 */ Opcode_Sys1_Unknown_13,
	/* 0x0E  14 */ Opcode_Sys1_Unknown_14,
	/* 0x0F  15 */ Opcode_Sys1_Unknown_15,
	/* 0x10  16 */ Opcode_Sys1_Unknown_16,
	/* 0x11  17 */ Opcode_Sys1_Unknown_17,
	/* 0x12  18 */ NULL,
	/* 0x13  19 */ NULL,
	/* 0x14  20 */ Opcode_Sys1_Unknown_20,
	/* 0x15  21 */ NULL,
	/* 0x16  22 */ Opcode_Sys1_Unknown_22,
	/* 0x17  23 */ Opcode_Sys1_Unknown_23,
	/* 0x18  24 */ Opcode_Sys1_Unknown_24,
	/* 0x19  25 */ Opcode_Sys1_Unknown_25,
	/* 0x1A  26 */ NULL,
	/* 0x1B  27 */ Opcode_Sys1_Unknown_27,
	/* 0x1C  28 */ NULL,
	/* 0x1D  29 */ Opcode_Sys1_Unknown_29,
	/* 0x1E  30 */ Opcode_Sys1_Unknown_30,
	/* 0x1F  31 */ Opcode_Sys1_Unknown_31,
	/* 0x20  32 */ NULL,
	/* 0x21  33 */ NULL,
	/* 0x22  34 */ NULL,
	/* 0x23  35 */ NULL,
	/* 0x24  36 */ NULL,
	/* 0x25  37 */ NULL,
	/* 0x26  38 */ NULL,
	/* 0x27  39 */ NULL,
	/* 0x28  40 */ Opcode_Sys1_Unknown_40,
	/* 0x29  41 */ Opcode_Sys1_Unknown_41,
	/* 0x2A  42 */ Opcode_Sys1_Unknown_42,
	/* 0x2B  43 */ Opcode_Sys1_Unknown_43,
	/* 0x2C  44 */ Opcode_Sys1_Unknown_44,
	/* 0x2D  45 */ Opcode_Sys1_Unknown_45,
	/* 0x2E  46 */ NULL,
	/* 0x2F  47 */ Opcode_Sys1_Unknown_47,
	/* 0x30  48 */ Opcode_Sys1_Unknown_48,
	/* 0x31  49 */ Opcode_Sys1_Unknown_49,
	/* 0x32  50 */ Opcode_Sys1_Unknown_50,
	/* 0x33  51 */ NULL,
	/* 0x34  52 */ NULL,
	/* 0x35  53 */ Opcode_Sys1_Unknown_53,
	/* 0x36  54 */ Opcode_Sys1_Unknown_54,
	/* 0x37  55 */ Opcode_Sys1_Unknown_55,
	/* 0x38  56 */ Opcode_Sys1_Unknown_56,
	/* 0x39  57 */ Opcode_Sys1_Unknown_57,
	/* 0x3A  58 */ Opcode_Sys1_Unknown_58,
	/* 0x3B  59 */ Opcode_Sys1_Unknown_59,
	/* 0x3C  60 */ Opcode_Sys1_Unknown_60,
	/* 0x3D  61 */ Opcode_Sys1_Unknown_61,
	/* 0x3E  62 */ Opcode_Sys1_Unknown_62,
	/* 0x3F  63 */ NULL,
	/* 0x40  64 */ NULL,
	/* 0x41  65 */ NULL,
	/* 0x42  66 */ NULL,
	/* 0x43  67 */ NULL,
	/* 0x44  68 */ Opcode_Sys1_Unknown_68,
	/* 0x45  69 */ NULL,
	/* 0x46  70 */ NULL,
	/* 0x47  71 */ NULL,
	/* 0x48  72 */ NULL,
	/* 0x49  73 */ NULL,
	/* 0x4A  74 */ NULL,
	/* 0x4B  75 */ NULL,
	/* 0x4C  76 */ NULL,
	/* 0x4D  77 */ NULL,
	/* 0x4E  78 */ NULL,
	/* 0x4F  79 */ NULL,
	/* 0x50  80 */ NULL,
	/* 0x51  81 */ NULL,
	/* 0x52  82 */ NULL,
	/* 0x53  83 */ NULL,
	/* 0x54  84 */ NULL,
	/* 0x55  85 */ NULL,
	/* 0x56  86 */ NULL,
	/* 0x57  87 */ NULL,
	/* 0x58  88 */ NULL,
	/* 0x59  89 */ NULL,
	/* 0x5A  90 */ NULL,
	/* 0x5B  91 */ NULL,
	/* 0x5C  92 */ NULL,
	/* 0x5D  93 */ NULL,
	/* 0x5E  94 */ NULL,
	/* 0x5F  95 */ NULL,
	/* 0x60  96 */ Opcode_Sys1_SetDisplayModeSize,
	/* 0x61  97 */ Opcode_Sys1_Unknown_97,
	/* 0x62  98 */ Opcode_Sys1_Unknown_98,
	/* 0x63  99 */ Opcode_Sys1_SetScreenMappingMode,
	/* 0x64 100 */ Opcode_Sys1_Unknown_100,
	/* 0x65 101 */ Opcode_Sys1_Unknown_101,
	/* 0x66 102 */ NULL,
	/* 0x67 103 */ NULL,
	/* 0x68 104 */ Opcode_Sys1_SetAudioResumeOnActivate,
	/* 0x69 105 */ Opcode_Sys1_Unknown_105,
	/* 0x6A 106 */ Opcode_Sys1_Unknown_106,
	/* 0x6B 107 */ Opcode_Sys1_Unknown_107,
	/* 0x6C 108 */ NULL,
	/* 0x6D 109 */ Opcode_Sys1_Unknown_109,
	/* 0x6E 110 */ Opcode_Sys1_Unknown_110,
	/* 0x6F 111 */ Opcode_Sys1_Unknown_111,
	/* 0x70 112 */ NULL,
	/* 0x71 113 */ NULL,
	/* 0x72 114 */ NULL,
	/* 0x73 115 */ NULL,
	/* 0x74 116 */ NULL,
	/* 0x75 117 */ NULL,
	/* 0x76 118 */ NULL,
	/* 0x77 119 */ NULL,
	/* 0x78 120 */ NULL,
	/* 0x79 121 */ NULL,
	/* 0x7A 122 */ NULL,
	/* 0x7B 123 */ NULL,
	/* 0x7C 124 */ NULL,
	/* 0x7D 125 */ NULL,
	/* 0x7E 126 */ NULL,
	/* 0x7F 127 */ NULL,
	/* 0x80 128 */ NULL,
	/* 0x81 129 */ NULL,
	/* 0x82 130 */ NULL,
	/* 0x83 131 */ NULL,
	/* 0x84 132 */ NULL,
	/* 0x85 133 */ NULL,
	/* 0x86 134 */ NULL,
	/* 0x87 135 */ NULL,
	/* 0x88 136 */ NULL,
	/* 0x89 137 */ NULL,
	/* 0x8A 138 */ NULL,
	/* 0x8B 139 */ NULL,
	/* 0x8C 140 */ NULL,
	/* 0x8D 141 */ NULL,
	/* 0x8E 142 */ NULL,
	/* 0x8F 143 */ NULL,
	/* 0x90 144 */ NULL,
	/* 0x91 145 */ NULL,
	/* 0x92 146 */ NULL,
	/* 0x93 147 */ NULL,
	/* 0x94 148 */ NULL,
	/* 0x95 149 */ NULL,
	/* 0x96 150 */ NULL,
	/* 0x97 151 */ NULL,
	/* 0x98 152 */ NULL,
	/* 0x99 153 */ NULL,
	/* 0x9A 154 */ NULL,
	/* 0x9B 155 */ NULL,
	/* 0x9C 156 */ NULL,
	/* 0x9D 157 */ NULL,
	/* 0x9E 158 */ NULL,
	/* 0x9F 159 */ NULL,
	/* 0xA0 160 */ NULL,
	/* 0xA1 161 */ NULL,
	/* 0xA2 162 */ NULL,
	/* 0xA3 163 */ NULL,
	/* 0xA4 164 */ NULL,
	/* 0xA5 165 */ NULL,
	/* 0xA6 166 */ NULL,
	/* 0xA7 167 */ NULL,
	/* 0xA8 168 */ NULL,
	/* 0xA9 169 */ NULL,
	/* 0xAA 170 */ NULL,
	/* 0xAB 171 */ NULL,
	/* 0xAC 172 */ NULL,
	/* 0xAD 173 */ NULL,
	/* 0xAE 174 */ NULL,
	/* 0xAF 175 */ NULL,
	/* 0xB0 176 */ Opcode_Sys1_Unknown_176,
	/* 0xB1 177 */ NULL,
	/* 0xB2 178 */ NULL,
	/* 0xB3 179 */ NULL,
	/* 0xB4 180 */ NULL,
	/* 0xB5 181 */ NULL,
	/* 0xB6 182 */ NULL,
	/* 0xB7 183 */ Opcode_Sys1_Unknown_183,
	/* 0xB8 184 */ NULL,
	/* 0xB9 185 */ NULL,
	/* 0xBA 186 */ NULL,
	/* 0xBB 187 */ NULL,
	/* 0xBC 188 */ NULL,
	/* 0xBD 189 */ NULL,
	/* 0xBE 190 */ NULL,
	/* 0xBF 191 */ NULL,
	/* 0xC0 192 */ NULL,
	/* 0xC1 193 */ NULL,
	/* 0xC2 194 */ NULL,
	/* 0xC3 195 */ NULL,
	/* 0xC4 196 */ NULL,
	/* 0xC5 197 */ NULL,
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
	/* 0xD0 208 */ Opcode_Sys1_Unknown_208,
	/* 0xD1 209 */ Opcode_Sys1_Unknown_209,
	/* 0xD2 210 */ Opcode_Sys1_Unknown_210,
	/* 0xD3 211 */ Opcode_Sys1_Unknown_211,
	/* 0xD4 212 */ Opcode_Sys1_Unknown_212,
	/* 0xD5 213 */ Opcode_Sys1_Unknown_213,
	/* 0xD6 214 */ NULL,
	/* 0xD7 215 */ NULL,
	/* 0xD8 216 */ NULL,
	/* 0xD9 217 */ NULL,
	/* 0xDA 218 */ NULL,
	/* 0xDB 219 */ NULL,
	/* 0xDC 220 */ NULL,
	/* 0xDD 221 */ NULL,
	/* 0xDE 222 */ NULL,
	/* 0xDF 223 */ NULL,
	/* 0xE0 224 */ Opcode_Sys1_Unknown_224,
	/* 0xE1 225 */ NULL,
	/* 0xE2 226 */ NULL,
	/* 0xE3 227 */ NULL,
	/* 0xE4 228 */ NULL,
	/* 0xE5 229 */ NULL,
	/* 0xE6 230 */ NULL,
	/* 0xE7 231 */ NULL,
	/* 0xE8 232 */ NULL,
	/* 0xE9 233 */ Opcode_Sys1_Unknown_233,
	/* 0xEA 234 */ Opcode_Sys1_Unknown_234,
	/* 0xEB 235 */ NULL,
	/* 0xEC 236 */ Opcode_Sys1_Unknown_236,
	/* 0xED 237 */ Opcode_Sys1_Unknown_237,
	/* 0xEE 238 */ NULL,
	/* 0xEF 239 */ NULL,
	/* 0xF0 240 */ NULL,
	/* 0xF1 241 */ NULL,
	/* 0xF2 242 */ Opcode_Sys1_Unknown_242,
	/* 0xF3 243 */ NULL,
	/* 0xF4 244 */ NULL,
	/* 0xF5 245 */ NULL,
	/* 0xF6 246 */ NULL,
	/* 0xF7 247 */ Opcode_Sys1_Unknown_247,
	/* 0xF8 248 */ NULL,
	/* 0xF9 249 */ NULL,
	/* 0xFA 250 */ NULL,
	/* 0xFB 251 */ NULL,
	/* 0xFC 252 */ NULL,
	/* 0xFD 253 */ NULL,
	/* 0xFE 254 */ NULL,
	/* 0xFF 255 */ NULL,
};

uint32_t Opcode_Sys1_Unknown_4(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_7(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_8(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_9(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_10(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_11(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_12(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_13(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_14(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_15(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_16(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_17(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_20(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_22(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_23(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_24(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_25(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_27(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_29(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_30(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_31(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_40(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_41(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_42(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_43(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_44(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_45(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_47(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_48(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_49(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_50(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_53(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_54(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_55(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_56(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_57(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_58(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_59(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_60(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_61(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_62(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_68(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_SetDisplayModeSize(Thread_t* thread)
{
	uint32_t height = Thread_PopStack(thread);
	uint32_t width = Thread_PopStack(thread);
	uint32_t index = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_SetDisplayModeSize(index, width, height));
	return 0;
}

uint32_t Opcode_Sys1_Unknown_97(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_98(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_SetDisplayFlagUnknown98(value));
	return 0;
}

uint32_t Opcode_Sys1_SetScreenMappingMode(Thread_t* thread)
{
	uint32_t mode = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_SetScreenMappingMode(mode));
	return 0;
}

uint32_t Opcode_Sys1_Unknown_100(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_101(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_SetAudioResumeOnActivate(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Thread_PushStack(thread, Engine_SetAudioResumeOnActivate(value));
	return 0;
}

uint32_t Opcode_Sys1_Unknown_105(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_106(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_107(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_109(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_110(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_111(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_176(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_183(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_208(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_209(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_210(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_211(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_212(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_213(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_224(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_233(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_234(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_236(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_237(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_242(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Sys1_Unknown_247(Thread_t* thread)
{
	return 0xFFFFFFFF;
}
