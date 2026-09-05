#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>
#include "engine.h"
#include "arc.h"
#include "renderer.h"
#include "golden_log.h"
#include "font.h"
#include "os.h"

void Engine_Init(Engine_t* engine)
{
	engine->threadCounter = 2;
	engine->programCounter = 0;
	engine->threads = NULL;
	engine->programs = NULL;
	engine->memory = NULL;
	for(int i = 0; i < 48; i++)
		engine->auxMemory[i] = NULL;
	engine->globalBufferSize = 0;
	engine->globalMem = NULL;
	engine->windowObjectHandle = 0xC0000000;
	engine->filterObjectHandle = 0x90000000;
	engine->spriteObjectHandle = 0x80000000;
	engine->knobObjectHandle = 0xE0000000;
	engine->nextThreadRequest = 0;
	engine->renderer = Renderer_Init(engine);
	engine->window = NULL;
	// The original engine allocates global memory before it runs a line of script:
	// its startup path at 0x0046D340 calls the same allocator the InitGlobalMem
	// opcode uses (0x0046BD10) with level 4, i.e. 0x1000 << 4 = 64 KiB. Scripts
	// rely on that; ipl._bp reads global memory long before it calls InitGlobalMem,
	// and without this the read resolves against a NULL base.
	Engine_InitGlobalMemory(engine, 4);
	printf("[Engine]: Engine initialised\n");
}

Thread_t* Engine_CreateThread(Engine_t* engine, uint32_t stackSize, uint32_t codeSize, uint32_t memorySize)
{
	Thread_t* thread = (Thread_t*)malloc(sizeof(Thread_t));
	thread->previousThread = engine->threads;
	engine->threads = thread;

	thread->programId = 0;
	thread->threadId = engine->threadCounter++;
	thread->flags = 0;
	thread->stackPointer = 0;
	thread->instructionPointer = 0;
	thread->nextInstructionPointer = 0;
	thread->basePointer = 0;
	thread->stackSize = stackSize;
	thread->stackMemoryConfig.isAllocated = 1;
	thread->stackMemoryConfig.mem = (uint8_t*)malloc(stackSize * sizeof(uint32_t));
	thread->stackMemoryConfig.size = stackSize;
	thread->stack = (uint32_t*)thread->stackMemoryConfig.mem;
	thread->codeSize = codeSize;
	thread->codeMemoryConfig.isAllocated = 1;
	thread->codeMemoryConfig.mem = (uint8_t*)malloc(codeSize);
	thread->codeMemoryConfig.size = codeSize;
	thread->code = thread->codeMemoryConfig.mem;
	thread->programs = NULL;
	thread->programCount = 0;
	thread->codeSpaceUsed = 0;
	thread->localMemSize = memorySize;
	thread->localMemConfig.isAllocated = 1;
	thread->localMemConfig.mem = (uint8_t*)malloc(memorySize);
	thread->localMemConfig.size = memorySize;
	thread->localMem = thread->localMemConfig.mem;

	thread->level = 0;
	thread->running = 0;
	thread->ticks = 0;
	thread->error = 0;
	thread->engine = engine;
	thread->opcode = 0;
	thread->waitTicks = 0;
	thread->queuePush = 0;

	thread->silenceBasicOpcodeLog = 1;
	thread->silenceYield = 1;

	printf("[Engine]: Created thread %d (stack: 0x%.8X, code: 0x%.8X, memory: 0x%.8X)\n", thread->threadId, thread->stackSize, thread->codeSize, thread->localMemSize);

	return thread;
}

char* Engine_SearchForArchive(const char* archive)
{
	char arcName[256];
	strcpy(arcName, archive);
	for(int i = 0; arcName[i] != '\0'; i++)
		arcName[i] = tolower(arcName[i]);
	char path[256];
    DIR *dir;
    struct dirent *entry;

    dir = opendir(".");
    if(dir == NULL)
    {
        perror("[Engine]");
        return NULL;
    }

    int found = 0;
    while((entry = readdir(dir)) != NULL)
    {
        char* name = entry->d_name;
        int i;
		for(i = 0; name[i] != '\0'; i++)
			path[i] = tolower(name[i]);
		path[i] = 0;
		if(strcmp(path, arcName) == 0)
		{
			strcpy(path, name);
			found = 1;
			break;
		}
		int pi = i;
		path[i++] = '.';
		path[i++] = 'a';
		path[i++] = 'r';
		path[i++] = 'c';
		path[i++] = 0;
		if(strcmp(path, arcName) == 0)
		{
			path[pi] = 0;
			strcpy(path, name);
			found = 1;
			break;
		}
    }
    closedir(dir);
    if(found)
    {
    	char* pathname = (char*)malloc(strlen(path) + 1);
    	strcpy(pathname, path);
    	return pathname;
    }
    return NULL;
}

// The engine runs on Windows, where the file system matches names without regard to
// case; every lookup here has to do that itself. A match must also be a real file:
// the original asks GetFileAttributesA and rejects the answer when the directory bit
// is set, so a directory of the right name is not a file of that name.
static char* Engine_ResolveInDirectory(const char* directory, const char* name)
{
	DIR* dir = opendir(directory);
	if(dir == NULL)
		return NULL;

	char* found = NULL;
	struct dirent* entry;
	while((entry = readdir(dir)) != NULL)
	{
		if(strcasecmp(entry->d_name, name) != 0)
			continue;

		char path[512];
		int length = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
		if(length < 0 || length >= (int)sizeof(path))
			break;

		struct stat info;
		if(stat(path, &info) != 0 || !S_ISREG(info.st_mode))
			break;

		found = (char*)malloc(strlen(path) + 1);
		if(found != NULL)
			strcpy(found, path);
		break;
	}
	closedir(dir);
	return found;
}

char* Engine_SearchForFile(const char* archive, const char* filename)
{
	char* arcPath = Engine_SearchForArchive(archive);
	if(arcPath == NULL)
		return NULL;
	char* path = Engine_ResolveInDirectory(arcPath, filename);
	free(arcPath);
	return path;
}

uint8_t* Engine_ReadFile(Engine_t* engine, const char* archive, const char* filename, size_t* outSize)
{
	printf("[Engine]: Attempting to read file \"%s\" from archive \"%s\"\n", filename, archive);
	char* path = Engine_SearchForFile(archive, filename);
	if(!path)
	{
		// Not unpacked on disk: read it out of the archive as shipped.
		uint8_t* packed = Arc_ReadFile(archive, filename, outSize);
		if(packed == NULL)
			printf("[Engine]: Failed to find file \"%s\" from archive \"%s\"\n", filename, archive);
		return packed;
	}
	printf("[Engine]: Found file at \"%s\"\n", path);

	FILE* f = fopen(path, "rb");
	if(f == NULL)
	{
		// TODO: Error
		perror("Error");
		free(path);
		return NULL;
	}
	if(fseek(f, 0, SEEK_END) != 0)
	{
		// TODO: Error
		perror("Error");
		free(path);
		fclose(f);
		return NULL;
	}
	size_t size = ftell(f);
	if(fseek(f, 0, SEEK_SET) != 0)
	{
		// TODO: Error
		perror("Error");
		free(path);
		fclose(f);
		return NULL;
	}
	uint8_t* file = (uint8_t*)malloc(size);
	if(file == NULL)
	{
		// TODO: Error
		fclose(f);
		free(path);
		return NULL;
	}
	if(fread(file, 1, size, f) != size)
	{
		// TODO: Error
		perror("Error");
		free(file);
		free(path);
		fclose(f);
		return NULL;
	}
	fclose(f);
	printf("[Engine]: Read file \"%s\"\n", path);
	free(path);
	if(outSize != NULL)
		*outSize = size;
	return file;
}

uint32_t Engine_LoadProgram(Engine_t* engine, const char* archive, const char* filename, uint32_t stackSize, uint32_t codeSize, uint32_t memorySize)
{
	printf("[Engine]: Loading program \"%s\" from archive \"%s\"\n", filename, archive);
	Thread_t* thread = Engine_CreateThread(engine, stackSize, codeSize, memorySize);
	size_t fileSize;
	uint8_t* code = Engine_ReadFile(engine, archive, filename, &fileSize);
	if(code == NULL)
		return 1;
	Thread_LoadCode(thread, code, filename);
	free(code);

	return thread->threadId;
}

uint32_t Engine_ReadFileToMemory(Engine_t* engine, const char* archive, const char* filename, uint8_t* buffer)
{
	printf("[Engine]: Attempting to read file \"%s\" from archive \"%s\" into memory\n", filename, archive);
	
	char* path = Engine_SearchForFile(archive, filename);
	if(!path)
	{
		// Not unpacked on disk: read it out of the archive as shipped.
		size_t packedSize = 0;
		uint8_t* packed = Arc_ReadFile(archive, filename, &packedSize);
		if(packed == NULL)
		{
			printf("[Engine]: Failed to find file \"%s\" from archive \"%s\"\n", filename, archive);
			return 0;
		}
		memcpy(buffer, packed, packedSize);
		free(packed);
		return (uint32_t)packedSize;
	}
	printf("[Engine]: Found file at \"%s\"\n", path);

	FILE* f = fopen(path, "rb");
	if(f == NULL)
	{
		// TODO: Error
		free(path);
		perror("[Engine]");
		return 0;
	}
	if(fseek(f, 0, SEEK_END) != 0)
	{
		// TODO: Error
		free(path);
		perror("[Engine]");
		fclose(f);
		return 0;
	}
	size_t size = ftell(f);
	if(fseek(f, 0, SEEK_SET) != 0)
	{
		// TODO: Error
		free(path);
		perror("[Engine]");
		fclose(f);
		return 0;
	}
	if(fread(buffer, 1, size, f) != size)
	{
		// TODO: Error
		free(path);
		perror("[Engine]");
		fclose(f);
		return 0;
	}
	fclose(f);
	printf("[Engine]: Read file \"%s\" into memory\n", path);
	free(path);
	return size;
}

Thread_t* Engine_GetThreadById(Engine_t* engine, uint32_t threadId)
{
	Thread_t* thread = engine->threads;
	while(thread)
	{
		if(thread->threadId == threadId)
			return thread;
		thread = thread->previousThread;
	}
	return NULL;
}

void Engine_Sleep(int microseconds)
{
	struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

int totalTicks = 0;
// A thread that has ended stays in the list so its id still resolves, but it must
// never be scheduled again.
static int Engine_HasRunnableThread(Engine_t* engine)
{
	for(Thread_t* thread = engine->threads; thread != NULL; thread = thread->previousThread)
	{
		if(!(thread->flags & THREAD_FLAG_TERMINATED))
			return 1;
	}
	return 0;
}

void Engine_Execute(Engine_t* engine)
{
	engine->isRunning = 1;

	Thread_t* thread = engine->threads;
	Thread_t* nextThread;
	int lastSleep = 0;
	while(engine->isRunning)
	{
		OS_Poll();

		if(GoldenLogTotal)
		{
			if(lastSleep == 0)
				lastSleep = GoldenLog[GoldenLogIndex].time;
			else
			{
				int delta = GoldenLog[GoldenLogIndex].time - lastSleep;
				if(delta > 20000)
				{
					Renderer_DrawScreen(engine->renderer);
					lastSleep = GoldenLog[GoldenLogIndex].time;
					Engine_Sleep(delta);
				}
			}
		}

		if(thread->waitTicks == 0)
			Engine_ExecuteThread(engine, thread->threadId, 1);
		else
			thread->waitTicks--;
		if(engine->nextThreadRequest)
		{
			nextThread = Engine_GetThreadById(engine, engine->nextThreadRequest);
			engine->nextThreadRequest = 0;
		}
		else
			nextThread = Engine_GetThreadById(engine, thread->threadId + 1);
		if(nextThread == NULL)
			nextThread = Engine_GetThreadById(engine, 2);

		if(!Engine_HasRunnableThread(engine))
		{
			printf("[Engine]: Every thread has ended.\n");
			engine->isRunning = 0;
			break;
		}
		while(nextThread == NULL || (nextThread->flags & THREAD_FLAG_TERMINATED))
		{
			uint32_t after = (nextThread == NULL) ? 1 : nextThread->threadId;
			nextThread = Engine_GetThreadById(engine, after + 1);
			if(nextThread == NULL)
				nextThread = Engine_GetThreadById(engine, 2);
		}
		thread = nextThread;
	}
	printf("[Engine]: Engine stopped. Executed %d ticks...\n", totalTicks);
}

void Engine_ExecuteThread(Engine_t* engine, uint32_t threadId, int ticks)
{
	Thread_t* thread = Engine_GetThreadById(engine, threadId);
	int runSteps = ticks; //81944;
	int steps = runSteps;
	thread->running = 1;


	//printf("[Engine]: Running %d instructions...\n", steps);
	while(steps && thread->running)
	{
		// Delayed push from async execution
		while(thread->queuePush)
		{
			thread->queuePush--;
			Thread_PushStack(thread, thread->queuePushQueue[thread->queuePush]);
		}

		uint32_t res = Thread_Execute(thread);
		if(res == 0xFFFFFFFF)
		{
			printf("[Engine]: Stub opcode encountered. Stopping.\n");
			engine->isRunning = 0;
			break;
		}
		if(res == 0xFFFFFFFE)
		{
			printf("[Engine]: Golden log mismatch encountered. Stopping.\n");
			engine->isRunning = 0;
			break;
		}
		if(res == 0xFFFFFFFD)
		{
			printf("[Engine]: Unknown opcode encountered. Stopping.\n");
			engine->isRunning = 0;
			break;
		}
		if(res == 0xFFFFFFFC)
		{
			printf("[Engine]: Error encountered. Stopping.\n");
			engine->isRunning = 0;
			break;
		}
		// Result 4 is the outermost frame returning: the thread is over. The
		// original handles it at 0x0048D06C by setting bit 31 of the thread's flags
		// word at +0x70 and, unlike a yield, staying where it is instead of stepping
		// on; the scheduler sees the mark on its next visit and unlinks the thread.
		if(res == 4)
		{
			thread->flags |= THREAD_FLAG_TERMINATED;
			thread->running = 0;
			printf("[Engine]: Thread %d has ended.\n", thread->threadId);
			break;
		}
		if(res != 0 && res != 1 && res != 2 && res != 3)
		{
			printf("[Engine]: Non-zero result from opcode (%u / 0x%.8X). Stopping.\n", res, res);
			engine->isRunning = 0;
			break;
		}
		if(res == 2)
		{
			// Simulate async if we have golden log
			if(GoldenLogTotal)
			{
				int idx = GoldenLogIndex;
				int repeatThread = GoldenLog[GoldenLogIndex].thread;
				int ticks = 0;
				idx++;
				while(idx < GoldenLogTotal)
				{
					if(GoldenLog[idx].type != LOG_TYPE_EXEC)
					{
						idx++;
						continue;
					}
					int cThread = GoldenLog[idx].thread;
					if(cThread == repeatThread)
						ticks++;
					if(cThread == threadId)
						break;
					idx++;
				}
				thread->waitTicks = ticks - 1;
				printf("[Engine]: Sleeping thread for %d ticks.\n", ticks);
			}
		}
		if(res == 1 || res == 3)
		{
			break;
		}
		steps--;
		totalTicks++;
	}
	//printf("[Engine]: Ran %d instructions; Yielding at tick %d...\n", runSteps - steps, thread->ticks);
}

uint32_t gUnknownVal001 = 0;
void SetGlobalUnknownVal001(uint32_t value)
{
	gUnknownVal001 = value;
}

// Sys0 0x52 in the original engine (fureraba.exe 0x004891D0) pops one value and
// stores it in a global at 0x00503EF8. The only consumer is the main loop
// (0x0048CE1E): when no timed event is pending it passes this value to the frame
// wait (0x00493C70 -> 0x004528A0), which turns it into a SetWaitableTimer delay of
// n milliseconds (0.5 ms for the special case n == 1). Zero means "do not sleep on
// this path", and the loop falls back to its own one-millisecond wait.
uint32_t gIdleWaitTime = 0;
void Engine_SetIdleWaitTime(uint32_t value)
{
	gIdleWaitTime = value;
}

// Sys1 0x60 (fureraba.exe 0x0048C260 -> 0x004611C0) fills one of the engine's eight
// display-mode slots. The original keeps two parallel eight-entry arrays, widths at
// 0x00506B8C and heights at 0x00506BAC; the window-creation path at 0x004612A0 reads
// slot 0x00506B88 out of them and adds the window border extents before sizing the
// window, which is what identifies the two arrays as width and height. The return
// value is a status the script reads back: 1 for a slot out of range, 2 if either
// dimension is zero, 0 on success. Nothing is written in the two failure cases.
uint32_t gDisplayModeWidth[8] = { 0 };
uint32_t gDisplayModeHeight[8] = { 0 };
uint32_t Engine_SetDisplayModeSize(uint32_t index, uint32_t width, uint32_t height)
{
	if(index >= 8)
		return 1;
	if(width == 0 || height == 0)
		return 2;
	gDisplayModeWidth[index] = width;
	gDisplayModeHeight[index] = height;
	return 0;
}

// Sys1 0x62 (fureraba.exe 0x0048C2C0 -> 0x0045E9B0) sets a display flag at
// 0x0050721C, rejecting anything above 1 and reporting success as 1 / failure as 0.
// Two places read it back through the getter at 0x0045E9D0: the adapter chooser at
// 0x0045E670 only walks IDirect3D9::GetAdapterMonitor to find the adapter holding
// the game window when the flag is 1, and the dialog scopes at 0x00460140 and
// 0x0045F6A0 only call IDirect3DDevice9::SetDialogBoxMode when it is 0. What the
// game calls this option is not established from the binary, so it keeps the
// engine's own numbering.
uint32_t gDisplayFlagUnknown98 = 0;
uint32_t Engine_SetDisplayFlagUnknown98(uint32_t value)
{
	if(value > 1)
		return 0;
	gDisplayFlagUnknown98 = value;
	return 1;
}

// Grp0 0x06 (fureraba.exe 0x004796B0 -> 0x00461FB0 -> 0x00442F50) moves the mouse
// cursor. It writes x and y into the input object at +0x40 and +0x44 and raises the
// pending flag at +0x4C of its child object, so the move is applied by the input
// pump rather than immediately. The reader at 0x00442F70 only hands the position
// back when both coordinates are inside the client area, which is what pins +0x40
// as x and +0x44 as y. The original stores the request unclamped; the clamp is on
// the read side, so this does the same.
int gMousePosX = 0;
int gMousePosY = 0;
int gMousePosPending = 0;
void Engine_SetMousePosition(int x, int y)
{
	gMousePosX = x;
	gMousePosY = y;
	gMousePosPending = 1;
}

// Grp1 0x0D (fureraba.exe 0x004808D0 -> 0x00469100 -> 0x0042DD10) pops one value
// into the global at 0x00565B60 and pushes nothing. The single reader, at
// 0x0042E1F2, is a gate: when the global is zero the routine keeps the value it has
// just computed, and when it is non-zero it consults 0x0042DC40 and discards that
// value if the query answers 2. What the option is called is not established from
// the binary, so it keeps the engine's numbering.
uint32_t gGrp1FlagUnknown13 = 0;
void Engine_SetGrp1FlagUnknown13(uint32_t value)
{
	gGrp1FlagUnknown13 = value;
}

// Ext0 0xC7 (fureraba.exe 0x00479440 -> 0x004690E0 -> 0x0042D900) fills the engine's
// font substitution table: a singly linked list of { name, replacement } whose head
// is 0x00565B5C, plus a fallback replacement at 0x00565B4C used when a name is not
// listed. The text path at 0x0042DF60 is what identifies it: when the face name a
// script asked for does not match the one already selected, it looks the name up
// through 0x0042ECB0 and draws with whatever comes back. A NULL name sets the
// fallback instead of an entry, and a NULL replacement clears the entry's own
// replacement without removing the entry - both are the original's behaviour.
FontSubstitution_t* gFontSubstitutions = NULL;
char* gFontSubstitutionDefault = NULL;

static char* Engine_DupString(const char* str)
{
	if(str == NULL)
		return NULL;
	char* copy = (char*)malloc(strlen(str) + 1);
	if(copy != NULL)
		strcpy(copy, str);
	return copy;
}

void Engine_SetFontSubstitution(const char* name, const char* replacement)
{
	if(name == NULL)
	{
		free(gFontSubstitutionDefault);
		gFontSubstitutionDefault = Engine_DupString(replacement);
		return;
	}

	FontSubstitution_t* entry = gFontSubstitutions;
	while(entry != NULL && strcmp(entry->name, name) != 0)
		entry = entry->next;

	if(entry == NULL)
	{
		entry = (FontSubstitution_t*)malloc(sizeof(FontSubstitution_t));
		if(entry == NULL)
			return;
		entry->name = Engine_DupString(name);
		entry->replacement = NULL;
		// The original starts a fresh entry's charset at -1 (0x0042D9A0), i.e. "not
		// stated", until Ext0 0xC1 sets one.
		entry->charset = -1;
		entry->next = gFontSubstitutions;
		gFontSubstitutions = entry;
	}
	else
	{
		free(entry->replacement);
		entry->replacement = NULL;
	}

	entry->replacement = Engine_DupString(replacement);
}

const char* Engine_GetFontSubstitution(const char* name)
{
	for(FontSubstitution_t* entry = gFontSubstitutions; entry != NULL; entry = entry->next)
	{
		if(strcmp(entry->name, name) == 0)
			return entry->replacement != NULL ? entry->replacement : gFontSubstitutionDefault;
	}
	return gFontSubstitutionDefault;
}

// Ext0 0xC4 (fureraba.exe 0x004793E0 -> 0x004690D0 -> 0x0042DBC0) enumerates the
// installed font families. The original walks EnumFontFamiliesEx with an all-zero
// LOGFONT, so it sees every family, and its callback at 0x0042ED30 keeps three
// running totals: how many families it saw, where to write the next name, and how
// many bytes the names take with their terminators. Which of those it returns
// depends on the argument: with no buffer it returns the byte count, so a script can
// size a buffer, and with a buffer it fills it with NUL-terminated names and returns
// how many it wrote. Fureraba calls it both ways in that order.
// Ext0 0xC1 (fureraba.exe 0x00479340 -> 0x00468BF0) does two things with one font
// name. First it records which character set the font should be looked up under, in
// the same table Ext0 0xC7 uses: 0 from the script means SHIFTJIS_CHARSET (0x80) and
// 1 means ANSI_CHARSET (0), which is what the query at 0x0042DC80 drops into
// LOGFONT.lfCharSet before calling EnumFontFamiliesEx. Anything else leaves the
// entry alone. Then it interns the name in a second list (head 0x0056631C, counter
// 0x00566314) and returns the id, so later opcodes can name the font by number.
void Engine_SetFontCharset(const char* name, int charset)
{
	if(name == NULL)
		return;

	FontSubstitution_t* entry = gFontSubstitutions;
	while(entry != NULL && strcmp(entry->name, name) != 0)
		entry = entry->next;

	if(entry == NULL)
	{
		entry = (FontSubstitution_t*)malloc(sizeof(FontSubstitution_t));
		if(entry == NULL)
			return;
		entry->name = Engine_DupString(name);
		entry->replacement = NULL;
		entry->charset = -1;
		entry->next = gFontSubstitutions;
		gFontSubstitutions = entry;
	}

	entry->charset = charset;
}

FontName_t* gFontNames = NULL;
static uint32_t gFontNameCounter = 0;

uint32_t Engine_InternFontName(const char* name)
{
	if(name == NULL)
		return 0;

	for(FontName_t* entry = gFontNames; entry != NULL; entry = entry->next)
	{
		if(strcmp(entry->name, name) == 0)
			return entry->id;
	}

	FontName_t* entry = (FontName_t*)malloc(sizeof(FontName_t));
	if(entry == NULL)
		return 0;
	entry->id = gFontNameCounter++;
	entry->name = Engine_DupString(name);
	entry->next = gFontNames;
	gFontNames = entry;
	return entry->id;
}

// Grp1 0x0E (fureraba.exe 0x004808F0 -> 0x00461F10 -> 0x00407BC0 -> 0x0042EEB0)
// attaches a scale and an origin to a named font, which it finds by walking the list
// at +0xAC of the object held in 0x00566750. Fureraba calls it once for every family
// Ext0 0xC4 handed back, with all four values zero, which is what identifies the name
// as a font family rather than anything else. The script sees only whether it was
// accepted: the two rejections have their own messages in the binary, "invalid scale"
// and "invalid coordinates", and both are fatal.
//
// The validator at 0x0042EC10 is what says how the numbers are meant. Both scales
// are 16.16 fixed point and must be between 1.0 and 2.0, except that scaleX may also
// be 0, meaning "use scaleY"; all four values zero is accepted as a reset. The origin
// is 16.16 too, and each axis must be no larger than 1.0 - 1/scale - that is, the
// visible rectangle is 1/scale of the whole and the origin may move it only as far
// as its own far edge. The original computes that bound in double precision as
// 65536.0 - 4294967296.0 / scale and truncates it, so this does the same.
FontAdjust_t* gFontAdjusts = NULL;

static int32_t Engine_FontAdjustOriginLimit(uint32_t scale)
{
	return (int32_t)(65536.0 - 4294967296.0 / (double)scale);
}

uint32_t Engine_SetFontAdjust(const char* name, uint32_t scaleX, uint32_t scaleY, int32_t originX, int32_t originY)
{
	if(scaleX == 0 && scaleY == 0 && originX == 0 && originY == 0)
		return 0;

	if((scaleX < 0x10000 || scaleX > 0x20000) && scaleX != 0)
		return 0x80000005;
	if(scaleY < 0x10000 || scaleY > 0x20000)
		return 0x80000005;

	uint32_t reference = scaleX != 0 ? scaleX : scaleY;
	if(originX > Engine_FontAdjustOriginLimit(reference))
		return 0x80000006;
	if(originY > Engine_FontAdjustOriginLimit(scaleY))
		return 0x80000006;

	if(name == NULL)
		return 0;

	FontAdjust_t* entry = gFontAdjusts;
	while(entry != NULL && strcmp(entry->name, name) != 0)
		entry = entry->next;

	if(entry == NULL)
	{
		entry = (FontAdjust_t*)malloc(sizeof(FontAdjust_t));
		if(entry == NULL)
			return 0;
		entry->name = Engine_DupString(name);
		entry->next = gFontAdjusts;
		gFontAdjusts = entry;
	}

	entry->scaleX = scaleX;
	entry->scaleY = scaleY;
	entry->originX = originX;
	entry->originY = originY;
	return 0;
}

// Grp1 0x9A (fureraba.exe 0x00484A90 -> 0x00463490 -> 0x00434490) sets one of four
// values by number. The engine calls the selector a "function number" and the value
// a "function parameter" in its own two error messages, and rejects anything else:
// an unknown number is fatal, and so is a negative value for function 0x80000001,
// which is the only one that checks its parameter. The four destinations are
// 0x0050764C, 0x00565CF4, 0x00507654 and 0x00565D30; what they mean is not
// established from the binary, so they are held by number here.
uint32_t gFunctionParameters[4] = { 0 };

uint32_t Engine_SetFunctionParameter(uint32_t function, int32_t value)
{
	int slot;
	switch(function)
	{
		case 0x00000000: slot = 0; break;
		case 0x80000000: slot = 1; break;
		case 0x80000001:
			if(value < 0)
				return 0x80000008;
			slot = 2;
			break;
		case 0x80000002: slot = 3; break;
		default:
			return 0x80000007;
	}

	gFunctionParameters[slot] = (uint32_t)value;
	return 0;
}

uint32_t Engine_EnumerateFontFamilies(char* buffer)
{
	Font_Init();

	uint32_t count = Font_GetFamilyCount();
	uint32_t bytes = 0;

	for(uint32_t i = 0; i < count; i++)
	{
		const char* name = Font_GetFamilyName(i);
		size_t length = strlen(name) + 1;
		if(buffer != NULL)
		{
			memcpy(buffer, name, length);
			buffer += length;
		}
		bytes += (uint32_t)length;
	}

	return buffer != NULL ? count : bytes;
}

int Engine_InitGlobalMemory(Engine_t* engine, uint32_t level)
{
	if(level < 0 || level >= 13)
	{
		printf("[Engine]: Error: Attempted to initialise global memory with an invalid level of %d\n", level);
		return 0;
	}

	uint32_t bufferSize = 0x1000 << level; // 4096 * (1 << level)

	engine->globalBufferSize = bufferSize;

	if(engine->globalMem != NULL)
	{
		printf("[Engine]: Freeing previous global memory\n");
		free(engine->globalMem);
	}

	engine->globalMem = (uint8_t*)calloc(bufferSize, sizeof(uint8_t));

	printf("[Engine]: Initialised global memory with size 0x%.8X\n", bufferSize);

	return 1;
}

uint32_t Engine_AllocAuxMemory(Engine_t* engine, uint32_t size)
{
	if(size > 0x2000000)
	{
		printf("[Engine]: Attempting to allocate too much aux memory\n");
		engine->isRunning = 0;
		return 0;
	}
	for(int i = 0; i < 48; i++)
	{
		if(engine->auxMemory[i] != NULL)
			continue;
		engine->auxMemory[i] = (uint8_t*)malloc(size);
		printf("[Engine]: Initialised aux memory in slot %d with size 0x%.8X\n", i, size);
		return (i + 32) * 0x2000000;
	}
	return 0;
}

uint8_t* Engine_GetAuxMemory(Engine_t* engine, uint8_t slot)
{
	if(slot >= 48)
		return NULL;
	return NULL;
}

// Sys0 0x21 (fureraba.exe 0x00488550 -> 0x0048DEA0) releases the aux memory area an
// address belongs to. The original finds the area by decoding the address the same
// way its resolver does, frees the block and clears the slot, and answers 1. A null
// address is accepted and answers 1 without freeing anything. An address that is not
// in any allocated area is fatal - "an invalid address was given as the target of a
// global memory free" - so this reports it and stops the thread rather than ignoring
// it.
uint32_t Engine_FreeAuxMemory(Engine_t* engine, uint32_t address)
{
	if(address == 0)
		return 1;

	int tag = address >> 24;
	if(tag < 0x40)
		return 0;

	int slot = (tag >> 1) - 32;
	if(slot < 0 || slot >= 48 || engine->auxMemory[slot] == NULL)
		return 0;

	free(engine->auxMemory[slot]);
	engine->auxMemory[slot] = NULL;
	printf("[Engine]: Freed aux memory in slot %d\n", slot);
	return 1;
}

uint32_t gFrameTimeMs = 0;
uint32_t gFrameTimer = 0;
void Engine_SetFramerateTime(uint32_t fps)
{
	if(fps == 0)
		gFrameTimeMs = 1;
	else
	{
		gFrameTimeMs = 1000U / fps;

		if(gFrameTimeMs == 0)
			gFrameTimeMs = 1;
	}

	printf("[Engine]: Set framerate to %d (%d ms)\n", fps, gFrameTimeMs);

	gFrameTimer = 0;
}

int gAntiAliasing1 = 0;
int gAntiAliasing2 = 0;
int gAntiAliasing3 = 0;
void Engine_SetAntialiasingLevel(int level)
{
	if(level > 4)
		return;

	switch(level)
	{
		case 0:
			gAntiAliasing1 = 1;
			gAntiAliasing2 = 2;
			gAntiAliasing3 = 2;
			return;
		case 1:
			gAntiAliasing1 = 2;
			gAntiAliasing2 = 4;
			gAntiAliasing3 = 4;
			return;
		case 2:
			gAntiAliasing2 = 6;
			gAntiAliasing3 = 8;
			gAntiAliasing1 = 3;
			return;
		case 3:
			gAntiAliasing2 = 8;
			gAntiAliasing3 = 0x10;
			gAntiAliasing1 = 4;
			return;
		default:
			gAntiAliasing2 = 0;
			gAntiAliasing3 = 1;
			gAntiAliasing1 = 0;
	}

	printf("[Engine]: Set antialiasing level to %d\n", level);

	return;
}

// Sys0 0x66 (fureraba.exe 0x00489580) is the window title, not the cursor shape:
// it resolves one script address and hands the string to SetWindowTextA on the game
// window, then, if that worked, caches it in a 256-byte buffer at 0x00506A88. A
// title of 256 characters or more is not cached, though the window still gets it.
//
// Sys0 0x67 (0x004895B0) is the cursor shape. It accepts 0 to 4, which the window
// procedure at 0x00498FBC uses to index the cursor handles at 0x005666C4 before
// calling SetCursor; anything higher is fatal, with its own message about an invalid
// mouse cursor shape number.
char gWindowTitle[256] = { 0 };
void Engine_SetWindowTitle(const char* title)
{
	if(title == NULL)
		return;
	if(strlen(title) < sizeof(gWindowTitle))
		strcpy(gWindowTitle, title);
	printf("[Engine]: Window title is now \"%s\"\n", title);
}

int gCursorShape = 0;

// Whether the game window is shown. Sys0 0x64 (0x00489540) pops a value, hands it to
// 0x0049A300 and then flushes the input state table at 0x0046DBA0. It pushes nothing.
//
// 0x0049A300 does nothing at all until graphics are up (0x005666F0). Otherwise a zero
// hides the window with ShowWindow(SW_HIDE) and a non-zero shows it with
// ShowWindow(SW_SHOWNORMAL), preceded by a SetWindowPos to the current display size
// when the pending-reposition flag at 0x00566A6C is set, and followed by a clear of
// that flag. Either way it records the new state at 0x00566A70 (read back by
// 0x0049A3B0) and recomputes "this window is the active one" at 0x005666E8 from
// GetActiveWindow.
//
// The flush at 0x0046DBA0 walks 256 entries of 0x18 bytes from 0x00518C98 and zeroes
// four dwords of each, so no key or button is left looking held across the change.
// OpenBGI keeps no such table yet; when it does, this is where it is cleared.
int gWindowVisible = 1;

void Engine_SetWindowVisible(uint32_t visible)
{
	gWindowVisible = visible != 0;
	printf("[Engine]: Set WindowVisible to %d\n", gWindowVisible);
}

// Whether sound resumes when the window is activated again. Sys1 0x68 (0x0048C370)
// pops a value, stores it at 0x00566A4C and pushes back what was there before, so it
// is a swap rather than a plain set and a script can save and restore it.
//
// The flag is read in two places. The window activation handler at 0x00499020 stops
// every sound object in the list at 0x005076C8 whenever the window is deactivated,
// but restarts them on activation only through 0x00461E10, which does nothing at all
// unless this flag is set. The idle bookkeeping at 0x00498900 also consults it before
// stamping a resume time. Nothing gates the per-frame sound update at 0x00461E00,
// which the frame loop calls at 0x0048CDB9 either way.
//
// OpenBGI has neither the sound object list nor the activation handler yet, so the
// flag is only recorded here; there is nothing for it to gate.
int gAudioResumeOnActivate = 0;

uint32_t Engine_SetAudioResumeOnActivate(uint32_t value)
{
	uint32_t previous = (uint32_t)gAudioResumeOnActivate;
	gAudioResumeOnActivate = (int)value;
	printf("[Engine]: Set AudioResumeOnActivate to %d (was %d)\n",
		(int)value, (int)previous);
	return previous;
}

// Cursor auto-hide. Ext0 0x05 (0x00478340) pops a timeout in milliseconds and hands
// it to the setter at 0x0048E9E0. A non-zero timeout arms the mechanism: it records
// the timeout, sets the deadline to now + timeout (the clock at 0x004988B0, which is
// timeGetTime or GetTickCount depending on 0x00566A48), marks the cursor as shown,
// and clears the remembered cursor position to 0x80000000 in both axes so the first
// sample counts as a move. Re-arming while already active only refreshes the timeout
// and the deadline. A zero timeout disables it and, if the cursor is currently
// hidden, shows it again; if the cursor was already shown, nothing else happens.
//
// The watcher itself is the per-frame routine at 0x0048EC40. While the window is
// active it samples the cursor in client coordinates and, if the position changed or
// left the client area, or any mouse button is down, it shows the cursor and pushes
// the deadline out again; otherwise, once the deadline passes, it hides the cursor.
// While the window is not active the cursor is always shown. That watcher is not
// wired up here: OpenBGI has no per-frame engine tick and no cursor visibility to
// drive yet, so this records the state the original records and nothing pretends to
// hide anything.
#define ENGINE_CURSOR_POSITION_UNKNOWN ((int)0x80000000)

int gCursorAutoHideTimeout = 0;
int gCursorAutoHideActive = 0;
int gCursorShown = 0;
uint32_t gCursorAutoHideDeadline = 0;
int gCursorLastX = ENGINE_CURSOR_POSITION_UNKNOWN;
int gCursorLastY = ENGINE_CURSOR_POSITION_UNKNOWN;

void Engine_SetCursorAutoHideTimeout(uint32_t timeout)
{
	if(!gCursorAutoHideActive)
	{
		if(timeout == 0)
			return;
		gCursorAutoHideActive = 1;
		gCursorShown = 1;
		gCursorAutoHideTimeout = (int)timeout;
		gCursorAutoHideDeadline = OS_GetTicks() + timeout;
		gCursorLastX = ENGINE_CURSOR_POSITION_UNKNOWN;
		gCursorLastY = ENGINE_CURSOR_POSITION_UNKNOWN;
		printf("[Engine]: Cursor auto-hide armed at %d ms\n", (int)timeout);
		return;
	}

	if(timeout != 0)
	{
		gCursorAutoHideTimeout = (int)timeout;
		gCursorAutoHideDeadline = OS_GetTicks() + timeout;
		printf("[Engine]: Cursor auto-hide re-armed at %d ms\n", (int)timeout);
		return;
	}

	gCursorAutoHideActive = 0;
	if(!gCursorShown)
		gCursorShown = 1;
	printf("[Engine]: Cursor auto-hide disabled\n");
}

// Ext1 0x1F (fureraba.exe 0x004760A0 -> 0x00491270) selects the engine's "control
// mode", which is the name its own error message uses. The setter takes 0, 1 or 2
// and nothing else; a larger number is fatal and names itself in the message.
//
// The mode is read by the routine at 0x00490F90, which Ext1 0x18, 0x24 and 0x2C all
// call, and it decides how that routine derives its second 16.16 rate from a total
// (t) and two counts (a, b). The first rate is always (t << 16) / a; the second is
//   mode 0: (t << 16) / b                      - the two counts are rated apart
//   mode 1: (t << 16) / (a + b) - (t << 16) / a - the shortfall against the pair
//   mode 2: ((t << 16) / a) * b / a            - the first rate scaled by b / a
// A zero divisor anywhere makes the whole result zero rather than faulting, and
// mode 1's second rate is a signed difference, so it is normally negative.
// An unset mode is 0, which is what the engine starts in.
int gControlMode = 0;

uint32_t Engine_SetControlMode(uint32_t mode)
{
	if(mode > 2)
		return 0;
	gControlMode = (int)mode;
	printf("[Engine]: Set ControlMode to %d\n", (int)mode);
	return 1;
}

// Screen mapping mode: how a point in the game's base coordinate space is placed in
// the actual window. Set by Sys1 0x63 (0x0048C2F0 -> the setter at 0x0045E9E0), which
// accepts only 0, 1 and 2, stores the value at 0x00565E98 and pushes 1, or changes
// nothing and pushes 0. Unlike the control mode, a rejected value is not fatal here;
// the script is told and carries on.
//
// Sys0 0x63 (0x00489520) is the older boolean form of the same setting: it pops one
// value and calls the same setter with (value != 0) ? 0 : 1, discarding the result.
// So a true value means mode 0 and a false value means mode 1.
//
// The mode is read through the resolver at 0x0045EA00, which returns the stored mode
// as it stands for 0 and 1 but treats 2 as "decide now": it compares the current
// display size (0x00461220 / 0x00461240) against the game's base size halved
// (0x0045E700 called with 1) and answers 2 only when the halved size is at least as
// large as the display in both directions, otherwise 0. The transform at 0x0045EA50
// then scales for modes 0 and 1 and merely centres for mode 2, which is what makes
// this a mapping mode rather than a scale factor.
//
// That resolver is Sys1 0x61 and is deliberately not implemented yet: OpenBGI tracks
// neither the current display mode index nor the game's base size, so mode 2 cannot
// be resolved faithfully, and inventing an answer would put the game's coordinates
// somewhere the original would not.
int gScreenMappingMode = 0;

uint32_t Engine_SetScreenMappingMode(uint32_t mode)
{
	if(mode > 2)
		return 0;
	gScreenMappingMode = (int)mode;
	printf("[Engine]: Set ScreenMappingMode to %d\n", (int)mode);
	return 1;
}

// Master volume. Grp0 0xF3 (0x00480420) pops one value and hands it to the setter at
// 0x0048F880, which takes 0 to 128 and rejects anything larger. The setter turns the
// step into DirectSound attenuation in hundredths of a decibel:
//   volume 0        -> -10000, DirectSound's silence, not the value the curve gives
//   volume 1 to 128 -> -round((128 - volume) * 100 / 2.6666666666)
// (the divisor is the double at 0x004EC8E0), so 128 is 0 and the scale is 37.5
// hundredths of a decibel per step. The result is stored at 0x00566928 and pushed
// into the sound device through its vtable slot +0x1C, except while the mute flag at
// 0x0056692C is set, when the value is still recorded but the device is left alone.
// The opcode itself discards the setter's success flag and pushes nothing.
int gMasterVolume = 128;
int gMasterVolumeAttenuation = 0;
int gMasterVolumeMuted = 0;

uint32_t Engine_SetMasterVolume(uint32_t volume)
{
	if(volume > 128)
		return 0;
	gMasterVolume = (int)volume;
	gMasterVolumeAttenuation = volume == 0
		? -10000
		: -(int)((128 - volume) * 100 / 2.6666666666 + 0.5);
	printf("[Engine]: Set MasterVolume to %d (%d hundredths of a dB)\n",
		gMasterVolume, gMasterVolumeAttenuation);
	return 1;
}

int gFlagUnknown10 = 0;
void Engine_SetFlagUnknown10(int value)
{
	gFlagUnknown10 = value;
	printf("[Engine]: Set FlagUnknown10 to %d\n", value);
}

int gEnableSearchPaths = 0;
void Engine_SetEnableSearchPaths(int value)
{
	gEnableSearchPaths = value;
	printf("[Engine]: Set EnableSearchPaths to %d\n", value);
}

int gFlagUnknown2 = 0;
int gFlagUnknown3 = 0;
int gFlagUnknown4 = 0;
void Engine_SetFlagUnknown1to4(int value)
{
	gFlagUnknown2 = value;
	gFlagUnknown3 = value;
	gFlagUnknown4 = value;
	printf("[Engine]: Set FlagUnknown2 to %d\n", value);
	printf("[Engine]: Set FlagUnknown3 to %d\n", value);
	printf("[Engine]: Set FlagUnknown4 to %d\n", value);
}

// Sys0 0xE8 (fureraba.exe 0x0048ACE0) writes the game's own identifier into the
// address the script hands it. In the original it is a compile-time constant of the
// executable, four dwords at 0x004EBDAC that spell "FriendToLoverHD" with its
// terminator, copied out as exactly 16 bytes; the same constant is handed to the
// startup call at 0x00472610. Fureraba's ipl script carries two copies of the same
// literal in its string pool, next to "UserData" and "%s%s", which is how the save
// files come to be named UserData\FriendToLoverHD000.sud.
//
// There is no general place to read it from: it is not in the game's data and not in
// the executable's version resource, so OpenBGI has to be told it per game. Until it
// is, this is empty, which is at least honest about not knowing.
char gGameId[ENGINE_GAME_ID_SIZE] = { 0 };

void Engine_SetGameId(const char* id)
{
	memset(gGameId, 0, sizeof(gGameId));
	if(id == NULL)
		return;
	// The original copies a fixed 16 bytes, so an identifier that fills the field
	// leaves no terminator; do the same rather than truncating to 15.
	size_t length = strlen(id);
	if(length > sizeof(gGameId))
		length = sizeof(gGameId);
	memcpy(gGameId, id, length);
}

// Base opcode 0xFF (fureraba.exe 0x00498D20) is the group the script fills in for
// itself: user-defined instructions, which the engine calls "mediation programs"
// (0x004EC6E8). Numbers 0x00 to 0xEF are the script's own, held in the table at
// 0x00560FC0 - all 0xFFFEFEFE in the image, so it is built at run time.
//
// Defining one (0x00498B20) clears whatever the number held, loads the named program
// into a 0x20000 scratch buffer through 0x00465C30, and keeps it only if something
// loaded: an 8-byte entry with the program's name strdup'd at +0 and a right-sized
// copy of the loaded bytes at +4, after which the scratch buffer is freed. A load of
// zero length is not recorded and the define reports failure.
//
// Undefining one (0x00498AB0) frees both and nulls the slot, and reports whether
// there was anything there.
UserInstruction_t gUserInstructions[USER_INSTRUCTION_COUNT] = { 0 };

int Engine_UndefineUserInstruction(uint32_t number)
{
	if(number >= USER_INSTRUCTION_COUNT)
		return 0;

	UserInstruction_t* instruction = &gUserInstructions[number];
	if(instruction->program == NULL && instruction->code == NULL)
		return 0;

	free(instruction->program);
	free(instruction->code);
	instruction->program = NULL;
	instruction->code = NULL;
	instruction->codeSize = 0;
	return 1;
}

int Engine_DefineUserInstruction(Engine_t* engine, uint32_t number, const char* archive, const char* program)
{
	if(number >= USER_INSTRUCTION_COUNT)
		return 0;

	Engine_UndefineUserInstruction(number);

	size_t size = 0;
	uint8_t* code = Engine_ReadFile(engine, archive, program, &size);
	if(code == NULL || size == 0)
	{
		// The original keeps nothing when nothing loaded, and says so.
		free(code);
		printf("[Engine]: User instruction 0x%.2X: could not load \"%s\" from \"%s\"\n", number, program, archive);
		return 0;
	}

	UserInstruction_t* instruction = &gUserInstructions[number];
	instruction->program = (char*)malloc(strlen(program) + 1);
	if(instruction->program != NULL)
		strcpy(instruction->program, program);
	instruction->code = code;
	instruction->codeSize = size;
	printf("[Engine]: User instruction 0x%.2X is \"%s\" from \"%s\" (%zu bytes)\n", number, program, archive, size);
	return 1;
}

void Engine_FreeUserInstructions(void)
{
	for(uint32_t i = 0; i < USER_INSTRUCTION_COUNT; i++)
		Engine_UndefineUserInstruction(i);
}

// Sys0 0x39 (fureraba.exe 0x00488B80 -> 0x0046B510) points the engine at a directory
// and answers whether it took. The original asks GetFileAttributesA for the path and
// refuses it unless it exists and has the directory bit set - so, unlike Sys0 0x34,
// this one wants a directory and nothing else. It then stores the path in the buffer
// at 0x00518978, appending a separator with "%s\" unless the path already ends in
// one, and pushes 1; a path that is not a directory is stored nowhere and pushes 0.
char gUserDirectory[512] = { 0 };

int Engine_SetUserDirectory(const char* path)
{
	if(path == NULL || path[0] == 0)
		return 0;

	char resolved[512];
	int length = snprintf(resolved, sizeof(resolved), "%s", path);
	if(length < 0 || length >= (int)sizeof(resolved))
		return 0;
	for(char* c = resolved; *c != 0; c++)
	{
		if(*c == '\\')
			*c = '/';
	}

	struct stat info;
	if(stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode))
	{
		printf("[Engine]: \"%s\" is not a directory\n", path);
		return 0;
	}

	// The original keeps the trailing separator so the path can be pasted straight
	// in front of a file name. Separators are '/' here, as everywhere else.
	if(length > 0 && resolved[length - 1] != '/')
	{
		if(length + 1 >= (int)sizeof(resolved))
			return 0;
		resolved[length] = '/';
		resolved[length + 1] = 0;
	}

	strcpy(gUserDirectory, resolved);
	printf("[Engine]: User directory is now \"%s\"\n", gUserDirectory);
	return 1;
}

SearchPathNode_t* gSearchPaths = NULL;
void Engine_AddSearchPath(char* path)
{
	if(path == NULL)
		return;

	SearchPathNode_t* node = (SearchPathNode_t*)malloc(sizeof(SearchPathNode_t));

	size_t len = strlen(path) + 1;
	char* copy = (char*)malloc(len);

	node->path = copy;
	node->next = gSearchPaths;

	memcpy(copy, path, len);

	gSearchPaths = node;

	printf("[Engine]: Added search path \"%s\"\n", copy);
}

// Sys0 0x34 (fureraba.exe 0x00488A40 -> 0x00466740) answers whether a file exists,
// and it answers from the disk and the archives rather than from any list of names.
// Its shape there is:
//
//   with no archive named, an absolute name (one starting "\" or with ":" second)
//   is tested where it stands; otherwise the bare name is looked for under the
//   install directory, and then under the second data root when the game has one;
//
//   with an archive named, the bare name is still tried under the install directory
//   first - that is how an unpacked file wins over the packed one - and only then is
//   the name looked up inside the archive.
//
// Either way the loose search also walks the search paths added by Sys0 0x37, but
// only while Sys0 0x36 has them enabled: the list head is masked with that flag
// before the walk at 0x00466620, so a disabled list is skipped rather than emptied.
//
// OpenBGI has one root, the game folder it was pointed at, and no second data root.
int Engine_FileExists(const char* archive, const char* filename)
{
	if(filename == NULL || filename[0] == 0)
		return 0;

	char* path = Engine_ResolveInDirectory(".", filename);
	if(path != NULL)
	{
		free(path);
		return 1;
	}

	if(gEnableSearchPaths)
	{
		for(SearchPathNode_t* node = gSearchPaths; node != NULL; node = node->next)
		{
			char directory[512];
			int length = snprintf(directory, sizeof(directory), "./%s", node->path);
			if(length < 0 || length >= (int)sizeof(directory))
				continue;
			for(char* c = directory; *c != 0; c++)
			{
				if(*c == '\\')
					*c = '/';
			}

			path = Engine_ResolveInDirectory(directory, filename);
			if(path != NULL)
			{
				free(path);
				return 1;
			}
		}
	}

	if(archive == NULL || archive[0] == 0)
		return 0;

	path = Engine_SearchForFile(archive, filename);
	if(path != NULL)
	{
		free(path);
		return 1;
	}

	return Arc_FileExists(archive, filename);
}

int gFlagUnknown20 = 0;
void Engine_SetFlagUnknown20(int value)
{
	gFlagUnknown20 = value;
}

int gUnknownGrp0Val1 = 0;
int gUnknownGrp0Val2 = 0;
void Engine_SetUnknownGrp0Val1and2(int value1, int value2)
{
	gUnknownGrp0Val1 = value1;
	gUnknownGrp0Val2 = value2;
}

int gFlagUnknown21 = 0;
void Engine_SetFlagUnknown21(int value)
{
	gFlagUnknown21 = value;
}

ListNode_t* gLinkedListHead;
ListNode_t* gLinkedListTailNext;
ListNode_t  gLinkedListSentinel = {
	.data1 = 0,
    .data2 = 0,
    .data3 = 0,
    .next = NULL
};
void Engine_PushGlobalList(uint32_t value1, uint32_t value2, uint32_t value3)
{
	//ListNode_t* node = (ListNode_t*)malloc(sizeof(ListNode_t));
	//node->data1 = value1;
	//node->data2 = value2;
	//node->data3 = value3;
	//node->next = NULL;
	//if(gLinkedListTailNext)
	//	gLinkedListTailNext->next = node;
	//gLinkedListTailNext = node;

}
int Engine_PopGlobalList(uint32_t* output)
{
    ListNode_t* node = gLinkedListHead;
    if(node == NULL)
        return 0;

    if(node->next == NULL)
        gLinkedListTailNext = (ListNode_t*)&gLinkedListSentinel;

    output[0] = node->data1;
    output[1] = node->data2;
    output[2] = node->data3;

    gLinkedListHead = node->next;
    free(node);

    return 1;
}

int gSomethingToDoWithKeylots = 0;
int gKeySlots[16] = { 0 };
int Engine_SetKeySlots(int value, int* keys)
{
    gSomethingToDoWithKeylots = value;

    if (value != 0) {
        if (keys == NULL)
        {
            gKeySlots[0] = 0;
        }
        else
        {
            // Count elements until the terminating 0
            int numSlots = 0;
            while(keys[numSlots] != 0)
                numSlots++;

            if(numSlots > 15)
                return 0;

            // Copy slots + terminating sentinel
            for(int i = 0; i <= numSlots; i++)
                gKeySlots[i] = keys[i];
            return 1;
        }
    }
    return 1;
}

ThreadNode_t* gThreadList = NULL;
int gThreadListCount = 0;
uint32_t Engine_AddThreadToList(Thread_t* thread)
{
	//ThreadNode_t* node = operator_new(0xc);
	//uint32_t threadId = Thread_GetThreadID(thread);
	//node->threadId = threadId;
	//node->thread = thread;
	//node->next = gThreadList;
	//gThreadList = node;
	//gThreadListCount++;
	//return node->threadId;
	return 0;
}
Thread_t* Engine_GetThreadFromListById(uint32_t threadId)
{
	ThreadNode_t* node = gThreadList;
	while(node)
	{
		if(node->threadId == threadId)
			return node->thread;
		node = node->next;
	}
	return NULL;
}

int Engine_PlaySound(char* path)
{
	printf("Play sound: %s\n", path);
	//return PlaySoundA(path, gHinstance, SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | SND_FILENAME)
	return 1;
}

bool Str_IsDoubleByteSJIS(char c)
{
	if((c < 0x80 || c > 0x9F) && c < 0xE0)
		return false;
	return true;
}

void Str_StrToLowerCase(char* ptr)
{
	char c = *ptr;
	while(c)
	{
		if(Str_IsDoubleByteSJIS(c))
			ptr++;
		else
		{
			if(c >= 'A' && c <= 'Z')
				*ptr |= 0x20;
		}
		ptr++;
		c = *ptr;
	}
}

void Engine_Free(Engine_t* engine)
{
	printf("[Engine]: Freeing memory\n");
	Thread_t* thread = engine->threads;
	while(thread)
	{
		Thread_t* nextThread = thread->previousThread;
		if(thread->stackMemoryConfig.isAllocated)
			free(thread->stackMemoryConfig.mem);
		if(thread->codeMemoryConfig.isAllocated)
			free(thread->codeMemoryConfig.mem);
		if(thread->localMemConfig.isAllocated)
			free(thread->localMemConfig.mem);
		Program_t* program = thread->programs;
		while(program)
		{
			Program_t* nextProgram = program->previousProgram;
			free(program->filename);
			free(program);
			program = nextProgram;
		}
		free(thread);
		thread = nextThread;
	}
	engine->threads = NULL;

	if(engine->globalMem != NULL)
	{
		free(engine->globalMem);
		engine->globalMem = NULL;
	}

	for(int i = 0; i < 48; i++)
	{
		if(engine->auxMemory[i])
		{
			free(engine->auxMemory[i]);
			engine->auxMemory[i] = NULL;
		}
	}

	Renderer_Free(engine->renderer);

	Engine_FreeUserInstructions();
	while(gSearchPaths)
	{
		SearchPathNode_t* next = gSearchPaths->next;
		free(gSearchPaths->path);
		free(gSearchPaths);
		gSearchPaths = next;
	}
}
