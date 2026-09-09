#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "thread.h"
#include "engine.h"
#include "platform_detection.h"
#include "version.h"
#include "golden_log.h"
#include "os.h"
#include "gameid.h"
#include "renderer.h"

void PrintVersion()
{
	printf("%s\n", VERSION_STRING);
	printf("Built with %s version %d.%d.%d on %s (%s), %s, %s\n\n",
		COMPILER_NAME,
		COMPILER_VERSION_MAJOR,
		COMPILER_VERSION_MINOR,
		COMPILER_VERSION_PATCH,
		PLATFORM_NAME,
		ARCH_NAME,
		__DATE__,
		__TIME__
	);
}

Engine_t* gEngine;

int main(int argc, char** argv)
{
	// --shot <file> keeps the frame the engine ends on, which is how a change to
	// the drawing can be looked at without a screen, --ticks <n> ends the run
	// after n ticks, which is how a boot that no longer stops on anything is
	// looked at at all, and --watch <addr>[:width] reports every change of one
	// engine-wide memory word with the thread and program offset that made it.
	// All three are read out of the arguments first so that the two positional
	// ones keep their places.
	const char* shot = NULL;
	int argumentCount = 0;
	char* arguments[3] = { NULL, NULL, NULL };
	for(int i = 0; i < argc; i++)
	{
		if(strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
			shot = argv[++i];
		else if(strcmp(argv[i], "--ticks") == 0 && i + 1 < argc)
			gTickLimit = atoi(argv[++i]);
		else if(strcmp(argv[i], "--watch") == 0 && i + 1 < argc)
		{
			// --watch <address>[:<width>], the address in the script's own tagged
			// form and the width in bytes (4 by default).
			const char* spec = argv[++i];
			char* end = NULL;
			uint32_t address = (uint32_t)strtoul(spec, &end, 0);
			uint32_t width = 4;
			if(end != NULL && *end == ':')
				width = (uint32_t)strtoul(end + 1, NULL, 0);
			if(!Engine_AddWatch(address, width))
				return 2;
		}
		else if(argumentCount < 3)
			arguments[argumentCount++] = argv[i];
	}
	argc = argumentCount;
	argv = arguments;

	if(argc < 2 || argv[1] == NULL || chdir(argv[1]) != 0)
	{
		fprintf(stderr, "[EngineHost]: Could not enter the supplied game folder.\n");
		return 2;
	}

	// The game's identifier is a constant of each original executable and is not
	// recorded anywhere in the game's data, so it is read back out of that
	// executable (see gameid.c). Fureraba's ipl takes a different branch without it
	// and never loads the programs it names, so an unreadable identifier is a fatal
	// error naming the files that were looked at, never a guess or an empty string.
	// The second argument overrides the scan, including with an empty string, which
	// is how the short boot without an identifier can still be run.
	if(argc > 2 && argv[2] != NULL)
	{
		Engine_SetGameId(argv[2]);
	}
	else
	{
		char gameId[ENGINE_GAME_ID_SIZE] = { 0 };
		char exeName[256] = { 0 };
		char examined[512] = { 0 };

		if(!GameId_ScanFolder(".", gameId, exeName, sizeof(exeName), examined, sizeof(examined)))
		{
			fprintf(stderr, "[EngineHost]: No BURIKO game identifier could be read from "
				"any executable in \"%s\".\n", argv[1]);
			if(examined[0])
				fprintf(stderr, "[EngineHost]: Executables examined: %s\n", examined);
			else
				fprintf(stderr, "[EngineHost]: There are no .exe files in that folder.\n");
			fprintf(stderr, "[EngineHost]: Supply the identifier as the second argument "
				"if the game's own executable is missing or packed.\n");
			return 3;
		}

		printf("[EngineHost]: Game identifier \"%s\" read from %s\n", gameId, exeName);
		Engine_SetGameId(gameId);
	}

	PrintVersion();


	GoldenLog_Load("golden_log.log");

	Engine_t engine;
	gEngine = &engine;
	Engine_Init(&engine);
	OS_Init(&engine);

	// 0x0048CCF1 makes the boot thread with a 0x1000 stack, 0x80000 of code
	// space and 0x40000 of local memory. Fureraba's boot loads twelve programs
	// into that code space one after another and needs every byte of it.
	uint32_t mainThreadId = Engine_LoadProgram(gEngine, "system", "ipl._bp", 0x1000, 0x80000, 0x40000);

	//Engine_ExecuteThread(gEngine, mainThreadId);
	Engine_Execute(gEngine);

	if(shot != NULL)
	{
		int result = Renderer_SaveScreenPng(engine.renderer, shot);
		if(result != 0)
			fprintf(stderr, "[EngineHost]: The frame could not be written to \"%s\" (%d).\n",
			        shot, result);
		else
			printf("[EngineHost]: Frame written to %s\n", shot);
	}

/*
	int runSteps = 2000;
	int steps = runSteps;
	printf("[Engine]: Running %d instructions...\n", steps);
	while(steps)
	{
		uint32_t res = Thread_Execute(thread);
		if(res == 0xFFFFFFFF)
			break;
		steps--;
	}
	printf("[Engine]: Ran %d instructions; Exiting...\n", runSteps - steps);
*/

	OS_Quit();
	Engine_Free(&engine);


	printf("\nThanks for playing!\n\n");
}
