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
	PROCESS_WAIT_TIMING = 0,
	/* The animation, vtable 0x004E51B0: it moves one display object's four
	   animated channels - position, effect level and scale - from where they
	   are to where they are asked for, over a duration, and holds the thread
	   that started it until it is done. Made by Grp0 0x20 to 0x23 and 0x28. */
	PROCESS_OBJECT_ANIMATION = 1,
	/* A process whose Run belongs to the subsystem that made it: the SE
	   registration of Snd0 0x20, 0x21, 0x23 and 0x27 (the loader at 0x00439FD0). */
	PROCESS_CALLBACK = 2,
	/* The bitmap loader (0x00439D50, vtable 0x004E57A0): the thread waits while the
	   loader thread reads and decodes; its Run (0x00439A70) ends the wait once the
	   load is done and pushes nothing. Made by Grp0 0x10. */
	PROCESS_LOAD = 3
} ProcessKind_t;

typedef int  (*ProcessCallbackRun_t)(void* context);   /* 1 when finished */
typedef void (*ProcessCallbackFree_t)(void* context);

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

	/* The animation's own, all of them fields of the 0xA8-byte object
	   (0x00431CB0, vtable 0x004E51B0). */
	uint32_t        objectHandle;  /* +0x20, re-resolved on every pass */
	uint32_t        stopCode;      /* +0x28: 1 a click, 0x100 a key, 0x80000000 skipping */
	uint32_t        elapsed;       /* +0x2C */
	uint32_t        duration;      /* +0x30, milliseconds, never zero */
	uint32_t        fps;           /* +0x38 */
	uint32_t        frameStep;     /* +0x3C */
	uint32_t        frames;        /* +0x40 */
	int32_t         startX, startY;   /* +0x44, +0x48 */
	int32_t         deltaX, deltaY;   /* +0x4C, +0x50 */
	uint32_t        positionCurve;    /* +0x54 */
	int32_t         startLevel;       /* +0x58 */
	int32_t         deltaLevel;       /* +0x5C */
	uint32_t        levelCurve;       /* +0x60 */
	int32_t         startEffect2;     /* +0x64 */
	int32_t         deltaEffect2;     /* +0x68 */
	int32_t         last[4];          /* +0x6C..+0x78: x, y, level, effect2 */
	int             keyRegistered;    /* +0x7C */
	uint32_t        allowSkip;        /* +0x80 */
	uint32_t        priority;         /* +0x84 */
	int             hadMouse;         /* +0x88 */
	int             hadKeyboard;      /* +0x8C */
	uint32_t        mouseTotal;       /* +0x90 */
	uint32_t        keyTotal;         /* +0x94 */
	int             stopRequested;    /* +0x98 */
	uint32_t        startTime;        /* +0x9C */
	uint32_t        frameInterval;    /* +0xA0 */
	uint32_t        nextFrame;        /* +0xA4 */

	/* The callback process's own. */
	ProcessCallbackRun_t  callbackRun;
	ProcessCallbackFree_t callbackFree;
	void*                 callbackContext;
};

Process_t* Process_CreateWaitTiming(Thread_t* thread, uint32_t delay, uint32_t allowKey, uint32_t keyMask);
/* What 0x00431E70 is given: where the object goes, over which curve (0x0041A760), the
   effect level it goes to and over which curve, the second effect parameter it goes
   to (negative: it stays), the duration in milliseconds, and the frame rate and step
   that quantise the clock. */
typedef struct ObjectAnimation
{
	int32_t  x, y;
	uint32_t positionCurve;
	int32_t  level;
	uint32_t levelCurve;
	int32_t  effect2;
	uint32_t duration;
	uint32_t fps;
	uint32_t frameStep;
} ObjectAnimation_t;

/* 0x00431CB0 + 0x00431E70 + 0x00431F60. NULL when the handle names no object. When
   allowSkip is set, a click or a key on the region (priority << 16 | 0xFFFF) or the
   skip keys cut it short. */
Process_t* Process_CreateObjectAnimation(Thread_t* thread, uint32_t objectHandle,
                                         const ObjectAnimation_t* animation,
                                         uint32_t allowSkip, uint32_t priority);
/* The curve table at 0x0041A760: t is 0 to 1 << 24, the answer 0 to 0x10000. */
int32_t    Process_Ease(uint32_t curve, int32_t t);
Process_t* Process_CreateCallback(Thread_t* thread, ProcessCallbackRun_t run,
                                  ProcessCallbackFree_t destroy, void* context);
/* Grp0 0x10's wait on the loader. The load itself is already done here, so the wait
   ends at its first pass, as the original's does when its loader thread has
   finished by then. */
Process_t* Process_CreateLoadWait(Thread_t* thread);
void       Process_PostEvent(Process_t* process, uint32_t a, uint32_t b, uint32_t c);
/* 1 when the process has finished (and has pushed its result on the thread),
   0 while it is still waiting. */
int        Process_Run(Process_t* process);
void       Process_Destroy(Process_t* process);

#endif /* PROCESS_H_ */
