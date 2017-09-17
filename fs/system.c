#include <exec/memory.h>
#include <dos30/dos.h>
#include <dos30/dosextens.h>

#include "debug.h"
#include "handler.h"
#include "ufs.h"
#include "packets.h"
#include "system.h"

#define ACCESS_INVISIBLE 0

char *strchr();

extern	char *handler_name;
extern	int fs_partition;
char	*volumename;
struct	stat *stat = NULL;

int dir_comments   = 0;		/* symlink and device comments */
int dir_comments2  = 0;		/* all inode info comments */
int og_perm_invert = 0;		/* invert permissions on other/group */
int GMT = 5;			/* GMT offset for localtime */

struct icommon *inode_modify();

void NewVolNode()
{
    ULONG ttime;
    struct DosInfo *info;

    if (VolNode != NULL) {
	PRINT(("Attempted to create new volume node without releasing old\n"));
	return;
    }

    info = (struct DosInfo *) BTOC( ((struct RootNode *) DOSBase->dl_Root)->rn_Info);

    VolNode = (struct DeviceList *)
			AllocMem(sizeof(struct DeviceList), MEMF_PUBLIC);

    if (VolNode == NULL) {
	PRINT(("NewVolNode: unable to allocate %d bytes!\n",
		sizeof(struct DeviceList)));
	return(NULL);
    }

    volumename = (char *) AllocMem(MAXMNTLEN, MEMF_PUBLIC);
    if (volumename == NULL) {
	PRINT(("NewVolNode: unable to allocate %d bytes!\n", MAXMNTLEN));
	FreeMem(VolNode, sizeof(struct DeviceList));
	VolNode = NULL;
	return;
    }

    fsname(info, volumename);

    ttime = superblock->fs_time + GMT * 3600;
    /* (1978 - 1970) * 365.25 = 2922 */
    VolNode->dl_VolumeDate.ds_Days   = ttime / 86400 - 2922;
    VolNode->dl_VolumeDate.ds_Minute = (ttime % 86400) / 60;
    VolNode->dl_VolumeDate.ds_Tick   = (ttime % 60) * TICKS_PER_SECOND;
    VolNode->dl_Type	 = DLT_VOLUME;
    VolNode->dl_Task	 = DosPort;
    VolNode->dl_Lock	 = NULL;
    VolNode->dl_LockList = NULL;
    VolNode->dl_DiskType = ID_BFFS_DISK;   /* Jesup said if we want WB to recognize,
						it'll probably have to fake ID_DOS */
    VolNode->dl_unused   = NULL;
    VolNode->dl_Name	 = CTOB(volumename);

    Forbid();
	VolNode->dl_Next = info->di_DevInfo;
	info->di_DevInfo = (BPTR) CTOB(VolNode);
    Permit();
}

fsname(info, volumename)
struct DosInfo *info;
char *volumename;
{
    struct DeviceList *tmp;
    char *name;

    for (name = superblock->fs_fsmnt + MAXMNTLEN - 2;
	 name > superblock->fs_fsmnt + 1; name--)
	if (*name == '\0') {
	    name++;
	    break;
	}

    if (*name == '\0') {
	name = "BFFS0";
	name[strlen(name) - 1] = 'a' + fs_partition;
    }

    /* rename if already exists */
    Forbid();
	tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
	while (tmp != NULL)
	    if (!strcmp(((char *) BTOC(tmp->dl_Name)) + 1, name)) {
		name[strlen(name) - 1]++;
		tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
	    } else
		tmp = (struct DeviceList *) BTOC(tmp->dl_Next);
    Permit();

    strcpy(volumename + 1, name);
    volumename[0] = strlen(volumename + 1);
}

void RemoveVolNode()
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
	PRINT(("Unable to find our VolNode to remove.\n"));

    FreeMem(BTOC(VolNode->dl_Name), MAXMNTLEN);
    FreeMem(VolNode, sizeof(struct DeviceList));

    VolNode = NULL;
}

close_files()
{
    struct BFFSLock *lock = NULL;

    if (VolNode == 0) {
	PRINT(("volume was not opened\n"));
	return;
    }

    PRINT(("looping da nodes\n"));
    lock = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
    while (lock != NULL) {
	if (lock->fl_Fileh && (lock->fl_Fileh->access_mode == MODE_WRITE)) {
	    PRINT(("write closing i=%d\n", lock->fl_Key));
	    truncate_file(lock->fl_Fileh);
	}
	lock = (struct BFFSLock *) BTOC(lock->fl_Link);
    }
}


truncate_file(fileh)
struct BFFSfh *fileh;
{
    struct icommon *inode;
    time_t timeval;

    if (fileh->truncate_mode == MODE_TRUNCATE) {
	inode = inode_modify(fileh->real_inum);
	time(&timeval);				/* get local time */
	timeval += 2922 * 86400 - GMT * 3600;
	inode->ic_mtime = timeval;
	if (fileh->access_mode == MODE_READ)
	    file_block_extend(fileh->real_inum);
	if (IC_SIZE(inode) > fileh->maximum_position) {
	    inode = inode_modify(fileh->real_inum);
	    IC_SETSIZE(inode, fileh->maximum_position);
	    file_blocks_deallocate(fileh->real_inum);
	}
	file_block_retract(fileh->real_inum);
	fileh->access_mode = MODE_READ;
    } else if (fileh->access_mode == MODE_WRITE) {
	inode = inode_modify(fileh->real_inum);
	time(&timeval);				/* get local time */
	timeval += 2922 * 86400 - GMT * 3600;
	inode->ic_mtime = timeval;
	file_block_retract(fileh->real_inum);
	fileh->access_mode = MODE_READ;
    }
}

struct BFFSLock *CreateLock(key, mode, pinum, poffset)
ULONG	key;
int	mode;
ULONG	pinum;
ULONG	poffset;
{
    struct BFFSLock *lock = NULL;
    int access = 0;

    if (VolNode == NULL) {
	global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
	return(NULL);
    }

    lock = (struct BFFSLock *) BTOC(VolNode->dl_LockList);
    while (lock != NULL)
	if (lock->fl_Key == key) {
	    access = lock->fl_Access;
	    break;
	} else
	    lock = (struct BFFSLock *) BTOC(lock->fl_Link);

    switch (mode) {
	case EXCLUSIVE_LOCK:
		if (access) {  /* somebody else has a lock on it... */
		    global.Res2 = ERROR_OBJECT_IN_USE;
		    return(NULL);
		}
		break;
	case ACCESS_INVISIBLE:
		break;
	default:  /* C= told us if it's not EXCLUSIVE, it's SHARED */
		if (access == EXCLUSIVE_LOCK) {
		    global.Res2 = ERROR_OBJECT_IN_USE;
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

MoveMemBack(source, dest, size)
char *source;
char *dest;
int size;
{
	for (; size; size--)
		*(dest++) = *(source++);
}

int ResolveColon(name)
char *name;
{
    struct	DosInfo *info;
    struct	DeviceList *tmp;
    int		length;
    char	*pos;
    struct	BFFSLock *fl;

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

strneqv(str, lower, length)
char	*str;
char	*lower;
int	length;
{
	while (length) {
		if (*str != *lower)
			if ((*str >= 'A') && (*str <= 'Z')) {
				if ((*str + ('a' - 'A')) != *lower)
					return(0);
			} else
				return(0);
		str++;
		lower++;
		length--;
	}

	if (*lower == '\0')
		return(1);
	else
		return(0);
}

NameHandler(name)
char *name;
{
	char *pos;
	if (handler_name != NULL)
		return;

	handler_name = (char *) AllocMem(name[0] + 1, MEMF_PUBLIC);
	if (handler_name == NULL) {
		PRINT(("NameHandler: unable to allocate %d bytes!\n", name[0]));
		exit(1);
	}
	strncpy(handler_name, name + 1, name[0] - 1);
	handler_name[name[0] - 1] = '\0';
	if (pos = strchr(handler_name, ':'))
		*pos = '\0';
	strlower(handler_name);
	PRINT(("handler=\"%s\"\n", handler_name));
}

UnNameHandler()
{
	if (handler_name) {
		FreeMem(handler_name, strlen(handler_name[0]));
		handler_name = NULL;
	}
}

InitStats()
{
	int	current;
	int	size;
	ULONG	*ptr;

	if (stat == NULL) {
		stat = (struct stat *) AllocMem(sizeof(struct stat), MEMF_PUBLIC);
		if (stat == NULL) {
			PRINT(("** not enough memory for stats\n"));
			return(1);
		}
	}

	size = sizeof(struct stat) / sizeof(ULONG);
	current = size;
	ptr = (ULONG *) stat;

	for (; current; current--, ptr++)
		*ptr = 0;

	stat->size = size;
	((char *) stat->magic)[0] = 'B';
	((char *) stat->magic)[1] = 'F';
	((char *) stat->magic)[2] = 'F';
	((char *) stat->magic)[3] = '\0';

	return(0);
}

void FillInfoBlock(fib, lock, finode, dir_ent)
struct FileInfoBlock *fib;
struct BFFSLock *lock;
struct icommon *finode;
struct direct *dir_ent;
{
    int		fmode;
    int		temp;
    ULONG	ttime;

    fmode = finode->ic_mode;
    fib->fib_Comment[0] = 0;				/* zero length */
    fib->fib_Comment[1] = '\0';				/* NULL terminated */
    fib->fib_Size	= (long) 0;
    fib->fib_NumBlocks	= (long) 0;

    /* give file information in the comment field */
    if (dir_comments2) {
	sprintf(fib->fib_Comment + 1,
		"%si=%-4d p=%04o u=%-5d g=%-5d l=%-2d bl=%-4d s=%d",
		temp ? " " : "", dir_ent ? dir_ent->d_ino : 0, fmode & 07777,
		finode->ic_uid, finode->ic_gid, finode->ic_nlink,
		finode->ic_blocks, IC_SIZE(finode));
	fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
    }

    fib->fib_Size	= IC_SIZE(finode);	/* bytes */
    fib->fib_NumBlocks  = finode->ic_blocks / NSPF(superblock);

    if ((lock->fl_Key == ROOTINO) && (pack->dp_Type == ACTION_EXAMINE_OBJECT))
	fib->fib_DirEntryType = ST_ROOT;
    else if ((fmode & IFMT) == IFREG)
	fib->fib_DirEntryType = ST_FILE;
    else if ((fmode & IFMT) == IFDIR) {
	fib->fib_DirEntryType = ST_USERDIR;
	fib->fib_Size	    = IC_SIZE(finode);	/* bytes */
	fib->fib_NumBlocks  = finode->ic_blocks / NSPF(superblock);
    } else if ((fmode & IFMT) == IFLNK) {
	fib->fib_DirEntryType = ST_SOFTLINK;
	if (dir_comments) {
	    if (temp = strlen(fib->fib_Comment + 1))
		fib->fib_Comment[temp++ + 1] = ' ';
	    strcpy(fib->fib_Comment + temp + 1, "-> ");
	    file_read(dir_ent->d_ino, 0, 78 - temp,
		      fib->fib_Comment + strlen(fib->fib_Comment + 1) + 1);
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if (fmode & IFCHR) {
	if ((fmode & IFMT) == IFBLK)
		fib->fib_DirEntryType = ST_BDEVICE;
	else
		fib->fib_DirEntryType = ST_CDEVICE;

	if (dir_comments) {
	    if (temp = strlen(fib->fib_Comment + 1))
		fib->fib_Comment[temp++ + 1] = ' ';
	    sprintf(fib->fib_Comment + temp + 1, "%cdev(%d,%d)",
		    'B' + (-1 * ST_BDEVICE) + fib->fib_DirEntryType,
		    finode->ic_db[0] >> 8, finode->ic_db[0] & 255);
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if ((fmode & IFMT) == IFIFO) {
	fib->fib_DirEntryType = ST_FIFO;
	if (dir_comments) {
	    sprintf(fib->fib_Comment + temp + 1, "fifo");
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    } else if ((fmode & IFMT) == IFSOCK) {
	fib->fib_DirEntryType = ST_SOCKET;
	if (dir_comments) {
	    sprintf(fib->fib_Comment + temp + 1, "socket");
	    fib->fib_Comment[0] = strlen(fib->fib_Comment + 1);
	}
    }


    fib->fib_Protection = 0;
    /* bummer, can't support archive or hidden - not enough UNIX bits	     */
    /* fib->fib_Protection |= ((fmode & (IEXEC >> 3))? 0 : FIBF_ARCHIVE);    */
    /* fib->fib_Protection |= (((fmode & IFMT) == IFLNK) ? FIBF_HIDDEN : 0); */

    /* Yes, it's complicated to figure out */
    if (fmode & ISUID) {
	if (fmode & ISVTX)
		fib->fib_Protection |= FIBF_SCRIPT;
	else
		fib->fib_Protection |= FIBF_PURE | FIBF_SCRIPT;
    } else if (fmode & ISVTX)
	fib->fib_Protection |= FIBF_PURE;


    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_READ   );
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_WRITE | FIBF_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_EXECUTE);

    fmode <<= 3;	/* look at group bits now */
if (og_perm_invert) {
    fib->fib_Protection |= ((fmode & IREAD)  ? FIBF_GRP_READ    : 0);
    fib->fib_Protection |= ((fmode & IWRITE) ? FIBF_GRP_WRITE | FIBF_GRP_DELETE : 0);
    fib->fib_Protection |= ((fmode & IEXEC)  ? FIBF_GRP_EXECUTE : 0);
    fmode <<= 3;	/* look at other bits now */
    fib->fib_Protection |= ((fmode & IREAD)  ? FIBF_OTR_READ    : 0);
    fib->fib_Protection |= ((fmode & IWRITE) ? FIBF_OTR_WRITE | FIBF_OTR_DELETE : 0);
    fib->fib_Protection |= ((fmode & IEXEC)  ? FIBF_OTR_EXECUTE : 0);
} else {
    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_GRP_READ   );
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_GRP_WRITE | FIBF_GRP_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_GRP_EXECUTE);
    fmode <<= 3;	/* look at other bits now */
    fib->fib_Protection |= ((fmode & IREAD)  ? 0 : FIBF_OTR_READ   );
    fib->fib_Protection |= ((fmode & IWRITE) ? 0 : FIBF_OTR_WRITE | FIBF_OTR_DELETE);
    fib->fib_Protection |= ((fmode & IEXEC)  ? 0 : FIBF_OTR_EXECUTE);
}

    fib->fib_OwnerUID  = finode->ic_uid;
    fib->fib_OwnerGID  = finode->ic_gid;

    fib->fib_EntryType = fib->fib_DirEntryType;		/* must be the same */
    ttime = finode->ic_mtime + GMT * 3600;
    if (ttime == 0)
	ttime = superblock->fs_time;

    /* 86400 = 24 * 60 * 60           = (seconds in a day ) */
    /*  2922 = (1978 - 1970) * 365.25 = (clock birth difference in days) */
    /* 18000 = 5 * 60 * 60            = (clock birth difference remainder seconds) */

    fib->fib_Date.ds_Days   =  ttime / 86400 - 2922;	      /* Date today */
    fib->fib_Date.ds_Minute = (ttime % 86400) / 60;	      /* Minutes today */
    fib->fib_Date.ds_Tick   = (ttime % 60) * TICKS_PER_SECOND;/* Ticks this minute */

#define DIRNAMESIZE 107
    if ((lock->fl_Key == ROOTINO) && (pack->dp_Type == ACTION_EXAMINE_OBJECT))
	strcpy(fib->fib_FileName + 1, (char *) (BTOC(VolNode->dl_Name)) + 1);
    else {
	strncpy(fib->fib_FileName + 1, dir_ent->d_name, DIRNAMESIZE - 1);
	fib->fib_FileName[DIRNAMESIZE] = '\0';
    }
    fib->fib_FileName[0] = (char) strlen(fib->fib_FileName + 1);
/*
    PRINT(("  i=%-4d fs=%-8d fb=%-3d name=%s\n",
	   dir_ent ? dir_ent->d_ino : 0, fib->fib_Size,
	   fib->fib_NumBlocks, fib->fib_FileName+1));
*/
}


/* parent_parse()
 *	This routine will, given a BFFSLock and a path relative to that lock,
 *	return the parent inode number and component name in the parent inode
 *	directory.  If the return value from this function (inode number) is
 *	zero, then the path is unparseable.
 */
ULONG parent_parse(lock, path, name)
struct BFFSLock *lock;
char *path;
char **name;
{
	int	slash = 0;
	char	*part;
	char	*sname;
	ULONG	pinum = 0;

	/* PRINT(("parent_parse l=%d path=%s\n", lock, path)); */

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
	PRINT(("part=%s sname=%s (%d %d) pinum=%d   ",
		part, sname, part, sname, pinum));
*/
	if (sname != part)
		pinum = file_find(pinum, sname);
	else if (*part == '/') {
		pinum = file_find(pinum, "/");
		part++;
	}

	if (slash)
		*(part - 1) = ((slash == 2) ? ':' : '/');

	*name = part;

/*	PRINT(("part=%s pinum=%d\n", part, pinum)); */

	return(pinum);
}


/* path_parse()
 *	This routine will, given a BFFSLock and a path relative to that lock,
 *	return the inode number of the file (or directory) pointed to by that
 *	combination.
 */
ULONG path_parse(lock, path)
struct BFFSLock *lock;
char *path;
{
	char	*sname;
	ULONG	inum = 0;

/*
PRINT(("path_parse l=%d path=%s\n", lock, path));
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

/*
PRINT(("sname=%s inum=%d\n", sname, inum));
*/

	return(inum);
}


open_filesystem()
{
	if (inhibited)
		return(1);

	PRINT(("open filesystem\n"));

	if (dev_openfail == 1)
		dev_openfail = open_ufs();
	if (dev_openfail != 1) {
		PRINT(("reading superblock\n"));
		dev_openfail = find_superblock();
		if (dev_openfail)
			strcpy(stat->disk_type, "No fs");
	} else
		global.Res2 = ERROR_DEVICE_NOT_MOUNTED;

	if (dev_openfail) {
		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_NOT_A_DOS_DISK;
	} else {
		open_cache();
		RemoveVolNode();
		NewVolNode();
	}
	motor_off();
}


close_filesystem()
{
	if (inhibited)
		return(1);

	PRINT(("close filesystem\n"));

	if (superblock) {
		cache_flush();
		cache_cg_flush();
		superblock->fs_clean = 1;	/* unmounted clean */
		superblock_flush();
		close_cache();
		superblock_destroy();
		strcpy(stat->disk_type, "Unmounted");
	}
	motor_off();
	close_ufs();
	dev_openfail = 1;

	RemoveVolNode();
}

strlower(str)
char	*str;
{
	while (*str != '\0') {
		if ((*str >= 'A') && (*str <= 'Z'))
			*str += ('a' - 'A');
		str++;
	}
}
