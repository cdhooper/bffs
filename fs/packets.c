#include <exec/memory.h>
#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <dos/filehandler.h>

#include "debug.h"
#include "ufs.h"
#include "packets.h"
#include "system.h"
#include "handler.h"
#include "file.h"
#include "cache.h"

char	*strchr();
ULONG	path_parse();
ULONG	parent_parse();
struct	direct *dir_next();

extern	int GMT;
extern	int resolve_symlinks;
extern	struct DosPacket *pack;
extern	int minfree;


void PLocateObject(Block, Bname, mode)
ULONG	Block;
ULONG	Bname;
ULONG	mode;
{
	struct	BFFSLock *lock;
	char	*name;
	char	*sname;
	char	cho;
	int	inum;
	int	pinum;

	UPSTAT(locates);
	lock = (struct BFFSLock *) BTOC(Block);
	name = ((char *) BTOC(Bname)) + 1;

	cho = *(name + *(name - 1));
	*(name + *(name - 1)) = '\0';

	PRINT(("lock=%x name='%s' ", lock, name));

	if (!(pinum = parent_parse(lock, name, &sname))) {
		global.Res1 = DOSFALSE;
		if (global.Res2 == 0)
			global.Res2 = ERROR_DIR_NOT_FOUND;
		*(name + *(name - 1)) = cho;
		return;
	}
	PRINT((" : pinum=%d subname=%s\n", pinum, sname));

	if ((superblock->fs_ronly) && (mode == ACCESS_WRITE)) {
		global.Res1 = DOSFALSE;
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		*(name + *(name - 1)) = cho;
		return;
	}

	if ((inum = file_name_find(pinum, sname)) == NULL) {
		global.Res1 = DOSFALSE;
		if (global.Res2 == 0)
			global.Res2 = ERROR_OBJECT_NOT_FOUND;
	} else {
		global.Res1 = CTOB(CreateLock(inum, mode, global.Pinum,
				   global.Poffset));
	}

	*(name + *(name - 1)) = cho;
}


void PFreeLock(lock)
struct BFFSLock *lock;
{
    struct BFFSLock *current;
    struct BFFSLock *parent;

    if (lock == NULL) {
	PRINT(("** ERROR - PFreeLock called with NULL lock!\n"));
	return;
    }

/*
    PRINT(("  PFreeLock: inum=%d pinum=%d poffset=%d\n", lock->fl_Key,
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


void PExamineObject(Block, Bfib)
ULONG Block;
ULONG Bfib;
{
    ULONG  inum;
    char   *buf;
    ULONG  fsize;
    struct BFFSLock	 *lock;
    struct FileInfoBlock *fib;
    struct direct	 *dir_ent = NULL;
    struct icommon	 *inode;

    lock = (struct BFFSLock *) BTOC(Block);
    fib  = (struct FileInfoBlock *) BTOC(Bfib);

    UPSTAT(examines);
    inum = lock->fl_Key;
    fib->fib_DiskKey = MSb;

    inode = inode_read(inum);

    if (inum != ROOTINO)
	if ((inode->ic_mode & IFMT) == IFLNK)
	    if (resolve_symlinks) {
		fsize = IC_SIZE(inode);
		buf = (char *) AllocMem(fsize, MEMF_PUBLIC);
		file_read(inum, 0, fsize, buf);
		inum = file_find(lock->fl_Pinum, buf);
		FreeMem(buf, fsize);
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


void PExamineNext(Block, Bfib)
ULONG Block;
ULONG Bfib;
{
    struct BFFSLock *lock;
    struct FileInfoBlock *fib;
    struct icommon *pinode;
    struct direct *dir_ent;

    lock = (struct BFFSLock *) BTOC(Block);
    fib  = (struct FileInfoBlock *) BTOC(Bfib);

    UPSTAT(examinenexts);
    if (fib->fib_DiskKey & MSb) {
	if (!(fib->fib_DirEntryType & (ST_USERDIR | ST_ROOT))) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_NO_MORE_ENTRIES;
	    return;
	}
	fib->fib_DiskKey = 0;
    }

    pinode = inode_read(lock->fl_Key);
    if ((pinode == NULL) || ((pinode->ic_mode & IFMT) != IFDIR)) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DIR_NOT_FOUND;  /* NO_DEFAULT_DIR OBJECT_WRONG_TYPE */
	return;
    }

getname:
    if (fib->fib_DiskKey >= IC_SIZE(pinode))  {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    dir_ent = dir_next(pinode, fib->fib_DiskKey);

    if (dir_ent == NULL) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    /* kludge to skip . and ..  In a galaxy far far away (When all
       AmigaDOS programs become link smart), we might be able to put
       the dots back in. */
    if (fib->fib_DiskKey == 0) {
	fib->fib_DiskKey += ((int) dir_ent->d_reclen +
	                     (int) ((struct direct *) ((char *) dir_ent +
						     dir_ent->d_reclen))->d_reclen);
	goto getname;
    }

    fib->fib_DiskKey += (int) dir_ent->d_reclen;

    if (dir_ent->d_ino == 0) {
	if (dir_ent->d_reclen == 0)		/* Skip to next dir block */
	    fib->fib_DiskKey += DIRBLKSIZ - (fib->fib_DiskKey & (DIRBLKSIZ-1));
	goto getname;
    }

    FillInfoBlock(fib, lock, inode_read(dir_ent->d_ino), dir_ent);
}


void PFindInput(Bfh, Block, Bname)
ULONG Bfh;
ULONG Block;
ULONG Bname;
{
    struct  FileHandle *fh;
    struct  BFFSLock *lock;
    char    *name;
    struct  BFFSfh *fileh;
    struct  icommon *inode;
    ULONG   inum;
    char    *buf;
    char    cho;

    UPSTAT(findinput);
    fh   = (struct FileHandle *) BTOC(Bfh);
    lock =   (struct BFFSLock *) BTOC(Block);
    name =	       ((char *) BTOC(Bname)) + 1;

    fileh = (struct BFFSfh *) AllocMem(sizeof(struct BFFSfh), MEMF_PUBLIC);
    if (fileh == NULL) {
	PRINT(("FindInput: unable to allocate %d bytes!\n", sizeof(struct BFFSfh)));
	global.Res1 = DOSFALSE;
	return;
    }

    cho = *(name + *(name - 1));
    *(name + *(name - 1)) = '\0';

    if (inum = path_parse(lock, name)) {
	fileh->lock = CreateLock(inum, ACCESS_READ,
				 global.Pinum, global.Poffset);
	if (fileh->lock == NULL) {
	    global.Res2 = ERROR_LOCK_COLLISION;
	    goto doserror;
	}
	fileh->lock->fl_Fileh = fileh;
    } else {
	if (global.Res2 == 0)
	    global.Res2 = ERROR_OBJECT_NOT_FOUND;
	goto doserror;
    }

    inode = inode_read(inum);
    if (inode == NULL) {
	global.Res2 = ERROR_NO_FREE_STORE;
	PRINT(("Unable to find inode %d for read\n", fileh->lock->fl_Key));
	goto doserror;
    }

    if ((inode->ic_mode & IFMT) != IFREG) {	/* dir or special */
	PRINT(("attempt to open special file %s for read\n", name));
	global.Res2 = ERROR_OBJECT_WRONG_TYPE;
	goto doserror;
    } else if ((inode->ic_mode & IREAD) == 0) {
	global.Res2 = ERROR_READ_PROTECTED;
	goto doserror;
    }

    fileh->current_position = 0;
    fileh->maximum_position = 0;
    fileh->access_mode	    = MODE_READ;   /* read */
    fileh->truncate_mode    = MODE_UPDATE; /* don't truncate at close */
    fileh->real_inum	    = fileh->lock->fl_Key;
    fh->fh_Arg1		    = fileh;

    *(name + *(name - 1)) = cho;
    return;

    doserror:
    *(name + *(name - 1)) = cho;
    global.Res1 = DOSFALSE;
    FreeMem(fileh, sizeof(struct BFFSfh));
}


void PFindOutput(Bfh, Block, Bname)
ULONG Bfh;
ULONG Block;
ULONG Bname;
{
    struct  FileHandle *fh;
    struct  BFFSLock *lock;
    char    *name;
    char    cho;
    struct  BFFSfh *fileh;
    struct  icommon *inode;
    char    *sname;
    LONG    pinum;
    LONG    inum;
    ULONG   poffset;

    UPSTAT(findoutput);
    fh   = (struct FileHandle *) BTOC(Bfh);
    lock =   (struct BFFSLock *) BTOC(Block);
    name =	       ((char *) BTOC(Bname)) + 1;

    if (superblock->fs_ronly) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    fileh = (struct BFFSfh *) AllocMem(sizeof(struct BFFSfh), MEMF_PUBLIC);
    if (fileh == NULL) {
	PRINT(("FindOutput: unable to allocate %d bytes!\n", sizeof(struct BFFSfh)));
	global.Res1 = DOSFALSE;
	return;
    }

    cho = *(name + *(name - 1));
    *(name + *(name - 1)) = '\0';

    if (!(pinum = parent_parse(lock, name, &sname))) {
	global.Res2 = ERROR_DIR_NOT_FOUND;
	goto doserror;
    }

    inum = dir_name_search(pinum, sname);
    if (inum) {
	fileh->lock = CreateLock(inum, ACCESS_WRITE, global.Pinum,
				     global.Poffset);
	fileh->lock->fl_Fileh = fileh;
    } else
	fileh->lock = NULL;

    if (fileh->lock == NULL) {
	if (dir_name_is_illegal(sname)) {
		PRINT(("given bad filename %s", sname));
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

	inum = inum_new(pinum, I_FILE);
	if (inum == 0) {
		PRINT(("failed to get a new inode from inum_new()\n"));
		global.Res2 = ERROR_NO_MORE_ENTRIES;
		goto doserror;
	}

	if (poffset = dir_create(pinum, sname, inum)) {
	    fileh->lock = CreateLock(inum, ACCESS_WRITE, pinum, poffset);

	    if (fileh->lock == NULL) {
		PRINT(("INCON: FindOutput: file created, but could not lock!\n"));
		inum_free(inum);
		global.Res2 = ERROR_SEEK_ERROR;
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
    }

    inum = fileh->lock->fl_Key;

    if ((inode = inode_read(inum)) == NULL) {
	PRINT(("Unable to find inode %d for write\n", inum));
	goto doserror;
    }

    if ((inode->ic_mode & IFMT) != IFREG) {	/* dir or special */
	PRINT(("attempt to open special file %s for write\n", name));
	global.Res2 = ERROR_OBJECT_WRONG_TYPE;
	goto doserror;
    } else if ((inode->ic_mode & IWRITE) == 0) {
	PRINT(("%s is write protected\n", name));
	global.Res2 = ERROR_WRITE_PROTECTED;
	goto doserror;
    }

    fileh->current_position = 0;
    fileh->maximum_position = 0;
    fileh->access_mode	    = MODE_READ;      /* start out read */
    fileh->truncate_mode    = MODE_TRUNCATE;  /* truncate file to max seek */
    fileh->real_inum	    = fileh->lock->fl_Key;
    fh->fh_Arg1		    = fileh;

    *(name + *(name - 1)) = cho;
    return;

    doserror:
    *(name + *(name - 1)) = cho;
    global.Res1 = DOSFALSE;
    FreeMem(fileh, sizeof(struct BFFSfh));
}


void PRead(fileh, buf, size)
struct BFFSfh *fileh;
char *buf;
LONG size;
{
    UPSTAT(reads);

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 1048576)) {
	PRINT(("** invalid real inode number %d given to Read for file %d\n",
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


void PWrite(fileh, buf, size)
struct BFFSfh *fileh;
char *buf;
LONG size;
{
    UPSTAT(writes);

    if (superblock->fs_ronly) {
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 1048576)) {
	PRINT(("** invalid real inode number %d given to Write for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
#endif

    if (fileh->access_mode == MODE_READ) {
	fileh->access_mode = MODE_WRITE;  /* change to write if not yet written */
	file_block_extend(fileh->real_inum);
    }

    global.Res1 = file_write(fileh->real_inum, fileh->current_position, size, buf);

    if (global.Res1 >= 0) {
	fileh->current_position += global.Res1;
	if (fileh->current_position > fileh->maximum_position)
		fileh->maximum_position	= fileh->current_position;
    } else {
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
}


void PSeek(fileh, movement, mode)
struct BFFSfh *fileh;
LONG movement;
LONG mode;
{
    ULONG  end;
    struct icommon *inode;

#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 1048576)) {
	PRINT(("** invalid real inode number %d given to Seek for file %d\n",
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


void PEnd(fileh)
struct BFFSfh *fileh;
{
#ifndef FAST
    if ((fileh->real_inum < 3) || (fileh->real_inum > 1048576)) {
	PRINT(("** invalid real inode number %d given to End for file %d\n",
		fileh->real_inum, fileh->lock->fl_Key));
	global.Res1 = -1;
	global.Res2 = ERROR_SEEK_ERROR;
    }
#endif

    truncate_file(fileh);

    PFreeLock(fileh->lock);
    FreeMem(fileh, sizeof(struct BFFSfh));
}


void PParent(lock)
struct BFFSLock *lock;
{
    ULONG pinum;
    ULONG poffset;

    if (lock->fl_Key == ROOTINO)
	global.Res1 = 0L;
    else {
	pinum = dir_name_search(lock->fl_Pinum, "..");
	if (pinum) {
	    poffset = dir_inum_search(pinum, lock->fl_Pinum);
	    global.Res1 = CTOB(CreateLock(lock->fl_Pinum, SHARED_LOCK,
					  pinum, poffset));
	} else
	    global.Res1 = 0L;
    }
}


void PDeviceInfo(infodata)
struct InfoData *infodata;
{
	extern int timing;

	infodata->id_NumSoftErrors	= 0L;
	infodata->id_UnitNumber 	= DISK_UNIT;
	if (superblock->fs_ronly)
		infodata->id_DiskState	= ID_WRITE_PROTECTED;
	else
		infodata->id_DiskState	= ID_VALIDATED;

	infodata->id_NumBlocks	   = superblock->fs_dsize;
	infodata->id_NumBlocksUsed = (superblock->fs_dsize -
				      superblock->fs_cstotal.cs_nbfree *
				      superblock->fs_frag + (minfree ?
				      (superblock->fs_minfree *
					superblock->fs_dsize / 100) : 0) );
	infodata->id_BytesPerBlock = (long) superblock->fs_fsize;
	infodata->id_DiskType	   = ID_FFS_DISK;
	infodata->id_VolumeNode    = CTOB(VolNode);  /* BPTR */
	infodata->id_InUse	   = timing;
}


void PDeleteObject(Block, Bname)
ULONG Block;
ULONG Bname;
{
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

	lock	= (struct BFFSLock *) BTOC(Block);
	name	= ((char *) BTOC(Bname)) + 1;

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
		    case 1:	/* error - dir routine found non-empty directory */
			global.Res2 = ERROR_DIRECTORY_NOT_EMPTY;
			break;
		    case 2:	/* Don't let 'em delete the root directory */
			global.Res2 = ERROR_OBJECT_WRONG_TYPE;
			break;
		}
		return;
	}

	if (dir_offset_delete(pinum, ioffset, 1) == 0) {
		PRINT(("dir delete returned error code\n"));
		global.Res1 = DOSFALSE;
		return;
	}

	inode = inode_modify(inum);

	PRINT(("delete %s, i=%d offset=%d nlink=%d\n",
		name, inum, ioffset, inode->ic_nlink));

	inode->ic_nlink--;
	if ((inode->ic_mode & IFMT) == IFDIR) { /* it's a dir */
		inode->ic_nlink--;			/* for "."  */
		pinode = inode_modify(pinum);		/* for ".." */
		pinode->ic_nlink--;
	}
	inode = inode_read(inum);
	if (inode->ic_nlink < 1) {
		if (((inode->ic_mode & IFMT) != IFCHR) &&
		    ((inode->ic_mode & IFMT) != IFBLK))
			file_deallocate(inum);
		inum_free(inum);
	}
}


void PMoreCache(amount)
int amount;
{
	PRINT(("morecache: cache=%d amount=%d ", cache_size, amount));
	cache_size += (amount * DEV_BSIZE / FSIZE);

	if (cache_size < 3)
		cache_size = 3;

	global.Res1 = cache_size * FSIZE / DEV_BSIZE;

	PRINT(("-> cache=%d\n", cache_size));

	cache_adjust();
}


/* CreateDir()
 *	Resolve name
 *	Check for existence of dir/file with same name
 *	Allocate new inode
 *	Add dir entry
 *		If unsuccessful, deallocate new inode, return error
 *		If successful, set DIR attributes
 */
void PCreateDir(Block, Bname)
ULONG Block;
ULONG Bname;
{
	char	*name;
	char	*sname;
	ULONG	pinum;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	int	pos;
	int	inum;
	int	newinum;
	ULONG	poffset;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	lock = (struct BFFSLock *) BTOC(Block);
	name = ((char *) BTOC(Bname)) + 1;

	if (!(pinum = parent_parse(lock, name, &sname))) {
		PRINT(("** CreateDir: error, lock is null and can't find colon\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DIR_NOT_FOUND;
		return;
	}

	PRINT(("CreateDir parent=%d, name=%s sname=%s\n", pinum, name, sname));
	if (dir_name_is_illegal(sname)) {
		PRINT(("given bad filename %s", sname));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		return;
	}

	if (dir_name_search(pinum, sname)) {
		PRINT(("file %s already exists!\n", name));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_EXISTS;
		return;
	}

	if ((newinum = inum_new(pinum, I_DIR)) == 0) {
		PRINT(("couldn't find free inode\n"));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_NO_MORE_ENTRIES;
		return;
	}

	if (poffset = dir_create(pinum, sname, newinum))
		inum_sane(newinum, I_DIR);
		dir_create(newinum, ".", newinum);
		if (dir_create(newinum, "..", pinum)) {
			inode = inode_modify(pinum);
			inode->ic_nlink++;
			global.Res1 = CTOB(CreateLock(newinum, ACCESS_WRITE,
						      pinum, poffset));
			global.Res2 = 0;
			return;
		} else
			file_deallocate(newinum);

	inum_free(newinum);
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_INVALID_COMPONENT_NAME;
}


/* RenameObject()
 *	This routine handles the AmigaDOS file rename request.
 *	It takes a lock on the old parent, the old file name,
 *	a lock on the new parent, and the new file name.
 *	The routine will handle directory renames and other
 *	special cases.
 */
void PRenameObject(Boldparent, Boldname, Bnewparent, Bnewname)
ULONG Boldparent;
ULONG Boldname;
ULONG Bnewparent;
ULONG Bnewname;
{
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

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	UPSTAT(renames);

	oldname = ((char *) BTOC(Boldname)) + 1;
	newname = ((char *) BTOC(Bnewname)) + 1;
	szo = *(oldname - 1);
	szn = *(newname - 1);
	cho = *(oldname + szo);
	chn = *(newname + szn);

	*(oldname + szo) = '\0';
	*(newname + szn) = '\0';
	oldparent = (struct BFFSLock *) BTOC(Boldparent);
	newparent = (struct BFFSLock *) BTOC(Bnewparent);

	oldpinum = parent_parse(oldparent, oldname, &oldsubname);
	newpinum = parent_parse(newparent, newname, &newsubname);

	if (dir_name_is_illegal(oldsubname)) {
		PRINT(("oldname %s is illegal\n", oldsubname));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		return;
	}

	if (dir_name_is_illegal(newsubname)) {
		PRINT(("newname %s is illegal\n", newsubname));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		return;
	}

	if (oldpinum && newpinum) {
		PRINT(("Rename from (%s) p=%d %s to (%s) p=%d %s\n",
		    oldname, oldpinum, oldsubname,
		    newname, newpinum, newsubname));
		if ((inum = dir_name_search(oldpinum, oldsubname)) == 0) {
		    PRINT(("source not found\n"));
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_OBJECT_NOT_FOUND;
		} else {
		    inode = inode_read(inum);
		    isdir = ((inode->ic_mode & IFMT) == IFDIR) &&
			    (oldpinum != newpinum);
		    if (dir_rename(oldpinum, oldsubname, newpinum,
				   newsubname, isdir)) {
			if (isdir) {
			    dir_inum_change(inum, oldpinum, newpinum);
			    inode = inode_modify(oldpinum);
			    inode->ic_nlink--;
			    inode = inode_modify(newpinum);
			    inode->ic_nlink++;
			}
		    } else {
			global.Res1 = DOSFALSE;
			global.Res2 = ERROR_OBJECT_IN_USE;
		    }
		}
	} else {
		PRINT(("bad %s path\n", oldpinum ? "dest" : "source"));
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DIR_NOT_FOUND;
	}

	*(oldname + szo) = cho;
	*(newname + szn) = chn;
}


/* RenameDisk()
 *	This routine modifies the superblock.  It stores the current name
 *	imposed by AmigaDOS in the last characters of the "last" mounted
 *	on field in the superblock.
 */
void PRenameDisk(Bnewname)
ULONG Bnewname;
{
	int	newlen;
	char	*temp;
	char	*newname;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res1 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	newname = ((char *) BTOC(Bnewname)) + 1;
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
}


/* SetProtect()
 *	This packet sets permissions on the specified file,
 *	given a lock and a path relative to that lock.
 */
void PSetProtect(unknown, Block, Bname, mask)
ULONG	Bname;
ULONG	Block;
ULONG	mask;
{
	ULONG	inum;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;

	name = ((char *) BTOC(Bname)) + 1;
	lock = (struct BFFSLock *) BTOC(Block);

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		inode->ic_mode &= ~ (ICHG | ISUID | ISVTX | 0777);

		inode->ic_mode |= (mask & FIBF_ARCHIVE) ? 0 : (IEXEC >> 3);
		inode->ic_mode |= (mask & FIBF_SCRIPT)  ? ISUID : 0;
		inode->ic_mode |= (mask & FIBF_PURE)    ? ISVTX : 0;
		inode->ic_mode |= (mask & FIBF_READ)    ? 0 : IREAD;
		inode->ic_mode |= (mask & FIBF_WRITE)   ? 0 : IWRITE;
		inode->ic_mode |= (mask & FIBF_DELETE)  ? 0 : IWRITE;
		inode->ic_mode |= (mask & FIBF_EXECUTE) ? 0 : IEXEC;
		inode->ic_mode |= (mask & FIBF_GRP_READ)    ? 0 : (IREAD  >> 3);
		inode->ic_mode |= (mask & FIBF_GRP_WRITE)   ? 0 : (IWRITE >> 3);
		inode->ic_mode |= (mask & FIBF_GRP_DELETE)  ? 0 : (IWRITE >> 3);
		inode->ic_mode |= (mask & FIBF_GRP_EXECUTE) ? 0 : (IEXEC  >> 3);
		inode->ic_mode |= (mask & FIBF_OTR_READ)    ? 0 : (IREAD  >> 6);
		inode->ic_mode |= (mask & FIBF_OTR_WRITE)   ? 0 : (IWRITE >> 6);
		inode->ic_mode |= (mask & FIBF_OTR_DELETE)  ? 0 : (IWRITE >> 6);
		inode->ic_mode |= (mask & FIBF_OTR_EXECUTE) ? 0 : (IEXEC  >> 6);
	} else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
}

/* SetOwner()
 *	This packet sets the owner and group of the specified
 *	file, given the name of the file.
 */
void PSetOwner(unknown, Block, Bname, ownership)
ULONG	unknown;
ULONG	Block;
ULONG	Bname;
ULONG	ownership;
{
	ULONG	inum;
	struct	icommon  *inode;
	struct	BFFSLock *lock;
	char	*name;

	name = ((char *) BTOC(Bname)) + 1;
	lock = (struct BFFSLock *) BTOC(Block);

	inum = path_parse(lock, name);

	if (inum && (inode = inode_modify(inum))) {
		inode->ic_uid = ownership >> 16;
		inode->ic_gid = ownership & 65535;
	} else {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
	}
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
void PCopyDir(lock)
struct BFFSLock *lock;
{
	global.Res1 = CTOB(CreateLock(lock->fl_Key, lock->fl_Access,
			   lock->fl_Pinum, lock->fl_Poffset));
}


/* Inhibit()
 *	This packet tells the filesystem to keep its hands off the
 *	media until another inhibit packet is sent with flag=0
 */
void PInhibit(flag)
int flag;
{
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
void PFormat(Bvolume, Bname, type)
ULONG Bvolume;
ULONG Bname;
LONG type;
{
	char	*volume;
	char	*name;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	volume	= ((char *) BTOC(Bvolume)) + 1;
	name	= ((char *) BTOC(Bname)) + 1;

	PRINT(("format: Vol=%s Name=%s Type=%d\n", volume, name, type));

	/* let DOS think we formatted the disk */
}


/* WriteProtect()
 *	This packet is sent by the dos C:Lock command to protect/
 *	unprotect the media.
 */
void PWriteProtect(flag)
int flag;
{
	extern int physical_ro;

	if (flag)
		superblock->fs_ronly = 1;
	else if (!physical_ro)
		superblock->fs_ronly = 0;
	superblock->fs_fmod++;
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
	RemoveVolNode();
	receiving_packets = 0;
}


/* Flush()
 *	Manually write the contents of the cache out to disk.
 */
void PFlush()
{
	if (!superblock)
		return;
	UPSTAT(flushes);
	superblock_flush();
	cache_cg_flush();
	cache_flush();
}


void PSameLock(Block1, Block2)
ULONG Block1;
ULONG Block2;
{
	ULONG maxi;
	struct BFFSLock *lock1;
	struct BFFSLock *lock2;

	lock1 = (struct BFFSLock *) BTOC(Block1);
	lock2 = (struct BFFSLock *) BTOC(Block2);

	PRINT(("same_lock: comparing %d and %d\n", lock1->fl_Key, lock2->fl_Key));

	/* Something (ie: WorkBench) is hosed.  This should not be hardcoded! */
global.Res1 = LOCK_SAME;
return;

	maxi = superblock->fs_ipg * superblock->fs_ncg;

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
	extern int	dir_comments;
	extern int	dir_comments2;
	extern int	GMT;
	extern int	og_perm_invert;

	stat->superblock	= (ULONG) superblock;
	stat->cache_head	= (ULONG) &cache_stack_tail;
	stat->cache_hash	= (ULONG) hashtable;
	stat->cache_size	= &cache_size;
	stat->cache_cg_size	= &cache_cg_size;
	stat->cache_item_dirty	= &cache_item_dirty;
	stat->cache_alloced	= &cache_alloced;
	stat->disk_poffset	= &poffset;
	stat->disk_pmax		= &pmax;
	stat->unixflag		= &unixflag;
	stat->resolve_symlinks	= &resolve_symlinks;
	stat->case_independent	= &case_independent;
	stat->dir_comments	= &dir_comments;
	stat->dir_comments2	= &dir_comments2;
	stat->cache_used	= &cache_used;
	stat->timer_secs	= &timer_secs;
	stat->timer_loops	= &timer_loops;
	stat->GMT		= &GMT;
	stat->minfree		= &minfree;
	stat->og_perm_invert	= &og_perm_invert;

	global.Res2		= (ULONG) stat;
}

void PCreateObject(Block, Bname, st_type, device)
ULONG Block;
ULONG Bname;
ULONG st_type;
{
	ULONG	pinum;
	ULONG	inum;
	struct	BFFSLock *lock;
	struct	icommon *inode;
	char	*name;
	char	*subname;

	switch (st_type) {
		case ST_USERDIR:
			return(PCreateDir(Block, Bname));
		case ST_LINKFILE:
			return(PMakeLink(Block, Bname, device, 0));
		case ST_SOFTLINK:
			return(PMakeLink(Block, Bname, device, 1));
	}

	if (superblock->fs_ronly) {
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		goto doserror;
	}

	name  = ((char *) BTOC(Bname)) + 1;
	lock  = (struct BFFSLock *) BTOC(Block);
	pinum = parent_parse(lock, name, &subname);

	PRINT(("CreateObject: %x(%s) type=%c d=%d,%d p=%d %s\n",
		lock, name, ((st_type == ST_BDEVICE) ? 'b' : 'c'),
		device >> 8, device & 255, pinum, subname));

	if ((pinum == 0) || dir_name_is_illegal(subname)) {
		global.Res2 = ERROR_INVALID_COMPONENT_NAME;
		goto doserror;
	}

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

	if (dir_create(pinum, subname, inum)) {
	    inum_sane(inum, I_FILE);
	    inode = inode_modify(inum);

	    switch(st_type) {
		case ST_FILE:
			return;
		case ST_CDEVICE:
			inode->ic_mode &= ~IFREG;
			inode->ic_mode |= IFCHR;
			inode->ic_db[0] = device;
			return;
		case ST_BDEVICE:
			inode->ic_mode &= ~IFREG;
			inode->ic_mode |= IFBLK;
			inode->ic_db[0] = device;
			return;
		default:
			global.Res2 = ERROR_BAD_NUMBER;
			PRINT(("bad file type %d for '%s'\n", st_type, name));
	    }
	    return;
	} else {
	    PRINT(("Create failed for object '%s' type=%d\n", name, st_type));
	    global.Res2 = ERROR_DISK_FULL;
	}
	inum_free(inum);

	doserror:
	global.Res1 = DOSFALSE;
}

void PMakeLink(Block, Bname, Block_from, link_type)
ULONG Block;
ULONG Bname;
ULONG Block_from;
ULONG link_type;
{
	ULONG	pinum;
	ULONG	inum;
	ULONG	ioffset;
	char	*name;
	char	*subname;
	struct	BFFSLock *lock;
	struct	BFFSLock *lock_from;
	struct	icommon *inode;
	int	namelen;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	PRINT(("MakeLink: %s\n", link_type ? "SOFT" : "HARD"));

	name      = ((char *) BTOC(Bname)) + 1;
	lock      = (struct BFFSLock *) BTOC(Block);
	lock_from = (struct BFFSLock *) BTOC(Block_from);
	pinum     = parent_parse(lock, name, &subname);

	PRINT(("lock=%x\n", lock));
	PRINT(("name=%s\n", name));

	if (link_type)	/* soft */
	    PRINT(("from=%s\n", Block_from));
	else		/* hard */
	    PRINT(("from=%x\n", lock_from));

	PRINT(("pinum=%d\n", pinum));

	if ((pinum == 0) || dir_name_is_illegal(subname)) {
	    global.Res1 = DOSFALSE;
	    global.Res2 = ERROR_INVALID_COMPONENT_NAME;
	    return;
	}

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
	    if ((inode->ic_mode & IFMT) == IFDIR) {
		global.Res2 = ERROR_OBJECT_WRONG_TYPE;
		goto doserror;
	    }
	    if (!dir_create(pinum, subname, inum)) {
		global.Res2 = ERROR_DISK_FULL;
		goto doserror;
	    }
	    inode = inode_modify(inum);
	    inode->ic_nlink++;
	    return;
	}

	inum = inum_new(pinum, I_FILE);
	if (inum == 0) {
	    PRINT(("failed to get a new inode from inum_new()\n"));
	    global.Res2 = ERROR_NO_MORE_ENTRIES;
	    goto doserror;
	}

	if (ioffset = dir_create(pinum, subname, inum))
	    inum_sane(inum, I_FILE);
	else {
	    PRINT(("Create failed for %s\n", name));
	    inum_free(inum);
	    global.Res2 = ERROR_DISK_FULL;
 	    goto doserror;
	}

	namelen = strlen(Block_from);

	if (file_write(inum, 0, namelen + 1, Block_from) == namelen + 1) {
	    file_block_retract(inum);
	    inode = inode_modify(inum);
	    inode->ic_mode &= ~IFREG;
	    inode->ic_mode |=  IFLNK;
	    return;
	}

	dir_offset_delete(pinum, ioffset, 0);
	file_deallocate(inum);
	inum_free(inum);

	doserror:
	global.Res1 = DOSFALSE;
}

void PReadLink(Block, name, pathbuf, sizebuf)
ULONG Block;
char  *name;
char  *pathbuf;
ULONG sizebuf;
{
	ULONG	inum;
	struct	BFFSLock *lock;

	lock = (struct BFFSLock *) BTOC(Block);
	inum = path_parse(lock, name);

	PRINT(("ReadLink "));
	PRINT(("lock=%d ", lock));
	PRINT(("name=%s ", name));
	PRINT(("pathbuf=%s ", pathbuf));
	PRINT(("sizebuf=%d ", sizebuf));
	PRINT(("i=%d\n", inum));

	if (inum == 0) {
		global.Res1 = -1;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	global.Res1 = 0;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
}

void PNil()
{
}


void PSetFileSize(a, b, c, d)
ULONG a;
ULONG b;
ULONG c;
ULONG d;
{
	PRINT(("SetFileSize\n"));
	PRINT(("a=%d %s\n", a, ((char *) BTOC(a)) + 1));
	PRINT(("b=%d %s\n", b, ((char *) BTOC(b)) + 1));
	PRINT(("c=%d %s\n", c, ((char *) BTOC(c)) + 1));
	PRINT(("d=%d %s\n", d, ((char *) BTOC(d)) + 1));

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
}


void PSetDate(unused, Block, Bname, datestamp)
ULONG unused;
ULONG Block;
ULONG Bname;
struct DateStamp *datestamp;
{
	ULONG	inum;
	char	*name;
	struct	BFFSLock *lock;
	struct	icommon *inode;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	name = ((char *) BTOC(Bname)) + 1;
	lock = (struct BFFSLock *) BTOC(Block);
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
		inode->ic_mtime = (datestamp->ds_Days + 2922) * 86400 +
				  (datestamp->ds_Minute * 60) - GMT * 3600 +
				  datestamp->ds_Tick / TICKS_PER_SECOND;
	}

	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
}

void PSetDates(what, Block, Bname, datestamp)
ULONG what;
ULONG Block;
ULONG Bname;
struct DateStamp *datestamp;
{
	ULONG	inum;
	ULONG	ttime;
	char	*name;
	struct	BFFSLock *lock;
	struct	icommon *inode;

	if (superblock->fs_ronly) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DISK_WRITE_PROTECTED;
		return;
	}

	name = ((char *) BTOC(Bname)) + 1;
	lock = (struct BFFSLock *) BTOC(Block);
	inum = path_parse(lock, name);

	if (inum == 0) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_OBJECT_NOT_FOUND;
		return;
	}

	PRINT(("SetDates: %d:%s %d  OPER=%d\n", lock, name, inum, what));
	PRINT(("day=%d min=%d tick=%d\n", datestamp->ds_Days,
		datestamp->ds_Minute, datestamp->ds_Tick));


	if (inum) {
	    switch (what) {
		case 0: /*    set modify date stamp */
		    inode = inode_modify(inum);
		    inode->ic_mtime = (datestamp->ds_Days + 2922) * 86400 +
				      (datestamp->ds_Minute * 60) - GMT * 3600 +
				      datestamp->ds_Tick / TICKS_PER_SECOND;
		    break;
		case 1: /* return modify date stamp */
		    ttime = inode->ic_mtime + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		case 2: /*    set create date stamp */
		    inode = inode_modify(inum);
		    inode->ic_ctime = (datestamp->ds_Days + 2922) * 86400 +
				      (datestamp->ds_Minute * 60) - GMT * 3600 +
				      datestamp->ds_Tick / TICKS_PER_SECOND;
		    break;
		case 3: /* return create date stamp */
		    ttime = inode->ic_ctime + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		case 4: /*    set access date stamp */
		    inode = inode_modify(inum);
		    inode->ic_atime = (datestamp->ds_Days + 2922) * 86400 +
				      (datestamp->ds_Minute * 60) - GMT * 3600 +
				      datestamp->ds_Tick / TICKS_PER_SECOND;
		    break;
		case 5: /* return access date stamp */
		    ttime = inode->ic_atime + GMT * 3600;
		    datestamp->ds_Days = ttime / 86400 - 2922;
		    datestamp->ds_Minute = (ttime % 86400) / 60;
		    datestamp->ds_Tick = (ttime % 60) * TICKS_PER_SECOND;
		    break;
		default:
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_NOT_IMPLEMENTED;
	    }
	}
}

void PExamineFh(Bfh, Bfib)
ULONG Bfh;
ULONG Bfib;
{
	struct	BFFSfh *fileh;
	struct	BFFSfh *Bfh;

	fileh = (struct BFFSfh *) BTOC(Bfh);
	PRINT(("ExamineFh\n"));
	PExamineObject(CTOB((ULONG) fileh->lock), Bfib);
}

/*

void PDiskType() {
	PRINT(("DiskType\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PFhFromLock() {
	PRINT(("FhFromLock\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PCopyDirFh() {
	PRINT(("CopyDirFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PParentFh() {
	PRINT(("ParentFh\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PExamineAll() {
	PRINT(("ExamineAll\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PAddNotify() {
	PRINT(("AddNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}

void PRemoveNotify() {
	PRINT(("RemoveNotify\n"));
	global.Res1 = DOSFALSE;
	global.Res2 = ERROR_NOT_IMPLEMENTED;
}
*/
