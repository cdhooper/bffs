#include <exec/memory.h>
#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <dos/filehandler.h>

#include "config.h"
#include "ufs.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "packets.h"
#include "system.h"
#include "handler.h"
#include "file.h"
#include "cache.h"
#include "stat.h"

char	*strchr();
ULONG	path_parse();
ULONG	parent_parse();
struct	direct *dir_next();
struct  PartInfo *partinfo;

extern	int GMT;
extern	int resolve_symlinks;
extern	struct DosPacket *pack;
extern	int minfree;

#define TYPE pack->dp_Type
#define ARG1 pack->dp_Arg1
#define ARG2 pack->dp_Arg2
#define ARG3 pack->dp_Arg3
#define ARG4 pack->dp_Arg4



void PUnimplemented() {
	PRINT2(("Unimplemented\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PLocateObject()
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

	PRINT(("lock=%x name='%s' ", lock, name));

	if (!(pinum = parent_parse(lock, name, &sname))) {
	    global.Res1 = DOSFALSE;
	    if (global.Res2 == 0)
		global.Res2 = ERROR_DIR_NOT_FOUND;
	    goto endlocate;
	}
	PRINT((" : pinum=%d subname=%s\n", pinum, sname));

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


void FreeLock(lock)
struct BFFSLock *lock;
{
    struct BFFSLock *current;
    struct BFFSLock *parent;

    if (lock == NULL) {
	PRINT2(("** ERROR - FreeLock called with NULL lock!\n"));
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
	PRINT(("Did not find lock in global locklist\n"));
	global.Res1 = DOSFALSE;
    } else
	FreeMem(current, sizeof(struct BFFSLock));
}


void PFreeLock()
{
	FreeLock((struct BFFSLock *) BTOC(ARG1));
}


void PExamineObject()
{
    ULONG  inum;
    char   *buf;
    ULONG  filesize;
    struct BFFSLock	 *lock;
    struct FileInfoBlock *fib;
    struct direct	 *dir_ent = NULL;
    struct icommon	 *inode;

    lock = (struct BFFSLock *) BTOC(ARG1);
    fib  = (struct FileInfoBlock *) BTOC(ARG2);

    UPSTAT(examines);
    inum = lock->fl_Key;
    fib->fib_DirOffset = MSb;

    inode = inode_read(inum);

    if (inum != ROOTINO)
	if ((DISK16(inode->ic_mode) & IFMT) == IFLNK)
	    if (resolve_symlinks) {
		filesize = IC_SIZE(inode);
		buf = (char *) AllocMem(filesize, MEMF_PUBLIC);
		file_read(inum, 0, filesize, buf);
		inum = file_find(lock->fl_Pinum, buf);
		FreeMem(buf, filesize);
		if (inum) {
		    dir_ent = dir_next(inode_read(global.Pinum), global.Poffset);
		    PRINT(("resolved link: %d to %d (%s)\n", lock->fl_Key,
			   inum, dir_ent->d_name));
		    inode = inode_read(inum);
		    lock->fl_Key     = inum;
		    lock->fl_Pinum   = global.Pinum;
		    lock->fl_Poffset = global.Poffset;
		} else {
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_OBJECT_NOT_FOUND;
		    return;
		}
	    } else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_IS_SOFT_LINK;
		return;
	    }
	else
	    dir_ent = dir_next(inode_read(lock->fl_Pinum), lock->fl_Poffset);

    FillInfoBlock(fib, lock, inode, dir_ent);
}


void PExamineNext()
{
    struct BFFSLock *lock;
    struct FileInfoBlock *fib;
    struct icommon *pinode;
    struct direct *dir_ent;

    lock = (struct BFFSLock *) BTOC(ARG1);
    fib  = (struct FileInfoBlock *) BTOC(ARG2);

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

getname:
    if (fib->fib_DirOffset >= IC_SIZE(pinode))  {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    dir_ent = dir_next(pinode, fib->fib_DirOffset);

    if (dir_ent == NULL) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    /* kludge to skip . and ..  In a galaxy far far away (When all
       AmigaDOS programs become link smart), we might be able to put
       the dots back in. */
#ifndef SHOWDOTDOT
    if (fib->fib_DirOffset == 0) {
	fib->fib_DirOffset += DISK16(dir_ent->d_reclen) +
	                      DISK16(((struct direct *) ((char *) dir_ent +
				   DISK16(dir_ent->d_reclen)))->d_reclen);
	PRINT(("skipped=%d\n", fib->fib_DirOffset));
	goto getname;
    }
#endif

    fib->fib_DirOffset += (int) DISK16(dir_ent->d_reclen);

    if (dir_ent->d_ino == 0) {
	if (dir_ent->d_reclen == 0)		/* Skip to next dir block */
	    fib->fib_DirOffset += DIRBLKSIZ - (fib->fib_DirOffset & (DIRBLKSIZ-1));
	goto getname;
    }

    FillInfoBlock(fib, lock, inode_read(DISK32(dir_ent->d_ino)), dir_ent);
}


void PFindInput()
{
    struct  FileHandle *fh;
    struct  BFFSLock *lock;
    char    *name;
    char    *bname;
    struct  BFFSfh *fileh;
    struct  icommon *inode;
    ULONG   inum;
    char    *buf;
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
    if (inode == NULL) {
	global.Res2 = ERROR_NO_FREE_STORE;
	PRINT(("Unable to find inode %d for read\n", inum));
	goto doserror;
    }

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
	PRINT(("FindInput: unable to allocate %d bytes!\n", sizeof(struct BFFSfh)));
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


void PFindOutput()
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
	PRINT(("FindOutput: unable to allocate %d bytes!\n", sizeof(struct BFFSfh)));
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
		PRINT(("FindOutput: parent dir not writable\n"));
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
		PRINT2(("INCON: FO: file created, but could not lock!\n"));
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
	if ((inode = inode_read(inum)) == NULL) {
	    PRINT2(("Unable to find inode %d for write\n", inum));
	    goto doserror;
	}

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


void PRead()
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


void PWrite()
{
    struct BFFSfh *fileh;
    char	  *buf;
    LONG	  size;

#ifdef RONLY
    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_DISK_WRITE_PROTECTED;
    return;
#else
    fileh = (struct BFFSfh *) ARG1;
    buf   =	     (char *) ARG2;
    size  =	       (LONG) ARG3;
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


void PSeek()
{
    ULONG 	   end;
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

    if ((movement >= 0) && (movement <= end)) {
	global.Res1 = fileh->current_position;
	fileh->current_position = movement;
    } else {
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
}


void PEnd()
{
	struct BFFSfh *fileh;

	fileh = (struct BFFSfh *) ARG1;

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 2097152)) {
	PRINT2(("** invalid real inode number %d given to End for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
	return;
    }
#endif

#ifndef RONLY
    if (fileh->access_mode == MODE_WRITE)
	truncate_file(fileh);
#endif

    FreeLock(fileh->lock);
    FreeMem(fileh, sizeof(struct BFFSfh));
}


void PParent()
{
    ULONG	    pinum;
    ULONG 	    ioffset;
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


void PDeviceInfo()
{
	extern int timing;
	struct InfoData *infodata;

	if (TYPE == ACTION_INFO)
		infodata = (struct InfoData *) BTOC(ARG2);
	else
		infodata = (struct InfoData *) BTOC(ARG1);

	infodata->id_NumSoftErrors	= 0L;
	infodata->id_UnitNumber 	= DISK_UNIT;
	if (superblock->fs_ronly)
		infodata->id_DiskState	= ID_WRITE_PROTECTED;
	else
		infodata->id_DiskState	= ID_VALIDATED;

	infodata->id_NumBlocks	   = DISK32(superblock->fs_dsize);
	infodata->id_NumBlocksUsed = (DISK32(superblock->fs_dsize) -
				      DISK32(superblock->fs_cstotal.cs_nbfree) *
				      DISK32(superblock->fs_frag) + (minfree ?
				      (DISK32(superblock->fs_minfree) *
				      DISK32(superblock->fs_dsize) / 100) : 0) );
	infodata->id_BytesPerBlock = FSIZE;
	infodata->id_DiskType	   = ID_FFS_DISK;
	infodata->id_VolumeNode    = CTOB(VolNode);  /* BPTR */
	infodata->id_InUse	   = timing;
}


void PDeleteObject()
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

	PRINT(("delete %s, i=%d offset=%d nlink=%d\n",
		name, inum, ioffset, DISK16(inode->ic_nlink)));

#ifdef INTEL
	inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);
#else
	inode->ic_nlink--;
#endif
	if ((DISK16(inode->ic_mode) & IFMT) == IFDIR) { /* it's a dir */
		pinode = inode_modify(pinum);
#ifdef INTEL
		inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);
		pinode->ic_nlink = DISK16(DISK16(pinode->ic_nlink) - 1);
#else
		inode->ic_nlink--;			/* for "."  */
		pinode->ic_nlink--;			/* for ".." */
#endif

	}
	inode = inode_read(inum);
	if (DISK16(inode->ic_nlink) < 1) {
	    if (((DISK16(inode->ic_mode) & IFMT) != IFCHR) &&
		((DISK16(inode->ic_mode) & IFMT) != IFBLK) &&
		((DISK16(inode->ic_mode) & IFMT) != IFLNK))
		file_deallocate(inum);
	    if (!bsd44fs ||
		(((DISK16(inode->ic_mode) & IFMT) == IFLNK) &&
		  (IC_SIZE(inode) >= MAXSYMLINKLEN)))
		file_deallocate(inum);
	    inum_free(inum);
	}
#endif
}


void PMoreCache()
{
	if (abs(ARG1) > 9999) {
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
	    PRINT(("morecache: cache=%d amount=%d ", cache_size, ARG1));
	    cache_size += (ARG1 * DEV_BSIZE / FSIZE);

	    if (cache_size < 8)
		cache_size = 8;

	    global.Res1 = cache_size * FSIZE / DEV_BSIZE;

	    PRINT(("-> cache=%d\n", cache_size));

	    cache_adjust();
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
void PCreateDir()
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
	int	pos;
	int	inum;
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
		PRINT(("file %s already exists!\n", name));
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
#ifdef INTEL
			inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
#else
			inode->ic_nlink++;
#endif
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
void PRenameObject()
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
#ifdef INTEL
			    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) - 1);
			    inode = inode_modify(newpinum);
			    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
#else
			    inode->ic_nlink--;
			    inode = inode_modify(newpinum);
			    inode->ic_nlink++;
#endif
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
void PRenameDisk()
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	int	newlen;
	char	*temp;
	char	*newname;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	newname = ((char *) BTOC(ARG1)) + 1;
/*	PRINT(("rename: New name=%.*s\n", *(newname - 1), newname)); */
	if (VolNode == NULL) {
		PRINT(("rename called with NULL Volnode!!\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = 0;
		return;
	}

	newlen = *(newname - 1) + 1;
	if (newlen >= MAXMNTLEN)
		newlen = MAXMNTLEN - 1;

	superblock->fs_fsmnt[0] = '/';

	strncpy(superblock->fs_fsmnt + MAXMNTLEN - newlen, newname, newlen);
	superblock->fs_fsmnt[MAXMNTLEN - 1] = '\0';
	superblock->fs_fsmnt[MAXMNTLEN - newlen - 1] = '\0';
	superblock->fs_fmod++;

	temp = (char *) AllocMem(MAXMNTLEN, MEMF_PUBLIC);
	if (temp == NULL) {
		PRINT(("RenameDisk: unable to allocate %d bytes!\n",
			newlen + 1));
		global.Res1 = DOSFALSE;
		return;
	}

	strcpy(temp + 1, superblock->fs_fsmnt + MAXMNTLEN - newlen);
	temp[0] = (char) strlen(temp + 1);

	VolNode->dl_Name = CTOB(temp);
	FreeMem(volumename, MAXMNTLEN);
	volumename = temp;
#endif
}


/* SetProtect()
 *	This packet sets permissions on the specified file,
 *	given a lock and a path relative to that lock.
 */
void PSetProtect()
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
		temp &= ~ (ICHG | ISUID | ISVTX | 0777);

		temp |= (mask & FIBF_ARCHIVE) ? 0 : (IEXEC >> 3);
		temp |= (mask & FIBF_SCRIPT)  ? ISUID : 0;
		temp |= (mask & FIBF_PURE)    ? ISVTX : 0;
		temp |= (mask & FIBF_READ)    ? 0 : IREAD;
		temp |= (mask & FIBF_WRITE)   ? 0 : IWRITE;
		temp |= (mask & FIBF_DELETE)  ? 0 : IWRITE;
		temp |= (mask & FIBF_EXECUTE) ? 0 : IEXEC;
		temp |= (mask & FIBF_GRP_READ)    ? 0 : (IREAD  >> 3);
		temp |= (mask & FIBF_GRP_WRITE)   ? 0 : (IWRITE >> 3);
		temp |= (mask & FIBF_GRP_DELETE)  ? 0 : (IWRITE >> 3);
		temp |= (mask & FIBF_GRP_EXECUTE) ? 0 : (IEXEC  >> 3);
		temp |= (mask & FIBF_OTR_READ)    ? 0 : (IREAD  >> 6);
		temp |= (mask & FIBF_OTR_WRITE)   ? 0 : (IWRITE >> 6);
		temp |= (mask & FIBF_OTR_DELETE)  ? 0 : (IWRITE >> 6);
		temp |= (mask & FIBF_OTR_EXECUTE) ? 0 : (IEXEC  >> 6);
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
void PSetPerms()
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
 *	This packet sets full UNIX permissions on the specified
 *	file, given a lock and a path relative to that lock.
 */
void PGetPerms()
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
void PSetOwner()
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
	uid_t	owner;
	gid_t	group;

	lock = (struct BFFSLock *) BTOC(ARG2);
	name = ((char *) BTOC(ARG3)) + 1;
	both = (ULONG) ARG4;

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		owner = (uid_t) (both >> 16);
		group = (gid_t) (both & 65535);
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
void PCurrentVolume()
{
	if (VolNode == NULL) {
		PRINT(("CurrentVolume called with NULL Volnode!\n"));
		NewVolNode();
	}
	global.Res1 = CTOB(VolNode);
}


/* CopyDir()
 *	This packet is used to create a second shared lock on an
 *	object identical to the first.
 */
void PCopyDir()
{
	struct BFFSLock *lock;

	lock = (struct BFFSLock *) BTOC(ARG1);

	global.Res1 = CTOB(CreateLock(lock->fl_Key, lock->fl_Access,
			   lock->fl_Pinum, lock->fl_Poffset));
}


/* Inhibit()
 *	This packet tells the filesystem to keep its hands off the
 *	media until another inhibit packet is sent with flag=0
 */
void PInhibit()
{
	int flag;

	flag = ARG1;

	if (flag)
		PRINT(("INHIBIT\n"));
	else
		PRINT(("UNINHIBIT\n"));

	if (inhibited)
		if (flag) {
			PRINT(("Already inhibited!\n"));
			global.Res1 = DOSFALSE;
			global.Res2 = 0;
		} else {
			inhibited = 0;
			open_filesystem();
		}
	else
		if (flag) {
			close_files();
			close_filesystem();
			inhibited = 1;
		} else {
			PRINT(("Already uninhibited!\n"));
			global.Res1 = DOSFALSE;
			global.Res2 = 0;
		}
}


/* DiskChange()
 *	This packet tells the filesystem that the user has changed
 *	the media in a removable drive.  It is also useful after a
 *	fsck of the media where fsck modified some data.
 *	Any dirty buffers still in the cache are destroyed!
 */
void PDiskChange()
{
	extern int cache_item_dirty;

	if (cache_item_dirty) {
		PRINT(("WARNING!  Loss of %d dirty frags still in cache!\n"));
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
void PFormat()
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
void PWriteProtect()
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
	superblock->fs_fmod++;
#endif
}


/* IsFilesystem()
 *	Always answer yes, unless we failed to open the device.
 */
void PIsFilesystem()
{
	if (dev_openfail) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
	}
}


/* Die()
 *	Go away and stop processing packets.
 */
void PDie()
{
	close_files();
	close_filesystem();
	RemoveVolNode();
	receiving_packets = 0;
}


/* Flush()
 *	Manually write the contents of the cache out to disk.
 */
void PFlush()
{
#ifndef RONLY
	if (!superblock)
		return;
	UPSTAT(flushes);
	superblock_flush();
	cache_cg_flush();
	cache_flush();
#endif
}


void PSameLock()
{
	ULONG maxi;
	struct BFFSLock *lock1;
	struct BFFSLock *lock2;

	lock1 = (struct BFFSLock *) BTOC(ARG1);
	lock2 = (struct BFFSLock *) BTOC(ARG2);

	PRINT(("same_lock: comparing %d and %d\n", lock1->fl_Key, lock2->fl_Key));

	/* Something (ie: WorkBench) is hosed.  This should not be hardcoded! */
global.Res1 = LOCK_SAME;
return;

	maxi = DISK32(superblock->fs_ipg) * DISK32(superblock->fs_ncg);

	if ((lock1->fl_Key == lock2->fl_Key) &&
	    (lock1->fl_Access == lock2->fl_Access))
		global.Res1 = LOCK_SAME;
	else if (((lock1->fl_Pinum > 1) && (lock1->fl_Pinum < maxi)) &&
		((lock2->fl_Pinum > 1) && (lock2->fl_Pinum <= maxi)))
		global.Res1 = LOCK_SAME_HANDLER;
	else
		global.Res1 = LOCK_DIFFERENT;
}

void PFilesysStats()
{
	extern struct	cache_set *cache_stack_tail;
	extern struct	cache_set *hashtable[];
	extern int	cache_cg_size;
	extern int	cache_item_dirty;
	extern int	cache_alloced;
	extern ULONG	pmax;
	extern int	case_independent;
	extern int	unixflag;
	extern int	cache_used;
	extern int	timer_secs;
	extern int	timer_loops;
	extern int	link_comments;
	extern int	inode_comments;
	extern int	GMT;
	extern int	og_perm_invert;

	stat->superblock	= (ULONG) superblock;
	stat->cache_head	= (ULONG) &cache_stack_tail;
	stat->cache_hash	= (ULONG *) hashtable;
	stat->cache_size	= (ULONG *) &cache_size;
	stat->cache_cg_size	= (ULONG *) &cache_cg_size;
	stat->cache_item_dirty	= (ULONG *) &cache_item_dirty;
	stat->cache_alloced	= (ULONG *) &cache_alloced;
	stat->disk_poffset	= (ULONG *) &poffset;
	stat->disk_pmax		= (ULONG *) &pmax;
	stat->unixflag		= (ULONG *) &unixflag;
	stat->resolve_symlinks	= (ULONG *) &resolve_symlinks;
	stat->case_independent	= (ULONG *) &case_independent;
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

void PCreateObject()
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
			PRINT(("bad file type %d for '%s'\n", st_type, name));
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

void PMakeLink()
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
#ifdef INTEL
	    inode->ic_nlink = DISK16(DISK16(inode->ic_nlink) + 1);
#else
	    inode->ic_nlink++;
#endif
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

	if (bsd44fs && (namelen < MAXSYMLINKLEN)) {
	    inode = inode_modify(inum);
	    strcpy((char *) inode->ic_db, name_from);
	    IC_SETSIZE(inode, namelen);
	    inode->ic_mode = DISK16((DISK16(inode->ic_mode) & ~IFMT) | IFLNK);
	    return;
	}

	if (file_write(inum, 0, namelen + 1, name_from) == namelen + 1) {
	    file_block_retract(inum);
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

void PReadLink()
{
	int	len;
	ULONG	inum;
	struct	BFFSLock *lock;

	linkname = NULL;

	lock = (struct BFFSLock *) BTOC(ARG1);
	read_link = 1;
	inum = path_parse(lock, ARG2);
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

	if (linkname) {
	    len = strlen(linkname);
	    if (len > ARG4) {
		global.Res1 = -2;
		global.Res2 = ERROR_OBJECT_TOO_LARGE;
		return;
	    }
	    global.Res1 = len + 1;
	    strcpy((char *) ARG3, linkname);
	    PRINT1(("return name is %s %x\n", ARG3, ARG3));
	    FreeMem(linkname, len + 2);
	    return;
	}

	global.Res1 = -1;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
}

void PNil()
{
}


void PSetFileSize()
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	PRINT(("SetFileSize\n"));
	PRINT(("a=%d %s\n", ARG1, ((char *) BTOC(ARG1)) + 1));
	PRINT(("b=%d %s\n", ARG2, ((char *) BTOC(ARG2)) + 1));
	PRINT(("c=%d %s\n", ARG3, ((char *) BTOC(ARG3)) + 1));
	PRINT(("d=%d %s\n", ARG4, ((char *) BTOC(ARG4)) + 1));

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
#endif
}


void PSetDate()
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
		return;
		inode = inode_modify(inum);
		inode->ic_mtime = DISK32((datestamp->ds_Days + 2922) * 86400 +
				         (datestamp->ds_Minute * 60) - GMT * 3600 +
				         datestamp->ds_Tick / TICKS_PER_SECOND);
	}

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
#endif
}

void PSetDates()
{
#ifdef RONLY
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
#else
	ULONG	inum;
	ULONG	ttime;
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
	name	  = 	       ((char *) BTOC(ARG3)) + 1;
	datestamp = (struct DateStamp *) ARG4;
	inum = path_parse(lock, name);

	if (inum == 0) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	PRINT(("SetDates: %d:%s %d  OPER=%d\n", lock, name, inum, ARG1));
	PRINT(("day=%d min=%d tick=%d\n", datestamp->ds_Days,
		datestamp->ds_Minute, datestamp->ds_Tick));


	if (inum) {
	    switch (ARG1) {
		case 0: /*    set modify date stamp */
		    inode = inode_modify(inum);
		    inode->ic_mtime =
			DISK32((datestamp->ds_Days + 2922) * 86400 +
			       (datestamp->ds_Minute * 60) - GMT * 3600 +
			       datestamp->ds_Tick / TICKS_PER_SECOND);
		    break;
		case 1: /* return modify date stamp */
		    ttime = DISK32(inode->ic_mtime) + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		case 2: /*    set change date stamp */
		    inode = inode_modify(inum);
		    inode->ic_ctime =
			DISK32((datestamp->ds_Days + 2922) * 86400 +
			       (datestamp->ds_Minute * 60) - GMT * 3600 +
			       datestamp->ds_Tick / TICKS_PER_SECOND);
		    break;
		case 3: /* return change date stamp */
		    ttime = DISK32(inode->ic_ctime) + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		case 4: /*    set access date stamp */
		    inode = inode_modify(inum);
		    inode->ic_atime =
			DISK32((datestamp->ds_Days + 2922) * 86400 +
			       (datestamp->ds_Minute * 60) - GMT * 3600 +
			       datestamp->ds_Tick / TICKS_PER_SECOND);
		    break;
		case 5: /* return access date stamp */
		    ttime = DISK32(inode->ic_atime) + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		default:
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_ACTION_NOT_KNOWN;
	    }
	}
#endif
}


/* Nearly identical to PSetDates, but does so with UNIX Date
   as parameter (does not convert at all) */
void PSetTimes()
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

	lock	=  (struct BFFSLock *) BTOC(ARG2);
	name	= 	       ((char *) BTOC(ARG3)) + 1;
	ttime	=  (ULONG *) ARG4;
	inum	= path_parse(lock, name);

	if (inum == 0) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	PRINT2(("SetTimes: %d:%s %d  OPER=%d t=%x t=%d:%d\n",
		lock, name, inum, ARG1, ttime, ttime[0], ttime[1]));


	if (inum) {
	    switch (ARG1) {
		case 0: /*    set modify date stamp */
		    inode = inode_modify(inum);
		    inode->ic_mtime	= DISK32(ttime[0]) - GMT * 3600;
		    inode->ic_mtime_ns	= DISK32(ttime[1]);
		    break;
		case 1: /* return modify date stamp */
		    inode = inode_read(inum);
		    ttime[0] = DISK32(inode->ic_mtime) + GMT * 3600;
		    ttime[1] = DISK32(inode->ic_mtime_ns);
		    break;
		case 2: /*    set change date stamp */
		    inode = inode_modify(inum);
		    inode->ic_ctime	= DISK32(ttime[0]) - GMT * 3600;
		    inode->ic_ctime_ns	= DISK32(ttime[1]);
		    break;
		case 3: /* return change date stamp */
		    inode = inode_read(inum);
		    ttime[0] = DISK32(inode->ic_ctime) + GMT * 3600;
		    ttime[1] = DISK32(inode->ic_ctime_ns);
		    break;
		case 4: /*    set access date stamp */
		    inode = inode_modify(inum);
		    inode->ic_atime	= DISK32(ttime[0]) - GMT * 3600;
		    inode->ic_atime_ns	= DISK32(ttime[1]);
		    break;
		case 5: /* return access date stamp */
		    inode = inode_read(inum);
		    ttime[0] = DISK32(inode->ic_atime) + GMT * 3600;
		    ttime[1] = DISK32(inode->ic_atime_ns);
		    break;
		default:
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_ACTION_NOT_KNOWN;
	    }
	}
#endif
}


/*
 *  This code is mainly identical to that of PExamine()
 */
void PExamineFh()
{
    struct BFFSfh *fileh;
    ULONG  inum;
    char   *buf;
    ULONG  filesize;
    struct BFFSLock	 *lock;
    struct FileInfoBlock *fib;
    struct direct	 *dir_ent = NULL;
    struct icommon	 *inode;

    PRINT(("ExamineFh\n"));

    fileh = (struct BFFSfh *) ARG1;
    lock = (struct BFFSLock *) fileh->lock;
    fib  = (struct FileInfoBlock *) BTOC(ARG2);

    UPSTAT(examines);
    inum = lock->fl_Key;
    fib->fib_DirOffset = MSb;

    inode = inode_read(inum);

    if (inum != ROOTINO)
	if ((DISK16(inode->ic_mode) & IFMT) == IFLNK)
	    if (resolve_symlinks) {
		filesize = IC_SIZE(inode);
		buf = (char *) AllocMem(filesize, MEMF_PUBLIC);
		file_read(inum, 0, filesize, buf);
		inum = file_find(lock->fl_Pinum, buf);
		FreeMem(buf, filesize);
		if (inum) {
		    dir_ent = dir_next(inode_read(global.Pinum), global.Poffset);
		    PRINT(("resolved link: %d to %d (%s)\n", lock->fl_Key,
			   inum, dir_ent->d_name));
		    inode = inode_read(inum);
		    lock->fl_Key     = inum;
		    lock->fl_Pinum   = global.Pinum;
		    lock->fl_Poffset = global.Poffset;
		} else {
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_OBJECT_NOT_FOUND;
		    return;
		}
	    } else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_IS_SOFT_LINK;
		return;
	    }
	else
	    dir_ent = dir_next(inode_read(lock->fl_Pinum), lock->fl_Poffset);

    FillInfoBlock(fib, lock, inode, dir_ent);
}

/* These below are commented out because they are not implemented yet.
void PDiskType() {
	PRINT(("DiskType\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PFhFromLock() {
	PRINT(("FhFromLock\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PCopyDirFh() {
	PRINT(("CopyDirFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PParentFh() {
	PRINT(("ParentFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PExamineAll() {
	PRINT(("ExamineAll\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PAddNotify() {
	PRINT(("AddNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PRemoveNotify() {
	PRINT(("RemoveNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}
*/

void PGetDiskFSSM()
{
	PRINT(("Get the FFSM\n"));
	global.Res1 = STARTUPMSG;
}

void PFreeDiskFSSM()
{
	PRINT(("Free the FFSM\n"));
}
#ifdef NOT
void PExObject()
{
	struct	BFFSLock *lock;
	struct	FileInfoBlock *fib;
	unsigned long *rbuf;

	PExamineObject();

	lock = (struct BFFSLock *) BTOC(ARG1);
	fib  = (struct FileInfoBlock *) BTOC(ARG2);
	rbuf = (unsigned long *) BTOC(ARG3);

	PRINT2(("LSGoober "));
	PRINT2(("ARG1=0x%08x ", ARG1));
	PRINT2(("ARG2=0x%08x ", ARG2));
	PRINT2(("ARG3=0x%08x ", ARG3));
	PRINT2(("ARG4=0x%08x\n", ARG4));
	PRINT2(("ino=%d pino=%d poff=%d\n", lock->fl_Key, lock->fl_Pinum, lock->fl_Poffset));
	PRINT2(("key=%d size=%d owner=%d\n", fib->fib_DiskKey, fib->fib_Size, fib->fib_OwnerUID));

/*
	rbuf[0] = 33;
	rbuf[1] = 5;
*/

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
}

void PExNext()
{
	struct	BFFSLock *lock;
	struct	FileInfoBlock *fib;
	unsigned long *rbuf;
	static int buf[32];

	lock = (struct BFFSLock *) BTOC(ARG1);
	fib  = (struct FileInfoBlock *) BTOC(ARG2);
	rbuf = (unsigned long *) BTOC(ARG3);

	PRINT2(("GetAttributes "));
	PRINT2(("ARG1=0x%08x ", ARG1));
	PRINT2(("ARG2=0x%08x ", ARG2));
	PRINT2(("ARG3=0x%08x ", ARG3));
	PRINT2(("ARG4=0x%08x\n", ARG4));
	PRINT2(("0ino=%d pino=%d poff=%d\n", lock->fl_Key, lock->fl_Pinum, lock->fl_Poffset));
	PRINT2(("key=%d size=%d owner=%d\n", fib->fib_DiskKey, fib->fib_Size, fib->fib_OwnerUID));


/*
	PRINT2(("nam=%.6s\n", ARG3));
ARG3 unknown, not string

at most 64 bytes in length; return UID buffer?
*/


	buf[0] = 1640;
	buf[1] = 10;


	global.Res1 = (unsigned long) buf;
/*
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_ACTION_NOT_KNOWN;
*/
}
#endif

void PExObject()
{
    struct FileInfoBlock *fib;
    int index;

    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_ACTION_NOT_KNOWN;
    return;

    fib  = (struct FileInfoBlock *) BTOC(ARG2);

    PExamineObject();

    if (global.Res1 != DOSFALSE) {
	fib->is_remote = 1;
	fib->mode = 0;
	fib->fib_OwnerUID = 0;
	fib->fib_OwnerGID = 0;
	fib->fib_Protection = 0;
	for (index = 0; index < 80; index++)
		fib->fib_Comment[index] = 0;
	for (index = 0; index < 24; index++)
		fib->fib_Reserved[index] = 0;
    }
}

void PExNext()
{
    struct FileInfoBlock *fib;
    int index;

    global.Res1 = DOSFALSE;
    global.Res2 = ERROR_ACTION_NOT_KNOWN;
    return;

    fib  = (struct FileInfoBlock *) BTOC(ARG2);

    PExamineNext();

    if (global.Res1 != DOSFALSE) {
	fib->is_remote = 1;
	fib->mode = 0;
	fib->fib_OwnerUID = 0;
	fib->fib_OwnerGID = 0;
	fib->fib_Protection = 0;
	for (index = 0; index < 80; index++)
		fib->fib_Comment[index] = 0;
	for (index = 0; index < 24; index++)
		fib->fib_Reserved[index] = 0;
    }
}

