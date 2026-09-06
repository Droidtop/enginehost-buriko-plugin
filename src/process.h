#ifndef PROCESS_H_
#define PROCESS_H_

#include <stdint.h>

typedef struct Thread Thread_t;

/*
 * A process is what a thread waits on. The original keeps one per thread at
 * +0x58 of the thread object and marks the thread with bit 0 of its flags; the
 * interpreter runs the process (0x004452D0) before it runs any instruction and
 * destroys it as soon as its Run says it is finished, at which point the thread
 * carries on with whatever the process pushed on its stack.
 *
 * Every process shares the base at 0x004319D0: the thread it belongs to, a
 * deadline in ticks (+0x08), an aborted flag (+0x10) and a queue of events
 * (+0x14) posted to it. The kinds differ only in what their Run does with them.
 */
typedef enum ProcessKind
{
	/* CProcWaitTimingEx, 0x004E5A34: wait out a delay, optionally cut short by
	   a key. Made by Sys0 0x5C. */
	PROCESS_WAIT_TIMING = 0
} ProcessKind_t;

typedef struct ProcessEvent ProcessEvent_t;
struct ProcessEvent
{
	/* The original's events are three dwords and a link (0x00431C70). */
	uint32_t        value[3];
	ProcessEvent_t* next;
};

typedef struct Process Process_t;
struct Process
{
	ProcessKind_t   kind;
	Thread_t*       thread;      /* +0x04 */
	uint32_t        deadline;    /* +0x08, a tick count */
	int             aborted;     /* +0x10 */
	ProcessEvent_t* events;      /* +0x14 */
	ProcessEvent_t* eventsTail;
	uint32_t        allowKey;    /* +0x20, may a key cut the wait short */
	uint32_t        keyMask;     /* +0x24 */
	int             stopped;     /* +0x28, an event of 1 was posted */
};

Process_t* Process_CreateWaitTiming(Thread_t* thread, uint32_t delay, uint32_t allowKey, uint32_t keyMask);
void       Process_PostEvent(Process_t* process, uint32_t a, uint32_t b, uint32_t c);
/* 1 when the process has finished (and has pushed its result on the thread),
   0 while it is still waiting. */
int        Process_Run(Process_t* process);
void       Process_Destroy(Process_t* process);

#endif /* PROCESS_H_ */
