#ifndef THREAD_H_
#define THREAD_H_

#include <stdint.h>

typedef struct Engine Engine_t;
struct Process;

// Program structure:
// 4 bytes - Code offset
// 4 bytes - Program size
typedef struct Program Program_t;
struct Program
{
	char* filename;
	uint32_t size;
	uint32_t location;
	Program_t* previousProgram;
};

typedef struct Memory Memory_t;
struct Memory
{
	int isAllocated;
	uint8_t* mem;
	uint32_t size;
};


// A thread's message queue, +0x5C and +0x60 of the original's thread object:
// values arrive at the tail (0x004453A0) and are taken from the head
// (0x004453E0).
typedef struct Message Message_t;
struct Message
{
	uint32_t   value;
	Message_t* next;
};

typedef struct Thread Thread_t;
// Bit 31 of a thread's flags, which the scheduler sets when the thread's
// outermost frame returns. Same bit the original sets at 0x0048D06C.
#define THREAD_FLAG_TERMINATED 0x80000000
// Bit 0 of the same word: the thread is waiting on the process at +0x58 of
// the original's thread object. 0x004452A0 sets it, 0x00445090 clears it.
#define THREAD_FLAG_WAITING 0x00000001

struct Thread
{
	uint32_t programId;
	uint32_t threadId;
	Thread_t* previousThread;
	uint32_t flags;
	uint32_t stackPointer;
	uint32_t instructionPointer;
	uint32_t nextInstructionPointer;
	uint32_t basePointer;
	uint32_t stackSize;
	Memory_t stackMemoryConfig;
	uint32_t* stack;
	uint32_t codeSize;
	Memory_t codeMemoryConfig;
	uint8_t* code;
	Program_t* programs;
	uint32_t programCount;
	uint32_t codeSpaceUsed;
	uint32_t localMemSize;
	Memory_t localMemConfig;
	uint8_t* localMem;
	void* unknownStruct;
	void* unknownFuncPointer;
	uint32_t unknownTimestamp;
	uint32_t unknownField2;
	uint32_t unknownField3;

	int level;
	int running;
	int ticks;
	int error;
	Engine_t* engine;
	uint16_t opcode;
	int waitTicks;
	struct Process* process;

	Message_t* messages;
	Message_t* messagesTail;

	int queuePush;
	int queuePushQueue[10];

	int inBasicOpcode;
	int silenceBasicOpcodeLog;
	int silenceYield;
	int silenceGlobalList;
};

extern char* TLevel[4];

// Answers where the program landed, or THREAD_LOAD_FAILED if the file cannot
// hold the program its own header describes.
#define THREAD_LOAD_FAILED 0xFFFFFFFFu
uint32_t Thread_LoadCode(Thread_t* thread, uint8_t* code, size_t codeSize, const char* filename);
uint32_t Thread_DeleteProgram(Thread_t* thread);
void Thread_PushStack(Thread_t* thread, uint32_t data);
uint32_t Thread_PopStack(Thread_t* thread);
uint8_t Thread_ReadCode8(Thread_t* thread);
uint16_t Thread_ReadCode16(Thread_t* thread);
uint32_t Thread_ReadCode32(Thread_t* thread);
uint8_t Thread_ReadImm8(Thread_t* thread);
uint32_t Thread_Execute(Thread_t* thread);
uint32_t Thread_GetBasePointer(Thread_t* thread);
void Thread_SetBasePointer(Thread_t* thread, uint32_t value);
uint32_t Thread_GetInstructionPointer(Thread_t* thread);
// Names the program an address in the thread's code space belongs to,
// as "<program>+0x<offset>", in a static buffer.
const char* Thread_Where(Thread_t* thread, uint32_t address);
void Thread_SetInstructionPointer(Thread_t* thread, uint32_t value);
void Thread_SetUnknownTimestamp(Thread_t* thread, uint32_t value);
uint8_t* Thread_PopAndResolveAddress(Thread_t* thread);
uint8_t* Thread_ResolveAddr(Thread_t* thread, uint32_t address);
uint32_t Thread_WriteIntToMemory(Thread_t* thread, uint8_t* ptr, uint8_t size, uint32_t value);
void Thread_WriteReturnAddr(Thread_t* thread, uint32_t addr);
uint32_t Thread_ReadReturnAddr(Thread_t* thread);
uint32_t Thread_GetLocalMemSize(Thread_t* thread);
uint32_t Thread_GetThreadID(Thread_t* thread);
void Thread_Sprintf(Thread_t* thread, char* dst, const char* fmt);
void Thread_SchedulePush(Thread_t* thread, uint32_t data);
// 0x004452A0: the thread waits on this process from now on. Any process it
// was already waiting on is destroyed, as there.
void Thread_SetProcess(Thread_t* thread, struct Process* process);
// 0x004452D0: run the process the thread is waiting on. 1 when it finished
// (it is destroyed and the thread may run again), 0 while it is still
// waiting, -1 when the thread had no process at all.
int Thread_RunProcess(Thread_t* thread);
void Thread_PostMessage(Thread_t* thread, uint32_t value);
// 1 when a message was taken and written to value, 0 when the queue was empty.
int Thread_TakeMessage(Thread_t* thread, uint32_t* value);

#endif