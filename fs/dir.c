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

#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include "config.h"
#include "superblock.h"
#include "ufs.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "file.h"
#include "handler.h"
#include "cache.h"
#include "inode.h"
#include "dir.h"

#define DIRNAMELEN 107

#undef DIR_DEBUG
#ifdef DIR_DEBUG
#define DDPRINT(x) PRINT(x)
#else
#define DDPRINT(x)
#endif


/*
 * streqv()
 *      Perform a case-insensitive compare of filenames, stopping at AmigaOS
 *      maximum file name length.
 */
int
streqv(const char *str1, const char *str2)
{
#ifdef NO_STRNICMP
    int count = 0;

    while (*str1 != '\0') {
        if ((*str1 != *str2) && ((*str1 | 32) != (*str2 | 32)))
            return (0);
        str1++;
        str2++;
        if (++count == DIRNAMELEN)
            return (0);
    }

    if (*str2 == '\0')
        return (1);
    else
        return (0);
#else
    return (strnicmp(str1, str2, DIRNAMELEN) == 0);
#endif
}

/*
 * This set of routines will only modify directory files.  They do not
 * handle allocation, except when more directory space is required.
 */

#ifndef RONLY
/*
 * dir_fragfill()
 *      WARNING: This routine will modify the last frag in a dir block
 *      to have blank directory entries starting at the spill position.
 */
static void
dir_fragfill(int spill, char *buf)
{
    int index;

    for (index = spill; index < FSIZE; index += DIRBLKSIZ) {
        direct_t *ptr = (direct_t *) (buf + index);
        ptr->d_ino     = 0;
        ptr->d_namlen  = 0;
        ptr->d_type    = 0;
        ptr->d_reclen  = DISK16(DIRBLKSIZ);
        ptr->d_name[0] = '\0';
    }
}


/*
 * dir_create()
 *      This routine will add a name to the inode specified by the inode
 *      number passed to this routine.  If the directory needs to be
 *      expanded to accommodate the filename, this is handled automatically.
 *      The algorithm is as follows:
 *              Read directory, one block at a time
 *              If at beginning of block and inode=0, add file there
 *              If reclen of new entry < (reclen of current - actual)
 *                      adjust reclen of current, add entry after current,
 *                      size = remainder
 *              If entire dir full, call allocate routine for another block
 *                      near last dir block allocated.
 *      On success, this routine returns the directory offset where the
 *      new directory entry is to be found.  It will return 0 on error.
 */
int
dir_create(ULONG pinum, char *name, ULONG inum, int type)
{
    int        blk;
    int        spill;
    int        fblks;
    int        needed;
    int        oldsize;
    int        newfrag;
    char      *buf;
    icommon_t *pinode;
    direct_t  *bend;
    direct_t  *current;

    PRINT(("dirc: pinum=%d name=%s inum=%d\n", pinum, name, inum));

    if (pinum < 2) {
        PRINT2(("dc: bad parent inum %d\n", pinum));
        return (0);
    }

    pinode = inode_read(pinum);
    if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
        return (0);

    needed = (strlen(name) + sizeof (direct_t) - MAXNAMLEN - 1 + 3) & ~3;
    if (needed < 0) {
        PRINT2(("dirc: bad filename size %d\n", needed));
        return (0);
    }

    DDPRINT(("name='%s' len=%d needed=%d ", name, strlen(name), needed));

    fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill =  IC_SIZE(pinode)              % FSIZE;

    DDPRINT(("cre: fblks=%d spill=%d size=%u icblks=%u pinum=%u\n", fblks,
            spill, IC_SIZE(pinode), DISK32(pinode->ic_blocks), pinum));

    for (blk = 0; blk < fblks; blk++) {
        DDPRINT(("_"));
        buf = cache_frag(ridisk_frag(blk, pinum));
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_create: cache gave NULL\n"));
            return (0);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        DDPRINT(("current=%p bend=%p\n", current, bend));
        while (current < bend) {
            UWORD dirsize = UFS_DIRSIZ(!bsd44fs, current, !is_big_endian);
            DDPRINT((":%s", current->d_name));

            if ((DISK16(current->d_reclen) - dirsize) > needed) {
                /* Found a place to put the new name */
                buf = cache_frag_write(ridisk_frag(blk, pinum), 1);
#ifndef FAST
                if (buf == NULL) {
                    PRINT2(("dir_create: cache_write gave NULL\n"));
                    return (0);
                }
#endif
                oldsize = DISK16(current->d_reclen);
                current->d_reclen = DISK16(dirsize);
                oldsize -= DISK16(current->d_reclen);
                current = (direct_t *)
                          ((char *) current + DISK16(current->d_reclen));
                goto finish_success;
            }
#ifndef FAST
            if (DISK16(current->d_reclen) > DIRBLKSIZ) {
                PRINT2(("INCON: directory corrupt - reclen %d > %d\n",
                        DISK16(current->d_reclen), DIRBLKSIZ));
                superblock->fs_ronly = 1;
                return (0);
            }
#endif
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

    DDPRINT(("\n"));

    if (spill != 0) {
        /*
         * No space is available in any directory block, but there is
         * space in the last frag for at least one more directory block.
         * Update the inode to expand the directory.
         */
        pinode = inode_modify(pinum);
        PRINT(("inum %u blocks=%u size=%u",
               pinum, DISK32(pinode->ic_blocks), IC_SIZE(pinode)));
        ADJUST_IC_SIZE(pinode, FSIZE - spill);
        PRINT((" -> size=%u\n", IC_SIZE(pinode)));
        blk = fblks - 1;
        newfrag = ridisk_frag(blk, pinum);
        buf = cache_frag_write(newfrag, 1);
    } else {
        /*
         * Searched all directory blocks and no space is available.
         * We must allocate another frag to the directory.
         */
        newfrag = file_frag_expand(pinum);
        if (newfrag == 0) {
            PRINT(("No space left in dir\n"));
            return (0);
        }
        buf = cache_frag_write(newfrag, 0);
    }
#ifndef FAST
    if (buf == NULL) {
        PRINT2(("dir_create: cache write gave NULL\n"));
        return (0);
    }
#endif
    dir_fragfill(spill, buf);
    current = (direct_t *) (buf + spill);
    oldsize = DIRBLKSIZ;

finish_success:
    strcpy(current->d_name, name);
    current->d_ino    = DISK32(inum);
    current->d_reclen = DISK16(oldsize);
    current->d_namlen = strlen(name);
    if (bsd44fs) {
        current->d_type  = type;
    } else {
        current->d_type  = 0;           /* dir type unknown now */
#ifdef BOTHENDIAN
        /* Little endian BSD43 FFS swaps d_namlen and d_type */
        if (is_big_endian == 0) {
            current->d_type   = current->d_namlen;
            current->d_namlen = 0;
        }
#endif
    }
    return (FSIZE * blk + (char *) current - buf);   /* dir offset */
}

/*
 * dir_is_not_empty()
 *      This routine will return true if the passed dir (referenced by
 *      inode number) is not empty (not counting the . and .. entries).
 */
static int
dir_is_not_empty(int pinum)
{
    int        blk;
    int        fblks;
    int        count = 0;
    int        spill;
    char      *buf;
    direct_t  *bend;
    direct_t  *current;
    icommon_t *pinode = inode_read(pinum);
#ifndef FAST
    if (pinode == NULL) {
        PRINT2(("dine: iread gave NULL\n"));
        return (1);
    }
#endif

    if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
        return (0);

    fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill =  IC_SIZE(pinode)              % FSIZE;

    DDPRINT(("dir contents=\n"));
    for (blk = 0; blk < fblks; blk++) {
        buf = cache_frag(ridisk_frag(blk, pinum));
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_is_not_empty: cache gave NULL\n"));
            return (1);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        while (current < bend) {
            if (current->d_ino) {
                DDPRINT(("  %.32s rl=%d\n", current->d_name,
                         DISK16(current->d_reclen)));
                count++;
                if (count > 2) {
                    DDPRINT(("\n"));
                    return (1);
                }
            }
#ifndef FAST
            if ((DISK16(current->d_reclen) > DIRBLKSIZ) ||
                (DISK16(current->d_reclen) == 0)) {
                PRINT2(("INCON: directory corrupt - remove directory?"
                        "  Run fsck\n"));
                /* return 0 to remove dir, 1 to leave it alone */
                return (0);
            }
#endif
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

    return (0);
}

/*
 * dir_offset_delete()
 *      This routine will delete the name of a file from the passed
 *      directory given that name's offset into the directory.  Pass
 *      the inode number of the parent and that file's offset in the
 *      parent.  When given the offset for a directory, the routine
 *      will first check to assure the directory is empty before
 *      erasing the name.  If the check parameter is zero, this check
 *      is skipped and the deletion proceeds unconditionally.
 *
 *      The algorithm is as follows:
 *          Read dir blocks one at a time (into buf)
 *          Once found, extend prior directory entry to cover current
 *          If first in directory block, move a successor to the file
 *              and extend that.
 *          If there is no successor, zero the inode and set its record
 *              length to a directory block size.
 */
int
dir_offset_delete(ULONG pinum, ULONG offset, int check)
{
    ULONG      inum;
    ULONG      blk;
    ULONG      fblks;
    ULONG      spill;
    ULONG      pos;
    ULONG      roffset;
    ULONG      dirblk;
    char      *buf;
    char      *obuf;
    direct_t  *bend;
    direct_t  *last;
    direct_t  *current;
    icommon_t *pinode;

    PRINT(("dir_offset_delete: pinum=%d, offset=%d check=%d\n",
            pinum, offset, check));

    pinode  = inode_read(pinum);
#ifndef FAST
    if (pinode == NULL) {
        PRINT2(("dirod: iread gave NULL\n"));
        return (0);
    }
#endif

    if ((offset == 0) || (offset > IC_SIZE(pinode))) {
        PRINT2(("INCON: dir_offset_delete: impossible offset %u\n", offset));
        superblock->fs_ronly = 1;
        return (0);
    }

    fblks   = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill   =  IC_SIZE(pinode)              % FSIZE;
    roffset = (offset / FSIZE) * FSIZE;

    for (blk = offset / FSIZE; blk < fblks; blk++) {
        dirblk = ridisk_frag(blk, pinum);
        buf    = cache_frag(dirblk);
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_od: cache gave NULL\n"));
            return (0);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        pos  = 0;
        last = NULL;
        while (current < bend) {
            if (pos == DIRBLKSIZ) {
                pos = 0;
                last = NULL;
            }
            if (offset == roffset) {
                inum = DISK32(current->d_ino);
                goto found_object;
            }
            last = current;
#ifndef FAST
            if (DISK16(current->d_reclen) > DIRBLKSIZ) {
                PRINT2(("INCON: dir corrupt: reclen %d > DIRBLKSIZ\n",
                        DISK16(current->d_reclen)));
                superblock->fs_ronly = 1;
                return (0);
            }
#endif
            pos     += DISK16(current->d_reclen);
            roffset += DISK16(current->d_reclen);
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

    /* if we got here, file was not found - this is an error */
    global.Res2 = ERROR_OBJECT_NOT_FOUND;
    return (0);

found_object:

    /*
     * If deleting a directory, first check to make sure there are only
     * two entries left in it; '.' and '..'
     */
    if (check && dir_is_not_empty(inum)) {
        global.Res2 = ERROR_DIRECTORY_NOT_EMPTY;
        PRINT(("directory %d is not empty\n", inum));
        return (0);
    }
    obuf = buf;
    buf  = cache_frag_write(dirblk, 1);

#ifndef FAST
    if (buf != obuf) {
        if (buf == NULL) {
            PRINT2(("INCON: Can't ro->rw inode %d cached blk %d\n",
                    inum, dirblk));
            return (0);
        }
        PRINT(("dir re-cache dirblk %u: dod %08x != %08x\n",
               dirblk, buf, obuf));

        current = (direct_t *) ((char *) current + (buf - obuf));
        if (last != NULL)
            last = (direct_t *) ((char *) last + (buf - obuf));
    }
#endif
    if (last == NULL) { /* at beginning */
        current->d_ino = 0;
    } else {
#ifndef FAST
        if (DISK16(current->d_reclen) > DIRBLKSIZ)
            return (0);
#endif
        last->d_reclen = DISK16(DISK16(last->d_reclen) +
                                DISK16(current->d_reclen));
    }

    return (inum);
}


/*
 * dir_rename()
 *      This routine will rename a file in the passed directory to
 *      the passed new name (possibly) in another directory.  Pass
 *      it from parent, from name, to parent, to name.  This routine
 *      will check for loops or disconnections before allowing the
 *      rename to occur.
 */
int
dir_rename(ULONG frominum, char *fromname, ULONG toinum, char *toname,
           int isdir, int type)
{
    ULONG      inum;
    ULONG      inumfrom;
    ULONG      offsetfrom;
    ULONG      offsetto;
    icommon_t *inode;

    inumfrom   = dir_name_search(frominum, fromname);
    offsetfrom = global.Poffset;

    if (inumfrom == 0)  /* failed to find source file/dir */
        return (0);

    inode = inode_read(frominum);
    if ((DISK16(inode->ic_mode) & IFMT) != IFDIR) {
        PRINT2(("bad source directory i=%d\n", frominum));
        return (0);
    }

    inode = inode_read(toinum);
    if ((DISK16(inode->ic_mode) & IFMT) != IFDIR) {
        PRINT2(("bad destination directory i=%d\n", toinum));
        return (0);
    }

    /* check for bad dest directory */
    if (toinum == inumfrom) {
        PRINT2(("attempted to put parent %d in itself\n", toinum));
        return (0);
    }

    inum = toinum;

    /* Block attempt to put parent in child */
    if (isdir) {
        while (inum != 2) {
            inum = dir_name_search(inum, "..");
            if (inum == inumfrom) {
                PRINT2(("rename: attempted to put parent %d in child %d\n",
                        toinum, inumfrom));
                return (0);
            }
        }
    }

    if (offsetto = dir_create(toinum, toname, inumfrom, type)) {
        if ((inum = dir_offset_delete(frominum, offsetfrom, 0)) == 0) {
            PRINT2(("dr: ** can't delete old %s, deleting new\n", toname));
            dir_offset_delete(toinum, offsetto, 1);
        } else {
            return (inum);
        }
    } else {
        PRINT2(("rename: can't create %s\n", toname));
    }

    return (0);
}

/*
 * dir_inum_change()
 *      This routine will  change an inode number in a directory structure.
 *      The parent is first checked to assure it is a directory.
 */
int
dir_inum_change(ULONG pinum, ULONG finum, ULONG tinum)
{
    int        blk;
    int        fblks;
    int        spill;
    char      *buf;
    direct_t  *bend;
    direct_t  *current;
    icommon_t *pinode;

    PRINT(("dic: p=%d from=%d to=%d\n", pinum, finum, tinum));

    if (pinum < 2) {
        PRINT2(("bad parent directory inode number %d\n", pinum));
        return (0);
    }

    pinode = inode_read(pinum);
    fblks  = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill  =  IC_SIZE(pinode)              % FSIZE;

    for (blk = 0; blk < fblks; blk++) {
        buf = cache_frag(ridisk_frag(blk, pinum));
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_ic: cache gave NULL\n"));
            return (0);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        while (current < bend) {
            if (DISK32(current->d_ino) == finum) {
                buf = cache_frag_write(ridisk_frag(blk, pinum), 1);
                goto found_object;
            }
#ifndef FAST
            if (DISK16(current->d_reclen) > DIRBLKSIZ) {
                PRINT2(("INCON: dir corrupt: reclen %d > DIRBLKSIZ\n",
                        DISK16(current->d_reclen)));
                superblock->fs_ronly = 1;
                return (0);
            }
#endif
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

    PRINT2(("** Unable to find inode %u in %u to change to %u\n",
            pinum, finum, tinum));
    return (0);

found_object:
    current->d_ino = DISK32(tinum);
    return (1);
}

/*
 * dir_name_is_illegal()
 *      This routine will return true if the specified file name
 *      is not a legal file name to be written in a directory.
 */
int
dir_name_is_illegal(const char *name)
{
    /* check for no name */
    if (*name == '\0')
        return (1);

    /* check for . and .. */
    if ((*name == '.') && ((*(name + 1) == '\0') ||
        ((*(name + 1) == '.') && (*(name + 2) == '\0')))) {
        return (1);
    }

    /* check for path components in name */
    while (*name != '\0') {
        if ((*name == '/') || (*name == ':') || (*name == '\"'))
            return (1);
        name++;
    }

    return (0);
}
#endif  /* !RONLY */


/*
 * dir_name_search()
 *      This routine is called to locate a given filename in the
 *      filesystem.  The inode number for that name is returned
 *      to the calling routine.  The name must be located directly
 *      in the specified parent inode number.  If a path lookup
 *      is required, call the file_find routine in file.c
 */
ULONG
dir_name_search(ULONG pinum, const char *name)
{
    ULONG      inum = 0;
    int        fblks;
    int        spill;
    int        blk;
    char      *buf;
    direct_t  *current;
    direct_t  *bend;
    icommon_t *pinode;

    pinode = inode_read(pinum);
    if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR) {
        PRINT2(("INCON: Not a directory i=%d n=%s\n", pinum, name));
        superblock->fs_ronly = 1;
        return (0);
    }

    fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill =  IC_SIZE(pinode)              % FSIZE;

    for (blk = 0; blk < fblks; blk++) {
        buf = cache_frag(ridisk_frag(blk, pinum));
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_ns: cache gave NULL\n"));
            return (0);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        while (current < bend) {
            if (current->d_ino && (strcmp(current->d_name, name) == 0)) {
                inum = DISK32(current->d_ino);
                goto breakloop;
            }
#ifndef FAST
            if (DISK16(current->d_reclen) > DIRBLKSIZ) {
                PRINT2(("** dir reclen %d > BLKSIZ\n",
                        DISK16(current->d_reclen)));
                return (0);
            }
            if (current->d_reclen == 0) {
                PRINT2(("** panic, dir record size 0 at offset %d\n",
                        (ULONG)((char *)current - buf)));
                return (0);
            }
#endif
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

    if ((inum == 0) && (case_dependent == 0)) {
        /* Search again, this time ignoring case */
        for (blk = fblks - 1; blk >= 0; blk--) {
            buf = cache_frag(ridisk_frag(blk, pinum));
            current = (direct_t *) buf;
            bend    = (direct_t *)
                      (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));
            while (current < bend) {
                if (streqv(current->d_name, name)) {
                    inum = DISK32(current->d_ino);
                    goto breakloop;
                }
                current = (direct_t *)
                          ((char *) current + DISK16(current->d_reclen));
            }
        }
    }

breakloop:

    if (inum)
        global.Poffset  = blk * FSIZE + ((char *) current - buf);

    return (inum);
}


/*
 * dir_inum_search()
 *      This routine is called to locate a specific inode number
 *      in a parent inode number.  If found, the offset for that
 *      inode number will be returned to the calling routine.
 */
ULONG
dir_inum_search(ULONG pinum, int inum)
{
    int        fblks;
    int        spill;
    int        blk;
    char      *buf;
    direct_t  *current;
    direct_t  *bend;
    icommon_t *pinode;

    pinode = inode_read(pinum);
    if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
        return (0);

    fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
    spill =  IC_SIZE(pinode)              % FSIZE;

    for (blk = 0; blk < fblks; blk++) {
        buf = cache_frag(ridisk_frag(blk, pinum));
#ifndef FAST
        if (buf == NULL) {
            PRINT2(("dir_is: cache gave NULL\n"));
            return (0);
        }
#endif
        current = (direct_t *) buf;
        bend    = (direct_t *)
                  (buf + ((spill && (blk == fblks - 1)) ? spill : FSIZE));

        while (current < bend) {
            if (DISK32(current->d_ino) == inum)
                goto breakloop;
#ifndef FAST
            if (DISK16(current->d_reclen) > DIRBLKSIZ)
                return (0);
#endif
            current = (direct_t *)
                      ((char *) current + DISK16(current->d_reclen));
        }
    }

breakloop:

    if (blk != fblks)
        return (blk * DIRBLKSIZ + ((char *) current - buf));

    return (0);
}


/*
 * dir_next()
 *      This routine will return a pointer to a directory entry
 *      given that entry's offset into its parent directory.
 */
direct_t *
dir_next(ULONG inum, ULONG offset)
{
    ULONG  frag;
    char  *ptr;

    if ((frag = ridisk_frag(offset / FSIZE, inum)) == 0) {
        PRINT2(("** dir_next: bad frag %u for inum %u off %u\n",
                frag, inum, offset));
        return (NULL);
    }

    ptr = cache_frag(frag);
#ifndef FAST
    if (ptr == NULL) {
        PRINT2(("dir_next: cache gave NULL for frag %d\n", frag));
        return (NULL);
    }
#endif
    return ((direct_t *) (ptr + (offset % FSIZE)));
}
