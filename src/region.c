//
// The two priority-keyed region lists at 0x005667F0 and 0x0056680C.
//

#include <stdio.h>
#include <stdlib.h>
#include "region.h"

static Region_t* gRegionLists[REGION_LIST_COUNT] = { NULL, NULL };

// The region the input pass is currently holding (0x00566824). Nothing writes it
// yet; Region_Recompute is the only reader and says so.
static Region_t* gHeldRegion = NULL;

Region_t* Region_First(int list)
{
	return gRegionLists[list];
}

void Region_Add(int list, uint32_t key, int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t owner)
{
	Region_t* region = (Region_t*)malloc(sizeof(Region_t));
	if(region == NULL)
	{
		printf("[Region]: Out of memory\n");
		exit(1);
	}
	region->key = key;
	region->left = left;
	region->top = top;
	region->right = right;
	region->bottom = bottom;
	region->owner = owner;

	// Descending key order: in before the first region whose key is no greater.
	Region_t* previous = NULL;
	Region_t* next = gRegionLists[list];
	while(next != NULL && next->key > key)
	{
		previous = next;
		next = next->next;
	}
	region->next = next;
	if(previous != NULL)
		previous->next = region;
	else
		gRegionLists[list] = region;
}

int Region_RemoveByKey(int list, uint32_t key)
{
	Region_t* previous = NULL;
	for(Region_t* region = gRegionLists[list]; region != NULL; region = region->next)
	{
		if(region->key != key)
		{
			previous = region;
			continue;
		}
		if(previous != NULL)
			previous->next = region->next;
		else
			gRegionLists[list] = region->next;
		if(gHeldRegion == region)
			gHeldRegion = NULL;
		free(region);
		return 1;
	}
	return 0;
}

const char* Region_Recompute(uint32_t key)
{
	// 0x0046D990: nothing to do without a held region, and nothing to do for a key
	// below the held region's own.
	if(gHeldRegion == NULL || key < gHeldRegion->key)
		return NULL;
	return "what is under the pointer (0x0046DE00 / 0x0046DFB0)";
}

void Region_FreeAll(void)
{
	for(int list = 0; list < REGION_LIST_COUNT; list++)
	{
		Region_t* region = gRegionLists[list];
		while(region != NULL)
		{
			Region_t* next = region->next;
			free(region);
			region = next;
		}
		gRegionLists[list] = NULL;
	}
	gHeldRegion = NULL;
}
