#ifndef MOVIE_H_
#define MOVIE_H_

#include <stdint.h>
#include <stddef.h>

#include "renderer.h"

/*
 * The movie player (fureraba.exe 0x0048F110 to 0x0048F870).
 *
 * The original builds a DirectShow graph (0x0048F110): a filter graph at
 * 0x00566930, its IMediaControl (0x00566948), IMediaEventEx (0x0056694C),
 * IMediaSeeking (0x00566950) and IBasicAudio (0x00566954), and a video renderer
 * of its own (0x0045B810) that hands every frame to the drawing device, so the
 * movie is shown over the game's own frame while the frame loop keeps running.
 * 0x0048F400 plays a file: the loose file in the game's folder first, and when
 * the graph cannot find it and an archive was named, the member read out of that
 * archive (0x00467D30) through a memory source; rate 1.0, the movie volume
 * (0x00566928, Grp0 0xF3), the graph's completion message (0x8000), Run, and the
 * stop position in milliseconds as its answer. 0x00566960 is 1 while it plays.
 * The window procedure takes the completion (EC_COMPLETE, 0x00499D9D) as the end:
 * the flag drops, the graph is stopped (0x0048F270) and a full redraw is asked for
 * (0x00461EF0).
 *
 * Here the file is decoded by src/mpeg1.c: the video onto the back buffer after
 * the frame is composed, the audio through an SDL device of its own.
 */

// 0x0048F400. data is the whole file, which the player takes over. Answers the
// stop position in milliseconds, or 0 when it cannot be played (the original's
// failure answer).
uint32_t Movie_Play(uint8_t* data, size_t size);
// 0x0048F270: stop and throw the movie away.
void     Movie_Stop(void);
// 0x0048F810: 0x00566960.
int      Movie_IsPlaying(void);
// 0x0048F820 (Grp0 0xF2): 1 while playing and the position is before the stop
// position.
int      Movie_Status(void);
// Called once a frame after the screen is composed: the movie's current frame is
// drawn over it, and the end of the movie is noticed.
void     Movie_Update(Bitmap_t* back);
// The movie volume changed (Grp0 0xF3); hundredths of a decibel, as DirectSound.
void     Movie_SetAttenuation(int hundredthsOfDb);

#endif
