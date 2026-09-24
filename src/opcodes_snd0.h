#ifndef OPCODES_SND0_H_
#define OPCODES_SND0_H_

#include <stdint.h>

// ----------------------------------------------------------------------------------
// SE records
//
// The engine keeps a 0x40-byte record per SE number, sixty-four of them
// (0x00497AE0 refuses a number of 0x40 or more, by name), as a flat table at
// 0x0055EF18. 0x00494490 fills one when an SE is registered: it is the member's own
// 0x40-byte bw header with +0x3C overwritten by 65536 / speed, and 0x00494570 reads
// the length in milliseconds back out of it (frames at +0x0C, rate at +0x10). Snd0
// 0x22 clears one. The sound itself lives in the sound library (audio.c).
// ----------------------------------------------------------------------------------
#define SND0_CHANNEL_COUNT      0x40
#define SND0_CHANNEL_RECORD_WORDS 16

extern uint32_t gSoundChannels[SND0_CHANNEL_COUNT][SND0_CHANNEL_RECORD_WORDS];

typedef struct Thread Thread_t;

// 0x004945F0 / 0x00494640: silence every channel's master volume while the window
// is inactive and give the scripts' values back after (called by 0x0049905F).
void Snd0_Suspend(void);
void Snd0_Resume(void);

uint32_t Opcode_Snd0_Constant(Thread_t* thread);
uint32_t Opcode_Snd0_SetMusicMasterVolume(Thread_t* thread);
uint32_t Opcode_Snd0_SetSEMasterVolume(Thread_t* thread);
uint32_t Opcode_Snd0_LoadMusicFile(Thread_t* thread);
uint32_t Opcode_Snd0_LoadMusic(Thread_t* thread);
uint32_t Opcode_Snd0_LoadMusicLoop(Thread_t* thread);
uint32_t Opcode_Snd0_PlayMusic(Thread_t* thread);
uint32_t Opcode_Snd0_GetMusicStatus(Thread_t* thread);
uint32_t Opcode_Snd0_FadeMusicVolume(Thread_t* thread);
uint32_t Opcode_Snd0_SetMusicPan(Thread_t* thread);
uint32_t Opcode_Snd0_FadeInMusic(Thread_t* thread);
uint32_t Opcode_Snd0_FadeOutMusic(Thread_t* thread);
uint32_t Opcode_Snd0_SetMusicVolume(Thread_t* thread);
uint32_t Opcode_Snd0_RegisterSE(Thread_t* thread);
uint32_t Opcode_Snd0_RegisterSEEx(Thread_t* thread);
uint32_t Opcode_Snd0_ResetSE(Thread_t* thread);
uint32_t Opcode_Snd0_RegisterSEDouble(Thread_t* thread);
uint32_t Opcode_Snd0_PlaySE(Thread_t* thread);
uint32_t Opcode_Snd0_StopSE(Thread_t* thread);
uint32_t Opcode_Snd0_FadeOutSE(Thread_t* thread);
uint32_t Opcode_Snd0_RegisterSESpeed(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_40(Thread_t* thread);
uint32_t Opcode_Snd0_SetSEVolume(Thread_t* thread);
uint32_t Opcode_Snd0_GetSEDuration(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_128(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_129(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_132(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_133(Thread_t* thread);
uint32_t Opcode_Snd0_Unknown_134(Thread_t* thread);
uint32_t Opcode_Snd0_PlaySound(Thread_t* thread);

extern OpcodePtr_t OpcodesSnd0[256];
extern char* OpcodesSnd0Mnemonics[256];

#endif
