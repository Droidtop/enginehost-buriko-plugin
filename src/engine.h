#ifndef ENGINE_H_
#define ENGINE_H_

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "thread.h"

typedef struct Renderer Renderer_t;
typedef struct Engine Engine_t;
struct Engine
{
	uint32_t threadCounter;
	uint32_t programCounter;
	Thread_t* threads;
	Program_t* programs;
	Memory_t* memory;
    uint8_t* auxMemory[48];
    uint32_t globalBufferSize;
    uint8_t* globalMem;
    int isRunning;

    uint32_t windowObjectHandle;
    uint32_t filterObjectHandle;
    uint32_t knobObjectHandle;

    uint32_t nextThreadRequest;

    Renderer_t* renderer;
    SDL_Window* window;
};

extern Engine_t* gEngine;

uint32_t Engine_LoadProgram(Engine_t* engine, const char* archive, const char* filename, uint32_t stackSize, uint32_t codeSize, uint32_t memorySize);
Thread_t* Engine_CreateThread(Engine_t* engine, uint32_t stackSize, uint32_t codeSize, uint32_t memorySize);
uint8_t* Engine_ReadFile(Engine_t* engine, const char* archive, const char* filename, size_t* outSize);
uint32_t Engine_ReadFileToMemory(Engine_t* engine, const char* archive, const char* filename, uint8_t* buffer);
void Engine_Execute(Engine_t* engine);
void Engine_ExecuteThread(Engine_t* engine, uint32_t threadId, int ticks);
Thread_t* Engine_GetThreadById(Engine_t* engine, uint32_t threadId);
void Engine_Free(Engine_t* engine);
void Engine_Init(Engine_t* engine);

extern uint32_t gUnknownVal001;
void SetGlobalUnknownVal001(uint32_t value);

extern uint32_t gIdleWaitTime;
void Engine_SetIdleWaitTime(uint32_t value);

extern uint32_t gDisplayModeWidth[8];
extern uint32_t gDisplayModeHeight[8];
uint32_t Engine_SetDisplayModeSize(uint32_t index, uint32_t width, uint32_t height);

extern uint32_t gDisplayFlagUnknown98;
uint32_t Engine_SetDisplayFlagUnknown98(uint32_t value);

extern int gMousePosX;
extern int gMousePosY;
extern int gMousePosPending;
void Engine_SetMousePosition(int x, int y);

extern uint32_t gGrp1FlagUnknown13;
void Engine_SetGrp1FlagUnknown13(uint32_t value);

typedef struct FontSubstitution
{
	char* name;
	char* replacement;
	int charset;
	struct FontSubstitution* next;
} FontSubstitution_t;

typedef struct FontName
{
	uint32_t id;
	char* name;
	struct FontName* next;
} FontName_t;

extern FontSubstitution_t* gFontSubstitutions;
extern char* gFontSubstitutionDefault;
void Engine_SetFontSubstitution(const char* name, const char* replacement);
const char* Engine_GetFontSubstitution(const char* name);
uint32_t Engine_EnumerateFontFamilies(char* buffer);
extern uint32_t gFunctionParameters[4];
uint32_t Engine_SetFunctionParameter(uint32_t function, int32_t value);
void Engine_SetFontCharset(const char* name, int charset);
extern FontName_t* gFontNames;
uint32_t Engine_InternFontName(const char* name);

typedef struct FontAdjust
{
	char* name;
	uint32_t scaleX;
	uint32_t scaleY;
	int32_t originX;
	int32_t originY;
	struct FontAdjust* next;
} FontAdjust_t;

extern FontAdjust_t* gFontAdjusts;
// 0 on success, 0x80000005 for a rejected scale, 0x80000006 for a rejected origin.
uint32_t Engine_SetFontAdjust(const char* name, uint32_t scaleX, uint32_t scaleY, int32_t originX, int32_t originY);

int Engine_InitGlobalMemory(Engine_t* engine, uint32_t level);

uint32_t Engine_AllocAuxMemory(Engine_t* engine, uint32_t size);
uint8_t* Engine_GetAuxMemory(Engine_t* engine, uint8_t slot);
uint32_t Engine_FreeAuxMemory(Engine_t* engine, uint32_t address);

extern uint32_t gFrameTimeMs;
extern uint32_t gFrameTimer;
void Engine_SetFramerateTime(uint32_t fps);

extern int gAntiAliasing1;
extern int gAntiAliasing2;
extern int gAntiAliasing3;
void Engine_SetAntialiasingLevel(int level);

extern char gWindowTitle[256];
void Engine_SetWindowTitle(const char* title);

extern int gCursorShape;

uint32_t Engine_CreateRecordTable(uint32_t recordSize, uint32_t* idOut);
uint32_t Engine_DestroyRecordTable(uint32_t id);
uint32_t Engine_SetRecord(uint32_t id, const char* key, const uint8_t* value);

extern int gWindowVisible;
void Engine_SetWindowVisible(uint32_t visible);

extern int gAudioResumeOnActivate;
uint32_t Engine_SetAudioResumeOnActivate(uint32_t value);

extern int gCursorAutoHideTimeout;
extern int gCursorAutoHideActive;
extern int gCursorShown;
extern uint32_t gCursorAutoHideDeadline;
extern int gCursorLastX;
extern int gCursorLastY;
void Engine_SetCursorAutoHideTimeout(uint32_t timeout);

extern int gControlMode;
uint32_t Engine_SetControlMode(uint32_t mode);

extern int gScreenMappingMode;
uint32_t Engine_SetScreenMappingMode(uint32_t mode);
extern int gFlagUnknown10;
void Engine_SetFlagUnknown10(int value);

extern int gMasterVolume;
extern int gMasterVolumeAttenuation;
extern int gMasterVolumeMuted;
uint32_t Engine_SetMasterVolume(uint32_t volume);

typedef struct SearchPathNode SearchPathNode_t;
struct SearchPathNode
{
    char*             path;
    SearchPathNode_t* next;
};
extern SearchPathNode_t* gSearchPaths;
void Engine_AddSearchPath(char* path);

extern int gEnableSearchPaths;
void Engine_SetEnableSearchPaths(int value);

int Engine_FileExists(const char* archive, const char* filename);

extern char gUserDirectory[512];
int Engine_SetUserDirectory(const char* path);

#define USER_INSTRUCTION_COUNT 0xF0
typedef struct UserInstruction UserInstruction_t;
struct UserInstruction
{
    char*    program;
    uint8_t* code;
    size_t   codeSize;
};
extern UserInstruction_t gUserInstructions[USER_INSTRUCTION_COUNT];
int Engine_DefineUserInstruction(Engine_t* engine, uint32_t number, const char* archive, const char* program);
int Engine_UndefineUserInstruction(uint32_t number);
void Engine_FreeUserInstructions(void);

extern uint32_t gLoadWaitTimeout;
extern uint32_t gLoadWaitDeadline;
void Engine_SetLoadWaitTimeout(uint32_t timeout);

#define ENGINE_GAME_ID_SIZE 16
extern char gGameId[ENGINE_GAME_ID_SIZE];
void Engine_SetGameId(const char* id);

extern int gFlagUnknown2;
extern int gFlagUnknown3;
extern int gFlagUnknown4;
void Engine_SetFlagUnknown1to4(int value);

extern int gFlagUnknown20;
void Engine_SetFlagUnknown20(int value);

extern int gUnknownGrp0Val1;
extern int gUnknownGrp0Val2;
void Engine_SetUnknownGrp0Val1and2(int value1, int value2);

extern int gFlagUnknown21;
void Engine_SetFlagUnknown21(int value);

typedef struct ListNode ListNode_t;
struct ListNode
{
    uint32_t data1;
    uint32_t data2;
    uint32_t data3;
    ListNode_t* next;
};

extern ListNode_t* gLinkedListHead;
extern ListNode_t* gLinkedListTailNext;
extern ListNode_t  gLinkedListSentinel;
void Engine_PushGlobalList(uint32_t value1, uint32_t value2, uint32_t value3);
int Engine_PopGlobalList(uint32_t* output);

extern int gSomethingToDoWithKeylots;
extern int gKeySlots[16];
int Engine_SetKeySlots(int value, int* keys);

typedef struct ThreadNode ThreadNode_t;
struct ThreadNode
{
    uint32_t threadId;
    Thread_t* thread;
    ThreadNode_t* next;
};
extern ThreadNode_t* gThreadList;
extern int gThreadListCount;
uint32_t Engine_AddThreadToList(Thread_t* thread);
Thread_t* Engine_GetThreadFromListById(uint32_t threadId);

int Engine_PlaySound(char* path);

bool Str_IsDoubleByteSJIS(char c);
void Str_StrToLowerCase(char* ptr);

#endif