#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <stdint.h>

#include "renderer.h"

// Display objects. The original keeps a table per kind on the display root at
// 0x0056674C and tells the kinds apart by the top byte of the handle; the index is
// the rest of it. Its resolver (0x00443350) walks all ten in this order:
//
//   tag   slots  table       resolver    what it is
//   0x80  0x200  root+0x05C  0x0043E6B0  a sprite      (built here)
//   0x90  0x008  root+0x864  0x0043F130
//   0x91  0x008  root+0x88C  0x0043F430
//   0xA0  0x008  root+0x8B4  0x0043F990
//   0xA1  0x004  root+0x8DC  0x0043FE10
//   0xB0  0x010  root+0x8F4  0x004407A0  a window   (built here; the renderer
//                                        holds its pixels, 0x0042AF00, type 3)
//   0xC0  0x008  root+0x93C  0x00441320
//   0xC1  0x008  root+0x964  0x00441AA0
//   0xF0  0x020  root+0x98C  0x00442300
//   0xF1  0x008  root+0xA14  0x004426F0  a group       (built here)
//
// Every kind is the same base class (0x0041A4D0), which is why the enable, visible
// and parameter opcodes take any of them; what differs is the class the constructor
// puts on top, and the type it passes the base: 2 for a sprite, 3 for a window,
// 9 for a group.
#define OBJECT_TAG_MASK     0xFF000000u
#define OBJECT_INDEX_MASK   0x00FFFFFFu

#define OBJECT_TAG_SPRITE   0x80000000u
#define OBJECT_TAG_WINDOW   0xB0000000u
#define OBJECT_TAG_GROUP    0xF1000000u
#define SPRITE_SLOT_COUNT   0x200
#define WINDOW_SLOT_COUNT   0x010
#define GROUP_SLOT_COUNT    0x008
// The screen object, which the display root builds at root+0x50 (0x0041E960, vtable
// 0x004E4854) and immediately puts at the head of its display list through
// 0x004307D0. It is a plain display object with the type 1 and no class of its own,
// and it is what the handle 0 names: 0x00443350 answers 0 with root+0x50 before it
// looks at any table.
#define OBJECT_HANDLE_SCREEN 0u
#define OBJECT_TYPE_SCREEN  1
#define OBJECT_TYPE_SPRITE  2
#define OBJECT_TYPE_WINDOW  3
#define OBJECT_TYPE_GROUP   9
// CDspObjVirtual (0x0042AD30, vtable 0x004E4FA4): a display object with no class of
// its own that another object owns. It is the one type 0x0041AC10 will re-parent
// even though it already has an owner.
#define OBJECT_TYPE_VIRTUAL 8

typedef struct DisplayObject DisplayObject_t;
// A child of a group, as 0x0041AC10 allocates it: SIXTEEN bytes, because a child
// carries its own offset inside the parent and not only a link. The offsets are what
// 0x0041B2A0 adds to the parent's base position on its way down, so two windows in
// one group can sit in different places.
typedef struct ObjectChild ObjectChild_t;
struct ObjectChild
{
	DisplayObject_t* child;   // +0x00
	int32_t          x;       // +0x04
	int32_t          y;       // +0x08
	ObjectChild_t*   next;    // +0x0C
};
struct DisplayObject
{
	// The handle this object was handed out under, which is how the parts of the
	// object that live outside it - a window's pixels, held by the renderer - are
	// found again from the object itself. The original needs no such thing: its
	// window object owns its pixels.
	uint32_t  handle;
	uint32_t  serial;             // +0x20, the value of the counter before this one
	uint32_t  type;               // +0x18, 2 for a sprite
	// +0x1C, the object's own place in the draw order, which vtable+0x54
	// (0x0041B980) writes and refuses at 0x10000 or above. The base constructor
	// leaves it 0.
	uint32_t  layer;
	// +0x24, added to the key after the layer and the type have been shifted up, so
	// it orders objects that share both. 0x0041BFE0 writes it; the base constructor
	// leaves it 0.
	uint32_t  orderBase;
	// +0x7C. When it is set the object's own serial is its depth in the key;
	// otherwise the depth comes from the third component of the accumulated
	// position vector (0x0041B590), which is 0 in a scene with no Z - every scene
	// this engine has seen - and leaves the depth at 0xFFF.
	uint32_t  depthFromSerial;
	int       enabled;            // +0x04
	int       propagateEnabled;   // +0x08
	int       hidden;      // +0x0C
	int       propagateHidden; // +0x10
	int       visible;            // +0x14
	// +0x30 and +0x34, the position the object's OWNER gives it: vtable+0x28
	// (0x0041B2A0) writes it as the parent's own base plus the offset the child's
	// node carries, and vtable+0x30 (0x0041B310) reads it back. An object with no
	// owner keeps it at zero, which is why nothing needed it until groups did.
	int32_t   baseX;
	int32_t   baseY;
	// +0x38 and +0x3C, the object's position, which vtable+0x38 (0x0041B3A0) writes
	// and then writes down every child through the child's own virtual.
	int32_t   x;
	int32_t   y;
	uint32_t  priority;           // +0x48
	// +0xA8, the blend mode the object is drawn with: 0x0041B6E0 reads it back and
	// hands it straight to the blit. The base constructor leaves it 0x80, a copy.
	uint32_t  unknownA8;          // +0xA8
	uint32_t  unknownAC;          // +0xAC
	// +0xB0, 0 opaque and 0x100 completely transparent: Grp0 0x34 sets it and
	// "would this be drawn" refuses the object once it reaches 0x100.
	uint32_t  transparency;
	uint32_t  opacity;            // +0xB4
	// +0xBC, which the parameter 0x7FFF0000 writes (0x0041C290) and 0x0041C2A0 reads
	// back. What consumes it is not read yet; scrdrv sets it on the screen object.
	uint32_t  fieldBC;
	// +0x138, the second bitmap a sprite can be given. The sprite constructor
	// (0x00425790) leaves it 0, and nothing in this engine sets it; the arms of the
	// sprite's draw that read it (0x00425BB8 and the masked path at 0x00425ADC) are
	// therefore not written, and say so by name when they are reached.
	int32_t   maskBitmapId;
	// A sprite's own two, which its vtable+0x48 dispatches on. The sprite
	// constructor (0x00425790) leaves the content kind at -1 and the kind at 0,
	// and nothing here can give a sprite content yet, so they stay that way -
	// but the dispatch is written out, so the day content exists it is already
	// the original's dispatch and not a special case.
	int32_t   contentKind;        // +0x244
	uint32_t  kind;               // +0x134
	// A sprite's content, as the kind 0 arm (0x004274E0) leaves it: the bitmap
	// itself and the serial that bitmap slot had when it was handed over, so a slot
	// refilled behind the sprite's back can be told apart from the image it was
	// given. The constructor (0x00425790) leaves +0x150 at -1.
	int32_t   bitmapId;           // +0x150
	uint32_t  bitmapSerial;       // +0x158
	// The object's own surface, which vtable+0x74 (0x0041C090) sizes from whatever
	// content it was just given, in the screen's pixel mode. The original leaves the
	// pixels at +0x90 NULL until something draws into them, and so does this.
	uint8_t*  surfacePixels;      // +0x90
	int       surfaceStride;      // +0x94
	int       surfaceWidth;       // +0x98
	int       surfaceHeight;      // +0x9C
	int       surfaceMode;        // +0xA0
	int       surfacePixelBytes;  // +0xA4
	// +0x11C, the group this object belongs to. 0x0041ADB0 reads it and 0x0041ADA0
	// writes it, and 0x0041AC10 refuses to attach an object that already has one
	// unless its type is 8 - a kind this engine does not build.
	DisplayObject_t* owner;
	// +0x12C, the head of the child list. The nodes are the group's, not the
	// children's: an object is in at most one list, so it has an owner and not a
	// sibling link.
	ObjectChild_t*   children;
	// ------------------------------------------------------------------------
	// A window's own contents (window+0x310 to +0x34C, and the layer order at
	// +0x3C0). Only a window has them; every other kind leaves them empty.
	//
	// A window is not one image. Its pixels are composed out of three layers
	// (0x0042CB10) in the order the three dwords at +0x3C0 give, which
	// 0x0042C950 writes as one of six permutations and the constructor asks for
	// as 0: the background image, the frame, and a display list of the window's
	// own. The last is what an icon fills: 0x0042BFB0 puts a SPRITE into a
	// numbered slot of +0x314 and files it in the list at +0x340, and the
	// redraw's third layer draws that list into the window's own pixels.
	// ------------------------------------------------------------------------
	DisplayObject_t** contentSlots;      // +0x314
	uint32_t          contentSlotCount;  // +0x310
	struct ObjectList* contentList;      // +0x340
	// +0x344, +0x348 and +0x34C, which 0x0042BCB0 sets from the display mode as
	// -(width/2), -(height/2) and width/2, and 0x0042BEC0 recomputes when the
	// mode changes: the content list's coordinates are measured from the middle
	// of the screen, not from the window's own corner.
	int32_t           contentOriginX;
	int32_t           contentOriginY;
	int32_t           contentHalfWidth;
	// +0x3C0, +0x3C4, +0x3C8: which layer is drawn first, second and third.
	uint32_t          layerOrder[3];
	// Which display list this object is filed in, or NULL. The original does not
	// keep it: every one of its removals already has the list in hand, because it
	// removes through the owner that put the object there (0x0042BCFF hands the
	// window's list to 0x00430850). This engine's Object_Destroy does not, so the
	// object remembers where it went in.
	struct ObjectList* list;
};

// ----------------------------------------------------------------------------------
// A display list (0x00430650, 0x70 bytes)
//
// There is more than one. The display root owns the one the frame is composed from
// (root+0x14, built by 0x00442930), and every window that is given content owns a
// second (window+0x340, built by 0x0042BE38 through the same constructor), whose
// objects are drawn into that window's own pixels rather than into the screen. The
// nodes, the ascending order, the walk and the draw priority are the list's, not the
// engine's, which is why they live here rather than in a file-static head.
// ----------------------------------------------------------------------------------
typedef struct ObjectNode ObjectNode_t;
typedef struct ObjectList
{
	ObjectNode_t* head;
	// +0x48, the priority the screen object draws at, held as the original holds
	// it: shifted up sixteen places, so the low half orders what shares a
	// priority. Grp0 0x09 sets the root list's through 0x00462020.
	uint32_t      priority;
} ObjectList_t;
#define SPRITE_MAX_PRIORITY 0x10000
// root+0x14: the list the frame is composed from.
extern ObjectList_t gRootList;

extern uint32_t gObjectDamage;

// ----------------------------------------------------------------------------------
// Display object parameters: the virtual at vtable+0x5C, which Grp0 0x38 calls.
//
// A sprite answers it at 0x00428670 and hands everything it does not know to the
// display-object base at 0x0041B9B0. The three codes below are the base's own; the
// caller (0x00443990) turns them into the opcode's result.
// ----------------------------------------------------------------------------------
#define OBJECT_PARAM_OK            0x00000000u
#define OBJECT_PARAM_UNSUPPORTED   0xFFFF0001u
#define OBJECT_PARAM_BAD_ARGUMENT  0xFFFF0002u
// Not one of the original's codes. It means a parameter the original implements and
// this engine has not read out of the binary yet, so it is refused by name rather
// than guessed at; `*unread` then names it.
#define OBJECT_PARAM_UNREAD        0xFFFF00FFu

uint32_t Object_SetParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// The worker behind Grp0 0x38 (0x00443990), the parameter write with its own
// bracket. Its results are the original's: 0 done, 5 unsupported number, 0xFE wrong
// arguments, and 0xFD for a number this engine has not read yet (`*unread` names
// it). The opcode answers 0xFF for a handle that did not resolve at all.
#define OBJECT_SET_OK            0x00u
#define OBJECT_SET_UNSUPPORTED   0x05u
#define OBJECT_SET_BAD_ARGUMENT  0xFEu
#define OBJECT_SET_BAD_HANDLE    0xFFu
#define OBJECT_SET_UNREAD        0xFDu
uint32_t Object_ApplyParameter(DisplayObject_t* object, uint32_t number, uint32_t value1, uint32_t value2, const char** unread);

// A handle of the given kind, or 0 when every slot of it is taken (which the
// opcodes treat as fatal, as the original does).
uint32_t Object_Create(uint32_t tag);
// NULL for anything that is not a live object handle: a tag byte of no kind that is
// built, an index past that kind's table, or a slot that has been freed.
DisplayObject_t* Object_Resolve(uint32_t handle);
// Free the object a handle names and give its slot back, so the next object of
// that kind takes it - which is what the original does and a counter cannot.
void Object_Destroy(uint32_t handle);
// The same, but only if the object is of that type.
DisplayObject_t* Object_ResolveKind(uint32_t handle, uint32_t type);
int  Object_IsDrawable(const DisplayObject_t* object);
// vtable+0x28 (0x0041B2A0): the position an owner gives the object. It writes
// +0x30/+0x34, optionally tells the owner that the object moved inside it, and
// optionally passes the sum down every child with that child's own offset added.
void Object_SetBasePosition(DisplayObject_t* object, int32_t x, int32_t y,
                            int notifyOwner, int walkChildren);
// 0x0041AC10, the parenting itself. The results are the original's, as 0x00442860
// and 0x00463710 pass them on to the opcode.
#define OBJECT_GROUP_OK          0x00u
#define OBJECT_GROUP_BAD_OBJECT  0x01u
#define OBJECT_GROUP_SELF        0x03u
#define OBJECT_GROUP_HAS_OWNER   0x04u
#define OBJECT_GROUP_BAD_GROUP   0xFFu
// 0x00442860: resolve the group, resolve the object, refuse the object that is the
// group itself, and attach it at the offset given.
uint32_t Object_AddToGroup(uint32_t groupHandle, uint32_t objectHandle, int32_t x, int32_t y);
// 0x0041AC10 itself, for the parts of the engine that parent an object they own
// rather than one a script named.
uint32_t Object_Attach(DisplayObject_t* parent, DisplayObject_t* child, int32_t x, int32_t y);
// vtable+0x38 (0x0041B3A0) without the damage bracket Grp0 0x37 puts around it,
// which is how the icon's constructor copies a window's position onto the display
// object it has just made.
void Object_SetPosition(DisplayObject_t* object, int32_t x, int32_t y);
// A CDspObjVirtual (0x0042AD30): the plain display object of type 8 that another
// object owns. It is in no handle table and in no display list - it is a place in
// the tree, not something a script can name or the walk can draw - and its owner is
// set before it is attached, which is exactly the case 0x0041AC10 lets through.
DisplayObject_t* Object_CreateVirtual(DisplayObject_t* owner);
// The flag and the walk down the children, as the original's setters do it.
void Object_SetVisible(DisplayObject_t* object, int visible);
void Object_SetEnabled(DisplayObject_t* object, int enabled);
void Object_SetHidden(DisplayObject_t* object, int hidden);
// The same, bracketed the way the opcodes' workers bracket them: would it be drawn,
// change it, would it be drawn now, and dirty the screen only when that answer moved.
void Object_ApplyVisible(DisplayObject_t* object, int visible);
void Object_ApplyEnabled(DisplayObject_t* object, int enabled);
void Object_ApplyHidden(DisplayObject_t* object, int hidden);

// The effect level (Grp0 0x32, the virtual at vtable+0x48). The original refuses
// anything above 0x100 before it even looks at the handle, in 0x00497F40.
#define OBJECT_EFFECT_LEVEL_MAX 0x100u
// The worker behind the opcode (0x00443540). NULL when the level was set;
// otherwise the name of the arm of the sprite's override this engine has not
// read yet, so it is refused by name rather than guessed at.
const char* Object_ApplyEffectLevel(DisplayObject_t* object, uint32_t level);
// The transparency (Grp0 0x34, 0x00443690), which takes the same range.
void Object_ApplyTransparency(DisplayObject_t* object, uint32_t transparency);
// The position (Grp0 0x37, the virtual at vtable+0x38), bracketed as 0x004437B0
// brackets it.
void Object_ApplyPosition(DisplayObject_t* object, int32_t x, int32_t y);
// ----------------------------------------------------------------------------------
// Sprite content: the virtual-free function 0x004273C0, which is both the sprite
// parameter 0x10 and the worker behind Grp0 0x57. It dispatches on the sprite's kind
// (+0x134) through the jump table at 0x004274BC, seven arms wide.
// ----------------------------------------------------------------------------------
// The original's own two results, and one of this engine's.
#define OBJECT_CONTENT_OK           0x00000000u
// 0x80000001: there is no such bitmap. The opcode turns it into result 1.
#define OBJECT_CONTENT_BAD_BITMAP   0x80000001u
// Not the original's: a kind whose arm this engine has not read out of the binary
// yet, refused by name in `*unread` rather than guessed at.
#define OBJECT_CONTENT_UNREAD       0x80FF0000u
uint32_t Object_SetContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread);
// The same, bracketed as 0x0043ED80 brackets it. 0xFF for a handle that is not a
// live sprite, which the opcode treats as fatal.
#define OBJECT_CONTENT_BAD_SPRITE   0x000000FFu
uint32_t Object_ApplyContentBitmap(Renderer_t* renderer, DisplayObject_t* sprite, int number, const char** unread);

// ----------------------------------------------------------------------------------
// The display list: every object that can be drawn, in the order it is drawn.
//
// The original keeps it on the display root at root+0x14 and builds it with
// 0x00442930; the screen object goes in first (0x004429F9) and every other kind puts
// itself in as it is created. A node is twelve bytes - key, object, next - and
// 0x004307D0 inserts before the first node whose key is strictly greater, so the list
// is in ascending key order and objects that share a key keep the order they arrived
// in. 0x00430850 takes one out again and 0x004433E0 re-sorts one that has moved, by
// taking it out and putting it back.
// ----------------------------------------------------------------------------------
// 0x0041B190, the key an object is filed under:
//   ((layer * 8 + min(type, 7)) << 13) + orderBase + (depth & 0x1FFF)
// vtable+0x74 (0x0041C090): the size of the object's own surface, which is what its
// bounds are measured from and therefore what decides whether any of it is drawn.
// A sprite is sized by the content it is given; a window by its pixels.
int Object_SetSurfaceSize(Renderer_t* renderer, DisplayObject_t* object, int width, int height);
uint32_t Object_DrawKey(const DisplayObject_t* object);
// 0x00565B44 and 0x00565B48, which 0x00440650 writes together: whether windows are
// drawn at all, and a transparency laid over every one of them. The display root's
// own constructor calls it with both zero, so windows start invisible.
extern uint32_t gWindowsVisible;
extern uint32_t gWindowTransparency;
// 0x004307D0 and 0x00430850 on a named list, which is how a window files its own
// contents; the three below are the same on the root's.
void Object_ListInsertInto(ObjectList_t* list, DisplayObject_t* object);
void Object_ListRemoveFrom(ObjectList_t* list, DisplayObject_t* object);
void Object_ListInsert(DisplayObject_t* object);
// Out of whatever list it is in, which the object itself remembers.
void Object_ListRemove(DisplayObject_t* object);
// Out and back in, which is the only way a key that has moved reaches its new place.
void Object_ListResort(DisplayObject_t* object);
// 0x00431630: the walk that reads the list, drawing every object that is drawable
// and reaches `clip` into `target`. `target` is the surface the frame is composed
// into and `clip` is the part of it being redrawn.
void Object_DrawListOf(ObjectList_t* list, Renderer_t* renderer, Bitmap_t* target, const Rect_t* clip);
// The same on the root's list: the frame.
void Object_DrawList(Renderer_t* renderer, Bitmap_t* target, const Rect_t* clip);
// Every node out of a list, without touching the objects themselves - a window's
// list owns no object in it, and neither does the root's.
void Object_ListClear(ObjectList_t* list);

// The three layers a window's pixels are composed of, which are what the dwords at
// window+0x3C0 order.
#define WINDOW_LAYER_BACKGROUND 0
#define WINDOW_LAYER_FRAME      1
#define WINDOW_LAYER_CONTENT    2

void Object_FreeAll(void);

#endif // __OBJECT_H__
