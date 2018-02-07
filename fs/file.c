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

#include <clib/exec_protos.h>
#include <exec/memory.h>
#include <dos/dos.h>
#ifndef GCC
#include <string.h>
#endif

#include "config.h"
#include "fsmacros.h"
#include "handler.h"
#include "ufs.h"
#include "superblock.h"
#include "ufs/inode.h"
#include "file.h"
#include "cache.h"
#include "alloc.h"
#include "inode.h"
#include "dir.h"

#define MAX_PATH_LEVEL_LEN 8191

#undef DIR_DEBUG
#ifdef DIR_DEBUG
#define DDPRINT(x) PRINT(x)
#else
#define DDPRINT(x)
#endif

#undef DEBUG_FILE_DEALLOCATE
#ifdef DEBUG_FILE_DEALLOCATE
#define FDPRINT(x) PRINT(x)
#else
#define FDPRINT(x)
#endif


extern int resolve_symlinks;    /* 1=resolve sym links for AmigaDOS */
extern int unix_paths;          /* 1=Use Unix pathnames 0=AmigaDOS pathnames */
extern int read_link;           /* 1=currently reading a link */

char    *linkname;              /* buffer holding path of resolved symlink */

static char *
nextslash(char *str)
{
    while (*str != '\0') {
        if (*str == '/') {
            *str = '\0';
            return (str + 1);
        } else {
            str++;
        }
    }
    return (str);
}


/*
 * file_find()
 *    This routine, given a parental inode number and pathname, will
 *    return the inode number of the basename within the path of the
 *    pathname.  If no file is found, the return inode number is zero.
 *
 *    This is a focal routine for the filesystem, as it is the main
 *    interface to the path resolution routines.
 */
int
file_find(ULONG pinum, char *name)
{
    ULONG      finum;
    int        namelen;
    int        templen;
    int        inosize;
    int        passes = 0;
    char      *strend;
    char      *current;
    char      *filename;
    char      *next;
    char      *buf;
    icommon_t *inode;

    namelen  = strlen(name) + 1;
    filename = (char *) AllocMem(namelen, MEMF_PUBLIC);
    if (filename == NULL) {
        PRINT2(("ff: unable to allocate %d bytes\n", namelen));
        global.Res2 = ERROR_NO_FREE_STORE;
        return (0);
    }
    strcpy(filename, name);

    finum   = pinum;
    current = filename;

    if (unix_paths && (*current == '/')) {
        finum = 2;
        current++;
    }

    strend = current + strlen(current);
    if (current == strend)
        finum = dir_name_find(finum, ".");

    while (current < strend) {
        next = nextslash(current);
        DDPRINT(("nextslash from %s to %s\n", current, next));

        if (!unix_paths && (*current == '\0')) {
            finum = dir_name_find(pinum, "..");
        } else if ((finum = dir_name_find(pinum, current)) == NULL) {
            break;
        }

        inode = inode_read(finum);
#ifndef FAST
        if (inode == NULL) {
            PRINT2(("ff: iread gave NULL\n"));
            return (0);
        }
#endif
        if ((DISK16(inode->ic_mode) & IFMT) == IFLNK) {
            /* This is a symbolic link */
            if (!resolve_symlinks && !read_link) {
                FreeMem(filename, namelen);
                global.Res2 = ERROR_IS_SOFT_LINK;
                return (0);
            }

            inosize = IC_SIZE(inode);
            templen = inosize + strlen(next) + 2;
            if (templen > MAX_PATH_LEVEL_LEN) {
                FreeMem(filename, namelen);
                global.Res2 = ERROR_IS_SOFT_LINK;
                return (0);
            }

            buf = (char *) AllocMem(templen, MEMF_PUBLIC);
            if (buf == NULL) {  /* error: hit symlink and no memory */
                global.Res2 = ERROR_NO_FREE_STORE;
                break;
            }

            if (bsd44fs && (inosize < MAXSYMLINKLEN))
                strcpy(buf, (char *) inode->ic_db);
            else
                file_read(finum, 0, inosize, buf);

            buf[inosize] = '/';
            strcpy(buf + inosize + 1, next);
            FreeMem(filename, namelen);
            namelen  = templen;
            filename = buf;

            if (read_link) {                /* return link name */
                filename[namelen - 2] = '\0';
                PRINT(("readlink %s\n", filename));
                linkname = filename;
                return (finum);
                /*
                 * Note buf mem is deallocated by ReadLink routine
                 * using linkname global.
                 */
            }

            /* transition back to root or parent directory */
            if (*buf == '/') {
                if (unix_paths)
                    finum = 2;
                else
                    finum = dir_name_find(pinum, "..");
                next = buf + 1;
            } else if (*buf == ':') {
                finum = 2;
                next  = buf + 1;
            } else {
                next  = buf;  /* which will set current = buf */
                finum = pinum;
            }

            strend = filename + strlen(filename);

        } else if (DISK16(inode->ic_mode) & IFCHR) {
            /* Character or block device */
            break;
        }

        current = next;
        if (passes++ > 127) {
            PRINT2(("ff: Too many levels"));
            finum = 0;
            global.Res2 = ERROR_TOO_MANY_LEVELS;
            break;
        }
        pinum = finum;
    }

    FreeMem(filename, namelen);
    return (finum);
}

int
dir_name_find(ULONG pinum, char *name)
{
    int inum;

    if (*name == '\0')
        inum = pinum;
    else if ((*name == '.') && (*(name + 1) == '\0'))
        inum = pinum;
    else
        inum = dir_name_search(pinum, name);

    DDPRINT(("dnf: %u \"%s\" = %u\n", pinum, name, inum));
    global.Pinum = pinum;

    /* Check for "" or "." or ".." */
    if ((*name == '\0') ||
        ((*name == '.') &&
         ((name[1] == '\0') ||
          ((name[1] == '.') && (name[2] == '\0'))))) {
        global.Pinum   = dir_name_search(inum, "..");
        global.Poffset = dir_inum_search(global.Pinum, inum);
    }

    return (inum);
}

#ifndef RONLY
static ULONG *level_buffer[4];

static int
iblock_deallocate(int level, ULONG fragaddr)
{
    int index;
    int deallocated = 0;

    PRINT(("iblock_deallocate: level=%d fragaddr=%d\n", level, fragaddr));

    /* Tell the cache to discard these frags */
    for (index = 0; index < FRAGS_PER_BLK; index++)
        cache_invalidate(fragaddr + index);

    if (data_read(level_buffer[level], fragaddr, FBSIZE)) {
        PRINT2(("** iblock_deallocate: failed block read, run fsck\n"));
        superblock->fs_ronly = 1;
        return (0);
    }

    for (index = 0; index < DISK32(superblock->fs_nindir); index++) {
        if (level_buffer[level][index] != 0) {
            ULONG iblock = DISK32(level_buffer[level][index]);
            if (level > 0)
                deallocated += iblock_deallocate(level - 1, iblock);
            block_deallocate(iblock);
            deallocated++;
        }
    }
    return (deallocated);
}

/*
 * file_deallocate()
 *    This routine will deallocate all blocks (including index blocks) for
 *    the file pointed to by the specified inode number.  The inode pointers
 *    will be cleared as the inode is deallocated.  This function returns 0
 *    on success and 1 on failure.
 */
int
file_deallocate(ULONG inum)
{
    int        index;
    int        ffrags;
    int        block;
    int        rc;
    ULONG      totfrags;
    ULONG      ibs[NIADDR];
    icommon_t *inode;

    inode = inode_modify(inum);

#ifndef FAST
    if (inode == NULL) {
        PRINT2(("fd: bad buffer from inode_modify\n"));
        return (1);
    }
#endif

    FDPRINT(("file_deallocate %u\n", inum));

    totfrags = numfrags(superblock, fragroundup(superblock, IC_SIZE(inode)));

    /* if file is larger, allocation is granular only to entire fs blocks */
    if (totfrags / FRAGS_PER_BLK < NDADDR) {
        ffrags  = fragnum(superblock, totfrags);
        block   = ridisk_frag(totfrags - ffrags, inum);

        FDPRINT(("i=%u size=%u blocks=%u totfrags=%d ffrags=%d block=%d\n",
                 inum, IC_SIZE(inode), DISK32(inode->ic_blocks),
                 totfrags, ffrags, block));

        inode = inode_modify(inum);
        if (ffrags && block) {
            inode->ic_db[totfrags / FRAGS_PER_BLK] = 0;
            frags_deallocate(block, ffrags);
        }
    }

    inode = inode_modify(inum);
    for (index = 0; index < NDADDR; index++) {
        if (inode->ic_db[index]) {
            FDPRINT(("del blk %u\n", DISK32(inode->ic_db[index])));

            block_deallocate(DISK32(inode->ic_db[index]));
            inode = inode_modify(inum);
            inode->ic_db[index] = 0;
        }
    }

    for (index = 0; index < NIADDR; index++) {
        ibs[index] = DISK32(inode->ic_ib[index]);
        inode->ic_ib[index] = 0;
    }

    for (index = 0; index < NIADDR; index++) {
        level_buffer[index] = (ULONG *) AllocMem(FBSIZE, MEMF_PUBLIC);
        if (level_buffer[index] == NULL) {
            /*
             * Can't deallocate all of file - need to use fsck to
             * recover lost blocks in the filesystem.
             */
            PRINT2(("fd: Unable to alloc temp mem\n"));
            rc = 1;
            goto cleanup_return;
        }
        if (ibs[index]) {
            iblock_deallocate(index, ibs[index]);
            block_deallocate(ibs[index]);
        }
    }
    rc = 0;
cleanup_return:
    for (index = 0; index < NIADDR; index++) {
        if (level_buffer[index] != NULL) {
            FreeMem(level_buffer[index], FBSIZE);
            level_buffer[index] = NULL;
        }
    }

    return (rc);
}

/*
 * file_blocks_add()
 *    This routine will add more filesystem blocks to the allocation for
 *    the specified inode.  The number of filesystem blocks actually
 *    allocated will be returned to the calling routine.
 *
 *    Policy is as follows:
 *    o  If first block in the file, allocate a block near the inode
 *       in the inode's cg.
 *    o  If direct block in the inode, allocate closest to the
 *       previous block.
 *    o  If first indirect block in the inode, allocate in another cg.
 *    o  If indirect block in the inode, allocate closest to the
 *       previous indirect block.
 *
 *    NEEDS WORK to complete the policy....should allocate from another
 *    cg (one which has the fewest blocks allocated) if stepping across
 *    the line from direct blocks to first indirect index block.
 *
 *    inode is file inode
 *    inum is file inode number
 *    startfrag is file start fragment
 *    blocks is number of fs blocks to allocate
 */
int
file_blocks_add(icommon_t *inode, ULONG inum, ULONG startfrag,
                ULONG blocks)
{
    ULONG      prev_frag = 0;
    ULONG      index;
    ULONG      cgx;
    ULONG      blockgot;
    struct cg *mycg;

    PRINT(("fba: i=%u start=%u blocks=%u\n", inum, startfrag, blocks));

    if (startfrag > 0)
        for (index = startfrag - 1; index != -1; index--)
            if ((prev_frag = ridisk_frag(index, inum)) != 0)
                break;

    if (prev_frag == 0) {
        prev_frag = itod(superblock, inum);
        PRINT(("no prev frag found, set to %u\n", prev_frag));
    }

    cgx  = dtog(superblock, prev_frag);
    mycg = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("fba: inum %u cache_cg_write gave NULL", inum));
        return (0);
    }
#endif
    for (index = 0; index < blocks; index++) {
        /* check already alloced */
        if (ridisk_frag(startfrag, inum))
            continue;
        blockgot = block_allocate(prev_frag);
        if (blockgot == 0) {
            PRINT(("fba: failed to allocate all requested blocks\n"));
            break;
        }
        inode = inode_modify(inum);
        ADJUST_IC_BLKS(inode, NDSPB);
        cidisk_frag(startfrag, inum, blockgot);
        prev_frag = blockgot;
        startfrag += FRAGS_PER_BLK;
    }
    return (index);
}


/*
 * file_block_extend()
 *    This routine will, given an inode, assure the last block is allocated
 *    as a full block.  This simplifies and speeds the allocation policies.
 *    If the machine crashes before the file is closed, fsck must be run to
 *    recover space.
 *
 *    This scheme of allocation is bad if the filesystem must rapidly open
 *    and close files, due to the copying involved in moving blocks.  A
 *    sufficient cache will greatly alleviate the overhead because it can
 *    dynamically remap blocks before they are written to media.
 *
 *    This is not all that bad.  The number of blocks allocated to the file
 *    will remain high after a crash, but the next time the file is opened
 *    and closed for write, it will be cleaned up automatically.
 *
 *    If the number of frags allocated to the file is an even multiple of
 *    the number of blocks (meaning no frags), then nothing will be done
 *    to the file.
 */
int
file_block_extend(ULONG inum)
{
    int        index;
    ULONG      cgx;
    ULONG      dblk;
    ULONG      fblk;
    ULONG      frags;
    ULONG      fspill;
    ULONG      newblk;
    struct cg *mycg;
    icommon_t *inode;

    inode = inode_read(inum);
#ifndef FAST
    if (inode == NULL) {
        PRINT2(("fbe: iread gave NULL\n"));
        return (1);
    }
#endif
    frags  = DISK32(inode->ic_blocks) / NDSPF;
    fspill = frags % FRAGS_PER_BLK;

    PRINT(("fbe: inum=%u curfrags=%u fspill=%u\n", inum, frags, fspill));

    if (fspill == 0)
        return (0);      /* block is already full size */

    fblk = frags - fspill;
    dblk = ridisk_frag(fblk, inum);
    cgx  = dtog(superblock, dblk);
    mycg = cache_cg(cgx);

    PRINT(("fbe: last file block=%d, frag_addr=%d, fspill=%d; cg=%d\n",
            fblk, dblk, fspill, cgx));

    if (((dblk % FRAGS_PER_BLK) == 0) &&
        frags_left_are_free(cg_blksfree(mycg),
                            dtogd(superblock, dblk + fspill))) {
        /*
         * Allocation of the last block started on a block boundary
         * and the remaining disk frags are free in that block.
         */
        PRINT(("%d in use, allocating %d more in block %u\n",
               fspill, FRAGS_PER_BLK - fspill, dblk));

        if (frags_allocate(dblk + fspill, FRAGS_PER_BLK - fspill))
            return (1);
    } else {
        /* we must move (fspill) fragments to a new block */
        if (fblk)
            newblk = block_allocate(ridisk_frag(fblk - 1, inum));
        else
            newblk = block_allocate(itod(superblock, inum));

        if (newblk == 0) {
            PRINT2(("fbe: unable to allocate space for WRITE\n"));
            return (1);
        }

        for (index = 0; index < fspill; index++)
            cache_frag_move(newblk + index, dblk + index);

        /* point inode at new spot */
        cidisk_frag(fblk, inum, newblk);
        frags_deallocate(dblk, fspill);
    }

    inode = inode_modify(inum);
    ADJUST_IC_BLKS(inode, (FRAGS_PER_BLK - fspill) * NDSPF);
    return (0);
}

/*
 * file_blocks_deallocate()
 *    This routine will deallocate file blocks starting at the
 *    specified BLOCK address.  WARNING: This routine does not
 *    handle odd blocks at the end of a file.
 */
void
file_blocks_deallocate(ULONG inum)
{
    int        index;
    int        level;
    ULONG      blkstart;
    ULONG      fragcur;
    ULONG      ptrcur;
    ULONG      deallocated = 0;
    ULONG      curblk;
    ULONG      curdelblk;
    ULONG      startblk;
    ULONG      fragno[NIADDR];
    ULONG      ptrnum[NIADDR];
    ULONG     *temp;
    icommon_t *inode;

    inode    = inode_modify(inum);
    startblk = (IC_SIZE(inode) + FBSIZE - 1) / FBSIZE;

    for (blkstart = startblk; blkstart < NDADDR; blkstart++) {
        if (inode->ic_db[blkstart]) {
            PRINT(("fbd: removing direct %d addr %d\n",
                    blkstart, DISK32(inode->ic_db[blkstart])));
            block_deallocate(DISK32(inode->ic_db[blkstart]));
            inode->ic_db[blkstart] = 0;
            deallocated++;
        }
    }

    blkstart -= NDADDR;

    level = 0;

    do {
        if (level)
            blkstart--;
        ptrnum[level] = blkstart & pfragmask;
        blkstart    >>= pfragshift;
        fragno[level] = blkstart & fblkmask;
        blkstart    >>= fblkshift;
        level++;
    } while (blkstart > 0);

    PRINT(("fbd: now we have our start: l=%d\n", level));

    for (index = 0; index < NIADDR; index++) {
        level_buffer[index] = (ULONG *) AllocMem(FBSIZE, MEMF_PUBLIC);
        if (level_buffer[index] == NULL) {
            PRINT2(("fbd: Unable to allocate temp space\n"));
            for (index--; index >= 0; index--) {
                FreeMem(level_buffer[index], FBSIZE);
                level_buffer[index] = NULL;
            }
            return;
        }
    }

    /*
     * If we start deallocating in direct blocks, just deallocate
     * all of the trees.
     */
    if (startblk < NDADDR) {
        level = 0;
        goto deallocate_trees;
    }

    /*
     * Algorithm: seek last file block, along the way deallocating
     * other trees which are higher than the current index.  At last
     * file block, deallocate all blocks in that level greater than
     * that file block position.  iblock_deallocate routine will
     * record the number of blocks actually deallocated for us.
     */

    curblk = DISK32(inode->ic_ib[level - 1]);
    if (curblk == 0)
        goto deallocate_trees;

    index = level;
    do {
        index--;

        ptrcur = ptrnum[index] + 1;
        for (fragcur = fragno[index]; fragcur < FRAGS_PER_BLK; fragcur++) {
            temp = (ULONG *) cache_frag_write(curblk + fragcur, 1);
#ifndef FAST
            if (temp == NULL) {
                PRINT2(("fbd: cache_write gave NULL\n"));
                return;
            }
#endif
            for (; ptrcur < DISK32(superblock->fs_nindir) / FRAGS_PER_BLK;
                 ptrcur++) {
                if (temp[ptrcur]) {
                    curdelblk = DISK32(temp[ptrcur]);
                    if (index)
                        deallocated += iblock_deallocate(index - 1, curdelblk);
                    block_deallocate(curdelblk);
                    temp[ptrcur] = 0;
                    deallocated++;
                }
            }
            ptrcur = 0;
        }
        temp = (ULONG *) cache_frag_write(curblk + fragno[index], 1);
#ifndef FAST
        if (temp == NULL) {
            PRINT2(("fbd: cache_write gave NULL\n"));
            return;
        }
#endif
        curblk = DISK32(temp[ptrnum[index]]);
        if (curblk == 0)
            goto deallocate_trees;

    } while (index > 0);

    block_deallocate(curblk);
    temp[ptrnum[index]] = 0;
    deallocated++;

deallocate_trees:
    /* deallocate other inode trees */

    PRINT(("fbd: dealloc trees, start=%d\n", level));
    for (index = level; index < NIADDR; index++)
        if (inode->ic_ib[index]) {
            ULONG iblock = DISK32(inode->ic_ib[index]);
            deallocated += iblock_deallocate(index, iblock);
            block_deallocate(iblock);
            deallocated++;
            inode->ic_ib[index] = 0;
        }

    for (index = 0; index < NIADDR; index++) {
        FreeMem(level_buffer[index], FBSIZE);
        level_buffer[index] = NULL;
    }

    inode = inode_modify(inum);
    ADJUST_IC_BLKS(inode, -NDSPB * deallocated);

    PRINT(("fbd: deallocated final=%u ic_blocks=%u\n", deallocated,
           DISK32(inode->ic_blocks)));
}

/*
 * file_block_retract()
 *    This routine will retract retract the size of the file to the size
 *    specified in the inode.
 *    o  The file must have its last block completely allocated before calling
 *    o  The amount of space to deallocate must exist within one filesystem
 *            block.  You should call file_blocks_deallocate() if you wish to
 *            deal with more than just the last file block.
 *    o  This routine automatically relocates partial blocks as necessary.
 *    o  If the size of the file exceeds that which can be recorded solely
 *            by the inode direct addresses, then this trimming is not done.
 *            This is per the apparent (read: undocumented) BSD FFS (and fsck)
 *            specification.
 */
int
file_block_retract(int inum)
{
    int        index;
    int        cgx;
    ULONG      reqfrags;  /* Required frags in last block */
    ULONG      curfrags;  /* Current frags in inode ic_blocks field */
    ULONG      totfrags;
    ULONG      dblk;
    ULONG      fdblk;
    ULONG      pblk;
    ULONG      newblk;
    ULONG      newblkfrags;
    ULONG      newdblk;
    struct cg *mycg;
    icommon_t *inode;

    inode    = inode_modify(inum);
    totfrags = numfrags(superblock, fragroundup(superblock, IC_SIZE(inode)));
    reqfrags = fragnum(superblock, totfrags);
    curfrags = fragnum(superblock, (DISK32(inode->ic_blocks) / NDSPF));

    PRINT(("fbr: i=%u filesize=%u totfrags=%u reqfrags=%u curfrags=%u\n",
           inum, IC_SIZE(inode), totfrags, reqfrags, curfrags));

    if ((reqfrags == 0) || (totfrags > FRAGS_PER_BLK * NDADDR)) {
        /* No need to shrink as file size is 0 or > than direct blocks */
        return (0);
    }

#ifndef FAST
    if (totfrags > DISK32(inode->ic_blocks) * NDSPF) {
        /*
         * This could happen if another filesystem (not BFFS) creates
         * a sparse file...
         */
        PRINT2(("INCON: inum=%u size=%u frags=%u > ic_blocks frags %u\n",
                inum, IC_SIZE(inode), totfrags,
                DISK32(inode->ic_blocks) * NDSPF));
        return (1);
    }

    if (reqfrags == curfrags) {
        PRINT2(("fbr: inum=%u caught required frags %u same as size\n",
                inum, reqfrags));
        return (1);
    }
#endif

    fdblk = ridisk_frag(blknum(superblock, totfrags), inum);
    if (fdblk == 0) {
        /*
         * Last block has a hole.  This should not happen with current
         * BFFS code, but might have happened if written by another
         * filesystem.
         */
        return (0);
    }
    inode   = inode_modify(inum);
    ADJUST_IC_BLKS(inode, -NDSPF * (FRAGS_PER_BLK - reqfrags));

    cgx     = dtog(superblock, fdblk);
    mycg    = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("fbr: inum %u cache_cg_write gave NULL", inum));
        return (1);
    }
#endif
    dblk    = dtogd(superblock, fdblk);
    if (totfrags < FRAGS_PER_BLK)
        pblk = dtogd(superblock, itod(superblock, inum));
    else
        pblk = dtogd(superblock, ridisk_frag(fdblk - FRAGS_PER_BLK, inum));

    PRINT(("fbr: inum=%u cg=%u d=%u p=%u\n", inum, cgx, dblk, pblk));
    PRINT(("attempting to relocate partial block size=%u\n", reqfrags));

    newblk = block_fragblock_find(mycg, pblk, reqfrags);
    if (newblk != -1) {
        newblkfrags = newblk % FRAGS_PER_BLK;
        newdblk     = newblk + cgbase(superblock, cgx);

        PRINT(("fbr: will realloc to block %u, %u pieces\n",
               newblk, newblkfrags));

        /* if found = full block, leave data where it is, dealloc extra */
        if ((newblkfrags == 0) &&
            (block_avail(cg_blksfree(mycg), newblk))) {
            PRINT(("fbr: got full block, don't want it\n"));
            goto dealloc_extra;
        }

        /* check if given the same block as we already have */
        if (newdblk - newblkfrags == fdblk - fdblk % FRAGS_PER_BLK) {
            /*
             * Should not happen because entire block should be
             * allocated to file at this point.
             */
            PRINT2(("INCON: fbr got same block %u[%u]\n",
                    newdblk - newblkfrags, cgx));
/*          goto dealloc_extra; */
            superblock->fs_ronly = 1;
            return (0);
        }

        /* allocate frags of that new block */
        if (frags_allocate(newdblk, reqfrags))
            return (1);

        /* do cache frag block moves */
        for (index = 0; index < reqfrags; index++)
            cache_frag_move(newdblk + index, fdblk + index);

        /* free old block */
        block_deallocate(fdblk);

        /* point inode at new location */
        cidisk_frag(totfrags - reqfrags, inum, newdblk);

        return (0);
    }

dealloc_extra:
    PRINT(("fbr: freeing %u frags in %u (ic_blocks=%u)\n",
           FRAGS_PER_BLK - reqfrags, dblk, DISK32(inode->ic_blocks)));

    /* deallocate the unused frags in the current block */
    frags_deallocate(fdblk + reqfrags, FRAGS_PER_BLK - reqfrags);
    return (0);
}

/*
 * file_frag_expand()
 *    This routine will use the allocation policies to expand the specified
 *    inum by one fragment.  This routine is called by the directory routines
 *    to give more directory space.  This routine returns the newly allocated
 *    frag address (or 0 if it was not possible to allocate an address)
 *
 *    Policy:
 *    o  If next frag is free and does not spill into another block, then
 *       alloc and return that frag
 *    o  If last block is full block (of just this inode's data), allocate
 *       just one frag for new address
 *    o  Is next frag available in the current block?
 *       This implies that there _is_ a next frag in the current block.
 *       o  If so, then allocate it and set file size - done
 *       o  If not, then find/allocate new fragments (size in old block + 1)
 *          and move old frags plus new frag to this new location.
 *          Deallocate frags used of old location
 */
ULONG
file_frag_expand(ULONG inum)
{
    int        index;
    ULONG      cgx;
    ULONG      dblk;
    ULONG      fblk;
    ULONG      frags;
    ULONG      fspill;
    ULONG      startoff;
    ULONG      cgblk;
    ULONG      newblk;
    int        count;
    int        inosize;
    struct cg *mycg;
    icommon_t *inode;

    inode   = inode_modify(inum);
    inosize = IC_SIZE(inode);
    frags   = DISK32(inode->ic_blocks) / NDSPF;
    fspill  = frags % FRAGS_PER_BLK;
    fblk    = frags - fspill;
    dblk    = ridisk_frag(fblk, inum);
    cgx     = dtog(superblock, dblk);
    mycg    = cache_cg_write(cgx);
#ifndef FAST
    if (mycg == NULL) {
        PRINT2(("fe: inum %u cache_cg_write gave NULL", inum));
        return (0);
    }
#endif
    PRINT(("fe: i=%d size=%d last=%d spill=%d diskaddr=%d cg=%d\n",
            inum, inosize, fblk, fspill, dblk, cgx));

    startoff = dblk % FRAGS_PER_BLK;
    cgblk    = dblk - cgbase(superblock, cgx);

    if (fspill == 0) {
        /* need to alloc one new frag in another block */
        if (fblk)
            count = block_fragblock_find(mycg, cgblk, 1);
        else
            count = block_fragblock_find(mycg, itod(superblock, inum) -
                                         cgbase(superblock, cgx), 1);

        if (count != -1) {                  /* found frag */
            newblk = count + cgbase(superblock, cgx);
            if (frags_allocate(newblk, 1))
                return (0);
        } else {                            /* alloc block elsewhere */
            if (dblk)
                newblk = block_allocate(dblk);
            else
                newblk = block_allocate(itod(superblock, inum));

            if (newblk)
                frags_deallocate(newblk + 1, FRAGS_PER_BLK - 1);
        }

        if (newblk) {
            inode = inode_modify(inum);
            ADJUST_IC_BLKS(inode, NDSPF);
            ADJUST_IC_SIZE(inode, FSIZE);

            if (fblk)
                cidisk_frag(fblk, inum, newblk);
            else
                cidisk_frag(0, inum, newblk);
        } else {
            PRINT(("fe: unable to allocate block - no space free\n"));
        }

        return (newblk);

    } else if (startoff + fspill < FRAGS_PER_BLK)  {
        /* allocate fragment in existing block */
        PRINT(("fe: Looking for more space in current block %d %d\n",
               cgblk + fspill, dblk + fspill));
        if (bit_val(cg_blksfree(mycg), cgblk + fspill)) {
            if (frags_allocate(dblk + fspill, 1))
                return (0);

            inode = inode_modify(inum);
            ADJUST_IC_BLKS(inode, NDSPF);
            ADJUST_IC_SIZE(inode, FSIZE);

            PRINT(("fe: returning %d\n", dblk + fspill));
            return (dblk + fspill);
        }
        PRINT(("fe: none found\n"));
    }

    /*
     * Situation: fspill != 0, but the block is full.  This means that
     *            file is sharing space with other files in same block
     * Solution:  Allocate frags + 1 and copy contents of old to the new
     *            place.  free the space allocated in the old block
     */

#ifndef FAST
    if (dblk == 0) {
        PRINT2(("fe: Error, dblk was 0, but trying to move frags\n"));
        return (0);
    }
#endif

    if (fspill + 1 == FRAGS_PER_BLK)
        count = -1;
    else
        count = block_fragblock_find(mycg, cgblk, fspill + 1);

    if (count != -1) {                              /* found frag */
        newblk = count + cgbase(superblock, cgx);
        if (frags_allocate(newblk, fspill + 1))
            return (0);
    } else {                                /* alloc block elsewhere */
        newblk = block_allocate(dblk);
        if (newblk)
            frags_deallocate(newblk + fspill + 1, FRAGS_PER_BLK - fspill - 1);
        else
            PRINT(("fe: failed to allocate a new block - no space\n"));
    }

    if (newblk) {
        inode = inode_modify(inum);
        ADJUST_IC_BLKS(inode, NDSPF);
        ADJUST_IC_SIZE(inode, FSIZE);

        for (index = 0; index < fspill; index++)
            cache_frag_move(newblk + index, dblk + index);

        frags_deallocate(dblk, fspill);
        cidisk_frag(fblk, inum, newblk);
    } else {
        PRINT(("fe: unable to allocate block - no space free\n"));
        return (0);
    }

    return (newblk + fspill);
}

/* Delete symlink given inum and inode size */
void
symlink_delete(ULONG inum, ULONG size)
{
    if (!bsd44fs || (size >= MAXSYMLINKLEN))
        file_deallocate(inum);
}

int
symlink_create(ULONG inum, icommon_t *inode, char *contents)
{
    int namelen = strlen(contents);

    if (bsd44fs && (namelen < MAXSYMLINKLEN)) {
        inode = inode_modify(inum);
        strcpy((char *) inode->ic_db, contents);
        SET_IC_SIZE(inode, namelen);
        return (1);
    }

    if (file_write(inum, 0, namelen + 1, contents) == namelen + 1) {
        file_block_retract(inum);
        return (1);
    }
    return (0);
}
#endif
