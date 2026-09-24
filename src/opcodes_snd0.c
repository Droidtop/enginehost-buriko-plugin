#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "opcodes.h"
#include "opcodes_snd0.h"
#include "thread.h"
#include "process.h"
#include "audio.h"

uint32_t gSoundChannels[SND0_CHANNEL_COUNT][SND0_CHANNEL_RECORD_WORDS] = {{0}};

char* OpcodesSnd0Mnemonics[256] = {
	/* 0x00   0 */ "Constant",
	/* 0x01   1 */ "--Unknown--",
	/* 0x02   2 */ "--Unknown--",
	/* 0x03   3 */ "--Unknown--",
	/* 0x04   4 */ "--Unknown--",
	/* 0x05   5 */ "--Unknown--",
	/* 0x06   6 */ "--Unknown--",
	/* 0x07   7 */ "--Unknown--",
	/* 0x08   8 */ "SetMusicMasterVolume",
	/* 0x09   9 */ "SetSEMasterVolume",
	/* 0x0A  10 */ "--Unknown--",
	/* 0x0B  11 */ "--Unknown--",
	/* 0x0C  12 */ "--Unknown--",
	/* 0x0D  13 */ "--Unknown--",
	/* 0x0E  14 */ "--Unknown--",
	/* 0x0F  15 */ "--Unknown--",
	/* 0x10  16 */ "LoadMusicFile",
	/* 0x11  17 */ "LoadMusic",
	/* 0x12  18 */ "LoadMusicLoop",
	/* 0x13  19 */ "--Unknown--",
	/* 0x14  20 */ "PlayMusic",
	/* 0x15  21 */ "GetMusicStatus",
	/* 0x16  22 */ "FadeMusicVolume",
	/* 0x17  23 */ "SetMusicPan",
	/* 0x18  24 */ "FadeInMusic",
	/* 0x19  25 */ "FadeOutMusic",
	/* 0x1A  26 */ "--Unknown--",
	/* 0x1B  27 */ "--Unknown--",
	/* 0x1C  28 */ "SetMusicVolume",
	/* 0x1D  29 */ "--Unknown--",
	/* 0x1E  30 */ "--Unknown--",
	/* 0x1F  31 */ "--Unknown--",
	/* 0x20  32 */ "RegisterSE",
	/* 0x21  33 */ "RegisterSEEx",
	/* 0x22  34 */ "ResetSE",
	/* 0x23  35 */ "RegisterSEDouble",
	/* 0x24  36 */ "PlaySE",
	/* 0x25  37 */ "StopSE",
	/* 0x26  38 */ "FadeOutSE",
	/* 0x27  39 */ "RegisterSESpeed",
	/* 0x28  40 */ "Unknown_40",
	/* 0x29  41 */ "--Unknown--",
	/* 0x2A  42 */ "--Unknown--",
	/* 0x2B  43 */ "--Unknown--",
	/* 0x2C  44 */ "SetSEVolume",
	/* 0x2D  45 */ "--Unknown--",
	/* 0x2E  46 */ "--Unknown--",
	/* 0x2F  47 */ "GetSEDuration",
	/* 0x30  48 */ "--Unknown--",
	/* 0x31  49 */ "--Unknown--",
	/* 0x32  50 */ "--Unknown--",
	/* 0x33  51 */ "--Unknown--",
	/* 0x34  52 */ "--Unknown--",
	/* 0x35  53 */ "--Unknown--",
	/* 0x36  54 */ "--Unknown--",
	/* 0x37  55 */ "--Unknown--",
	/* 0x38  56 */ "--Unknown--",
	/* 0x39  57 */ "--Unknown--",
	/* 0x3A  58 */ "--Unknown--",
	/* 0x3B  59 */ "--Unknown--",
	/* 0x3C  60 */ "--Unknown--",
	/* 0x3D  61 */ "--Unknown--",
	/* 0x3E  62 */ "--Unknown--",
	/* 0x3F  63 */ "--Unknown--",
	/* 0x40  64 */ "--Unknown--",
	/* 0x41  65 */ "--Unknown--",
	/* 0x42  66 */ "--Unknown--",
	/* 0x43  67 */ "--Unknown--",
	/* 0x44  68 */ "--Unknown--",
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
	/* 0x60  96 */ "--Unknown--",
	/* 0x61  97 */ "--Unknown--",
	/* 0x62  98 */ "--Unknown--",
	/* 0x63  99 */ "--Unknown--",
	/* 0x64 100 */ "--Unknown--",
	/* 0x65 101 */ "--Unknown--",
	/* 0x66 102 */ "--Unknown--",
	/* 0x67 103 */ "--Unknown--",
	/* 0x68 104 */ "--Unknown--",
	/* 0x69 105 */ "--Unknown--",
	/* 0x6A 106 */ "--Unknown--",
	/* 0x6B 107 */ "--Unknown--",
	/* 0x6C 108 */ "--Unknown--",
	/* 0x6D 109 */ "--Unknown--",
	/* 0x6E 110 */ "--Unknown--",
	/* 0x6F 111 */ "--Unknown--",
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
	/* 0x80 128 */ "Unknown_128",
	/* 0x81 129 */ "Unknown_129",
	/* 0x82 130 */ "--Unknown--",
	/* 0x83 131 */ "--Unknown--",
	/* 0x84 132 */ "Unknown_132",
	/* 0x85 133 */ "Unknown_133",
	/* 0x86 134 */ "Unknown_134",
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
	/* 0xB0 176 */ "--Unknown--",
	/* 0xB1 177 */ "--Unknown--",
	/* 0xB2 178 */ "--Unknown--",
	/* 0xB3 179 */ "--Unknown--",
	/* 0xB4 180 */ "--Unknown--",
	/* 0xB5 181 */ "--Unknown--",
	/* 0xB6 182 */ "--Unknown--",
	/* 0xB7 183 */ "--Unknown--",
	/* 0xB8 184 */ "--Unknown--",
	/* 0xB9 185 */ "--Unknown--",
	/* 0xBA 186 */ "--Unknown--",
	/* 0xBB 187 */ "--Unknown--",
	/* 0xBC 188 */ "--Unknown--",
	/* 0xBD 189 */ "--Unknown--",
	/* 0xBE 190 */ "--Unknown--",
	/* 0xBF 191 */ "--Unknown--",
	/* 0xC0 192 */ "PlaySound",
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
	/* 0xD0 208 */ "--Unknown--",
	/* 0xD1 209 */ "--Unknown--",
	/* 0xD2 210 */ "--Unknown--",
	/* 0xD3 211 */ "--Unknown--",
	/* 0xD4 212 */ "--Unknown--",
	/* 0xD5 213 */ "--Unknown--",
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
	/* 0xE0 224 */ "--Unknown--",
	/* 0xE1 225 */ "--Unknown--",
	/* 0xE2 226 */ "--Unknown--",
	/* 0xE3 227 */ "--Unknown--",
	/* 0xE4 228 */ "--Unknown--",
	/* 0xE5 229 */ "--Unknown--",
	/* 0xE6 230 */ "--Unknown--",
	/* 0xE7 231 */ "--Unknown--",
	/* 0xE8 232 */ "--Unknown--",
	/* 0xE9 233 */ "--Unknown--",
	/* 0xEA 234 */ "--Unknown--",
	/* 0xEB 235 */ "--Unknown--",
	/* 0xEC 236 */ "--Unknown--",
	/* 0xED 237 */ "--Unknown--",
	/* 0xEE 238 */ "--Unknown--",
	/* 0xEF 239 */ "--Unknown--",
	/* 0xF0 240 */ "--Unknown--",
	/* 0xF1 241 */ "--Unknown--",
	/* 0xF2 242 */ "--Unknown--",
	/* 0xF3 243 */ "--Unknown--",
	/* 0xF4 244 */ "--Unknown--",
	/* 0xF5 245 */ "--Unknown--",
	/* 0xF6 246 */ "--Unknown--",
	/* 0xF7 247 */ "--Unknown--",
	/* 0xF8 248 */ "--Unknown--",
	/* 0xF9 249 */ "--Unknown--",
	/* 0xFA 250 */ "--Unknown--",
	/* 0xFB 251 */ "--Unknown--",
	/* 0xFC 252 */ "--Unknown--",
	/* 0xFD 253 */ "--Unknown--",
	/* 0xFE 254 */ "--Unknown--",
	/* 0xFF 255 */ "--Unknown--",
};

OpcodePtr_t OpcodesSnd0[256] = {
	/* 0x00   0 */ Opcode_Snd0_Constant,
	/* 0x01   1 */ NULL,
	/* 0x02   2 */ NULL,
	/* 0x03   3 */ NULL,
	/* 0x04   4 */ NULL,
	/* 0x05   5 */ NULL,
	/* 0x06   6 */ NULL,
	/* 0x07   7 */ NULL,
	/* 0x08   8 */ Opcode_Snd0_SetMusicMasterVolume,
	/* 0x09   9 */ Opcode_Snd0_SetSEMasterVolume,
	/* 0x0A  10 */ NULL,
	/* 0x0B  11 */ NULL,
	/* 0x0C  12 */ NULL,
	/* 0x0D  13 */ NULL,
	/* 0x0E  14 */ NULL,
	/* 0x0F  15 */ NULL,
	/* 0x10  16 */ Opcode_Snd0_LoadMusicFile,
	/* 0x11  17 */ Opcode_Snd0_LoadMusic,
	/* 0x12  18 */ Opcode_Snd0_LoadMusicLoop,
	/* 0x13  19 */ NULL,
	/* 0x14  20 */ Opcode_Snd0_PlayMusic,
	/* 0x15  21 */ Opcode_Snd0_GetMusicStatus,
	/* 0x16  22 */ Opcode_Snd0_FadeMusicVolume,
	/* 0x17  23 */ Opcode_Snd0_SetMusicPan,
	/* 0x18  24 */ Opcode_Snd0_FadeInMusic,
	/* 0x19  25 */ Opcode_Snd0_FadeOutMusic,
	/* 0x1A  26 */ NULL,
	/* 0x1B  27 */ NULL,
	/* 0x1C  28 */ Opcode_Snd0_SetMusicVolume,
	/* 0x1D  29 */ NULL,
	/* 0x1E  30 */ NULL,
	/* 0x1F  31 */ NULL,
	/* 0x20  32 */ Opcode_Snd0_RegisterSE,
	/* 0x21  33 */ Opcode_Snd0_RegisterSEEx,
	/* 0x22  34 */ Opcode_Snd0_ResetSE,
	/* 0x23  35 */ Opcode_Snd0_RegisterSEDouble,
	/* 0x24  36 */ Opcode_Snd0_PlaySE,
	/* 0x25  37 */ Opcode_Snd0_StopSE,
	/* 0x26  38 */ Opcode_Snd0_FadeOutSE,
	/* 0x27  39 */ Opcode_Snd0_RegisterSESpeed,
	/* 0x28  40 */ Opcode_Snd0_Unknown_40,
	/* 0x29  41 */ NULL,
	/* 0x2A  42 */ NULL,
	/* 0x2B  43 */ NULL,
	/* 0x2C  44 */ Opcode_Snd0_SetSEVolume,
	/* 0x2D  45 */ NULL,
	/* 0x2E  46 */ NULL,
	/* 0x2F  47 */ Opcode_Snd0_GetSEDuration,
	/* 0x30  48 */ NULL,
	/* 0x31  49 */ NULL,
	/* 0x32  50 */ NULL,
	/* 0x33  51 */ NULL,
	/* 0x34  52 */ NULL,
	/* 0x35  53 */ NULL,
	/* 0x36  54 */ NULL,
	/* 0x37  55 */ NULL,
	/* 0x38  56 */ NULL,
	/* 0x39  57 */ NULL,
	/* 0x3A  58 */ NULL,
	/* 0x3B  59 */ NULL,
	/* 0x3C  60 */ NULL,
	/* 0x3D  61 */ NULL,
	/* 0x3E  62 */ NULL,
	/* 0x3F  63 */ NULL,
	/* 0x40  64 */ NULL,
	/* 0x41  65 */ NULL,
	/* 0x42  66 */ NULL,
	/* 0x43  67 */ NULL,
	/* 0x44  68 */ NULL,
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
	/* 0x60  96 */ NULL,
	/* 0x61  97 */ NULL,
	/* 0x62  98 */ NULL,
	/* 0x63  99 */ NULL,
	/* 0x64 100 */ NULL,
	/* 0x65 101 */ NULL,
	/* 0x66 102 */ NULL,
	/* 0x67 103 */ NULL,
	/* 0x68 104 */ NULL,
	/* 0x69 105 */ NULL,
	/* 0x6A 106 */ NULL,
	/* 0x6B 107 */ NULL,
	/* 0x6C 108 */ NULL,
	/* 0x6D 109 */ NULL,
	/* 0x6E 110 */ NULL,
	/* 0x6F 111 */ NULL,
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
	/* 0x80 128 */ Opcode_Snd0_Unknown_128,
	/* 0x81 129 */ Opcode_Snd0_Unknown_129,
	/* 0x82 130 */ NULL,
	/* 0x83 131 */ NULL,
	/* 0x84 132 */ Opcode_Snd0_Unknown_132,
	/* 0x85 133 */ Opcode_Snd0_Unknown_133,
	/* 0x86 134 */ Opcode_Snd0_Unknown_134,
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
	/* 0xB0 176 */ NULL,
	/* 0xB1 177 */ NULL,
	/* 0xB2 178 */ NULL,
	/* 0xB3 179 */ NULL,
	/* 0xB4 180 */ NULL,
	/* 0xB5 181 */ NULL,
	/* 0xB6 182 */ NULL,
	/* 0xB7 183 */ NULL,
	/* 0xB8 184 */ NULL,
	/* 0xB9 185 */ NULL,
	/* 0xBA 186 */ NULL,
	/* 0xBB 187 */ NULL,
	/* 0xBC 188 */ NULL,
	/* 0xBD 189 */ NULL,
	/* 0xBE 190 */ NULL,
	/* 0xBF 191 */ NULL,
	/* 0xC0 192 */ Opcode_Snd0_PlaySound,
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
	/* 0xD0 208 */ NULL,
	/* 0xD1 209 */ NULL,
	/* 0xD2 210 */ NULL,
	/* 0xD3 211 */ NULL,
	/* 0xD4 212 */ NULL,
	/* 0xD5 213 */ NULL,
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
	/* 0xE0 224 */ NULL,
	/* 0xE1 225 */ NULL,
	/* 0xE2 226 */ NULL,
	/* 0xE3 227 */ NULL,
	/* 0xE4 228 */ NULL,
	/* 0xE5 229 */ NULL,
	/* 0xE6 230 */ NULL,
	/* 0xE7 231 */ NULL,
	/* 0xE8 232 */ NULL,
	/* 0xE9 233 */ NULL,
	/* 0xEA 234 */ NULL,
	/* 0xEB 235 */ NULL,
	/* 0xEC 236 */ NULL,
	/* 0xED 237 */ NULL,
	/* 0xEE 238 */ NULL,
	/* 0xEF 239 */ NULL,
	/* 0xF0 240 */ NULL,
	/* 0xF1 241 */ NULL,
	/* 0xF2 242 */ NULL,
	/* 0xF3 243 */ NULL,
	/* 0xF4 244 */ NULL,
	/* 0xF5 245 */ NULL,
	/* 0xF6 246 */ NULL,
	/* 0xF7 247 */ NULL,
	/* 0xF8 248 */ NULL,
	/* 0xF9 249 */ NULL,
	/* 0xFA 250 */ NULL,
	/* 0xFB 251 */ NULL,
	/* 0xFC 252 */ NULL,
	/* 0xFD 253 */ NULL,
	/* 0xFE 254 */ NULL,
	/* 0xFF 255 */ NULL,
};

/* ------------------------------------------------------------------------- */
/* The engine's side of the sound library                                    */
/* ------------------------------------------------------------------------- */

/*
 * The master volumes the scripts set are remembered here as well as handed to
 * the library: 16 music words at 0x0055FF18 and 64 SE words at 0x0055EE18
 * (0x00493CF0 / 0x00493D20). While the flag at 0x00566994 is set they are only
 * remembered; 0x004945F0 sets it and turns every library master to 0, and
 * 0x00494640 clears it and hands the remembered words back (0x00493D50). Both
 * are called from 0x0049905F, the window's activation handling.
 */
static uint32_t gMusicMasterTable[AUDIO_MUSIC_CHANNELS];
static uint32_t gSEMasterTable[AUDIO_SE_CHANNELS];
static int      gSoundSuspended = 0;

void Snd0_Suspend(void)
{
	/* 0x004945F0 */
	if(gSoundSuspended)
		return;
	gSoundSuspended = 1;
	Audio_Init();
	for(uint32_t i = 0; i < AUDIO_MUSIC_CHANNELS; i++)
		Audio_MusicSetMaster(i, 0);
	for(uint32_t i = 0; i < AUDIO_SE_CHANNELS; i++)
		Audio_SESetMaster(i, 0);
}

void Snd0_Resume(void)
{
	/* 0x00494640 -> 0x00493D50 */
	if(!gSoundSuspended)
		return;
	Audio_Init();
	for(uint32_t i = 0; i < AUDIO_MUSIC_CHANNELS; i++)
		Audio_MusicSetMaster(i, gMusicMasterTable[i]);
	for(uint32_t i = 0; i < AUDIO_SE_CHANNELS; i++)
		Audio_SESetMaster(i, gSEMasterTable[i]);
	gSoundSuspended = 0;
}

/*
 * The four range checks every handler makes before it does anything. Each
 * formats its message and hands it to 0x00464870, which does not come back:
 *
 *   0x00497AE0  SE number below 0x40     無効な効果音番号 [ %d ] が指定されました
 *   0x00497B30  pan 0..0x80 (signed)     無効なパンポット（定位） [ %d ] が指定されました
 *   0x00497B80  volume 0..0x80 (signed)  無効なボリューム（音量） [ %d ] が指定されました
 *   0x00497BD0  music channel below 0x10 無効な音楽チャンネル番号 [ %d ] が指定されました
 */
static int Snd0_CheckSE(Thread_t* thread, uint32_t se)
{
	if(se < AUDIO_SE_CHANNELS)
		return 1;
	printf("[Thread %d]: %sError: an invalid sound effect number [ %d ] was specified\n",
	       thread->threadId, TLevel[thread->level], (int32_t)se);
	return 0;
}

static int Snd0_CheckPan(Thread_t* thread, uint32_t pan)
{
	if((int32_t)pan >= 0 && (int32_t)pan <= 0x80)
		return 1;
	printf("[Thread %d]: %sError: an invalid pan position [ %d ] was specified\n",
	       thread->threadId, TLevel[thread->level], (int32_t)pan);
	return 0;
}

static int Snd0_CheckVolume(Thread_t* thread, uint32_t volume)
{
	if((int32_t)volume >= 0 && (int32_t)volume <= 0x80)
		return 1;
	printf("[Thread %d]: %sError: an invalid volume [ %d ] was specified\n",
	       thread->threadId, TLevel[thread->level], (int32_t)volume);
	return 0;
}

static int Snd0_CheckMusic(Thread_t* thread, uint32_t ch)
{
	if(ch < AUDIO_MUSIC_CHANNELS)
		return 1;
	printf("[Thread %d]: %sError: an invalid music channel number [ %d ] was specified\n",
	       thread->threadId, TLevel[thread->level], (int32_t)ch);
	return 0;
}

/*
 * A script string. 0x0048E0E0 answers NULL for the address 0 (0x0048DF7A),
 * which is how the loaders are told there is no archive; any other address
 * resolves as usual.
 */
static const char* Snd0_PopString(Thread_t* thread, int* ok)
{
	uint32_t address = Thread_PopStack(thread);
	if(address == 0)
		return NULL;
	const char* s = (const char*)Thread_ResolveAddr(thread, address);
	if(s == NULL)
		*ok = 0;
	return s;
}


/*
 * The candidate directories 0x00493E40 and 0x00494100 try in turn while the
 * answer is "no file": the game directory, then - when the flag at 0x00506BE0
 * is set - each directory on the list at 0x00566630, as "%s%s\%s" (0x004E6E4C).
 * The alternate directory at 0x00517C08 that 0x00493D90 and the retry loops
 * fall back to is empty in this engine (nothing sets it), so 0x00464D00 refuses
 * it and those fall-backs never run.
 */
static int Snd0_Candidate(int index, char* out, size_t outSize, const char* name)
{
	if(index == 0)
	{
		snprintf(out, outSize, "%s", name);
		return 1;
	}
	if(!gEnableSearchPaths)
		return 0;
	SearchPathNode_t* node = gSearchPaths;
	for(int i = 1; node != NULL && i < index; i++)
		node = node->next;
	if(node == NULL)
		return 0;
	snprintf(out, outSize, "%s\\%s", node->path, name);
	return 1;
}

/* 0x004A4920 / 0x004A4120: a loose music file into a channel, 0x0C when absent. */
static uint32_t Snd0_LoadMusicLoose(uint32_t ch, const char* path, uint32_t volume, uint32_t pan)
{
	size_t size = 0;
	uint8_t* data = Engine_ReadLooseFile(path, &size);
	if(data == NULL)
		return AUDIO_NO_FILE;
	return Audio_MusicLoad(ch, data, size, volume, pan, 0);
}

/* 0x004A4A70 / 0x004A43A0: the same two names are one stream (flags 1, or 3
   with the loop flag); two names are an intro and a loop part (0x004A39A0). */
static uint32_t Snd0_LoadMusicPair(uint32_t ch, uint8_t* a, size_t aSize, uint8_t* b, size_t bSize,
                                   int same, uint32_t loopFlag, uint32_t volume, uint32_t pan)
{
	if(same)
	{
		free(b);
		return Audio_MusicLoad(ch, a, aSize, volume, pan, loopFlag ? 3 : 1);
	}
	return Audio_MusicLoadPair(ch, a, aSize, b, bSize, loopFlag, volume, pan);
}

static uint32_t Snd0_LoadMusicPairLoose(uint32_t ch, const char* pathA, const char* pathB,
                                        uint32_t loopFlag, uint32_t volume, uint32_t pan)
{
	size_t aSize = 0, bSize = 0;
	int same = strcmp(pathA, pathB) == 0;
	uint8_t* a = Engine_ReadLooseFile(pathA, &aSize);
	if(a == NULL)
		return AUDIO_NO_FILE;
	uint8_t* b = NULL;
	if(!same)
	{
		b = Engine_ReadLooseFile(pathB, &bSize);
		if(b == NULL)
		{
			free(a);
			return AUDIO_NO_FILE;
		}
	}
	return Snd0_LoadMusicPair(ch, a, aSize, b, bSize, same, loopFlag, volume, pan);
}

/* The archive route: 0x004069D0 opens the archive the script named and
   0x004A4F40 the member in it. */
static uint8_t* Snd0_ReadMember(Thread_t* thread, const char* archive, const char* member, size_t* size)
{
	return Engine_ReadFile(thread->engine, archive, member, size);
}

/* ------------------------------------------------------------------------- */
/* Opcodes                                                                   */
/* ------------------------------------------------------------------------- */

/* Snd0 0x00 (0x00487190) pushes 0x14 and nothing else. */
uint32_t Opcode_Snd0_Constant(Thread_t* thread)
{
	Thread_PushStack(thread, 0x14);
	return 0;
}

/*
 * Snd0 0x08 (0x004871B0): pops the volume, then the music channel; checks them
 * (0x00497B80, 0x00497BD0) and hands them to 0x00493CF0, which remembers the
 * value and, unless sound is suspended, sets the channel's master volume
 * (0x004A3400 -> 0x004A2CE0, stored unclamped). Pushes nothing.
 */
uint32_t Opcode_Snd0_SetMusicMasterVolume(Thread_t* thread)
{
	uint32_t volume = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	gMusicMasterTable[ch] = volume;
	if(!gSoundSuspended)
		Audio_MusicSetMaster(ch, volume);
	return 0;
}

/* Snd0 0x09 (0x004871F0): the same for an SE: 0x00493D20 -> 0x004A2F80 ->
   0x004A29B0, which clamps to 0x80. */
uint32_t Opcode_Snd0_SetSEMasterVolume(Thread_t* thread)
{
	uint32_t volume = Thread_PopStack(thread);
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Audio_Init();
	gSEMasterTable[se] = volume;
	if(!gSoundSuspended)
		Audio_SESetMaster(se, volume);
	return 0;
}

/*
 * Snd0 0x10 (0x00487230): pops the volume, the file name and the music channel.
 * 0x00493D90 stops the channel (0x004A35B0), then loads the file from the game
 * directory with the pan centred (0x40) - 0x004A4920 -> 0x004A4120 -> 0x004A3700,
 * which leaves it loaded and stopped. "No file" (0x0C) and "not a BW file"
 * (0x0E) are fatal here, the rest is discarded. Pushes nothing.
 */
uint32_t Opcode_Snd0_LoadMusicFile(Thread_t* thread)
{
	int ok = 1;
	uint32_t volume = Thread_PopStack(thread);
	const char* name = Snd0_PopString(thread, &ok);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	if(!ok || name == NULL)
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicStop(ch);
	uint32_t r = Snd0_LoadMusicLoose(ch, name, volume, 0x40);
	if(r == AUDIO_NO_FILE)
	{
		/* 0x004EB6D8 指定されたBWファイル [ %s ] は存在しません */
		printf("[Thread %d]: %sError: the specified BW file [ %s ] does not exist\n",
		       thread->threadId, TLevel[thread->level], name);
		return 0xFFFFFFFF;
	}
	if(r == AUDIO_NOT_BW)
	{
		/* 0x004EB704 指定されたファイル [ %s ] はBWファイルではないようです */
		printf("[Thread %d]: %sError: the specified file [ %s ] does not seem to be a BW file\n",
		       thread->threadId, TLevel[thread->level], name);
		return 0xFFFFFFFF;
	}
	if(r == AUDIO_REFUSED)
		return 0xFFFFFFFF;
	printf("[Thread %d]: %sMusic channel %u loaded from \"%s\" (0x%X)\n",
	       thread->threadId, TLevel[thread->level], ch, name, r);
	return 0;
}

/*
 * Snd0 0x11 (0x00487300): pops the pan, the volume, the member name, the
 * archive name (0 for none) and the music channel. 0x00493F40 stops the
 * channel, tries the member as a loose file in each candidate directory
 * (0x00493E40), and, if none holds it, reads it from the archive
 * (0x004069D0, 0x004A49C0 -> 0x004A4260). A file found nowhere is fatal inside
 * the loader (0x00465BA0 -> 0x004646A0, as no alternate directory is set);
 * back in the handler 0x0E is fatal too, other answers are discarded.
 */
uint32_t Opcode_Snd0_LoadMusic(Thread_t* thread)
{
	int ok = 1;
	uint32_t pan = Thread_PopStack(thread);
	uint32_t volume = Thread_PopStack(thread);
	const char* member = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckPan(thread, pan) || !Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	if(!ok || member == NULL)
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicStop(ch);

	uint32_t r = AUDIO_NO_FILE;
	char path[1024];
	for(int i = 0; r == AUDIO_NO_FILE && Snd0_Candidate(i, path, sizeof(path), member); i++)
		r = Snd0_LoadMusicLoose(ch, path, volume, pan);
	if(r == AUDIO_NO_FILE)
	{
		if(archive == NULL)
		{
			/* 0x004E6E54 指定されたファイル [ %s ] は存在しません */
			printf("[Thread %d]: %sError: the specified file [ %s ] does not exist\n",
			       thread->threadId, TLevel[thread->level], member);
			return 0xFFFFFFFF;
		}
		size_t size = 0;
		uint8_t* data = Snd0_ReadMember(thread, archive, member, &size);
		if(data == NULL)
		{
			/* 0x004E6E80 指定されたファイル [ %s : %s ] は存在しません */
			printf("[Thread %d]: %sError: the specified file [ %s : %s ] does not exist\n",
			       thread->threadId, TLevel[thread->level], archive, member);
			return 0xFFFFFFFF;
		}
		r = Audio_MusicLoad(ch, data, size, volume, pan, 0);
	}
	if(r == AUDIO_NOT_BW)
	{
		/* 0x004E57C4 指定されたファイル [ %s : %s ] はBWファイルではないようです */
		printf("[Thread %d]: %sError: the specified file [ %s : %s ] does not seem to be a BW file\n",
		       thread->threadId, TLevel[thread->level], archive ? archive : "", member);
		return 0xFFFFFFFF;
	}
	if(r == AUDIO_REFUSED)
		return 0xFFFFFFFF;
	printf("[Thread %d]: %sMusic channel %u loaded from [ %s : %s ] (0x%X)\n",
	       thread->threadId, TLevel[thread->level], ch, archive ? archive : "", member, r);
	return 0;
}

/*
 * Snd0 0x12 (0x00487400): pops the pan, the volume, the loop flag, the loop
 * part's name, the first part's name, the archive name (0 for none) and the
 * music channel. 0x00494260 works as 0x11 does with two names: the same name
 * twice is one stream whose header loop is overridden by the flag (0x004A86B0
 * with flags 3 or 1), two names are an intro and a loop part (0x004A39A0).
 */
uint32_t Opcode_Snd0_LoadMusicLoop(Thread_t* thread)
{
	int ok = 1;
	uint32_t pan = Thread_PopStack(thread);
	uint32_t volume = Thread_PopStack(thread);
	uint32_t loopFlag = Thread_PopStack(thread);
	const char* second = Snd0_PopString(thread, &ok);
	const char* first = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckPan(thread, pan) || !Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	if(!ok || first == NULL || second == NULL)
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicStop(ch);

	uint32_t r = AUDIO_NO_FILE;
	char pathA[1024], pathB[1024];
	for(int i = 0; r == AUDIO_NO_FILE && Snd0_Candidate(i, pathA, sizeof(pathA), first); i++)
	{
		Snd0_Candidate(i, pathB, sizeof(pathB), second);
		r = Snd0_LoadMusicPairLoose(ch, pathA, pathB, loopFlag, volume, pan);
	}
	if(r == AUDIO_NO_FILE)
	{
		if(archive == NULL)
		{
			/* 0x004EC24C 指定されたファイル [ %s / %s ] は存在しません */
			printf("[Thread %d]: %sError: the specified file [ %s / %s ] does not exist\n",
			       thread->threadId, TLevel[thread->level], first, second);
			return 0xFFFFFFFF;
		}
		int same = strcmp(first, second) == 0;
		size_t aSize = 0, bSize = 0;
		uint8_t* a = Snd0_ReadMember(thread, archive, first, &aSize);
		uint8_t* b = (a != NULL && !same) ? Snd0_ReadMember(thread, archive, second, &bSize) : NULL;
		if(a == NULL || (!same && b == NULL))
		{
			free(a);
			free(b);
			/* 0x004EC27C 指定されたファイル [ %s : %s / %s ] は存在しません */
			printf("[Thread %d]: %sError: the specified file [ %s : %s / %s ] does not exist\n",
			       thread->threadId, TLevel[thread->level], archive, first, second);
			return 0xFFFFFFFF;
		}
		r = Snd0_LoadMusicPair(ch, a, aSize, b, bSize, same, loopFlag, volume, pan);
	}
	if(r == AUDIO_NOT_BW)
	{
		/* 0x004EB7A8 指定されたファイル [ %s : %s / %s ] はBWファイルではないようです */
		printf("[Thread %d]: %sError: the specified file [ %s : %s / %s ] does not seem to be a BW file\n",
		       thread->threadId, TLevel[thread->level], archive ? archive : "", first, second);
		return 0xFFFFFFFF;
	}
	if(r == AUDIO_REFUSED)
		return 0xFFFFFFFF;
	printf("[Thread %d]: %sMusic channel %u loaded from [ %s : %s / %s ], loop %u (0x%X)\n",
	       thread->threadId, TLevel[thread->level], ch, archive ? archive : "", first, second, loopFlag, r);
	return 0;
}

/*
 * Snd0 0x14 (0x00487540): pops a flag, then the music channel. 0x004A32E0 ->
 * 0x004A2C40 hands the player's vtable +0x0C whether the flag is zero: a flag
 * of 1 plays (or resumes where it paused), 0 pauses. Pushes nothing.
 */
uint32_t Opcode_Snd0_PlayMusic(Thread_t* thread)
{
	uint32_t play = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicPlay(ch, play == 0);
	return 0;
}

/*
 * Snd0 0x15 (0x00487580): pops an address, then the music channel. 0x00494440
 * pushes whether the channel is playing (0x004A26F0; 0 when it is not loaded)
 * and, when the address is not 0, stores there how many times the loop part of
 * an intro-and-loop pair has come round (0x004A2730 -> 0x004A8670).
 */
uint32_t Opcode_Snd0_GetMusicStatus(Thread_t* thread)
{
	uint32_t address = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	uint32_t playing = 0;
	Audio_MusicStatus(ch, &playing);
	if(address != 0)
	{
		uint8_t* p = Thread_ResolveAddr(thread, address);
		uint32_t count = 0;
		if(p == NULL)
			return 0xFFFFFFFF;
		if(Audio_MusicLoopCount(ch, &count) == AUDIO_OK)
		{
			p[0] = (uint8_t)count;
			p[1] = (uint8_t)(count >> 8);
			p[2] = (uint8_t)(count >> 16);
			p[3] = (uint8_t)(count >> 24);
		}
	}
	Thread_PushStack(thread, playing);
	return 0;
}

/* Snd0 0x16 (0x004875D0): pops the time in milliseconds, the volume and the
   music channel; 0x004A3370 -> 0x004A2C70 fades the channel's first fade from
   where it is to the volume over the time. */
uint32_t Opcode_Snd0_FadeMusicVolume(Thread_t* thread)
{
	uint32_t time = Thread_PopStack(thread);
	uint32_t volume = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicFade(ch, volume, time);
	return 0;
}

/* Snd0 0x17 (0x00487620): pops the pan and the music channel; 0x004A3520 ->
   0x004A2DB0. */
uint32_t Opcode_Snd0_SetMusicPan(Thread_t* thread)
{
	uint32_t pan = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckPan(thread, pan) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicSetPan(ch, (int32_t)pan);
	return 0;
}

/* Snd0 0x18 (0x00487660): pops the time and the music channel; the second fade
   goes to full over the time (0x004A31C0 -> 0x004A2B80). */
uint32_t Opcode_Snd0_FadeInMusic(Thread_t* thread)
{
	uint32_t time = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicFadeIn(ch, time);
	return 0;
}

/* Snd0 0x19 (0x004876A0): the second fade goes to 0 (0x004A3130 -> 0x004A2B20).
   The channel keeps playing, silent. */
uint32_t Opcode_Snd0_FadeOutMusic(Thread_t* thread)
{
	uint32_t time = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicFadeOut(ch, time);
	return 0;
}

/* Snd0 0x1C (0x004876E0): pops the volume and the music channel; 0x004A3490 ->
   0x004A2D40 sets the volume word, clamped to 0x80. */
uint32_t Opcode_Snd0_SetMusicVolume(Thread_t* thread)
{
	uint32_t volume = Thread_PopStack(thread);
	uint32_t ch = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckMusic(thread, ch))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_MusicSetVolume(ch, volume);
	return 0;
}

/*
 * SE registration, shared by 0x20, 0x21, 0x23 and 0x27. Each pops what it has,
 * checks the SE number (0x00497AE0) and joins a 0x670-byte loader process
 * (0x00439FD0, vtable 0x004E58A4) to the thread, returning 2; the process
 * pushes nothing. The loader reads [ archive : file ] like every loader
 * (0x00439BC0: +0x28 the archive, +0x334 the file) and its Run (0x0043A0E0)
 * queues 0x00498290, whose worker (0x00498330) copies the member's 0x40-byte
 * header into the SE's record, puts 65536 / speed in the record's +0x3C
 * (0x00494490) and registers it (0x004A4880). The worker's answer is mapped
 * (0x00498468): 0 and 0x14 are success, 0x0E is 0x80000001, 0x12 0x80000002,
 * anything else 0x8FFFFFFF, and the Run makes each of those three fatal:
 *
 *   0x004E57C4  指定されたファイル [ %s : %s ] はBWファイルではないようです
 *   0x004E5800  指定されたBWファイル [ %s : %s ] はモノラルではないので効果音として使用できません
 *   0x004E5858  指定されたファイル [ %s : %s ] の登録中に致命的なエラーが発生しました
 *
 * Here the read and the checks happen at once, so those errors are reported by
 * the opcode; only the decode runs while the thread waits.
 */
typedef struct Snd0Registration
{
	AudioSEJob_t* job;
	uint32_t      se;
} Snd0Registration_t;

static int Snd0_RegistrationRun(void* context)
{
	Snd0Registration_t* reg = (Snd0Registration_t*)context;
	uint32_t result = 0;
	if(!Audio_SEPoll(reg->job, &result))
		return 0;
	reg->job = NULL;
	return 1;
}

static void Snd0_RegistrationFree(void* context)
{
	Snd0Registration_t* reg = (Snd0Registration_t*)context;
	if(reg->job != NULL)
		Audio_SEFinish(reg->job);
	free(reg);
}

static uint32_t Snd0_RegisterSE(Thread_t* thread, uint32_t se, const char* archive, const char* file,
                                uint32_t rampMs, double gain, double speed)
{
	if(!Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	if(file == NULL)
		return 0xFFFFFFFF;
	Audio_Init();
	const char* arc = archive != NULL ? archive : "";
	size_t size = 0;
	uint8_t* data = Engine_ReadFile(thread->engine, arc, file, &size);
	if(data == NULL)
	{
		printf("[Thread %d]: %sError: the SE file [ %s : %s ] could not be read; what the loader "
		       "(0x00439A70) does with a missing file is not traced, so it is refused\n",
		       thread->threadId, TLevel[thread->level], arc, file);
		return 0xFFFFFFFF;
	}

	/* 0x00494490: the record is the member's header, with the speed at +0x3C. */
	memset(gSoundChannels[se], 0, sizeof(gSoundChannels[se]));
	for(int i = 0; i < SND0_CHANNEL_RECORD_WORDS && (size_t)(i * 4 + 4) <= size; i++)
		gSoundChannels[se][i] = (uint32_t)data[i * 4] | ((uint32_t)data[i * 4 + 1] << 8)
		                      | ((uint32_t)data[i * 4 + 2] << 16) | ((uint32_t)data[i * 4 + 3] << 24);
	gSoundChannels[se][15] = (uint32_t)(int64_t)(65536.0 / speed);

	uint32_t result = AUDIO_OK;
	AudioSEJob_t* job = Audio_SEBegin(se, data, size, rampMs, gain, speed, &result);
	if(job == NULL)
	{
		if(result == AUDIO_OK || result == AUDIO_NO_DEVICE)
			return 0;
		if(result == AUDIO_REFUSED)
			return 0xFFFFFFFF;
		if(result == AUDIO_NOT_BW)
			printf("[Thread %d]: %sError: the specified file [ %s : %s ] does not seem to be a BW file\n",
			       thread->threadId, TLevel[thread->level], arc, file);
		else if(result == AUDIO_NOT_MONO)
			printf("[Thread %d]: %sError: the specified BW file [ %s : %s ] is not mono and cannot be used as a sound effect\n",
			       thread->threadId, TLevel[thread->level], arc, file);
		else
			printf("[Thread %d]: %sError: a fatal error occurred while registering the specified file [ %s : %s ]\n",
			       thread->threadId, TLevel[thread->level], arc, file);
		return 0xFFFFFFFF;
	}
	printf("[Thread %d]: %sRegistering SE %u from [ %s : %s ] (fade-in %u ms, gain %.4f)\n",
	       thread->threadId, TLevel[thread->level], se, arc, file, rampMs, gain);

	Snd0Registration_t* reg = (Snd0Registration_t*)malloc(sizeof(Snd0Registration_t));
	if(reg != NULL)
	{
		reg->job = job;
		reg->se = se;
		Process_t* process = Process_CreateCallback(thread, Snd0_RegistrationRun, Snd0_RegistrationFree, reg);
		if(process != NULL)
		{
			Thread_SetProcess(thread, process);
			return 2;
		}
		free(reg);
	}
	/* Out of memory for the process: the decode is waited for here. */
	Audio_SEFinish(job);
	return 0;
}

/* Snd0 0x20 (0x00487720): pops the file, the archive and the SE number;
   no fade-in, gain 1.0, speed 1.0. */
uint32_t Opcode_Snd0_RegisterSE(Thread_t* thread)
{
	int ok = 1;
	const char* file = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t se = Thread_PopStack(thread);
	if(!ok)
		return 0xFFFFFFFF;
	return Snd0_RegisterSE(thread, se, archive, file, 0, 1.0, 1.0);
}

/* Snd0 0x21 (0x004877D0): pops the gain as 16.16 (times the double 1/65536 at
   0x004EC930), the fade-in in milliseconds, the file, the archive and the SE
   number; speed 1.0. */
uint32_t Opcode_Snd0_RegisterSEEx(Thread_t* thread)
{
	int ok = 1;
	double gain = (double)(int32_t)Thread_PopStack(thread) * (1.0 / 65536.0);
	uint32_t rampMs = Thread_PopStack(thread);
	const char* file = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t se = Thread_PopStack(thread);
	if(!ok)
		return 0xFFFFFFFF;
	return Snd0_RegisterSE(thread, se, archive, file, rampMs, gain, 1.0);
}

/* Snd0 0x23 (0x004878F0): 0x21 at speed 2.0 (the double at 0x004EC8B8). */
uint32_t Opcode_Snd0_RegisterSEDouble(Thread_t* thread)
{
	int ok = 1;
	double gain = (double)(int32_t)Thread_PopStack(thread) * (1.0 / 65536.0);
	uint32_t rampMs = Thread_PopStack(thread);
	const char* file = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t se = Thread_PopStack(thread);
	if(!ok)
		return 0xFFFFFFFF;
	return Snd0_RegisterSE(thread, se, archive, file, rampMs, gain, 2.0);
}

/* Snd0 0x27 (0x00487AE0): pops the speed and the gain, both 16.16, then the
   fade-in, the file, the archive and the SE number. */
uint32_t Opcode_Snd0_RegisterSESpeed(Thread_t* thread)
{
	int ok = 1;
	double speed = (double)(int32_t)Thread_PopStack(thread) * (1.0 / 65536.0);
	double gain = (double)(int32_t)Thread_PopStack(thread) * (1.0 / 65536.0);
	uint32_t rampMs = Thread_PopStack(thread);
	const char* file = Snd0_PopString(thread, &ok);
	const char* archive = Snd0_PopString(thread, &ok);
	uint32_t se = Thread_PopStack(thread);
	if(!ok)
		return 0xFFFFFFFF;
	return Snd0_RegisterSE(thread, se, archive, file, rampMs, gain, speed);
}

/*
 * Snd0 0x22 (0x004878C0 -> 0x00494510): pops the SE number, clears its 0x40-byte
 * record and unloads the channel (0x004A28A0 -> 0x004A2170). Pushes nothing.
 */
uint32_t Opcode_Snd0_ResetSE(Thread_t* thread)
{
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	memset(gSoundChannels[se], 0, sizeof(gSoundChannels[se]));
	Audio_Init();
	Audio_SEUnload(se);
	return 0;
}

/*
 * 0x00494570: an SE's length in milliseconds, from its record: frames * 1000 /
 * rate, times the 16.16 speed word, the 64-bit result's low word shifted right
 * 16; 0 when the rate is 0.
 */
static uint32_t Snd0_SEDuration(uint32_t se)
{
	uint32_t rate = gSoundChannels[se][4];
	if(rate == 0)
		return 0;
	double ms = (double)gSoundChannels[se][3] * 1000.0 / (double)rate * (double)gSoundChannels[se][15];
	return (uint32_t)(int64_t)ms >> 16;
}

/*
 * Snd0 0x24 (0x004879E0): pops the pan, the volume and the SE number, plays it
 * (0x00494530 -> 0x004A3F40 -> 0x004A3670) and pushes its length in
 * milliseconds when that answered 0 or 0x14, and 0 otherwise.
 */
uint32_t Opcode_Snd0_PlaySE(Thread_t* thread)
{
	uint32_t pan = Thread_PopStack(thread);
	uint32_t volume = Thread_PopStack(thread);
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckPan(thread, pan) || !Snd0_CheckVolume(thread, volume) || !Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Audio_Init();
	uint32_t r = Audio_SEPlay(se, volume, pan);
	Thread_PushStack(thread, (r == AUDIO_OK || r == AUDIO_NO_DEVICE) ? Snd0_SEDuration(se) : 0);
	return 0;
}

/* Snd0 0x25 (0x00487A70): pops the SE number and stops it (0x00494550 ->
   0x004A30A0 -> 0x004A2AF0). */
uint32_t Opcode_Snd0_StopSE(Thread_t* thread)
{
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_SEStop(se);
	return 0;
}

/* Snd0 0x26 (0x00487AA0): pops the time and the SE number; the second fade
   goes to 0 over the time (0x00494560 -> 0x004A3250 -> 0x004A2BE0). */
uint32_t Opcode_Snd0_FadeOutSE(Thread_t* thread)
{
	uint32_t time = Thread_PopStack(thread);
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_SEFadeOut(se, time);
	return 0;
}

/* Snd0 0x28 (0x00487BF0) joins a 0x48-byte process made by 0x00452610 with a
   name, an SE number and two 16.16 values. Neither is read; no script uses it. */
uint32_t Opcode_Snd0_Unknown_40(Thread_t* thread)
{
	printf("[Thread %d]: %sError: Snd0 0x28 (0x00487BF0, the process at 0x00452610) is not written\n",
	       thread->threadId, TLevel[thread->level]);
	return 0xFFFFFFFF;
}

/* Snd0 0x2C (0x00487CE0): pops the volume and the SE number; 0x004A3010 ->
   0x004A2A20 sets the volume word, clamped to 0x80. */
uint32_t Opcode_Snd0_SetSEVolume(Thread_t* thread)
{
	uint32_t volume = Thread_PopStack(thread);
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckVolume(thread, volume) || !Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Audio_Init();
	Audio_SESetVolume(se, volume);
	return 0;
}

/* Snd0 0x2F (0x00487D20): pops the SE number and pushes its length
   (0x00494570). */
uint32_t Opcode_Snd0_GetSEDuration(Thread_t* thread)
{
	uint32_t se = Thread_PopStack(thread);
	if(!Snd0_CheckSE(thread, se))
		return 0xFFFFFFFF;
	Thread_PushStack(thread, Snd0_SEDuration(se));
	return 0;
}

/* Snd0 0x80, 0x81, 0x84, 0x85 and 0x86 call 0x0048D7D0, 0x0048D950, 0x0048D9E0,
   0x0048DA80 and 0x0048DAA0. None of those is read and no script uses them. */
static uint32_t Snd0_Unread(Thread_t* thread, uint32_t op, uint32_t target)
{
	printf("[Thread %d]: %sError: Snd0 0x%02X (-> 0x%08X) is not written\n",
	       thread->threadId, TLevel[thread->level], op, target);
	return 0xFFFFFFFF;
}

uint32_t Opcode_Snd0_Unknown_128(Thread_t* thread) { return Snd0_Unread(thread, 0x80, 0x0048D7D0); }
uint32_t Opcode_Snd0_Unknown_129(Thread_t* thread) { return Snd0_Unread(thread, 0x81, 0x0048D950); }
uint32_t Opcode_Snd0_Unknown_132(Thread_t* thread) { return Snd0_Unread(thread, 0x84, 0x0048D9E0); }
uint32_t Opcode_Snd0_Unknown_133(Thread_t* thread) { return Snd0_Unread(thread, 0x85, 0x0048DA80); }
uint32_t Opcode_Snd0_Unknown_134(Thread_t* thread) { return Snd0_Unread(thread, 0x86, 0x0048DAA0); }

uint32_t Opcode_Snd0_PlaySound(Thread_t* thread)
{
	char* path = (char*)Thread_PopAndResolveAddress(thread);
	uint32_t res = Engine_PlaySound(path);
	Thread_PushStack(thread, res);
	return 0;
}
