#ifndef OPCODES_USER_H
#define OPCODES_USER_H

#include <stdint.h>
#include "thread.h"

/*
 * Base opcode 0xFF: the user-defined instruction group.
 *
 * Unlike the other groups this one has no fixed table of handlers. Sub-opcodes
 * 0x00 to 0xEF are whatever the running script has defined for itself, and the
 * three above them are the management instructions that define them.
 */
uint32_t Opcode_User(Thread_t* thread);

#endif
