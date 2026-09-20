//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

// Altered source version of recastnavigation RecastLayers, modified for
// Dagor Engine; not the original library (github.com/recastnavigation).
//
// Most important changed and added parts marked with 'dagor' word in text.

#include <float.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "Recast.h"
#include "RecastAlloc.h"
#include "RecastAssert.h"


// Must be 255 or smaller (not 256) because layer IDs are stored as
// a byte where 255 is a special value.
#ifndef RC_MAX_LAYERS_DEF
#define RC_MAX_LAYERS_DEF 240
#endif

#if RC_MAX_LAYERS_DEF > 255
#error RC_MAX_LAYERS_DEF must be 255 or smaller
#endif

#ifndef RC_MAX_NEIS_DEF
#define RC_MAX_NEIS_DEF 16
#endif

// Keep type checking.
static const int RC_MAX_LAYERS = RC_MAX_LAYERS_DEF;
static const int RC_MAX_NEIS = RC_MAX_NEIS_DEF;

// dagor: More regions using Layers List Blocks (LLB)
static const int RC_REG_MAXN = 4096;
static const int RC_LLIST_BLKCNT = 15;
static const int RC_LLIST_BLKMAX = 8192;
static const int RC_LLIST_BLKEND = 0xFFFF;
struct rcLayersListBlock
{
	unsigned short layers[RC_LLIST_BLKCNT];
	unsigned short next;
};
static bool checkAllocLLBs(rcScopedDelete<rcLayersListBlock> &llbs, int llbn)
{
	if (!llbs)
		llbs.Inject((rcLayersListBlock*)rcAlloc(sizeof(rcLayersListBlock)*llbn, RC_ALLOC_TEMP));
	return llbs != nullptr;
}

struct rcLayerRegion
{
	unsigned short layers_llb; // dagor
	unsigned short neis[RC_MAX_NEIS];
	unsigned short ymin, ymax;
	unsigned char last_llb; // dagor
	unsigned char layerId;		// Layer ID
	unsigned char nneis;		// Neighbour count
	unsigned char base;		// Flag indicating if the region is the base of merged regions.
};


static bool contains(const unsigned short* a, const unsigned char an, const unsigned short v)
{
	const int n = (int)an;
	for (int i = 0; i < n; ++i)
	{
		if (a[i] == v)
			return true;
	}
	return false;
}

static bool addUnique(unsigned short* a, unsigned char& an, int anMax, unsigned short v)
{
	if (contains(a, an, v))
		return true;

	if ((int)an >= anMax)
		return false;

	a[an] = v;
	an++;
	return true;
}

static bool contains(const rcLayersListBlock *llbs, unsigned short llb0, unsigned char last, const unsigned short v)
{
	unsigned short llb = llb0;
	while (llb != RC_LLIST_BLKEND)
	{
		const rcLayersListBlock &list = llbs[llb];
		const int cnt = (llb == llb0) ? last : RC_LLIST_BLKCNT;
		for (int i = 0; i < cnt; ++i)
			if (list.layers[i] == v)
				return true;
		llb = list.next;
	}
	return false;
}

static bool addUnique(rcLayersListBlock *llbs, int llbn, int &llbf, unsigned short &llb0, unsigned char &last, unsigned short v)
{
	if (contains(llbs, llb0, last, v))
		return true;

	if (llb0 != RC_LLIST_BLKEND)
	{
		rcLayersListBlock &list = llbs[llb0];
		if (last < RC_LLIST_BLKCNT)
		{
			list.layers[last++] = v;
			return true;
		}
	}

	if (llbf >= llbn)
		return false;

	rcLayersListBlock &list = llbs[llbf];
	list.layers[0] = v;
	list.next = llb0;
	last = 1;
	llb0 = llbf++;
	return true;
}

static bool addUniques(rcLayersListBlock *llbs, int llbn, int &llbf, unsigned short &llb0, unsigned char &last, unsigned short from_llb, unsigned char from_last)
{
	unsigned short llb = from_llb;
	while (llb != RC_LLIST_BLKEND)
	{
		const rcLayersListBlock &list = llbs[llb];
		const int cnt = (llb == from_llb) ? from_last : RC_LLIST_BLKCNT;
		for (int i = 0; i < cnt; ++i)
			if (!addUnique(llbs, llbn, llbf, llb0, last, list.layers[i]))
				return false;
		llb = list.next;
	}
	return true;
}


inline bool overlapRange(const unsigned short amin, const unsigned short amax,
						 const unsigned short bmin, const unsigned short bmax)
{
	return (amin > bmax || amax < bmin) ? false : true;
}



struct rcLayerSweepSpan
{
	unsigned short ns;	// number samples
	unsigned short id;	// region id
	unsigned short nei;	// neighbour id
};

/// @par
/// 
/// See the #rcConfig documentation for more information on the configuration parameters.
/// 
/// @see rcAllocHeightfieldLayerSet, rcCompactHeightfield, rcHeightfieldLayerSet, rcConfig
bool rcBuildHeightfieldLayers(rcContext* ctx, const rcCompactHeightfield& chf,
							  const int borderSize, const int walkableHeight,
							  rcHeightfieldLayerSet& lset)
{
	rcAssert(ctx);
	
	rcScopedTimer timer(ctx, RC_TIMER_BUILD_LAYERS);
	
	const int w = chf.width;
	const int h = chf.height;
	
	rcScopedDelete<unsigned short> srcReg((unsigned short*)rcAlloc(sizeof(unsigned short)*chf.spanCount, RC_ALLOC_TEMP));
	if (!srcReg)
	{
		ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'srcReg' (%d).", chf.spanCount);
		return false;
	}
	memset(srcReg,0xff,sizeof(unsigned short)*chf.spanCount);
	
	const int nsweeps = chf.width;
	rcScopedDelete<rcLayerSweepSpan> sweeps((rcLayerSweepSpan*)rcAlloc(sizeof(rcLayerSweepSpan)*nsweeps, RC_ALLOC_TEMP));
	if (!sweeps)
	{
		ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'sweeps' (%d).", nsweeps);
		return false;
	}
	
	
	// Partition walkable area into monotone regions.
	int prevCount[RC_REG_MAXN];
	unsigned short regId = 0;

	for (int y = borderSize; y < h-borderSize; ++y)
	{
		memset(prevCount,0,sizeof(int)*regId);
		unsigned char sweepId = 0;
		
		for (int x = borderSize; x < w-borderSize; ++x)
		{
			const rcCompactCell& c = chf.cells[x+y*w];
			
			for (int i = (int)c.index, ni = (int)(c.index+c.count); i < ni; ++i)
			{
				const rcCompactSpan& s = chf.spans[i];
				if (chf.areas[i] == RC_NULL_AREA) continue;

				unsigned short sid = 0xffff;

				// -x
				if (rcGetCon(s, 0) != RC_NOT_CONNECTED)
				{
					const int ax = x + rcGetDirOffsetX(0);
					const int ay = y + rcGetDirOffsetY(0);
					const int ai = (int)chf.cells[ax+ay*w].index + rcGetCon(s, 0);
					if (chf.areas[ai] != RC_NULL_AREA && srcReg[ai] != 0xffff)
						sid = srcReg[ai];
				}
				
				if (sid == 0xffff)
				{
					sid = sweepId++;
					if (sid >= nsweeps)
					{
						ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: sweeps (%d) overflow. Too many layers of surfaces inside navmesh tile?", nsweeps);
						return false;
					}
					sweeps[sid].nei = 0xffff;
					sweeps[sid].ns = 0;
				}
				
				// -y
				if (rcGetCon(s,3) != RC_NOT_CONNECTED)
				{
					const int ax = x + rcGetDirOffsetX(3);
					const int ay = y + rcGetDirOffsetY(3);
					const int ai = (int)chf.cells[ax+ay*w].index + rcGetCon(s, 3);
					const unsigned short nr = srcReg[ai];
					if (nr != 0xffff)
					{
						// Set neighbour when first valid neighbour is encoutered.
						if (sweeps[sid].ns == 0)
							sweeps[sid].nei = nr;
						
						if (sweeps[sid].nei == nr)
						{
							// Update existing neighbour
							sweeps[sid].ns++;
							prevCount[nr]++;
						}
						else
						{
							// This is hit if there is nore than one neighbour.
							// Invalidate the neighbour.
							sweeps[sid].nei = 0xffff;
						}
					}
				}
				
				srcReg[i] = sid;
			}
		}
		
		// Create unique ID.
		for (int i = 0; i < sweepId; ++i)
		{
			// If the neighbour is set and there is only one continuous connection to it,
			// the sweep will be merged with the previous one, else new region is created.
			if (sweeps[i].nei != 0xffff && prevCount[sweeps[i].nei] == (int)sweeps[i].ns)
			{
				sweeps[i].id = sweeps[i].nei;
			}
			else
			{
				if (regId >= RC_REG_MAXN)
				{
					ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Region ID overflow.");
					return false;
				}
				sweeps[i].id = regId++;
			}
		}
		
		// Remap local sweep ids to region ids.
		for (int x = borderSize; x < w-borderSize; ++x)
		{
			const rcCompactCell& c = chf.cells[x+y*w];
			for (int i = (int)c.index, ni = (int)(c.index+c.count); i < ni; ++i)
			{
				if (srcReg[i] != 0xffff)
					srcReg[i] = sweeps[srcReg[i]].id;
			}
		}
	}

	// Allocate layers lists blocks.
	int llbf = 0; // next free block
	const int llbn = RC_LLIST_BLKMAX;
	rcScopedDelete<rcLayersListBlock> llbs;

	// Allocate and init layer regions.
	const int nregs = (int)regId;
	rcScopedDelete<rcLayerRegion> regs((rcLayerRegion*)rcAlloc(sizeof(rcLayerRegion)*nregs, RC_ALLOC_TEMP));
	if (!regs)
	{
		ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'regs' (%d).", nregs);
		return false;
	}
	memset(regs, 0, sizeof(rcLayerRegion)*nregs);
	for (int i = 0; i < nregs; ++i)
	{
		regs[i].layers_llb = RC_LLIST_BLKEND;
		regs[i].layerId = 0xff;
		regs[i].ymin = 0xffff;
		regs[i].ymax = 0;
	}
	
	// Find region neighbours and overlapping regions.
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const rcCompactCell& c = chf.cells[x+y*w];
			
			unsigned short lregs[RC_MAX_LAYERS];
			int nlregs = 0;
			
			for (int i = (int)c.index, ni = (int)(c.index+c.count); i < ni; ++i)
			{
				const rcCompactSpan& s = chf.spans[i];
				const unsigned short ri = srcReg[i];
				if (ri == 0xffff) continue;
				
				regs[ri].ymin = rcMin(regs[ri].ymin, s.y);
				regs[ri].ymax = rcMax(regs[ri].ymax, s.y);
				
				// Collect all region layers.
				if (nlregs < RC_MAX_LAYERS)
					lregs[nlregs++] = ri;
				
				// Update neighbours
				for (int dir = 0; dir < 4; ++dir)
				{
					if (rcGetCon(s, dir) != RC_NOT_CONNECTED)
					{
						const int ax = x + rcGetDirOffsetX(dir);
						const int ay = y + rcGetDirOffsetY(dir);
						const int ai = (int)chf.cells[ax+ay*w].index + rcGetCon(s, dir);
						const unsigned short rai = srcReg[ai];
						if (rai != 0xffff && rai != ri)
						{
							// Don't check return value -- if we cannot add the neighbor
							// it will just cause a few more regions to be created, which
							// is fine.
							addUnique(regs[ri].neis, regs[ri].nneis, RC_MAX_NEIS, rai);
						}
					}
				}
				
			}
			
			// Update overlapping regions.
			for (int i = 0; i < nlregs-1; ++i)
			{
				for (int j = i+1; j < nlregs; ++j)
				{
					if (lregs[i] != lregs[j])
					{
						if (!checkAllocLLBs(llbs, llbn))
						{
							ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'llbs' (%d).", llbn);
							return false;
						}

						rcLayerRegion& ri = regs[lregs[i]];
						rcLayerRegion& rj = regs[lregs[j]];

						if (!addUnique(llbs, llbn, llbf, ri.layers_llb, ri.last_llb, lregs[j]) ||
							!addUnique(llbs, llbn, llbf, rj.layers_llb, rj.last_llb, lregs[i]))
						{
							ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: layer overflow (too many overlapping walkable platforms). Try increasing RC_LLIST_BLKMAX.");
							return false;
						}
					}
				}
			}
			
		}
	}
	
	// Create 2D layers from regions.
	unsigned char layerId = 0;
	
	static const int MAX_STACK = 64;
	unsigned short stack[MAX_STACK];
	int nstack = 0;
	
	for (int i = 0; i < nregs; ++i)
	{
		rcLayerRegion& root = regs[i];
		// Skip already visited.
		if (root.layerId != 0xff)
			continue;

		// Start search.
		root.layerId = layerId;
		root.base = 1;
		
		nstack = 0;
		stack[nstack++] = (unsigned short)i;
		
		while (nstack)
		{
			// Pop front
			rcLayerRegion& reg = regs[stack[0]];
			nstack--;
			for (int j = 0; j < nstack; ++j)
				stack[j] = stack[j+1];
			
			const int nneis = (int)reg.nneis;
			for (int j = 0; j < nneis; ++j)
			{
				const unsigned short nei = reg.neis[j];
				rcLayerRegion& regn = regs[nei];
				// Skip already visited.
				if (regn.layerId != 0xff)
					continue;
				// Skip if the neighbour is overlapping root region.
				if (contains(llbs, root.layers_llb, root.last_llb, nei))
					continue;
				// Skip if the height range would become too large.
				const int ymin = rcMin(root.ymin, regn.ymin);
				const int ymax = rcMax(root.ymax, regn.ymax);
				if ((ymax - ymin) >= 255)
					 continue;

				if (nstack < MAX_STACK)
				{
					if (!checkAllocLLBs(llbs, llbn))
					{
						ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'llbs' (%d).", llbn);
						return false;
					}

					// Deepen
					stack[nstack++] = (unsigned short)nei;

					// Mark layer id
					regn.layerId = layerId;
					// Merge current layers to root.
					if (!addUniques(llbs, llbn, llbf, root.layers_llb, root.last_llb, regn.layers_llb, regn.last_llb))
					{
						ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: layer overflow (too many overlapping walkable platforms). Try increasing RC_LLIST_BLKMAX.");
						return false;
					}
					root.ymin = rcMin(root.ymin, regn.ymin);
					root.ymax = rcMax(root.ymax, regn.ymax);
				}
			}
		}
		
		layerId++;
	}
	
	// Merge non-overlapping regions that are close in height.
	const unsigned short mergeHeight = (unsigned short)walkableHeight * 4;
	
	for (int i = 0; i < nregs; ++i)
	{
		rcLayerRegion& ri = regs[i];
		if (!ri.base) continue;
		
		unsigned char newId = ri.layerId;
		
		for (;;)
		{
			unsigned char oldId = 0xff;
			
			for (int j = 0; j < nregs; ++j)
			{
				if (i == j) continue;
				rcLayerRegion& rj = regs[j];
				if (!rj.base) continue;
				
				// Skip if the regions are not close to each other.
				if (!overlapRange(ri.ymin,ri.ymax+mergeHeight, rj.ymin,rj.ymax+mergeHeight))
					continue;
				// Skip if the height range would become too large.
				const int ymin = rcMin(ri.ymin, rj.ymin);
				const int ymax = rcMax(ri.ymax, rj.ymax);
				if ((ymax - ymin) >= 255)
				  continue;
						  
				// Make sure that there is no overlap when merging 'ri' and 'rj'.
				bool overlap = false;
				// Iterate over all regions which have the same layerId as 'rj'
				for (int k = 0; k < nregs; ++k)
				{
					if (regs[k].layerId != rj.layerId)
						continue;
					// Check if region 'k' is overlapping region 'ri'
					// Index to 'regs' is the same as region id.
					if (contains(llbs, ri.layers_llb , ri.last_llb, (unsigned short)k))
					{
						overlap = true;
						break;
					}
				}
				// Cannot merge of regions overlap.
				if (overlap)
					continue;
				
				// Can merge i and j.
				oldId = rj.layerId;
				break;
			}
			
			// Could not find anything to merge with, stop.
			if (oldId == 0xff)
				break;
			
			// Merge
			for (int j = 0; j < nregs; ++j)
			{
				rcLayerRegion& rj = regs[j];
				if (rj.layerId == oldId)
				{
					if (!checkAllocLLBs(llbs, llbn))
					{
						ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'llbs' (%d).", llbn);
						return false;
					}

					rj.base = 0;
					// Remap layerIds.
					rj.layerId = newId;
					// Add overlaid layers from 'rj' to 'ri'.
					if (!addUniques(llbs, llbn, llbf, ri.layers_llb, ri.last_llb, rj.layers_llb, rj.last_llb))
					{
						ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: layer overflow (too many overlapping walkable platforms). Try increasing RC_LLIST_BLKMAX.");
						return false;
					}

					// Update height bounds.
					ri.ymin = rcMin(ri.ymin, rj.ymin);
					ri.ymax = rcMax(ri.ymax, rj.ymax);
				}
			}
		}
	}
	
	// Compact layerIds
	unsigned char remap[256];
	memset(remap, 0, 256);

	// Find number of unique layers.
	layerId = 0;
	for (int i = 0; i < nregs; ++i)
		remap[regs[i].layerId] = 1;
	for (int i = 0; i < 256; ++i)
	{
		if (remap[i])
			remap[i] = layerId++;
		else
			remap[i] = 0xff;
	}
	// Remap ids.
	for (int i = 0; i < nregs; ++i)
		regs[i].layerId = remap[regs[i].layerId];
	
	// No layers, return empty.
	if (layerId == 0)
		return true;
	
	// Create layers.
	rcAssert(lset.layers == 0);
	
	const int lw = w - borderSize*2;
	const int lh = h - borderSize*2;

	// Build contracted bbox for layers.
	float bmin[3], bmax[3];
	rcVcopy(bmin, chf.bmin);
	rcVcopy(bmax, chf.bmax);
	bmin[0] += borderSize*chf.cs;
	bmin[2] += borderSize*chf.cs;
	bmax[0] -= borderSize*chf.cs;
	bmax[2] -= borderSize*chf.cs;
	
	lset.nlayers = (int)layerId;
	
	lset.layers = (rcHeightfieldLayer*)rcAlloc(sizeof(rcHeightfieldLayer)*lset.nlayers, RC_ALLOC_PERM);
	if (!lset.layers)
	{
		ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'layers' (%d).", lset.nlayers);
		return false;
	}
	memset(lset.layers, 0, sizeof(rcHeightfieldLayer)*lset.nlayers);

	
	// Store layers.
	for (int i = 0; i < lset.nlayers; ++i)
	{
		unsigned char curId = (unsigned char)i;

		rcHeightfieldLayer* layer = &lset.layers[i];

		const int gridSize = sizeof(unsigned char)*lw*lh;

		layer->heights = (unsigned char*)rcAlloc(gridSize, RC_ALLOC_PERM);
		if (!layer->heights)
		{
			ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'heights' (%d).", gridSize);
			return false;
		}
		memset(layer->heights, 0xff, gridSize);

		layer->areas = (unsigned char*)rcAlloc(gridSize, RC_ALLOC_PERM);
		if (!layer->areas)
		{
			ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'areas' (%d).", gridSize);
			return false;
		}
		memset(layer->areas, 0, gridSize);

		layer->cons = (unsigned char*)rcAlloc(gridSize, RC_ALLOC_PERM);
		if (!layer->cons)
		{
			ctx->log(RC_LOG_ERROR, "rcBuildHeightfieldLayers: Out of memory 'cons' (%d).", gridSize);
			return false;
		}
		memset(layer->cons, 0, gridSize);
		
		// Find layer height bounds.
		int hmin = 0, hmax = 0;
		for (int j = 0; j < nregs; ++j)
		{
			if (regs[j].base && regs[j].layerId == curId)
			{
				hmin = (int)regs[j].ymin;
				hmax = (int)regs[j].ymax;
			}
		}

		layer->width = lw;
		layer->height = lh;
		layer->cs = chf.cs;
		layer->ch = chf.ch;
		
		// Adjust the bbox to fit the heightfield.
		rcVcopy(layer->bmin, bmin);
		rcVcopy(layer->bmax, bmax);
		layer->bmin[1] = bmin[1] + hmin*chf.ch;
		layer->bmax[1] = bmin[1] + hmax*chf.ch;
		layer->hmin = hmin;
		layer->hmax = hmax;

		// Update usable data region.
		layer->minx = layer->width;
		layer->maxx = 0;
		layer->miny = layer->height;
		layer->maxy = 0;
		
		// Copy height and area from compact heightfield. 
		for (int y = 0; y < lh; ++y)
		{
			for (int x = 0; x < lw; ++x)
			{
				const int cx = borderSize+x;
				const int cy = borderSize+y;
				const rcCompactCell& c = chf.cells[cx+cy*w];
				for (int j = (int)c.index, nj = (int)(c.index+c.count); j < nj; ++j)
				{
					const rcCompactSpan& s = chf.spans[j];
					// Skip unassigned regions.
					if (srcReg[j] == 0xffff)
						continue;
					// Skip of does nto belong to current layer.
					unsigned char lid = regs[srcReg[j]].layerId;
					if (lid != curId)
						continue;
					
					// Update data bounds.
					layer->minx = rcMin(layer->minx, x);
					layer->maxx = rcMax(layer->maxx, x);
					layer->miny = rcMin(layer->miny, y);
					layer->maxy = rcMax(layer->maxy, y);
					
					// Store height and area type.
					const int idx = x+y*lw;
					layer->heights[idx] = (unsigned char)(s.y - hmin);
					layer->areas[idx] = chf.areas[j];
					
					// Check connection.
					unsigned char portal = 0;
					unsigned char con = 0;
					for (int dir = 0; dir < 4; ++dir)
					{
						if (rcGetCon(s, dir) != RC_NOT_CONNECTED)
						{
							const int ax = cx + rcGetDirOffsetX(dir);
							const int ay = cy + rcGetDirOffsetY(dir);
							const int ai = (int)chf.cells[ax+ay*w].index + rcGetCon(s, dir);
							unsigned char alid = srcReg[ai] != 0xffff ? regs[srcReg[ai]].layerId : 0xff;
							// Portal mask
							if (chf.areas[ai] != RC_NULL_AREA && lid != alid)
							{
								portal |= (unsigned char)(1<<dir);
								// Update height so that it matches on both sides of the portal.
								const rcCompactSpan& as = chf.spans[ai];
								if (as.y > hmin)
									layer->heights[idx] = rcMax(layer->heights[idx], (unsigned char)(as.y - hmin));
							}
							// Valid connection mask
							if (chf.areas[ai] != RC_NULL_AREA && lid == alid)
							{
								const int nx = ax - borderSize;
								const int ny = ay - borderSize;
								if (nx >= 0 && ny >= 0 && nx < lw && ny < lh)
									con |= (unsigned char)(1<<dir);
							}
						}
					}
					
					layer->cons[idx] = (portal << 4) | con;
				}
			}
		}
		
		if (layer->minx > layer->maxx)
			layer->minx = layer->maxx = 0;
		if (layer->miny > layer->maxy)
			layer->miny = layer->maxy = 0;
	}
	
	return true;
}
