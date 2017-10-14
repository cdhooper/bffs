#include <exec/memory.h>
#include <dos30/dos.h>

#include "config.h"
#include "superblock.h"
#include "ufs.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "file.h"
#include "handler.h"
#include "cache.h"

char *strchr();
struct direct *dir_next();
ULONG dir_name_search();
extern int case_independent;	/* 1=case independent dir name searches */


/* This set of routines will only modify directory files.  They do not
   handle allocation, except when more directory space is required. */


#ifndef RONLY
/* dir_create()
 *	This routine will add a name to the inode specified by the inode
 *	number passed to this routine.  If the directory needs to be
 *	expanded to accommodate the filename, this is handled automatically.
 *	The algorithm is as follows:
 *		Read directory, one block at a time
 *		If at beginning of block and inode=0, add file there
 *		If reclen of new entry < (reclen of current - actual)
 *			adjust reclen of current, add entry after current,
 *			size = remainder
 *		If entire dir full, call allocate routine for another block
 *			near last dir block allocated.
 *	On success, this routine returns the directory offset where the
 *	new directory entry is to be found.
 */
dir_create(pinum, name, inum, type)
ULONG	pinum;
char	*name;
ULONG	inum;
int	type;
{
	char	*buf;
	int	blk;
	int	spill;
	int	fblks;
	int	index;
	int	needed;
	int	oldsize;
	int	newfrag;
	struct	icommon	*pinode;
	struct	direct	*bend;
	struct	direct	*current;

	PRINT(("dir_create pinum=%d name=%s inum=%d\n", pinum, name, inum));

	if (pinum < 2) {
		PRINT2(("dc: ** bad parent directory inode number %d\n", pinum));
		return(0);
	}

	pinode	= inode_read(pinum);
	if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
		return(0);

	needed	= (strlen(name) + sizeof(struct direct) - MAXNAMLEN - 1 + 3) & ~3;
	if (needed < 0) {
		PRINT2(("dc: ** bad filename size %d\n", needed));
		return(0);
	}

#undef DIR_DEBUG
#ifdef DIR_DEBUG
	PRINT(("name='%s' len=%d needed=%d ", name, strlen(name), needed));
#endif

	fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill =  IC_SIZE(pinode)              % FSIZE;

#ifdef DIR_DEBUG
	PRINT(("cre: fblks=%d spill=%d dblks=%d icblks=%d pinum=%d\n", fblks,
		spill, IC_SIZE(pinode), DISK32(pinode->ic_blocks), pinum));
#endif

	for (blk = 0; blk < fblks; blk++) {
#ifdef DIR_DEBUG
	    PRINT(("_"));
#endif
	    buf     = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
	    if (buf == NULL)
		break;
	    current = (struct direct *) buf;
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));
#ifdef DIR_DEBUG
	    PRINT(("current=%p bend=%p\n", current, bend));
#endif
	    while (current < bend) {
#ifdef DIR_DEBUG
		PRINT((":%s", current->d_name));
#endif
		if ((DISK16(current->d_reclen) - DIRSIZ(current)) > needed) {
		    buf = cache_frag_write(ridisk_frag(blk, pinode), 1);
		    if (buf == NULL)
			break;
		    /* otherwise, we found a place to put new name */
		    oldsize = DISK16(current->d_reclen);
		    current->d_reclen = DISK16(DIRSIZ(current));
		    oldsize -= DISK16(current->d_reclen);
		    current = (struct direct *) ((char *) current +
			DISK16(current->d_reclen));
		    goto finish_success;
		}
#ifndef FAST
		if (DISK16(current->d_reclen) > DIRBLKSIZ) {
			PRINT2(("INCON: directory corrupt - reclen %d > %d\n",
				DISK16(current->d_reclen), DIRBLKSIZ));
			return(0);
		}
#endif
		current = (struct direct *) ((char *) current +
			   DISK16(current->d_reclen));
	    }
	}

#ifdef DIR_DEBUG
	PRINT(("\n"));
#endif

	if (spill != 0) {
	    /*
	     * No space is available in all directory blocks, but there is
	     * space in the frag for at least one more directory block.
	     * Update the inode to expand the directory.
	     */
	    pinode = inode_modify(pinum);
	    PRINT(("inum %d size=%d blocks=%d",
		   pinum, IC_SIZE(pinode), DISK32(pinode->ic_blocks)));
	    IC_INCSIZE(pinode, FSIZE - spill);
	    PRINT((" -> size=%d blocks=%d\n",
		   IC_SIZE(pinode), DISK32(pinode->ic_blocks)));
	    blk = fblks - 1;
	    newfrag = ridisk_frag(blk, pinode);
	    buf = cache_frag_write(newfrag, 1);
	} else {
	    /*
	     * Searched all directory blocks and no space is available.
	     * We must allocate another frag to the directory.
	     */
	    newfrag = frag_expand(pinum);
	    if (newfrag == 0) {
		PRINT(("No space left in dir!\n"));
		return(0);
	    }
	    buf = cache_frag_write(newfrag, 0);
	}

#ifndef FAST
	if (buf == NULL) {
		PRINT2(("** ERROR, cache routine returned NULL pointer\n"));
		return(0);
	}
#endif
	dir_fragfill(spill, buf);
	current = (struct direct *) (buf + spill);
	oldsize = DIRBLKSIZ;
	goto finish_success;

#if 0
	strcpy(current->d_name, name);
	current->d_ino    = DISK32(inum);
	current->d_reclen = DISK16(oldsize);
	current->d_namlen = strlen(name);
	if (bsd44fs)
		current->d_type  = type;
	else
		current->d_type  = 0;		/* dir type unknown now */
	return(FSIZE * blk + spill);
#endif

finish_success:
	strcpy(current->d_name, name);
	current->d_ino    = DISK32(inum);
	current->d_reclen = DISK16(oldsize);
	current->d_namlen = strlen(name);
	if (bsd44fs)
		current->d_type  = type;
	else
		current->d_type  = 0;		/* dir type unknown now */
	return(FSIZE * blk + (char *) current - buf);	/* dir offset */
}



/* dir_offset_delete()
 *	This routine will delete the name of a file from the passed
 *	directory given that name's offset into the directory.  Pass
 *	the inode number of the parent and that file's offset in the
 *	parent.  When given the offset for a directory, the routine
 *	will first check to assure the directory is empty before
 *	erasing the name.  If the check parameter is zero, this check
 *	is skipped and the deletion proceeds unconditionally.
 *
 *	The algorithm is as follows:
 *		Read dir blocks one at a time (into buf)
 *		Once found, extend prior directory entry to cover current
 *		If first in directory block, move a successor to the
 *			file and extend that.
 *		If there is no successor, zero the inode and set its
 *			record length to a directory block size.
 */
dir_offset_delete(pinum, offset, check)
ULONG	pinum;
ULONG	offset;
int	check;
{
	ULONG	inum;
	ULONG	blk;
	ULONG	fblks;
	ULONG	spill;
	char	*buf;
	char	*obuf;
	ULONG	pos;
	ULONG	roffset;
	ULONG	dirblk;
	struct	direct *bend;
	struct	direct *last;
	struct	direct *current;
	struct	icommon *pinode;

	PRINT(("dir_offset_delete: pinum=%d, offset=%d check=%d\n",
		pinum, offset, check));

	pinode	= inode_read(pinum);

	if ((offset == 0) || (offset > IC_SIZE(pinode))) {
		PRINT2(("INCON: dir_offset_delete: impossible offset %d\n", offset));
		return(0);
	}


	fblks	= (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill	=  IC_SIZE(pinode)              % FSIZE;
	roffset = (offset / FSIZE) * FSIZE;

	for (blk = offset / FSIZE; blk < fblks; blk++) {
	    dirblk = ridisk_frag(blk, inode_read(pinum));
	    buf    = cache_frag(dirblk);
	    if (buf == NULL)
		break;
	    current = (struct direct *) buf;
/*
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));
*/
	    bend = (struct direct *) (buf + ((spill && (blk == fblks - 1)) ?
						spill : FSIZE));

	    pos  = 0;
	    last = NULL;
	    while (current < bend) {
		if (pos == DIRBLKSIZ) {
		    pos = 0;
		    last = NULL;
		}
/*		PRINT((":%s%d", current->d_name, DISK16(current->d_reclen))); */
		if (offset == roffset) {
		    inum = DISK32(current->d_ino);
		    goto breakloop;
		}
		last = current;
#ifndef FAST
		if (DISK16(current->d_reclen) > DIRBLKSIZ) {
			PRINT2(("INCON: directory corrupt - DIRBLKSIZ > %d\n",
				DIRBLKSIZ));
			return(0);
		}
#endif
		pos     += DISK16(current->d_reclen);
		roffset += DISK16(current->d_reclen);
		current = (struct direct *) ((char *) current +
				DISK16(current->d_reclen));
	    }
	}

	/* if we got here, file was not found - this is an error */
	global.Res2 = ERROR_OBJECT_NOT_FOUND;
/*	PRINT(("\n")); */
	return(0);

	breakloop:
/*	PRINT(("\n")); */

	/* if we are deleting a directory, we must first check to make
		sure there are only two entries left in it; '.' and '..' */
	if (check && dir_is_not_empty(inum)) {
		global.Res2 = ERROR_DIRECTORY_NOT_EMPTY;
		PRINT2(("directory %d is not empty\n", inum));
		return(0);
	}
	obuf = buf;
	buf  = cache_frag_write(dirblk, 1);

#ifndef FAST
	if (buf != obuf) {
		PRINT2(("INCON: dod %08x != %08x\n", buf, obuf));
		current = (struct direct *)
				((char *) current + (ULONG) buf - (ULONG) obuf);
		if (last != NULL)
			last = (struct direct *)
				((char *) last + (ULONG) buf - (ULONG) obuf);
	}
	if (buf == NULL) {
		PRINT2(("INCON: Can't ro->rw inode %d's cached blk %d\n",
			inum, dirblk));
		return(0);
	}
#endif
	if (last == NULL) { /* at beginning */
/*
		PRINT(("%d is at beginning %s\n", inum, current->d_name));
*/
		current->d_ino = 0;
	} else {
/*
		PRINT(("%d is in middle: last=%d(%d)%s current=%d(%d)%s\n",
			inum, DISK16(last->d_reclen), DISK32(last->d_ino),
			last->d_name, DISK16(current->d_reclen),
			DISK32(current->d_ino), current->d_name));
*/

#ifndef FAST
		if (DISK16(current->d_reclen) > DIRBLKSIZ)
			return(0);
#endif
		last->d_reclen = DISK16(DISK16(last->d_reclen) +
				 	DISK16(current->d_reclen));
	}

	return(inum);
}


/* dir_rename()
 *	This routine will rename a file in the passed directory to
 *	the passed new name (possibly) in another directory.  Pass
 *	it from parent, from name, to parent, to name.  This routine
 * 	will check for loops or disconnections before allowing the
 *	rename to occur.
 */
dir_rename(frominum, fromname, toinum, toname, isdir, type)
ULONG	frominum;
char	*fromname;
ULONG	toinum;
char	*toname;
int	isdir;
int	type;
{
	ULONG	inum;
	ULONG	inumfrom;
	ULONG	offsetfrom;
	ULONG	offsetto;
	struct	icommon *inode;

	inumfrom   = dir_name_search(frominum, fromname);
	offsetfrom = global.Poffset;

	if (inumfrom == 0)		/* failed to find source file/dir */
		return(0);

	inode = inode_read(frominum);
	if ((DISK16(inode->ic_mode) & IFMT) != IFDIR) {
		PRINT2(("bad source directory i=%d\n", frominum));
		return(0);
	}

	inode = inode_read(toinum);
	if ((DISK16(inode->ic_mode) & IFMT) != IFDIR) {
		PRINT2(("bad destination directory i=%d\n", toinum));
		return(0);
	}

	/* check for bad dest directory */
	if (toinum == inumfrom) {
		PRINT2(("attempted to put parent %d in itself\n", toinum));
		return(0);
	}

	inum = toinum;

	/* check for attempt to put parent in child */
	if (isdir)
	    while (inum != 2) {
		inum = dir_name_search(inum, "..");
/*
		PRINT2(("ltp inum=%d\n", inum));
*/
		if (inum == inumfrom) {
			PRINT2(("rename: attempted to put parent %d in child %d\n",
				toinum, inumfrom));
			return(0);
		}
	    }

	if (offsetto = dir_create(toinum, toname, inumfrom, type))
	    if ((inum = dir_offset_delete(frominum, offsetfrom, 0)) == 0) {
		PRINT2(("dr: ** can't delete old %s, deleting new\n", toname));
		dir_offset_delete(toinum, offsetto, 1);
	    } else
		return(inum);
	else
	    PRINT2(("rename: can't create %s\n", toname));

	return(0);
}


/* dir_inum_change()
 *	This routine will  change an inode number in a directory structure.
 *	The parent is first checked to assure it is a directory.
 */
dir_inum_change(pinum, finum, tinum)
ULONG pinum;
ULONG finum;
ULONG tinum;
{
	int	blk;
	int	fblks;
	int	spill;
	char	*buf;
	struct	icommon	*pinode;
	struct	direct	*bend;
	struct	direct	*current;

	PRINT(("dic: p=%d from=%d to=%d\n", pinum, finum, tinum));

	if (pinum < 2) {
		PRINT2(("bad parent directory inode number %d\n", pinum));
		return(0);
	}

	pinode	= inode_read(pinum);
	fblks	= (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill	=  IC_SIZE(pinode)              % FSIZE;

	for (blk = 0; blk < fblks; blk++) {
	    buf     = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
	    if (buf == NULL)
		break;
	    current = (struct direct *) buf;
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));

	    while (current < bend) {
		if (DISK32(current->d_ino) == finum) {
		    buf = cache_frag_write(ridisk_frag(blk, pinode), 1);
		    if (buf == NULL)
			break;
		    goto breakloop;
		}
#ifndef FAST
		if (DISK16(current->d_reclen) > DIRBLKSIZ)
			return(0);
#endif
		current = (struct direct *) ((char *) current + DISK16(current->d_reclen));
	    }
	}

	PRINT2(("** Unable to find inode %d in %d to change to %d!\n",
		pinum, finum, tinum));
	return(0);

	breakloop:
	current->d_ino = DISK32(tinum);
	return(1);
}


/* dir_fragfill()
 * 	warning - this routine will modify the last frag in a dir block
 *	to have blank directory entries starting at the spill position.
 */
dir_fragfill(int spill, char *buf)
{
	int index;

	for (index = spill; index < FSIZE; index += DIRBLKSIZ) {
		struct direct *ptr = (struct direct *) (buf + index);
		ptr->d_ino     = 0;
		ptr->d_namlen  = 0;
		ptr->d_type    = 0;
		ptr->d_reclen  = DISK16(DIRBLKSIZ);
		ptr->d_name[0] = '\0';
	}
}
#endif


/* dir_is_not_empty()
 *	This routine will return true if the passed dir (referenced by inode
 *	number) is not empty.  (not counting the . and .. entries)
 */
dir_is_not_empty(pinum)
int pinum;
{
	int	blk;
	int	fblks;
	int	count = 0;
	int	spill;
	struct	icommon *pinode;
	char	*buf;
	struct	direct *bend;
	struct	direct *current;

	pinode	= inode_read(pinum);

	if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
		return(0);

	fblks	= (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill	=  IC_SIZE(pinode)              % FSIZE;

	PRINT(("dir contents=\n"));
	for (blk = 0; blk < fblks; blk++) {
	    buf     = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
	    if (buf == NULL)
		break;
	    current = (struct direct *) buf;
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));

	    while (current < bend) {
		if (current->d_ino) {
		    PRINT(("%s%d:\n", current->d_name,
				DISK16(current->d_reclen)));
		    count++;
		    if (count > 2) {
			PRINT(("\n"));
			return(1);
		    }
		}
#ifndef FAST
		if ((DISK16(current->d_reclen) > DIRBLKSIZ) ||
		    (DISK16(current->d_reclen) == 0)) {
			PRINT2(("INCON: directory corrupt - remove directory?  run fsck\n"));
			/* return 0 to remove dir, 1 to leave it alone */
			return(0);
		}
#endif
		current = (struct direct *) ((char *) current + DISK16(current->d_reclen));
	    }
	}
	PRINT(("\n"));

	return(0);
}


/* dir_name_is_illegal()
 *	This routine will return true if the specified file name
 *	is not a legal file name to be written in a directory.
 */
dir_name_is_illegal(name)
char *name;
{
	/* check for no name */
	if (*name == '\0')
		return(1);

	/* check for . and .. */
	if ((*name == '.') && ((*(name + 1) == '\0') ||
	    ((*(name + 1) == '.') && (*(name + 2) == '\0'))))
		return(1);

	/* check for path components in name */
	while (*name != '\0') {
		if ((*name == '/') || (*name == ':') || (*name == '\"'))
			return(1);
		name++;
	}

	return(0);
}


/* dir_name_search()
 *	This routine is called to locate a given filename in the
 *	filesystem.  The inode number for that name is returned
 *	to the calling routine.  The name must be located directly
 *	in the specified parent inode number.  If a path lookup
 *	is required, call the file_find routine in file.c
 */
ULONG dir_name_search(pinum, name)
ULONG	pinum;
char	*name;
{
	int	fblks;
	int	spill;
	int	blk;
	char	*buf;
	ULONG	inum = 0;
	struct	icommon *pinode;
	struct	direct  *dirent;
	struct	direct  *bend;

	pinode = inode_read(pinum);
	if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR) {
		PRINT2(("INCON: Not a directory! i=%d n=%s\n", pinum, name));
		return(0);
	}

	fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill =  IC_SIZE(pinode)              % FSIZE;

	for (blk = 0; blk < fblks; blk++) {
/*	    PRINT(("_")); */
	    buf    = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
	    if (buf == NULL) {
		PRINT2(("** INCON: cache gave null buffer\n"));
		break;
	    }
	    dirent = (struct direct *) buf;
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));

	    while (dirent < bend) {
/*		PRINT((";%s", dirent->d_name)); */
		if (dirent->d_ino && !strcmp(dirent->d_name, name)) {
		    inum = DISK32(dirent->d_ino);
		    goto breakloop;
		}
#ifndef FAST
		if (DISK16(dirent->d_reclen) > DIRBLKSIZ) {
			PRINT2(("** dir reclen %d > BLKSIZ\n",
				DISK16(dirent->d_reclen)));
			return(0);
		}
		if (dirent->d_reclen == 0) {
			PRINT2(("** panic, dir record size 0 at offset %d\n",
				(ULONG)((char *)dirent - buf)));
			return(0);
		}
#endif
		dirent = (struct direct *) ((char *) dirent +
					    DISK16(dirent->d_reclen));
	    }
	}

	if ((inum == 0) && case_independent) {
	    for (blk = fblks - 1; blk >= 0; blk--) {
/*		PRINT(("_")); */
		buf    = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
		if (buf == NULL) {
		    PRINT2(("** cache gave null buffer\n"));
		    break;
		}
	        dirent = (struct direct *) buf;
		bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));

		while (dirent < bend) {
/*		    PRINT(("|%s", dirent->d_name)); */
		    if (streqv(dirent->d_name, name)) {
			inum = DISK32(dirent->d_ino);
			goto breakloop;
		    }
		    dirent = (struct direct *) ((char *) dirent +
						DISK16(dirent->d_reclen));
		}
	    }
	}

	breakloop:

/*	PRINT(("\n"));
	PRINT(("returning i=%d\n", inum)); */

	if (inum)
		global.Poffset	= blk * FSIZE + ((char *) dirent - buf);

	return(inum);
}


/* dir_inum_search()
 *	This routine is called to locate a specific inode number
 *	in a parent inode number.  If found, the offset for that
 *	inode number will be returned to the calling routine.
 */
ULONG dir_inum_search(pinum, inum)
ULONG	pinum;
int	inum;
{
	int	fblks;
	int	spill;
	int	blk;
	char	*buf;
	struct	icommon *pinode;
	struct	direct  *dirent;
	struct	direct  *bend;

	pinode = inode_read(pinum);
	if ((DISK16(pinode->ic_mode) & IFMT) != IFDIR)
		return(0);

	fblks = (IC_SIZE(pinode) + FSIZE - 1) / FSIZE;
	spill =  IC_SIZE(pinode)              % FSIZE;

	for (blk = 0; blk < fblks; blk++) {
/*	    PRINT(("_")); */
	    buf    = cache_frag(ridisk_frag(blk, pinode = inode_read(pinum)));
	    if (buf == NULL)
		break;
	    dirent = (struct direct *) buf;
	    bend   = (struct direct *) (buf + ((blk < fblks - 1) ? FSIZE :
			(spill ? spill : FSIZE)));
	    while (dirent < bend) {

/*		PRINT((";%s", dirent->d_name)); */

		if (DISK32(dirent->d_ino) == inum)
		    goto breakloop;
#ifndef FAST
		if (DISK16(dirent->d_reclen) > DIRBLKSIZ)
		    return(0);
#endif
		dirent = (struct direct *) ((char *) dirent + DISK16(dirent->d_reclen));
	    }
	}

	breakloop:

/*	PRINT(("\n")); */

	if (blk != fblks)
		return(blk * DIRBLKSIZ + ((char *) dirent - buf));

	return(0);
}


/* dir_next()
 *	This routine will return a pointer to a directory entry
 *	given that entry's offset into its parent directory.
 */
struct direct *dir_next(inode, offset)
struct	icommon *inode;
ULONG	offset;
{
	ULONG	frag;
	char	*ptr;

	if ((frag = ridisk_frag(offset / FSIZE, inode)) == 0) {
		PRINT2(("** dir_next: bad frag %d\n", frag));
		return(NULL);
	}

	if (ptr = cache_frag(frag))
		return((struct direct *) (ptr + (offset % FSIZE)));
	else {
		PRINT2(("** cache did not give dir_next frag %d\n", frag));
		return(NULL);
	}
}
