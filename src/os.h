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

#endif