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

	uint32_t mainThreadId = Engine_LoadProgram(gEngine, "system", "ipl._bp", 0x1000, 0x20000, 0x20000);

	//Engine_ExecuteThread(gEngine, mainThreadId);
	Engine_Execute(gEngine);

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
