#ifndef OPCODES_SND0_H_
#define OPCODES_SND0_H_

// ----------------------------------------------------------------------------------
// Sound channels
//
// The engine keeps sixty-four of them (0x00497AE0 refuses a number of 0x40 or more,
// by name) as a flat table of 0x40-byte records at 0x0055EF18. The fields inside a
// record belong to the opcodes that fill them in, and none of those are written yet,
// so a record is kept here as the sixteen words it is rather than as a shape guessed
// from one opcode. The audio itself - decoding and mixing - does not exist in this
// engine at all; where an opcode would reach it, it says so by name.
// ----------------------------------------------------------------------------------
#define SND0_CHANNEL_COUNT      0x40
#define SND0_CHANNEL_RECORD_WORDS 16

extern uint32_t gSoundChannels[SND0_CHANNEL_COUNT][SND0_CHANNEL_RECORD_WORDS];

#include <stdint.h>

typedef struct Thread Thread_t;

uint32_t Opcode_Snd0_Unknown_0(Thread_t* thread);
uint32_t Opcode_Snd0_SetChannelVolume(Thread_t* thread);
uint32_t Opcode_Snd0_SetEffectVolume(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_16(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_17(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_18(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_20(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_21(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_22(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_23(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_24(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_25(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_32(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_33(Thread_t* thread);
uint32_t Opcode_Snd0_ResetChannel(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_36(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_37(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_38(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_128(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_129(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_132(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_133(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_134(Thread_t* thread);
uint32_t Opcode_Snd0_PlaySound(Thread_t* thread);

extern OpcodePtr_t OpcodesSnd0[256];
extern char* OpcodesSnd0Mnemonics[256];

#endif