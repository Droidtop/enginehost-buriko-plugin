#include <stdio.h>
#include <stdlib.h>

#include "process.h"
#include "thread.h"
#include "engine.h"
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

int Process_Run(Process_t* process)
{
	switch(process->kind)
	{
		case PROCESS_WAIT_TIMING:
			return Process_RunWaitTiming(process);
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
