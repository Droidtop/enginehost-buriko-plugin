#ifndef SELECT_H_
#define SELECT_H_

#include <stdint.h>

#include "thread.h"

/*
 * Grp0 0xB0 / 0xB1 (0x90B0 / 0x90B1): choice icons in a window, run as the select process
 * the script thread waits on (CProcSelectIcon, 0x658 bytes, and CProcSelectIconEx). From
 * the local lane's trace on Droidtop/tracker#123, 2026-11-05.
 *
 * Pops, top first: pixel hit testing (a), the hotkey list or 0 (b), the focus mode (0-3), the
 * icon list, an unused pointer, the choice count (1-64) and the window. A list is 4 words a
 * choice for 0xB0 (x, y, image, lit image) and 16 for 0xB1 (x, y, image, ..., [15] hit mask).
 * When it finishes it pushes the pointer's x and y inside the chosen icon, then the chosen
 * index (-1 cancelled), on top.
 */
uint32_t Select_Start(Thread_t* thread, int ex);

#endif
