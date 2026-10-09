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

struct Thread;
struct TextStyle;

// What 0x004913B0 is given by Grp2 0x90 (0x00486560), past the window and the text.
typedef struct MessageRequest
{
	uint32_t colour;            // +0x2C of the message (0x00434EC0)
	uint32_t ruby;              // whether ruby is laid out (+0xC4)
	uint32_t rubyColour;        // +0xDC
	uint32_t kinsoku;
	uint32_t instant;           // +0x30: the whole text at once
	uint32_t waitForKey;        // +0x38: wait for a key at the end
	uint32_t skipKeyAllowed;    // +0x3C: the skip key (bit 31 of the region's keys) counts
	uint32_t buttonsAllowed;    // +0x40: the configured buttons (0x00507690 | 0x80) count
} MessageRequest_t;

// 0x004913B0 with mode 1, as Grp2 0x90 calls it: a message object made for the
// window, the text laid out into it, and the thread joined to it (0x004452A0). The
// message draws its characters into the window's text surface over time, waits at the
// end as asked, and when it is done pushes whether it was skipped. 0 once the thread
// waits; -1 when the handle is not a window, 0x80000001 when the window has no font,
// and 0xFFFFFFFF when the window writes downwards, which needs the vertical class
// (0x00438270) this engine does not have yet.
uint32_t Message_Print(struct Thread* thread, uint32_t windowHandle, const char* text,
                       const struct TextStyle* style, const MessageRequest_t* request);

#endif // __MESSAGE_H__
