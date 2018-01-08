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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/exec_protos.h>

#include "config.h"
#include "handler.h"
#include "superblock.h"
#include "ufs.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "fsmacros.h"
#include "packets.h"
#include "stat.h"
#include "bffs_dosextens.h"
#include "system.h"
#include "file.h"
#include "inode.h"
#include "dir.h"
#include "cache.h"
#include "unixtime.h"

#define ACCESS_INVISIBLE 0

extern	char *handler_name;
extern	int fs_partition;
extern	int unix_paths;
extern	struct  DeviceNode *DevNode;

int link_comments = 0;		/* symlink and device comments */
int inode_comments = 0;		/* all inode info comments */
int og_perm_invert = 0;		/* invert permissions on other/group */

char	*volumename;
struct	stat *stat = NULL;

static void
fsname(struct DosInfo *info, char *volumename)
{
    struct DeviceList *tmp;
    char *fsmnt = superblock->fs_fsmnt;
    char *name;
    int   len;
    ULONG count = 0;

    if (superblock == NULL) {
	PRINT2(("** superblock is null in fsname()\n"));
	sprintf(volumename, "%cNULL", 4);
	return;
    }
    PRINT(("fs last mounted as %s\n", superblock->fs_fsmnt));

    name = fsmnt + strlen(fsmnt);
    while ((*name == '/' || *name == '\0') && (name > fsmnt))
	name--;
    while (name > fsmnt) {
	if (*name == '/')
	    break;
	else
	    name--;
    }
    if (*name == '/')
	name++;

    strcpy(volumename + 1, name);
    if (*name == '\0') {
	strcpy(volumename + 1, "BFFS0");
	volumename[strlen(volumename + 1)] = 'a' + fs_partition;
    }
    len = strlen(volumename + 1) + 1;

    /* rename if already exists */
    Forbid();
search_again:
	tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
	while (tmp != NULL)
	    if (streqv(((char *) BTOC(tmp->dl_Name)) + 1, volumename + 1)) {
		sprintf(volumename + len, "%u", count++);
		goto search_again;
	    } else {
		tmp = (struct DeviceList *) BTOC(tmp->dl_Next);
	    }
    Permit();

    volumename[0] = strlen(volumename + 1);
}

void
NewVolNode(void)
{
    struct DosInfo *info;

    if (VolNode != NULL) {
	PRINT2(("Attempted to create new volume node without releasing old\n"));
	return;
    }

    info = (struct DosInfo *) BTOC( ((struct RootNode *) DOSBase->dl_Root)->rn_Info);

    VolNode = (struct DeviceList *)
			AllocMem(sizeof(struct DeviceList), MEMF_PUBLIC);

    if (VolNode == NULL) {
	PRINT2(("NewVolNode: unable to allocate %d bytes\n",
		sizeof(struct DeviceList)));
	return;
    }

    volumename = (char *) AllocMem(MAXMNTLEN, MEMF_PUBLIC);
    if (volumename == NULL) {
	PRINT2(("NewVolNode: unable to allocate %d bytes\n", MAXMNTLEN));
	FreeMem(VolNode, sizeof(struct DeviceList));
	VolNode = NULL;
	return;
    }

    fsname(info, volumename);

    unix_time_to_amiga_ds(DISK32(superblock->fs_time), &VolNode->dl_VolumeDate);
    VolNode->dl_Type	 = DLT_VOLUME;
    VolNode->dl_Task	 = DosPort;
    VolNode->dl_Lock	 = 0;
    VolNode->dl_LockList = 0;

    /*
     * Randell Jesup said (around 1991) that if we want WB to recognize BFFS,
     * it might need to fake ID_DOS.  At least with OS 3.x, it seems content
     * with BFFS.  Not sure about OS 2.x...
     */
    VolNode->dl_DiskType = ID_BFFS_DISK;

    VolNode->dl_unused   = 0;
    VolNode->dl_Name	 = CTOB(volumename);

    Forbid();
	VolNode->dl_Next = info->di_DevInfo;
	info->di_DevInfo = (BPTR) CTOB(VolNode);
    Permit();
}

void
RemoveVolNode(void)
{
    int		notremoved	= 1;
    struct	DeviceList	*current;
    struct	DeviceList	*parent;
    struct	DosInfo		*info;

    if (VolNode == NULL) {
	PRINT(("VolNode already removed\n"));
	return;
    }

    info = (struct DosInfo *)
		BTOC( ((struct RootNode *) DOSBase->dl_Root)->rn_Info );
    parent  = NULL;

    Forbid();
	current = (struct DeviceList *) BTOC(info->di_DevInfo);
	while (current != NULL) {
	    if (current == VolNode) {
		current->dl_Task = NULL;
		if (parent == NULL)
		    info->di_DevInfo = current->dl_Next;
		else
		    parent->dl_Next  = current->dl_Next;
		notremoved = 0;
	    } else
		parent = current;
	    current = (struct DeviceList *) BTOC(current->dl_Next);
	}
    Permit();

    if (notremoved)
	PRINT2(("Unable to find our VolNode to remove.\n"));

    FreeMem(BTOC(VolNode->dl_Name), MAXMNTLEN);
    FreeMem(VolNode, sizeof(struct DeviceList));

    VolNode = NULL;
}

void
close_files(void)
{
#ifndef RONLY
    struct BFFSLock *lock = NULL;

    if (VolNode == 0) {
	PRINT2(("volume was not opened\n"));
	return;
    }

    PRINT(("close_files\n"));
    lock = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
    while (lock != NULL) {
	if (lock->fl_Fileh && (lock->fl_Fileh->access_mode == MODE_WRITE)) {
	    PRINT(("write closing i=%d\n", lock->fl_Key));
	    if (lock->fl_Fileh->access_mode == MODE_WRITE)
		truncate_file(lock->fl_Fileh);
	}
	lock = (struct BFFSLock *) BTOC(lock->fl_Link);
    }
#endif
}


#ifndef RONLY
void
truncate_file(struct BFFSfh *fileh)
{
    struct icommon *inode;

    if (fileh->access_mode == MODE_WRITE) {
	inode = inode_modify(fileh->real_inum);
	if (fileh->truncate_mode == MODE_TRUNCATE) {
	    if (IC_SIZE(inode) > fileh->maximum_position) {
		IC_SETSIZE(inode, fileh->maximum_position);
		file_blocks_deallocate(fileh->real_inum);
	    }
	    fileh->truncate_mode = MODE_UPDATE;
	}
	inode->ic_mtime = DISK32(unix_time());
	file_block_retract(fileh->real_inum);
	fileh->access_mode = MODE_READ;
    }
}
#endif

struct BFFSLock *
CreateLock(ULONG key, int mode, ULONG pinum, ULONG poffset)
{
    struct BFFSLock *lock = NULL;
    int access = 0;

    if (VolNode == NULL) {
	global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
	PRINT1(("device is not mounted\n"));
	return(NULL);
    }

    lock = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
    while (lock != NULL)
	if (lock->fl_Key == key) {
	    access = lock->fl_Access;
	    break;
	} else
	    lock = (struct BFFSLock *) BTOC(lock->fl_Link);

    if ((lock != NULL) && (access == 0))
	PRINT(("access on invisible lock\n"));
    switch (mode) {
	case EXCLUSIVE_LOCK:
		if (access) {  /* somebody else has a lock on it... */
		    global.Res2 = ERROR_OBJECT_IN_USE;
		    PRINT(("exclusive: lock is already exclusive\n"));
		    return(NULL);
		}
		break;
	case ACCESS_INVISIBLE:
		if (access)
		    PRINT(("invisible: access on locked file\n"));
		break;
	default:  /* C= told us if it's not EXCLUSIVE, it's SHARED */
		if (access == EXCLUSIVE_LOCK) {
		    global.Res2 = ERROR_OBJECT_IN_USE;
		    PRINT(("exclusive: lock is already shared\n"));
		    return(NULL);
		}
		break;
    }

    lock = (struct BFFSLock *) AllocMem(sizeof(struct BFFSLock), MEMF_PUBLIC);
    if (lock == NULL) {
	global.Res2 = ERROR_NO_FREE_STORE;
	return(NULL);
    }

    lock->fl_Key	= key;
    lock->fl_Access	= mode;
    lock->fl_Task	= DosPort;
    lock->fl_Volume	= CTOB(VolNode);
    lock->fl_Pinum	= pinum;
    lock->fl_Poffset	= poffset;
    lock->fl_Fileh	= NULL;

/*
    PRINT(("CreateLock: inum=%d pinum=%d poffset=%d type=%s\n", key, pinum,
	   poffset, ((mode == EXCLUSIVE_LOCK) ? "exclusive" : "shared")));
*/

    Forbid();
	lock->fl_Link = VolNode->dl_LockList;
	VolNode->dl_LockList = CTOB(lock);
    Permit();

    return(lock);
}

/* XXX: strnicmp() */
static int
strneqv(const char *str1, const char *str2, int length)
{
	while (length) {
		if ((*str1 != *str2) && ((*str1 | 32) != (*str2 | 32)))
			return(0);
		if (*str1 == 0)
			return(1);
		str1++;
		str2++;
		length--;
	}

	return(1);
}

int
ResolveColon(char *name)
{
    struct	DosInfo *info;
    struct	DeviceList *tmp;
    struct	BFFSLock *fl;
    char	*pos;

    pos = strchr(name, ':');

    if (pos == NULL)
	return(0);

    if (pos == name)
	return(2);

    *pos = '\0';

    if ((streqv(volumename + 1, name)) || (streqv(handler_name, name))) {
	*pos = ':';
	return(2);
    }

    info= (struct DosInfo *) BTOC(((struct RootNode *) DOSBase->dl_Root)->rn_Info);

    tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
    while (tmp != NULL) {
	if (tmp->dl_Type == 1) {
	    PRINT(("%.*s ", ((char *) BTOC(tmp->dl_Name))[0],
		   ((char *) BTOC(tmp->dl_Name)) + 1));
	    if (strneqv(((char *) BTOC(tmp->dl_Name)) + 1, name,
			((char *) BTOC(tmp->dl_Name))[0])) {
		PRINT(("<- "));
		*pos = ':';
		fl = (struct BFFSLock *) BTOC(tmp->dl_Lock);
		global.Pinum	= fl->fl_Pinum;
		global.Poffset	= fl->fl_Poffset;
		return(fl->fl_Key);
	    }
	}
	tmp = (struct DeviceList *) BTOC(tmp->dl_Next);
    }

    PRINT(("\n"));
    *pos = ':';
    return(2);
}

static void
strlower(char *str)
{
	while (*str != '\0') {
		if ((*str >= 'A') && (*str <= 'Z'))
			*str += ('a' - 'A');
		str++;
	}
}

void
NameHandler(const char *name)
{
	const char *pos;
	if (handler_name != NULL)
		return;

	handler_top:
	handler_name = (char *) AllocMem(name[0], MEMF_PUBLIC);
	if (handler_name == NULL) {
		PRINT2(("NameHandler: unable to allocate %d bytes\n", name[0]));
		goto handler_top;
	}
	strncpy(handler_name, name + 1, name[0] - 1);
	handler_name[name[0] - 1] = '\0';
	if (pos = strchr(handler_name, ':'))
		*pos = '\0';
	strlower(handler_name);
	PRINT(("handler=\"%s\"\n", handler_name));
}

void
UnNameHandler(void)
{
	if (handler_name) {
		FreeMem(handler_name, strlen(handler_name) + 1);
		handler_name = NULL;
	}
}

void
InitStats(void)
{
	int	current;
	int	size;
	ULONG	*ptr;

	if (stat == NULL) {
		stat = (struct stat *) AllocMem(sizeof(struct stat), MEMF_PUBLIC);
		if (stat == NULL) {
			PRINT2(("** not enough memory for stats\n"));
			return;
		}
	}

	size = sizeof(struct stat) / sizeof(ULONG);
	current = size;
	ptr = (ULONG *) stat;

	for (; current; current--, ptr++)
		*ptr = 0;

	stat->size = size;
	stat->magic = ('B' << 24) | ('F' << 16) | ('F' << 8) | '\0';

	return;
}

void
FillInfoBlock(FileInfoBlock_3_t *fib, struct BFFSLock *lock,
              struct icommon *finode, struct direct *dir_ent)
{
    int fmode;
    int temp;

    fmode = DISK16(finode->ic_mode);
    fib->fib_Comment[0] = 0;				/* zero length */
    fib->fib_Comment[1] = '\0';				/* NULL terminated */
    fib->fib_Size	= (long) 0;
    fib->fib_NumBlocks	= (long) 0;

    /* give file information in the comment field */
    if (inode_comments) {
	if (bsd44fs)
	    sprintf(fib->fib_Comment + 1,
		    "i=%-4d p=%04o u=%-5d g=%-5d l=%-2d bl=%-4d",
		    dir_ent ? DISK32(dir_ent->d_ino) : 0, fmode & 07777,
		    DISK32(finode->ic_nuid), DISK32(finode->ic_ngid),
		    DISK16(finode->ic_nlink), DISK32(finode->ic_blocks));
	else
	    sprintf(fib->fib_Comment + 1,
		    "i=%-4d p=%04o u=%-5d g=%-5d l=%-2d bl=%-4d",
		    dir_ent ? DISK32(dir_ent->d_ino) : 0, fmode & 07777,
		    DISK16(finode->ic_ouid), DISK16(finode->ic_ogid),
		    DISK16(finode->ic_nlink), DISK32(finode->ic_blocks));
	fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
    }

    /* Note that most programs don't show directory size */
    fib->fib_Size	= IC_SIZE(finode);	/* bytes */
    fib->fib_NumBlocks  = DISK32(finode->ic_blocks) / NDSPF;

    if (DISK32(dir_ent->d_ino) == ROOTINO) {
	fib->fib_DirEntryType = ST_ROOT;
    } else if ((fmode & IFMT) == IFREG) {
	fib->fib_DirEntryType = ST_FILE;
    } else if ((fmode & IFMT) == IFDIR) {
	fib->fib_DirEntryType = ST_USERDIR;
    } else if ((fmode & IFMT) == IFLNK) {
	fib->fib_DirEntryType = ST_SOFTLINK;
	if (link_comments) {
	    int	temp2;
	    if ((temp = strlen(fib->fib_Comment + 1)) != 0)
		fib->fib_Comment[temp++ + 1] = ' ';
	    strcpy(fib->fib_Comment + temp + 1, "-> ");
	    if (finode->ic_blocks == 0) {	/* must be 4.4 fastlink */
	        temp2 = strlen((char *) finode->ic_db);
		strcpy(fib->fib_Comment + temp + 4, (char *) finode->ic_db);
	    } else {
	        temp2 = file_read(DISK32(dir_ent->d_ino), 0, 76 - temp,
			          fib->fib_Comment + temp + 4);
	    }
	    fib->fib_Comment[0] = temp + 3 + temp2;
	}
    } else if (fmode & IFCHR) {
	if ((fmode & IFMT) == IFBLK)
		fib->fib_DirEntryType = ST_BDEVICE;
	else
		fib->fib_DirEntryType = ST_CDEVICE;

	if (link_comments) {
	    if (temp = strlen(fib->fib_Comment + 1))
		fib->fib_Comment[temp++ + 1] = ' ';
	    sprintf(fib->fib_Comment + temp + 1, "-] %cdev(%d,%d)",
		    ((fib->fib_DirEntryType == ST_BDEVICE) ? 'B' : 'C'),
		    DISK32(finode->ic_db[0]) >> 8,
		    DISK32(finode->ic_db[0]) & 255);
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if ((fmode & IFMT) == IFIFO) {
	fib->fib_DirEntryType = ST_FIFO;
	if (temp = strlen(fib->fib_Comment + 1))
	    fib->fib_Comment[temp++ + 1] = ' ';
	if (link_comments) {
	    sprintf(fib->fib_Comment + temp + 1, "fifo");
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if ((fmode & IFMT) == IFSOCK) {
	fib->fib_DirEntryType = ST_SOCKET;
	if (temp = strlen(fib->fib_Comment + 1))
	    fib->fib_Comment[temp++ + 1] = ' ';
	if (link_comments) {
	    sprintf(fib->fib_Comment + temp + 1, "socket");
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if ((fmode & IFMT) == IFWHT) {
	fib->fib_DirEntryType = ST_WHITEOUT;
	if (temp = strlen(fib->fib_Comment + 1))
	    fib->fib_Comment[temp++ + 1] = ' ';
	if (link_comments) {
	    sprintf(fib->fib_Comment + temp + 1, "whiteout");
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else {
	PRINT(("Unknown file type: %s %x\n", dir_ent->d_name, fmode));
	fib->fib_DirEntryType = ST_FILE;
    }

    fib->fib_Protection = 0;
    /*
     * Code here must stay in sync with code in PSetProtect().
     *
     * Not enough UNIX bits to support FIBF_ARCHIVE
     * Could overlap group exec...
     *     fib->fib_Protection |= ((fmode & (IEXEC >> 3))? 0 : FIBF_ARCHIVE);
     */
    fib->fib_Protection |= ((fmode & ISUID)  ? FIBF_SCRIPT     : 0);
    fib->fib_Protection |= ((fmode & ISGID)  ? FIBF_HIDDEN     : 0);
    fib->fib_Protection |= ((fmode & ISVTX)  ? FIBF_PURE       : 0);

    /* Owner bits first */
    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_READ);
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_WRITE | FIBF_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_EXECUTE);

    fmode <<= 3;	/* Group bits */
    if (og_perm_invert)
	fmode ^= (IREAD | IWRITE | IEXEC | ((IREAD | IWRITE | IEXEC) >> 3));

    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_GRP_READ);
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_GRP_WRITE | FIBF_GRP_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_GRP_EXECUTE);

    fmode <<= 3;	/* Other bits */
    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_OTR_READ);
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_OTR_WRITE | FIBF_OTR_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_OTR_EXECUTE);

    if (bsd44fs) {
	fib->fib_OwnerUID  = (UWORD) DISK32(finode->ic_nuid);
	fib->fib_OwnerGID  = (UWORD) DISK32(finode->ic_ngid);
    } else {
	fib->fib_OwnerUID  = (UWORD) DISK16(finode->ic_ouid);
	fib->fib_OwnerGID  = (UWORD) DISK16(finode->ic_ogid);
    }

    /* fib->fib_DirOffset = lock->fl_Poffset is assigned by caller */

    fib->fib_EntryType = fib->fib_DirEntryType;		/* must be the same */
    if (finode->ic_mtime != 0)
	unix_time_to_amiga_ds(DISK32(finode->ic_mtime), &fib->fib_Date);
    else
	unix_time_to_amiga_ds(DISK32(superblock->fs_time), &fib->fib_Date);

#define DIRNAMESIZE (sizeof (fib->fib_FileName))
    if ((lock->fl_Key == ROOTINO) &&
	((pack->dp_Type == ACTION_EXAMINE_OBJECT) ||
	 (pack->dp_Type == ACTION_EX_OBJECT))) {
	strcpy(fib->fib_FileName + 1, (char *) (BTOC(VolNode->dl_Name)) + 1);
    } else {
	strncpy(fib->fib_FileName + 1, dir_ent->d_name, DIRNAMESIZE - 1);
	fib->fib_FileName[DIRNAMESIZE - 1] = '\0';
	fib->fib_DiskKey = DISK32(dir_ent->d_ino);
    }
    fib->fib_FileName[0] = (char) strlen(fib->fib_FileName + 1);
/*
    PRINT(("  i=%-4d fs=%-8d fb=%-3d name=%s\n",
	   dir_ent ? DISK32(dir_ent->d_ino) : 0, fib->fib_Size,
	   fib->fib_NumBlocks, fib->fib_FileName+1));
*/
    if ((pack->dp_Type == ACTION_EX_OBJECT) ||
	(pack->dp_Type == ACTION_EX_NEXT)) {
	/* XXX: needed? */
	fib->mode = fmode;
    }
}

void
FillAttrBlock(fileattr_t *attr, struct icommon *finode)
{
    attr->fa_type      = 0;
    attr->fa_mode      = DISK16(finode->ic_mode);  /* Same as UNIX disk mode */
    attr->fa_nlink     = DISK16(finode->ic_nlink);
    attr->fa_uid       = DISK32(finode->ic_ouid);
    attr->fa_gid       = DISK32(finode->ic_ogid);
    attr->fa_size      = IC_SIZE(finode);
    attr->fa_blocksize = phys_sectorsize;
    attr->fa_rdev      = 0;
    attr->fa_blocks    = DISK32(finode->ic_blocks);
    attr->fa_fsid      = 0;
    attr->fa_fileid    = DISK32(finode->ic_ouid);
    attr->fa_atime     = DISK32(finode->ic_atime) + GMT * 3600;
    attr->fa_atime_us  = DISK32(finode->ic_atime_ns) / 1000;
    attr->fa_mtime     = DISK32(finode->ic_mtime) + GMT * 3600;
    attr->fa_mtime_us  = DISK32(finode->ic_mtime_ns) / 1000;
    attr->fa_ctime     = DISK32(finode->ic_ctime) + GMT * 3600;
    attr->fa_ctime_us  = DISK32(finode->ic_ctime_ns) / 1000;

    if (attr->fa_mode & IFCHR) {
	/* Character or block device */
	attr->fa_rdev  = DISK32(finode->ic_db[0]);
    }
}


/* parent_parse()
 *	This routine will, given a BFFSLock and a path relative to that lock,
 *	return the parent inode number and component name in the parent inode
 *	directory.  If the return value from this function (inode number) is
 *	zero, then the path is unparseable.
 */
ULONG
parent_parse(struct BFFSLock *lock, char *path, char **name)
{
	int	slash = 0;
	char	*part;
	char	*sname;
	ULONG	pinum = 0;

	PRINT(("parent_parse l=%d path=%s\n", lock, path));

	if (pinum = ResolveColon(path))
		if ((sname = strchr(path, ':')) == NULL)
			pinum = 2;
		else
			sname++;
	else {
		sname = path;
		if (lock)
			pinum = lock->fl_Key;
		else
			pinum = 2;

	}

	for (part = sname; *part != '\0'; part++);

	for (; part > sname; part--) {
		if ((*part == '/') || (*part == ':')) {
			slash = ((*part == ':') ? 2 : 1);
			*part = '\0';
			part++;
			break;
		}
	}

/*
	PRINT(("part=%s sname=%s (dif=%d) pinum=%d   ",
		part, sname, part - sname, pinum));
*/

	if (sname != part) {
		pinum = file_find(pinum, sname);
		if (!unix_paths && (part > sname + 1) && (*(part - 2) == '/'))
			pinum = dir_name_search(pinum, "..");
	} else if (*part == '/') {
/*
		PRINT(("searching for parent of /\n"));
*/
		if (unix_paths)
			pinum = 2;
		else
			pinum = dir_name_search(pinum, "..");
/*
		PRINT(("parent pinum found is %d\n", pinum));
*/
		part++;
	}

	if (slash)
		*(part - 1) = ((slash == 2) ? ':' : '/');

	*name = part;

/*
	PRINT(("part=%s pinum=%d name=%s\n\n", part, pinum, *name));
*/

	return(pinum);
}


/* path_parse()
 *	This routine will, given a BFFSLock and a path relative to that lock,
 *	return the inode number of the file (or directory) pointed to by that
 *	combination.
 */
ULONG
path_parse(struct BFFSLock *lock, char *path)
{
	char	*sname;
	ULONG	inum = 0;

/*
	PRINT(("path_parse l=%d path=%s addr=%x\n", lock, path, path));
*/

	if (inum = ResolveColon(path))
	    if (sname = strchr(path, ':'))
		sname++;
	    else {
		sname = path;
		inum = 2;
	    }
	else {
		sname = path;
	    if (lock) {
		inum		= lock->fl_Key;
		global.Pinum	= lock->fl_Pinum;
		global.Poffset	= lock->fl_Poffset;
	    } else
		inum = 2;
	}

	inum = file_find(inum, sname);


	return(inum);
}

int
open_filesystem(void)
{
	if (inhibited) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
		return (1);
	}

	PRINT(("open filesystem\n"));

	if (dev_openfail == 1)
		dev_openfail = open_ufs();
	if (dev_openfail != 1) {
		PRINT(("reading superblock\n"));
		dev_openfail = find_superblock();
		if (dev_openfail)
			strcpy(stat->disk_type, "No fs");
	} else {
		global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
	}

	if (dev_openfail) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_NOT_A_DOS_DISK;
	} else {
		open_cache();
		RemoveVolNode();
		NewVolNode();
	}
	motor_off();
	return (0);
}

void
close_filesystem(void)
{
	if (inhibited)
		return;

	PRINT(("close_filesystem\n"));

	if (superblock) {
#ifndef RONLY
		cache_flush();
		cache_cg_flush();
		superblock->fs_clean = 1;	/* unmounted clean */
		superblock_flush();
#endif
		close_cache();
		superblock_destroy();
		strcpy(stat->disk_type, "Unmounted");
	}
	if (motor_is_on)
		motor_off();
	close_ufs();
	dev_openfail = 1;

	RemoveVolNode();
}

#ifdef INTEL

unsigned short
disk16(unsigned short x)
{
    /*
     * This could be much faster in assembly as:
     *     rol.w #8, d0
     */
    return(((x >> 8) & 255) | ((x & 255) << 8));
}

unsigned long
disk32(unsigned long x)
{
    /*
     * This could be much faster in assembly as:
     *     rol.w #8, d0
     *     swap d1
     *     rol.w $8, d0
     */
    return((x >> 24) | (x << 24) |
	   ((x << 8) & (255 << 16)) |
	   ((x >> 8) & (255 << 8)));
}

#endif
