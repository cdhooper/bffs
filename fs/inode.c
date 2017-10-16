#include <exec/memory.h>

#include "config.h"
#include "superblock.h"
#include "ufs/inode.h"
#include "fsmacros.h"
#include "file.h"
#include "cache.h"
#include "alloc.h"
#include "ufs.h"


/* Special file block address handling
 *
 *  When computing inode tree address:
 *
 *
 *                                     <..fblkshift..> <..pfragshift..>
 * ADDR  <.. continues shifting left  |_______________|________________|
 *
 *
 * pfragshift = n where 2^n = num of [fsfrag] ptrs in frag
 * fblkshift  = n where 2^n = num of frags in blk
 *
 * pfragmask = 2^pfragshift - 1    (fragsize / 4 - 1)
 * fblkmask  = 2^fblkshift - 1
 *
 * 4k blk, 512b frag: fblkshift=3; fblkmask=7; pfragshift=7;  pfragmask=127;
 * 4k blk, 1k   frag: fblkshift=2; fblkmask=3; pfragshift=8;  pfragmask=255;
 * 8k blk, 1k   frag: fblkshift=3; fblkmask=7; pfragshift=8;  pfragmask=255;
 * 8k blk, 8k   frag: fblkshift=0; fblkmask=0; pfragshift=11; pfragmask=2047;
 */

extern int GMT;
int   minfree = 0;
ULONG pfragshift = 8;
ULONG pfragmask  = 255;
ULONG fblkshift  = 2;
ULONG fblkmask   = 3;

ULONG fragno[NIADDR];
ULONG ptrnum[NIADDR];

ULONG prev_frag_addr();

/* ridisk_frag()
 *	This routine will return the frag address of a block in a fs given
 *	the file block number (frag address) and inode pointer.  It will not
 *	allocate blocks for holes in a file; rather, it will return 0.
 */
ULONG ridisk_frag(file_frag_num, inode)
ULONG	file_frag_num;
struct	icommon *inode;
{
	int	ind;
	int	level = 0;
	ULONG	curblk;
	ULONG	file_blk_num;
	ULONG	*temp;
	int	frac;

	file_blk_num = file_frag_num / FRAGS_PER_BLK;
	frac = file_frag_num - file_blk_num * FRAGS_PER_BLK;

	ind = file_blk_num - NDADDR;
	if (ind < 0) {
		curblk = DISK32(inode->ic_db[file_blk_num]);
		goto endreturn;
	}

	do {
		if (level)
			ind--;
		ptrnum[level] = ind & pfragmask;
		ind	    >>= pfragshift;
		fragno[level] = ind & fblkmask;
		ind	    >>= fblkshift;


/*
		PRINT(("level=%d ptrnum=%-3d fragno=%d blk=%d frag=%d ind=%d\n",
			level, ptrnum[level], fragno[level], file_blk_num,
			file_frag_num, ind));
*/


		level++;
	} while (ind > 0);

	curblk = DISK32(inode->ic_ib[level - 1]);
	if (curblk == 0) {
		PRINT(("empty block in inode\n"));
		return(0);
	}

	do {
		level--;
		temp = (ULONG *) cache_frag(curblk + fragno[level]);
#ifndef FAST
		if (temp == NULL) {
			PRINT(("no memory from cache_frag!\n"));
			return(0);
		}
#endif
		curblk = DISK32(temp[ptrnum[level]]);
		if (curblk == 0) {
/*
			PRINT(("empty block at level %d\n", level));
*/
			return(0);
		}
	} while (level > 0);

	endreturn:
	if (curblk == 0)
		return(0);
	else
		return(curblk + frac);
}


#ifndef RONLY
/* cidisk_frag()
 *	This routine will allow the change of a frag address of a block in
 *	the fs given the file block number and the inode pointer.  If an
 *	index block was required but no blocks are available on the disk,
 *	a 1 will be returned.  Otherwise, zero will be returned.  This
 *	routine will not allocate new blocks, except where as needed for
 *	index blocks.
 */
ULONG cidisk_frag(file_frag_num, inum, newblk)
ULONG	file_frag_num;
ULONG	inum;
ULONG	newblk;
{
	int	ind;
	int	level = 0;
	ULONG	curblk;
	ULONG	ocurblk;
	ULONG	file_blk_num;
	struct	icommon *inode;
	ULONG	*temp;
	int	frac;
	ULONG	prev_frag;

	inode = inode_read(inum);

	file_blk_num = file_frag_num / FRAGS_PER_BLK;
	frac = file_frag_num - file_blk_num * FRAGS_PER_BLK;

	prev_frag = prev_frag_addr(file_frag_num, inode, inum);

	ind = file_blk_num - NDADDR;
	if (ind < 0) {
	    inode = inode_modify(inum);

	    PRINT(("cidisk: d=%d from %d to %d\n",
		    file_blk_num, DISK32(inode->ic_db[file_blk_num]), newblk));

	    inode->ic_db[file_blk_num] = DISK32(newblk);
	    return(0);
	}

	do {
	    if (level)
		ind--;
	    ptrnum[level] = ind & pfragmask;
	    ind         >>= pfragshift;
	    fragno[level] = ind & fblkmask;
	    ind         >>= fblkshift;
	    level++;
	} while (ind > 0);

	curblk = DISK32(inode->ic_ib[level - 1]);
	if (curblk == 0) {
/*
	    PRINT(("cidisk: alloc main index at level l=%d\n", level));
*/
	    curblk = block_allocate(prev_frag);
	    if (curblk == 0) {
		PRINT(("cidisk: failed to alloc index block\n"));
		return(1);
	    }
	    block_erase(curblk);
	    inode = inode_modify(inum); /* modify inode on disk */
	    inode->ic_ib[level - 1] = DISK32(curblk);
	    inode->ic_blocks = DISK32(DISK32(inode->ic_blocks) + NDSPB);
	}

	do {
	    level--;
	    if (level) {
		ocurblk = curblk;
		temp = (ULONG *) cache_frag(curblk + fragno[level]);
#ifndef FAST
		if (temp == NULL)
			return(0);
#endif
		curblk = temp[ptrnum[level]];
		if (curblk == 0) {
/*
		    PRINT(("cidisk: alloc index at %d\n", ocurblk));
*/

		    /* allocate index block and zero it */
		    curblk = block_allocate(ocurblk);
		    if (curblk == 0) {		  /* no free space */
			PRINT(("cidisk: No free space to alloc iblock\n"));
			return(1);
		    }
		    block_erase(curblk);

		    inode = inode_modify(inum);   /* modify inode on disk */
		    inode->ic_blocks = DISK32(DISK32(inode->ic_blocks) + NDSPB);

		    /* update parent index block pointer */
		    temp = (ULONG *) cache_frag_write(ocurblk + fragno[level], 1);
#ifndef FAST
		    if (temp == NULL) {
			PRINT(("** cif cannot cache frag %d previously cached!\n",
				ocurblk + fragno[level]));
			return(0);
		    }
#endif
		    temp[ptrnum[level]] = DISK32(curblk);
		}
	    } else {	/* level = 0  (ptr to data block) */
		temp = (ULONG *) cache_frag_write(curblk + fragno[0], 1);
#ifndef FAST
		if (temp == NULL) {
		    PRINT(("** cif cannot cache frag %d for write!\n",
			   curblk + fragno[0]));
		    return(0);
		}
#endif

/*
		PRINT(("cidisk: update iblk %d:%d from %d to %d\n",
			curblk, fragno[0], temp[ptrnum[0]], newblk));
*/

		temp[ptrnum[0]] = DISK32(newblk);
	    }
	} while (level > 0);

	return(0);
}


/* widisk_frag()
	This routine will return the frag address of a block in a fs given
	the file block number and inode pointer.  If a hole exists at the
	specified position in the file, an entire fs block will be allocated
	and a frag address inside that block will be returned.  If no empty
	whole blocks in the filesystem exist, 0 is returned; indicating
	filesystem full.
*/
ULONG widisk_frag(file_frag_num, inode, inum)
ULONG	file_frag_num;
struct	icommon *inode;
ULONG	inum;
{
	int	ind;
	int	level = 0;
	ULONG	curblk;
	ULONG	ocurblk;
	ULONG	file_blk_num;
	ULONG	*temp;
	ULONG	prev_frag = 0;
	int	frac;

	file_blk_num = file_frag_num / FRAGS_PER_BLK;
	frac = file_frag_num - file_blk_num * FRAGS_PER_BLK;

	prev_frag = prev_frag_addr(file_frag_num, inode, inum);

	if ((ind = file_blk_num - NDADDR) < 0) {
	    curblk = DISK32(inode->ic_db[file_blk_num]);
	    if (curblk == 0) {
	        curblk = block_allocate(prev_frag);
		if (curblk == 0) {
		    PRINT(("widisk: failed to alloc block\n"));
		    return(0);
		}
		inode = inode_modify(inum);
		inode->ic_blocks = DISK32(DISK32(inode->ic_blocks) + NDSPB);
		inode->ic_db[file_blk_num] = DISK32(curblk);
	    }
	    return(curblk + frac);
	}

	do {
	    if (level)
		ind--;
	    ptrnum[level] = ind & pfragmask;
	    ind         >>= pfragshift;
	    fragno[level] = ind & fblkmask;
	    ind         >>= fblkshift;
	    level++;
	} while (ind > 0);

	curblk = DISK32(inode->ic_ib[level - 1]);
	if (curblk == 0) {
	    curblk = block_allocate(prev_frag);
	    if (curblk == 0) {
		PRINT(("widisk: failed to alloc block\n"));
		return(0);
	    }
	    inode = inode_modify(inum);
	    inode->ic_blocks = DISK32(DISK32(inode->ic_blocks) + NDSPB);
	    inode->ic_ib[level - 1] = DISK32(curblk);
	    block_erase(curblk);
	}

	do {
	    level--;
	    ocurblk = curblk;
	    temp = (ULONG *) cache_frag(curblk + fragno[level]);
#ifndef FAST
	    if (temp == NULL)
		return(0);
#endif
	    curblk = DISK32(temp[ptrnum[level]]);
	    if (curblk == 0) {
		temp = (ULONG *) cache_frag_write(ocurblk + fragno[level], 1);
#ifndef FAST
		if (temp == NULL)
		    return(0);
#endif
		curblk = block_allocate(prev_frag);
		if (curblk == 0) {
		    PRINT(("widisk: failed to alloc block\n"));
		    return(0);
		}
		inode = inode_modify(inum);
		inode->ic_blocks = DISK32(DISK32(inode->ic_blocks) + NDSPB);
		temp[ptrnum[level]] = DISK32(curblk);
		if (level)
		    block_erase(curblk);
	    }
	} while (level > 0);

	if (curblk == 0)
		return(0);
	else
		return(curblk + frac);
}


/* block_erase()
 *	This routine will erase the passed filesystem block, using the
 *	cache routines to bring each frag into memory.  Only when
 *	initializing a new index block is this routine called.
 */
block_erase(fsblock)
ULONG fsblock;
{
	int	index;

	PRINT(("block erase, frags 0-%d of fsblock %d\n",
		index, FRAGS_PER_BLK));
	for (index = 0; index < FRAGS_PER_BLK ; index++)
		ZeroMem(cache_frag_write(fsblock + index, 0), FSIZE);

}
#endif


/* inode_read()
 *	This routine will cache the given inode and return a pointer
 *	to that inode.
 *	WARNING:  The pointer returned is only guaranteed valid until
 *		  the next cache operation.
 */
struct icommon *inode_read(inum)
int inum;
{
	struct icommon *buf;

	if (inum == 0)
		inum = 2;

	buf = (struct icommon *)
		cache_frag(itod(superblock, inum) + itodo(superblock,inum));

	if (buf)
		return(buf + itofo(superblock, inum));
	else {
		PRINT(("inode_read: buffer from cache_frag is NULL!\n"));
		return(NULL);
	}
}


#ifndef RONLY
/* inode_modify()
 *	This routine will cache the given inode and return a pointer
 *	to that inode.  The inode data block containing that inode
 *	will be marked as dirty in the cache.
 *	WARNING:  The pointer returned is only guaranteed valid until
 *		  the next cache operation.  Also, this data may be
 *		  flushed out to disk with the next cache operation.
 */
struct icommon *inode_modify(inum)
int inum;
{
	struct icommon *buf;

	if (inum == 0)
		inum = 2;

	buf = (struct icommon *)
		cache_frag_write(itod(superblock, inum) + itodo(superblock,inum), 1);

	if (buf)
		return(buf + itofo(superblock, inum));
	else
		return(0);
}


/* inum_new()
 *	This routine will allocate a new inode on the disk.
 *	The inode allocation strategy implemented is as follows:
 *	If it's a file, pick the same cg as the parent directory
 *		if that cg is full (of inodes), pick the nearest cg
 *	If it's a new directory, pick the most unused cg close to the
 *		the parent directory.
 *
 *	  type = I_DIR or I_FILE
 *	parent = parent inum
 */
int inum_new(parent, type)
int	parent;
int	type;
{
	struct	cg *mycg;
	int	cgx;
	int	inum = 0;
	int	stopat = 0;
	int	index;
	int	ifree;

	if (superblock->fs_cstotal.cs_nifree == 0) {
		PRINT(("No inodes available in filesystem\n"));
		return(0);
	}

#ifndef FAST
	if (parent > DISK32(superblock->fs_ipg) * DISK32(superblock->fs_ncg)) {
		PRINT(("bad parent inode number!\n"));
		return(0);
	}
#endif


	cgx = itog(superblock, parent);
	mycg = cache_cg(cgx);


#ifndef FAST
	if (!cg_chkmagic(mycg)) {
		PRINT(("Read a block which was NOT a cg block!\n"));
		return(0);
	}
#endif

	if (type == I_DIR) {
	    ifree = 0;
	    for (index = 0; index < DISK32(superblock->fs_ncg); index++)
		if ((long) (DISK32(superblock->fs_cs(superblock,index).cs_nifree) > ifree) &&
		    (DISK32(superblock->fs_cs(superblock, index).cs_nbfree) ||
		     DISK32(superblock->fs_cs(superblock, index).cs_nffree))) {
		    ifree = DISK32(superblock->fs_cs(superblock, index).cs_nifree);
		    cgx   = index;
		}

	    if (ifree == 0) {
		PRINT2(("INCON: No inodes available in filesystem\n"));
		return(0);
	    }

	    mycg = cache_cg(cgx);
	} else if (mycg->cg_cs.cs_nifree == 0) {
	    for (index = cgx; index < DISK32(superblock->fs_ncg); index++)
		if (superblock->fs_cs(superblock, index).cs_nifree)
		    break;
	    if (index == DISK32(superblock->fs_ncg))
		for (index = cgx - 1; index >= 0; index--)
		    if (superblock->fs_cs(superblock, index).cs_nifree)
			break;

	    if (index < 0) {
		PRINT2(("INCON: No inodes available in filesystem\n"));
		return(0);
	    }

	    cgx  = index;
	    mycg = cache_cg(cgx);
	}

	if (DISK32(mycg->cg_irotor) < DISK32(mycg->cg_niblk))
		inum = DISK32(mycg->cg_irotor);

	if (inum == 0)
		inum = (cgx ? 0 : 3);

	stopat = inum;

	for (; inum < DISK32(mycg->cg_niblk); inum++)
		if (bit_chk(cg_inosused(mycg), inum) == 0)
			goto found_clear;

	PRINT(("inew: none found in search from preferred to %d\n",
		DISK32(mycg->cg_niblk)));

	for (inum = (cgx ? 0 : 3); inum < stopat; inum++)
		if (bit_chk(cg_inosused(mycg), inum) == 0)
			goto found_clear;

	PRINT2(("INCON: Unable to find free inode - SUMMARY LIED!\n"));
	return(0);

	found_clear:

	mycg = cache_cg_write(cgx);
	mycg->cg_irotor = DISK32(inum);
	bit_set(cg_inosused(mycg), inum);

#ifdef INTEL
	mycg->cg_cs.cs_nifree = DISK32(DISK32(mycg->cg_cs.cs_nifree) - 1);
	superblock->fs_cstotal.cs_nifree =
			DISK32(DISK32(superblock->fs_cstotal.cs_nifree) - 1);
	superblock->fs_cs(superblock, cgx).cs_nifree =
		DISK32(DISK32(superblock->fs_cs(superblock, cgx).cs_nifree) - 1);
	superblock->fs_fmod = DISK32(DISK32(superblock->fs_fmod) + 1);
#else
	mycg->cg_cs.cs_nifree--;
	superblock->fs_cstotal.cs_nifree--;
	superblock->fs_cs(superblock, cgx).cs_nifree--;
	superblock->fs_fmod++;
#endif

	if (type == I_DIR) {
#ifdef INTEL
		mycg->cg_cs.cs_ndir = DISK32(DISK32(mycg->cg_cs.cs_ndir) + 1);
		superblock->fs_cstotal.cs_ndir =
			DISK32(DISK32(superblock->fs_cstotal.cs_ndir) + 1);
		superblock->fs_cs(superblock, cgx).cs_ndir =
			DISK32(DISK32(superblock->fs_cs(superblock,
							cgx).cs_ndir) + 1);
#else
		mycg->cg_cs.cs_ndir++;
		superblock->fs_cstotal.cs_ndir++;
		superblock->fs_cs(superblock, cgx).cs_ndir++;
#endif
	}

	PRINT(("inew: %d\n", inum + cgx * DISK32(superblock->fs_ipg)));
	return(inum + cgx * DISK32(superblock->fs_ipg));
}


/* inum_sane()
 *	This routine will do the initial setup of a new inode.  The
 *	routine is normally called after a call to inum_new().
 *	The file will be given regular permissions and a creation
 *	timestamp.
 */
inum_sane(inum, type)
ULONG	inum;
int	type;
{
	time_t	timeval;
	struct	icommon *inode;
	int	index;

	inode = inode_modify(inum);

#ifndef FAST
	if (inode == NULL) {
		PRINT(("inum_sane(): bad buffer given from inode_modify\n"));
		return;
	}
#endif

	/* regular file, 0755 permissions */
	if (type == I_DIR) {
		inode->ic_mode		= DISK16(IFDIR | 0755);
		inode->ic_nlink		= DISK16(2);
	} else {
		inode->ic_mode		= DISK16(IFREG | 0755);
		inode->ic_nlink		= DISK16(1);
	}
	inode->ic_ouid		= 0;
	inode->ic_ouid		= 0;
	inode->ic_ngid		= 0;
	inode->ic_ngid		= 0;
	inode->ic_blocks	= 0;
	inode->ic_size.val[0]	= 0;
	inode->ic_size.val[1]	= 0;

#ifdef INTEL
	inode->ic_gen = DISK32(DISK32(inode->ic_gen) + 1);
#else
	inode->ic_gen++;
#endif

	/* get local time */
	time(&timeval);

	/* 2922 = clock birth difference in days, 86400 = seconds in a day */
	/* 18000 = clock birth difference remainder  5 * 60 * 60 */
	timeval += 2922 * 86400 - GMT * 3600;

	inode->ic_mtime = DISK32(timeval);
	inode->ic_atime = DISK32(timeval);
	inode->ic_ctime = DISK32(timeval);

	for (index = 0; index < NDADDR; index++)
		inode->ic_db[index] = 0;

	for (index = 0; index < NIADDR; index++)
		inode->ic_ib[index] = 0;
}


/* inum_free()
 *	This routine performs the action opposite of inum_new().
 *	It will return the specified inode to the pool of available
 *	inodes for its associated cylinder group.
 */
inum_free(inum)
ULONG inum;
{
	struct	icommon *inode;
	int	index;
	ULONG	cgx;
	struct	cg *mycg;
#ifdef INTEL
	ULONG	temp;
#endif

	inode = inode_modify(inum);

#ifndef FAST
	if (inode == NULL) {
		PRINT(("inum_free(): bad buffer given from inode_modify\n"));
		return;
	}
#endif

	cgx  = itog(superblock, inum);
	mycg = cache_cg_write(cgx);

	mycg->cg_irotor = DISK32(inum % DISK32(superblock->fs_ipg));

#ifdef INTEL
	mycg->cg_cs.cs_nifree = DISK32(DISK32(mycg->cg_cs.cs_nifree) + 1);
#else
	mycg->cg_cs.cs_nifree++;
#endif

	bit_clr(cg_inosused(mycg), DISK32(mycg->cg_irotor));

#ifdef INTEL
	temp = DISK32(superblock->fs_cstotal.cs_nifree) + 1;
	superblock->fs_cstotal.cs_nifree = DISK32(temp);
	temp = DISK32(superblock->fs_cs(superblock, cgx).cs_nifree) + 1;
	superblock->fs_cs(superblock, cgx).cs_nifree = DISK32(temp);
	temp = DISK32(superblock->fs_fmod) + 1;
	superblock->fs_fmod = DISK32(temp);

	if ((DISK16(inode->ic_mode) & IFMT) == IFDIR) {
		temp = DISK32(mycg->cg_cs.cs_ndir) - 1;
		mycg->cg_cs.cs_ndir = DISK32(temp);
		temp = DISK32(superblock->fs_cstotal.cs_ndir) - 1;
		superblock->fs_cstotal.cs_ndir = DISK32(temp);
		temp = DISK32(superblock->fs_cs(superblock, cgx).cs_ndir) - 1;
		superblock->fs_cs(superblock, cgx).cs_ndir = DISK32(temp);
	}
#else
	superblock->fs_cstotal.cs_nifree++;
	superblock->fs_cs(superblock, cgx).cs_nifree++;
	superblock->fs_fmod++;

	if ((DISK16(inode->ic_mode) & IFMT) == IFDIR) {
		mycg->cg_cs.cs_ndir--;
		superblock->fs_cstotal.cs_ndir--;
		superblock->fs_cs(superblock, cgx).cs_ndir--;
	}
#endif

	inode->ic_mode		= 0;
	inode->ic_nlink		= 0;
	inode->ic_ouid		= 0;
	inode->ic_ogid		= 0;
	inode->ic_nuid		= 0;
	inode->ic_ngid		= 0;
	inode->ic_blocks	= 0;
	inode->ic_size.val[0]	= 0;
	inode->ic_size.val[1]	= 0;
	inode->ic_mtime         = 0;
	inode->ic_mtime_ns      = 0;
	inode->ic_atime         = 0;
	inode->ic_atime_ns      = 0;
	inode->ic_ctime         = 0;
	inode->ic_ctime_ns      = 0;

	for (index = 0; index < NDADDR; index++)
		inode->ic_db[index] = 0;

	for (index = 0; index < NIADDR; index++)
		inode->ic_ib[index] = 0;
}


int is_writable(inum)
ULONG inum;
{
	struct icommon *inode;

	inode = inode_read(inum);
	return(DISK16(inode->ic_mode) & IWRITE);
}
#endif !RONLY

int is_readable(inum)
ULONG inum;
{
	struct icommon *inode;

	inode = inode_read(inum);
	return(DISK16(inode->ic_mode) & IREAD);
}

ULONG prev_frag_addr(file_frag_num, inode, inum)
ULONG	file_frag_num;
struct	icommon *inode;
ULONG	inum;
{
	ULONG	prev_frag;
	int	index;

	for (index = file_frag_num - 1; index >= 0; index--)
	    if (prev_frag = ridisk_frag(index, inode))
		break;

	if (prev_frag == 0) {
	    prev_frag = itod(superblock, inum);
	    PRINT(("no prev frag found, i=%d at blk %d\n", inum, prev_frag));
	}

	return(prev_frag);
}
