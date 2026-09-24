#include <SDL2/SDL.h>
#include <unistd.h>
#include "os.h"
#include "version.h"
#include "engine.h"
#include "input.h"

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

// Which virtual keys are held, as the host's events say. The original asks the
// system (GetAsyncKeyState); here every source of input - keyboard, mouse, touch
// and the controller - reports into this one table, which is what OS_IsKeyDown
// answers from, so a key a controller holds is as held as a key on a keyboard.
static uint8_t gVkHeld[256];

// The window shows the composed frame through a renderer whose logical size is
// the game's screen: the frame is scaled to fit with its aspect kept, and SDL
// maps pointer positions back into the game's own pixels.
static SDL_Renderer* gRenderer = NULL;
static SDL_Texture* gFrameTexture = NULL;
static int gFrameWidth = 0, gFrameHeight = 0;

// Scancode to virtual key, built once from OS_ScancodeForVk below.
static uint8_t gVkOfScancode[SDL_NUM_SCANCODES];
static SDL_Scancode OS_ScancodeForVk(uint32_t vk);
static void OS_BuildKeyTable(void)
{
	for(uint32_t vk = 1; vk < 256; vk++)
	{
		SDL_Scancode sc = OS_ScancodeForVk(vk);
		if(sc != SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES && gVkOfScancode[sc] == 0)
			gVkOfScancode[sc] = (uint8_t)vk;
	}
	// A keyboard message names the modifier, not its half (VK_SHIFT, VK_CONTROL,
	// VK_MENU); the halves are what OS_IsKeyDown's table below also answers for.
	gVkOfScancode[SDL_SCANCODE_LSHIFT] = gVkOfScancode[SDL_SCANCODE_RSHIFT] = 0x10;
	gVkOfScancode[SDL_SCANCODE_LCTRL]  = gVkOfScancode[SDL_SCANCODE_RCTRL]  = 0x11;
	gVkOfScancode[SDL_SCANCODE_LALT]   = gVkOfScancode[SDL_SCANCODE_RALT]   = 0x12;
	// The console's Back key is the window's Escape.
	gVkOfScancode[SDL_SCANCODE_AC_BACK] = 0x1B;
}

static uint32_t OS_HalfOf(SDL_Scancode sc)
{
	switch(sc)
	{
		case SDL_SCANCODE_LSHIFT: return 0xA0;
		case SDL_SCANCODE_RSHIFT: return 0xA1;
		case SDL_SCANCODE_LCTRL:  return 0xA2;
		case SDL_SCANCODE_RCTRL:  return 0xA3;
		case SDL_SCANCODE_LALT:   return 0xA4;
		case SDL_SCANCODE_RALT:   return 0xA5;
		default: return 0;
	}
}

void OS_SetVkHeld(uint32_t vk, int held)
{
	gVkHeld[vk & 0xFF] = held ? 1 : 0;
}


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
#ifdef __ANDROID__
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
#else
        SDL_WINDOW_SHOWN            // Flags (make it visible)
#endif
    );

    if(window == NULL)
    {
        SDL_Quit();
        return 1;
    }
    engine->window = window;
    gAppActive = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    // The pads, as 0x00460860 enumerates the joysticks at start-up; devices that come
    // later arrive as SDL_CONTROLLERDEVICEADDED. Without the subsystem there are none.
    if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        printf("[OS]: No game controller support: %s\n", SDL_GetError());
    OS_BuildKeyTable();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    gRenderer = SDL_CreateRenderer(window, -1, 0);
    if(gRenderer == NULL)
        gRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
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

/*
 * Game controllers, as the original's DirectInput poll (0x00460C00) reports joysticks.
 * It reads each device's buffered data and posts system messages that the scripts
 * take with Sys0 0xA0, the third word being the device's own number:
 *   0x100  button:  (pressed ? 0x80 : 0) | index           (0x00460DD7)
 *   0x101  axes:    pair << 24 | (y & 0xFFF) << 12 | (x & 0xFFF), pair 0 the X/Y
 *                   axes, pair 1 Z/RZ, each on -0x400..0x400 (DIPROP_RANGE set
 *                   at 0x004609A0), sent once per poll in which one of the pair
 *                   moved (0x00460BD0)
 *   0x102  POV:     index << 16 | the angle in eighths (hundredths of a degree /
 *                   4500), 0xFFFF centred (0x00460CDD)
 * Each button, the POV's four directions and full deflection of the X/Y axes can
 * also pulse a virtual key from the table at 0x00565F08; only Sys1 0x1B fills it, the
 * game never calls that, and the table starts empty, so no key is pulsed here either.
 *
 * DirectInput's numbering is the device's own. The layout used for an SDL game
 * controller is the one Windows reports for the common XInput pad: buttons A, B, X,
 * Y, LB, RB, Back, Start, left stick, right stick as 0 to 9; the D-pad as POV 0;
 * the left stick as X/Y and the right stick as Z/RZ.
 */
#define OS_MAX_PADS 4
static SDL_GameController* gPads[OS_MAX_PADS];
static SDL_JoystickID gPadIds[OS_MAX_PADS];
static int32_t gPadAxis[OS_MAX_PADS][4];     // X, Y, Z, RZ on -0x400..0x400
static uint32_t gPadPov[OS_MAX_PADS];

static int OS_PadSlot(SDL_JoystickID id)
{
	for(int i = 0; i < OS_MAX_PADS; i++)
		if(gPads[i] != NULL && gPadIds[i] == id)
			return i;
	return -1;
}

static void OS_PadAdded(int deviceIndex)
{
	if(!SDL_IsGameController(deviceIndex))
		return;
	for(int i = 0; i < OS_MAX_PADS; i++)
	{
		if(gPads[i] == NULL)
		{
			gPads[i] = SDL_GameControllerOpen(deviceIndex);
			if(gPads[i] != NULL)
			{
				gPadIds[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gPads[i]));
				gPadPov[i] = 0xFFFF;
				printf("[OS]: Game controller %d: %s\n", i, SDL_GameControllerName(gPads[i]));
			}
			return;
		}
	}
}

static void OS_PadPov(int slot)
{
	SDL_GameController* pad = gPads[slot];
	int up = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
	int down = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
	int left = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
	int right = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
	int dx = right - left, dy = down - up;
	uint32_t eighth = 0xFFFF;
	if(dx != 0 || dy != 0)
	{
		static const uint32_t table[3][3] = { { 7, 0, 1 }, { 6, 0xFFFF, 2 }, { 5, 4, 3 } };
		eighth = table[dy + 1][dx + 1];
	}
	if(eighth != gPadPov[slot])
	{
		gPadPov[slot] = eighth;
		Engine_PushGlobalList(0x102, eighth, (uint32_t)slot);
	}
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
        else if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
        	SDL_Scancode sc = event.key.keysym.scancode;
        	uint32_t vk = sc < SDL_NUM_SCANCODES ? gVkOfScancode[sc] : 0;
        	uint32_t half = OS_HalfOf(sc);
        	int down = event.type == SDL_KEYDOWN;
        	if(half)
        		OS_SetVkHeld(half, down);
        	if(vk)
        	{
        		OS_SetVkHeld(vk, down);
        		if(down)
        			Input_KeyDown(vk);
        		else
        			Input_KeyUp(vk);
        	}
        }
        else if(event.type == SDL_MOUSEMOTION)
        {
        	Input_MouseMove(event.motion.x, event.motion.y);
        }
        else if(event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
        {
        	int button = -1;
        	switch(event.button.button)
        	{
        		case SDL_BUTTON_LEFT:   button = 0; break;
        		case SDL_BUTTON_RIGHT:  button = 1; break;
        		case SDL_BUTTON_MIDDLE: button = 2; break;
        		case SDL_BUTTON_X1:     button = 3; break;
        		case SDL_BUTTON_X2:     button = 4; break;
        	}
        	if(button >= 0)
        	{
        		static const uint32_t vks[5] = { 0x01, 0x02, 0x04, 0x05, 0x06 };
        		int down = event.type == SDL_MOUSEBUTTONDOWN;
        		OS_SetVkHeld(vks[button], down);
        		Input_MouseButton(button, down, event.button.x, event.button.y);
        	}
        }
        else if(event.type == SDL_CONTROLLERDEVICEADDED)
        {
        	OS_PadAdded(event.cdevice.which);
        }
        else if(event.type == SDL_CONTROLLERDEVICEREMOVED)
        {
        	int slot = OS_PadSlot(event.cdevice.which);
        	if(slot >= 0)
        	{
        		SDL_GameControllerClose(gPads[slot]);
        		gPads[slot] = NULL;
        	}
        }
        else if(event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
        {
        	int slot = OS_PadSlot(event.cbutton.which);
        	if(slot >= 0)
        	{
        		int down = event.type == SDL_CONTROLLERBUTTONDOWN;
        		int index = -1;
        		switch(event.cbutton.button)
        		{
        			case SDL_CONTROLLER_BUTTON_A:             index = 0; break;
        			case SDL_CONTROLLER_BUTTON_B:             index = 1; break;
        			case SDL_CONTROLLER_BUTTON_X:             index = 2; break;
        			case SDL_CONTROLLER_BUTTON_Y:             index = 3; break;
        			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  index = 4; break;
        			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: index = 5; break;
        			case SDL_CONTROLLER_BUTTON_BACK:          index = 6; break;
        			case SDL_CONTROLLER_BUTTON_START:         index = 7; break;
        			case SDL_CONTROLLER_BUTTON_LEFTSTICK:     index = 8; break;
        			case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    index = 9; break;
        			case SDL_CONTROLLER_BUTTON_DPAD_UP:
        			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        				OS_PadPov(slot);
        				break;
        		}
        		if(index >= 0)
        			Engine_PushGlobalList(0x100, (down ? 0x80u : 0u) | (uint32_t)index, (uint32_t)slot);
        	}
        }
        else if(event.type == SDL_CONTROLLERAXISMOTION)
        {
        	int slot = OS_PadSlot(event.caxis.which);
        	int axis = -1;
        	switch(event.caxis.axis)
        	{
        		case SDL_CONTROLLER_AXIS_LEFTX:  axis = 0; break;
        		case SDL_CONTROLLER_AXIS_LEFTY:  axis = 1; break;
        		case SDL_CONTROLLER_AXIS_RIGHTX: axis = 2; break;
        		case SDL_CONTROLLER_AXIS_RIGHTY: axis = 3; break;
        	}
        	if(slot >= 0 && axis >= 0)
        	{
        		// SDL's -32768..32767 onto DirectInput's range of -0x400..0x400.
        		int32_t value = (int32_t)event.caxis.value * 0x400 / 32767;
        		if(value < -0x400)
        			value = -0x400;
        		if(value != gPadAxis[slot][axis])
        		{
        			gPadAxis[slot][axis] = value;
        			int pair = axis / 2;
        			uint32_t x = (uint32_t)gPadAxis[slot][pair * 2] & 0xFFF;
        			uint32_t y = (uint32_t)gPadAxis[slot][pair * 2 + 1] & 0xFFF;
        			Engine_PushGlobalList(0x101, ((uint32_t)pair << 24) | (y << 12) | x, (uint32_t)slot);
        		}
        	}
        }
        else if(event.type == SDL_MOUSEWHEEL)
        {
        	int dy = event.wheel.y;
        	if(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        		dy = -dy;
        	Input_MouseWheel(dy);
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
	vk &= 0xFF;
	// VK_SHIFT, VK_CONTROL and VK_MENU are held when either half is.
	if(vk == 0x10) return gVkHeld[0x10] || gVkHeld[0xA0] || gVkHeld[0xA1];
	if(vk == 0x11) return gVkHeld[0x11] || gVkHeld[0xA2] || gVkHeld[0xA3];
	if(vk == 0x12) return gVkHeld[0x12] || gVkHeld[0xA4] || gVkHeld[0xA5];
	return gVkHeld[vk] != 0;
}

void OS_Present(const uint8_t* pixels, int width, int height, int stride)
{
	if(gRenderer == NULL)
		return;
	if(gFrameTexture == NULL || gFrameWidth != width || gFrameHeight != height)
	{
		if(gFrameTexture)
			SDL_DestroyTexture(gFrameTexture);
		gFrameTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_BGRA32,
		                                  SDL_TEXTUREACCESS_STREAMING, width, height);
		gFrameWidth = width;
		gFrameHeight = height;
		SDL_RenderSetLogicalSize(gRenderer, width, height);
	}
	if(gFrameTexture == NULL)
		return;
	SDL_UpdateTexture(gFrameTexture, NULL, pixels, stride);
	SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255);
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gFrameTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
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
