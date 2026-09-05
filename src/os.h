#ifndef _OS_H_
#define _OS_H_

#include <stdint.h>

typedef struct Engine Engine_t;

int OS_Init(Engine_t* engine);
int OS_Poll();
int OS_Quit();

// Physical memory, in bytes, as the engine reports it to scripts.
void OS_GetPhysicalMemory(uint64_t* total, uint64_t* available);

#endif