#include <SDL2/SDL.h>
#include <unistd.h>
#include "os.h"
#include "version.h"
#include "engine.h"

Engine_t* osEngine = NULL;

// 0x005666E8, set and cleared as the window gains and loses the input focus.
int gAppActive = 0;

// 0x005667EC. Set, keys are read even while the window is not active. Nothing
// in the engine sets it yet; it is here because 0x0046D6E0 tests it.
int gReadKeysWhenInactive = 0;

// 0x0056691C, GetSystemMetrics(SM_SWAPBUTTON) at start-up: with the buttons
// swapped the engine reads the OTHER physical button for VK_LBUTTON and
// VK_RBUTTON. Only Windows swaps them, and SDL reports the buttons already
// swapped, so this stays 0 and the engine reads what SDL gives it.
int gSwapMouseButtons = 0;


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
    gAppActive = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
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
        else if(event.type == SDL_WINDOWEVENT)
        {
        	if(event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        		gAppActive = 1;
        	else if(event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        		gAppActive = 0;
        }
    }
}

/*
 * Win32 virtual keys onto SDL scancodes. Scancodes and not keycodes, because
 * a virtual key is a position on the keyboard, which is what a scancode is;
 * a keycode would move with the layout and a script's configured key would
 * land somewhere else on an AZERTY console.
 *
 * The keys with no scancode of their own - the mouse buttons, and the
 * modifiers whose left and right halves the engine asks about as one - are
 * answered above the table.
 */
static SDL_Scancode OS_ScancodeForVk(uint32_t vk)
{
	static const SDL_Scancode named[] = {
		[0x08] = SDL_SCANCODE_BACKSPACE,  [0x09] = SDL_SCANCODE_TAB,
		[0x0D] = SDL_SCANCODE_RETURN,     [0x13] = SDL_SCANCODE_PAUSE,
		[0x14] = SDL_SCANCODE_CAPSLOCK,   [0x1B] = SDL_SCANCODE_ESCAPE,
		[0x20] = SDL_SCANCODE_SPACE,      [0x21] = SDL_SCANCODE_PAGEUP,
		[0x22] = SDL_SCANCODE_PAGEDOWN,   [0x23] = SDL_SCANCODE_END,
		[0x24] = SDL_SCANCODE_HOME,       [0x25] = SDL_SCANCODE_LEFT,
		[0x26] = SDL_SCANCODE_UP,         [0x27] = SDL_SCANCODE_RIGHT,
		[0x28] = SDL_SCANCODE_DOWN,       [0x2C] = SDL_SCANCODE_PRINTSCREEN,
		[0x2D] = SDL_SCANCODE_INSERT,     [0x2E] = SDL_SCANCODE_DELETE,
		[0x5B] = SDL_SCANCODE_LGUI,       [0x5C] = SDL_SCANCODE_RGUI,
		[0x5D] = SDL_SCANCODE_APPLICATION,
		[0x6A] = SDL_SCANCODE_KP_MULTIPLY,[0x6B] = SDL_SCANCODE_KP_PLUS,
		[0x6D] = SDL_SCANCODE_KP_MINUS,   [0x6E] = SDL_SCANCODE_KP_PERIOD,
		[0x6F] = SDL_SCANCODE_KP_DIVIDE,  [0x90] = SDL_SCANCODE_NUMLOCKCLEAR,
		[0x91] = SDL_SCANCODE_SCROLLLOCK,
		[0xA0] = SDL_SCANCODE_LSHIFT,     [0xA1] = SDL_SCANCODE_RSHIFT,
		[0xA2] = SDL_SCANCODE_LCTRL,      [0xA3] = SDL_SCANCODE_RCTRL,
		[0xA4] = SDL_SCANCODE_LALT,       [0xA5] = SDL_SCANCODE_RALT,
		[0xBA] = SDL_SCANCODE_SEMICOLON,  [0xBB] = SDL_SCANCODE_EQUALS,
		[0xBC] = SDL_SCANCODE_COMMA,      [0xBD] = SDL_SCANCODE_MINUS,
		[0xBE] = SDL_SCANCODE_PERIOD,     [0xBF] = SDL_SCANCODE_SLASH,
		[0xC0] = SDL_SCANCODE_GRAVE,      [0xDB] = SDL_SCANCODE_LEFTBRACKET,
		[0xDC] = SDL_SCANCODE_BACKSLASH,  [0xDD] = SDL_SCANCODE_RIGHTBRACKET,
		[0xDE] = SDL_SCANCODE_APOSTROPHE,
	};

	if(vk >= '0' && vk <= '9')            // VK_0..VK_9 are the ASCII digits
		return (SDL_Scancode)(vk == '0' ? SDL_SCANCODE_0
		                                : SDL_SCANCODE_1 + (vk - '1'));
	if(vk >= 'A' && vk <= 'Z')            // VK_A..VK_Z are the ASCII letters
		return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
	if(vk >= 0x60 && vk <= 0x69)          // VK_NUMPAD0..9
		return (SDL_Scancode)(vk == 0x60 ? SDL_SCANCODE_KP_0
		                                 : SDL_SCANCODE_KP_1 + (vk - 0x61));
	if(vk >= 0x70 && vk <= 0x7B)          // VK_F1..VK_F12
		return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - 0x70));
	if(vk < sizeof(named) / sizeof(named[0]) && named[vk] != SDL_SCANCODE_UNKNOWN)
		return named[vk];
	return SDL_SCANCODE_UNKNOWN;
}

int OS_IsKeyDown(uint32_t vk)
{
	// 0x0046D744: a key is only reported while the window is active, unless
	// the engine has been told to read them anyway.
	if(!gAppActive && !gReadKeysWhenInactive)
		return 0;

	uint32_t buttons = SDL_GetMouseState(NULL, NULL);
	switch(vk)
	{
		case 0x01:  // VK_LBUTTON
			return (buttons & SDL_BUTTON(gSwapMouseButtons ? SDL_BUTTON_RIGHT
			                                               : SDL_BUTTON_LEFT)) != 0;
		case 0x02:  // VK_RBUTTON
			return (buttons & SDL_BUTTON(gSwapMouseButtons ? SDL_BUTTON_LEFT
			                                               : SDL_BUTTON_RIGHT)) != 0;
		case 0x04:  // VK_MBUTTON
			return (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
		case 0x05:  // VK_XBUTTON1
			return (buttons & SDL_BUTTON(SDL_BUTTON_X1)) != 0;
		case 0x06:  // VK_XBUTTON2
			return (buttons & SDL_BUTTON(SDL_BUTTON_X2)) != 0;
		default:
			break;
	}

	int count = 0;
	const Uint8* keys = SDL_GetKeyboardState(&count);
	if(keys == NULL)
		return 0;

	// VK_SHIFT, VK_CONTROL and VK_MENU ask about either half.
	SDL_Scancode left = SDL_SCANCODE_UNKNOWN, right = SDL_SCANCODE_UNKNOWN;
	if(vk == 0x10)      { left = SDL_SCANCODE_LSHIFT; right = SDL_SCANCODE_RSHIFT; }
	else if(vk == 0x11) { left = SDL_SCANCODE_LCTRL;  right = SDL_SCANCODE_RCTRL;  }
	else if(vk == 0x12) { left = SDL_SCANCODE_LALT;   right = SDL_SCANCODE_RALT;   }
	if(left != SDL_SCANCODE_UNKNOWN)
		return (left < count && keys[left]) || (right < count && keys[right]);

	SDL_Scancode code = OS_ScancodeForVk(vk);
	if(code == SDL_SCANCODE_UNKNOWN || code >= count)
		return 0;
	return keys[code] != 0;
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
