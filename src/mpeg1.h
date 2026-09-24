#ifndef MPEG1_H_
#define MPEG1_H_

#include <stdint.h>
#include <stddef.h>

/*
 * The movie decoder.
 *
 * BGI's movies are MPEG-1 system streams (ISO/IEC 11172-1): packs carrying one
 * MPEG-1 video stream (ISO/IEC 11172-2, PES 0xE0..0xEF) and one MPEG audio
 * stream (ISO/IEC 11172-3, PES 0xC0..0xDF). A bare video elementary stream
 * (one that starts with a sequence header) is accepted as well.
 *
 * The whole file is handed over in memory, as the engine reads every archive
 * member into memory. Open indexes the packets of both streams once; video and
 * audio are then pulled independently, each from its own queue of PES payloads,
 * so the caller never has to follow the file's interleaving.
 *
 * Both streams share one clock: 0 is the earliest PTS in the file. Video frames
 * carry their presentation time on that clock; audio starts at 0 too (silence
 * is put in front when the first audio PTS is later than 0), so audio frame n
 * plays at n / Mpeg1_AudioRate() seconds.
 *
 * The video decoder is written here. The audio is MPEG-1 layer I, II or III,
 * decoded by minimp3 (vendor/minimp3.h).
 *
 * Nothing here is thread-safe; one decoder is driven from one thread at a time.
 */

typedef struct Mpeg1 Mpeg1_t;

/* Indexes the stream and reads the first sequence header. The data must stay
   valid (and unchanged) until Mpeg1_Close. Returns NULL, having printed why,
   when there is no MPEG-1 video stream in it or it uses something this decoder
   refuses by name. */
Mpeg1_t* Mpeg1_Open(const uint8_t* data, size_t size);
void     Mpeg1_Close(Mpeg1_t* m);

/* The displayed size (the sequence header's horizontal and vertical size). */
int      Mpeg1_Width(const Mpeg1_t* m);
int      Mpeg1_Height(const Mpeg1_t* m);
/* Frames per second from the sequence header's picture_rate. */
double   Mpeg1_FrameRate(const Mpeg1_t* m);
/* Seconds from 0 to the end of the last video frame, from the PTS index. */
double   Mpeg1_Duration(const Mpeg1_t* m);

/* 0 for both when the file has no audio stream (or it could not be read). */
int      Mpeg1_AudioRate(const Mpeg1_t* m);
int      Mpeg1_AudioChannels(const Mpeg1_t* m);

/* Decodes the next video frame in display order and writes it, width x height,
   4 bytes to the pixel in B, G, R, x order (x is 0xFF), stride bytes to the row.
   *pts (when not NULL) receives its presentation time in seconds. Returns 1
   for a frame, 0 at the end of the stream (or on an error it cannot get past). */
int      Mpeg1_NextFrame(Mpeg1_t* m, uint8_t* bgrx, int stride, double* pts);

/* Writes up to maxFrames frames of interleaved signed 16-bit audio,
   Mpeg1_AudioChannels() samples to the frame. Returns the frames written; 0
   only at the end of the audio. */
int      Mpeg1_ReadAudio(Mpeg1_t* m, int16_t* out, int maxFrames);

#endif
