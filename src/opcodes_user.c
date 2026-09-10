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
	printf("[Thread %d]: %sUser Executing opcode User.%s (0x%.2X / %d) (%d)\n", thread->threadId, TLevel[thread->level], mnemonic, opcode, opcode, GoldenLog_Time());

	thread->level++;
	uint32_t res;
	if(opcode == 0xF0)
		res = Opcode_User_Define(thread);
	else if(opcode == 0xF1)
		res = Opcode_User_Undefine(thread);
	else if(opcode < USER_INSTRUCTION_COUNT)
	{
		// Calling one is the half that is not read out of the original yet. The
		// engine keeps the program's bytes against the number; what it does with
		// them on a call is still to be worked out, so say so rather than guess.
		if(gUserInstructions[opcode].code == NULL)
			printf("[Thread %d]: %sError: user instruction 0x%.2X was never defined\n", thread->threadId, TLevel[thread->level], opcode);
		else
			printf("[Thread %d]: %sError: calling user instruction 0x%.2X (\"%s\") is not implemented\n", thread->threadId, TLevel[thread->level], opcode, gUserInstructions[opcode].program);
		res = 0xFFFFFFFF;
	}
	else
	{
		// 0xF8 (0x00498D00) is a third management instruction, not read yet; the
		// original treats every other number above 0xEF as fatal.
		printf("[Thread %d]: %sError: opcode 0xFF%.2X (%d) not implemented\n", thread->threadId, TLevel[thread->level], opcode, opcode);
		res = 0xFFFFFFFF;
	}
	thread->level--;
	return res;
}
