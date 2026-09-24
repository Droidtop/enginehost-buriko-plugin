#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "icon.h"
#include "opcodes.h"
#include "nametable.h"
#include "object.h"
#include "screen.h"
#include "opcodes_grp1.h"
#include "font.h"
#include "text.h"
#include "renderer.h"
#include "thread.h"

char* OpcodesGrp1Mnemonics[256] = {
    /* 0x00   0 */ "--Unknown--",
    /* 0x01   1 */ "--Unknown--",
    /* 0x02   2 */ "--Unknown--",
    /* 0x03   3 */ "--Unknown--",
    /* 0x04   4 */ "--Unknown--",
    /* 0x05   5 */ "--Unknown--",
    /* 0x06   6 */ "--Unknown--",
    /* 0x07   7 */ "--Unknown--",
    /* 0x08   8 */ "--Unknown--",
    /* 0x09   9 */ "--Unknown--",
    /* 0x0A  10 */ "--Unknown--",
    /* 0x0B  11 */ "--Unknown--",
    /* 0x0C  12 */ "SetCoverageCurve",
    /* 0x0D  13 */ "SetFontPitchCheck",
    /* 0x0E  14 */ "SetFontAdjust",
    /* 0x0F  15 */ "--Unknown--",
    /* 0x10  16 */ "Unknown_16",
    /* 0x11  17 */ "Unknown_17",
    /* 0x12  18 */ "Unknown_18",
    /* 0x13  19 */ "Unknown_19",
    /* 0x14  20 */ "Unknown_20",
    /* 0x15  21 */ "Unknown_21",
    /* 0x16  22 */ "--Unknown--",
    /* 0x17  23 */ "--Unknown--",
    /* 0x18  24 */ "Unknown_24",
    /* 0x19  25 */ "Unknown_25",
    /* 0x1A  26 */ "--Unknown--",
    /* 0x1B  27 */ "--Unknown--",
    /* 0x1C  28 */ "ScaleBitmap",
    /* 0x1D  29 */ "--Unknown--",
    /* 0x1E  30 */ "Unknown_30",
    /* 0x1F  31 */ "DuplicateBitmap",
    /* 0x20  32 */ "--Unknown--",
    /* 0x21  33 */ "--Unknown--",
    /* 0x22  34 */ "--Unknown--",
    /* 0x23  35 */ "--Unknown--",
    /* 0x24  36 */ "--Unknown--",
    /* 0x25  37 */ "--Unknown--",
    /* 0x26  38 */ "--Unknown--",
    /* 0x27  39 */ "--Unknown--",
    /* 0x28  40 */ "--Unknown--",
    /* 0x29  41 */ "--Unknown--",
    /* 0x2A  42 */ "--Unknown--",
    /* 0x2B  43 */ "--Unknown--",
    /* 0x2C  44 */ "--Unknown--",
    /* 0x2D  45 */ "--Unknown--",
    /* 0x2E  46 */ "--Unknown--",
    /* 0x2F  47 */ "--Unknown--",
    /* 0x30  48 */ "--Unknown--",
    /* 0x31  49 */ "SetObjectHidden",
    /* 0x32  50 */ "--Unknown--",
    /* 0x33  51 */ "Unknown_51",
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
    /* 0x40  64 */ "SetScreenLayered",
    /* 0x41  65 */ "Unknown_65",
    /* 0x42  66 */ "Unknown_66",
    /* 0x43  67 */ "Unknown_67",
    /* 0x44  68 */ "Unknown_68",
    /* 0x45  69 */ "Unknown_69",
    /* 0x46  70 */ "Unknown_70",
    /* 0x47  71 */ "Unknown_71",
    /* 0x48  72 */ "Unknown_72",
    /* 0x49  73 */ "Unknown_73",
    /* 0x4A  74 */ "Unknown_74",
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
    /* 0x60  96 */ "Unknown_96",
    /* 0x61  97 */ "Unknown_97",
    /* 0x62  98 */ "--Unknown--",
    /* 0x63  99 */ "--Unknown--",
    /* 0x64 100 */ "Unknown_100",
    /* 0x65 101 */ "Unknown_101",
    /* 0x66 102 */ "Unknown_102",
    /* 0x67 103 */ "Unknown_103",
    /* 0x68 104 */ "Unknown_104",
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
    /* 0x80 128 */ "--Unknown--",
    /* 0x81 129 */ "--Unknown--",
    /* 0x82 130 */ "--Unknown--",
    /* 0x83 131 */ "--Unknown--",
    /* 0x84 132 */ "--Unknown--",
    /* 0x85 133 */ "--Unknown--",
    /* 0x86 134 */ "--Unknown--",
    /* 0x87 135 */ "--Unknown--",
    /* 0x88 136 */ "SetWindowFont",
    /* 0x89 137 */ "SetWindowGapCoefficient",
    /* 0x8A 138 */ "Unknown_138",
    /* 0x8B 139 */ "SetWindowSwingingStyle",
    /* 0x8C 140 */ "Unknown_140",
    /* 0x8D 141 */ "GetTextCursor",
    /* 0x8E 142 */ "Unknown_142",
    /* 0x8F 143 */ "--Unknown--",
    /* 0x90 144 */ "Unknown_144",
    /* 0x91 145 */ "Unknown_145",
    /* 0x92 146 */ "Unknown_146",
    /* 0x93 147 */ "Unknown_147",
    /* 0x94 148 */ "Unknown_148",
    /* 0x95 149 */ "Unknown_149",
    /* 0x96 150 */ "LoadNameTable",
    /* 0x97 151 */ "--Unknown--",
    /* 0x98 152 */ "SetRubyStyle",
    /* 0x99 153 */ "--Unknown--",
    /* 0x9A 154 */ "SetFunctionParameter",
    /* 0x9B 155 */ "--Unknown--",
    /* 0x9C 156 */ "DrawTextInDefaultStyle",
    /* 0x9D 157 */ "DrawTextInDefaultStyleOrPlain",
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
    /* 0xB8 184 */ "CreateIconEx",
    /* 0xB9 185 */ "--Unknown--",
    /* 0xBA 186 */ "SetIconContent",
    /* 0xBB 187 */ "SetIconPartEnabled",
    /* 0xBC 188 */ "--Unknown--",
    /* 0xBD 189 */ "--Unknown--",
    /* 0xBE 190 */ "--Unknown--",
    /* 0xBF 191 */ "SetIconKeyMap",
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

OpcodePtr_t OpcodesGrp1[256] = {
    /* 0x00   0 */ NULL,
    /* 0x01   1 */ NULL,
    /* 0x02   2 */ NULL,
    /* 0x03   3 */ NULL,
    /* 0x04   4 */ NULL,
    /* 0x05   5 */ NULL,
    /* 0x06   6 */ NULL,
    /* 0x07   7 */ NULL,
    /* 0x08   8 */ NULL,
    /* 0x09   9 */ NULL,
    /* 0x0A  10 */ NULL,
    /* 0x0B  11 */ NULL,
    /* 0x0C  12 */ Opcode_Grp1_SetCoverageCurve,
    /* 0x0D  13 */ Opcode_Grp1_SetFontPitchCheck,
    /* 0x0E  14 */ Opcode_Grp1_SetFontAdjust,
    /* 0x0F  15 */ NULL,
    /* 0x10  16 */ Opcode_Grp1_Unknown_16,
    /* 0x11  17 */ Opcode_Grp1_Unknown_17,
    /* 0x12  18 */ Opcode_Grp1_Unknown_18,
    /* 0x13  19 */ Opcode_Grp1_Unknown_19,
    /* 0x14  20 */ Opcode_Grp1_Unknown_20,
    /* 0x15  21 */ Opcode_Grp1_Unknown_21,
    /* 0x16  22 */ NULL,
    /* 0x17  23 */ NULL,
    /* 0x18  24 */ Opcode_Grp1_Unknown_24,
    /* 0x19  25 */ Opcode_Grp1_Unknown_25,
    /* 0x1A  26 */ NULL,
    /* 0x1B  27 */ NULL,
    /* 0x1C  28 */ Opcode_Grp1_ScaleBitmap,
    /* 0x1D  29 */ NULL,
    /* 0x1E  30 */ Opcode_Grp1_Unknown_30,
    /* 0x1F  31 */ Opcode_Grp1_DuplicateBitmap,
    /* 0x20  32 */ NULL,
    /* 0x21  33 */ NULL,
    /* 0x22  34 */ NULL,
    /* 0x23  35 */ NULL,
    /* 0x24  36 */ NULL,
    /* 0x25  37 */ NULL,
    /* 0x26  38 */ NULL,
    /* 0x27  39 */ NULL,
    /* 0x28  40 */ NULL,
    /* 0x29  41 */ NULL,
    /* 0x2A  42 */ NULL,
    /* 0x2B  43 */ NULL,
    /* 0x2C  44 */ NULL,
    /* 0x2D  45 */ NULL,
    /* 0x2E  46 */ NULL,
    /* 0x2F  47 */ NULL,
    /* 0x30  48 */ NULL,
    /* 0x31  49 */ Opcode_Grp1_SetObjectHidden,
    /* 0x32  50 */ NULL,
    /* 0x33  51 */ Opcode_Grp1_Unknown_51,
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
    /* 0x40  64 */ Opcode_Grp1_SetScreenLayered,
    /* 0x41  65 */ Opcode_Grp1_Unknown_65,
    /* 0x42  66 */ Opcode_Grp1_Unknown_66,
    /* 0x43  67 */ Opcode_Grp1_Unknown_67,
    /* 0x44  68 */ Opcode_Grp1_Unknown_68,
    /* 0x45  69 */ Opcode_Grp1_Unknown_69,
    /* 0x46  70 */ Opcode_Grp1_Unknown_70,
    /* 0x47  71 */ Opcode_Grp1_Unknown_71,
    /* 0x48  72 */ Opcode_Grp1_Unknown_72,
    /* 0x49  73 */ Opcode_Grp1_Unknown_73,
    /* 0x4A  74 */ Opcode_Grp1_Unknown_74,
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
    /* 0x60  96 */ Opcode_Grp1_Unknown_96,
    /* 0x61  97 */ Opcode_Grp1_Unknown_97,
    /* 0x62  98 */ NULL,
    /* 0x63  99 */ NULL,
    /* 0x64 100 */ Opcode_Grp1_Unknown_100,
    /* 0x65 101 */ Opcode_Grp1_Unknown_101,
    /* 0x66 102 */ Opcode_Grp1_Unknown_102,
    /* 0x67 103 */ Opcode_Grp1_Unknown_103,
    /* 0x68 104 */ Opcode_Grp1_Unknown_104,
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
    /* 0x80 128 */ NULL,
    /* 0x81 129 */ NULL,
    /* 0x82 130 */ NULL,
    /* 0x83 131 */ NULL,
    /* 0x84 132 */ NULL,
    /* 0x85 133 */ NULL,
    /* 0x86 134 */ NULL,
    /* 0x87 135 */ NULL,
    /* 0x88 136 */ Opcode_Grp1_SetWindowFont,
    /* 0x89 137 */ Opcode_Grp1_SetWindowGapCoefficient,
    /* 0x8A 138 */ Opcode_Grp1_Unknown_138,
    /* 0x8B 139 */ Opcode_Grp1_SetWindowSwingingStyle,
    /* 0x8C 140 */ Opcode_Grp1_Unknown_140,
    /* 0x8D 141 */ Opcode_Grp1_GetTextCursor,
    /* 0x8E 142 */ Opcode_Grp1_Unknown_142,
    /* 0x8F 143 */ NULL,
    /* 0x90 144 */ Opcode_Grp1_Unknown_144,
    /* 0x91 145 */ Opcode_Grp1_Unknown_145,
    /* 0x92 146 */ Opcode_Grp1_Unknown_146,
    /* 0x93 147 */ Opcode_Grp1_Unknown_147,
    /* 0x94 148 */ Opcode_Grp1_Unknown_148,
    /* 0x95 149 */ Opcode_Grp1_Unknown_149,
    /* 0x96 150 */ Opcode_Grp1_LoadNameTable,
    /* 0x97 151 */ NULL,
    /* 0x98 152 */ Opcode_Grp1_SetRubyStyle,
    /* 0x99 153 */ NULL,
    /* 0x9A 154 */ Opcode_Grp1_SetFunctionParameter,
    /* 0x9B 155 */ NULL,
    /* 0x9C 156 */ Opcode_Grp1_DrawTextInDefaultStyle,
    /* 0x9D 157 */ Opcode_Grp1_DrawTextInDefaultStyleOrPlain,
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
    /* 0xB8 184 */ Opcode_Grp1_CreateIconEx,
    /* 0xB9 185 */ NULL,
    /* 0xBA 186 */ Opcode_Grp1_SetIconContent,
    /* 0xBB 187 */ Opcode_Grp1_SetIconPartEnabled,
    /* 0xBC 188 */ NULL,
    /* 0xBD 189 */ NULL,
    /* 0xBE 190 */ NULL,
    /* 0xBF 191 */ Opcode_Grp1_SetIconKeyMap,
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

uint32_t Opcode_Grp1_SetFunctionParameter(Thread_t* thread)
{
	int32_t value = (int32_t)Thread_PopStack(thread);
	uint32_t function = Thread_PopStack(thread);

	uint32_t result = Engine_SetFunctionParameter(function, value);
	if(result == 0x80000007)
	{
		printf("[Thread %d]: %sError: 0x%.8X is not a valid function number\n", thread->threadId, TLevel[thread->level], function);
		return 0xFFFFFFFF;
	}
	if(result == 0x80000008)
	{
		printf("[Thread %d]: %sError: %d is not a valid parameter for function 0x%.8X\n", thread->threadId, TLevel[thread->level], value, function);
		return 0xFFFFFFFF;
	}
	return 0;
}

uint32_t Opcode_Grp1_SetFontAdjust(Thread_t* thread)
{
	int32_t originY = (int32_t)Thread_PopStack(thread);
	int32_t originX = (int32_t)Thread_PopStack(thread);
	uint32_t scaleY = Thread_PopStack(thread);
	uint32_t scaleX = Thread_PopStack(thread);
	const char* name = (const char*)Thread_PopAndResolveAddress(thread);

	printf("[Thread %d]: %sAdjust font \"%s\" by %d.%04X/%d.%04X at %d.%04X, %d.%04X\n", thread->threadId, TLevel[thread->level], name != NULL ? name : "(none)", scaleX >> 16, scaleX & 0xFFFF, scaleY >> 16, scaleY & 0xFFFF, originX >> 16, originX & 0xFFFF, originY >> 16, originY & 0xFFFF);

	uint32_t result = Engine_SetFontAdjust(name, scaleX, scaleY, originX, originY);
	if(result != 0)
	{
		printf("[Thread %d]: %sError: %s rejected\n", thread->threadId, TLevel[thread->level], result == 0x80000005 ? "scale" : "origin");
		return result;
	}
	return 0;
}

// Grp1 0x0C (0x004808A0 -> 0x00469110 -> 0x0042DD20): how a glyph's inked samples
// become its coverage - 0 in proportion, 1 through a sine curve. Pushes 1 when the
// value was taken, 0 for anything above 1.
uint32_t Opcode_Grp1_SetCoverageCurve(Thread_t* thread)
{
	uint32_t curve = Thread_PopStack(thread);
	Thread_PushStack(thread, (uint32_t)Font_SetCoverageCurve(curve));
	return 0;
}

// Grp1 0x0D (0x004808D0 -> 0x00469100 -> 0x0042DD10): one value into 0x00565B60,
// which 0x0042E1F0 reads when it makes a font: with it set, a face that reports
// itself as variable pitch is not given the width the font was asked for. Nothing
// is pushed.
uint32_t Opcode_Grp1_SetFontPitchCheck(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	Font_SetPitchCheck(value);
	return 0;
}

uint32_t Opcode_Grp1_Unknown_16(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_17(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_18(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_19(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_20(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_21(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_24(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_25(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * Grp1 0x1C (0x00481740 -> 0x00402B90) scales one bitmap into another. The script
 * pushes the destination, the source, the horizontal rate, the vertical rate and
 * the filter flag, both rates being 16.16 fixed point, so they pop back to front.
 * All four of the original's failures are fatal and name what caused them
 * (0x004EA8DC, 0x004EA858, 0x004EA90C, 0x004EA94C).
 */
uint32_t Opcode_Grp1_ScaleBitmap(Thread_t* thread)
{
	int filter      = (int)Thread_PopStack(thread);
	int rateY       = (int)Thread_PopStack(thread);
	int rateX       = (int)Thread_PopStack(thread);
	int source      = (int)Thread_PopStack(thread);
	int destination = (int)Thread_PopStack(thread);

	Engine_t* engine = thread->engine;
	switch(Renderer_ScaleBitmap(engine->renderer, destination, source, rateX, rateY, filter))
	{
		case 0:
			return 0;
		case 1:
			printf("[Thread %d]: %sError: the specified destination bitmap [ %d ] is invalid\n",
			       thread->threadId, TLevel[thread->level], destination);
			return 0xFFFFFFFF;
		case 2:
			printf("[Thread %d]: %sError: the specified reference bitmap [ %d ] is invalid\n",
			       thread->threadId, TLevel[thread->level], source);
			return 0xFFFFFFFF;
		case 3:
			printf("[Thread %d]: %sError: the specified reference bitmap [ %d ] is not TRUECOLOR\n",
			       thread->threadId, TLevel[thread->level], source);
			return 0xFFFFFFFF;
		case 4:
			printf("[Thread %d]: %sError: an invalid stretch rate [ %d , %d ] was specified\n",
			       thread->threadId, TLevel[thread->level], rateY, rateX);
			return 0xFFFFFFFF;
		default:
			printf("[Thread %d]: %sError: the smooth scaler for rates [ %d , %d ] is not written yet\n",
			       thread->threadId, TLevel[thread->level], rateX, rateY);
			return 0xFFFFFFFF;
	}
}

uint32_t Opcode_Grp1_Unknown_30(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * Grp1 0x1F (0x00481A50 -> 0x00403450) makes one bitmap a copy of another. The
 * source pops first and the destination second; the destination is created at the
 * source's own size and pixel mode, the whole surface is copied, and the source's
 * offset pair goes with it. Both failures are fatal in the original and name the
 * bitmap that caused them (0x004E8B74, 0x004E8B44).
 */
uint32_t Opcode_Grp1_DuplicateBitmap(Thread_t* thread)
{
	int source      = (int)Thread_PopStack(thread);
	int destination = (int)Thread_PopStack(thread);

	Engine_t* engine = thread->engine;
	switch(Renderer_DuplicateBitmap(engine->renderer, destination, source))
	{
		case 0:
			return 0;
		case 1:
			printf("[Thread %d]: %sError: the specified destination bitmap [ %d ] is invalid\n",
			       thread->threadId, TLevel[thread->level], destination);
			return 0xFFFFFFFF;
		default:
			printf("[Thread %d]: %sError: the specified source bitmap [ %d ] is invalid\n",
			       thread->threadId, TLevel[thread->level], source);
			return 0xFFFFFFFF;
	}
}

uint32_t Opcode_Grp1_Unknown_51(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_SetScreenLayered(Thread_t* thread)
{
	// 0x00481F10 -> 0x004623D0 -> 0x0043DD90: the screen becomes class 12 and its
	// layer 0 shows an image. Popped: a flag (+0x190), the y and x scales (16.16),
	// the angle, the anchor's y and x inside the image (16.16), the image, then the
	// y and x (16.16) the anchor goes to. A bad image (0x004E8DC4) or a scale of
	// zero (0x004E9530) is fatal.
	uint32_t flag = Thread_PopStack(thread);
	uint32_t scaleY = Thread_PopStack(thread);
	uint32_t scaleX = Thread_PopStack(thread);
	uint32_t angle = Thread_PopStack(thread);
	int32_t anchorY = (int32_t)Thread_PopStack(thread);
	int32_t anchorX = (int32_t)Thread_PopStack(thread);
	int32_t bitmap = (int32_t)Thread_PopStack(thread);
	int32_t y = (int32_t)Thread_PopStack(thread);
	int32_t x = (int32_t)Thread_PopStack(thread);
	uint32_t r = Screen_SetLayered(x, y, bitmap, anchorX, anchorY, angle, scaleX, scaleY, flag);
	if(r != 0)
	{
		printf("[Thread %d]: %sError: the layered screen failed (%u): image %d, scale %u x %u\n",
		       thread->threadId, TLevel[thread->level], r, bitmap, scaleX, scaleY);
		return 0xFFFFFFFC;
	}
	return 0;
}

uint32_t Opcode_Grp1_Unknown_65(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_66(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_67(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_68(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_69(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_70(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_71(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_72(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_73(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_74(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_96(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_97(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_100(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_101(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_102(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_103(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_104(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_SetWindowFont(Thread_t* thread)
{
	uint32_t value364 = Thread_PopStack(thread);
	uint32_t value354 = Thread_PopStack(thread);
	uint32_t style = Thread_PopStack(thread);
	uint32_t width = Thread_PopStack(thread);
	uint32_t size = Thread_PopStack(thread);
	uint32_t number = Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	Screen_t* window = Renderer_ResolveScreen(thread->engine->renderer, handle);
	if(window == NULL)
	{
		printf("[Thread %d]: %sError: an invalid window handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}

	const char* family = Engine_FontNameById(number);
	if(family == NULL)
	{
		printf("[Thread %d]: %sError: the font number [ %d ] is invalid\n",
		       thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFF;
	}

	window->field354 = (int)value354;
	window->field364 = (int)value364;
	window->fontFamily = family;
	window->fontSize = (int)size;
	window->fontWidth = (int)width;
	window->fontStyle = (int)style;
	window->fontScaledWidth = (int)((size * width) / 100);

	printf("[Thread %d]: %sWindow font \"%s\" (number %d), size %d, width %d, style %d\n",
	       thread->threadId, TLevel[thread->level], family, number, size, width, style);
	return 0;
}

uint32_t Opcode_Grp1_SetWindowGapCoefficient(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	Screen_t* window = Renderer_ResolveScreen(thread->engine->renderer, handle);
	if(window == NULL)
	{
		printf("[Thread %d]: %sError: an invalid window handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}
	if(value > SCREEN_MAX_GAP_COEFFICIENT)
	{
		printf("[Thread %d]: %sError: the gap coefficient [ %d ] is invalid\n",
		       thread->threadId, TLevel[thread->level], value);
		return 0xFFFFFFFF;
	}
	window->gapCoefficient = (int)value;
	return 0;
}

uint32_t Opcode_Grp1_Unknown_138(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_SetWindowSwingingStyle(Thread_t* thread)
{
	uint32_t style = Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	Screen_t* window = Renderer_ResolveScreen(thread->engine->renderer, handle);
	if(window == NULL)
	{
		printf("[Thread %d]: %sError: an invalid window handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}
	if(style > SCREEN_MAX_SWINGING_STYLE)
	{
		printf("[Thread %d]: %sError: an invalid message swinging style [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], style);
		return 0xFFFFFFFF;
	}
	window->swingingStyle = (int)style;
	return 0;
}

uint32_t Opcode_Grp1_Unknown_140(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

/*
 * Grp1 0x8D (0x00484520 -> 0x00463290 -> 0x004410F0 -> 0x0042C800): pops a window and
 * pushes 1 (it exists), then its text cursor, +0x368 (x) and +0x36C (y), which
 * 0x0042C690 puts at the client area's top-left (or top-right for vertical text) when
 * the client area is set (Grp0 0x88). A handle that is not a window is fatal
 * (0x004E9AC8). Net, the stack grows by two. The reference trace of the original
 * shows 1, 0x12A, 0x58 after the message window's client area was set to
 * (0x12A, 0x58).
 */
uint32_t Opcode_Grp1_GetTextCursor(Thread_t* thread)
{
	uint32_t handle = Thread_PopStack(thread);
	DisplayObject_t* window = Object_ResolveKind(handle, OBJECT_TYPE_WINDOW);
	if(window == NULL)
	{
		printf("[Thread %d]: %sError: an invalid window handle [ 0x%.8X ] was specified\n",
		       thread->threadId, TLevel[thread->level], handle);
		return 0xFFFFFFFC;
	}
	Thread_PushStack(thread, 1);
	Thread_PushStack(thread, (uint32_t)window->textCursorX);
	Thread_PushStack(thread, (uint32_t)window->textCursorY);
	return 0;
}

uint32_t Opcode_Grp1_Unknown_142(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_144(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_145(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_146(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_147(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_Unknown_148(Thread_t* thread)
{
    uint8_t* ptr1 = Thread_PopAndResolveAddress(thread);
    uint8_t* ptr2 = Thread_PopAndResolveAddress(thread);
    printf("[Thread %d]: %sWarning: dummy opcode\n", thread->threadId, TLevel[thread->level]);
    return 0;
}

uint32_t Opcode_Grp1_Unknown_149(Thread_t* thread)
{
	return 0xFFFFFFFF;
}

uint32_t Opcode_Grp1_LoadNameTable(Thread_t* thread)
{
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);

	uint32_t result = NameTable_Load(text);
	printf("[Thread %d]: %sLoad the name table from %d bytes of text: %s\n", thread->threadId, TLevel[thread->level], text != NULL ? (int)strlen(text) : 0, result != 0 ? "all of it" : "stopped early");

	Thread_PushStack(thread, result);
	return 0;
}

uint32_t Opcode_Grp1_SetPhoneticMargin(Thread_t* thread)
{
	uint32_t value1 = Thread_PopStack(thread);
	uint32_t value2 = Thread_PopStack(thread);
	uint32_t value3 = Thread_PopStack(thread);
	uint32_t value4 = Thread_PopStack(thread);
	uint32_t value5 = Thread_PopStack(thread);
	//uint32_t value6 = Thread_PopStack(thread);
	printf("[Thread %d]: %sWarning: dummy opcode\n", thread->threadId, TLevel[thread->level]);
	return 0;
}

/*
 * Grp1 0x9C (0x00484BB0): Grp2 0x9C's draw with the default style (0x00433650) and
 * one colour for the text and its ruby. Popped: the colour, the line spacing,
 * kinsoku, the proportional setting, bold, width, size, font number, the ruby
 * dictionary, whether there is ruby, the text, y, x and the bitmap; the checks and
 * the failures are Grp2 0x9C's. Pushes the number of lines.
 */
uint32_t Opcode_Grp1_DrawTextInDefaultStyle(Thread_t* thread)
{
	uint32_t colour = Thread_PopStack(thread);
	int32_t lineSpacing = (int32_t)Thread_PopStack(thread);
	uint32_t kinsoku = Thread_PopStack(thread);
	uint32_t proportional = Thread_PopStack(thread);
	uint32_t bold = Thread_PopStack(thread);
	int32_t width = (int32_t)Thread_PopStack(thread);
	int32_t size = (int32_t)Thread_PopStack(thread);
	uint32_t fontNumber = Thread_PopStack(thread);
	const char* dictionary = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t ruby = Thread_PopStack(thread);
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);
	int32_t y = (int32_t)Thread_PopStack(thread);
	int32_t x = (int32_t)Thread_PopStack(thread);
	int32_t bitmap = (int32_t)Thread_PopStack(thread);
	if(bitmap < 0 || bitmap >= RENDERER_MAX_BITMAPS)
	{
		printf("[Thread %d]: %sError: an invalid bitmap number [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], bitmap);
		return 0xFFFFFFFC;
	}
	if(Engine_FontNameById(fontNumber) == NULL)
	{
		printf("[Thread %d]: %sError: an invalid font number [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], fontNumber);
		return 0xFFFFFFFC;
	}
	TextStyle_t style;
	Text_DefaultStyle(&style);
	uint32_t lines = (uint32_t)x;
	uint32_t result = Text_DrawIntoBitmap(thread->engine->renderer, bitmap, &lines, x, y, text, ruby,
	                                      dictionary, fontNumber, size, width, bold, proportional,
	                                      kinsoku, lineSpacing, colour, colour, &style);
	if(Opcode_ReportTextFailure(thread, result, size, width, fontNumber, bitmap))
		return 0xFFFFFFFC;
	Thread_PushStack(thread, lines);
	return 0;
}

/*
 * Grp1 0x9D (0x00484DC0): Grp1 0x9C with one more value popped first - non-zero
 * draws the text plain (0x00484EB9, a style of kind 0), zero in the default style.
 */
uint32_t Opcode_Grp1_DrawTextInDefaultStyleOrPlain(Thread_t* thread)
{
	uint32_t plain = Thread_PopStack(thread);
	uint32_t colour = Thread_PopStack(thread);
	int32_t lineSpacing = (int32_t)Thread_PopStack(thread);
	uint32_t kinsoku = Thread_PopStack(thread);
	uint32_t proportional = Thread_PopStack(thread);
	uint32_t bold = Thread_PopStack(thread);
	int32_t width = (int32_t)Thread_PopStack(thread);
	int32_t size = (int32_t)Thread_PopStack(thread);
	uint32_t fontNumber = Thread_PopStack(thread);
	const char* dictionary = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t ruby = Thread_PopStack(thread);
	const char* text = (const char*)Thread_PopAndResolveAddress(thread);
	int32_t y = (int32_t)Thread_PopStack(thread);
	int32_t x = (int32_t)Thread_PopStack(thread);
	int32_t bitmap = (int32_t)Thread_PopStack(thread);
	if(bitmap < 0 || bitmap >= RENDERER_MAX_BITMAPS)
	{
		printf("[Thread %d]: %sError: an invalid bitmap number [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], bitmap);
		return 0xFFFFFFFC;
	}
	if(Engine_FontNameById(fontNumber) == NULL)
	{
		printf("[Thread %d]: %sError: an invalid font number [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], fontNumber);
		return 0xFFFFFFFC;
	}
	TextStyle_t style;
	if(plain != 0)
		Text_MakeStyle(&style, 0, 0, 0, 0, 0);
	else
		Text_DefaultStyle(&style);
	uint32_t lines = (uint32_t)x;
	uint32_t result = Text_DrawIntoBitmap(thread->engine->renderer, bitmap, &lines, x, y, text, ruby,
	                                      dictionary, fontNumber, size, width, bold, proportional,
	                                      kinsoku, lineSpacing, colour, colour, &style);
	if(Opcode_ReportTextFailure(thread, result, size, width, fontNumber, bitmap))
		return 0xFFFFFFFC;
	Thread_PushStack(thread, lines);
	return 0;
}

// Grp1 0xB8 (0x00485070) pops a window handle, makes a DCIPIconEx out of it
// (0x0046C7B0 with the kind 1) and pushes the icon's handle. iconmngr._bp does this
// once and then configures the icon through the opcodes beside this one.
uint32_t Opcode_Grp1_CreateIconEx(Thread_t* thread)
{
	uint32_t windowHandle = Thread_PopStack(thread);
	uint32_t handle = Icon_Create(windowHandle, ICON_KIND_EX);

	printf("[Thread %d]: %sIcon 0x%08X made from window 0x%08X\n",
	       thread->threadId, TLevel[thread->level], handle, windowHandle);
	Thread_PushStack(thread, handle);
	return 0;
}

// Grp1 0xBA (0x004850A0) gives an Ex icon its content. It pops the address of the
// descriptor tree and resolves it, pops the icon's handle, and pushes back what
// 0x0046CCE0 answers: 0 when the icon took the content, 1 when the handle is not an
// icon, 4 when it is a plain DCIPIcon rather than an Ex (0x00448590 reads +0x24 and
// 0x0046CCF9 insists on 1), and 2 or 3 for the two malformed shapes of the tree.
// The descriptor is freed either way: the icon copies what it keeps.
uint32_t Opcode_Grp1_SetIconContent(Thread_t* thread)
{
	const uint32_t* root = (const uint32_t*)Thread_PopAndResolveAddress(thread);
	uint32_t handle = Thread_PopStack(thread);

	Icon_t* icon = Icon_Resolve(handle);
	if(icon == NULL)
	{
		printf("[Thread %d]: %sIcon 0x%08X does not exist\n",
		       thread->threadId, TLevel[thread->level], handle);
		Thread_PushStack(thread, 1);
		return 0;
	}
	if(icon->kind != ICON_KIND_EX)
	{
		printf("[Thread %d]: %sIcon 0x%08X is not an Ex icon and has no content\n",
		       thread->threadId, TLevel[thread->level], handle);
		Thread_PushStack(thread, 4);
		return 0;
	}

	IconContent_t* content = NULL;
	uint32_t status = Icon_ReadContent(thread, root, &content);
	if(status != 0)
	{
		printf("[Thread %d]: %sIcon 0x%08X was given a malformed content descriptor (%u)\n",
		       thread->threadId, TLevel[thread->level], handle, status);
		Thread_PushStack(thread, status);
		return 0;
	}

	printf("[Thread %d]: %sIcon 0x%08X content: %u entr%s (",
	       thread->threadId, TLevel[thread->level], handle, content->entryCount,
	       content->entryCount == 1 ? "y" : "ies");
	for(uint32_t i = 0; i < content->entryCount; i++)
		printf("%s%u parts", i == 0 ? "" : ", ", content->entries[i].partCount);
	printf(")\n");

	// 0x0044A9E0 builds from the copy, which 0x0046CB00 then frees; the pushed
	// result is 0, or 2 and 3 for 0x80000001 and 0x80000002.
	uint32_t result = Icon_SetContent(thread->engine->renderer, icon, content);
	Icon_FreeContent(content);
	Thread_PushStack(thread, result == ICON_CONTENT_SET_BAD_COUNT ? 2 : result == ICON_CONTENT_SET_BAD_ENTRY ? 3 : 0);
	return 0;
}

/*
 * Grp1 0xBF (0x00485130 -> 0x00447C90): pops the address of 24 actions and a key map
 * number; maps 4 to 7 take them. Any other number is fatal.
 */
uint32_t Opcode_Grp1_SetIconKeyMap(Thread_t* thread)
{
	const uint32_t* actions = (const uint32_t*)Thread_PopAndResolveAddress(thread);
	uint32_t map = Thread_PopStack(thread);
	if(actions == NULL || !Icon_SetKeyMap(map, actions))
	{
		printf("[Thread %d]: %sError: an invalid icon key map [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], (int32_t)map);
		return 0xFFFFFFFC;
	}
	return 0;
}

// Grp1 0x31 (0x00481AE0 -> 0x004620B0 -> 0x004434D0) sets the display object's second
// hiding flag, +0x0C. It is the twin of Grp0 0x31, which sets +0x04: an object is
// drawn only when +0x04 is set and +0x0C is clear (0x0041AF00), and the two
// propagation flags Grp0 0x38 writes decide which of them reaches the children.
// The script pushes the handle and then the flag, so the flag is popped first, and
// an unresolvable handle is fatal: "an invalid object handle was specified"
// (0x004E8BD0).
uint32_t Opcode_Grp1_SetObjectHidden(Thread_t* thread)
{
	uint32_t hidden = Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);

	DisplayObject_t* object = Object_Resolve(handle);
	if(object == NULL)
	{
		printf("[Thread %d]: %sError: an invalid object handle was specified\n",
		       thread->threadId, TLevel[thread->level]);
		return 0xFFFFFFFF;
	}

	Object_ApplyHidden(object, (int)hidden);
	return 0;
}

/*
 * Grp1 0x98 (0x00484980 -> 0x00463470 -> 0x00434420): the text engine's ruby (reading
 * aid) settings and the four values beside them. Popped, in order: a value kept at
 * 0x00565CF0, the ruby margin (0x00565BDC; below 0 is fatal, "invalid margin size for
 * the reading", 0x004EB2EC), the ruby size rate in percent (0x00507640; outside 25 to
 * 100 is fatal, "invalid size rate for the reading", 0x004EB2B4), a value kept at
 * 0x00565BB0, one kept at 0x0050763C (0 becomes 1) and one at 0x00507638. What reads
 * the four unnamed ones belongs to the text drawing, which is not read yet; they are
 * kept by address until it is.
 */
uint32_t gText565CF0 = 0, gRubyMargin = 0, gRubyRate = 0, gText565BB0 = 0, gText50763C = 0, gText507638 = 0;

uint32_t Opcode_Grp1_SetRubyStyle(Thread_t* thread)
{
	uint32_t a = Thread_PopStack(thread);
	int32_t margin = (int32_t)Thread_PopStack(thread);
	uint32_t rate = Thread_PopStack(thread);
	uint32_t b = Thread_PopStack(thread);
	uint32_t c = Thread_PopStack(thread);
	uint32_t d = Thread_PopStack(thread);
	if(rate - 0x19u > 0x4Bu)
	{
		printf("[Thread %d]: %sError: an invalid size rate for the reading [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], (int32_t)rate);
		return 0xFFFFFFFC;
	}
	if(margin < 0)
	{
		printf("[Thread %d]: %sError: an invalid margin size for the reading [ %d ] was specified\n",
		       thread->threadId, TLevel[thread->level], margin);
		return 0xFFFFFFFC;
	}
	gText507638 = d;
	gText50763C = c == 0 ? 1 : c;
	gRubyRate = rate;
	gText565CF0 = a;
	gText565BB0 = b;
	gRubyMargin = (uint32_t)margin;
	return 0;
}

/*
 * Grp1 0xBB (0x004850E0 -> 0x0046CE20 -> 0x0044B540): pops the value, the part, the
 * entry and an Ex icon's handle; the part's sprite is turned on or off. Pushes 0,
 * 1 for a handle that is not an icon, 4 for a plain icon, 2 and 3 for a bad entry and
 * a bad part.
 */
uint32_t Opcode_Grp1_SetIconPartEnabled(Thread_t* thread)
{
	uint32_t value = Thread_PopStack(thread);
	int32_t part = (int32_t)Thread_PopStack(thread);
	int32_t entry = (int32_t)Thread_PopStack(thread);
	uint32_t handle = Thread_PopStack(thread);
	Icon_t* icon = Icon_Resolve(handle);
	uint32_t result;
	if(icon == NULL)
		result = 1;
	else if(icon->kind != ICON_KIND_EX)
		result = 4;
	else
	{
		uint32_t r = Icon_SetPartEnabled(thread->engine->renderer, icon, entry, part, value);
		result = r == 0x80000001u ? 2 : r == 0x80000002u ? 3 : 0;
	}
	Thread_PushStack(thread, result);
	return 0;
}
