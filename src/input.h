#ifndef INPUT_H_
#define INPUT_H_

#include <stdint.h>

/*
 * The original's input model (fureraba.exe 0x0046D6E0 through 0x0046E3E0 and the
 * window procedure at 0x00499200).
 *
 * Every Win32 virtual key has a record (0x00518C98, 0x18 bytes): whether it is
 * held, whether a hold has been reported, the presses not yet taken, the presses
 * ever, and when a held key starts to repeat. The window procedure turns key and
 * mouse messages into presses and releases (0x0046DC00, 0x0046DC80) and posts a
 * system message for each; the scripts take presses (0x0046DCC0), which answers
 * the count and clears it.
 *
 * On top of that the scripts see twenty logical buttons (bits 0x40 to 0x80000000),
 * each a list of virtual keys the script can replace (Sys0 0x1B), five mouse bits
 * (1, 2, 4, 0x10, 0x20), four composite keys 0xC1 to 0xC4 that stand for a list,
 * and whether the skip keys (the 0x80000000 list, Ctrl by default) are held.
 * Keyboard input goes to the highest region in the focus list and mouse input to
 * the highest region under the pointer (both lists are region.c).
 */

// The window procedure's side: what the host feeds in. vk is a Win32 virtual key.
void Input_KeyDown(uint32_t vk);            // WM_KEYDOWN, 0x00499575
void Input_KeyUp(uint32_t vk);              // WM_KEYUP, 0x004995F2
void Input_MouseButton(int button, int down, int32_t x, int32_t y); // 0 left, 1 right, 2 middle, 3/4 X1/X2
void Input_MouseWheel(int delta);           // WM_MOUSEWHEEL, 0x00499A86
void Input_MouseMove(int32_t x, int32_t y); // in game pixels
void Input_GetMouse(int32_t* x, int32_t* y);// 0x0048E810
int  Input_ClickPosition(uint32_t button, int32_t* x, int32_t* y); // 0x0048E720

// 0x0046DC00 / 0x0046DC80: a press and a release of one virtual key.
int  Input_Press(uint32_t vk);
void Input_Release(uint32_t vk);
// 0x0046DCC0: the presses since the last take, with bit 31 set the first time a
// hold is seen; composite keys 0xC1-0xC4 add up their list.
uint32_t Input_Take(uint32_t vk);
// 0x0046DDF0: every press there has been.
uint32_t Input_Total(uint32_t vk);
// 0x0046D6E0 without the window test: whether the key is physically held.
int  Input_IsHeld(uint32_t vk);

// Script state.
void     Input_SetEnabled(uint32_t enabled);   // Sys0 0x10 (0x0046DAF0 then 0x0046DBA0)
void     Input_SetSkipAllowed(uint32_t v);     // Sys0 0x14 (0x0046DB00)
void     Input_SetSkipLatch(uint32_t v);       // Sys0 0x15 (0x0046DB10)
void     Input_SkipToggle(void);               // Sys0 0x16 (0x0046DB20)
uint32_t Input_SkipQuery(void);                // Sys0 0x17 (0x0046DFB0)
uint32_t Input_Activity(void);                 // Sys0 0x13 (0x0046E740)
uint32_t Input_RegionState(uint32_t key);      // Sys0 0x1A (0x0046E080)
uint32_t Input_RegionStateOf(uint32_t keyboardKey, uint32_t mouseKey); // 0x0046E080 itself
// The pointer shut-out list at 0x00566830 (0x0046E680 / 0x0046E6D0 / 0x0046E700).
void Input_AddModal(uint32_t id, uint32_t key);
void Input_RemoveModal(uint32_t id);
int  Input_AboveModal(uint32_t key);
uint32_t Input_MouseHeld(uint32_t key);        // 0x0046E610
int  Input_HeldLong(uint32_t vk);              // 0x0046DDC0
int  Input_BitForKey(uint32_t bits, uint32_t vk); // 0x0046E3F0
uint32_t Input_SetButtonKeys(const uint32_t* keys, uint32_t bit); // Sys0 0x1B (0x0046E120)
uint32_t Input_ButtonTotals(uint32_t mask);    // Sys0 0x1C (0x0046E1F0)
uint32_t Input_RegionTake(uint32_t vk, uint32_t key); // Sys0 0x1D (0x00488410)
uint32_t Input_SetSwapButtons(uint32_t v);     // Sys0 0x1E (0x0048EFE0)
uint32_t Input_ListTotals(const uint32_t* keys);// Sys0 0x12 (0x004881C0)

// 0x0046D990 / 0x0046D9B0: whether a region key has the keyboard, the mouse.
int Input_HasKeyboard(uint32_t key);
int Input_HasMouse(uint32_t key);

#endif
