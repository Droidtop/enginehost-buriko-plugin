#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>

#include "audio.h"

/*
 * stb_vorbis (public domain, vendor/stb_vorbis.c) is compiled into this file so
 * that neither build has to learn about a second directory of sources: the
 * makefile finds src/ recursively and the Android CMakeLists globs the .c
 * files directly in src/.
 * Its own warnings are not this engine's.
 */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
#include "../vendor/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------------- */
/* The bw header                                                             */
/* ------------------------------------------------------------------------- */

/*
 * A sound member is a 0x40-byte header and the codec's stream. What reads the
 * header is the decoder base's open, 0x004A84C0, which copies it to +0x50 of the
 * decoder and so names every field by the accessor that returns it:
 *
 *   +0x00  header size (0x40; the Vorbis stream callbacks at 0x004A92E0 add a
 *          constant 0x40 rather than this word)
 *   +0x04  "bw  ", compared as a NUL-terminated string against 0x004DBFDC
 *   +0x08  data size (0x004A3FD0 sizes an SE's memory stream as +0x00 + +0x08)
 *   +0x0C  sample frames (vtable +0x0C, 0x004A8640: where the read at 0x004A8230
 *          stops)
 *   +0x10  sample rate      +0x14  channels (0x004A8140 builds the 16-bit format)
 *   +0x18  loop flag (vtable +0x24, 0x004A8650)
 *   +0x1C  loop start frame (+0x6C, where vtable +0x28 puts the position back)
 *   +0x30  codec: 0..3 pick the decoder class (0x004A3700 / 0x004A3C90); 3 is
 *          Ogg Vorbis (its +0x38, 0x004A9290, accepts only 3)
 */
#define BW_HEADER_SIZE 0x40
enum { BW_SIZE = 0, BW_MAGIC = 1, BW_DATA = 2, BW_FRAMES = 3, BW_RATE = 4, BW_CHANNELS = 5,
       BW_LOOP = 6, BW_LOOP_START = 7, BW_CODEC = 12 };

static uint32_t Bw_Word(const uint8_t* data, int index)
{
	const uint8_t* p = data + index * 4;
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 0x004A84C0 with the codec switch in front of it (0x004A3700 / 0x004A3C90). */
static uint32_t Bw_Check(const uint8_t* data, size_t size, uint32_t* header)
{
	if(size < BW_HEADER_SIZE)
		return AUDIO_NOT_BW;                     /* 0x10000003, caught as 0x0E */
	for(int i = 0; i < 16; i++)
		header[i] = Bw_Word(data, i);
	if(memcmp(data + 4, "bw  ", 4) != 0)
		return AUDIO_NOT_BW;                     /* 0x11000001 */
	if(header[BW_CODEC] > 3)
		return AUDIO_NOT_BW;                     /* the switch's default */
	return AUDIO_OK;
}

/* ------------------------------------------------------------------------- */
/* Channels                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct AudioFade
{
	int      active;     /* +0x00 */
	uint32_t start;      /* +0x04, timeGetTime */
	uint32_t end;        /* +0x08 */
	int32_t  from;       /* +0x0C */
	int32_t  to;         /* +0x10 */
	int32_t  current;    /* +0x14 */
} AudioFade_t;

typedef struct AudioPart
{
	uint8_t*    data;
	size_t      size;
	uint32_t    header[16];
	stb_vorbis* vorbis;
} AudioPart_t;

#define DECODE_FRAMES 2048

typedef struct AudioChannel
{
	/* The library's 0x44-byte record. */
	int         loaded;       /* +0x00 */
	uint32_t    master;       /* +0x0C */
	uint32_t    volume;       /* +0x10 */
	AudioFade_t fade1;        /* +0x14 */
	AudioFade_t fade2;        /* +0x2C */

	/* The player. */
	int         playing;      /* DirectSound's DSBSTATUS_PLAYING */
	int32_t     pan;          /* +0x74, -128 .. 128 */
	float       gainLeft;     /* what SetVolume and SetPan leave the buffer at */
	float       gainRight;
	uint32_t    rate;
	uint32_t    channels;
	double      phase;        /* resampling, only when rate is not the device's */
	int16_t     prev[2];
	int16_t     next[2];
	int         primed;

	/* A music decoder. */
	int         pair;         /* 0x004AA960 rather than 0x004A9A50 */
	AudioPart_t part[2];
	int         partIndex;    /* +0x9B0 of the pair */
	uint32_t    position;     /* +0x1C */
	uint32_t    total;        /* vtable +0x0C */
	uint32_t    loopFlag;     /* +0x68 (single) or +0x9B4 (pair) */
	uint32_t    loopStart;    /* +0x6C */
	uint32_t    loopCount;    /* +0x9C */
	uint32_t    loopDeadline; /* +0xA0 */
	int         exhausted;    /* the read came back short: state 2 of the player */
	int16_t     decoded[DECODE_FRAMES * 2];
	int         decodedFrames;
	int         decodedPos;

	/* An SE's static buffer. */
	int16_t*    pcm;
	uint32_t    frames;
	uint32_t    cursor;
} AudioChannel_t;

static AudioChannel_t    gMusic[AUDIO_MUSIC_CHANNELS];
static AudioChannel_t    gSE[AUDIO_SE_CHANNELS];
static SDL_AudioDeviceID gDevice = 0;
static int               gDeviceRate = 44100;
static int               gDeviceTried = 0;
static uint32_t          gDeviceFlags = 0;   /* 0x005085A4: bit 0 open, bit 1 timer */
static uint32_t          gTimerLast = 0;
static float*            gMix = NULL;
static int               gMixFrames = 0;

static void Channel_Defaults(AudioChannel_t* c)
{
	/* 0x004A2760, the channel record's constructor. */
	memset(c, 0, sizeof(*c));
	c->master = 0x80;
	c->volume = 0x80;
	c->fade1.current = 0x80;
	c->fade2.current = 0x80;
	c->gainLeft = 1.0f;
	c->gainRight = 1.0f;
}

static int Audio_Ready(void)
{
	return (gDeviceFlags & 3) == 3;
}

static void Audio_Lock(void)
{
	if(gDevice != 0)
		SDL_LockAudioDevice(gDevice);
}

static void Audio_Unlock(void)
{
	if(gDevice != 0)
		SDL_UnlockAudioDevice(gDevice);
}

static uint32_t Audio_Now(void)
{
	return SDL_GetTicks();   /* timeGetTime */
}

/*
 * 0x004A2350: the attenuation a channel's four volume words amount to, in the
 * decibels the player takes. Each word is a step of 0..0x80 and each step short
 * of 0x80 costs 1/2.66666666 dB (the double at 0x004DBA20), so a word alone
 * spans 48 dB. If any single word is 48 dB down the channel is off - the
 * answer is 0x80 - otherwise it is the four losses together, truncated.
 */
static int32_t Channel_Attenuation(const AudioChannel_t* c)
{
	const double step = 2.66666666;
	int32_t a = (int32_t)c->master, b = (int32_t)c->volume;
	int32_t d = c->fade1.current, e = c->fade2.current;
	int32_t sum = (int32_t)(((double)(0x200 - a - b - d - e)) / step);
	if((double)(0x80 - a) / step >= 48.0) return 0x80;
	if((double)(0x80 - b) / step >= 48.0) return 0x80;
	if((double)(0x80 - d) / step >= 48.0) return 0x80;
	if((double)(0x80 - e) / step >= 48.0) return 0x80;
	return sum;
}

/*
 * The player's SetVolume (0x004A6A70) and SetPan (0x004A6AF0), and what
 * DirectSound does with them. SetVolume hands IDirectSoundBuffer::SetVolume
 * -100 times the attenuation, never below -10000 (DSBVOLUME_MIN); SetPan clamps
 * to -128..128 and hands SetPan 10000 * (p / 128)^3 (the doubles 3.0 at
 * 0x004EC8B0 and -10000.0 at 0x004DBE30, the result negated). DirectSound
 * applies a volume of v hundredths of a decibel to both sides and a pan of p by
 * attenuating the far side by |p| hundredths.
 */
static void Channel_Apply(AudioChannel_t* c)
{
	int32_t hundredths = -100 * Channel_Attenuation(c);
	if(hundredths < -10000)
		hundredths = -10000;
	double gain = pow(10.0, hundredths / 2000.0);
	double p = (double)c->pan;
	int32_t dsPan = (int32_t)(-(pow(p, 3.0) / pow(128.0, 3.0) * -10000.0));
	double left = dsPan > 0 ? pow(10.0, -dsPan / 2000.0) : 1.0;
	double right = dsPan < 0 ? pow(10.0, dsPan / 2000.0) : 1.0;
	c->gainLeft = (float)(gain * left);
	c->gainRight = (float)(gain * right);
}

static void Channel_SetPan(AudioChannel_t* c, int32_t p)
{
	if(p < -0x80) p = -0x80;
	if(p > 0x80) p = 0x80;
	c->pan = p;
	Channel_Apply(c);
}

/* 0x004A2410: one step of a fade. */
static void Fade_Step(AudioFade_t* f, uint32_t now)
{
	int32_t duration = (int32_t)(f->end - f->start);
	int32_t delta = f->to - f->from;
	if(duration > 0)
	{
		double t = (double)(int32_t)(now - f->start) / (double)duration;
		if(t < 1.0)
		{
			f->current = (int32_t)(t * delta + f->from);
			return;
		}
	}
	f->active = 0;
	f->current = (int32_t)(1.0 * delta + f->from);
}

/* 0x004A2490 over one bank, as the timer at 0x004A2E50 calls it for both. */
static void Bank_Step(AudioChannel_t* bank, int count, uint32_t now)
{
	for(int i = 0; i < count; i++)
	{
		AudioChannel_t* c = &bank[i];
		if(!c->loaded)
			continue;
		int changed = 0;
		if(c->fade1.active)
		{
			Fade_Step(&c->fade1, now);
			changed = 1;
		}
		if(c->fade2.active)
		{
			Fade_Step(&c->fade2, now);
			changed = 1;
		}
		if(changed)
			Channel_Apply(c);
	}
}

/* ------------------------------------------------------------------------- */
/* Music decoding                                                            */
/* ------------------------------------------------------------------------- */

static void Part_Free(AudioPart_t* part)
{
	if(part->vorbis != NULL)
		stb_vorbis_close(part->vorbis);
	free(part->data);
	memset(part, 0, sizeof(*part));
}

/* The codec 3 decoder's open (0x004A98D0 for music, 0x004A9410 for SE): the
   Ogg stream starts 0x40 bytes in. */
static uint32_t Part_Open(AudioPart_t* part)
{
	int error = 0;
	if(part->size <= BW_HEADER_SIZE)
		return AUDIO_NOT_BW;
	part->vorbis = stb_vorbis_open_memory(part->data + BW_HEADER_SIZE,
	                                      (int)(part->size - BW_HEADER_SIZE), &error, NULL);
	if(part->vorbis == NULL)
		return AUDIO_NOT_BW;
	stb_vorbis_info info = stb_vorbis_get_info(part->vorbis);
	if((uint32_t)info.channels != part->header[BW_CHANNELS] || info.channels < 1 || info.channels > 2)
		return AUDIO_NOT_BW;
	return AUDIO_OK;
}

static void Music_Unload(AudioChannel_t* c)
{
	Part_Free(&c->part[0]);
	Part_Free(&c->part[1]);
	c->pair = 0;
	c->decodedFrames = c->decodedPos = 0;
	c->primed = 0;
}

/*
 * The decoder's rewind (vtable +0x14 with 1, 0x004A8340, which resets through
 * +0x2C first): back to the first frame of the first part, counters cleared.
 */
static void Music_Rewind(AudioChannel_t* c)
{
	c->position = 0;
	c->partIndex = 0;
	c->loopCount = 0;
	c->loopDeadline = 0;
	c->exhausted = 0;
	c->decodedFrames = c->decodedPos = 0;
	c->primed = 0;
	c->phase = 0.0;
	if(c->part[0].vorbis != NULL)
		stb_vorbis_seek_start(c->part[0].vorbis);
	if(c->part[1].vorbis != NULL)
		stb_vorbis_seek_start(c->part[1].vorbis);
}

/*
 * One read from the codec. A single stream stops at its end. The pair's
 * callback at 0x004AAAB0 carries on instead: at the end of the intro it moves
 * to the loop part (0x004AA8D0 with 1), and at the end of the loop part it
 * counts a loop (+0x9C), notes when that loop will have been heard - 4000 ms
 * on (0x0FA0), the depth of the streaming buffer - and starts the loop part
 * again.
 */
static int Music_DecodeSome(AudioChannel_t* c, int16_t* out, int frames)
{
	int channels = (int)c->channels;
	for(int attempt = 0; attempt < 3; attempt++)
	{
		AudioPart_t* part = &c->part[c->partIndex];
		int got = stb_vorbis_get_samples_short_interleaved(part->vorbis, channels, out, frames * channels);
		if(got > 0 || !c->pair)
			return got;
		if(c->partIndex == 0)
		{
			c->partIndex = 1;
			stb_vorbis_seek_start(c->part[1].vorbis);
		}
		else
		{
			c->loopCount++;
			c->loopDeadline = Audio_Now() + 0x0FA0;
			stb_vorbis_seek_start(c->part[1].vorbis);
		}
	}
	return 0;
}

/*
 * The decoder base's read, 0x004A8230: frames up to the total (vtable +0x0C),
 * and at the total, if the loop flag (vtable +0x24) says so, back to the loop
 * start (vtable +0x28) and on. For a single stream the loop flag is +0x68 and
 * the loop start +0x6C; the pair's flag is 1 while the intro plays and +0x9B4
 * after it, and its +0x28 (0x004AA8C0) puts the position back to 0 once the
 * loop part is playing, so the loop part repeats for as long as the flag holds.
 * A read that comes back short ends the stream: the player then plays out what
 * it has and stops itself (0x004A79FA).
 */
static void Music_Refill(AudioChannel_t* c)
{
	int want = DECODE_FRAMES;
	int filled = 0;
	c->decodedPos = 0;
	while(want > 0 && !c->exhausted)
	{
		if(c->position >= c->total)
		{
			uint32_t loop = c->pair ? (c->partIndex == 0 ? 1 : c->loopFlag) : c->loopFlag;
			if(loop == 0)
			{
				c->exhausted = 1;
				break;
			}
			if(c->pair)
			{
				if(c->partIndex == 1)
					c->position = 0;
			}
			else
			{
				c->position = c->loopStart;
				if(c->loopStart == 0)
					stb_vorbis_seek_start(c->part[0].vorbis);
				else
					stb_vorbis_seek(c->part[0].vorbis, c->loopStart);
			}
			if(c->position >= c->total)
			{
				c->exhausted = 1;
				break;
			}
		}
		int n = want;
		if((uint32_t)n > c->total - c->position)
			n = (int)(c->total - c->position);
		int got = Music_DecodeSome(c, c->decoded + filled * c->channels, n);
		if(got <= 0)
		{
			c->exhausted = 1;
			break;
		}
		c->position += (uint32_t)got;
		filled += got;
		want -= got;
	}
	c->decodedFrames = filled;
}

/* ------------------------------------------------------------------------- */
/* Mixing                                                                    */
/* ------------------------------------------------------------------------- */

/* One source frame of a channel, 0 when the channel has nothing more. */
static int Channel_Pull(AudioChannel_t* c, int isMusic, int16_t* frame)
{
	if(isMusic)
	{
		if(c->decodedPos >= c->decodedFrames)
		{
			if(c->exhausted)
				return 0;
			Music_Refill(c);
			if(c->decodedFrames == 0)
				return 0;
		}
		const int16_t* s = c->decoded + c->decodedPos * c->channels;
		frame[0] = s[0];
		frame[1] = c->channels == 2 ? s[1] : s[0];
		c->decodedPos++;
		return 1;
	}
	if(c->cursor >= c->frames)
		return 0;
	frame[0] = frame[1] = c->pcm[c->cursor++];   /* SE buffers are mono */
	return 1;
}

/*
 * The end of a buffer. A static SE buffer simply stops where it is. The music
 * player stops itself through its own Stop (vtable +0x10, 0x004A7B50), which
 * also rewinds the decoder.
 */
static void Channel_Ended(AudioChannel_t* c, int isMusic)
{
	c->playing = 0;
	if(isMusic)
		Music_Rewind(c);
}

static void Channel_Mix(AudioChannel_t* c, int isMusic, float* mix, int frames)
{
	if(!c->loaded || !c->playing)
		return;
	float gl = c->gainLeft, gr = c->gainRight;
	int16_t frame[2];
	if((int)c->rate == gDeviceRate)
	{
		for(int i = 0; i < frames; i++)
		{
			if(!Channel_Pull(c, isMusic, frame))
			{
				Channel_Ended(c, isMusic);
				return;
			}
			mix[i * 2] += frame[0] * gl;
			mix[i * 2 + 1] += frame[1] * gr;
		}
		return;
	}
	/* DirectSound converts a secondary buffer's rate to the primary's; this
	   does the same with a linear interpolation. */
	double step = (double)c->rate / (double)gDeviceRate;
	if(!c->primed)
	{
		if(!Channel_Pull(c, isMusic, c->prev) || !Channel_Pull(c, isMusic, c->next))
		{
			Channel_Ended(c, isMusic);
			return;
		}
		c->phase = 0.0;
		c->primed = 1;
	}
	for(int i = 0; i < frames; i++)
	{
		while(c->phase >= 1.0)
		{
			c->prev[0] = c->next[0];
			c->prev[1] = c->next[1];
			if(!Channel_Pull(c, isMusic, c->next))
			{
				Channel_Ended(c, isMusic);
				return;
			}
			c->phase -= 1.0;
		}
		float t = (float)c->phase;
		mix[i * 2] += (c->prev[0] + (c->next[0] - c->prev[0]) * t) * gl;
		mix[i * 2 + 1] += (c->prev[1] + (c->next[1] - c->prev[1]) * t) * gr;
		c->phase += step;
	}
}

static void SDLCALL Audio_Callback(void* userdata, Uint8* stream, int len)
{
	(void)userdata;
	int frames = len / 4;
	int16_t* out = (int16_t*)stream;

	/* The 20 ms timer (0x004A2F20 -> 0x004A2E50). */
	uint32_t now = Audio_Now();
	if(now - gTimerLast >= 20)
	{
		gTimerLast = now;
		Bank_Step(gMusic, AUDIO_MUSIC_CHANNELS, now);
		Bank_Step(gSE, AUDIO_SE_CHANNELS, now);
	}

	while(frames > 0)
	{
		int n = frames < gMixFrames ? frames : gMixFrames;
		memset(gMix, 0, sizeof(float) * 2 * n);
		for(int i = 0; i < AUDIO_MUSIC_CHANNELS; i++)
			Channel_Mix(&gMusic[i], 1, gMix, n);
		for(int i = 0; i < AUDIO_SE_CHANNELS; i++)
			Channel_Mix(&gSE[i], 0, gMix, n);
		for(int i = 0; i < n * 2; i++)
		{
			float v = gMix[i];
			if(v > 32767.0f) v = 32767.0f;
			if(v < -32768.0f) v = -32768.0f;
			out[i] = (int16_t)v;
		}
		out += n * 2;
		frames -= n;
	}
}

/* ------------------------------------------------------------------------- */
/* The device                                                                */
/* ------------------------------------------------------------------------- */

int Audio_Init(void)
{
	if(gDeviceTried)
		return gDevice != 0;
	gDeviceTried = 1;
	for(int i = 0; i < AUDIO_MUSIC_CHANNELS; i++)
		Channel_Defaults(&gMusic[i]);
	for(int i = 0; i < AUDIO_SE_CHANNELS; i++)
		Channel_Defaults(&gSE[i]);

	if(!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		printf("[Audio]: No audio subsystem (%s); the sound library stays uninitialised and answers 0x14\n", SDL_GetError());
		return 0;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = Audio_Callback;
	gDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if(gDevice == 0)
	{
		printf("[Audio]: No audio device (%s); the sound library stays uninitialised and answers 0x14\n", SDL_GetError());
		return 0;
	}
	gDeviceRate = have.freq;
	gMixFrames = have.samples > 0 ? have.samples : 1024;
	gMix = (float*)malloc(sizeof(float) * 2 * (size_t)gMixFrames);
	if(gMix == NULL)
	{
		SDL_CloseAudioDevice(gDevice);
		gDevice = 0;
		return 0;
	}
	gDeviceFlags = 1;          /* 0x004A20E1 */
	gTimerLast = Audio_Now();
	gDeviceFlags |= 2;         /* 0x004A3654, the timer */
	SDL_PauseAudioDevice(gDevice, 0);
	printf("[Audio]: Device open at %d Hz, %d frames per period\n", gDeviceRate, gMixFrames);
	return 1;
}

void Audio_Free(void)
{
	if(gDevice == 0)
		return;
	SDL_PauseAudioDevice(gDevice, 1);
	Audio_Lock();
	gDeviceFlags = 0;
	for(int i = 0; i < AUDIO_MUSIC_CHANNELS; i++)
	{
		Music_Unload(&gMusic[i]);
		Channel_Defaults(&gMusic[i]);
	}
	for(int i = 0; i < AUDIO_SE_CHANNELS; i++)
	{
		free(gSE[i].pcm);
		Channel_Defaults(&gSE[i]);
	}
	Audio_Unlock();
	SDL_CloseAudioDevice(gDevice);
	gDevice = 0;
	gDeviceTried = 0;
	free(gMix);
	gMix = NULL;
}

/* 0x004A2310 / 0x004A22D0: the device, the range, and whether it is loaded. */
static uint32_t Music_Check(uint32_t ch)
{
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(ch >= AUDIO_MUSIC_CHANNELS) return AUDIO_BAD_CHANNEL;
	return gMusic[ch].loaded ? AUDIO_OK : AUDIO_NOT_LOADED;
}

static uint32_t SE_Check(uint32_t se)
{
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(se >= AUDIO_SE_CHANNELS) return AUDIO_BAD_CHANNEL;
	return gSE[se].loaded ? AUDIO_OK : AUDIO_NOT_LOADED;
}

/* ------------------------------------------------------------------------- */
/* Music                                                                     */
/* ------------------------------------------------------------------------- */

uint32_t Audio_MusicStop(uint32_t ch)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
	{
		gMusic[ch].playing = 0;
		Music_Rewind(&gMusic[ch]);
	}
	Audio_Unlock();
	return r;
}

/*
 * What 0x004A3700 and 0x004A39A0 do once the decoder is made: the player takes
 * it in place of the old one (vtable +0x1C gives the old one back to be
 * deleted, +0x04 opens the new one, stopped), fade 1 starts at the volume
 * asked for, fade 2 at full, the channel is loaded, and the volume and the pan
 * (0x004A2DB0) are applied.
 */
static void Music_Install(AudioChannel_t* c, AudioChannel_t* fresh, uint32_t volume, uint32_t pan)
{
	Music_Unload(c);
	c->pair = fresh->pair;
	c->part[0] = fresh->part[0];
	c->part[1] = fresh->part[1];
	c->rate = fresh->rate;
	c->channels = fresh->channels;
	c->total = fresh->total;
	c->loopFlag = fresh->loopFlag;
	c->loopStart = fresh->loopStart;
	c->playing = 0;
	Music_Rewind(c);
	c->fade1.active = 0;
	c->fade1.current = (int32_t)volume;
	c->fade2.active = 0;
	c->fade2.current = 0x80;
	c->loaded = 1;
	Channel_Apply(c);
	uint32_t p = pan > 0x80 ? 0x80 : pan;        /* 0x004A2DB0's clamp */
	Channel_SetPan(c, (int32_t)((double)((int32_t)p - 0x40) * 0.015625 * 128.0));
}

static uint32_t Music_Fail(uint32_t ch, uint32_t code)
{
	/* The handler at 0x004A3960: the channel is no longer loaded. */
	Audio_Lock();
	gMusic[ch].loaded = 0;
	gMusic[ch].playing = 0;
	Audio_Unlock();
	return code;
}

static uint32_t Music_Refuse(const char* what, uint32_t codec)
{
	printf("[Audio]: Refused: a bw member of codec %u needs %s, which is not written; only codec 3 (Ogg Vorbis) is\n",
	       codec, what);
	return AUDIO_REFUSED;
}

uint32_t Audio_MusicLoad(uint32_t ch, uint8_t* data, size_t size,
                         uint32_t volume, uint32_t pan, uint32_t flags)
{
	if(!Audio_Ready())
	{
		free(data);
		return AUDIO_NO_DEVICE;
	}
	if(ch >= AUDIO_MUSIC_CHANNELS)
	{
		free(data);
		return AUDIO_BAD_CHANNEL;
	}
	AudioChannel_t fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.part[0].data = data;
	fresh.part[0].size = size;
	uint32_t r = Bw_Check(data, size, fresh.part[0].header);
	if(r != AUDIO_OK)
	{
		Part_Free(&fresh.part[0]);
		return Music_Fail(ch, r);
	}
	uint32_t codec = fresh.part[0].header[BW_CODEC];
	if(codec != 3)
	{
		Part_Free(&fresh.part[0]);
		static const char* const names[3] = {
			"the decoder at 0x004AA460", "the decoder at 0x004AA0E0", "the decoder at 0x004A9E10" };
		return Music_Refuse(names[codec], codec);
	}
	r = Part_Open(&fresh.part[0]);
	if(r != AUDIO_OK)
	{
		Part_Free(&fresh.part[0]);
		return Music_Fail(ch, r);
	}
	fresh.rate = fresh.part[0].header[BW_RATE];
	fresh.channels = fresh.part[0].header[BW_CHANNELS];
	fresh.total = fresh.part[0].header[BW_FRAMES];
	fresh.loopFlag = fresh.part[0].header[BW_LOOP];
	fresh.loopStart = fresh.part[0].header[BW_LOOP_START];
	if(flags & 1)
	{
		/* 0x004A86B0 with (flags & 2, 0). */
		fresh.loopFlag = flags & 2;
		fresh.loopStart = 0;
	}
	if(fresh.rate == 0)
	{
		Part_Free(&fresh.part[0]);
		return Music_Fail(ch, AUDIO_OPEN_FAILED);
	}
	Audio_Lock();
	Music_Install(&gMusic[ch], &fresh, volume, pan);
	Audio_Unlock();
	printf("[Audio]: Music channel %u: %u Hz, %u channel(s), %u frames, loop %s from %u\n",
	       ch, fresh.rate, fresh.channels, fresh.total, fresh.loopFlag ? "on" : "off", fresh.loopStart);
	return AUDIO_OK;
}

uint32_t Audio_MusicLoadPair(uint32_t ch, uint8_t* intro, size_t introSize,
                             uint8_t* loop, size_t loopSize, uint32_t loopFlag,
                             uint32_t volume, uint32_t pan)
{
	if(!Audio_Ready() || ch >= AUDIO_MUSIC_CHANNELS)
	{
		free(intro);
		free(loop);
		return !Audio_Ready() ? AUDIO_NO_DEVICE : AUDIO_BAD_CHANNEL;
	}
	AudioChannel_t fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.pair = 1;
	fresh.part[0].data = intro;
	fresh.part[0].size = introSize;
	fresh.part[1].data = loop;
	fresh.part[1].size = loopSize;
	/* 0x004A39A0: both headers must read, name the same codec, and that codec
	   must be 3; anything else is 0x0E. */
	uint32_t r = Bw_Check(intro, introSize, fresh.part[0].header);
	if(r == AUDIO_OK)
		r = Bw_Check(loop, loopSize, fresh.part[1].header);
	if(r == AUDIO_OK && (fresh.part[0].header[BW_CODEC] != fresh.part[1].header[BW_CODEC]
	                     || fresh.part[0].header[BW_CODEC] != 3))
		r = AUDIO_NOT_BW;
	if(r == AUDIO_OK)
		r = Part_Open(&fresh.part[0]);
	if(r == AUDIO_OK)
		r = Part_Open(&fresh.part[1]);
	if(r == AUDIO_OK && (fresh.part[0].header[BW_RATE] != fresh.part[1].header[BW_RATE]
	                     || fresh.part[0].header[BW_CHANNELS] != fresh.part[1].header[BW_CHANNELS]
	                     || fresh.part[0].header[BW_RATE] == 0))
	{
		/* 0x004AA5B0 builds one format from both parts; parts that disagree
		   are not something this engine will guess at. */
		printf("[Audio]: Refused: the two parts of a music pair differ in rate or channels\n");
		Part_Free(&fresh.part[0]);
		Part_Free(&fresh.part[1]);
		return AUDIO_REFUSED;
	}
	if(r != AUDIO_OK)
	{
		Part_Free(&fresh.part[0]);
		Part_Free(&fresh.part[1]);
		return Music_Fail(ch, r);
	}
	fresh.rate = fresh.part[0].header[BW_RATE];
	fresh.channels = fresh.part[0].header[BW_CHANNELS];
	/* 0x004AA790: the pair's length is both parts'. */
	fresh.total = fresh.part[0].header[BW_FRAMES] + fresh.part[1].header[BW_FRAMES];
	fresh.loopFlag = loopFlag;               /* 0x004AA950 */
	fresh.loopStart = 0;
	Audio_Lock();
	Music_Install(&gMusic[ch], &fresh, volume, pan);
	Audio_Unlock();
	printf("[Audio]: Music channel %u: intro and loop, %u Hz, %u channel(s), loop %s\n",
	       ch, fresh.rate, fresh.channels, loopFlag ? "on" : "off");
	return AUDIO_OK;
}

uint32_t Audio_MusicSetMaster(uint32_t ch, uint32_t volume)
{
	/* 0x004A2CE0 stores the value as given. */
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(ch >= AUDIO_MUSIC_CHANNELS) return AUDIO_BAD_CHANNEL;
	Audio_Lock();
	gMusic[ch].master = volume;
	Channel_Apply(&gMusic[ch]);
	Audio_Unlock();
	return AUDIO_OK;
}

uint32_t Audio_MusicSetVolume(uint32_t ch, uint32_t volume)
{
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(ch >= AUDIO_MUSIC_CHANNELS) return AUDIO_BAD_CHANNEL;
	Audio_Lock();
	gMusic[ch].volume = volume > 0x80 ? 0x80 : volume;
	Channel_Apply(&gMusic[ch]);
	Audio_Unlock();
	return AUDIO_OK;
}

uint32_t Audio_MusicSetPan(uint32_t ch, int32_t pan)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
	{
		if(pan > 0x80) pan = 0x80;
		if(pan < 0) pan = 0;
		Channel_SetPan(&gMusic[ch], (int32_t)((double)(pan - 0x40) * 0.015625 * 128.0));
	}
	Audio_Unlock();
	return r;
}

/*
 * The music player's vtable +0x0C (0x004A7600). Asked to stop while playing it
 * sets +0xBC, and the streaming thread (0x004A7890) stops the buffer at its
 * next notification and keeps the play cursor in +0xC0; asked to go while
 * stopped it puts the cursor back and plays. So 1 pauses and 0 resumes where
 * it paused, and neither touches the decoder. The pause takes effect here at
 * once instead of up to one notification later.
 */
uint32_t Audio_MusicPlay(uint32_t ch, uint32_t stop)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
		gMusic[ch].playing = stop ? 0 : 1;
	Audio_Unlock();
	return r;
}

uint32_t Audio_MusicFade(uint32_t ch, uint32_t volume, uint32_t time)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
	{
		AudioChannel_t* c = &gMusic[ch];
		uint32_t now = Audio_Now();
		c->fade1.start = now;
		c->fade1.end = now + time;
		c->fade1.to = (int32_t)(volume > 0x80 ? 0x80 : volume);
		c->fade1.from = c->fade1.current;
		c->fade1.active = 1;
	}
	Audio_Unlock();
	return r;
}

static uint32_t Fade2(AudioChannel_t* c, uint32_t time, int32_t to)
{
	uint32_t now = Audio_Now();
	c->fade2.start = now;
	c->fade2.end = now + time;
	c->fade2.to = to;
	c->fade2.from = c->fade2.current;
	c->fade2.active = 1;
	return AUDIO_OK;
}

uint32_t Audio_MusicFadeOut(uint32_t ch, uint32_t time)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
		Fade2(&gMusic[ch], time, 0);
	Audio_Unlock();
	return r;
}

uint32_t Audio_MusicFadeIn(uint32_t ch, uint32_t time)
{
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
		Fade2(&gMusic[ch], time, 0x80);
	Audio_Unlock();
	return r;
}

uint32_t Audio_MusicStatus(uint32_t ch, uint32_t* playing)
{
	/* Player vtable +0x20 (0x004A6520): GetStatus & DSBSTATUS_PLAYING. */
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
		*playing = gMusic[ch].playing ? 1 : 0;
	Audio_Unlock();
	return r;
}

uint32_t Audio_MusicLoopCount(uint32_t ch, uint32_t* count)
{
	/* 0x004A8670: the loops counted, less the last while it is still in the
	   buffer. A single stream never counts, so it answers 0. */
	Audio_Lock();
	uint32_t r = Music_Check(ch);
	if(r == AUDIO_OK)
	{
		AudioChannel_t* c = &gMusic[ch];
		*count = c->loopDeadline > Audio_Now() ? c->loopCount - 1 : c->loopCount;
	}
	Audio_Unlock();
	return r;
}

/* ------------------------------------------------------------------------- */
/* SE                                                                        */
/* ------------------------------------------------------------------------- */

uint32_t Audio_SESetMaster(uint32_t se, uint32_t volume)
{
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(se >= AUDIO_SE_CHANNELS) return AUDIO_BAD_CHANNEL;
	Audio_Lock();
	gSE[se].master = volume > 0x80 ? 0x80 : volume;   /* 0x004A29B0 clamps */
	Channel_Apply(&gSE[se]);
	Audio_Unlock();
	return AUDIO_OK;
}

uint32_t Audio_SESetVolume(uint32_t se, uint32_t volume)
{
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(se >= AUDIO_SE_CHANNELS) return AUDIO_BAD_CHANNEL;
	Audio_Lock();
	gSE[se].volume = volume > 0x80 ? 0x80 : volume;
	Channel_Apply(&gSE[se]);
	Audio_Unlock();
	return AUDIO_OK;
}

/*
 * 0x004A3670: set the pan (0x004A2A90, no clamp before the player's own),
 * start fade 1 at the volume asked for and fade 2 at full, stop the buffer and
 * rewind it (vtable +0x10, 0x004A6950), and play it once (vtable +0x08,
 * 0x004A68D0: SetVolume then Play with the flags of vtable +0x38, which for the
 * SE player is 0x004BE790 - no DSBPLAY_LOOPING).
 */
uint32_t Audio_SEPlay(uint32_t se, uint32_t volume, uint32_t pan)
{
	Audio_Lock();
	uint32_t r = SE_Check(se);
	if(r == AUDIO_OK)
	{
		AudioChannel_t* c = &gSE[se];
		c->pan = (int32_t)((double)((int32_t)pan - 0x40) * 0.015625 * 128.0);
		if(c->pan < -0x80) c->pan = -0x80;
		if(c->pan > 0x80) c->pan = 0x80;
		c->fade1.current = (int32_t)volume;
		c->fade2.active = 0;
		c->fade2.current = 0x80;
		c->cursor = 0;
		c->primed = 0;
		c->phase = 0.0;
		Channel_Apply(c);
		c->playing = c->frames > 0;
	}
	Audio_Unlock();
	return r;
}

uint32_t Audio_SEStop(uint32_t se)
{
	Audio_Lock();
	uint32_t r = SE_Check(se);
	if(r == AUDIO_OK)
	{
		gSE[se].playing = 0;
		gSE[se].cursor = 0;
		gSE[se].primed = 0;
	}
	Audio_Unlock();
	return r;
}

uint32_t Audio_SEFadeOut(uint32_t se, uint32_t time)
{
	Audio_Lock();
	uint32_t r = SE_Check(se);
	if(r == AUDIO_OK)
		Fade2(&gSE[se], time, 0);
	Audio_Unlock();
	return r;
}

uint32_t Audio_SEUnload(uint32_t se)
{
	/* 0x004A2170: the player gives its decoder back to be deleted, and the
	   channel's +0x08 and +0x00 are cleared. */
	if(!Audio_Ready()) return AUDIO_NO_DEVICE;
	if(se >= AUDIO_SE_CHANNELS) return AUDIO_BAD_CHANNEL;
	Audio_Lock();
	AudioChannel_t* c = &gSE[se];
	int16_t* pcm = c->pcm;
	c->pcm = NULL;
	c->frames = 0;
	c->cursor = 0;
	c->playing = 0;
	c->loaded = 0;
	Audio_Unlock();
	free(pcm);
	return AUDIO_OK;
}

struct AudioSEJob
{
	uint32_t      se;
	AudioPart_t   part;
	uint32_t      rampMs;
	double        gain;
	int16_t*      pcm;
	uint32_t      frames;
	uint32_t      rate;
	SDL_Thread*   thread;
	SDL_atomic_t  done;
};

/*
 * The decode 0x004A9410 does into its memory stream: the whole stream, as
 * 16-bit samples through ov_read, each multiplied by the gain (0x004A9160, the
 * double at +0x48; truncated, then clamped to 16 bits), into a buffer sized from
 * the header's frame count and so zero past a stream that runs short; then the
 * fade-in of +0x34 (0x004AADB0): n = ms / 1000 * rate + ms % 1000 * rate / 1000
 * frames, refused (and skipped) when longer than the sound, each sample of
 * frame i scaled by i / n.
 */
static int SDLCALL SEJob_Decode(void* data)
{
	AudioSEJob_t* job = (AudioSEJob_t*)data;
	uint32_t frames = job->frames;
	uint32_t got = 0;
	while(got < frames)
	{
		int want = (int)((frames - got) > 4096 ? 4096 : (frames - got));
		int n = stb_vorbis_get_samples_short_interleaved(job->part.vorbis, 1, job->pcm + got, want);
		if(n <= 0)
			break;
		got += (uint32_t)n;
	}
	if(job->gain != 1.0)
	{
		for(uint32_t i = 0; i < got; i++)
		{
			int32_t v = (int32_t)(job->pcm[i] * job->gain);
			if(v < -0x8000) v = -0x8000;
			if(v > 0x7FFF) v = 0x7FFF;
			job->pcm[i] = (int16_t)v;
		}
	}
	uint32_t ms = job->rampMs;
	uint32_t n = (ms / 1000) * job->rate + ((ms % 1000) * job->rate) / 1000;
	if(n <= frames)
	{
		for(uint32_t i = 0; i < n; i++)
			job->pcm[i] = (int16_t)(int32_t)((double)job->pcm[i] * (double)i / (double)n);
	}
	SDL_AtomicSet(&job->done, 1);
	return 0;
}

static void SEJob_Free(AudioSEJob_t* job)
{
	if(job == NULL)
		return;
	Part_Free(&job->part);
	free(job->pcm);
	free(job);
}

AudioSEJob_t* Audio_SEBegin(uint32_t se, uint8_t* data, size_t size, uint32_t rampMs,
                            double gain, double speed, uint32_t* result)
{
	/* 0x004A3FD0 */
	if(!Audio_Ready())
	{
		free(data);
		*result = AUDIO_NO_DEVICE;
		return NULL;
	}
	if(se >= AUDIO_SE_CHANNELS)
	{
		free(data);
		*result = AUDIO_BAD_CHANNEL;
		return NULL;
	}
	AudioSEJob_t* job = (AudioSEJob_t*)calloc(1, sizeof(AudioSEJob_t));
	if(job == NULL)
	{
		free(data);
		*result = AUDIO_REFUSED;
		return NULL;
	}
	job->se = se;
	job->part.data = data;
	job->part.size = size;
	job->rampMs = rampMs;
	job->gain = gain;
	uint32_t r = Bw_Check(data, size, job->part.header);
	if(r == AUDIO_OK && job->part.header[BW_CODEC] != 3)
	{
		static const char* const names[3] = {
			"the SE decoder at 0x004A2510", "the SE decoder at 0x004A2E80", "the SE decoder at 0x004A2570" };
		printf("[Audio]: Refused: an SE of codec %u needs %s, which is not written; only codec 3 (Ogg Vorbis) is\n",
		       job->part.header[BW_CODEC], names[job->part.header[BW_CODEC]]);
		r = AUDIO_REFUSED;
	}
	if(r == AUDIO_OK && job->part.header[BW_CHANNELS] != 1)
		r = AUDIO_NOT_MONO;
	if(r == AUDIO_OK)
		r = Part_Open(&job->part);
	if(r == AUDIO_OK && speed != 1.0)
	{
		printf("[Audio]: Refused: an SE played at %.4f times its speed needs the block "
		       "time-stretch of 0x004A6E80 / 0x004A7070, which is not written\n", speed);
		r = AUDIO_REFUSED;
	}
	if(r == AUDIO_OK && job->part.header[BW_RATE] == 0)
		r = AUDIO_OPEN_FAILED;
	if(r != AUDIO_OK)
	{
		SEJob_Free(job);
		*result = r;
		return NULL;
	}
	job->frames = job->part.header[BW_FRAMES];
	job->rate = job->part.header[BW_RATE];
	job->pcm = (int16_t*)calloc(job->frames ? job->frames : 1, sizeof(int16_t));
	if(job->pcm == NULL)
	{
		SEJob_Free(job);
		*result = AUDIO_REFUSED;
		return NULL;
	}
	SDL_AtomicSet(&job->done, 0);
	job->thread = SDL_CreateThread(SEJob_Decode, "bgi-se", job);
	if(job->thread == NULL)
		SEJob_Decode(job);
	*result = AUDIO_OK;
	return job;
}

/*
 * 0x004A3C90's tail: the player takes the new decoder in place of the old one
 * (vtable +0x1C, the old deleted; +0x24 the speed; +0x04 opens it, stopped),
 * fade 1 is set inactive at full and fade 2 at full, the channel is loaded and
 * its volume applied. The master and volume words are kept.
 */
static uint32_t SEJob_Install(AudioSEJob_t* job)
{
	if(job->thread != NULL)
	{
		SDL_WaitThread(job->thread, NULL);
		job->thread = NULL;
	}
	Audio_Lock();
	AudioChannel_t* c = &gSE[job->se];
	int16_t* old = c->pcm;
	c->pcm = job->pcm;
	job->pcm = NULL;
	c->frames = job->frames;
	c->cursor = 0;
	c->rate = job->rate;
	c->channels = 1;
	c->playing = 0;
	c->primed = 0;
	c->fade1.active = 0;
	c->fade1.current = 0x80;
	c->fade2.current = 0x80;
	c->fade2.active = 0;
	c->loaded = 1;
	Channel_Apply(c);
	Audio_Unlock();
	free(old);
	printf("[Audio]: SE channel %u: %u frames at %u Hz\n", job->se, job->frames, job->rate);
	SEJob_Free(job);
	return AUDIO_OK;
}

int Audio_SEPoll(AudioSEJob_t* job, uint32_t* result)
{
	if(!SDL_AtomicGet(&job->done))
		return 0;
	*result = SEJob_Install(job);
	return 1;
}

uint32_t Audio_SEFinish(AudioSEJob_t* job)
{
	return SEJob_Install(job);
}
