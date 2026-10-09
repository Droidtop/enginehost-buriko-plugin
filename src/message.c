//
// A window's message (message.h).
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"

uint32_t gMessageHold = 0;
uint32_t gMessageHoldTime = 0;
uint32_t gMessageAuto = 0;
uint32_t gMessageAutoTime = 0;
uint32_t gMessageKeyShowsAll = 0;
uint32_t gMessageRegionMode = 0;
uint32_t gMessageRegionPriority = 0;
uint32_t gMessageNoFlush = 0;
uint32_t gMessageCharacterWait = 0x32;

uint32_t Message_SetRegionMode(uint32_t mode, uint32_t priority)
{
	// 0x00432E60.
	if(mode == 0 || mode == 2)
	{
		gMessageRegionMode = mode;
		return 0;
	}
	if(mode != 1)
		return 0x80000004;
	if(priority >= 0x10000)
		return 0x80000005;
	gMessageRegionMode = 1;
	gMessageRegionPriority = priority;
	return 0;
}
