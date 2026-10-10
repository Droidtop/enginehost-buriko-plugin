/*
 * OpenBGI's OS layer (src/os.h) for Enginehost's sandbox, in place of the SDL
 * one in src/os.c, plus the JNI the plugin (BurikoPlugin.java) drives it with.
 *
 * Enginehost runs the engine in an android:isolatedProcess service
 * (Enginehost docs/engine-sandbox.md): no window, no GPU, no audio output, no
 * storage permission. So, exactly as the engine's own layering already allows:
 *
 * - main() runs on its own thread, handed the game folder. Its files come
 *   through Enginehost's file layer (plugin-native/enginehost_vfs_forward.c,
 *   linked into this library), which serves the game folder by its real path.
 * - OS_Present keeps the composed frame in memory; the plugin's step() copies
 *   the newest one to the host, which draws it scaled to the screen.
 * - The sound output is a thread here that runs the engine's mixer and writes
 *   into the host's audio ring (plugin-native/enginehost_audio_ring.c), with a
 *   movie's own sound mixed in at the ring's rate.
 * - Input arrives from the plugin as Windows virtual keys and mouse events and
 *   reaches the engine on its own thread, in OS_Poll, the way the SDL layer
 *   hands over SDL's events.
 *
 * Time, sleeping and memory are the same as os.c's; SDL is still linked for
 * the engine's threads, atomics and surfaces, none of which needs a device.
 */
#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "enginehost_audio_ring.h"
#include "engine.h"
#include "input.h"
#include "os.h"

#define TAG "openbgi"

int SDL_main(int argc, char** argv);

/* The same globals os.c defines; input.c and icon.c read gAppActive. */
int gAppActive = 1;
int gReadKeysWhenInactive = 0;
int gSwapMouseButtons = 0;

static Engine_t* gOsEngine = NULL;
static uint8_t gVkHeld[256];

/* ---- time and memory, as os.c ------------------------------------------ */

static uint64_t MonotonicMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static uint64_t gStartMs;

uint32_t OS_GetTicks()
{
	return (uint32_t)(MonotonicMs() - gStartMs);
}

void OS_Sleep(uint32_t milliseconds)
{
	struct timespec wait = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
	while(nanosleep(&wait, &wait) != 0 && errno == EINTR) {}
}

// The host draws at the display's rate; 60 Hz is what every BGI game assumes.
uint32_t OS_FrameInterval()
{
	return 1000 / 60;
}

void OS_GetPhysicalMemory(uint64_t* total, uint64_t* available)
{
	long pageSize = sysconf(_SC_PAGESIZE);
	long totalPages = sysconf(_SC_PHYS_PAGES);
	long freePages = sysconf(_SC_AVPHYS_PAGES);
	*total = (pageSize > 0 && totalPages > 0) ? (uint64_t)pageSize * (uint64_t)totalPages : 0;
	*available = (pageSize > 0 && freePages > 0) ? (uint64_t)pageSize * (uint64_t)freePages : 0;
}

/* ---- keys, as os.c ------------------------------------------------------ */

void OS_SetVkHeld(uint32_t vk, int held)
{
	gVkHeld[vk & 0xFF] = held ? 1 : 0;
}

int OS_IsKeyDown(uint32_t vk)
{
	if(!gAppActive && !gReadKeysWhenInactive)
		return 0;
	vk &= 0xFF;
	if(vk == 0x10) return gVkHeld[0x10] || gVkHeld[0xA0] || gVkHeld[0xA1];
	if(vk == 0x11) return gVkHeld[0x11] || gVkHeld[0xA2] || gVkHeld[0xA3];
	if(vk == 0x12) return gVkHeld[0x12] || gVkHeld[0xA4] || gVkHeld[0xA5];
	return gVkHeld[vk] != 0;
}

/* ---- input, from the plugin's thread to the engine's -------------------- */

enum { EVENT_KEY, EVENT_MOVE, EVENT_BUTTON, EVENT_WHEEL, EVENT_QUIT };

typedef struct
{
	int type;
	int a, b;
	int32_t x, y;
} OsEvent_t;

#define EVENT_CAPACITY 256
static OsEvent_t gEvents[EVENT_CAPACITY];
static int gEventHead, gEventCount;
static pthread_mutex_t gEventLock = PTHREAD_MUTEX_INITIALIZER;
static int32_t gPointerX, gPointerY;
static int gLeftDown;

static void PushEvent(int type, int a, int b, int32_t x, int32_t y)
{
	pthread_mutex_lock(&gEventLock);
	if(gEventCount < EVENT_CAPACITY)
	{
		OsEvent_t* e = &gEvents[(gEventHead + gEventCount) % EVENT_CAPACITY];
		e->type = type;
		e->a = a;
		e->b = b;
		e->x = x;
		e->y = y;
		gEventCount++;
	}
	pthread_mutex_unlock(&gEventLock);
}

int OS_Poll()
{
	for(;;)
	{
		OsEvent_t e;
		pthread_mutex_lock(&gEventLock);
		if(gEventCount == 0)
		{
			pthread_mutex_unlock(&gEventLock);
			return 0;
		}
		e = gEvents[gEventHead];
		gEventHead = (gEventHead + 1) % EVENT_CAPACITY;
		gEventCount--;
		pthread_mutex_unlock(&gEventLock);

		switch(e.type)
		{
			case EVENT_KEY:
				// The modifier names its left half too, as a keyboard's left key would.
				if(e.a == 0x10) OS_SetVkHeld(0xA0, e.b);
				if(e.a == 0x11) OS_SetVkHeld(0xA2, e.b);
				if(e.a == 0x12) OS_SetVkHeld(0xA4, e.b);
				OS_SetVkHeld((uint32_t)e.a, e.b);
				if(e.b)
					Input_KeyDown((uint32_t)e.a);
				else
					Input_KeyUp((uint32_t)e.a);
				break;
			case EVENT_MOVE:
				Input_MouseMove(e.x, e.y);
				break;
			case EVENT_BUTTON:
			{
				static const uint32_t vks[5] = { 0x01, 0x02, 0x04, 0x05, 0x06 };
				if(e.a >= 0 && e.a < 5)
				{
					OS_SetVkHeld(vks[e.a], e.b);
					Input_MouseButton(e.a, e.b, e.x, e.y);
				}
				break;
			}
			case EVENT_WHEEL:
				Input_MouseWheel(e.a);
				break;
			case EVENT_QUIT:
				if(gOsEngine != NULL)
					gOsEngine->isRunning = 0;
				break;
		}
	}
}

int OS_Init(Engine_t* engine)
{
	gOsEngine = engine;
	return 0;
}

int OS_Quit()
{
	return 0;
}

// The host owns the screen and scales the frame to it.
void OS_SetWindowSize(int width, int height)
{
	(void)width;
	(void)height;
}

/* ---- frames -------------------------------------------------------------- */

static pthread_mutex_t gFrameLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gFrameReady = PTHREAD_COND_INITIALIZER;
static uint32_t* gFrame;
static int gFrameWidth, gFrameHeight;
static int gFrameNew;
static int gEnded;
static int gExitCode;

/*
 * The frame's size is the first one the engine shows, which the host sizes
 * its picture by once. A later frame of another size (a display mode change
 * after the first frame) is scaled into it.
 */
void OS_Present(const uint8_t* pixels, int width, int height, int stride)
{
	if(pixels == NULL || width <= 0 || height <= 0)
		return;
	pthread_mutex_lock(&gFrameLock);
	if(gFrame == NULL)
	{
		gFrame = (uint32_t*)malloc((size_t)width * (size_t)height * 4);
		if(gFrame == NULL)
		{
			pthread_mutex_unlock(&gFrameLock);
			return;
		}
		gFrameWidth = width;
		gFrameHeight = height;
		__android_log_print(ANDROID_LOG_INFO, TAG, "first frame %dx%d", width, height);
	}
	// B, G, R, x in memory is 0xxxRRGGBB as a little-endian word: Android's
	// ARGB_8888 once the alpha is made opaque.
	for(int y = 0; y < gFrameHeight; y++)
	{
		int sy = height == gFrameHeight ? y : (int)((int64_t)y * height / gFrameHeight);
		const uint32_t* in = (const uint32_t*)(const void*)(pixels + (size_t)sy * (size_t)stride);
		uint32_t* out = gFrame + (size_t)y * (size_t)gFrameWidth;
		if(width == gFrameWidth)
			for(int x = 0; x < gFrameWidth; x++)
				out[x] = in[x] | 0xFF000000u;
		else
			for(int x = 0; x < gFrameWidth; x++)
				out[x] = in[(int64_t)x * width / gFrameWidth] | 0xFF000000u;
	}
	gFrameNew = 1;
	pthread_cond_broadcast(&gFrameReady);
	pthread_mutex_unlock(&gFrameLock);
}

/* ---- sound --------------------------------------------------------------- */

static struct enginehost_audio_ring gRing;
static int gRingRate;
static pthread_mutex_t gAudioLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t gAudioThread;
static int gAudioRunning;
static volatile int gAudioPaused = 1;
static volatile int gAudioStop;
static OS_AudioFill gAudioFill;
static int gAudioFrames = 1024;

// A movie's sound: interleaved samples at its own rate, mixed in by the thread below.
static pthread_mutex_t gMovieLock = PTHREAD_MUTEX_INITIALIZER;
static int16_t* gMovieQueue;
static size_t gMovieQueued, gMovieCapacity;   // in samples
static int gMovieRate, gMovieChannels, gMovieOpen;
static volatile int gMoviePaused = 1;
static double gMoviePosition;                 // in frames, from the front of the queue

static void MixMovie(int16_t* out, int frames)
{
	pthread_mutex_lock(&gMovieLock);
	if(!gMovieOpen || gMoviePaused || gMovieRate <= 0 || gMovieChannels <= 0)
	{
		pthread_mutex_unlock(&gMovieLock);
		return;
	}
	double step = (double)gMovieRate / (double)gRingRate;
	size_t available = gMovieQueued / (size_t)gMovieChannels;
	int c = gMovieChannels;
	for(int i = 0; i < frames; i++)
	{
		size_t index = (size_t)gMoviePosition;
		if(index + 1 >= available)
			break;
		double t = gMoviePosition - (double)index;
		const int16_t* a = gMovieQueue + index * (size_t)c;
		const int16_t* b = a + c;
		float left = (float)(a[0] + (b[0] - a[0]) * t);
		float right = c > 1 ? (float)(a[1] + (b[1] - a[1]) * t) : left;
		float l = out[i * 2] + left, r = out[i * 2 + 1] + right;
		out[i * 2] = (int16_t)(l > 32767.0f ? 32767 : l < -32768.0f ? -32768 : l);
		out[i * 2 + 1] = (int16_t)(r > 32767.0f ? 32767 : r < -32768.0f ? -32768 : r);
		gMoviePosition += step;
	}
	size_t used = (size_t)gMoviePosition;
	if(used > available)
		used = available;
	if(used > 0)
	{
		memmove(gMovieQueue, gMovieQueue + used * (size_t)c, (gMovieQueued - used * (size_t)c) * sizeof(int16_t));
		gMovieQueued -= used * (size_t)c;
		gMoviePosition -= (double)used;
	}
	pthread_mutex_unlock(&gMovieLock);
}

static void* AudioMain(void* unused)
{
	(void)unused;
	int frames = gAudioFrames;
	int16_t* buffer = (int16_t*)malloc(sizeof(int16_t) * 2 * (size_t)frames);
	if(buffer == NULL)
		return NULL;
	// About a twentieth of a second ahead of the host keeps the latency of a
	// sound effect low and still rides out a late wake-up.
	uint32_t ahead = (uint32_t)(gRingRate / 20 > frames ? gRingRate / 20 : frames);
	while(!gAudioStop)
	{
		if(enginehost_audio_ring_buffered_frames(&gRing) >= ahead)
		{
			OS_Sleep(2);
			continue;
		}
		memset(buffer, 0, sizeof(int16_t) * 2 * (size_t)frames);
		if(!gAudioPaused && gAudioFill != NULL)
		{
			pthread_mutex_lock(&gAudioLock);
			gAudioFill(NULL, (uint8_t*)buffer, frames * 4);
			pthread_mutex_unlock(&gAudioLock);
		}
		MixMovie(buffer, frames);
		uint32_t written = 0;
		while(written < (uint32_t)frames && !gAudioStop)
		{
			written += enginehost_audio_ring_write(&gRing, buffer + written * 2, (uint32_t)frames - written);
			if(written < (uint32_t)frames)
				OS_Sleep(2);
		}
	}
	free(buffer);
	return NULL;
}

static int StartAudioThread(void)
{
	if(gAudioRunning)
		return 1;
	if(gRing.base == NULL || gRingRate <= 0)
		return 0;
	gAudioStop = 0;
	if(pthread_create(&gAudioThread, NULL, AudioMain, NULL) != 0)
		return 0;
	gAudioRunning = 1;
	return 1;
}

int OS_AudioOpen(int wantRate, int wantFrames, OS_AudioFill fill, int* rate, int* frames)
{
	(void)wantRate;
	if(gRing.base == NULL || gRingRate <= 0)
	{
		__android_log_print(ANDROID_LOG_WARN, TAG, "no audio ring from the host; the game plays silent");
		return 0;
	}
	pthread_mutex_lock(&gAudioLock);
	gAudioFill = fill;
	gAudioPaused = 1;
	pthread_mutex_unlock(&gAudioLock);
	gAudioFrames = wantFrames > 0 ? wantFrames : 1024;
	if(!StartAudioThread())
		return 0;
	*rate = gRingRate;
	*frames = gAudioFrames;
	return 1;
}

void OS_AudioPause(int paused)
{
	gAudioPaused = paused;
}

void OS_AudioClose(void)
{
	pthread_mutex_lock(&gAudioLock);
	gAudioFill = NULL;
	gAudioPaused = 1;
	pthread_mutex_unlock(&gAudioLock);
}

void OS_AudioLock(void)
{
	pthread_mutex_lock(&gAudioLock);
}

void OS_AudioUnlock(void)
{
	pthread_mutex_unlock(&gAudioLock);
}

int OS_MovieAudioOpen(int rate, int channels)
{
	if(rate <= 0 || channels <= 0 || !StartAudioThread())
		return 0;
	pthread_mutex_lock(&gMovieLock);
	gMovieRate = rate;
	gMovieChannels = channels;
	gMovieQueued = 0;
	gMoviePosition = 0.0;
	gMoviePaused = 1;
	gMovieOpen = 1;
	pthread_mutex_unlock(&gMovieLock);
	return 1;
}

void OS_MovieAudioQueue(const int16_t* samples, int count)
{
	if(count <= 0)
		return;
	pthread_mutex_lock(&gMovieLock);
	if(gMovieOpen)
	{
		if(gMovieQueued + (size_t)count > gMovieCapacity)
		{
			size_t capacity = (gMovieQueued + (size_t)count) * 2;
			int16_t* grown = (int16_t*)realloc(gMovieQueue, capacity * sizeof(int16_t));
			if(grown != NULL)
			{
				gMovieQueue = grown;
				gMovieCapacity = capacity;
			}
		}
		if(gMovieQueued + (size_t)count <= gMovieCapacity)
		{
			memcpy(gMovieQueue + gMovieQueued, samples, (size_t)count * sizeof(int16_t));
			gMovieQueued += (size_t)count;
		}
	}
	pthread_mutex_unlock(&gMovieLock);
}

uint32_t OS_MovieAudioQueuedBytes(void)
{
	pthread_mutex_lock(&gMovieLock);
	uint32_t bytes = (uint32_t)(gMovieQueued * sizeof(int16_t));
	pthread_mutex_unlock(&gMovieLock);
	return bytes;
}

void OS_MovieAudioPause(int paused)
{
	gMoviePaused = paused;
}

void OS_MovieAudioClose(void)
{
	pthread_mutex_lock(&gMovieLock);
	gMovieOpen = 0;
	gMovieQueued = 0;
	gMoviePosition = 0.0;
	pthread_mutex_unlock(&gMovieLock);
}

/* ---- the engine's thread ------------------------------------------------- */

static char* gGamePath;
static char gError[256];

static void* EngineMain(void* unused)
{
	(void)unused;
	char* argv[] = { "openbgi", gGamePath, NULL };
	int code = SDL_main(2, argv);
	__android_log_print(ANDROID_LOG_INFO, TAG, "the engine ended (%d)", code);
	pthread_mutex_lock(&gFrameLock);
	gEnded = 1;
	gExitCode = code;
	pthread_cond_broadcast(&gFrameReady);
	pthread_mutex_unlock(&gFrameLock);
	return NULL;
}

static jlong JNICALL NativeStart(JNIEnv* env, jclass cls, jstring gamePath, jint ringFd, jint ringRate)
{
	(void)cls;
	if(gGamePath != NULL)
	{
		snprintf(gError, sizeof(gError), "OpenBGI is already running in this process");
		return 0;
	}
	gStartMs = MonotonicMs();
	if(ringFd >= 0 && ringRate > 0)
	{
		int result = enginehost_audio_ring_open(&gRing, ringFd);
		if(result == 0)
			gRingRate = ringRate;
		else
			__android_log_print(ANDROID_LOG_WARN, TAG, "the host's audio ring did not map (%d); the game plays silent", result);
	}
	const char* path = (*env)->GetStringUTFChars(env, gamePath, NULL);
	if(path == NULL)
		return 0;
	gGamePath = strdup(path);
	(*env)->ReleaseStringUTFChars(env, gamePath, path);
	if(gGamePath == NULL)
		return 0;
	// main() and the interpreter under it expect a desktop main thread's room.
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, 8u * 1024u * 1024u);
	pthread_t thread;
	int result = pthread_create(&thread, &attributes, EngineMain, NULL);
	pthread_attr_destroy(&attributes);
	if(result != 0)
	{
		snprintf(gError, sizeof(gError), "could not start the engine's thread (%d)", result);
		return 0;
	}
	pthread_detach(thread);
	return 1;
}

static jstring JNICALL NativeError(JNIEnv* env, jclass cls)
{
	(void)cls;
	return (*env)->NewStringUTF(env, gError[0] ? gError : "OpenBGI could not start");
}

/*
 * The host asks for the picture's size once; it is the first frame's, so wait
 * for it. The boot scripts run for seconds before they compose one; the host
 * gives this wait a minute (Enginehost's PICTURE_TIMEOUT_MS), so stop short
 * of that and let the host say why.
 */
static int WaitForFirstFrame(void)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 50;
	pthread_mutex_lock(&gFrameLock);
	while(gFrame == NULL && !gEnded)
	{
		if(pthread_cond_timedwait(&gFrameReady, &gFrameLock, &deadline) == ETIMEDOUT)
			break;
	}
	int ready = gFrame != NULL;
	pthread_mutex_unlock(&gFrameLock);
	if(!ready)
		__android_log_print(ANDROID_LOG_ERROR, TAG, gEnded ? "the engine ended before its first frame (%d)"
		                                                   : "no frame after 50 s", gExitCode);
	return ready;
}

static jint JNICALL NativeWidth(JNIEnv* env, jclass cls, jlong engine)
{
	(void)env; (void)cls; (void)engine;
	return WaitForFirstFrame() ? gFrameWidth : 0;
}

static jint JNICALL NativeHeight(JNIEnv* env, jclass cls, jlong engine)
{
	(void)env; (void)cls; (void)engine;
	return WaitForFirstFrame() ? gFrameHeight : 0;
}

static jint JNICALL NativeStep(JNIEnv* env, jclass cls, jlong engine, jintArray pixels)
{
	(void)cls; (void)engine;
	pthread_mutex_lock(&gFrameLock);
	if(!gFrameNew)
	{
		int ended = gEnded;
		pthread_mutex_unlock(&gFrameLock);
		return ended ? -1 : 0;
	}
	jsize length = (*env)->GetArrayLength(env, pixels);
	jsize size = gFrameWidth * gFrameHeight;
	if(length >= size)
		(*env)->SetIntArrayRegion(env, pixels, 0, size, (const jint*)gFrame);
	gFrameNew = 0;
	int rows = gFrameHeight;
	pthread_mutex_unlock(&gFrameLock);
	return length >= size ? rows : 0;
}

static void JNICALL NativePointer(JNIEnv* env, jclass cls, jlong engine, jint x, jint y, jboolean held)
{
	(void)env; (void)cls; (void)engine;
	gPointerX = x;
	gPointerY = y;
	PushEvent(EVENT_MOVE, 0, 0, x, y);
	if(held && !gLeftDown)
	{
		gLeftDown = 1;
		PushEvent(EVENT_BUTTON, 0, 1, x, y);
	}
	else if(!held && gLeftDown)
	{
		gLeftDown = 0;
		PushEvent(EVENT_BUTTON, 0, 0, x, y);
	}
}

static void JNICALL NativeKey(JNIEnv* env, jclass cls, jlong engine, jint vk, jboolean down)
{
	(void)env; (void)cls; (void)engine;
	PushEvent(EVENT_KEY, vk & 0xFF, down ? 1 : 0, 0, 0);
}

static void JNICALL NativeMouseButton(JNIEnv* env, jclass cls, jlong engine, jint button, jboolean down)
{
	(void)env; (void)cls; (void)engine;
	PushEvent(EVENT_BUTTON, button, down ? 1 : 0, gPointerX, gPointerY);
}

static void JNICALL NativeWheel(JNIEnv* env, jclass cls, jlong engine, jint steps)
{
	(void)env; (void)cls; (void)engine;
	PushEvent(EVENT_WHEEL, steps, 0, 0, 0);
}

static void JNICALL NativeFocus(JNIEnv* env, jclass cls, jlong engine, jboolean active)
{
	(void)env; (void)cls; (void)engine;
	gAppActive = active ? 1 : 0;
}

static void JNICALL NativeStop(JNIEnv* env, jclass cls, jlong engine)
{
	(void)env; (void)cls; (void)engine;
	PushEvent(EVENT_QUIT, 0, 0, 0, 0);
	gAudioStop = 1;
}

/*
 * The isolated runtime loads this library from a descriptor, where nothing
 * binds natives by name, and calls this to bind BurikoPlugin's itself
 * (Enginehost's isolated_native_bridge.c).
 */
__attribute__((visibility("default")))
void enginehost_register_natives(JNIEnv* env, jclass cls)
{
	static const JNINativeMethod methods[] = {
		{ "nativeStart", "(Ljava/lang/String;II)J", (void*)NativeStart },
		{ "nativeError", "()Ljava/lang/String;", (void*)NativeError },
		{ "nativeWidth", "(J)I", (void*)NativeWidth },
		{ "nativeHeight", "(J)I", (void*)NativeHeight },
		{ "nativeStep", "(J[I)I", (void*)NativeStep },
		{ "nativePointer", "(JIIZ)V", (void*)NativePointer },
		{ "nativeKey", "(JIZ)V", (void*)NativeKey },
		{ "nativeMouseButton", "(JIZ)V", (void*)NativeMouseButton },
		{ "nativeWheel", "(JI)V", (void*)NativeWheel },
		{ "nativeFocus", "(JZ)V", (void*)NativeFocus },
		{ "nativeStop", "(J)V", (void*)NativeStop },
	};
	(*env)->RegisterNatives(env, cls, methods, (jint)(sizeof(methods) / sizeof(methods[0])));
}
