#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"
#include "engine.h"
#include "process.h"
#include "xform.h"

// ----------------------------------------------------------------------------------
// Class data. The original keeps these fields in the object from +0x13C on; one
// struct stands for every class here.
// ----------------------------------------------------------------------------------
typedef struct ScreenLayer
{
	uint32_t set;         // +0x140, an image has been given (0x0041DE90)
	uint32_t shown;       // +0x144 (0x0041DCF0)
	int32_t  x, y;        // +0x148, +0x14C: where the anchor goes, 16.16 (0x0041DD50)
	uint32_t f150;        // +0x150
	uint32_t level;       // +0x154, the layer's effect level (0x0041DE30)
	int32_t  bitmap;      // +0x158
	uint32_t serial;      // +0x15C
	int32_t  anchorX;     // +0x160, 16.16
	int32_t  anchorY;     // +0x164
	uint32_t angle;       // +0x168 (0x0041DF50)
	uint32_t scaleX;      // +0x16C, 16.16
	uint32_t scaleY;      // +0x170
	uint32_t f174, f178;  // +0x174, +0x178 (0x0041DFC0)
	int32_t  f17C, f180;  // +0x17C, +0x180 (0x0041DFF0)
	uint32_t f184;        // +0x184 (0x0041E020)
	uint32_t f188, f18C;  // +0x188, +0x18C (0x0041E050)
	uint32_t f190;        // +0x190
} ScreenLayer_t;

typedef struct ScreenData
{
	// Class 4 (0x0041D2E0).
	int32_t  x, y;               // +0x13C, +0x140: the image's offset (0x0041D3E0)
	int32_t  bitmap;             // +0x144
	uint32_t bitmapSerial;       // +0x148
	int32_t  fromX, fromY;       // +0x14C, +0x150
	int32_t  fromBitmap;         // +0x154: a bitmap, or 0x7000 black, 0x7001 white, 0x7FFF / -1 none
	uint32_t fromSerial;         // +0x158
	int32_t  rule;               // +0x15C, -1 for none
	uint32_t ruleParameter;      // +0x160
	uint32_t ruleSerial;         // +0x164
	uint32_t param168, param16C; // +0x168, +0x16C (parameter 0x40000000, 0x0041D5E0)
	// Class 12 (0x0041DBF0).
	uint32_t current;            // +0x13C
	ScreenLayer_t layers[8];     // +0x140, 0x54 bytes each
} ScreenData_t;

static ScreenData_t* Screen_Data(DisplayObject_t* object)
{
	if(object->screen == NULL)
		object->screen = (ScreenData_t*)calloc(1, sizeof(ScreenData_t));
	return object->screen;
}

void Screen_FreeData(DisplayObject_t* object)
{
	free(object->screen);
	object->screen = NULL;
}

uint32_t Screen_Class(void)
{
	DisplayObject_t* screen = Object_Resolve(OBJECT_HANDLE_SCREEN);
	return screen ? screen->kind : 0;
}

uint32_t Screen_Replace(uint32_t cls)
{
	DisplayObject_t* screen = Object_Resolve(OBJECT_HANDLE_SCREEN);
	if(screen != NULL && screen->kind == cls)
		return 0;
	// 0x0043E2D9: classes 1 to 12, each its own constructor; anything else is the
	// plain class 1 (0x0043E4C3).
	if(cls < 1 || cls > 12)
		cls = 1;
	if(cls != 1 && cls != 4 && cls != 12)
		printf("[Screen]: Warning: the screen class %u's own draw is not written yet; it draws nothing\n", cls);
	DisplayObject_t* fresh = Object_ReplaceScreen();
	if(fresh == NULL)
		return 1;
	fresh->kind = cls;
	ScreenData_t* data = Screen_Data(fresh);
	if(cls == 4)
	{
		// 0x0041D325: +0x144, +0x154 and +0x15C all -1, and 0x0041D5E0(0, 0).
		data->bitmap = -1;
		data->fromBitmap = -1;
		data->rule = -1;
	}
	return 0;
}

void Screen_SetShown(uint32_t visible, uint32_t content)
{
	DisplayObject_t* screen = Object_Resolve(OBJECT_HANDLE_SCREEN);
	if(screen == NULL)
		return;
	// 0x0043E570: root+0x54 and vtable+0x04 (0x0041AED0, the visible flag at +0x14),
	// then root+0x58 and vtable+0x78 (0x0041C3F0, +0x138).
	Object_ApplyVisible(screen, (int)visible);
	screen->screenContent = content;
}

// ----------------------------------------------------------------------------------
// Class 4
// ----------------------------------------------------------------------------------
static int Screen_IsSpecialFrom(int32_t b)
{
	return b == 0x7000 || b == 0x7001 || b == 0x7FFF || b == -1;
}

uint32_t Screen_SetTransition(int32_t x, int32_t y, int32_t bitmap,
                              int32_t fromX, int32_t fromY, int32_t fromBitmap,
                              int32_t rule, uint32_t ruleParameter, uint32_t level)
{
	Renderer_t* renderer = gEngine->renderer;
	Screen_Replace(4);
	DisplayObject_t* screen = Object_Resolve(OBJECT_HANDLE_SCREEN);
	ScreenData_t* data = Screen_Data(screen);

	// 0x0041D420.
	if(Renderer_ResolveBitmap(renderer, bitmap) == NULL)
		return 1;
	if(!Screen_IsSpecialFrom(fromBitmap))
	{
		if(Renderer_ResolveBitmap(renderer, fromBitmap) == NULL)
			return 2;
		data->fromSerial = Renderer_BitmapSerial(renderer, fromBitmap);
	}
	data->bitmapSerial = Renderer_BitmapSerial(renderer, bitmap);
	data->x = x;
	data->y = y;
	data->bitmap = bitmap;
	data->fromX = fromX;
	data->fromY = fromY;
	data->fromBitmap = fromBitmap;

	// 0x0041D510: the rule must be an 8-bit (mode 3) image.
	if(rule == -1)
		data->rule = -1;
	else
	{
		Bitmap_t* r = Renderer_ResolveBitmap(renderer, rule);
		if(r == NULL)
			return 3;
		if(r->mode != BITMAP_MODE_8)
			return 4;
		data->rule = rule;
		data->ruleParameter = ruleParameter;
		data->ruleSerial = Renderer_BitmapSerial(renderer, rule);
	}

	// vtable+0x48 with the level: the base's, which writes +0xAC.
	Object_ApplyEffectLevel(screen, level);
	return 0;
}

// 0x0041D660, class 4's vtable+0x84. 1 when anything was drawn.
static int Screen_DrawTransition(Renderer_t* renderer, DisplayObject_t* object, ScreenData_t* data,
                                 Bitmap_t* target, const Rect_t* rect)
{
	Bitmap_t* image = Renderer_ResolveBitmap(renderer, data->bitmap);
	if(image == NULL || Renderer_BitmapSerial(renderer, data->bitmap) != data->bitmapSerial)
		return 0;
	uint32_t weight = Object_Weight(object);   // 0x0041B840
	int32_t ax = -(data->x + rect->left), ay = -(data->y + rect->top);

	if(!Screen_IsSpecialFrom(data->fromBitmap))
	{
		Bitmap_t* from = Renderer_ResolveBitmap(renderer, data->fromBitmap);
		if(from == NULL || Renderer_BitmapSerial(renderer, data->fromBitmap) != data->fromSerial)
			return 0;
		if(data->rule != -1)
		{
			printf("[Screen]: Warning: a rule transition (0x00411A60) is not written yet\n");
			return 0;
		}
		// The previous image copied, then the new one over it with the weight
		// (mode 1: the alpha blend with a transparency).
		Renderer_BlitAt(target, -(data->fromX + rect->left), -(data->fromY + rect->top), from, BITMAP_BLEND_COPY, 0);
		Renderer_BlitAt(target, ax, ay, image, BITMAP_BLEND_ALPHA_TRANS, (int)weight);
		return 1;
	}

	// A colour instead of a previous image: 0x7000 black (mode 0xC0), 0x7001 white
	// (0xC1), anything else a plain copy on black.
	uint32_t colour = 0;
	int mode = BITMAP_BLEND_COPY;
	if(data->fromBitmap == 0x7000) { colour = 0; mode = BITMAP_BLEND_FADE_BLACK2; }
	else if(data->fromBitmap == 0x7001) { colour = 0xFFFFFF; mode = BITMAP_BLEND_FADE_WHITE; }

	// For a colour, whether the image leaves part of the screen uncovered from its
	// offset (0x0041D871); "none" (0x7FFF, -1) never asks and counts as covered.
	int uncovered = 0;
	if(data->fromBitmap == 0x7000 || data->fromBitmap == 0x7001)
	{
		Bitmap_t* back = Renderer_BackBuffer(renderer);
		uncovered = 1;
		if(back != NULL && data->x >= 0 && data->y >= 0
		   && image->width - data->x >= back->width && image->height - data->y >= back->height
		   && data->rule == -1)
			uncovered = 0;
	}
	if(data->rule != -1)
	{
		printf("[Screen]: Warning: a rule transition from a colour (0x00411A60) is not written yet\n");
		return 0;
	}
	// Uncovered: the colour first, then the image in the colour's mode. Covered:
	// any weight at all darkens (0xC0), whatever the colour was - 0x0041D994 sets
	// it unconditionally.
	if(uncovered)
		Renderer_FillSolid(target, colour);
	else if(weight > 0)
		mode = BITMAP_BLEND_FADE_BLACK2;
	Renderer_BlitAt(target, ax, ay, image, mode, (int)weight);
	return 1;
}

// ----------------------------------------------------------------------------------
// Class 12
// ----------------------------------------------------------------------------------
uint32_t Screen_SetLayered(int32_t x, int32_t y, int32_t bitmap,
                           int32_t anchorX, int32_t anchorY, uint32_t angle,
                           uint32_t scaleX, uint32_t scaleY, uint32_t flag)
{
	Renderer_t* renderer = gEngine->renderer;
	Screen_Replace(12);
	DisplayObject_t* screen = Object_Resolve(OBJECT_HANDLE_SCREEN);
	ScreenData_t* data = Screen_Data(screen);
	ScreenLayer_t* layer = &data->layers[0];

	// 0x0041DE90 for layer 0.
	if(bitmap == -1)
		layer->set = 0;
	else
	{
		if(Renderer_ResolveBitmap(renderer, bitmap) == NULL)
			return 3;
		layer->set = 1;
		layer->bitmap = bitmap;
		layer->serial = Renderer_BitmapSerial(renderer, bitmap);
		layer->anchorX = anchorX;
		layer->anchorY = anchorY;
		layer->f17C = 0;
		layer->f180 = 0;
	}
	// 0x0041DF50: both scales above zero.
	if(scaleX == 0 || scaleY == 0)
		return 4;
	layer->angle = angle;
	layer->scaleX = scaleX;
	layer->scaleY = scaleY;
	layer->f184 = layer->f188 = layer->f18C = 0;
	layer->f190 = flag;
	// 0x0041DCF0(1), 0x0041E080(0), then the screen's vtable+0x2C.
	layer->shown = 1;
	data->current = 0;
	Screen_SetPosition(screen, x, y);
	return 0;
}

static int Screen_DrawLayered(Renderer_t* renderer, DisplayObject_t* object, ScreenData_t* data,
                              Bitmap_t* target, const Rect_t* rect)
{
	int drawn = 0;
	// 0x0041E284: the drawing device's size (0x0041C170), which the plain case is
	// measured against.
	Bitmap_t* device = Renderer_BackBuffer(renderer);
	for(int i = 0; i < 8; i++)
	{
		ScreenLayer_t* layer = &data->layers[i];
		if(layer->set && layer->shown)
		{
			Bitmap_t* image = Renderer_ResolveBitmap(renderer, layer->bitmap);
			if(image != NULL && Renderer_BitmapSerial(renderer, layer->bitmap) == layer->serial)
			{
				// 0x0041E2F2: the second effect parameter (whole, 0x0041B820 in mode 1)
				// is how far the layer has gone from its settings to their deltas:
				// the anchor moves by f17C / f180 times it (>> 24), the angle by f184
				// along curve f174, and the scales along curve f178 - interpolated in
				// reciprocal space, from 1 / scale to 1 / (scale + f188 / f18C).
				uint32_t e2 = object->fieldB8;
				int32_t pointX = layer->x - (rect->left << 16);
				int32_t pointY = layer->y - (rect->top << 16);
				int32_t anchorX = layer->anchorX + (int32_t)((int64_t)layer->f17C * (int64_t)(int32_t)e2 >> 24);
				int32_t anchorY = layer->anchorY + (int32_t)((int64_t)layer->f180 * (int64_t)(int32_t)e2 >> 24);
				int32_t angle = (int32_t)(((int64_t)Process_Ease(layer->f174, (int32_t)e2) * (int32_t)layer->f184) >> 16) + (int32_t)layer->angle;
				int32_t t2 = Process_Ease(layer->f178, (int32_t)e2);
				double unit = 65536.0;
				double inverseX = unit / (double)layer->scaleX;
				double inverseY = unit / (double)layer->scaleY;
				double toX = unit / (double)(uint32_t)(layer->scaleX + layer->f188);
				double toY = unit / (double)(uint32_t)(layer->scaleY + layer->f18C);
				uint32_t scaleX = (uint32_t)(int32_t)(unit / (inverseX + (toX - inverseX) * (double)t2 * (1.0 / 65536.0)));
				uint32_t scaleY = (uint32_t)(int32_t)(unit / (inverseY + (toY - inverseY) * (double)t2 * (1.0 / 65536.0)));
				if(i == 0)
				{
					// 0x0041E408: the plain case - the image covering the whole device
					// from a whole-pixel place at or before its corner, nothing drawn
					// yet, both scales 1:1 - is a clear and a darkening blit by the
					// layer's level (mode 5). The angle is not part of the test.
					int32_t sx = layer->x - anchorX;
					int32_t sy = layer->y - anchorY;
					int plain = device != NULL
					         && image->width >= device->width && image->height >= device->height
					         && (sx & 0xFFFF) == 0 && (sy & 0xFFFF) == 0 && sx <= 0 && sy <= 0
					         && image->width + (sx >> 16) >= device->width
					         && image->height + (sy >> 16) >= device->height
					         && !drawn && scaleX == 0x10000 && scaleY == 0x10000;
					if(plain)
					{
						Renderer_ClearBitmap(target);
						Renderer_BlitAt(target, (sx >> 16) - rect->left, (sy >> 16) - rect->top, image,
						                BITMAP_BLEND_FADE_BLACK, (int)layer->level);
					}
					else
						Xform_DrawCopy(target, pointX, pointY, image, anchorX, anchorY, angle, scaleX, scaleY,
						               layer->level, (int)layer->f190, 1);
				}
				else if(layer->f150 == 0 || layer->f150 == 1 || layer->f150 == 0x20)
				{
					// 0x0041E5AA: the layer's blend mode is an alpha one - blended
					// straight on, the level as the transparency.
					Xform_DrawBlend(target, pointX, pointY, image, anchorX, anchorY, angle, scaleX, scaleY,
					                layer->level, (int)layer->f190, 1);
				}
				else
				{
					// 0x0041E522: any other mode - drawn into a buffer the size of the
					// part being drawn (0x00409030), then blended onto it with the
					// layer's mode and level (0x0040A9E0).
					Bitmap_t temp = *target;
					temp.stride = target->width * Renderer_ModePixelBytes(target->mode);
					temp.bitmap = (uint8_t*)calloc(1, (size_t)temp.stride * (size_t)target->height);
					if(temp.bitmap != NULL)
					{
						Xform_DrawCopy(&temp, pointX, pointY, image, anchorX, anchorY, angle, scaleX, scaleY,
						               0, (int)layer->f190, 1);
						Renderer_BlitView(target, &temp, (int)layer->f150, (int)layer->level);
						free(temp.bitmap);
					}
				}
				drawn = 1;
			}
		}
		if(i == 0 && !drawn)
		{
			// Layer 0 drew nothing: the view is cleared (0x0041E5F7).
			Renderer_ClearBitmap(target);
			drawn = 1;
		}
	}
	return drawn;
}

// ----------------------------------------------------------------------------------
// Virtuals
// ----------------------------------------------------------------------------------
int Screen_SetEffectLevel(DisplayObject_t* object, uint32_t level)
{
	if(object->kind != 12)
		return 0;
	// 0x0041E100 -> 0x0041DE30: the current layer's own level.
	ScreenData_t* data = Screen_Data(object);
	if(data->current < 8)
		data->layers[data->current].level = level;
	return 1;
}

int Screen_SetPosition(DisplayObject_t* object, int32_t x, int32_t y)
{
	ScreenData_t* data = Screen_Data(object);
	if(object->kind == 4)
	{
		// 0x0041D3E0.
		data->x = x;
		data->y = y;
		return 1;
	}
	if(object->kind == 12)
	{
		// 0x0041E0A0 -> 0x0041B540 -> vtable+0x3C (0x0041E0D0): the base's own
		// position (0x0041B440), then the current layer's (0x0041DD50), which is
		// rounded to whole pixels when the object snaps (0x0041BFA0 - it does not
		// here: nothing sets +0x80).
		object->x = x;
		object->y = y;
		if(data->current < 8)
		{
			data->layers[data->current].x = x;
			data->layers[data->current].y = y;
		}
		return 1;
	}
	return 0;
}

int Screen_GetPosition(DisplayObject_t* object, int32_t* x, int32_t* y)
{
	ScreenData_t* data = Screen_Data(object);
	if(object->kind == 4)
	{
		// 0x0041D400.
		*x = data->x;
		*y = data->y;
		return 1;
	}
	if(object->kind == 12)
	{
		// 0x0041E0B0 -> 0x0041DDC0: the current layer's; with no current layer the
		// original leaves the caller's pair as it was (0x80000001).
		if(data->current < 8)
		{
			*x = data->layers[data->current].x;
			*y = data->layers[data->current].y;
		}
		return 1;
	}
	return 0;
}

int Screen_GetEffectLevel(DisplayObject_t* object, uint32_t* level)
{
	if(object->kind != 12)
		return 0;
	// 0x0041E110 -> 0x0041DE60: the current layer's own level. With no current layer
	// the original answers an uninitialised local; this answers 0.
	ScreenData_t* data = Screen_Data(object);
	*level = data->current < 8 ? data->layers[data->current].level : 0;
	return 1;
}

int Screen_SetParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, uint32_t* result)
{
	if(object->kind == 4 && number == 0x40000000u)
	{
		// 0x0041D5A0 -> 0x0041D5E0.
		ScreenData_t* data = Screen_Data(object);
		int ok = 0;
		if(data->param168 == 0 || value2 < 4)
		{
			data->param168 = value1;
			data->param16C = value2;
			ok = 1;
		}
		*result = ok ? 0 : 0xFFFF0002u;
		return 1;
	}
	return 0;
}

void Screen_Draw(Renderer_t* renderer, DisplayObject_t* object, Bitmap_t* target, const Rect_t* rect)
{
	// 0x0041C410: the class's own draw when the content flag is set, and the view
	// cleared when that drew nothing or the flag is clear.
	int drawn = 0;
	if(object->screenContent)
	{
		ScreenData_t* data = Screen_Data(object);
		if(object->kind == 4)
			drawn = Screen_DrawTransition(renderer, object, data, target, rect);
		else if(object->kind == 12)
			drawn = Screen_DrawLayered(renderer, object, data, target, rect);
	}
	if(!drawn)
		Renderer_ClearBitmap(target);
}
