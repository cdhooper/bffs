#include <dos/filehandler.h>

#include "config.h"

/* no need to go further if we are building the read only release */
#ifndef RONLY

#include "superblock.h"
#include "alloc.h"
#include "fsmacros.h"
#include "cache.h"

/* This set of routines deals directly with cg allocation blocks */

extern int optimization;
/* bit_set, bit_clr, bit_val */


/* This routine will allocate an entire fs block in the map */
block_alloc(map, fpos)
unsigned char *map;
ULONG	 fpos;
{
	switch (FRAGS_PER_BLK) {
		case 1:
/*
			bit_clr(map, fpos);
*/
			map[fpos >> 3] &= ~(1 << (fpos & 7));
			break;
		case 2:
/*
			bit_clr(map, fpos & ~1);
			bit_clr(map, fpos | 1);
*/
			map[fpos >> 3] &= ~(1 <<  (fpos & 6));
			map[fpos >> 3] &= ~(1 << ((fpos & 6) | 1));
			break;
		case 4:
			/* note 0xf0 and 0x0f are reversed because of & */
			map[fpos >> 3] &= ((fpos & 4) ? 0x0f : 0xf0);
			break;
		case 8:
			map[fpos >> 3] = 0x00;
			break;
		default:
			PRINT(("block_alloc: Bad frags per blk %d\n", FRAGS_PER_BLK));
	}
}


/* This routine will deallocate an entire fs block in the map */
block_dealloc(map, fpos)
unsigned char *map;
ULONG	 fpos;
{
	switch (FRAGS_PER_BLK) {
		case 1:
			map[fpos >> 3] |= (1 << (fpos & 7));
/*			bit_set(map, fpos); */
			break;
		case 2:
/*			bit_set(map, fpos & ~1);
			bit_set(map, fpos |  1); */
			map[fpos >> 3] |= (1 <<  (fpos & 6));
			map[fpos >> 3] |= (1 << ((fpos & 6) | 1));
			break;
		case 4:
			map[fpos >> 3] |= ((fpos & 4) ? 0xf0 : 0x0f);
			break;
		case 8:
			map[fpos >> 3] = 0xff;
			break;
		default:
			PRINT(("block_dealloc: Bad frags per blk %d\n",FRAGS_PER_BLK));
	}
}


/* This routine will return whether an entire block in the map is free */
block_avail(map, fpos)
unsigned char *map;
ULONG	 fpos;
{
	int temp;

	switch (FRAGS_PER_BLK) {
		case 1:
			return(bit_val(map, fpos));
		case 2:
			return(bit_val(map, fpos & ~1) && bit_val(map, fpos | 1));
		case 4:
			temp = (fpos & 4) ? 0xf0 : 0x0f;
			return((map[fpos >> 3] & temp) == temp);
		case 8:
			return(map[fpos >> 3] == 0xff);
		default:
			PRINT(("block_avail: Bad frags per blk %d\n", FRAGS_PER_BLK));
	}
}


/* MIN_CHECK
 * 	This is a reused code piece to check if the previous
 *	contiguous disk fragment segment is small enough for
 *	allocation by request of the calling routine.
 */
#define MIN_CHECK							\
	if (freehere == min_frags) {					\
		PRINT(("exact min pos=%d size=%d\n",			\
			index+index2-freehere, min_frags));		\
		return(index + index2 - freehere);			\
	}								\
	if ((freehere >= min_frags) && (freehere < min_found)) {	\
		min_found = freehere;					\
		min_place = index + index2 - freehere;			\
		PRINT(("new min pos=%d size=%d\n",			\
			min_place, min_found));				\
	}

/* block_fragblock_find()
 *	Find the closest position to the preferred with frags greater or
 *	equal to min_frags
 *
 *	This routine implements the following policy (loosely):
 *	    TIME:
 *		A fragment section forward from the preferred is desired
 *		The closest (large enough) fragment section is preferred
 *	    SPACE:
 *		The smallest (large enough) fragment section is preferred
 *		A fragment section forward from the preferred is desired
 *		Minimum distance is desired less than fragment size match
 *
 *	Note: Preferred block is relative to current cg and is a frag addr
 *	      rounded to a block addr by the caller
 *	      Return will be a frag addr (possibly not an even block
 *	      address).  This would happen if the first part of the block
 *	      were already in use.  Many times this routine will choose a
 *	      completely empty block.  If this happens and we were
 *	      preparing to move data, for a file shrink (close), then it
 *	      would be best to leave the data in the block it already
 *	      occupies.  // This routine returns -1 on failure. //
 */
int	block_fragblock_find(mycg, preferred, min_frags)
struct	cg *mycg;
ULONG	preferred;
int	min_frags;
{
	int	 index;
	int	 index2;
	int	 min_found;
	int	 min_place;
	int	 freehere;
	unsigned char *map;

	map = cg_blksfree(mycg);

	preferred = blknum(superblock, preferred);
	if (preferred > mycg->cg_ndblk)
		preferred = 0;

/*
	PRINT(("bfragfind near frag=%d of cg=%d size=%d\n",
		preferred, mycg->cg_cgx, min_frags));
*/

	if ((min_frags < 1) || (min_frags > FRAGS_PER_BLK)) {
		PRINT(("Error calling block_fragblock_find, size=%d\n", min_frags));
		return(-1);
	}

	if (optimization == TIME) {  /* find closest block with size >= needed */
	    PRINT(("time\n"));
	    for (index = preferred; index < mycg->cg_ndblk;
		 index += FRAGS_PER_BLK) {
		freehere = 0;
		for (index2 = 0; index2 < FRAGS_PER_BLK; index2++)
		    if (bit_val(map, index + index2)) {
			freehere++;
			if (freehere == min_frags) {
			    PRINT((">found at %d\n", index + index2 - freehere + 1));
			    return(index + index2 - freehere + 1);
			}
		    } else
			freehere = 0;
	    }
	    for (index = preferred - FRAGS_PER_BLK; index >= 0;
		 index -= FRAGS_PER_BLK) {
		freehere = 0;
		for (index2 = 0; index2 < FRAGS_PER_BLK; index2++)
		    if (bit_val(map, index + index2)) {
			freehere++;
			if (freehere == min_frags) {
			    PRINT(("<found at %d\n", index + index2 - freehere + 1));
			    return(index + index2 - freehere + 1);
			}
		    } else
			freehere = 0;
	    }
	    PRINT(("nothing found!|\n"));
	    return(-1);
	} else {
/*	    PRINT(("space\n")); */
	    /* find closest block with size >= min && min(size - min_frags) */

	    /* quicker - if there are no min size blocks, find bigger */
	    for (; min_frags < FRAGS_PER_BLK; min_frags++)
		if (mycg->cg_frsum[min_frags])
			break;

	    min_place = -1;
	    min_found = FRAGS_PER_BLK;

	    for (index = preferred; index < mycg->cg_ndblk;
		 index += FRAGS_PER_BLK) {
		freehere = 0;
		for (index2 = 0; index2 < FRAGS_PER_BLK; index2++)
		    if (bit_val(map, index + index2))
			freehere++;
		    else {
			MIN_CHECK;
			freehere = 0;
		    }
		MIN_CHECK;
	    }
	    for (index = preferred - FRAGS_PER_BLK; index >= 0;
		 index -= FRAGS_PER_BLK) {
		freehere = 0;
		for (index2 = 0; index2 < FRAGS_PER_BLK; index2++)
		    if (bit_val(map, index + index2))
			freehere++;
		    else {
			MIN_CHECK;
			freehere = 0;
		    }
		MIN_CHECK;
	    }
	    return(min_place);
	}
}

/* This routine will tell whether the given frag to the end of the
   block is available
	0 = not all available
	1 = all available

   if given a block address, can also tell if the entire block is free
*/
rest_is_free(map, frag)
unsigned char	*map;
ULONG	 frag;
{
	int index;
	for (index = frag % FRAGS_PER_BLK + 1; index < FRAGS_PER_BLK; index++)
		if (bit_val(map, frag + index) == 0)
			return(0);
	return(1);
}

/* this routine will tell whether the given frag is free */
frag_is_free(map, frag)
unsigned char *map;
ULONG	 frag;
{
	if (bit_val(map, frag))
		return(1);
	else
		return(0);
}

/* This routine will find the nearest free full fs block */
ULONG block_free_find(mycg, preferred)
struct	cg *mycg;
ULONG	preferred;
{
	int	 fragpos = -1;
	int	 mindist;
	int	 index;
	int	 temp;
	int	 temp2;
	int	 maxfrag;
	unsigned char *map;

	mindist = mycg->cg_ndblk;
	maxfrag = mycg->cg_ndblk;

	map = cg_blksfree(mycg);
	preferred = blknum(superblock, preferred);
	if (preferred > mycg->cg_ndblk)
		preferred = 0;

	switch (FRAGS_PER_BLK) {
	    case 1:
		for (index = 0; index < maxfrag; index++) {
			if (bit_val(map, index)) {
				temp = abs(preferred - index);
				if (temp <= mindist) {
					fragpos = index;
					mindist = temp;
				} else
					break;
			}
		}
/* I think the else break will cut down on search time - once the
   sweet spot is passed and we're not finding closer holes anymore */
		break;
	    case 2:
		for (index = 0; index < maxfrag; index += 2) {
			if ((bit_val(map, index & ~1)) &&
			    (bit_val(map, index | 1))) {
				temp = abs(preferred - index);
				if (temp <= mindist) {
					fragpos = index;
					mindist = temp;
				} else
					break;
			}
		}
		break;
	    case 4:
		for (index = 0; index < maxfrag; index += 8) {
			temp2 = index >> 3;
			if ((map[temp2] & 0x0f) == 0x0f) {
				temp = abs(preferred - index);
				if (temp <= mindist) {
					fragpos = index;
					mindist = temp;
				} else
					break;
			}
			if ((map[temp2] & 0xf0) == 0xf0) {
				temp = abs(preferred - (index + 4));
				if (temp <= mindist) {
					fragpos = index + 4;
					mindist = temp;
				} else
					break;
			}
		}
		break;
	    case 8:
		preferred >>= 3;
		maxfrag   >>= 3;
		for (index = 0; index < maxfrag; index++) {
			if (map[index] == 0xff) {
				temp = abs(preferred - index);
				if (temp <= mindist) {
					fragpos = index << 3;
					mindist = temp;
				} else
					break;
			}
		}
		break;
	    default:
		PRINT(("bff: Unsupported frags per blocks!! %d\n", FRAGS_PER_BLK));
	}

	return(fragpos);
}


ULONG block_allocate(nearblock)
ULONG nearblock;
{
	ULONG	cgx;
	ULONG	bfree;
	ULONG	temp;
	int	block;
	ULONG	index;
	int	cylinder;
	struct	cg *mycg;

	cgx  = dtog(superblock, nearblock);
	mycg = cache_cg(cgx);


	PRINT(("balloc: around blk %d (cg %d) - ", nearblock, cgx));


	if (superblock->fs_cstotal.cs_nbfree == 0) {
		PRINT(("no blocks available in filesystem\n"));
		return(0);
	}

	block = block_free_find(mycg, dtogd(superblock, nearblock));

	if (block == -1) {	/* then we must find cg with least blks used */
		ULONG	ncg;

		ncg = DISK32(superblock->fs_ncg);
		bfree = 0;
		for (index = 0; index < ncg; index++) {
		    temp = DISK32(superblock->fs_cs(superblock, index).cs_nbfree);
		    if (temp > bfree) {
			bfree = temp;
			cgx   = index;
			PRINT(("\n%d has %d blocks available  ", cgx, bfree));
		    }
		}

		if (bfree == 0) {
			PRINT2(("INCON: block_allocate: no space free in fs\n"));
			return(0);
		}

		mycg  = cache_cg(cgx);
		block = block_free_find(mycg, 0);

#ifndef FAST
		if (block == -1) {
			PRINT2(("INCON: cg %d should have block free!\n", cgx));
			return(0);
		}
#endif
	}

/*
	PRINT(("found block %d in cg %d\n", block, cgx));
*/
	mycg = cache_cg_write(cgx);
	block_alloc(cg_blksfree(mycg), block);
	mycg->cg_cs.cs_nbfree--;

#ifdef INTEL

	temp = DISK32(superblock->fs_fmod) + 1;
	superblock->fs_fmod = DISK32(temp);

	temp = DISK32(superblock->fs_cstotal.cs_nbfree) - 1;
	superblock->fs_cstotal.cs_nbfree = DISK32(temp);

	temp = DISK32(superblock->fs_cs(superblock, cgx).cs_nbfree) - 1;
	superblock->fs_cs(superblock, cgx).cs_nbfree = DISK32(temp);

	cylinder = cbtocylno(superblock, block);
	temp = DISK16(cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, block)]) - 1;
	cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, block)] = DISK16(temp);
	temp = DISK16(cg_blktot(mycg)[cylinder]) - 1;
	cg_blktot(mycg)[cylinder] = DISK16(temp);

#else

	superblock->fs_fmod++;
	superblock->fs_cstotal.cs_nbfree--;
	superblock->fs_cs(superblock, cgx).cs_nbfree--;
	cylinder = cbtocylno(superblock, block);
	cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, block)]--;
	cg_blktot(mycg)[cylinder]--;

#endif

	return(block + cgbase(superblock, cgx));
}


block_deallocate(block)
ULONG block;
{
	ULONG	cgx;
	ULONG	cblock;
	int	cylinder;
	struct	cg *mycg;

/*	PRINT(("block_deallocate %d\n", block)); */

	cgx  = dtog(superblock, block);
	mycg = cache_cg_write(cgx);

	cblock = dtogd(superblock, block);

	block_dealloc(cg_blksfree(mycg), cblock);
	mycg->cg_cs.cs_nbfree++;
	superblock->fs_fmod++;
	superblock->fs_cstotal.cs_nbfree++;
	superblock->fs_cs(superblock, cgx).cs_nbfree++;
	cylinder = cbtocylno(superblock, cblock);
	cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, cblock)]++;
	cg_blktot(mycg)[cylinder]++;
}


frags_allocate(fragstart, frags)
ULONG fragstart;
ULONG frags;
{
	int	index;
	int	fragoffset;
	int	cylinder;
	int	count_low  = 0;
	int	count_high = 0;
	struct	cg *mycg;
	ULONG	cgfragstart;
	ULONG	cgblkstart;
	ULONG	blkstart;
	ULONG	cgx;

/*	PRINT(("frags_allocate: start=%d frags=%d\n", fragstart, frags)); */

	cgx  = dtog(superblock, fragstart);
	mycg = cache_cg_write(cgx);

	cgfragstart = dtogd(superblock, fragstart);
	fragoffset  = cgfragstart % FRAGS_PER_BLK;
	cgblkstart  = cgfragstart - fragoffset;
	blkstart    =   fragstart - fragoffset;

/*	PRINT(("cgfragstart=%d fragoffset=%d cgblkstart=%d blkstart=%d\n",
		cgfragstart, fragoffset, cgblkstart, blkstart)); */

	/* check for low frags which will be broken off */
	for (index = fragoffset - 1; index >= 0; index--)
		if (bit_val(cg_blksfree(mycg), cgblkstart + index))
			count_low++;
		else
			break;

	/* check for high frags which will be broken off */
	for (index = fragoffset + frags; index < FRAGS_PER_BLK; index++)
		if (bit_val(cg_blksfree(mycg), cgblkstart + index))
			count_high++;
		else
			break;


	/* update summary info regarding new surrounding fragment sizes */
	if (count_low)
		mycg->cg_frsum[count_low]++;
	if (count_high)
		mycg->cg_frsum[count_high]++;

/*	PRINT(("alloc: low=%d frags=%d high=%d\n", count_low, frags, count_high)); */

	superblock->fs_fmod++;
	superblock->fs_cstotal.cs_nffree	     -= frags;
	superblock->fs_cs(superblock, cgx).cs_nffree -= frags;
	mycg->cg_cs.cs_nffree			     -= frags;

	if (frags + count_low + count_high == FRAGS_PER_BLK) {
		/* it was a completely empty block */
		cgblkstart = block_allocate(blkstart);
		if (cgblkstart != blkstart) {
			PRINT2(("INCON: blk %d should be free for split, but isn't!  got %d instead\n", blkstart, cgblkstart));
			if (cgblkstart)
				block_deallocate(cgblkstart);
			return;
		}

		/* deallocate unused block fragments */
		for (index = 0; index < count_low; index++)
			bit_set(cg_blksfree(mycg), cgfragstart - index - 1);
		for (index = 0; index < count_high; index++)
			bit_set(cg_blksfree(mycg), cgfragstart + frags + index);

		superblock->fs_cstotal.cs_nffree	     += FRAGS_PER_BLK;
		superblock->fs_cs(superblock, cgx).cs_nffree += FRAGS_PER_BLK;
		mycg->cg_cs.cs_nffree			     += FRAGS_PER_BLK;
	} else {  /* discount old frag */
		mycg->cg_frsum[frags + count_low + count_high]--;

		/* allocate the block fragments */
		for (index = 0; index < frags; index++)
			bit_clr(cg_blksfree(mycg), cgfragstart + index);
	}
}


/* deallocate requested number of frags starting at fragsstart */
frags_deallocate(fragstart, frags)
ULONG fragstart;
ULONG frags;
{
	int	index;
	int	fragoffset;
	int	cylinder;
	int	count_low  = 0;
	int	count_high = 0;
	struct	cg *mycg;
	ULONG	cgfragstart;
	ULONG	cgblkstart;
	ULONG	blkstart;
	ULONG	cgx;

	if (frags == 0)
		return;

/*	PRINT(("frags_deallocate: start=%d frags=%d\n", fragstart, frags)); */

	cgx  = dtog(superblock, fragstart);
	mycg = cache_cg_write(cgx);

	cgfragstart = dtogd(superblock, fragstart);
	fragoffset  = cgfragstart % FRAGS_PER_BLK;
	cgblkstart  = cgfragstart - fragoffset;
	blkstart    =   fragstart - fragoffset;

/*	PRINT(("cgfragstart=%d fragoffset=%d cgblkstart=%d blkstart=%d\n",
		cgfragstart, fragoffset, cgblkstart, blkstart)); */

	/* check for low frags which will be coalesced */
	for (index = fragoffset - 1; index >= 0; index--)
		if (bit_val(cg_blksfree(mycg), cgblkstart + index))
			count_low++;
		else
			break;

	/* check for high frags which will be coalesced */
	for (index = fragoffset + frags; index < FRAGS_PER_BLK; index++)
		if (bit_val(cg_blksfree(mycg), cgblkstart + index))
			count_high++;
		else
			break;

/*	PRINT(("dealloc: low=%d frags=%d high=%d\n", count_low, frags, count_high)); */

	/* reduce summary info regarding old surrounding frag sizes */
	if (count_low)
		mycg->cg_frsum[count_low]--;
	if (count_high)
		mycg->cg_frsum[count_high]--;

	superblock->fs_fmod++;
	superblock->fs_cstotal.cs_nffree	     += frags;
	superblock->fs_cs(superblock, cgx).cs_nffree += frags;
	mycg->cg_cs.cs_nffree			     += frags;

	/* if entire block is now available ... */
	if (frags + count_low + count_high == FRAGS_PER_BLK) {
		block_deallocate(blkstart);
		superblock->fs_cstotal.cs_nffree	     -= FRAGS_PER_BLK;
		superblock->fs_cs(superblock, cgx).cs_nffree -= FRAGS_PER_BLK;
		mycg->cg_cs.cs_nffree			     -= FRAGS_PER_BLK;
	} else {   /* coalesce with other frags in block, both before and after */
		mycg->cg_frsum[frags + count_low + count_high]++;
		/* free the block fragments */
		for (index = 0; index < frags; index++)
			bit_set(cg_blksfree(mycg), cgfragstart + index);
	}
}

#endif
