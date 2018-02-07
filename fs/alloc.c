/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <dos/filehandler.h>
#include "config.h"

/* no need to go further if we are building the read only release */
#ifndef RONLY

#include "superblock.h"
#include "alloc.h"
#include "fsmacros.h"
#include "cache.h"

/* allocation optimization, time is default */
#define OPT_TIME  0
#define OPT_SPACE 1

static int optimization = OPT_TIME; /* filesystem write optimization method */


/* This routine will allocate an entire fs block in the map */
static void
block_alloc(uint8_t *map, ULONG fpos)
{
    switch (FRAGS_PER_BLK) {
        case 1:
            /*
             * Same as
             * bit_clr(map, fpos);
             */
            map[fpos >> 3] &= ~(1 << (fpos & 7));
            break;
        case 2:
            /*
             * Same as
             * bit_clr(map, fpos & ~1);
             * bit_clr(map, fpos | 1);
             */
            map[fpos >> 3] &= ~(1 <<  (fpos & 6));
            map[fpos >> 3] &= ~(1 << ((fpos & 6) | 1));
            break;
        case 4:
            /* Note 0xf0 and 0x0f are reversed because of & */
            map[fpos >> 3] &= ((fpos & 4) ? 0x0f : 0xf0);
            /* XXX: This might be faster: (0xf0 >> (fpos & 4)) */
            break;
        case 8:
            map[fpos >> 3] = 0x00;
            break;
        default:
            PRINT2(("block_alloc: Bad frags per blk %u\n",
                    FRAGS_PER_BLK));
            break;
    }
}


/*
 * This routine will deallocate an entire fs block in the map.
 * The caller must provide a frag address which is block-aligned.
 */
static void
block_dealloc(uint8_t *map, ULONG fpos)
{
    switch (FRAGS_PER_BLK) {
        case 1:
            map[fpos >> 3] |= (1 << (fpos & 7));
            /*
             * Same as:
             * bit_set(map, fpos);
             */
            break;
        case 2:
            /*
             * Same as:
             * bit_set(map, fpos & ~1);
             * bit_set(map, fpos |  1);
             */
            map[fpos >> 3] |= (1 <<  (fpos & 6));
            map[fpos >> 3] |= (1 << ((fpos & 6) | 1));
            break;
        case 4:
            map[fpos >> 3] |= ((fpos & 4) ? 0xf0 : 0x0f);
            /* XXX: This might be faster: (0x0f << (fpos & 4)) */
            break;
        case 8:
            map[fpos >> 3] = 0xff;
            break;
        default:
            PRINT2(("block_dealloc: Bad frags per blk %u\n",
                    FRAGS_PER_BLK));
            break;
    }
}


/*
 * This routine will return whether an entire block in the map is free
 * The caller must provide a frag address which is block-aligned.
 * */
int
block_avail(uint8_t *map, ULONG fpos)
{
    int temp;

    switch (FRAGS_PER_BLK) {
        case 1:
            return (bit_val(map, fpos));
        case 2:
            return (bit_val(map, fpos & ~1) &&
                    bit_val(map, fpos | 1));
        case 4:
            temp = (fpos & 4) ? 0xf0 : 0x0f;
            return ((map[fpos >> 3] & temp) == temp);
        case 8:
            return (map[fpos >> 3] == 0xff);
        default:
            PRINT2(("block_avail: Bad frags per blk %u\n",
                    FRAGS_PER_BLK));
            return (0);
    }
}


#define MIN_DEBUG(x)  /* No debug */

/*
 * MIN_CHECK
 *      This macro is used in multiple places to check if the previous
 *      contiguous disk fragment segment is small enough for allocation.
 */
#define MIN_CHECK                                               \
    if (freehere == min_frags) {                                \
        MIN_DEBUG(("exact min pos=%d size=%d\n",                \
                   index+index2-freehere, min_frags));          \
        return (index + index2 - freehere);                     \
    }                                                           \
    if ((freehere >= min_frags) && (freehere < min_found)) {    \
        min_found = freehere;                                   \
        min_place = index + index2 - freehere;                  \
        MIN_DEBUG(("new min pos=%d size=%d\n",                  \
                  min_place, min_found));                       \
    }

/*
 * block_fragblock_find()
 *
 * Find the closest position to the preferred with frags greater or
 * equal to min_frags.
 *
 * This routine implements the following policy (loosely):
 *     OPT_TIME:
 *         A fragment section forward from the preferred is desired
 *         The closest (large enough) fragment section is preferred
 *     OPT_SPACE:
 *         The smallest (large enough) fragment section is preferred
 *         A fragment section forward from the preferred is desired
 *         Minimum distance is desired less than fragment size match
 *
 * Note: Preferred block is relative to current cg and is a frag addr
 *       rounded to a block addr by the caller.
 *
 *       Return will be a frag addr (possibly not an even block
 *       address).  This would happen if the first part of the block
 *       were already in use.  Many times this routine will choose a
 *       completely empty block.  If this happens and we were
 *       preparing to move data, for a file shrink (close), then it
 *       would be best to leave the data in the block it already
 *       occupies.
 *
 *       This routine returns -1 on failure.
 */
int
block_fragblock_find(struct cg *mycg, ULONG preferred, int min_frags)
{
    uint8_t *map;
    int      index;
    int      index2;
    int      min_found;
    int      min_place;
    int      freehere;
    int      ndblk = DISK32(mycg->cg_ndblk);

    map = cg_blksfree(mycg);

    preferred = blknum(superblock, preferred);
    if (preferred > ndblk)
        preferred = 0;

#ifdef BLOCK_FRAGFIND_DEBUG
    PRINT(("bfragfind near frag=%d of cg=%d size=%d\n",
            preferred, DISK32(mycg->cg_cgx), min_frags));
#endif

    if ((min_frags < 1) || (min_frags > FRAGS_PER_BLK)) {
        PRINT2(("Error calling block_fragblock_find, size=%d\n", min_frags));
        return (-1);
    }

    if (optimization == OPT_TIME) {
        /*
         * TIME:
         * Find closest block with size >= needed
         */
        for (index = preferred; index < ndblk; index += FRAGS_PER_BLK) {
            freehere = 0;
            for (index2 = 0; index2 < FRAGS_PER_BLK; index2++) {
                if (bit_val(map, index + index2)) {
                    freehere++;
                    if (freehere == min_frags) {
                        index += index2 - freehere + 1;
                        PRINT((">found at %d\n", index));
                        return (index);
                    }
                } else {
                    freehere = 0;
                }
            }
        }
        for (index = preferred - FRAGS_PER_BLK; index >= 0;
             index -= FRAGS_PER_BLK) {
            freehere = 0;
            for (index2 = 0; index2 < FRAGS_PER_BLK; index2++) {
                if (bit_val(map, index + index2)) {
                    freehere++;
                    if (freehere == min_frags) {
                        index += index2 - freehere + 1;
                        PRINT(("<found at %d\n", index));
                        return (index);
                    }
                } else {
                    freehere = 0;
                }
            }
        }
        PRINT(("nothing found\n"));
        return (-1);
    } else {
        /*
         * SPACE:
         * Find closest block with size >= min && min(size - min_frags)
         */

        /* quicker - if there are no min size blocks, find bigger */
        for (; min_frags < FRAGS_PER_BLK; min_frags++)
            if (mycg->cg_frsum[min_frags])
                    break;

        min_place = -1;
        min_found = FRAGS_PER_BLK;

        for (index = preferred; index < ndblk; index += FRAGS_PER_BLK) {
            freehere = 0;
            for (index2 = 0; index2 < FRAGS_PER_BLK; index2++) {
                if (bit_val(map, index + index2)) {
                    freehere++;
                } else {
                    MIN_CHECK;
                    freehere = 0;
                }
            }
            MIN_CHECK;
        }
        for (index = preferred - FRAGS_PER_BLK; index >= 0;
             index -= FRAGS_PER_BLK) {
            freehere = 0;
            for (index2 = 0; index2 < FRAGS_PER_BLK; index2++) {
                if (bit_val(map, index + index2)) {
                    freehere++;
                } else {
                    MIN_CHECK;
                    freehere = 0;
                }
            }
            MIN_CHECK;
        }
        return (min_place);
    }
}

/*
 * This routine will tell whether the given frag to the end of the
 * block is available
 *    0 = not all available
 *    1 = all available
 *
 * If given a block address, it can also tell if the entire block is free.
 */
int
frags_left_are_free(uint8_t *map, ULONG frag)
{
    int index;
    for (index = frag % FRAGS_PER_BLK; index < FRAGS_PER_BLK; index++) {
        if (bit_val(map, frag) == 0)
            return (0);
        frag++;
    }
    return (1);
}

/*
 * This routine will find the nearest free fs block.
 * The entire block is guaranteed to be free.
 * If no block is available, this routine returns -1.
 */
static ULONG
block_free_find(struct cg *mycg, ULONG preferred)
{
    int      fragpos = -1;
    int      mindist;
    int      index;
    int      maxfrag;
    int      ndblk = DISK32(mycg->cg_ndblk);
    uint8_t *map;

    maxfrag = ndblk;
    mindist = ndblk;

    map = cg_blksfree(mycg);
    preferred = blknum(superblock, preferred);
    if (preferred > ndblk)
        preferred = 0;

    switch (FRAGS_PER_BLK) {
        case 1:
            for (index = 0; index < maxfrag; index++) {
                if (bit_val(map, index)) {
                    int dist = preferred - index;
                    int absdist = abs(dist);
                    if (absdist <= mindist) {
                        fragpos = index;
                        mindist = absdist;
                    } else {
                        /*
                         * The "else break" should cut down on
                         * search time - once the sweet spot is
                         * passed and we're not finding closer
                         * holes anymore.
                         */
                        break;
                    }
                }
            }
            break;
        case 2:
            for (index = 0; index < maxfrag; index += 2) {
                if ((bit_val(map, index & ~1)) &&
                    (bit_val(map, index | 1))) {
                    int dist = preferred - index;
                    int absdist = abs(dist);
                    if (absdist <= mindist) {
                        fragpos = index;
                        mindist = absdist;
                    } else {
                        /* Past preferred position */
                        break;
                    }
                }
            }
            break;
        case 4:
            for (index = 0; index < maxfrag; index += 8) {
                int temp2 = index >> 3;
                if ((map[temp2] & 0x0f) == 0x0f) {
                    int dist = preferred - index;
                    int absdist = abs(dist);
                    if (absdist <= mindist) {
                        fragpos = index;
                        mindist = absdist;
                    } else {
                        break;
                    }
                }
                if ((map[temp2] & 0xf0) == 0xf0) {
                    int dist = preferred - (index + 4);
                    int absdist = abs(dist);
                    if (absdist <= mindist) {
                        fragpos = index + 4;
                        mindist = absdist;
                    } else {
                        /* Past preferred position */
                        break;
                    }
                }
            }
            break;
        case 8: {
            int nindex;
            int pindex;

            preferred >>= 3;
            maxfrag   >>= 3;

            /* Search backward from preferred position */
            for (nindex = preferred; nindex >= 0; nindex--)
                if (map[nindex] == 0xff)
                    break;

            /* Search forward */
            for (pindex = preferred + 1; pindex < maxfrag; pindex++)
                if (map[pindex] == 0xff)
                    break;

            /* Handle cases where both directions failed */
            if (nindex < 0) {
                if (pindex < maxfrag)
                    fragpos = pindex << 3;
                break;
            }
            if (pindex >= maxfrag) {
                fragpos = nindex << 3;
                break;
            }

            /* Compare distance of solutions */
            int pdist = pindex - preferred;
            int ndist = preferred - nindex;

            if (pdist < ndist)
                fragpos = pindex << 3;
            else
                fragpos = nindex << 3;
            break;
        }
        default:
            PRINT2(("bff: Unsupported frags per blocks %u\n",
                    FRAGS_PER_BLK));
            break;
    }

    return (fragpos);
}

static void
adjust_blks_free(struct cg *mycg, ULONG cgx, int block, long adjust)
{
    int cylinder = cbtocylno(superblock, block);

#ifdef BOTHENDIAN
    superblock->fs_cs(superblock, cgx).cs_nbfree =
        DISK32(DISK32(superblock->fs_cs(superblock, cgx).cs_nbfree) + adjust);
    cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, block)] =
        DISK16(DISK16(cg_blks(superblock, mycg,
                              cylinder)[cbtorpos(superblock, block)]) + adjust);
    cg_blktot(mycg)[cylinder] =
        DISK32(DISK32(cg_blktot(mycg)[cylinder]) + adjust);
    mycg->cg_cs.cs_nbfree = DISK32(DISK32(mycg->cg_cs.cs_nbfree) + adjust);

    superblock->fs_cstotal.cs_nbfree =
        DISK32(DISK32(superblock->fs_cstotal.cs_nbfree) + adjust);
    if (superblock->fs_flags & FS_FLAGS_UPDATED) {
        superblock->fs_new_cstotal.cs_nbfree[is_big_endian] =
            DISK32(DISK32(superblock->fs_new_cstotal.cs_nbfree[is_big_endian]) +
            adjust);
    }
#else
    superblock->fs_cs(superblock, cgx).cs_nbfree += adjust;
    cg_blks(superblock, mycg, cylinder)[cbtorpos(superblock, block)] += adjust;
    cg_blktot(mycg)[cylinder] += adjust;
    mycg->cg_cs.cs_nbfree += adjust;
    superblock->fs_cstotal.cs_nbfree += adjust;
    if (superblock->fs_flags & FS_FLAGS_UPDATED)
        superblock->fs_new_cstotal.cs_nbfree[1] += adjust;
#endif
    superblock->fs_fmod++;
}

#define NBBY 8
/*
 * Code from ffs_clusteracct() was adapted from the NetBSD 7.1.1 ffs
 * subroutine of the same name.  It has been modified to work within BFFS.
 *
 * mycg is a pointer to the CG in which the block resides.
 * block is a filesystem frag address.
 * adjust may be -1 or 1, depending on whether the block should be
 *     allocated (-1) or deallocated (1).
 */
static void
ffs_clusteracct(struct cg *mycg, ULONG block, int adjust)
{
    long    *sump;
    uint8_t *freemapp;
    uint8_t *mapp;
    int      i;
    int      start;
    int      end;
    int      forw;
    int      back;
    int      map;
    int      bit;
    long     contigsumsize = DISK32(superblock->fs_contigsumsize);
    ULONG    blkno;

    if (contigsumsize <= 0)
        return;

    blkno = fragstoblks(superblock, block);
    freemapp = cg_clustersfree(mycg);
    sump = cg_clustersum(mycg);

    /*
     * Allocate or clear the actual block.
     */
    if (adjust > 0)
        bit_set(freemapp, blkno);
    else
        bit_clr(freemapp, blkno);

    /*
     * Find the size of the cluster going forward.
     */
    start = blkno + 1;
    end = start + contigsumsize;
    if ((ULONG)end >= DISK32(mycg->cg_nclusterblks)) {
        end = DISK32(mycg->cg_nclusterblks);
    }
    mapp = &freemapp[start / NBBY];
    map = *mapp++;
    bit = 1 << (start % NBBY);
    for (i = start; i < end; i++) {
        if ((map & bit) == 0)
            break;
        if ((i & (NBBY - 1)) != (NBBY - 1)) {
            bit <<= 1;
        } else {
            map = *mapp++;
            bit = 1;
        }
    }
    forw = i - start;

    /*
     * Find the size of the cluster going backward.
     */
    start = blkno - 1;
    end = start - contigsumsize;
    if (end < 0)
            end = -1;
    mapp = &freemapp[start / NBBY];
    map = *mapp--;
    bit = 1 << (start % NBBY);
    for (i = start; i > end; i--) {
        if ((map & bit) == 0)
            break;
        if ((i & (NBBY - 1)) != 0) {
            bit >>= 1;
        } else {
            map = *mapp--;
            bit = 1 << (NBBY - 1);
        }
    }
    back = start - i;

    /*
     * Account for old cluster and the possibly new forward and back clusters.
     */
    i = back + forw + 1;
    if (i > contigsumsize)
        i = contigsumsize;
    sump[i] = DISK32(DISK32(sump[i]) + adjust);
    if (back > 0)
        sump[back] = DISK32(DISK32(sump[back]) - adjust);
    if (forw > 0)
        sump[forw] = DISK32(DISK32(sump[forw]) - adjust);
}

/*
 * block_allocate()
 *    Locate and allocate a full block from the filesystem and update
 *    relevant accounting information.  This function returns the allocated
 *    fragment address or 0 if no block is available in the filesystem.
 */
ULONG
block_allocate(ULONG nearblock)
{
    int        block;
    ULONG      cgx;
    ULONG      bfree;
    ULONG      index;
    int        updated = superblock->fs_flags & FS_FLAGS_UPDATED;
    struct cg *mycg;

    cgx  = dtog(superblock, nearblock);
    mycg = cache_cg(cgx);

    if ((updated && superblock->fs_new_cstotal.cs_nbfree[is_big_endian] == 0) ||
        (!updated && superblock->fs_cstotal.cs_nbfree == 0)) {
        PRINT(("no blocks available in filesystem\n"));
        return (0);
    }

    block = block_free_find(mycg, dtogd(superblock, nearblock));

    if (block == -1) {      /* then we must find cg with least blks used */
        ULONG ncg = DISK32(superblock->fs_ncg);

        bfree = 0;
        for (index = 0; index < ncg; index++) {
            ULONG temp = DISK32(superblock->fs_cs(superblock, index).cs_nbfree);
            if (bfree < temp) {
                bfree = temp;
                cgx   = index;
                PRINT(("  CG%u has %u blocks available\n", cgx, bfree));
            }
        }

        if (bfree == 0) {
            PRINT2(("INCON: block_allocate: no space free in fs\n"));
            superblock->fs_ronly = 1;
            return (0);
        }

        mycg  = cache_cg(cgx);
        block = block_free_find(mycg, 0);

#ifndef FAST
        if (block == -1) {
            PRINT2(("INCON: cg %u should have block free\n", cgx));
            superblock->fs_ronly = 1;
            return (0);
        }
#endif
    }

    PRINT(("balloc(~%u, cg=%u) -> %u\n", nearblock, cgx, block));

    mycg = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("balloc: cache_cg_write(%u) gave NULL", cgx));
        return (0);
    }
#endif
    block_alloc(cg_blksfree(mycg), block);
    adjust_blks_free(mycg, cgx, block, -1);
    ffs_clusteracct(mycg, block, -1);

    return (block + cgbase(superblock, cgx));
}

void
block_deallocate(ULONG block)
{
    ULONG      cgx;
    ULONG      cblock;
    struct cg *mycg;

/*  PRINT(("block_deallocate %u\n", block)); */

    cgx  = dtog(superblock, block);
    mycg = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("bdealloc: blk %u cache_cg_write gave NULL", block));
        return;
    }
#endif

    cblock = dtogd(superblock, block);

    block_dealloc(cg_blksfree(mycg), cblock);
    adjust_blks_free(mycg, cgx, cblock, 1);
    ffs_clusteracct(mycg, cblock, 1);
}

static void
adjust_frags_free(struct cg *mycg, ULONG cgx, long adjust)
{
#ifdef BOTHENDIAN
    superblock->fs_cs(superblock, cgx).cs_nffree =
        DISK32(DISK32(superblock->fs_cs(superblock, cgx).cs_nffree) + adjust);
    mycg->cg_cs.cs_nffree =
        DISK32(DISK32(mycg->cg_cs.cs_nffree) + adjust);

    superblock->fs_cstotal.cs_nffree =
        DISK32(DISK32(superblock->fs_cstotal.cs_nffree) + adjust);
    if (superblock->fs_flags & FS_FLAGS_UPDATED) {
        superblock->fs_new_cstotal.cs_nffree[is_big_endian] =
            DISK32(DISK32(superblock->fs_new_cstotal.cs_nffree[is_big_endian]) +
            adjust);
    }
#else
    superblock->fs_cs(superblock, cgx).cs_nffree += adjust;
    mycg->cg_cs.cs_nffree                        += adjust;

    superblock->fs_cstotal.cs_nffree             += adjust;
    if (superblock->fs_flags & FS_FLAGS_UPDATED)
        superblock->fs_new_cstotal.cs_nffree[1]  += adjust;
#endif
    superblock->fs_fmod++;
}

static void
adjust_frsum(struct cg *mycg, int count, long adjust)
{
#ifdef BOTHENDIAN
    if (count)
        mycg->cg_frsum[count] = DISK32(DISK32(mycg->cg_frsum[count]) + adjust);
#else
    if (count)
        mycg->cg_frsum[count] += adjust;
#endif
}

int
frags_allocate(ULONG fragstart, ULONG frags)
{
    int        index;
    int        fragoffset;
    int        count_low  = 0;  /* Free frags below start in block */
    int        count_high = 0;  /* Free frags after end in block */
    ULONG      cgfragstart;
    ULONG      cgblkstart;
    ULONG      blkstart;
    ULONG      cgx;
    struct cg *mycg;

    PRINT(("frags_allocate: start=%d frags=%d\n", fragstart, frags));

    cgx  = dtog(superblock, fragstart);
    mycg = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("falloc: frag %u cache_cg_write gave NULL", fragstart));
        return (1);
    }
#endif

    cgfragstart = dtogd(superblock, fragstart);
    fragoffset  = cgfragstart % FRAGS_PER_BLK;
    cgblkstart  = cgfragstart - fragoffset;
    blkstart    =   fragstart - fragoffset;

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
    adjust_frsum(mycg, count_low, 1);
    adjust_frsum(mycg, count_high, 1);

    if (frags + count_low + count_high == FRAGS_PER_BLK) {
        /* it was a completely empty block */
        cgblkstart = block_allocate(blkstart);
        if (cgblkstart != blkstart) {
            PRINT2(("INCON: blk %u should be free for split, but isn't!"
                    "  Got %u instead\n", blkstart, cgblkstart));
            PRINT2(("       frags=%u count_low=%d count_high=%u FPB=%u\n",
                    frags, count_low, count_high, FRAGS_PER_BLK));

            /* Roll back changes */
            if (cgblkstart)
                block_deallocate(cgblkstart);
            adjust_frags_free(mycg, cgx, frags);
            adjust_frsum(mycg, count_low, -1);
            adjust_frsum(mycg, count_high, -1);
            superblock->fs_ronly = 1;
            return (1);
        }

        /* deallocate unused block fragments */
        for (index = 0; index < count_low; index++)
            bit_set(cg_blksfree(mycg), cgfragstart - index - 1);
        for (index = 0; index < count_high; index++)
            bit_set(cg_blksfree(mycg), cgfragstart + frags + index);

        adjust_frags_free(mycg, cgx, FRAGS_PER_BLK - frags);
    } else {
        /* discount old frag */
        adjust_frsum(mycg, frags + count_low + count_high, -1);

        /* allocate the block fragments */
        for (index = 0; index < frags; index++)
            bit_clr(cg_blksfree(mycg), cgfragstart + index);

        adjust_frags_free(mycg, cgx, -frags);
    }
    return (0);
}

/* deallocate requested number of frags starting at fragsstart */
void
frags_deallocate(ULONG fragstart, ULONG frags)
{
    int        index;
    int        fragoffset;
    int        count_low  = 0;
    int        count_high = 0;
    ULONG      cgfragstart;
    ULONG      cgblkstart;
    ULONG      blkstart;
    ULONG      cgx;
    struct cg *mycg;

    if (frags == 0)
        return;

    PRINT(("frags_deallocate: start=%d frags=%d\n", fragstart, frags));

    cgx  = dtog(superblock, fragstart);
    mycg = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("fdealloc: frags %u-%u cache_cg_write gave NULL",
               fragstart, fragstart + frags - 1));
        return;
    }
#endif

    cgfragstart = dtogd(superblock, fragstart);
    fragoffset  = cgfragstart % FRAGS_PER_BLK;
    cgblkstart  = cgfragstart - fragoffset;
    blkstart    =   fragstart - fragoffset;

#ifdef FRAGS_DEALLOCATE_DEBUG
    PRINT(("cgfragstart=%d fragoffset=%d cgblkstart=%d blkstart=%d\n",
           cgfragstart, fragoffset, cgblkstart, blkstart));
#endif

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

#ifdef FRAGS_DEALLOCATE_DEBUG
    PRINT(("dealloc: low=%d frags=%d high=%d\n",
            count_low, frags, count_high));
#endif

    /* reduce summary info regarding old surrounding frag sizes */
    adjust_frsum(mycg, count_low, -1);
    adjust_frsum(mycg, count_high, -1);

    if (frags + count_low + count_high == FRAGS_PER_BLK) {
        /* entire block is now available */
        block_deallocate(blkstart);
        adjust_frags_free(mycg, cgx, frags - FRAGS_PER_BLK);
    } else {
        /* coalesce with other frags in block, both before and after */
        adjust_frsum(mycg, frags + count_low + count_high, 1);

        /* free the block fragments */
        for (index = 0; index < frags; index++)
            bit_set(cg_blksfree(mycg), cgfragstart + index);
        adjust_frags_free(mycg, cgx, frags);
    }
}

#endif  /* !RONLY */
