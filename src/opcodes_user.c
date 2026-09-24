#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include "engine.h"
#include "opcodes.h"
#include "golden_log.h"
#include "opcodes_user.h"
#include "thread.h"

// 0xFF 0xF0 (fureraba.exe 0x00498BF0): define a user instruction. It pops the
// program's name, then the archive it lives in, then the number to bind them to.
// A number of 0xF0 or higher is fatal, with the engine's own message about an
// invalid user-defined instruction / program number.
static uint32_t Opcode_User_Define(Thread_t* thread)
{
	const char* program = (const char*)Thread_PopAndResolveAddress(thread);
	const char* archive = (const char*)Thread_PopAndResolveAddress(thread);
	uint32_t number = Thread_PopStack(thread);
	if(number >= USER_INSTRUCTION_COUNT)
	{
		printf("[Thread %d]: %sError: %d is not a valid user instruction number\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFF;
	}

	Engine_DefineUserInstruction(thread->engine, number, archive, program);
	return 0;
}

// 0xFF 0xF1 (0x00498CA0): forget one again, with the same range check.
static uint32_t Opcode_User_Undefine(Thread_t* thread)
{
	uint32_t number = Thread_PopStack(thread);
	if(number >= USER_INSTRUCTION_COUNT)
	{
		printf("[Thread %d]: %sError: %d is not a valid user instruction number\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFF;
	}

	Engine_UndefineUserInstruction(number);
	printf("[Thread %d]: %sUser instruction 0x%.2X is undefined again\n", thread->threadId, TLevel[thread->level], number);
	return 0;
}

// The "Mediation program" (0x004EC6E8) a call runs through, as the original
// builds it on its stack at 0x00498D98: a program header of 0x10 bytes naming 0x10
// bytes of code, and the code - CodeOffset +0x10 (the start of whatever is loaded
// next), Call, then 0xFF 0xF8 for the return.
static const uint8_t kMediation[0x20] = {
	0x10, 0, 0, 0,   0x10, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
	0x06, 0x10, 0x00, 0x16,   0xFF, 0xF8, 0x00, 0x00,   0, 0, 0, 0,   0, 0, 0, 0,
};

// Calling a user instruction (0x00498D6B): the mediation program is pushed onto the
// thread's code space, the instruction's own program right after it (0x00444DC0 for
// each, which is why CodeOffset +0x10 lands on it), the return address - the
// instruction after the two-byte 0xFF nn - goes onto the call stack (0x004451F0),
// and the thread continues at the mediation program. An undefined number
// (0x004EC67C) and a code space that cannot take either program (0x004EC700) are
// fatal, as is a call stack with no room (0x004E7748).
static uint32_t Opcode_User_Call(Thread_t* thread, uint8_t number)
{
	UserInstruction_t* instruction = &gUserInstructions[number];
	if(instruction->code == NULL)
	{
		printf("[Thread %d]: %sError: user instruction $FF%.2X is not defined\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFC;
	}
	uint32_t mediation = Thread_LoadCode(thread, (uint8_t*)kMediation, sizeof(kMediation), "Mediation program");
	if(mediation == THREAD_LOAD_FAILED)
	{
		printf("[Thread %d]: %sError: the code area is too small to run user instruction $FF%.2X\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFC;
	}
	if(Thread_LoadCode(thread, instruction->code, instruction->codeSize, instruction->program) == THREAD_LOAD_FAILED)
	{
		printf("[Thread %d]: %sError: the code area is too small to run user instruction $FF%.2X\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFC;
	}
	if(thread->basePointer + 4 > Thread_GetLocalMemSize(thread))
	{
		printf("[Thread %d]: %sError: no room on the call stack for user instruction $FF%.2X\n", thread->threadId, TLevel[thread->level], number);
		return 0xFFFFFFFC;
	}
	Thread_WriteReturnAddr(thread, Thread_GetInstructionPointer(thread) + 2);
	Thread_SetInstructionPointer(thread, mediation);
	return 0;
}

// 0xFF 0xF8 (0x00498D00): the return from a user instruction. Both programs come
// off the code space (0x00444E60 twice) and the thread goes back to the address
// the call left on the call stack (0x004451D0).
static uint32_t Opcode_User_Return(Thread_t* thread)
{
	Thread_DeleteProgram(thread);
	Thread_DeleteProgram(thread);
	Thread_SetInstructionPointer(thread, Thread_ReadReturnAddr(thread));
	return 0;
}

uint32_t Opcode_User(Thread_t* thread)
{
	uint8_t opcode = Thread_ReadCode8(thread);
	thread->inBasicOpcode = 0;
	thread->opcode = (thread->opcode << 8) | opcode;

	const char* mnemonic = "--Unknown--";
	if(opcode < USER_INSTRUCTION_COUNT)
		mnemonic = "ScriptDefined";
	else if(opcode == 0xF0)
		mnemonic = "Define";
	else if(opcode == 0xF1)
		mnemonic = "Undefine";
	else if(opcode == 0xF8)
		mnemonic = "Return";
	printf("[Thread %d]: %sUser Executing opcode User.%s (0x%.2X / %d) (%d)\n", thread->threadId, TLevel[thread->level], mnemonic, opcode, opcode, GoldenLog_Time());

	thread->level++;
	uint32_t res;
	if(opcode == 0xF0)
		res = Opcode_User_Define(thread);
	else if(opcode == 0xF1)
		res = Opcode_User_Undefine(thread);
	else if(opcode == 0xF8)
		res = Opcode_User_Return(thread);
	else if(opcode < USER_INSTRUCTION_COUNT)
		res = Opcode_User_Call(thread, opcode);
	else
	{
		// The original treats every other number above 0xEF as fatal (0x004EC6AC).
		printf("[Thread %d]: %sError: $FF%.2X is not a user instruction or a management instruction\n", thread->threadId, TLevel[thread->level], opcode);
		res = 0xFFFFFFFC;
	}
	thread->level--;
	return res;
}
