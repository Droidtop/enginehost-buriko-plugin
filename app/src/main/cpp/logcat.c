/*
 * The engine's own output, on the console.
 *
 * OpenBGI says everything it has to say with printf and fprintf(stderr, ...) -
 * three hundred and eighty-nine places at the time of writing - which is what an
 * engine that grew up on a desktop does. Android has no console behind those two
 * descriptors and discards whatever is written to them: dq-buriko-01 was a run in
 * which the engine ran for thirteen and a half seconds and returned cleanly while
 * not one of its lines could be found in logcat, so the run could answer none of
 * the three questions it had been queued to answer.
 *
 * This is the wrapper's side of that, the same job CatSystem2's cs2_log_to does
 * for an engine that has a single logging function to hand a sink to. This engine
 * has no such function, so the sink goes one level lower: standard output and
 * standard error become a pipe, and a thread reads whole lines out of it and hands
 * each one to __android_log_print. No call site moves and nothing in src/ changes,
 * which keeps the engine tree portable and upstream-shaped.
 *
 * It runs as a load-time constructor because SDL calls SDL_main directly: there is
 * no wrapper entry point to put it in, and by the time main() runs the engine has
 * already printed its version banner.
 */

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logcat.h"

#define TAG "openbgi"

static int gPipe[2] = { -1, -1 };

// The engine stops its script with a line of its own and then returns as if
// the game had ended: "[Engine]: Stub opcode 0x9195 encountered in
// scrmsg._bp+0x066B. Stopping." (Fureraba, Droidtop/tracker#604). The wrapper
// keeps that line so the person is told it, not shown a game that closed.
#define STOP_START "[Engine]: "
#define STOP_END ". Stopping."

// A line only LogcatDrain writes: once the reader has it, it has every line
// written before it, since a pipe keeps order.
#define DRAIN_MARK "\001enginehost-drain"

static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gMarkRead = PTHREAD_COND_INITIALIZER;
static unsigned gMarks;
static char gStopped[256];

// A whole line at a time, because that is what a log a host keeps - Android's
// among them - is made of. The engine's lines end in \n, and a few of the
// original's own strings carry a \r as well.
static void EmitLine(const char* text, size_t length)
{
	char line[1024];
	while(length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n'))
		length--;
	if(length > sizeof(line) - 1)
		length = sizeof(line) - 1;
	memcpy(line, text, length);
	line[length] = '\0';
	if(strcmp(line, DRAIN_MARK) == 0)
	{
		pthread_mutex_lock(&gLock);
		gMarks++;
		pthread_cond_broadcast(&gMarkRead);
		pthread_mutex_unlock(&gLock);
		return;
	}
	size_t start = strlen(STOP_START), end = strlen(STOP_END);
	if(length > start + end && strncmp(line, STOP_START, start) == 0 && strcmp(line + length - end, STOP_END) == 0)
	{
		pthread_mutex_lock(&gLock);
		snprintf(gStopped, sizeof(gStopped), "%.*s", (int)(length - start - end), line + start);
		pthread_mutex_unlock(&gLock);
	}
	__android_log_print(ANDROID_LOG_INFO, TAG, "%s", line);
}

void LogcatDrain(void)
{
	if(gPipe[1] < 0)
		return;
	pthread_mutex_lock(&gLock);
	unsigned want = gMarks + 1;
	pthread_mutex_unlock(&gLock);
	static const char mark[] = DRAIN_MARK "\n";
	if(write(STDOUT_FILENO, mark, sizeof(mark) - 1) != (ssize_t)(sizeof(mark) - 1))
		return;
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 1;
	pthread_mutex_lock(&gLock);
	while(gMarks < want)
	{
		if(pthread_cond_timedwait(&gMarkRead, &gLock, &deadline) == ETIMEDOUT)
			break;
	}
	pthread_mutex_unlock(&gLock);
}

int LogcatStopReason(char* out, size_t size)
{
	pthread_mutex_lock(&gLock);
	snprintf(out, size, "%s", gStopped);
	pthread_mutex_unlock(&gLock);
	// It goes to Java through NewStringUTF; a script name in Shift-JIS is not UTF-8.
	for(char* c = out; *c; c++)
		if((unsigned char)*c >= 0x80 || (unsigned char)*c < 0x20)
			*c = '?';
	return out[0] != '\0';
}

static void* Pump(void* unused)
{
	// Lengths rather than strings throughout: a NUL that found its way into the
	// stream would otherwise stop the search for the next newline and the pump
	// would go quiet for the rest of the run.
	char buffer[1024];
	size_t held = 0;
	(void)unused;

	for(;;)
	{
		ssize_t got = read(gPipe[0], buffer + held, sizeof(buffer) - held);
		if(got < 0)
		{
			if(errno == EINTR)
				continue;
			break;
		}
		if(got == 0)
			break;
		held += (size_t)got;

		size_t start = 0;
		for(;;)
		{
			char* found = memchr(buffer + start, '\n', held - start);
			if(found == NULL)
				break;
			size_t end = (size_t)(found - buffer);
			EmitLine(buffer + start, end - start);
			start = end + 1;
		}
		held -= start;
		memmove(buffer, buffer + start, held);

		// A line longer than the buffer is cut rather than split, so nothing
		// arrives looking like two lines.
		if(held == sizeof(buffer))
		{
			EmitLine(buffer, held);
			held = 0;
		}
	}
	return NULL;
}

__attribute__((constructor)) static void RouteOutputToLogcat(void)
{
	pthread_t thread;

	if(pipe(gPipe) != 0)
	{
		__android_log_print(ANDROID_LOG_ERROR, TAG,
			"the engine's output could not be routed to logcat: %s", strerror(errno));
		return;
	}

	// The reader is started before the descriptors are moved. The other order
	// leaves the engine writing into a pipe nobody is reading, and it would stop
	// dead on a full one the first time it had a page of output to give.
	if(pthread_create(&thread, NULL, Pump, NULL) != 0)
	{
		__android_log_print(ANDROID_LOG_ERROR, TAG,
			"the engine's output could not be routed to logcat: no thread");
		close(gPipe[0]);
		close(gPipe[1]);
		gPipe[0] = gPipe[1] = -1;
		return;
	}
	pthread_detach(thread);

	// Unbuffered, because a pipe is not a terminal: with the block buffering the
	// C library chooses for one, a line would sit in the library until four
	// kilobytes had gathered behind it, and a run that stops or dies before that
	// would show nothing at all - which is the failure this whole file is for.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if(dup2(gPipe[1], STDOUT_FILENO) < 0 || dup2(gPipe[1], STDERR_FILENO) < 0)
		__android_log_print(ANDROID_LOG_ERROR, TAG,
			"the engine's output could not be routed to logcat: %s", strerror(errno));
	else
		__android_log_print(ANDROID_LOG_INFO, TAG,
			"the engine's output is routed to logcat under the tag \"%s\"", TAG);
}
