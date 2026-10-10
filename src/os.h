#ifndef _OS_H_
#define _OS_H_

#include <stdint.h>

typedef struct Engine Engine_t;

int OS_Init(Engine_t* engine);
void OS_Sleep(uint32_t milliseconds);
int OS_Poll();
int OS_Quit();

// Milliseconds since start-up. The original reads timeGetTime or GetTickCount
// through the wrapper at 0x004988B0; every engine deadline is measured against it.
uint32_t OS_GetTicks();
uint32_t OS_FrameInterval();

// Physical memory, in bytes, as the engine reports it to scripts.
void OS_GetPhysicalMemory(uint64_t* total, uint64_t* available);

// Whether a Windows virtual-key code is held down right now, the question
// 0x0046D6E0 answers with GetAsyncKeyState. The engine speaks Win32 virtual
// keys throughout - they are what the scripts' own key settings hold - so the
// mapping onto whatever the host uses lives here and nowhere else.
int OS_IsKeyDown(uint32_t vk);
// The host's input marks a virtual key held or not (keyboard, mouse, touch and
// the controller all report here).
void OS_SetVkHeld(uint32_t vk, int held);
// Shows a composed frame, scaled to the display with its aspect kept.
void OS_Present(const uint8_t* pixels, int width, int height, int stride);
// The window takes a display mode's size, as 0x00461290 sizes the original's.
// Only a desktop window can; a host that owns the screen scales the frame.
void OS_SetWindowSize(int width, int height);

// The sound output, as the original's DirectSound device (0x004A6540): one
// stream of interleaved 16-bit stereo, which `fill` writes `bytes` of at a
// time from the output's own thread. Opened paused; *rate and *frames say
// what the output took (the rate asked for is a preference, not a promise).
// 1 when it opened.
typedef void (*OS_AudioFill)(void* user, uint8_t* stream, int bytes);
int OS_AudioOpen(int wantRate, int wantFrames, OS_AudioFill fill, int* rate, int* frames);
void OS_AudioPause(int paused);
void OS_AudioClose(void);
// Keeps the output's thread out of `fill` while the engine changes what it reads.
void OS_AudioLock(void);
void OS_AudioUnlock(void);

// A movie's own sound, a queue of 16-bit samples at the movie's rate and
// channel count beside the engine's output (DirectShow's own renderer).
// `count` is the number of samples, all channels counted.
int OS_MovieAudioOpen(int rate, int channels);
void OS_MovieAudioQueue(const int16_t* samples, int count);
uint32_t OS_MovieAudioQueuedBytes(void);
void OS_MovieAudioPause(int paused);
void OS_MovieAudioClose(void);

#endif