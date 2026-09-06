#include <SDL2/SDL.h>
#include <unistd.h>
#include "os.h"
#include "version.h"
#include "engine.h"

Engine_t* osEngine = NULL;

uint32_t OS_GetTicks()
{
	return SDL_GetTicks();
}

int OS_Init(Engine_t* engine)
{
	osEngine = engine;

    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        VERSION_STRING,             // Window title
        SDL_WINDOWPOS_CENTERED,     // Initial x position
        SDL_WINDOWPOS_CENTERED,     // Initial y position
        800,                        // Width in pixels
        600,                        // Height in pixels
        SDL_WINDOW_SHOWN            // Flags (make it visible)
    );

    if(window == NULL)
    {
        SDL_Quit();
        return 1;
    }
    engine->window = window;
    return 0;
}

// 0x004615D0 works the length of a frame out from the display's own refresh rate
// (0x0045E600) and puts the next frame's deadline in 0x00565FAC; 0x00461D20 refuses
// to compose a frame before that deadline. SDL is asked for the same rate; 60 Hz is
// the fallback when it has none to give, which is every headless video driver.
uint32_t OS_FrameInterval()
{
	SDL_DisplayMode mode;
	if(SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0)
		return 1000 / (uint32_t)mode.refresh_rate;
	return 1000 / 60;
}

int OS_Poll()
{
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
        if(event.type == SDL_QUIT)
        {
        	osEngine->isRunning = 0;
        }
    }
}

int OS_Quit()
{
	SDL_DestroyWindow(osEngine->window);
    SDL_Quit();
    return 0;
}

void OS_GetPhysicalMemory(uint64_t* total, uint64_t* available)
{
	long pageSize = sysconf(_SC_PAGESIZE);
	long totalPages = sysconf(_SC_PHYS_PAGES);
	long freePages = sysconf(_SC_AVPHYS_PAGES);

	*total = (pageSize > 0 && totalPages > 0) ? (uint64_t)pageSize * (uint64_t)totalPages : 0;
	*available = (pageSize > 0 && freePages > 0) ? (uint64_t)pageSize * (uint64_t)freePages : 0;
}
