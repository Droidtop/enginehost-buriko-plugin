//
// A window's message (message.h).
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "engine.h"
#include "font.h"
#include "input.h"
#include "nametable.h"
#include "object.h"
#include "os.h"
#include "process.h"
#include "region.h"
#include "renderer.h"
#include "text.h"
#include "thread.h"
#include "window.h"

// 0x00507690, the buttons the process code counts (process.c).
extern uint32_t gProcessKeyButtons;

uint32_t gMessageHold = 0;
uint32_t gMessageHoldTime = 0;
uint32_t gMessageAuto = 0;
uint32_t gMessageAutoTime = 0;
uint32_t gMessageKeyShowsAll = 0;
uint32_t gMessageRegionMode = 0;
uint32_t gMessageRegionPriority = 0;
uint32_t gMessageNoFlush = 0;
uint32_t gMessageCharacterWait = 0x32;

uint32_t Message_SetRegionMode(uint32_t mode, uint32_t priority)
{
	// 0x00432E60.
	if(mode == 0 || mode == 2)
	{
		gMessageRegionMode = mode;
		return 0;
	}
	if(mode != 1)
		return 0x80000004;
	if(priority >= 0x10000)
		return 0x80000005;
	gMessageRegionMode = 1;
	gMessageRegionPriority = priority;
	return 0;
}

// ----------------------------------------------------------------------------------
// The message object. The original's is a process (0x004319D0) carrying a window
// (0x00432CA0, 0x7C bytes) and a layout of records (0x004342B0, 0xF8 bytes); the
// offsets are those of the 0xF8-byte object.
// ----------------------------------------------------------------------------------
typedef struct PrintMessage
{
	Process_t*   process;
	Thread_t*    thread;
	Renderer_t*  renderer;
	uint32_t     deadline;          // +0x08, on the message's own clock
	uint32_t     dirty;             // +0x0C
	uint32_t     window;            // +0x20, kept as the handle
	uint32_t     pressed;           // +0x24, the keys of this pass
	uint32_t     colour;            // +0x2C
	uint32_t     instant;           // +0x30
	uint32_t     skip;              // +0x34
	uint32_t     waitForKey;        // +0x38
	uint32_t     skipKeyAllowed;    // +0x3C
	uint32_t     buttonsAllowed;    // +0x40
	uint32_t     hold;              // +0x44
	uint32_t     holdUntil;         // +0x48
	uint32_t     autoAt;            // +0x5C
	uint32_t     pageFull;          // +0x60
	uint32_t     frame;             // +0x64, the wait glyph's frame, 0 before it starts
	uint32_t     toEnd;             // +0x74
	uint32_t     regionKey;         // +0x78
	TextLayout_t layout;            // +0x7C, the records at +0xC0
	uint32_t     ruby;              // +0xC4
	TextStyle_t  style;             // +0xC8
	uint32_t     rubyColour;        // +0xDC
	uint32_t     firstPass;         // +0xE0
	uint32_t     lastReal;          // +0xE8
	uint64_t     clock;             // +0xF0, milliseconds in 16.16
} PrintMessage_t;

static DisplayObject_t* Message_Window(PrintMessage_t* m)
{
	return Object_ResolveKind(m->window, OBJECT_TYPE_WINDOW);
}

// 0x004376E0: every record shown.
static int Message_AllShown(const PrintMessage_t* m)
{
	for(TextRecord_t* record = m->layout.records; record != NULL; record = record->next)
		if(!record->shown)
			return 0;
	return 1;
}

// 0x004381F0, the message's clock (its vtable+0x0C): the real milliseconds since the
// last call, at the speed 0x00507654 (16.16; Grp1 0x9A function 0x80000001) while
// characters are still to come and at 1 once all are shown.
static uint64_t Message_Now(PrintMessage_t* m)
{
	uint32_t real = OS_GetTicks();
	uint32_t elapsed = real - m->lastReal;
	m->lastReal = real;
	uint64_t speed = Message_AllShown(m) ? 0x10000u : gFunctionParameters[2];
	m->clock += (uint64_t)elapsed * speed;
	return m->clock >> 16;
}

// 0x00431AF0: the next deadline, `delay` from now.
static void Message_SetDelay(PrintMessage_t* m, uint32_t delay)
{
	m->deadline = (uint32_t)Message_Now(m) + delay;
}

// 0x00431B40: the deadline has come.
static int Message_Due(PrintMessage_t* m)
{
	uint64_t now = Message_Now(m);
	return (now >> 32) != 0 || m->deadline <= (uint32_t)now;
}

// 0x004338A0 (vtable+0x10): the screen is to be presented, unless 0x00565BAC says not.
static void Message_MarkDirty(PrintMessage_t* m)
{
	if(gMessageNoFlush == 0)
		m->dirty = 1;
}

// 0x00443320: a rectangle of the window damaged on the screen. The screen here is
// recomposed whole, so any damage is a frame.
static void Message_Damage(DisplayObject_t* window)
{
	if(Object_IsDrawable(window))
		gObjectDamage++;
}

// One record onto the window's text surface: what was there under its ink taken out
// (mode 0x40, weight 1), then the record blended on with `transparency` (mode 1).
static void Message_DrawRecord(PrintMessage_t* m, DisplayObject_t* window, TextRecord_t* record, uint32_t transparency)
{
	Window_DrawText(m->renderer, window, record->x, record->y, &record->bitmap, BITMAP_BLEND_ERASE, 1);
	Window_DrawText(m->renderer, window, record->x, record->y, &record->bitmap, BITMAP_BLEND_ALPHA_TRANS,
	                (int)transparency);
	Message_Damage(window);
	Message_MarkDirty(m);
}

// 0x00437700: the characters whose time has come are drawn, fading in over their
// fade length; with the message skipped, made instant, or no delay between
// characters (0x00507638), all of them at once and whole.
static void Message_Reveal(PrintMessage_t* m, DisplayObject_t* window)
{
	int instant = m->skip || m->instant || Text_DelayStep() == 0;
	if(gFunctionParameters[1] != 0)
	{
		// 0x0043773B: real time. A record starts once the clock has passed its
		// delay; an event record (0x80000000) posts its place instead (0x004966D0
		// with 0x30000001).
		uint32_t t = (uint32_t)Message_Now(m) - m->deadline;
		for(TextRecord_t* record = m->layout.records; record != NULL; record = record->next)
		{
			if(record->shown)
				continue;
			if(record->delay > t && !instant)
				continue;
			if(record->kind == 0x80000000u)
			{
				Engine_PushGlobalList(0x30000001, (uint32_t)record->x, (uint32_t)record->y);
				record->shown = 1;
				continue;
			}
			record->fade = t - record->delay;
			if(record->fade < record->fadeLength && !instant)
				Message_DrawRecord(m, window, record, 0x100 - (record->fade << 8) / record->fadeLength);
			else
			{
				Message_DrawRecord(m, window, record, 0);
				record->shown = 1;
			}
		}
		return;
	}

	// 0x00437882: steps of the clock. Every millisecond since the deadline is one
	// step; each step counts every waiting record's delay down and every fading
	// record's fade up, and only the last step of a pass draws.
	uint32_t steps = 0;
	while(!instant && Message_Due(m))
	{
		m->deadline += 1;   // 0x00431B10
		steps++;
	}
	for(;;)
	{
		if(steps == 0 && !instant)
			break;
		uint32_t remaining = steps - 1;
		for(TextRecord_t* record = m->layout.records; record != NULL; record = record->next)
		{
			if(record->shown)
				continue;
			if(record->delay != 0 && !instant)
			{
				record->delay--;
				if(record->delay == 0)
				{
					record->fade = 0;
					record->fadeLength = Text_FadeLength();
				}
				continue;
			}
			if(record->fade < record->fadeLength && !instant)
			{
				if(remaining == 0)
					Message_DrawRecord(m, window, record, 0x100 - (record->fade << 8) / record->fadeLength);
				record->fade++;
			}
			else
			{
				Message_DrawRecord(m, window, record, 0);
				record->shown = 1;
			}
		}
		if(instant)
			break;
		steps = remaining;
	}
	Message_SetDelay(m, 0);
}

// 0x00434020: the wait at the end of the message, with the animated glyph (Grp0 0x98
// to 0x9A) in the window's text part 0 after the last character. Answers whether a
// key ended it.
static int Message_WaitGlyph(PrintMessage_t* m, DisplayObject_t* window)
{
	Renderer_t* renderer = m->renderer;
	if(m->frame == 0)
	{
		// 0x00434032: the glyph's widest frame must still fit on the line; if not
		// the cursor goes to the next one, and when there is none the message
		// stops with its page full (0x00433A70).
		uint32_t widest = 0;
		for(int i = 0; i < renderer->animationFrameCount; i++)
			if(renderer->animationFrames[i] != NULL && (uint32_t)renderer->animationFrames[i]->width > widest)
				widest = (uint32_t)renderer->animationFrames[i]->width;
		if(!Window_Fits(window, (int32_t)widest))
		{
			if(!Window_NewLine(window))
			{
				m->pageFull = 1;
				return 0;
			}
			// vtable+0x2C (0x00437A80): past the ruby indent.
			Window_AdvanceCursor(window, Text_RubyIndent());
		}
		// 0x0043407A: only the skip key's bit survives into the wait.
		m->pressed &= 0x80000000u;
		if(m->pressed == 0)
			m->skip = 0;
		m->instant = 0;
		Message_SetDelay(m, 0);
		m->frame++;
	}
	Rect_t area;
	if(Window_PartScreenRect(renderer, window, 0, &area))
		Message_Damage(window);
	int ended = m->pressed != 0;
	if(ended)
	{
		Window_EnablePart(renderer, window, 0, 0);
		m->frame = 0;
	}
	else
	{
		Message_SetDelay(m, renderer->animationInterval);
		int count = renderer->animationFrameCount;
		if(count > 0)
		{
			Bitmap_t* entry = renderer->animationFrames[m->frame - 1];
			m->frame = m->frame < (uint32_t)count ? m->frame + 1 : m->frame + 1 - (uint32_t)count;
			if(entry != NULL && entry->bitmap != NULL)
			{
				int32_t x, y;
				if(renderer->animationPlacement == 1)
				{
					x = renderer->animationX;
					y = renderer->animationY;
				}
				else
				{
					// vtable+0x30 (0x00437A90): ruby lifts the glyph by its size.
					int32_t lift = 0;
					FontEntryInfo_t font;
					if(m->ruby && Font_GetInfo(window->fontId, &font))
						lift = Text_RubySize(font.size);
					x = window->textCursorX + renderer->animationX;
					y = window->textCursorY + renderer->animationY + lift;
				}
				Window_SetPartPosition(renderer, window, 0, x, y, 0);
				Window_SetPartBitmap(renderer, window, 0, entry);
				Window_EnablePart(renderer, window, 0, 1);
			}
			else
				Window_EnablePart(renderer, window, 0, 0);
		}
	}
	if(Window_PartScreenRect(renderer, window, 0, &area))
		Message_Damage(window);
	Message_MarkDirty(m);
	return ended;
}

// 0x00433F20: once every character is shown. With a wait asked for, the glyph runs
// until a key (or the automatic mode) ends it, but not before the minimum hold
// (Grp0 0x9B) unless the message was skipped; without one, the glyph's part goes and
// the message is done.
static int Message_End(PrintMessage_t* m, DisplayObject_t* window)
{
	if(m->waitForKey && !m->toEnd)
	{
		if(gMessageAuto && m->frame == 0)
			m->autoAt = OS_GetTicks() + gMessageAutoTime;
		if(m->hold)
		{
			if(m->holdUntil > OS_GetTicks() && !m->skip)
				return 0;
			m->hold = 0;
		}
		int ended = 0;
		if(Message_Due(m) || m->skip)
			ended = Message_WaitGlyph(m, window);
		if(gMessageAuto && !ended && m->autoAt <= OS_GetTicks())
			m->toEnd = 1;
		return ended;
	}
	Rect_t area;
	if(Window_PartScreenRect(m->renderer, window, 0, &area))
	{
		Message_Damage(window);
		Window_EnablePart(m->renderer, window, 0, 0);
	}
	Message_MarkDirty(m);
	return 1;
}

// 0x00434F70 (vtable+0x24): one pass. Nothing happens before the deadline unless the
// message was skipped or told to end; otherwise characters are revealed, and once
// all are shown the end wait runs.
static int Message_Step(PrintMessage_t* m, DisplayObject_t* window)
{
	if(gFunctionParameters[1] != 0)
	{
		if(m->firstPass)
		{
			Message_SetDelay(m, 0);
			m->firstPass = 0;
		}
	}
	else if(!Message_Due(m) && !m->skip && !m->toEnd && m->autoAt == 0)
		return 0;
	if(Message_AllShown(m))
		return Message_End(m, window);
	Message_Reveal(m, window);
	return 0;
}

// 0x00434230 (vtable+0x18): an event posted to the message. 1 and 0x102 end it at
// once and whole, 0x100 ends its wait, 0x101 sets whether the skip key counts.
static void Message_Event(PrintMessage_t* m, const ProcessEvent_t* event)
{
	switch(event->value[0])
	{
		case 0x100:
			m->toEnd = 1;
			break;
		case 0x101:
			m->skipKeyAllowed = event->value[1];
			break;
		case 1:
		case 0x102:
			m->toEnd = 1;
			m->instant = 1;
			break;
	}
}

// 0x004336E0 (vtable+0x04): the run. 1 once the message is finished, having pushed
// whether it was skipped.
static int Message_Run(void* context)
{
	PrintMessage_t* m = (PrintMessage_t*)context;
	Process_t* process = m->process;

	// 0x00431BD0: an event of 0 aborts the message.
	while(process->events != NULL && !process->aborted)
	{
		ProcessEvent_t* event = process->events;
		process->events = event->next;
		if(process->events == NULL)
			process->eventsTail = NULL;
		if(event->value[0] == 0)
			process->aborted = 1;
		else
			Message_Event(m, event);
		free(event);
	}

	DisplayObject_t* window = Message_Window(m);
	if(window == NULL)
	{
		printf("[Message]: Error: the window 0x%.8X went away under its message\n", m->window);
		Thread_PushStack(m->thread, m->skip);
		return 1;
	}

	// 0x004336EB: this pass's keys on the message's region, the buttons and the skip
	// key; the skip key only when +0x3C allows it, the buttons only when +0x40 does.
	uint32_t buttons = gProcessKeyButtons;
	m->pressed = Input_RegionState(m->regionKey) & (buttons | 0x80000181u);
	if(!m->skipKeyAllowed)
		m->pressed &= 0x7FFFFFFFu;
	if(!m->buttonsAllowed)
		m->pressed &= ~(buttons | 0x80u);
	if(m->pressed != 0)
	{
		m->skip = 1;
		if(gMessageKeyShowsAll)
			m->toEnd = 1;
	}

	int finished = Message_Step(m, window);
	// 0x00431B80: what was drawn is presented. The screen is recomposed whenever
	// something was damaged, so there is nothing more to do than to forget it.
	m->dirty = 0;

	if(finished || !m->thread->engine->isRunning || !gProcessesWait || process->aborted)
	{
		Thread_PushStack(m->thread, m->skip);
		return 1;
	}
	return 0;
}

// 0x004343C0 and 0x00432DE0: the records freed and the region given back.
static void Message_Free(void* context)
{
	PrintMessage_t* m = (PrintMessage_t*)context;
	Text_FreeLayout(&m->layout);
	Region_RemoveByKey(0, m->regionKey);
	Region_RemoveByKey(1, m->regionKey);
	free(m);
}

uint32_t Message_Print(Thread_t* thread, uint32_t windowHandle, const char* text,
                       const TextStyle_t* style, const MessageRequest_t* request)
{
	Renderer_t* renderer = thread->engine->renderer;
	// 0x004913DC: the window (0x004407A0), and a font set on it (0x0042C530).
	DisplayObject_t* window = Object_ResolveKind(windowHandle, OBJECT_TYPE_WINDOW);
	if(window == NULL)
		return 0xFFFFFFFFu;
	if(window->fontId == 0)
		return 0x80000001;
	// 0x00491465: a window that writes downwards takes the vertical class.
	if(window->textDirection == 1)
	{
		printf("[Message]: a message in a window that writes downwards needs the vertical class (0x00438270), which is not written yet\n");
		return 0xFFFFFFFE;
	}

	PrintMessage_t* m = (PrintMessage_t*)calloc(1, sizeof(PrintMessage_t));
	if(m == NULL)
		return 0xFFFFFFFE;
	m->thread = thread;
	m->renderer = renderer;
	m->window = windowHandle;

	// 0x00432CA0, the base: the region the keys are taken from (0x00432CFE), put on
	// both lists and its presses so far taken (0x0046D840, 0x0046D8A0, 0x0046E080);
	// the window's text shown, opaque, the window redrawn and its eight text parts
	// off; and the minimum hold.
	if(gMessageRegionMode == 0)
		m->regionKey = 2;
	else if(gMessageRegionMode == 1)
		m->regionKey = REGION_KEY(gMessageRegionPriority);
	else
		m->regionKey = REGION_KEY(window->layer);
	Region_Add(0, m->regionKey, (int32_t)0x80000000, (int32_t)0x80000000, 0x7FFFFFFF, 0x7FFFFFFF, NULL);
	Region_Add(1, m->regionKey, 0, 0, 0, 0, NULL);
	Input_RegionState(m->regionKey);
	Window_SetTextTransparency(window, 0);
	Window_SetTextShown(window, 1);
	Window_RedrawAll(renderer, window);
	for(int i = 0; i < 8; i++)
		Window_EnablePart(renderer, window, i, 0);
	m->hold = gMessageHold;
	m->holdUntil = OS_GetTicks() + gMessageHoldTime;

	// 0x004342B0: the default style, the first pass, and the clock started.
	Text_DefaultStyle(&m->style);
	m->firstPass = 1;
	m->lastReal = OS_GetTicks();
	m->clock = (uint64_t)m->lastReal << 16;

	// 0x00434EC0 and 0x00434EE0: the colour, the ruby colour and the style asked for
	// (a style 0x00434F10 refuses leaves the default).
	m->colour = request->colour;
	m->rubyColour = request->rubyColour;
	Text_MakeStyle(&m->style, style->kind, style->a, style->b, style->colour, style->weight);

	// 0x004351A0: the text laid out from the window's cursor within its client area,
	// in its font, line height and proportional setting, against the global ruby
	// dictionary (0x00565BB4); then the ruby, the alignment, and the cursor left after
	// the text.
	int32_t cursor[2] = { window->textCursorX, window->textCursorY };
	Rect_t client;
	Window_ClientRect(window, &client);
	uint32_t lines = 0;
	NameTable_t* dictionary = NameTable_Global();
	if(Text_Layout(renderer, &m->layout, text != NULL ? text : "", request->ruby, cursor, &client,
	               Window_LineHeight(window), window->fontId, window->proportional, request->kinsoku,
	               m->colour, &m->style, &lines, dictionary))
	{
		if(request->ruby)
			Text_LayoutRuby(renderer, &m->layout, window->fontId, &m->style, m->rubyColour, dictionary);
		Text_AlignLayout(renderer, &m->layout, cursor, &client, window->fontId, request->kinsoku,
		                 &m->style, window->textAlign);
		window->textCursorX = cursor[0];
		window->textCursorY = cursor[1];
		m->ruby = request->ruby;
		Message_SetDelay(m, 0);
	}

	// 0x0049154C: the four flags, and the thread waits on the message.
	m->instant = request->instant;
	m->waitForKey = request->waitForKey;
	m->skipKeyAllowed = request->skipKeyAllowed;
	m->buttonsAllowed = request->buttonsAllowed;

	m->process = Process_CreateCallback(thread, Message_Run, Message_Free, m);
	if(m->process == NULL)
	{
		Message_Free(m);
		return 0xFFFFFFFE;
	}
	Thread_SetProcess(thread, m->process);
	return 0;
}
