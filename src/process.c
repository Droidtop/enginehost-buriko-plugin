#include <stdio.h>
#include <stdlib.h>

#include "process.h"
#include "thread.h"
#include "engine.h"
#include "object.h"
#include "os.h"

Process_t* Process_CreateWaitTiming(Thread_t* thread, uint32_t delay, uint32_t allowKey, uint32_t keyMask)
{
	Process_t* process = (Process_t*)malloc(sizeof(Process_t));
	if(process == NULL)
		return NULL;
	process->kind = PROCESS_WAIT_TIMING;
	process->thread = thread;
	/* 0x00431AF0: the deadline is the current tick plus the delay. */
	process->deadline = OS_GetTicks() + delay;
	process->aborted = 0;
	process->events = NULL;
	process->eventsTail = NULL;
	process->allowKey = allowKey;
	process->keyMask = keyMask;
	process->stopped = 0;
	return process;
}

/*
 * 0x00491CD0 with 0x00431CB0 and 0x00431E70. The constructor keeps the handle
 * and re-resolves the display object on every pass; 0x00431E70 records where
 * each animated channel starts and how far it has to travel, makes the object
 * visible (vtable+0x04 with 1, at 0x00431F05) and starts the clock.
 *
 * Only the effect level moves. 0x00431DF0 reads the object's own position
 * through vtable+0x30 and its own scale through 0x0041B810 and passes both as
 * the TARGETS, so those deltas are zero by construction, not by omission - the
 * animation object can move them, this opcode never asks it to. Both easing
 * curves are 0 as well (0x00431E70's third and fifth arguments), and curve 0 is
 * 0x0041A760's default arm: a plain t >> 8, linear. The other fifteen curves in
 * the table at 0x0041AB2C are not written, and nothing can reach them from here.
 */
Process_t* Process_CreateObjectAnimation(Thread_t* thread, uint32_t objectHandle,
                                         uint32_t targetEffect, uint32_t duration)
{
	DisplayObject_t* object = Object_Resolve(objectHandle);
	if(object == NULL)
		return NULL;

	Process_t* process = (Process_t*)malloc(sizeof(Process_t));
	if(process == NULL)
		return NULL;
	process->kind = PROCESS_OBJECT_ANIMATION;
	process->thread = thread;
	process->deadline = 0;
	process->aborted = 0;
	process->events = NULL;
	process->eventsTail = NULL;
	process->allowKey = 0;
	process->keyMask = 0;
	process->stopped = 0;

	process->objectHandle = objectHandle;
	/* 0x00431E7F: a duration of zero is taken as one, so the first pass ends it. */
	process->duration = (duration == 0) ? 1 : duration;
	process->startTime = OS_GetTicks();
	process->elapsed = 0;
	process->frames = 0;
	process->stopCode = 0;
	process->stepDeadline = process->startTime;
	/* 0x00431EBA reads the level back through vtable+0x4C (0x0041B720) before it
	   works out how far it has to move. */
	process->startEffect = (int32_t)object->unknownAC;
	process->deltaEffect = (int32_t)targetEffect - process->startEffect;
	process->lastEffect = 0;
	process->haveLast = 0;

	/* 0x00431F05: vtable+0x04 with 1. An object being animated is made visible. */
	Object_ApplyVisible(object, 1);
	return process;
}

void Process_PostEvent(Process_t* process, uint32_t a, uint32_t b, uint32_t c)
{
	ProcessEvent_t* event = (ProcessEvent_t*)malloc(sizeof(ProcessEvent_t));
	if(event == NULL)
		return;
	event->value[0] = a;
	event->value[1] = b;
	event->value[2] = c;
	event->next = NULL;
	if(process->eventsTail == NULL)
		process->events = event;
	else
		process->eventsTail->next = event;
	process->eventsTail = event;
}

/* 0x00431BD0: every event posted since the last pass is taken and dispatched.
   An event of 0 aborts the process outright (0x00431AE0); the wait's own
   handler (0x0043D590) takes an event of 1 as "stop waiting". */
static void Process_TakeEvents(Process_t* process)
{
	while(process->events != NULL)
	{
		ProcessEvent_t* event = process->events;
		process->events = event->next;
		if(process->events == NULL)
			process->eventsTail = NULL;

		if(event->value[0] == 0)
			process->aborted = 1;
		else if(event->value[0] == 1)
			process->stopped = 1;
		free(event);
	}
}

static void Process_Finish(Process_t* process, uint32_t result)
{
	Thread_PushStack(process->thread, result);
}

static int Process_RunWaitTiming(Process_t* process)
{
	Process_TakeEvents(process);

	/* 0x00431B40: the tick count against the deadline. */
	if((int32_t)(OS_GetTicks() - process->deadline) >= 0)
	{
		Process_Finish(process, 0);
		return 1;
	}
	/* 0x00431AA0: the engine still running, and the process not aborted. */
	if(!process->thread->engine->isRunning || process->aborted)
	{
		Process_Finish(process, 0);
		return 1;
	}
	if(process->stopped)
	{
		Process_Finish(process, 0);
		return 1;
	}
	if(process->allowKey != 0)
	{
		/* 0x0043D53D asks 0x0046E080 whether any key in the mask is down, and
		   ors in the modifier state at 0x00507690. Neither is written: the
		   engine keeps no keyboard state yet, so say so rather than answer. */
		printf("[Process]: a wait that a key may cut short (mask 0x%.8X) is not written yet (0x0046E080); waiting out its delay\n",
		       process->keyMask);
		process->allowKey = 0;
	}
	return 0;
}

/*
 * 0x00432240, the animation's step (its vtable+0x1C). It answers whether the
 * animation is finished, and applies the values it has reached on the way.
 */
static int Process_StepObjectAnimation(Process_t* process, DisplayObject_t* object)
{
	int done = 0;

	if(process->stopCode != 0)
	{
		/* 0x004322EC: something stopped it, so it lands on its target at once. */
		process->elapsed = process->duration;
		done = 1;
	}
	else
	{
		/* 0x00432265: the elapsed time since it started, clamped to the duration,
		   and reaching the duration is what finishes it. */
		uint32_t elapsed = OS_GetTicks() - process->startTime;
		if(elapsed >= process->duration)
		{
			elapsed = process->duration;
			done = 1;
		}
		process->elapsed = elapsed;
	}

	int32_t effect;
	if(done)
	{
		/* 0x004322C4: the end is the sum, not the interpolation at t = 1. */
		effect = process->startEffect + process->deltaEffect;
	}
	else
	{
		/* 0x004322FC: t is (elapsed << 24) / duration, curve 0 turns that into the
		   16.16 fraction the deltas are weighted by (0x0041AB1C: t >> 8, rounded
		   towards zero), and 0x0043237E adds the weighted delta to the start. */
		int64_t t = ((int64_t)process->elapsed << 24) / (int64_t)process->duration;
		int64_t eased = t >> 8;
		effect = process->startEffect
		       + (int32_t)(((int64_t)process->deltaEffect * eased) >> 16);
	}

	/* 0x004323BF: nothing is touched while the values have not moved. */
	if(!process->haveLast || effect != process->lastEffect)
	{
		process->lastEffect = effect;
		process->haveLast = 1;
		/* vtable+0x48, which Object_ApplyEffectLevel is. The two calls to
		   vtable+0x0C that bracket it are the object's damage notification, which
		   that already does. */
		const char* unread = Object_ApplyEffectLevel(object, (uint32_t)effect);
		if(unread != NULL)
			printf("[Process]: an animated object's effect level needs %s, which is not written yet\n",
			       unread);
	}

	return done;
}

static int Process_RunObjectAnimation(Process_t* process)
{
	/* 0x00431FEA: the object is looked up again every pass, and it going away
	   under the animation is fatal in the original (0x004E517C). */
	DisplayObject_t* object = Object_Resolve(process->objectHandle);
	if(object == NULL)
	{
		printf("[Process]: the object 0x%.8X an animation was running on no longer exists\n",
		       process->objectHandle);
		Thread_PushStack(process->thread, 0);
		Thread_PushStack(process->thread, 0xFFFFFFFF);
		return 1;
	}

	Process_TakeEvents(process);
	if(process->stopped)
	{
		/* 0x00432190, the animation's own event handler, is not read: what an
		   event other than the base's abort means to an animation is unknown, so
		   say so rather than decide. Nothing posts one today. */
		printf("[Process]: an event was posted to an animation; its handler (0x00432190) is not written yet\n");
		process->stopped = 0;
	}

	/* 0x004320FC -> 0x00431AA0. When it says stop, the animation ends where it
	   stands and the thread is told so with -1 (0x004320F5). */
	if(!process->thread->engine->isRunning || process->aborted)
	{
		Thread_PushStack(process->thread, 0);
		Thread_PushStack(process->thread, 0xFFFFFFFF);
		return 1;
	}

	/* 0x0043210C -> 0x00431B40: the step runs at most once a millisecond, which
	   is what the step's own tail call to vtable+0x08 with 1 (0x0043242D) arms. */
	uint32_t now = OS_GetTicks();
	if((int32_t)(now - process->stepDeadline) < 0 && process->stopCode == 0)
		return 0;

	int done = Process_StepObjectAnimation(process, object);
	process->stepDeadline = OS_GetTicks() + 1;
	/* 0x00432154: the frame counter is what the animation reports back. */
	process->frames++;

	if(!done)
		return 0;

	/* 0x0043215B: the frames it managed over the duration, as frames a second,
	   and then whether anything cut it short (0x0043212F). */
	Thread_PushStack(process->thread, process->frames * 1000 / process->duration);
	Thread_PushStack(process->thread, (process->stopCode != 0) ? 1 : 0);
	return 1;
}

int Process_Run(Process_t* process)
{
	switch(process->kind)
	{
		case PROCESS_WAIT_TIMING:
			return Process_RunWaitTiming(process);
		case PROCESS_OBJECT_ANIMATION:
			return Process_RunObjectAnimation(process);
	}
	return 1;
}

void Process_Destroy(Process_t* process)
{
	while(process->events != NULL)
	{
		ProcessEvent_t* event = process->events;
		process->events = event->next;
		free(event);
	}
	free(process);
}
