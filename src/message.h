#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include <stdint.h>

// ----------------------------------------------------------------------------------
// A window's message (the classes at 0x00432CA0 / 0x004342B0, Grp2 0x90).
//
// The settings below are the engine-wide ones every new message reads; Grp0 0x91 to
// 0x9F write them (0x00432E50 .. 0x004335F0 behind the 0x0047E1C0 .. 0x0047E5E0
// handlers). 0x0056xxxx are in the zeroed part of the data section, 0x0050766x in the
// initialised part, which starts 0x00507660 at 0x32.
// ----------------------------------------------------------------------------------

// 0x00565B90 / 0x00565B94 (Grp0 0x9B, 0x00433590): whether a new message holds its
// end wait for a minimum time, and that time in milliseconds (0x00432D79: +0x44 and
// +0x48 = now + the time; 0x00433F5D waits it out unless the message is skipped).
extern uint32_t gMessageHold;
extern uint32_t gMessageHoldTime;
// 0x00565B98 / 0x00565B9C (Grp0 0x97, 0x004335F0): the automatic mode - the end wait
// ends by itself that many milliseconds after it started (0x00433F40).
extern uint32_t gMessageAuto;
extern uint32_t gMessageAutoTime;
// 0x00565BA0 (Grp0 0x9F, 0x00432E50): a key that skips a message also shows the rest
// of it at once (0x0043373B).
extern uint32_t gMessageKeyShowsAll;
// 0x00565BA4 / 0x00565BA8 (Grp0 0x91, 0x00432E60): which region a message takes its
// keys from - 0 the key 2, 1 a priority of the script's own, 2 the window's own layer
// (0x00432CFE).
extern uint32_t gMessageRegionMode;
extern uint32_t gMessageRegionPriority;
// 0x00565BAC (Grp0 0x92, 0x00432EA0): when set, drawing a message does not ask for the
// screen to be presented at once (0x004338A0).
extern uint32_t gMessageNoFlush;
// 0x00507660 (Grp0 0x94, 0x004335A0): the base text class's wait between characters
// (0x00433B90). 0x32 in the data section.
extern uint32_t gMessageCharacterWait;

// Grp0 0x91: 0, or 0x80000004 for a mode above 2 and 0x80000005 for a priority of
// 0x10000 or more with mode 1; nothing changes on a failure.
uint32_t Message_SetRegionMode(uint32_t mode, uint32_t priority);

#endif // __MESSAGE_H__
