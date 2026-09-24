#ifndef AUDIO_H_
#define AUDIO_H_

#include <stdint.h>
#include <stddef.h>

/*
 * The sound library.
 *
 * The original links a small DirectSound library (0x004A1FD0 .. 0x004AAE80) that
 * the Snd0 opcodes drive. It keeps two banks of channels behind one critical
 * section (0x005089E8):
 *
 *   music  16 channels of 0x44 bytes at 0x005085A8, each a streaming player
 *          (0x004A7360, vtable 0x004DBEA4) fed by a decoder;
 *   SE     64 channels of 0x44 bytes at 0x00508A08, each a static buffer
 *          (0x004A6C90, vtable 0x004DBE3C) filled once from a decoder.
 *
 * A channel's 0x44 bytes are what AudioChannel_t below keeps under the same
 * names: +0x00 loaded, +0x04 the player, +0x08 the decoder, +0x0C the master
 * volume, +0x10 the volume, +0x14 and +0x2C two fades of six words each
 * (active, start, end, from, to, current). Every setter ends by recomputing the
 * attenuation from the four volume words (0x004A2350) and handing it to the
 * player, which turns it into DirectSound hundredths of a decibel (0x004A6A70).
 * A 20 ms window timer (SetTimer at 0x004A3640, TimerProc 0x004A2F20) steps the
 * fades (0x004A2490).
 *
 * Here the players are voices mixed into one SDL audio device; the decoder is
 * stb_vorbis over the Ogg stream that follows the member's 0x40-byte "bw  "
 * header. Every entry point takes the device lock, as every one of the
 * original's takes the critical section.
 *
 * Results are the library's own codes.
 */

#define AUDIO_MUSIC_CHANNELS 16
#define AUDIO_SE_CHANNELS    64

#define AUDIO_OK             0x00
#define AUDIO_NO_FILE        0x0C   /* the stream could not be opened (0x004A4C90 / 0x004A4F40) */
#define AUDIO_NOT_BW         0x0E   /* header short, not "bw  ", or a decoder refused it */
#define AUDIO_NOT_MONO       0x12   /* mapped to 0x80000002 by 0x00498330 */
#define AUDIO_NOT_LOADED     0x13   /* 0x004A2310 / 0x004A22D0 */
#define AUDIO_NO_DEVICE      0x14   /* the device flags at 0x005085A4 are not both set */
#define AUDIO_BAD_CHANNEL    0x15
#define AUDIO_OPEN_FAILED    0x16   /* the player refused the decoder (vtable +0x04) */
#define AUDIO_PLAY_FAILED    0x17   /* 0x004A3670: the player's Play failed */
/* Not a code of the original: a path this engine refuses by name (it prints why). */
#define AUDIO_REFUSED        0xFFFFFFFFu

/* Opens the device (0x004A1FD0) and starts the fade timer (0x004A3640). Safe to
   call more than once; returns 1 when the device is open. When SDL cannot open
   an audio device the library stays uninitialised, exactly as the original does
   when DirectSound fails, and every call answers AUDIO_NO_DEVICE. */
int      Audio_Init(void);
/* 0x004A2250: unloads every channel and closes the device. */
void     Audio_Free(void);

/* ---- music (0x005085A8) ---- */
uint32_t Audio_MusicStop(uint32_t ch);                                 /* 0x004A35B0 -> 0x004A2E20 */
/* 0x004A3700. Takes ownership of data (a whole bw member). flags bit 0: set the
   loop flag from bit 1 and the loop start to 0 (0x004A86B0); 0 keeps the
   header's own (+0x18 loop flag, +0x1C loop start). */
uint32_t Audio_MusicLoad(uint32_t ch, uint8_t* data, size_t size,
                         uint32_t volume, uint32_t pan, uint32_t flags);
/* 0x004A39A0: an intro and a loop part played as one stream. Takes ownership of
   both buffers. */
uint32_t Audio_MusicLoadPair(uint32_t ch, uint8_t* intro, size_t introSize,
                             uint8_t* loop, size_t loopSize, uint32_t loopFlag,
                             uint32_t volume, uint32_t pan);
uint32_t Audio_MusicSetMaster(uint32_t ch, uint32_t volume);            /* 0x004A3400 -> 0x004A2CE0 */
uint32_t Audio_MusicSetVolume(uint32_t ch, uint32_t volume);            /* 0x004A3490 -> 0x004A2D40 */
uint32_t Audio_MusicSetPan(uint32_t ch, int32_t pan);                   /* 0x004A3520 -> 0x004A2DB0 */
uint32_t Audio_MusicPlay(uint32_t ch, uint32_t stop);                   /* 0x004A32E0 -> 0x004A2C40 */
uint32_t Audio_MusicFade(uint32_t ch, uint32_t volume, uint32_t time);  /* 0x004A3370 -> 0x004A2C70 */
uint32_t Audio_MusicFadeOut(uint32_t ch, uint32_t time);                /* 0x004A3130 -> 0x004A2B20 */
uint32_t Audio_MusicFadeIn(uint32_t ch, uint32_t time);                 /* 0x004A31C0 -> 0x004A2B80 */
uint32_t Audio_MusicStatus(uint32_t ch, uint32_t* playing);             /* 0x004A26F0 */
uint32_t Audio_MusicLoopCount(uint32_t ch, uint32_t* count);            /* 0x004A2730 */

/* ---- SE (0x00508A08) ---- */
uint32_t Audio_SESetMaster(uint32_t se, uint32_t volume);               /* 0x004A2F80 -> 0x004A29B0 */
uint32_t Audio_SESetVolume(uint32_t se, uint32_t volume);               /* 0x004A3010 -> 0x004A2A20 */
uint32_t Audio_SEPlay(uint32_t se, uint32_t volume, uint32_t pan);      /* 0x004A3F40 -> 0x004A3670 */
uint32_t Audio_SEStop(uint32_t se);                                     /* 0x004A30A0 -> 0x004A2AF0 */
uint32_t Audio_SEFadeOut(uint32_t se, uint32_t time);                   /* 0x004A3250 -> 0x004A2BE0 */
uint32_t Audio_SEUnload(uint32_t se);                                   /* 0x004A28A0 -> 0x004A2170 */

/*
 * SE registration, 0x004A4880 -> 0x004A3FD0 -> 0x004A3C90. The original runs it
 * on a worker thread fed by a queue (0x00498290 / 0x00498330). Here the checks
 * and the stream open are done at once by Audio_SEBegin, which returns a job
 * whose decode runs on its own thread; Audio_SEPoll answers 1 once the job has
 * finished and been installed in the channel, with the library's result, and
 * frees the job. data is taken over in every case.
 *
 * rampMs is the decoder's +0x34 argument (0x004AADB0: a linear fade-in over the
 * first rampMs milliseconds), gain its +0x20 argument (0x004A9080: every sample
 * is multiplied by it, truncated and clamped), speed the player's +0x24 (the
 * 30 ms block time-stretch at 0x004A6E80 when it is not 1.0).
 */
typedef struct AudioSEJob AudioSEJob_t;
AudioSEJob_t* Audio_SEBegin(uint32_t se, uint8_t* data, size_t size, uint32_t rampMs,
                            double gain, double speed, uint32_t* result);
int           Audio_SEPoll(AudioSEJob_t* job, uint32_t* result);
/* Waits for the job and installs it (for callers that cannot yield). */
uint32_t      Audio_SEFinish(AudioSEJob_t* job);

#endif /* AUDIO_H_ */
