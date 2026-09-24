#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>

#include "movie.h"
#include "mpeg1.h"
#include "object.h"

typedef struct
{
	uint8_t*          data;
	Mpeg1_t*          decoder;
	int               width, height;
	uint8_t*          frame;        // the frame on show, B, G, R, x
	int               haveFrame;
	uint8_t*          next;         // the next frame, decoded ahead
	double            nextPts;
	int               haveNext;
	int               videoDone;
	uint32_t          startTicks;
	double            stopSeconds;
	SDL_AudioDeviceID audio;
	int               channels;
	int               audioDone;
} Movie_t;

static Movie_t  gMovie;
static int      gPlaying = 0;          // 0x00566960
static float    gGain = 1.0f;          // from 0x00566928

void Movie_SetAttenuation(int hundredthsOfDb)
{
	// IBasicAudio::put_Volume: hundredths of a decibel, -10000 is silence.
	gGain = hundredthsOfDb <= -10000 ? 0.0f : (float)pow(10.0, hundredthsOfDb / 2000.0);
}

static double Movie_Now(void)
{
	return (double)(SDL_GetTicks() - gMovie.startTicks) / 1000.0;
}

static void Movie_Free(void)
{
	if(gMovie.audio != 0)
		SDL_CloseAudioDevice(gMovie.audio);
	if(gMovie.decoder != NULL)
		Mpeg1_Close(gMovie.decoder);
	free(gMovie.data);
	free(gMovie.frame);
	free(gMovie.next);
	memset(&gMovie, 0, sizeof(gMovie));
}

// Keeps about half a second of the movie's sound queued on its own device.
static void Movie_FeedAudio(void)
{
	if(gMovie.audio == 0 || gMovie.audioDone)
		return;
	int rate = Mpeg1_AudioRate(gMovie.decoder);
	uint32_t want = (uint32_t)(rate / 2) * (uint32_t)gMovie.channels * sizeof(int16_t);
	int16_t buffer[2048 * 2];
	while(SDL_GetQueuedAudioSize(gMovie.audio) < want)
	{
		int got = Mpeg1_ReadAudio(gMovie.decoder, buffer, 2048);
		if(got <= 0)
		{
			gMovie.audioDone = 1;
			break;
		}
		int samples = got * gMovie.channels;
		if(gGain != 1.0f)
			for(int i = 0; i < samples; i++)
				buffer[i] = (int16_t)(buffer[i] * gGain);
		SDL_QueueAudio(gMovie.audio, buffer, (Uint32)(samples * sizeof(int16_t)));
	}
}

uint32_t Movie_Play(uint8_t* data, size_t size)
{
	// 0x0048F44C: whatever was playing is stopped first.
	Movie_Stop();
	Mpeg1_t* decoder = Mpeg1_Open(data, size);
	if(decoder == NULL)
	{
		free(data);
		return 0;
	}
	gMovie.data = data;
	gMovie.decoder = decoder;
	gMovie.width = Mpeg1_Width(decoder);
	gMovie.height = Mpeg1_Height(decoder);
	gMovie.frame = (uint8_t*)malloc((size_t)gMovie.width * gMovie.height * 4);
	gMovie.next = (uint8_t*)malloc((size_t)gMovie.width * gMovie.height * 4);
	if(gMovie.frame == NULL || gMovie.next == NULL)
	{
		Movie_Free();
		return 0;
	}
	gMovie.stopSeconds = Mpeg1_Duration(decoder);
	gMovie.channels = Mpeg1_AudioChannels(decoder);
	if(Mpeg1_AudioRate(decoder) > 0 && gMovie.channels > 0)
	{
		if(!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO))
			SDL_InitSubSystem(SDL_INIT_AUDIO);
		SDL_AudioSpec want, have;
		SDL_zero(want);
		want.freq = Mpeg1_AudioRate(decoder);
		want.format = AUDIO_S16SYS;
		want.channels = (Uint8)gMovie.channels;
		want.samples = 2048;
		gMovie.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
		if(gMovie.audio == 0)
			printf("[Movie]: Warning: no audio device for the movie's sound (%s); it plays silent\n", SDL_GetError());
	}
	gMovie.haveNext = Mpeg1_NextFrame(decoder, gMovie.next, gMovie.width * 4, &gMovie.nextPts);
	gMovie.videoDone = !gMovie.haveNext;
	Movie_FeedAudio();
	gMovie.startTicks = SDL_GetTicks();
	if(gMovie.audio != 0)
		SDL_PauseAudioDevice(gMovie.audio, 0);
	gPlaying = 1;
	printf("[Movie]: Playing %dx%d, %.3f s\n", gMovie.width, gMovie.height, gMovie.stopSeconds);
	// 0x0048F7C3: the stop position (100 ns units) divided by 10000.
	return (uint32_t)(gMovie.stopSeconds * 1000.0);
}

void Movie_Stop(void)
{
	// 0x0048F2A9 onwards: Stop, wait for the graph to settle, release it all.
	gPlaying = 0;
	Movie_Free();
}

int Movie_IsPlaying(void)
{
	return gPlaying;
}

int Movie_Status(void)
{
	if(!gPlaying)
		return 0;
	return Movie_Now() < gMovie.stopSeconds ? 1 : 0;
}

void Movie_Update(Bitmap_t* back)
{
	if(!gPlaying)
		return;
	double now = Movie_Now();
	// Every frame whose time has come is decoded; the last of them is shown.
	while(gMovie.haveNext && gMovie.nextPts <= now)
	{
		uint8_t* swap = gMovie.frame;
		gMovie.frame = gMovie.next;
		gMovie.next = swap;
		gMovie.haveFrame = 1;
		gMovie.haveNext = Mpeg1_NextFrame(gMovie.decoder, gMovie.next, gMovie.width * 4, &gMovie.nextPts);
		if(!gMovie.haveNext)
			gMovie.videoDone = 1;
	}
	Movie_FeedAudio();

	if(gMovie.haveFrame && back != NULL && Renderer_ModePixelBytes(back->mode) == 4)
	{
		// The renderer filter's frame covers the drawing device; a movie of another
		// size is scaled to it.
		for(int y = 0; y < back->height; y++)
		{
			int sy = (int)((int64_t)y * gMovie.height / back->height);
			const uint32_t* in = (const uint32_t*)(gMovie.frame + (size_t)sy * gMovie.width * 4);
			uint32_t* out = (uint32_t*)(back->bitmap + (size_t)y * back->stride);
			if(back->width == gMovie.width)
				memcpy(out, in, (size_t)back->width * 4);
			else
				for(int x = 0; x < back->width; x++)
					out[x] = in[(int64_t)x * gMovie.width / back->width];
		}
	}

	// EC_COMPLETE (0x00499D9D): the flag drops and the graph is stopped
	// (0x0048F270), and a full redraw is asked for (0x00461EF0).
	if(gMovie.videoDone && now >= gMovie.stopSeconds)
	{
		Movie_Stop();
		gObjectDamage++;
	}
}
