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
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/exec_protos.h>

#include "config.h"
#include "superblock.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "fsmacros.h"
#include "packets.h"
#include "handler.h"
#include "file.h"
#include "cache.h"
#include "stat.h"
#include "bffs_dosextens.h"
#include "system.h"
#include "inode.h"
#include "dir.h"
#include "ufs.h"
#include "unixtime.h"

extern	int resolve_symlinks;
extern	struct DosPacket *pack;
extern	int minfree;
extern	ULONG phys_sectorsize;

#define TYPE pack->dp_Type
#define ARG1 pack->dp_Arg1
#define ARG2 pack->dp_Arg2
#define ARG3 pack->dp_Arg3
#define ARG4 pack->dp_Arg4


void
PUnimplemented(void)
{
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PLocateObject(void)
{
	struct	BFFSLock *lock;
	char	*name;
	char	*sname;
	char	cho;
	int	inum;
	int	pinum;
	struct	icommon *inode;

	if ((superblock->fs_ronly) && (ARG3 == ACCESS_WRITE)) {
	    global.Res1 = DOSFALSE;
	    global.Res1 = ERROR_DISK_WRITE_PROTECTED;
	    return;
	}

	UPSTAT(locates);
	lock = (struct BFFSLock *) BTOC(ARG1);
	name = ((char *) BTOC(ARG2)) + 1;

	cho = *(name + *(name - 1));
	*(name + *(name - 1)) = '\0';

	PRINT(("lock=%x name='%s'\n", lock, name));

	if (!(pinum = parent_parse(lock, name, &sname))) {
	    global.Res1 = DOSFALSE;
	    if (global.Res2 == 0)
		global.Res2 = ERROR_DIR_NOT_FOUND;
	    goto endlocate;
	}
/*
	PRINT((" : pinum=%d subname=%s\n", pinum, sname));
*/

	if ((inum = file_name_find(pinum, sname)) == 0) {
	    global.Res1 = DOSFALSE;
	    if (global.Res2 == 0)
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	} else {
	    /* first check to make sure it is not a symlink */
	    inode = inode_read(inum);
	    if (!resolve_symlinks) {
		if ((DISK16(inode->ic_mode) & IFMT) == IFLNK) {
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_IS_SOFT_LINK;
		    goto endlocate;
		}
	    }

/* Cannot change permissions on dir if following code is enabled, but
   does implement UNIX can't cd to dir without execute bit set otherwise

	    if (((DISK16(inode->ic_mode) & IFMT) == IFDIR) &&
		((DISK16(inode->ic_mode) & IEXEC) == 0)) {
		PRINT(("directory is not searchable\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_READ_PROTECTED;
		goto endlocate;
	    }
*/
	    global.Res1 = CTOB(CreateLock(inum, ARG3, global.Pinum,
					  global.Poffset));
	}

	endlocate:
	*(name + *(name - 1)) = cho;
}


void
FreeLock(struct BFFSLock *lock)
{
    struct BFFSLock *current;
    struct BFFSLock *parent;

    if (lock == NULL) {
	PRINT2(("** ERROR - FreeLock called with NULL lock\n"));
	return;
    }

/*
    PRINT(("  FreeLock: inum=%d pinum=%d ioffset=%d\n", lock->fl_Key,
	   lock->fl_Pinum, lock->fl_Poffset));
*/

    parent = NULL;
    Forbid();
	current = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
	while (current != NULL) {
	    if (current == lock) {
		if (parent == NULL)  /* at head */
		    VolNode->dl_LockList = current->fl_Link;
		else
		    parent->fl_Link      = current->fl_Link;
		break;
	    }
	    parent  = current;
	    current = (struct BFFSLock *) BTOC(current->fl_Link);
	}
    Permit();

    if (current == NULL) {
	PRINT2(("Did not find lock in global locklist\n"));
	global.Res1 = DOSFALSE;
    } else
	FreeMem(current, sizeof(struct BFFSLock));
}


void
PFreeLock(void)
{
    FreeLock((struct BFFSLock *) BTOC(ARG1));
}


/*
 * examine_common() will fill in a FileInfoBlock structure for the
 * specified file (the lock).  It will optionally fill the fattr
 * structure as well, if non-NULL.
 *
 * RES1 = Success (DOSTRUE) / Failure (DOSFALSE)
 * RES2 = Failure code when RES1 == DOSFALSE
 *        Can be ERROR_IS_SOFT_LINK or ERROR_OBJECT_NOT_FOUND.
 */
static void
examine_common(struct BFFSLock *lock, FileInfoBlock_3_t *fib, fileattr_t *fattr)
{
    ULONG           inum    = lock->fl_Key;
    struct direct  *dir_ent = NULL;
    struct icommon *inode;

    UPSTAT(examines);
    fib->fib_DirOffset = MSb;

    if (inum != ROOTINO) {
	inode = inode_read(inum);
	if ((DISK16(inode->ic_mode) & IFMT) == IFLNK) {
	    if (resolve_symlinks) {
		ULONG  ninum = 0;
		ULONG  filesize = IC_SIZE(inode);
		char  *buf = (char *) AllocMem(filesize + 1, MEMF_PUBLIC);

		if (bsd44fs && (filesize < MAXSYMLINKLEN)) {
		    strncpy(buf, inode->ic_db, filesize);
		    buf[filesize] = '\0';
		    ninum = file_find(lock->fl_Pinum, buf);
		} else if (file_read(inum, 0, filesize, buf) == filesize) {
		    buf[filesize] = '\0';
		    ninum = file_find(lock->fl_Pinum, buf);
		}
		FreeMem(buf, filesize + 1);
		if (ninum == 0) {
		    /* Broken link, so just report it as a soft link */
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_IS_SOFT_LINK;
		    return;
		}
		dir_ent = dir_next(global.Pinum, global.Poffset);
		PRINT(("resolved link: %d to %d (%s)\n", lock->fl_Key,
		       ninum, dir_ent->d_name));
		lock->fl_Key     = ninum;
		lock->fl_Pinum   = global.Pinum;
		lock->fl_Poffset = global.Poffset;
		inum = ninum;
	    } else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_IS_SOFT_LINK;
		return;
	    }
	} else {
	    dir_ent = dir_next(lock->fl_Pinum, lock->fl_Poffset);
	}
    }
    inode = inode_read(inum);
    FillInfoBlock(fib, lock, inode, dir_ent);
    if (fattr != NULL)
	FillAttrBlock(fattr, inode);
}

/*
 * PExamineObject() implements both ACTION_EXAMINE_OBJECT (23) and
 * ACTION_EX_OBJECT (50).  These packets are used by applications to
 * acquire file and directory metadata as stored in directories.
 *
 * ACTION_EX_OBJECT is used by the AS225 "ls" command to acquire
 * additional UNIX attributes on each directory object.  The main
 * difference is that ARG3 is added as a C pointer to an additional
 * Sun NFS RPC NFS fattr data structure.
 *
 * ARG1 = File lock
 * ARG2 = BPTR FileInfoBlock
 * ARG3 = Pointer to File Attr Block (ACTION_EX_OBJECT only)
 *
 * RES1 = Success (DOSTRUE) / Failure (DOSFALSE)
 * RES2 = Failure code when RES1 == DOSFALSE
 */
void
PExamineObject(void)
{
    struct BFFSLock   *lock  = (struct BFFSLock *) BTOC(ARG1);
    FileInfoBlock_3_t *fib   = (FileInfoBlock_3_t *) BTOC(ARG2);
    fileattr_t        *fattr = NULL;

    if ((pack->dp_Type == ACTION_EX_OBJECT) && (ARG3 != 0))
	fattr = (fileattr_t *) ARG3;

    examine_common(lock, fib, fattr);
}

/*
 * PExamineNext() implements both ACTION_EXAMINE_NEXT (24) and
 * ACTION_EX_NEXT (51).  These packets are used by applications to
 * acquire file and directory metadata as stored in directories,
 * and are usually iterated on to acquire all directory contents.
 *
 * ACTION_EX_NEXT is used by the AS225 "ls" command to acquire
 * additional UNIX attributes on each directory object.  The main
 * difference is that ARG3 is added as a C pointer to an additional
 * Sun NFS RPC NFS fattr data structure.
 *
 * ARG1 = File lock
 * ARG2 = BPTR FileInfoBlock
 * ARG3 = Pointer to File Attr Block (ACTION_EX_NEXT only)
 *
 * RES1 = Success (DOSTRUE) / Failure (DOSFALSE)
 * RES2 = Failure code when RES1 == DOSFALSE
 */
void
PExamineNext(void)
{
    struct BFFSLock *lock;
    FileInfoBlock_3_t *fib;
    struct icommon *pinode;
    struct icommon *inode;
    struct direct *dir_ent;
#ifdef SHOWDOTDOT
    const int showdotdot = 1;
#else
    const int showdotdot = 0;
#endif
    ULONG ic_size;
    ULONG inum;

    lock = (struct BFFSLock *) BTOC(ARG1);
    fib  = (FileInfoBlock_3_t *) BTOC(ARG2);

    UPSTAT(examinenexts);
    if (fib->fib_DirOffset & MSb) {
	if (!(fib->fib_DirEntryType & (ST_USERDIR | ST_ROOT))) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_NO_MORE_ENTRIES;
	    return;
	}
	fib->fib_DirOffset = 0;
#ifndef NOPERMCHECK
	if (!is_readable(lock->fl_Key)) {
	    PRINT(("directory is read protected\n"));
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_READ_PROTECTED;
	    return;
	}
#endif
    }

    pinode = inode_read(lock->fl_Key);
    if ((pinode == NULL) || ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DIR_NOT_FOUND;  /* NO_DEFAULT_DIR OBJECT_WRONG_TYPE */
	return;
    }
    ic_size = IC_SIZE(pinode);

getname:
    if (fib->fib_DirOffset >= ic_size) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    dir_ent = dir_next(lock->fl_Key, fib->fib_DirOffset);

    if (dir_ent == NULL) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }
    inum = DISK32(dir_ent->d_ino);

    /* kludge to skip . and ..  In a galaxy far far away (When all
       AmigaDOS programs become link smart), we might be able to put
       the dots back in. */
    if ((fib->fib_DirOffset == 0) && (showdotdot == 0) &&
	(pack->dp_Type != ACTION_EX_NEXT)) {
	fib->fib_DirOffset += DISK16(dir_ent->d_reclen) +
	                      DISK16(((struct direct *) ((char *) dir_ent +
				      DISK16(dir_ent->d_reclen)))->d_reclen);
	PRINT(("skipped=%d\n", fib->fib_DirOffset));
	goto getname;
    }

    fib->fib_DirOffset += (int) DISK16(dir_ent->d_reclen);

    if (dir_ent->d_ino == 0) {
	if (dir_ent->d_reclen == 0) {   /* Skip to start of next dir block */
	    fib->fib_DirOffset |= (DIRBLKSIZ - 1);
	    fib->fib_DirOffset++;
	}
	goto getname;
    }

    inode = inode_read(DISK32(dir_ent->d_ino));
    FillInfoBlock(fib, lock, inode, dir_ent);

    if ((pack->dp_Type == ACTION_EX_NEXT) && (ARG3 != 0))
	FillAttrBlock((fileattr_t *) ARG3, inode);
}


void
PFindInput(void)
{
    struct  FileHandle *fh;
    struct  BFFSLock *lock;
    char    *name;
    char    *bname;
    struct  BFFSfh *fileh;
    struct  icommon *inode;
    ULONG   inum;
    char    cho;

    UPSTAT(findinput);
    fh   = (struct FileHandle *) BTOC(ARG1);
    lock =   (struct BFFSLock *) BTOC(ARG2);
    bname =	       ((char *) BTOC(ARG3));
    name = bname + 1;
    PRINT(("FindInput for %s, size=%d, addr=%x\n", name, *bname, bname));


    bname = (name + *bname);
    cho = *bname;
    *bname = '\0';


    if ((inum = path_parse(lock, name)) == 0) {
	if (global.Res2 == 0)
	    global.Res2 = ERROR_OBJECT_NOT_FOUND;
	goto doserror;
    }

    inode = inode_read(inum);
#ifndef FAST
    if (inode == NULL) {
	PRINT2(("PFI: inode_read gave NULL\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_NOT_VALIDATED;
	return;
    }
#endif

    if ((DISK16(inode->ic_mode) & IFMT) != IFREG) {	/* dir or special */
	PRINT1(("attempt to open special file %s for read\n", name));
	global.Res2 = ERROR_OBJECT_WRONG_TYPE;
	goto doserror;
#ifndef NOPERMCHECK
    } else if ((DISK16(inode->ic_mode) & IREAD) == 0) {
	global.Res2 = ERROR_READ_PROTECTED;
	goto doserror;
#endif
    }

    fileh = (struct BFFSfh *) AllocMem(sizeof(struct BFFSfh), MEMF_PUBLIC);
    if (fileh == NULL) {
	PRINT2(("FindInput: unable to allocate %d bytes\n", sizeof(struct BFFSfh)));
	global.Res2 = ERROR_NO_FREE_STORE;
	goto doserror;
    }

    fileh->lock = CreateLock(inum, ACCESS_READ, global.Pinum, global.Poffset);
    if (fileh->lock == NULL) {
	global.Res2 = ERROR_OBJECT_IN_USE;
	FreeMem(fileh, sizeof(struct BFFSfh));
	goto doserror;
    }
    fileh->lock->fl_Fileh = fileh;
    fileh->current_position = 0;
    fileh->maximum_position = 0;
    fileh->access_mode	    = MODE_READ;   /* read */
    fileh->truncate_mode    = MODE_UPDATE; /* don't truncate at close */
    fileh->real_inum	    = fileh->lock->fl_Key;
    fh->fh_Arg1		    = (ULONG) fileh;

    *bname = cho;
    return;

    doserror:
    *bname = cho;
    global.Res1 = DOSFALSE;
}


void
PFindOutput(void)
{
#ifdef RONLY
    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
    return;
#else
    struct  FileHandle *fh;
    struct  BFFSLock *lock;
    char    *name;
    char    cho;
    struct  BFFSfh *fileh;
    struct  icommon *inode;
    char    *sname;
    LONG    pinum;
    LONG    inum;
    ULONG   ioffset;

    UPSTAT(findoutput);
    fh   = (struct FileHandle *) BTOC(ARG1);
    lock =   (struct BFFSLock *) BTOC(ARG2);
    name =	       ((char *) BTOC(ARG3)) + 1;

    if (superblock->fs_ronly) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    fileh = (struct BFFSfh *) AllocMem(sizeof(struct BFFSfh), MEMF_PUBLIC);
    if (fileh == NULL) {
	PRINT2(("FindOutput: unable to allocate %d bytes\n", sizeof(struct BFFSfh)));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_FREE_STORE;
	return;
    }

    cho = *(name + *(name - 1));
    *(name + *(name - 1)) = '\0';

    if (!(pinum = parent_parse(lock, name, &sname))) {
	global.Res2 = ERROR_DIR_NOT_FOUND;
	goto doserror;
    }

    inum = dir_name_search(pinum, sname);

    if (inum == NULL) {				/* must create new file */
	if (dir_name_is_illegal(sname)) {
		PRINT(("given bad filename %s\n", sname));
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}
#ifndef NOPERMCHECK
	if (!is_writable(pinum)) {
		PRINT(("PFO: parent dir not writable\n"));
		global.Res2 = ERROR_WRITE_PROTECTED;
		goto doserror;
	}
#endif
	inum = inum_new(pinum, I_FILE);
	if (inum == 0) {
		PRINT(("failed to get a new inode from inum_new()\n"));
		global.Res2 = ERROR_NO_MORE_ENTRIES;
		goto doserror;
	}

	if ((ioffset = dir_create(pinum, sname, inum, DT_REG)) != 0) {
	    fileh->lock = CreateLock(inum, ACCESS_WRITE, pinum, ioffset);

	    if (fileh->lock == NULL) {
		PRINT2(("BUG: FO file created, but could not lock\n"));
		inum_free(inum);
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		goto doserror;
	    }
	    fileh->lock->fl_Fileh = fileh;
	    inum_sane(inum, I_FILE);
	} else {
	    PRINT(("Create failed for %s\n", name));
	    inum_free(inum);
	    global.Res2 = ERROR_DISK_FULL;
	    goto doserror;
	}
    } else {					/* file exists, open it */
	inode = inode_read(inum);
#ifndef FAST
	if (inode == NULL) {
	    PRINT2(("PFO: inode_read gave NULL for %u\n", inum));
	    goto doserror;
	}
#endif

	if ((DISK16(inode->ic_mode) & IFMT) != IFREG) {	/* dir or special */
	    PRINT(("attempt to open special file %s for write\n", name));
	    global.Res2 = ERROR_OBJECT_WRONG_TYPE;
	    goto doserror;
#ifndef NOPERMCHECK
	} else if ((DISK16(inode->ic_mode) & IWRITE) == 0) {
	    PRINT(("%s is write protected\n", name));
	    global.Res2 = ERROR_WRITE_PROTECTED;
	    goto doserror;
#endif
	}
	fileh->lock = CreateLock(inum, ACCESS_WRITE, global.Pinum,
				 global.Poffset);
	if (fileh->lock == NULL) {
	    global.Res2 = ERROR_OBJECT_IN_USE;
	    goto doserror;
	}
	fileh->lock->fl_Fileh = fileh;
    }


    fileh->current_position = 0;
    fileh->maximum_position = 0;
    fileh->access_mode	    = MODE_READ;      /* start out read */
    fileh->truncate_mode    = MODE_TRUNCATE;  /* truncate file to max seek */
/*  fileh->real_inum	    = fileh->lock->fl_Key;
*/
    fileh->real_inum	    = inum;
    fh->fh_Arg1		    = (ULONG) fileh;

    *(name + *(name - 1)) = cho;
    return;

    doserror:
    *(name + *(name - 1)) = cho;
    global.Res1 = DOSFALSE;
    FreeMem(fileh, sizeof(struct BFFSfh));
#endif
}


void
PRead(void)
{
    struct BFFSfh *fileh;
    char	  *buf;
    LONG	  size;

    fileh = (struct BFFSfh *) ARG1;
    buf   =	     (char *) ARG2;
    size  =	       (LONG) ARG3;

#ifndef NOPERMCHECK
    if (!is_readable(fileh->real_inum)) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_READ_PROTECTED;
	return;
    }
#endif
    UPSTAT(reads);

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 2097152)) {
	PRINT2(("** invalid real inode number %d given to Read for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
#endif

    global.Res1 = file_read(fileh->real_inum, fileh->current_position, size, buf);

    if (global.Res1 >= 0) {
	fileh->current_position += global.Res1;
	if (fileh->current_position > fileh->maximum_position)
		fileh->maximum_position	= fileh->current_position;
    } else {
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
}


void
PWrite(void)
{
#ifdef RONLY
    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
    return;
#else
    struct BFFSfh *fileh = (struct BFFSfh *) ARG1;
    char	  *buf   = (char *) ARG2;
    LONG	   size  = (LONG) ARG3;

    UPSTAT(writes);

    if (superblock->fs_ronly) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 2097152)) {
	PRINT2(("** invalid real inode number %d given to Write for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
	return;
    }
#endif

    if (fileh->access_mode == MODE_READ) {
#ifndef NOPERMCHECK
	if (!is_writable(fileh->real_inum)) {
		global.Res1 = -1;
		global.Res2 = ERROR_WRITE_PROTECTED;
		return;
	}
#endif

	/* change to write since not yet written */
	if (file_block_extend(fileh->real_inum)) {
		global.Res1 = -1;
		global.Res2 = ERROR_DISK_FULL;
		return;
	}
	fileh->access_mode = MODE_WRITE;
    }

    global.Res2 = 0;
    global.Res1 = file_write(fileh->real_inum, fileh->current_position, size, buf);
    if (global.Res1 >= 0) {
	fileh->current_position += global.Res1;
	if (fileh->current_position > fileh->maximum_position)
	    fileh->maximum_position = fileh->current_position;
    } else {
	global.Res1 = -1;
	if (global.Res2 == 0)
	    global.Res2 = ERROR_SEEK_ERROR;
    }
#endif
}


void
PSeek(void)
{
    ULONG	   end;
    struct icommon *inode;
    struct BFFSfh *fileh;
    LONG	   movement;
    LONG	   mode;

    fileh	= (struct BFFSfh *) ARG1;
    movement	=		    ARG2;
    mode	=		    ARG3;

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 2097152)) {
	PRINT2(("** invalid real inode number %d given to Seek for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
#endif

    inode = inode_read(fileh->real_inum);
    end   = IC_SIZE(inode);

    if (mode == OFFSET_END)
	movement += end;
    else if (mode == OFFSET_CURRENT)
	movement += fileh->current_position;

#ifdef ALLOW_SPARSE_FILES
    /* Allow seek to any offset */
    global.Res1 = fileh->current_position;
    fileh->current_position = movement;
#else
    if ((movement >= 0) && (movement <= end)) {
	global.Res1 = fileh->current_position;
	fileh->current_position = movement;
    } else {
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
#endif
}


void
PEnd(void)
{
    struct BFFSfh *fileh;

    fileh = (struct BFFSfh *) ARG1;

#ifndef RONLY
    if (fileh->access_mode == MODE_WRITE)
	truncate_file(fileh);
#endif

    FreeLock(fileh->lock);
    FreeMem(fileh, sizeof(struct BFFSfh));
}


void
PParent(void)
{
    ULONG	    pinum;
    ULONG	    ioffset;
    struct BFFSLock *lock;

    lock = (struct BFFSLock *) BTOC(ARG1);

    if (lock->fl_Key == ROOTINO)
	global.Res1 = 0L;
    else {
	pinum = dir_name_search(lock->fl_Pinum, "..");
	PRINT(("parent of %d is %d; whose parent is %d\n",
		lock->fl_Key, lock->fl_Pinum, pinum));
	if (pinum) {
	    ioffset = dir_inum_search(pinum, lock->fl_Pinum);
	    global.Res1 = CTOB(CreateLock(lock->fl_Pinum, SHARED_LOCK,
					  pinum, ioffset));
	} else
	    global.Res1 = 0L;
    }
}


void
PDeviceInfo(void)
{
    extern int timing;
    struct InfoData *infodata;
    unsigned long nbfree;
    unsigned long dsize;

    if (superblock->fs_flags & FS_FLAGS_UPDATED) {
	 nbfree = DISK32(superblock->fs_new_cstotal.cs_nbfree[is_big_endian]);
	 dsize  = DISK32(superblock->fs_new_dsize[is_big_endian]);
    } else {
	 nbfree = DISK32(superblock->fs_cstotal.cs_nbfree);
	 dsize  = DISK32(superblock->fs_dsize);
    }

    if (TYPE == ACTION_INFO)
	    infodata = (struct InfoData *) BTOC(ARG2);
    else
	    infodata = (struct InfoData *) BTOC(ARG1);

    infodata->id_NumSoftErrors     = 0L;
    infodata->id_UnitNumber        = 0L;  /* was DISK_UNIT */
    if (superblock->fs_ronly)
	    infodata->id_DiskState = ID_WRITE_PROTECTED;
    else
	    infodata->id_DiskState = ID_VALIDATED;

    infodata->id_NumBlocks	   = dsize;

    infodata->id_NumBlocksUsed     = dsize - nbfree * FRAGS_PER_BLK;
    if (minfree != 0)
	infodata->id_NumBlocksUsed += DISK32(superblock->fs_minfree) *
				      dsize / 100;
    infodata->id_BytesPerBlock = FSIZE;
    infodata->id_DiskType          = ID_FFS_DISK;
    infodata->id_VolumeNode        = CTOB(VolNode);  /* BPTR */
    infodata->id_InUse             = timing;

    /*
     * Note that we need to return ID_DOS_DISK or ID_FFS_DISK here per
     * Randell Jesup at Commodore 22-Dec-1991.
     */
}


void
PDeleteObject(void)
{
#ifdef RONLY
    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
    return;
#else
	ULONG	inum;
	ULONG	ioffset;
	ULONG	pinum;
	struct	icommon *inode;
	struct	icommon *pinode;
	struct	BFFSLock *lock;
	char	*subname;
	char	*name;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	lock	= (struct BFFSLock *) BTOC(ARG1);
	name	= ((char *) BTOC(ARG2)) + 1;

	pinum = parent_parse(lock, name, &subname);
	if (pinum) {
		inum    = dir_name_search(pinum, subname);
		ioffset = global.Poffset;
	} else
		inum = 0;

	if (inum < 3) {
		global.Res1 = DOSFALSE;
		switch(inum) {
		    case 0:	/* error - dir routine did not find file */
			global.Res2 = ERROR_OBJECT_NOT_FOUND;
			break;
		    case 1:	/* error, dir routine found non-empty directory */
			global.Res2 = ERROR_DIRECTORY_NOT_EMPTY;
			break;
		    case 2:	/* Don't let 'em delete the root directory */
			global.Res2 = ERROR_OBJECT_WRONG_TYPE;
			break;
		}
		return;
	}
#ifndef NOPERMCHECK
	if (!is_writable(pinum)) {
		PRINT(("DeleteObject: Parent is not writable\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DELETE_PROTECTED;
		return;
	}
#endif

	/* check lock list to make sure file is not open */
	lock = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
	while (lock != NULL)
		if (lock->fl_Key == inum) {
			global.Res1 = DOSFALSE;
			global.Res2 = ERROR_OBJECT_IN_USE;
			return;
		} else
			lock = (struct BFFSLock *) BTOC(lock->fl_Link);

	if (dir_offset_delete(pinum, ioffset, 1) == 0) {
		PRINT(("dir delete returned error code\n"));
		global.Res1 = DOSFALSE;
		return;
	}

	inode = inode_modify(inum);
#ifndef FAST
	if (inode == NULL) {
	    PRINT2(("PDel: inode_modify %u gave NULL\n", inum));
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_DISK_NOT_VALIDATED;
	    return;
	}
#endif

	PRINT(("delete %s, i=%d offset=%d nlink=%d\n",
		name, inum, ioffset, DISK16(inode->ic_nlink)));

	inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);

	if ((DISK16(inode->ic_mode) & IFMT) == IFDIR) { /* it's a dir */
		pinode = inode_modify(pinum);
#ifndef FAST
		if (pinode == NULL) {
		    PRINT2(("PDel: inode_modify %u gave NULL\n", pinum));
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_DISK_NOT_VALIDATED;
		    return;
		}
#endif

		/* Below are for "." and ".." */
		inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);
		pinode->ic_nlink = DISK16(DISK16(pinode->ic_nlink) - 1);
	}
	inode = inode_read(inum);
	if (DISK16(inode->ic_nlink) < 1) {
	    if ((DISK16(inode->ic_mode) & IFMT) == IFLNK)
		symlink_delete(inum, IC_SIZE(inode));
	    else if (((DISK16(inode->ic_mode) & IFMT) != IFCHR) &&
		((DISK16(inode->ic_mode) & IFMT) != IFBLK))
		file_deallocate(inum);

	    inum_free(inum);
	}
#endif
}


void
PMoreCache(void)
{
	if (abs(ARG1) > 9999) {
	    /* Any AddBuffers amount > 10000 or < 10000 adjusts the CG cache */
	    int sign;
	    int value;

	    if (ARG1 > 0)
		sign = 1;
	    else if (ARG1 < 0)
		sign = -1;
	    else
		sign = 0;

	    value = abs(ARG1) - 10000;

	    PRINT(("morecgs: cg_cache=%d amount=%d (%s)", cache_cg_size, value,
		   ((sign == -1) ? "-" : "+") ));

	    cache_cg_size += (value * sign);

	    cache_cg_adjust();

	    PRINT(("-> cache_cg=%d\n", cache_cg_size));

	    global.Res1 = cache_cg_size;

	} else {
	    int amount = ARG1;
	    PRINT2(("morecache: cache=%d amount=%d ", cache_size, amount));
	    cache_size += amount;

	    if (cache_size < 4)
		cache_size = 4;

	    cache_adjust();

	    PRINT2(("-> cache=%d\n", cache_size));

	    global.Res1 = cache_size;
	}
}


/* CreateDir()
 *	Resolve name
 *	Check for existence of dir/file with same name
 *	Allocate new inode
 *	Add dir entry
 *		If unsuccessful, deallocate new inode, return error
 *		If successful, set DIR attributes
 */
void
PCreateDir(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	char	*name;
	char	*sname;
	ULONG	pinum;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	int	newinum;
	ULONG	ioffset;

	if (superblock->fs_ronly) {
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		goto doserror;
	}

	lock = (struct BFFSLock *) BTOC(ARG1);
	name = ((char *) BTOC(ARG2)) + 1;

	if (!(pinum = parent_parse(lock, name, &sname))) {
		PRINT2(("** CreateDir: error, lock is null and parse path for %s\n", name));
		global.Res2 = ERROR_DIR_NOT_FOUND;
		goto doserror;
	}

	PRINT(("CreateDir parent=%d, name=%s sname=%s\n", pinum, name, sname));
#ifndef NOPERMCHECK
	if (!is_writable(pinum)) {
		PRINT(("parent dir not writable\n"));
		global.Res2 = ERROR_WRITE_PROTECTED;
		goto doserror;
	}
#endif
	if (dir_name_is_illegal(sname)) {
		PRINT(("given bad filename %s", sname));
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

	if (dir_name_search(pinum, sname)) {
		PRINT(("file %s already exists\n", name));
		global.Res2 = ERROR_OBJECT_EXISTS;
		goto doserror;
	}

	if ((newinum = inum_new(pinum, I_DIR)) == 0) {
		PRINT(("couldn't find free inode\n"));
		global.Res2 = ERROR_NO_MORE_ENTRIES;
		goto doserror;
	}

	if (ioffset = dir_create(pinum, sname, newinum, DT_DIR)) {
		inum_sane(newinum, I_DIR);
		dir_create(newinum, ".", newinum, DT_DIR);  /* returns 0 */
		if (dir_create(newinum, "..", pinum, DT_DIR)) {
			inode = inode_modify(pinum);
			inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
			global.Res1 = CTOB(CreateLock(newinum, ACCESS_WRITE,
						      pinum, ioffset));
			global.Res2 = 0;
			return;
		} else {
			PRINT(("create of .. failed\n"));
			dir_offset_delete(pinum, ioffset, 0);
			file_deallocate(newinum);
		}
	}

	PRINT(("create of dir failed\n"));
	inum_free(newinum);
	global.Res2 = ERROR_INVALID_COMPONENT_NAME;

	doserror:
	global.Res1 = DOSFALSE;
#endif
}


/* RenameObject()
 *	This routine handles the AmigaDOS file rename request.
 *	It takes a lock on the old parent, the old file name,
 *	a lock on the new parent, and the new file name.
 *	The routine will handle directory renames and other
 *	special cases.
 */
void
PRenameObject(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	char	cho;
	char	chn;
	char	*oldname;
	char	*newname;
	char	*oldsubname;
	char	*newsubname;
	int	szo;
	int	szn;
	int	isdir;
	ULONG	inum;
	ULONG	oldpinum;
	ULONG	newpinum;
	struct	icommon *inode;
	struct	BFFSLock *oldparent;
	struct	BFFSLock *newparent;

	global.Res1 = DOSFALSE;
	if (superblock->fs_ronly) {
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	UPSTAT(renames);

	oldname = ((char *) BTOC(ARG2)) + 1;
	newname = ((char *) BTOC(ARG4)) + 1;
	szo = *(oldname - 1);
	szn = *(newname - 1);
	cho = *(oldname + szo);
	chn = *(newname + szn);

	*(oldname + szo) = '\0';
	*(newname + szn) = '\0';
	oldparent = (struct BFFSLock *) BTOC(ARG1);
	newparent = (struct BFFSLock *) BTOC(ARG3);

	oldpinum = parent_parse(oldparent, oldname, &oldsubname);
	newpinum = parent_parse(newparent, newname, &newsubname);
#ifndef NOPERMCHECK
	if (!is_writable(oldpinum)) {
		global.Res2 = ERROR_WRITE_PROTECTED;
		goto doserror;
	}
	if (!is_writable(newpinum)) {
		global.Res2 = ERROR_WRITE_PROTECTED;
		goto doserror;
	}
#endif
	if (dir_name_is_illegal(oldsubname)) {
		PRINT(("oldname %s is illegal\n", oldsubname));
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

	if (dir_name_is_illegal(newsubname)) {
		PRINT(("newname %s is illegal\n", newsubname));
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

	if (oldpinum && newpinum) {
		PRINT(("Rename from (%s) p=%d %s to (%s) p=%d %s\n",
		    oldname, oldpinum, oldsubname,
		    newname, newpinum, newsubname));
		if ((inum = dir_name_search(oldpinum, oldsubname)) == 0) {
		    PRINT(("source not found\n"));
		    global.Res2 = ERROR_OBJECT_NOT_FOUND;
		} else {
		    inode = inode_read(inum);
		    isdir = ((DISK16(inode->ic_mode) & IFMT) == IFDIR) &&
			    (oldpinum != newpinum);
		    if (dir_rename(oldpinum, oldsubname, newpinum, newsubname,
				   isdir, (DISK16(inode->ic_mode) & IFMT) >> 12)) {
			if (isdir) {
			    dir_inum_change(inum, oldpinum, newpinum);
			    inode = inode_modify(oldpinum);
			    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);
			    inode = inode_modify(newpinum);
			    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
			}
			global.Res1 = DOSTRUE;
		    } else
			global.Res2 = ERROR_OBJECT_IN_USE;
		}
	} else {
		PRINT(("bad %s path\n", (oldpinum ? "dest" : "source")));
		global.Res2 = ERROR_DIR_NOT_FOUND;
	}

	doserror:
	*(oldname + szo) = cho;
	*(newname + szn) = chn;
#endif
}


/* RenameDisk()
 *	This routine modifies the superblock.  It stores the current name
 *	imposed by AmigaDOS in the last characters of the "last" mounted
 *	on field in the superblock.
 */
void
PRenameDisk(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	int	newlen;
	char	*newname;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	newname = ((char *) BTOC(ARG1));
/*	PRINT(("renamedisk: New name=%.*s\n", *newname, newname + 1)); */
	if (VolNode == NULL) {
		PRINT2(("rename called with NULL Volnode\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = 0;
		return;
	}

	newlen = *newname;
	if (newlen >= MAXMNTLEN - 2)
		newlen = MAXMNTLEN - 2;

	strncpy(superblock->fs_fsmnt + 1, newname + 1, MAXMNTLEN);
	superblock->fs_fsmnt[0] = '/';
	superblock->fs_fsmnt[newlen + 1] = '\0';
	superblock->fs_fmod++;

	Forbid();
	memcpy(volumename, newname, newlen + 1);
	volumename[0] = newlen;
	Permit();
#endif
}


/* SetProtect()
 *	This packet sets permissions on the specified file,
 *	given a lock and a path relative to that lock.
 */
void
PSetProtect(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	ULONG	mask;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;
	ULONG	temp;


	lock = (struct BFFSLock *) BTOC(ARG2);
	name = ((char *) BTOC(ARG3)) + 1;
	mask = (ULONG) ARG4;

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		temp = DISK16(inode->ic_mode);
		temp &= ~(ISUID | ISGID | ISVTX | 0777);

		/* temp |= (mask & FIBF_ARCHIVE)     ? 0 : (IEXEC >> 3); */
		temp |= (mask & FIBF_SCRIPT)      ? ISUID : 0;
		temp |= (mask & FIBF_HIDDEN)      ? ISGID : 0;
		temp |= (mask & FIBF_PURE)        ? ISVTX : 0;
		temp |= (mask & FIBF_READ)        ? 0 : IREAD;
		temp |= (mask & FIBF_WRITE)       ? 0 : IWRITE;
		temp |= (mask & FIBF_DELETE)      ? 0 : IWRITE;
		temp |= (mask & FIBF_EXECUTE)     ? 0 : IEXEC;
		temp |= (mask & FIBF_GRP_READ)    ? 0 : (IREAD  >> 3);
		temp |= (mask & FIBF_GRP_WRITE)   ? 0 : (IWRITE >> 3);
		temp |= (mask & FIBF_GRP_DELETE)  ? 0 : (IWRITE >> 3);
		temp |= (mask & FIBF_GRP_EXECUTE) ? 0 : (IEXEC  >> 3);
		temp |= (mask & FIBF_OTR_READ)    ? 0 : (IREAD  >> 6);
		temp |= (mask & FIBF_OTR_WRITE)   ? 0 : (IWRITE >> 6);
		temp |= (mask & FIBF_OTR_DELETE)  ? 0 : (IWRITE >> 6);
		temp |= (mask & FIBF_OTR_EXECUTE) ? 0 : (IEXEC  >> 6);
		if (og_perm_invert)
		    temp ^= (((IREAD | IWRITE | IEXEC) >> 3) |
		             ((IREAD | IWRITE | IEXEC) >> 6));

		inode->ic_mode = DISK16(temp);
	} else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
#endif
}


/* SetPerms()
 *	This packet sets full UNIX permissions on the specified
 *	file, given a lock and a path relative to that lock.
 */
void
PSetPerms(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	ULONG	mode;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;

	lock = (struct BFFSLock *) BTOC(ARG1);
	name = ((char *) BTOC(ARG2)) + 1;
	mode = (ULONG) ARG3;

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		inode->ic_mode = DISK16((DISK16(inode->ic_mode) & IFMT) |
					(mode & ~IFMT));
	} else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
#endif
}


/* GetPerms()
 *	This packet gets full UNIX permissions on the specified
 *	file, given a lock and a path relative to that lock.
 */
void
PGetPerms(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;

	lock = (struct BFFSLock *) BTOC(ARG1);
	name = ((char *) BTOC(ARG2)) + 1;

	inum = path_parse(lock, name);

	if (inum && (inode = inode_read(inum)))
		global.Res2 = DISK16(inode->ic_mode);
	else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
#endif
}


/* SetOwner()
 *	This packet sets the owner and group of the specified
 *	file, given the name of the file.
 */
void
PSetOwner(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	ULONG	both;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;
	short	owner;
	short	group;

	lock = (struct BFFSLock *) BTOC(ARG2);
	name = ((char *) BTOC(ARG3)) + 1;
	both = (ULONG) ARG4;

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		owner = both >> 16;
		group = both & 65535;
		if (owner != -1) {
			inode->ic_ouid = DISK16(owner);
			inode->ic_nuid = DISK32(owner);
		} else
			PRINT2(("owner -1 for %s\n", name));
		if (group != -1) {
			inode->ic_ogid = DISK16(group);
			inode->ic_ngid = DISK32(group);
		} else
			PRINT2(("group -1 for %s\n", name));
	} else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
#endif
}

/* CurrentVolume()
 *	This packet gets the current volume node which is active
 *	for the filesystem.
 */
void
PCurrentVolume(void)
{
	if (VolNode == NULL) {
		PRINT2(("CurrentVolume called with NULL Volnode\n"));
		NewVolNode();
	}
	global.Res1 = CTOB(VolNode);
}


/* CopyDir()
 *	This packet is used to create a second shared lock on an
 *	object identical to the first.
 */
void
PCopyDir(void)
{
	struct BFFSLock *lock;

	lock = (struct BFFSLock *) BTOC(ARG1);

	global.Res1 = CTOB(CreateLock(lock->fl_Key, lock->fl_Access,
			   lock->fl_Pinum, lock->fl_Poffset));
}


/* Inhibit() implements ACTION_INHIBIT
 *	This packet tells the filesystem to keep its hands off the
 *	media until another inhibit packet is sent with flag=0.
 */
void
PInhibit(void)
{
    LONG do_inhibit = ARG1;

    if (do_inhibit) {
	PRINT(("->INHIBIT %d\n", inhibited));
	if (inhibited == 0) {
	    close_files();
	    close_filesystem();
	}
	inhibited++;  /* Must be incremented after close happens above */
    } else {
	PRINT(("->UNINHIBIT %d\n", inhibited));
	if (inhibited == 0) {
	    PRINT(("Already uninhibited\n"));
	    global.Res1 = DOSFALSE;
	    global.Res2 = 0;
	} else if (--inhibited == 0) {
	    open_filesystem();
	}
    }
}


/* DiskChange()
 *	This packet tells the filesystem that the user has changed
 *	the media in a removable drive.  It is also useful after a
 *	fsck of the media where fsck modified some data.
 *	Any dirty buffers still in the cache are destroyed!
 */
void
PDiskChange(void)
{
	extern int cache_item_dirty;

	if (cache_item_dirty) {
		/* XXX: This should really pop up a requester (corrupted fs) */
		PRINT1(("WARNING: Loss of %d dirty frags still in cache\n",
			cache_item_dirty));
	}

	cache_full_invalidate();
	close_files();
	close_filesystem();
	open_filesystem();
}


/* Format()
 *	This packet is not implemented, as newfs is required to
 *	lay out new filesystem information.
 */
void
PFormat(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	char	*volume;
	char	*name;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	volume	= ((char *) BTOC(ARG1)) + 1;
	name	= ((char *) BTOC(ARG2)) + 1;

	PRINT(("format: Vol=%s Name=%s Type=%d\n", volume, name, ARG3));

	/* let DOS think we formatted the disk */
#endif
}


/* WriteProtect()
 *	This packet is sent by the dos C:Lock command to protect/
 *	unprotect the media.
 */
void
PWriteProtect(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	extern int physical_ro;

	if (ARG1)
		superblock->fs_ronly = 1;
	else if (!physical_ro)
		superblock->fs_ronly = 0;
#endif
}


/* IsFilesystem()
 *	Always answer yes, unless we failed to open the device.
 */
void
PIsFilesystem(void)
{
	if (dev_openfail) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
	}
}


/* Die()
 *	Go away and stop processing packets.
 */
void
PDie(void)
{
#if 0
	close_files();
	close_filesystem();
	RemoveVolNode();
#endif
	receiving_packets = 0;
}


/* Flush()
 *	Manually write the contents of the cache out to disk.
 */
void
PFlush(void)
{
#ifndef RONLY
	int count;

	if (superblock == NULL)
		return;

	count  = superblock_flush();
	count += cache_cg_flush();
	count += cache_flush();

	if (count > 0)
	    UPSTAT(flushes);
#endif
}

void
PSameLock(void)
{
	struct BFFSLock *lock1 = (struct BFFSLock *) BTOC(ARG1);
	struct BFFSLock *lock2; lock2 = (struct BFFSLock *) BTOC(ARG2);

	ULONG           key1;
	ULONG           key2;
	BPTR            volume1;
	BPTR            volume2;
	struct MsgPort *task1;
	struct MsgPort *task2;

	/* NULL may correspond to root volume */
	if (lock1 == NULL) {
	    key1    = ROOTINO;
	    volume1 = CTOB(VolNode);
	    task1   = DosPort;
	} else {
	    key1    = lock1->fl_Key;
	    volume1 = lock1->fl_Volume;
	    task1   = lock1->fl_Task;
	}

	if (lock2 == NULL) {
	    key2    = ROOTINO;
	    volume2 = CTOB(VolNode);
	    task2   = DosPort;
	} else {
	    key2    = lock2->fl_Key;
	    volume2 = lock2->fl_Volume;
	    task2   = lock2->fl_Task;
	}

	PRINT(("same_lock: comparing inode %d and inode %d\n", key1, key2));

	if ((volume1 != volume2) || (task1 != task2)) {
	    /* Different message port or Volume */
	    global.Res1 = DOSFALSE;
	    global.Res2 = LOCK_DIFFERENT;
	} else if (key1 != key2) {
	    /* Same volume, different inode */
	    global.Res1 = DOSFALSE;
	    global.Res2 = LOCK_SAME_HANDLER;  /* LOCK_SAME_VOLUME */
	} else {
	    /* Same inode */
	    global.Res1 = DOSTRUE;
	    global.Res2 = LOCK_SAME;
	}
}

void
PFilesysStats(void)
{
	extern struct	cache_set *cache_stack_tail;
	extern struct	cache_set *hashtable[];
	extern int	cache_alloced;
	extern int	unix_paths;
	extern int	cache_used;
	extern int	timer_secs;
	extern int	timer_loops;
	extern int	link_comments;
	extern int	inode_comments;

	stat->phys_sectorsize	= (ULONG) phys_sectorsize;
	stat->superblock	= (ULONG) superblock;
	stat->cache_head	= (ULONG) &cache_stack_tail;
	stat->cache_hash	= (ULONG) hashtable;
	stat->cache_size	= (ULONG *) &cache_size;
	stat->cache_cg_size	= (ULONG *) &cache_cg_size;
	stat->cache_item_dirty	= (ULONG *) &cache_item_dirty;
	stat->cache_alloced	= (ULONG *) &cache_alloced;
	stat->disk_poffset	= (ULONG *) &psectoffset;
	stat->disk_pmax		= (ULONG *) &psectmax;
	stat->unix_paths	= (ULONG *) &unix_paths;
	stat->resolve_symlinks	= (ULONG *) &resolve_symlinks;
	stat->case_dependent	= (ULONG *) &case_dependent;
	stat->link_comments	= (ULONG *) &link_comments;
	stat->inode_comments	= (ULONG *) &inode_comments;
	stat->cache_used	= (ULONG *) &cache_used;
	stat->timer_secs	= (ULONG *) &timer_secs;
	stat->timer_loops	= (ULONG *) &timer_loops;
	stat->GMT		= (ULONG *) &GMT;
	stat->minfree		= (ULONG *) &minfree;
	stat->og_perm_invert	= (ULONG *) &og_perm_invert;

	global.Res2		= (ULONG) stat;
}

void
PCreateObject(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	pinum;
	ULONG	inum;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	char	*name;
	char	*subname;
	ULONG	st_type;
	int	dir_type;
	ULONG	temp;

	if (superblock->fs_ronly) {
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		goto doserror;
	}

	switch (st_type = ARG3) {
		case ST_USERDIR:
			return(PCreateDir());
		case ST_LINKFILE:
			ARG3 = ARG4;
			ARG4 = 0;
			return(PMakeLink());
		case ST_SOFTLINK:
			ARG3 = ARG4;
			ARG4 = 1;
			return(PMakeLink());
		case ST_FILE:
			dir_type = DT_REG;
			break;
		case ST_CDEVICE:
			dir_type = DT_CHR;
			break;
		case ST_BDEVICE:
			dir_type = DT_BLK;
			break;
		case ST_FIFO:
			dir_type = DT_FIFO;
			break;
		case ST_WHITEOUT:
			dir_type = DT_WHT;
			break;
		default:
			PRINT1(("bad file type %d for '%s'\n", st_type, name));
			global.Res2 = ERROR_BAD_NUMBER;
			goto doserror;
	}

	name  = ((char *) BTOC(ARG2)) + 1;
	lock  = (struct BFFSLock *) BTOC(ARG1);
	pinum = parent_parse(lock, name, &subname);

	PRINT(("CreateObject: %x(%s) type=%c d=%d,%d p=%d %s\n",
		lock, name, ((st_type == ST_BDEVICE) ? 'b' : 'c'),
		ARG4 >> 8, ARG4 & 255, pinum, subname));

	if ((pinum == 0) || dir_name_is_illegal(subname)) {
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

#ifndef NOPERMCHECK
	if (!is_writable(pinum)) {
		PRINT(("parent dir not writable\n"));
		global.Res2 = ERROR_WRITE_PROTECTED;
		goto doserror;
	}
#endif

	inum = dir_name_search(pinum, subname);
	if (inum) {
	    global.Res2 = ERROR_OBJECT_EXISTS;
	    goto doserror;
	}

	inum = inum_new(pinum, I_FILE);
	if (inum == 0) {
	    PRINT(("failed to get a new inode from inum_new()\n"));
	    global.Res2 = ERROR_NO_MORE_ENTRIES;
	    goto doserror;
	}

	if (dir_create(pinum, subname, inum, dir_type)) {
	    inum_sane(inum, I_FILE);
	    inode = inode_modify(inum);

	    switch(st_type) {
		case ST_FILE:
			return;
		case ST_FIFO:
			temp = DISK16(inode->ic_mode);
			temp &= ~IFREG;
			temp |= IFIFO;
			inode->ic_mode = DISK16(temp);
			return;
		case ST_CDEVICE:
			temp = DISK16(inode->ic_mode);
			temp &= ~IFREG;
			temp |= IFCHR;
			inode->ic_mode = DISK16(temp);
			inode->ic_db[0] = DISK32(ARG4);
			return;
		case ST_BDEVICE:
			temp = DISK16(inode->ic_mode);
			temp &= ~IFREG;
			temp |= IFBLK;
			inode->ic_mode = DISK16(temp);
			inode->ic_db[0] = DISK32(ARG4);
			return;
	    }
	    return;
	} else {
	    PRINT(("Create failed for object '%s' type=%d\n", name, st_type));
	    global.Res2 = ERROR_DISK_FULL;
	}
	inum_free(inum);

	doserror:
	global.Res1 = DOSFALSE;
#endif
}

void
PMakeLink(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	pinum;
	ULONG	inum;
	ULONG	ioffset;
	char	*name;
	char	*name_from;
	char	*subname;
	struct	BFFSLock *lock;
	struct	BFFSLock *lock_from;
	struct	icommon *inode;
	ULONG	link_type;
	int	namelen;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	link_type = ARG4;
	PRINT(("MakeLink: %s\n", link_type ? "SOFT" : "HARD"));

	lock      = (struct BFFSLock *) BTOC(ARG1);
	name      = ((char *) BTOC(ARG2)) + 1;
	lock_from = (struct BFFSLock *) BTOC(ARG3);	/* used for hard */
	name_from = ((char *) BTOC(ARG3)) + 1;		/* used for soft */
	pinum     = parent_parse(lock, name, &subname);

	PRINT(("lock=%x\n", lock));
	PRINT(("name=%s\n", name));

	if (link_type)	/* soft */
	    PRINT(("from=%s\n", name_from));
	else		/* hard */
	    PRINT(("from=%x\n", lock_from));

	PRINT(("pinum=%d\n", pinum));

	if ((pinum == 0) || dir_name_is_illegal(subname)) {
	    global.Res2 = ERROR_INVALID_COMPONENT_NAME;
	    goto doserror;
	}

#ifndef NOPERMCHECK
	if (!is_writable(pinum)) {
	    PRINT(("parent dir not writable\n"));
	    global.Res2 = ERROR_WRITE_PROTECTED;
	    goto doserror;
	}
#endif

	inum = dir_name_search(pinum, subname);
	if (inum) {
	    global.Res2 = ERROR_OBJECT_EXISTS;
	    goto doserror;
	}

	if (link_type == 0) {		/* handle hard link */
	    if (lock_from == NULL) {
		global.Res2 = ERROR_BAD_TEMPLATE;
		goto doserror;
	    }
	    inum = lock_from->fl_Key;
	    PRINT(("linking i=%d\n", inum));
	    inode = inode_read(inum);
	    if ((DISK16(inode->ic_mode) & IFMT) == IFDIR) {
		global.Res2 = ERROR_OBJECT_WRONG_TYPE;
		goto doserror;
	    }
	    if (!dir_create(pinum, subname, inum,(DISK16(inode->ic_mode) & IFMT) >> 12)) {
		global.Res2 = ERROR_DISK_FULL;
		goto doserror;
	    }
	    inode = inode_modify(inum);
	    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
	    return;
	}

					/* handle soft link - here on */
	inum = inum_new(pinum, I_FILE);
	if (inum == 0) {
	    PRINT(("failed to get a new inode from inum_new()\n"));
	    global.Res2 = ERROR_NO_MORE_ENTRIES;
	    goto doserror;
	}

	if (ioffset = dir_create(pinum, subname, inum, DT_LNK))
	    inum_sane(inum, I_FILE);
	else {
	    PRINT(("Create failed for %s\n", name));
	    inum_free(inum);
	    global.Res2 = ERROR_DISK_FULL;
	    goto doserror;
	}

	namelen = strlen(name_from);

	if (symlink_create(inum, inode, name_from)) {
	    inode = inode_modify(inum);
	    inode->ic_mode = DISK16((DISK16(inode->ic_mode) & ~IFMT) | IFLNK);
	    return;
	}

	/* symlink create failed */
	dir_offset_delete(pinum, ioffset, 0);
	file_deallocate(inum);
	inum_free(inum);

	doserror:
	global.Res1 = DOSFALSE;
#endif
}

void
PReadLink(void)
{
	int	len;
	ULONG	inum;
	struct	BFFSLock *lock;

	linkname = NULL;

	lock = (struct BFFSLock *) BTOC(ARG1);
	read_link = 1;
	inum = path_parse(lock, (char *)ARG2);
	read_link = 0;

/*
	PRINT(("ReadLink "));
	PRINT(("lock=%d ", lock));
	PRINT(("name=%s ", ARG2));
	PRINT(("pathbuf=0x%08x ", ARG3));
	PRINT(("sizebuf=%d ", ARG4));
	PRINT(("i=%d\n", inum));
*/

	if (inum == 0) {
		global.Res1 = -1;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	if (linkname != NULL) {
	    len = strlen(linkname);
	    if (len >= ARG4) {
		global.Res1 = -2;
		global.Res2 = ERROR_OBJECT_TOO_LARGE;
		return;
	    }
	    global.Res1 = len + 1;
	    strcpy((char *) ARG3, linkname);
	    PRINT(("link name of %s is %s\n", ARG2, linkname));
	    FreeMem(linkname, len + 2);
	    return;
	}

	global.Res1 = -1;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
}

void
PNil(void)
{
}


void
PSetFileSize(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
#endif
}


void
PSetDate(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	char	*name;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	struct	DateStamp *datestamp;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	lock	  =  (struct BFFSLock *) BTOC(ARG2);
	name	  =            ((char *) BTOC(ARG3)) + 1;
	datestamp = (struct DateStamp *) ARG4;

	inum = path_parse(lock, name);

	if (inum == 0) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	PRINT(("SetDate: %d:%s %d\n", lock, name, inum));
	PRINT(("to day=%d min=%d tick=%d\n", datestamp->ds_Days,
		datestamp->ds_Minute, datestamp->ds_Tick));


	if (inum) {
		inode = inode_modify(inum);
		inode->ic_mtime = DISK32(amiga_ds_to_unix_time(datestamp));
		return;
	}

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
#endif
}

void
PSetDates(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	char	*name;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	struct	DateStamp *datestamp;

	if (superblock->fs_ronly) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	    return;
	}

	lock	  =  (struct BFFSLock *) BTOC(ARG2);
	name	  =	       ((char *) BTOC(ARG3)) + 1;
	datestamp = (struct DateStamp *) ARG4;
	inum = path_parse(lock, name);

	if (inum == 0) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_OBJECT_NOT_FOUND;
	    return;
	}

	switch (ARG1) {
	    case 0:  /* set modify date stamp */
		inode = inode_modify(inum);
		inode->ic_mtime = DISK32(amiga_ds_to_unix_time(datestamp));
		break;
	    case 1:  /* get modify date stamp */
		inode = inode_read(inum);
		unix_time_to_amiga_ds(DISK32(inode->ic_mtime), datestamp);
		break;
	    case 2:  /* set change date stamp */
		inode = inode_modify(inum);
		inode->ic_ctime = DISK32(amiga_ds_to_unix_time(datestamp));
		break;
	    case 3:  /* get change date stamp */
		inode = inode_read(inum);
		unix_time_to_amiga_ds(DISK32(inode->ic_ctime), datestamp);
		break;
	    case 4:  /* set access date stamp */
		inode = inode_modify(inum);
		inode->ic_atime = DISK32(amiga_ds_to_unix_time(datestamp));
		break;
	    case 5:  /* get access date stamp */
		inode = inode_read(inum);
		unix_time_to_amiga_ds(DISK32(inode->ic_atime), datestamp);
		break;
	    default:
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_ACTION_NOT_KNOWN;
	}

	PRINT(("%cetDates: %d:%s %d  OP=%d day=%d min=%d tick=%d\n",
	       (ARG1 & 1) ? 'G' : 'S', lock, name, inum, ARG1,
	       datestamp->ds_Days, datestamp->ds_Minute, datestamp->ds_Tick));
#endif
}


/* PSetTimes() is nearly identical to PSetDates, but does so with UNIX
 * time as parameter (does not convert, but does adjust for timezone)
 */
void
PSetTimes(void)
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	ULONG	*ttime;
	char	*name;
	struct	BFFSLock *lock;
	struct	icommon *inode;

	if (superblock->fs_ronly) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	    return;
	}

	lock	= (struct BFFSLock *) BTOC(ARG2);
	name	= ((char *) BTOC(ARG3)) + 1;
	ttime	= (ULONG *) ARG4;
	inum	= path_parse(lock, name);

	if (inum == 0) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_OBJECT_NOT_FOUND;
	    return;
	}

	switch (ARG1) {
	    case 0:  /* set modify date stamp */
		inode = inode_modify(inum);
		inode->ic_mtime    = DISK32(ttime[0] - GMT * 3600);
		inode->ic_mtime_ns = DISK32(ttime[1]);
		break;
	    case 1:  /* get modify date stamp */
		inode = inode_read(inum);
		ttime[0] = DISK32(inode->ic_mtime) + GMT * 3600;
		ttime[1] = DISK32(inode->ic_mtime_ns);
		break;
	    case 2:  /* set change date stamp */
		inode = inode_modify(inum);
		inode->ic_ctime    = DISK32(ttime[0] - GMT * 3600);
		inode->ic_ctime_ns = DISK32(ttime[1]);
		break;
	    case 3:  /* get change date stamp */
		inode = inode_read(inum);
		ttime[0] = DISK32(inode->ic_ctime) + GMT * 3600;
		ttime[1] = DISK32(inode->ic_ctime_ns);
		break;
	    case 4:  /* set access date stamp */
		inode = inode_modify(inum);
		inode->ic_atime    = DISK32(ttime[0] - GMT * 3600);
		inode->ic_atime_ns = DISK32(ttime[1]);
		break;
	    case 5:  /* get access date stamp */
		inode = inode_read(inum);
		ttime[0] = DISK32(inode->ic_atime) + GMT * 3600;
		ttime[1] = DISK32(inode->ic_atime_ns);
		break;
	    default:
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_ACTION_NOT_KNOWN;
	}
	PRINT(("%cetTimes: %d:%s %d  OP=%d t=%d:%d\n",
		(ARG1 & 1) ? 'G' : 'S',
		lock, name, inum, ARG1, ttime[0], ttime[1]));

#endif
}


/*
 * Return file information on the specified file handle.
 * See also: PExamineObject()
 */
void
PExamineFh(void)
{
    struct BFFSfh     *fileh = (struct BFFSfh *) ARG1;
    struct BFFSLock   *lock  = (struct BFFSLock *) fileh->lock;
    FileInfoBlock_3_t *fib   = (FileInfoBlock_3_t *) BTOC(ARG2);

    examine_common(lock, fib, NULL);
}

/* These below are commented out because they are not implemented yet.
void
PDiskType(void) {
	PRINT(("DiskType\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PFhFromLock(void) {
	PRINT(("FhFromLock\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PCopyDirFh(void) {
	PRINT(("CopyDirFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PParentFh(void) {
	PRINT(("ParentFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PExamineAll(void) {
	PRINT(("ExamineAll\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PAddNotify(void) {
	PRINT(("AddNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void
PRemoveNotify(void) {
	PRINT(("RemoveNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}
*/

void
PGetDiskFSSM(void)
{
	PRINT(("Get the FFSM\n"));
	global.Res1 = (ULONG) BTOC(DevNode->dn_Startup);
}

void
PFreeDiskFSSM(void)
{
	PRINT(("Free the FFSM\n"));
	/* Nothing to do, since the FSSM was not allocated per caller */
}
