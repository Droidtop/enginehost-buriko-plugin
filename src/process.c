#include <stdio.h>
#include <stdlib.h>

#include "process.h"
#include "thread.h"
#include "engine.h"
#include "object.h"
#include "os.h"
#include "input.h"
#include "region.h"
#include <math.h>

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
 * The buttons a key press on an animation's region is counted over (0x00507690):
 * Sys1 0x1F writes it (0x0048B960 -> 0x0046E720 -> 0x00431AD0) and the image starts
 * it at 0x2000. The animation ors in 0x180 (0x00431FC0).
 */
uint32_t gProcessKeyButtons = 0x2000;

/*
 * 0x0041A760: t runs from 0 to 1 << 24 and the answer from 0 to 0x10000. Curve 0,
 * and anything above 15, is the default arm at 0x0041AB1C: t / 256, rounded towards
 * zero. The rest are the table at 0x0041AB2C; the angles are in 1/65536 degrees and
 * every result is truncated (0x004AD5F0 is cvttsd2si).
 */
int32_t Process_Ease(uint32_t curve, int32_t t)
{
	const double pi = 3.141592653589793;       // 0x004EC8F8
	const double degrees = 11796480.0;          // 0x004EC908, 180 << 16
	const double one = 16777216.0;              // 0x004EC950
	switch(curve)
	{
	case 1:
	{
		// 0x0041A77D: (cos(180 degrees * (1 - u)) + 1) * 32768, slow at both ends.
		int32_t a = (int32_t)(((int64_t)t * 0xB4) / 0x100);
		return (int32_t)((cos((double)(0xB40000 - a) * pi / degrees) + 1.0) * 32768.0);
	}
	case 2:
	{
		// 0x0041A7CD: sin(90 degrees * u), fast then slow.
		int32_t a = (int32_t)(((int64_t)t * 0x5A) / 0x100);
		return (int32_t)(sin((double)a * pi / degrees) * 65536.0);
	}
	case 3:
	{
		// 0x0041A80D: 1 - sin(90 degrees * (1 - u)), slow then fast.
		int32_t a = (int32_t)(((int64_t)t * 0x5A) / 0x100);
		return (int32_t)((1.0 - sin((double)(0x5A0000 - a) * pi / degrees)) * 65536.0);
	}
	default:
		break;
	}
	if(curve >= 4 && curve <= 15)
	{
		// 0x0041A858 onwards, in pairs: the even curve is u^p (slow then fast), the odd
		// one after it 1 - (1 - u)^p (fast then slow), for p = 2, 2.5, 3, 4, 5 and 6
		// (0x004EC8B8, 0x004EC948, 0x004EC8B0, 0x004EC910, 0x004EC940, 0x004EC938).
		static const double powers[6] = { 2.0, 2.5, 3.0, 4.0, 5.0, 6.0 };
		double p = powers[(curve - 4) / 2];
		if((curve & 1) == 0)
			return (int32_t)(pow((double)t, p) * 65536.0 / pow(one, p));
		return (int32_t)((1.0 - pow((double)(0x1000000 - t), p) / pow(one, p)) * 65536.0);
	}
	return t / 256;
}

/*
 * 0x00431CB0 (the constructor), 0x00431E70 (the start) and 0x00431F60 (the skip
 * registration), as 0x00491CD0 and 0x00491EF0 call them. The constructor keeps the
 * handle and the display object is looked up again on every pass.
 */
Process_t* Process_CreateObjectAnimation(Thread_t* thread, uint32_t objectHandle,
                                         const ObjectAnimation_t* animation,
                                         uint32_t allowSkip, uint32_t priority)
{
	DisplayObject_t* object = Object_Resolve(objectHandle);
	if(object == NULL)
		return NULL;

	Process_t* process = (Process_t*)calloc(1, sizeof(Process_t));
	if(process == NULL)
		return NULL;
	process->kind = PROCESS_OBJECT_ANIMATION;
	process->thread = thread;
	process->objectHandle = objectHandle;

	// 0x00431E70.
	process->elapsed = 0;
	process->duration = animation->duration == 0 ? 1 : animation->duration;
	Object_GetPosition(object, &process->startX, &process->startY);   // vtable+0x30
	process->deltaX = animation->x - process->startX;
	process->deltaY = animation->y - process->startY;
	process->positionCurve = animation->positionCurve;
	process->startLevel = (int32_t)Object_GetEffectLevel(object);      // vtable+0x4C
	process->deltaLevel = animation->level - process->startLevel;
	process->levelCurve = animation->levelCurve;
	process->startEffect2 = (int32_t)Object_GetEffect2(object);        // 0x0041B810
	// 0x00431EDF: a negative target leaves the parameter where it is.
	process->deltaEffect2 = animation->effect2 < 0 ? 0 : animation->effect2 - process->startEffect2;
	process->last[0] = process->last[1] = (int32_t)0x80000000;
	process->last[2] = process->last[3] = -1;
	// 0x00431F05 / 0x00431F11: vtable+0x04 with 1, the object is made visible, and
	// vtable+0x0C, its damage notification; Object_ApplyVisible is both.
	Object_ApplyVisible(object, 1);
	process->fps = animation->fps;
	process->frameStep = animation->frameStep;
	process->frames = 0;
	process->startTime = OS_GetTicks();
	process->frameInterval = process->frameStep * 1000 / process->fps;
	process->nextFrame = process->frameInterval;
	// 0x00431F53: vtable+0x08 with 1 (0x00431AF0), the first pass a millisecond on.
	process->deadline = OS_GetTicks() + 1;

	// 0x00431F60: with skipping allowed, the region (priority << 16 | 0xFFFF) goes on
	// both lists (0x0046D840 over the whole plane, 0x0046D8A0 for the keyboard), and
	// the button totals are taken as they stand, so only presses from now on count.
	process->allowSkip = allowSkip;
	if(allowSkip)
	{
		uint32_t key = REGION_KEY(priority);
		process->priority = priority;
		Region_Add(0, key, (int32_t)0x80000000, (int32_t)0x80000000, (int32_t)0x7FFFFFFF, (int32_t)0x7FFFFFFF, 0);
		Region_Add(1, key, 0, 0, 0, 0, 0);
		process->mouseTotal = Input_ButtonTotals(1);
		process->keyTotal = Input_ButtonTotals(gProcessKeyButtons | 0x180);
		process->keyRegistered = 1;
	}
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
	if(!process->thread->engine->isRunning || !gProcessesWait || process->aborted)
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
	if(process->stopCode != 0 || process->stopRequested)
	{
		// 0x004322EC: something stopped it, so it lands on its target at once.
		process->elapsed = process->duration;
	}
	else
	{
		uint32_t elapsed = OS_GetTicks() - process->startTime;
		if(process->frameStep == 0)
			process->elapsed = elapsed;
		else
		{
			// 0x0043227B: with a frame step the clock moves at most to the next frame
			// boundary per pass, and the boundary is re-armed from where it landed.
			process->elapsed = elapsed < process->nextFrame ? elapsed : process->nextFrame;
			process->nextFrame = process->frameInterval + process->elapsed;
		}
		// 0x0043229C: clamped to the duration (compared signed).
		if((int32_t)process->elapsed >= (int32_t)process->duration)
			process->elapsed = process->duration;
	}
	int done = process->elapsed == process->duration;

	int32_t x, y, level, effect2;
	if(done)
	{
		// 0x004322C4: the end is the sum, not the interpolation at t = 1.
		x = process->startX + process->deltaX;
		y = process->startY + process->deltaY;
		level = process->startLevel + process->deltaLevel;
		effect2 = (int32_t)((uint32_t)(process->startEffect2 + process->deltaEffect2) << 16);
	}
	else
	{
		// 0x004322FC: t = (elapsed << 24) / duration, eased per channel; the position
		// and level deltas are weighted by the 16.16 curve value, and the second
		// effect parameter moves linearly in 16.16.
		int64_t duration = (int64_t)(int32_t)process->duration;
		int32_t t = (int32_t)(((int64_t)(int32_t)process->elapsed << 24) / duration);
		int64_t pe = Process_Ease(process->positionCurve, t);
		x = process->startX + (int32_t)(((int64_t)process->deltaX * pe) >> 16);
		y = process->startY + (int32_t)(((int64_t)process->deltaY * pe) >> 16);
		int64_t le = Process_Ease(process->levelCurve, t);
		level = process->startLevel + (int32_t)(((int64_t)process->deltaLevel * le) >> 16);
		int32_t product = process->deltaEffect2 * (int32_t)process->elapsed;
		effect2 = (int32_t)((uint32_t)process->startEffect2 << 16)
		        + (int32_t)(((int64_t)product << 16) / duration);
	}

	// 0x004323BF: nothing is touched while none of the four has moved. Then the
	// object's vtable+0x2C, +0x48 and +0x50 (mode 1, the whole 16.16 value),
	// bracketed by its damage notification.
	if(x != process->last[0] || y != process->last[1] || level != process->last[2] || effect2 != process->last[3])
	{
		process->last[0] = x;
		process->last[1] = y;
		process->last[2] = level;
		process->last[3] = effect2;
		int drawable = Object_IsDrawable(object);
		if(drawable)
			gObjectDamage++;
		Object_Move(object, x, y);
		const char* unread = Object_ApplyEffectLevel(object, (uint32_t)level);
		if(unread != NULL)
			printf("[Process]: an animated object's effect level needs %s, which is not written yet\n", unread);
		unread = Object_SetEffect2(object, 1, (uint32_t)effect2);
		if(unread != NULL)
			printf("[Process]: an animated object's second effect parameter needs %s, which is not written yet\n", unread);
		if(drawable || Object_IsDrawable(object))
			gObjectDamage++;
	}
	// 0x00432428: vtable+0x08 with 1, the next pass a millisecond on.
	process->deadline = OS_GetTicks() + 1;
	return done;
}

/*
 * 0x00431FE0, the animation's run (its vtable+0x04).
 */
static int Process_RunObjectAnimation(Process_t* process)
{
	// 0x00431FF4: the object is looked up again every pass, and it going away under
	// the animation is fatal in the original (0x004E517C).
	DisplayObject_t* object = Object_Resolve(process->objectHandle);
	if(object == NULL)
	{
		printf("[Process]: Error: the object 0x%.8X was deleted while an animation controlled it\n",
		       process->objectHandle);
		Thread_PushStack(process->thread, 0);
		Thread_PushStack(process->thread, 0xFFFFFFFF);
		return 1;
	}

	// 0x00432010 -> 0x00431BD0: every posted event, through 0x00432190. An event of
	// 0 aborts it (0x00431AE0); one of 1 asks it to stop when its second value is
	// set or skipping is allowed.
	while(process->events != NULL)
	{
		ProcessEvent_t* event = process->events;
		process->events = event->next;
		if(process->events == NULL)
			process->eventsTail = NULL;
		if(event->value[0] == 0)
			process->aborted = 1;
		else if(event->value[0] == 1 && (event->value[1] != 0 || process->allowSkip != 0))
			process->stopRequested = 1;
		free(event);
	}

	process->stopCode = 0;
	if(process->allowSkip)
	{
		// 0x00432029: a new click while the region has had the pointer since the last
		// pass stops it with 1, a new key press while it has had the keyboard with
		// 0x100, and the skip keys with 0x80000000; any of them takes the region's
		// presses (0x0046E080).
		uint32_t key = REGION_KEY(process->priority);
		uint32_t mouseTotal = Input_ButtonTotals(1);
		int hasMouse = Input_HasMouse(key);
		if(hasMouse && process->hadMouse && mouseTotal > process->mouseTotal)
			process->stopCode = 1;
		uint32_t keyTotal = Input_ButtonTotals(gProcessKeyButtons | 0x180);
		int hasKeyboard = Input_HasKeyboard(key);
		if(hasKeyboard && process->hadKeyboard && keyTotal > process->keyTotal)
			process->stopCode = 0x100;
		if(Input_SkipQuery())
			process->stopCode = 0x80000000u;
		if(process->stopCode != 0)
			Input_RegionState(key);
		process->hadMouse = hasMouse;
		process->hadKeyboard = hasKeyboard;
		process->mouseTotal = mouseTotal;
		process->keyTotal = keyTotal;
	}

	// 0x004320FC -> 0x00431AA0: processes may wait (0x00507688) and this one is not
	// aborted. When not, it ends where it stands with -1 (0x0043215B with -1).
	if(!process->thread->engine->isRunning || !gProcessesWait || process->aborted)
	{
		Thread_PushStack(process->thread, process->frames * 1000 / process->duration);
		Thread_PushStack(process->thread, 0xFFFFFFFF);
		return 1;
	}

	// 0x0043210A -> 0x00431B40: a step only once its deadline has come, unless it
	// has been told to stop.
	if((int32_t)(OS_GetTicks() - process->deadline) < 0 && process->stopCode == 0 && !process->stopRequested)
		return 0;

	int done = Process_StepObjectAnimation(process, object);
	uint32_t result = (process->stopCode != 0 || process->stopRequested) ? 1 : 0;
	// 0x00432154: the frame counter.
	process->frames++;
	if(!done)
		return 0;

	// 0x0043215B: the frames it managed over the duration, as frames a second, and
	// then whether anything cut it short.
	Thread_PushStack(process->thread, process->frames * 1000 / process->duration);
	Thread_PushStack(process->thread, result);
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
		case PROCESS_CALLBACK:
			return process->callbackRun(process->callbackContext);
	}
	return 1;
}

Process_t* Process_CreateCallback(Thread_t* thread, ProcessCallbackRun_t run,
                                  ProcessCallbackFree_t destroy, void* context)
{
	Process_t* process = (Process_t*)calloc(1, sizeof(Process_t));
	if(process == NULL)
		return NULL;
	process->kind = PROCESS_CALLBACK;
	process->thread = thread;
	process->callbackRun = run;
	process->callbackFree = destroy;
	process->callbackContext = context;
	return process;
}

void Process_Destroy(Process_t* process)
{
	if(process->kind == PROCESS_CALLBACK && process->callbackFree != NULL)
		process->callbackFree(process->callbackContext);
	// 0x00431D70: an animation that registered its region takes it off both lists.
	if(process->kind == PROCESS_OBJECT_ANIMATION && process->keyRegistered)
	{
		Region_RemoveByKey(0, REGION_KEY(process->priority));
		Region_RemoveByKey(1, REGION_KEY(process->priority));
	}
	while(process->events != NULL)
	{
		ProcessEvent_t* event = process->events;
		process->events = event->next;
		free(event);
	}
	free(process);
}
