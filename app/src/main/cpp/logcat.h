/*
 * What logcat.c learns from the engine's output that the wrapper needs: the
 * line the engine stopped its script with. See logcat.c.
 */
#ifndef ENGINEHOST_BURIKO_LOGCAT_H
#define ENGINEHOST_BURIKO_LOGCAT_H

#include <stddef.h>

/* Waits (a second at most) until every line written so far has been read. */
void LogcatDrain(void);

/* The engine's "[Engine]: ... Stopping." line without those two ends; 0 if it printed none. */
int LogcatStopReason(char* out, size_t size);

#endif
