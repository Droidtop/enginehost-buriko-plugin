#include <stdio.h>
#include <string.h>
#include "input.h"
#include "region.h"
#include "engine.h"
#include "os.h"

extern int gAppActive;

// ---------------------------------------------------------------- key records
typedef struct KeyRecord
{
	uint32_t down;      // +0x00
	uint32_t reported;  // +0x04, a hold has been answered with bit 31
	uint32_t count;     // +0x08, presses not yet taken
	uint32_t total;     // +0x0C, presses ever
	uint32_t repeatAt;  // +0x10, now + 500 ms at the press
	uint32_t user;      // +0x14
} KeyRecord_t;
static KeyRecord_t gKeys[256];   // 0x00518C98

static uint32_t gEnabled = 1;        // 0x00506A44
static uint32_t gSkipAllowed = 1;    // 0x00506A48
static uint32_t gSkipLatch = 0;      // 0x00566828
static uint32_t gSkipToggle = 0;     // 0x0056682C
static uint32_t gActivity = 0;       // 0x0056683C
static uint32_t gSwapButtons = 0;    // 0x0056691C
static int32_t  gMouseX = 0, gMouseY = 0;

// ---------------------------------------------------------------- key lists
// The logical buttons, each a zero-terminated list of virtual keys, with the
// original's defaults (0x00506734 to 0x00506A3C). 0x0E and 0x0F are the wheel,
// which the window procedure presses and releases (0x00499ADB).
#define LIST_MAX 17
static uint32_t gList40[LIST_MAX]       = { 0x0E };
static uint32_t gList80[LIST_MAX]       = { 0x0F };
static uint32_t gList100[LIST_MAX]      = { 0x0D };
static uint32_t gList200[LIST_MAX]      = { 0x20 };
static uint32_t gList1000[LIST_MAX]     = { 0x26 };
static uint32_t gList2000[LIST_MAX]     = { 0x28 };
static uint32_t gList4000[LIST_MAX]     = { 0x25 };
static uint32_t gList8000[LIST_MAX]     = { 0x27 };
static uint32_t gList10000[3]           = { 0x31, 0x61 };
static uint32_t gList20000[3]           = { 0x32, 0x62 };
static uint32_t gList40000[3]           = { 0x33, 0x63 };
static uint32_t gList80000[3]           = { 0x34, 0x64 };
static uint32_t gList100000[3]          = { 0x35, 0x65 };
static uint32_t gList200000[3]          = { 0x36, 0x66 };
static uint32_t gList400000[3]          = { 0x37, 0x67 };
static uint32_t gList800000[3]          = { 0x38, 0x68 };
static uint32_t gList1000000[3]         = { 0x39, 0x69 };
static uint32_t gList2000000[3]         = { 0x30, 0x60 };
static uint32_t gList40000000[LIST_MAX] = { 0x09 };
static uint32_t gListSkip[LIST_MAX]     = { 0x11 };   // 0x005069F8, bit 0x80000000
// The mouse bits (0x0050670C to 0x0050672C).
static const uint32_t gMouse1[2] = { 0x01 }, gMouse2[2] = { 0x02 }, gMouse4[2] = { 0x04 },
                      gMouse10[2] = { 0x05 }, gMouse20[2] = { 0x06 };
// The composite keys 0xC1-0xC4 (0x0046DD1C): the 0x100 list, the 0x200 list,
// and two of their own.
static const uint32_t gCompositeUp[3]   = { 0x26, 0x0E };   // 0x005069AC
static const uint32_t gCompositeDown[3] = { 0x28, 0x0F };   // 0x00506A38

// 0x0046DE00's order: the twenty lists and their bits.
static const struct { uint32_t* list; uint32_t bit; } kButtons[] = {
	{ gList40, 0x40 }, { gList80, 0x80 }, { gList100, 0x100 }, { gList200, 0x200 },
	{ gList1000, 0x1000 }, { gList2000, 0x2000 }, { gList4000, 0x4000 }, { gList8000, 0x8000 },
	{ gList10000, 0x10000 }, { gList20000, 0x20000 }, { gList40000, 0x40000 }, { gList80000, 0x80000 },
	{ gList100000, 0x100000 }, { gList200000, 0x200000 }, { gList400000, 0x400000 }, { gList800000, 0x800000 },
	{ gList1000000, 0x1000000 }, { gList2000000, 0x2000000 }, { gList40000000, 0x40000000 },
};
#define BUTTON_COUNT (sizeof(kButtons) / sizeof(kButtons[0]))

static void Input_Activate(void) { gActivity++; }   // 0x0046E730

// ---------------------------------------------------------------- records
int Input_IsHeld(uint32_t vk)
{
	return OS_IsKeyDown(vk);
}

int Input_Press(uint32_t vk)
{
	KeyRecord_t* r = &gKeys[vk & 0xFF];
	int newly = r->down == 0;
	// The mouse buttons (1, 2, 4, 5, 6) count every press and start a new hold;
	// any other key counts only the press that finds it up (0x0046DC74).
	int alwaysCounts = vk >= 1 && vk <= 6 && vk != 3;
	if(alwaysCounts)
		r->reported = 0;
	if(alwaysCounts || !r->down)
	{
		r->count++;
		r->total++;
		r->repeatAt = OS_GetTicks() + 500;
	}
	r->down = 1;
	return newly;
}

void Input_Release(uint32_t vk)
{
	KeyRecord_t* r = &gKeys[vk & 0xFF];
	r->down = 0;
	r->reported = 0;
}

static uint32_t Input_TakeList(const uint32_t* list)
{
	uint32_t sum = 0, flags = 0;
	for(int i = 0; list[i] != 0; i++)
	{
		uint32_t r = Input_Take(list[i]);
		if(r)
		{
			flags |= r & 0x80000000u;
			sum += r & 0x7FFFFFFFu;
		}
	}
	return flags | sum;
}

uint32_t Input_Take(uint32_t vk)
{
	KeyRecord_t* r = &gKeys[vk & 0xFF];
	// A hold whose release was missed ends here (0x0046DCE9).
	if(r->down && !Input_IsHeld(vk))
		Input_Release(vk);
	if(vk >= 0xC1 && vk <= 0xD7)
	{
		switch(vk)
		{
			case 0xC1: return Input_TakeList(gList100);
			case 0xC2: return Input_TakeList(gList200);
			case 0xC3: return Input_TakeList(gCompositeUp);
			case 0xC4: return Input_TakeList(gCompositeDown);
			default:   return 0;
		}
	}
	uint32_t result = r->count;
	if(r->down && !r->reported)
	{
		result |= 0x80000000u;
		r->reported = 1;
	}
	r->count = 0;
	return result;
}

uint32_t Input_Total(uint32_t vk)
{
	return gKeys[vk & 0xFF].total;
}

// ---------------------------------------------------------------- the host's side
void Input_KeyDown(uint32_t vk)
{
	vk &= 0xFF;
	if(gKeys[vk].user)
		Input_Release(vk);
	if(Input_Press(vk))
	{
		Input_Activate();
		Engine_PushGlobalList(3, vk, 0);
	}
}

void Input_KeyUp(uint32_t vk)
{
	Input_Release(vk & 0xFF);
}

void Input_MouseMove(int32_t x, int32_t y)
{
	gMouseX = x;
	gMouseY = y;
}

void Input_GetMouse(int32_t* x, int32_t* y)
{
	*x = gMouseX;
	*y = gMouseY;
}

void Input_MouseButton(int button, int down, int32_t x, int32_t y)
{
	// The virtual key each physical button is, with the left and right exchanged
	// when the buttons are swapped (0x0048F000 inside the window procedure).
	static const uint32_t vks[5] = { 0x01, 0x02, 0x04, 0x05, 0x06 };
	if(button < 0 || button > 4)
		return;
	if(gSwapButtons && button < 2)
		button ^= 1;
	uint32_t vk = vks[button];
	Input_MouseMove(x, y);
	if(down)
	{
		if(button == 0)
			Engine_PushGlobalList(0x80, 0, 0);
		else if(button == 1)
			Engine_PushGlobalList(0x81, 0, 0);
		Input_Activate();
		Engine_PushGlobalList(3, vk, 0);
		Input_Press(vk);
	}
	else
		Input_Release(vk);
}

void Input_MouseWheel(int delta)
{
	if(delta == 0)
		return;
	uint32_t vk = delta < 0 ? 0x0F : 0x0E;
	Input_Activate();
	Engine_PushGlobalList(3, vk, 0);
	Input_Press(vk);
	Input_Release(vk);
}

// ---------------------------------------------------------------- regions
int Input_HasKeyboard(uint32_t key)
{
	if(!gEnabled)
		return 0;
	Region_t* top = Region_First(1);
	return top != NULL && key >= top->key;
}

int Input_HasMouse(uint32_t key)
{
	if(!gEnabled)
		return 0;
	int32_t x = gMouseX, y = gMouseY;
	for(Region_t* r = Region_First(0); r != NULL; r = r->next)
	{
		// Regions owned by a display object hit-test through it (vtable+0x64);
		// none are made yet (0x0046D860 has no caller that is written).
		int inside = r->left <= x && x <= r->right && r->top <= y && y <= r->bottom;
		if(r->key < key)
			return 0;
		if(r->key == key)
		{
			if(inside)
				return 1;
			continue;
		}
		if(inside)
			return 0;
	}
	return 0;
}

static uint32_t Input_TakeButtons(void)
{
	// 0x0046DE00: per list, the first key that was pressed sets the bit and the
	// rest of that list is left untaken.
	uint32_t bits = 0;
	for(size_t i = 0; i < BUTTON_COUNT; i++)
	{
		for(int k = 0; kButtons[i].list[k] != 0; k++)
		{
			if(Input_Take(kButtons[i].list[k]))
			{
				bits |= kButtons[i].bit;
				break;
			}
		}
	}
	return bits;
}

uint32_t Input_RegionState(uint32_t key)
{
	uint32_t bits = 0;
	if(Input_HasKeyboard(key))
	{
		bits = Input_TakeButtons();
		if(Input_SkipQuery())
			bits |= 0x80000000u;
	}
	if(Input_HasMouse(key))
	{
		if(Input_Take(1)) bits |= 0x01;
		if(Input_Take(2)) bits |= 0x02;
		if(Input_Take(4)) bits |= 0x04;
		if(Input_Take(5)) bits |= 0x10;
		if(Input_Take(6)) bits |= 0x20;
	}
	return bits;
}

uint32_t Input_RegionTake(uint32_t vk, uint32_t key)
{
	int has = (vk == 1 || vk == 2) ? Input_HasMouse(key) : Input_HasKeyboard(key);
	return has ? Input_Take(vk) : 0;
}

// ---------------------------------------------------------------- script state
void Input_SetEnabled(uint32_t enabled)
{
	gEnabled = enabled;
	// 0x0046DBA0 clears down, reported, count and total of every key.
	for(int i = 0; i < 256; i++)
	{
		gKeys[i].down = 0;
		gKeys[i].reported = 0;
		gKeys[i].count = 0;
		gKeys[i].total = 0;
	}
}

void Input_SetSkipAllowed(uint32_t v) { gSkipAllowed = v; }
void Input_SetSkipLatch(uint32_t v)   { gSkipLatch = v; }
uint32_t Input_Activity(void)         { return gActivity; }

void Input_SkipToggle(void)
{
	gSkipToggle = 1;
	for(int i = 0; gListSkip[i] != 0; i++)
		Input_Take(gListSkip[i]);
}

uint32_t Input_SkipQuery(void)
{
	uint32_t state = gSkipLatch;
	int held = 0;
	if(gAppActive && gEnabled)
	{
		for(int i = 0; gListSkip[i] != 0; i++)
		{
			int physical = OS_IsKeyDown(gListSkip[i]);
			if(held || physical)
				held = 1;
			if(gSkipToggle)
			{
				if(Input_Take(gListSkip[i]) && gAppActive)
					state = 1;
			}
			else if(physical && gAppActive)
				return gSkipAllowed ? 1 : 0;
		}
	}
	if(gSkipToggle && !held)
		gSkipToggle = 0;
	return (state && gSkipAllowed) ? 1 : 0;
}

uint32_t Input_SetButtonKeys(const uint32_t* keys, uint32_t bit)
{
	uint32_t n = 0;
	while(keys[n] != 0)
		n++;
	if(n >= 0x10)
		return 0x80000002u;
	uint32_t* list;
	switch(bit)
	{
		case 0x40:        list = gList40; break;
		case 0x80:        list = gList80; break;
		case 0x100:       list = gList100; break;
		case 0x200:       list = gList200; break;
		case 0x1000:      list = gList1000; break;
		case 0x2000:      list = gList2000; break;
		case 0x4000:      list = gList4000; break;
		case 0x8000:      list = gList8000; break;
		case 0x40000000u: list = gList40000000; break;
		case 0x80000000u: list = gListSkip; break;
		default:          return 0x80000001u;
	}
	memcpy(list, keys, (n + 1) * sizeof(uint32_t));
	return 0;
}

uint32_t Input_ListTotals(const uint32_t* keys)
{
	uint32_t sum = 0;
	for(int i = 0; keys[i] != 0; i++)
		sum += Input_Total(keys[i]);
	return sum;
}

uint32_t Input_ButtonTotals(uint32_t mask)
{
	static const struct { const uint32_t* list; uint32_t bit; } kAll[] = {
		{ gMouse1, 0x01 }, { gMouse2, 0x02 }, { gMouse4, 0x04 }, { gMouse10, 0x10 }, { gMouse20, 0x20 },
	};
	uint32_t sum = 0;
	for(size_t i = 0; i < sizeof(kAll) / sizeof(kAll[0]); i++)
		if(mask & kAll[i].bit)
			sum += Input_ListTotals(kAll[i].list);
	for(size_t i = 0; i < BUTTON_COUNT; i++)
		if(mask & kButtons[i].bit)
			sum += Input_ListTotals(kButtons[i].list);
	return sum;
}

uint32_t Input_SetSwapButtons(uint32_t v)
{
	if(v > 1)
		return 0;
	gSwapButtons = v;
	return 1;
}
