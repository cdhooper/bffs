#include <exec/memory.h>

#include "debug.h"
#include "ufs.h"
#include "file.h"
#include "cache.h"
#include "alloc.h"


/* pfragshift = n where 2^n = num of ptrs in frag
 * fblkshift  = n where 2^n = num of frags in blk
 *
 * pfragmask = 2^pfragshift - 1
 * fblkmask  = 2^fblkshift - 1
 *
 * 4k blk, 1k frag: pfragshift=8; pfragmask=255; fblkshift=2; fblkmask=3;
 */

extern int GMT;
int   minfree = 0;
ULONG pfragshift = 8;
ULONG pfragmask  = 255;
ULONG fblkshift  = 2;
ULONG fblkmask   = 3;

ULONG fragno[NIADDR];
ULONG ptrnum[NIADDR];

/* idisk_frag()
 *	This routine will return the frag address of a block in a fs given
 *	the file block number (frag address) and inode pointer.  It will not
 *	allocate blocks for holes in a file; rather, it will return 0.
 */
ULONG idisk_frag(file_frag_num, inode)
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

	if ((ind = file_blk_num - NDADDR) < 0) {
		curblk = inode->ic_db[file_blk_num];
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

	curblk = inode->ic_ib[level - 1];
	if (curblk == 0)
		return(0);

	do {
		level--;
		temp = (ULONG *) cache_frag(curblk + fragno[level]);
#ifndef FAST
		if (temp == NULL)
			return(0);
#endif
		curblk = temp[ptrnum[level]];
		if (curblk == 0)
			return(0);
	} while (level > 0);

	endreturn:
	if (curblk == 0)
		return(0);
	else
		return(curblk + frac);
}


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

	inode = inode_read(inum);

	file_blk_num = file_frag_num / FRAGS_PER_BLK;
	frac = file_frag_num - file_blk_num * FRAGS_PER_BLK;

	if ((ind = file_blk_num - NDADDR) < 0) {
	    inode = inode_modify(inum);
/*
	    PRINT(("cidisk: d=%d from %d to %d\n",
		    file_blk_num, inode->ic_db[file_blk_num], newblk));
*/
	    inode->ic_db[file_blk_num] = newblk;
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

	curblk = inode->ic_ib[level - 1];
	if (curblk == 0) {
	    PRINT(("cidisk: alloc main index at inode l=%d\n", level));
	    curblk = block_allocate(itod(superblock, inum));
	    if (curblk == 0) {
		PRINT(("cidisk: failed to alloc index block\n"));
		return(1);
	    }
	    block_erase(curblk);
	    inode = inode_modify(inum); /* modify inode on disk */
	    inode->ic_ib[level - 1] = curblk;
	    inode->ic_blocks += NSPB(superblock);
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
		    PRINT(("cidisk: alloc index at %d\n", ocurblk));
		    temp = (ULONG *) cache_frag_write(ocurblk + fragno[level], 1);
#ifndef FAST
			if (temp == NULL)
				return(0);
#endif
		    curblk = block_allocate(ocurblk);
		    if (curblk == 0) {		  /* no free space */
			PRINT(("cidisk: No free space to alloc iblock\n"));
			return(1);
		    }
		    inode = inode_modify(inum);   /* modify inode on disk */
		    temp[ptrnum[level]] = curblk;
		    inode->ic_blocks   += NSPB(superblock);
		    block_erase(curblk);
		}
	    } else {	/* level = 0  (ptr to data block) */
		temp = (ULONG *) cache_frag_write(curblk + fragno[level], 1);
#ifndef FAST
		if (temp == NULL)
			return(0);
#endif
/*
		PRINT(("cidisk: update %d from %d to %d\n",
			curblk, temp[ptrnum[level]], newblk));
*/
		temp[ptrnum[level]] = newblk;
	    }
	} while (level > 0);

	return(0);
}


#define BNEEDED(fromwhere)						\
	    if (curblk == 0) {						\
	        curblk = block_allocate(itod(superblock, inum));	\
		if (curblk == 0) {					\
		    PRINT(("widisk: failed to alloc block\n"));		\
		    return(0);						\
		}							\
		inode = inode_modify(inum);				\
		inode->ic_blocks += NSPB(superblock);			\
		fromwhere = curblk;					\
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
	int	frac;

	file_blk_num = file_frag_num / FRAGS_PER_BLK;
	frac = file_frag_num - file_blk_num * FRAGS_PER_BLK;

	if ((ind = file_blk_num - NDADDR) < 0) {
	    curblk = inode->ic_db[file_blk_num];
	    BNEEDED(inode->ic_db[file_blk_num]);
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

	curblk = inode->ic_ib[level - 1];
	if (curblk == 0) {
	    BNEEDED(inode->ic_ib[level - 1]);
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
	    curblk = temp[ptrnum[level]];
	    if (curblk == 0) {
		temp = (ULONG *) cache_frag_write(ocurblk + fragno[level], 1);
#ifndef FAST
		if (temp == NULL)
		    return(0);
#endif
		BNEEDED(temp[ptrnum[level]]);
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
	ULONG	index2;
	ULONG	fsteps;
	ULONG	*temp;

	fsteps = FSIZE >> 2;
	for (index = 0; index < FRAGS_PER_BLK ; index++)
		ZeroMem(cache_frag_write(fsblock + index, 0), FSIZE);
}


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
	else
		return(NULL);
}


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
	if (parent > superblock->fs_ipg * superblock->fs_ncg) {
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
	    for (index = 0; index < superblock->fs_ncg; index++)
		if ((superblock->fs_cs(superblock, index).cs_nifree > ifree) &&
		    (superblock->fs_cs(superblock, index).cs_nbfree +
		     superblock->fs_cs(superblock, index).cs_nffree > 0) ) {
		    ifree = superblock->fs_cs(superblock, index).cs_nifree;
		    cgx   = index;
		}

	    if (ifree == 0) {
		PRINT(("INCON: No inodes available in filesystem\n"));
		return(0);
	    }

	    mycg = cache_cg(cgx);
	} else if (mycg->cg_cs.cs_nifree == 0) {
	    for (index = cgx; index < superblock->fs_ncg; index++)
		if (superblock->fs_cs(superblock, index).cs_nifree)
		    break;
	    if (index == superblock->fs_ncg)
		for (index = cgx - 1; index >= 0; index--)
		    if (superblock->fs_cs(superblock, index).cs_nifree)
			break;

	    if (index < 0) {
		PRINT(("INCON: No inodes available in filesystem\n"));
		return(0);
	    }

	    cgx  = index;
	    mycg = cache_cg(cgx);
	}

	if (mycg->cg_irotor < mycg->cg_niblk)
		inum = mycg->cg_irotor;

	if (inum == 0)
		inum = (cgx ? 0 : 3);

	stopat = inum;

	for (; inum < mycg->cg_niblk; inum++)
		if (bit_chk(cg_inosused(mycg), inum) == 0)
			goto found_clear;

	PRINT(("inew: none found in search from preferred to %d\n", mycg->cg_niblk));

	for (inum = (cgx ? 0 : 3); inum < stopat; inum++)
		if (bit_chk(cg_inosused(mycg), inum) == 0)
			goto found_clear;

	PRINT(("INCON: Unable to find free inode - SUMMARY LIED!\n"));
	return(0);

	found_clear:

	mycg = cache_cg_write(cgx);
	mycg->cg_irotor = inum;
	mycg->cg_cs.cs_nifree--;
	bit_set(cg_inosused(mycg), inum);

	superblock->fs_cstotal.cs_nifree--;
	superblock->fs_cs(superblock, cgx).cs_nifree--;
	superblock->fs_fmod++;

	if (type == I_DIR) {
		mycg->cg_cs.cs_ndir++;
		superblock->fs_cstotal.cs_ndir++;
		superblock->fs_cs(superblock, cgx).cs_ndir++;
	}

	PRINT(("inew: %d\n", inum + cgx * superblock->fs_ipg));
	return(inum + cgx * superblock->fs_ipg);
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
		inode->ic_mode		= IFDIR | 0755;
		inode->ic_nlink		= 2;
	} else {
		inode->ic_mode		= IFREG | 0755;
		inode->ic_nlink		= 1;
	}
	inode->ic_uid		= 0;
	inode->ic_gid		= 0;
	inode->ic_blocks	= 0;
	inode->ic_size.val[0]	= 0;
	inode->ic_size.val[1]	= 0;
	inode->ic_gen++;

	/* get local time */
	time(&timeval);

	/* 2922 = clock birth difference in days, 86400 = seconds in a day */
	/* 18000 = clock birth difference remainder  5 * 60 * 60 */
	timeval += 2922 * 86400 - GMT * 3600;

	inode->ic_mtime = timeval;
	inode->ic_atime = timeval;
	inode->ic_ctime = timeval;

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

	inode = inode_modify(inum);

#ifndef FAST
	if (inode == NULL) {
		PRINT(("inum_free(): bad buffer given from inode_modify\n"));
		return;
	}
#endif

	cgx  = itog(superblock, inum);
	mycg = cache_cg_write(cgx);

	mycg->cg_irotor = inum % superblock->fs_ipg;
	mycg->cg_cs.cs_nifree++;
	bit_clr(cg_inosused(mycg), mycg->cg_irotor);

	superblock->fs_cstotal.cs_nifree++;
	superblock->fs_cs(superblock, cgx).cs_nifree++;
	superblock->fs_fmod++;
	if ((inode->ic_mode & IFMT) == IFDIR) {
		mycg->cg_cs.cs_ndir--;
		superblock->fs_cstotal.cs_ndir--;
		superblock->fs_cs(superblock, cgx).cs_ndir--;
	}

	inode->ic_mode		= 0;
	inode->ic_nlink		= 0;
	inode->ic_uid		= 0;
	inode->ic_gid		= 0;
	inode->ic_blocks	= 0;
	inode->ic_size.val[0]	= 0;
	inode->ic_size.val[1]	= 0;
	inode->ic_mtime         = 0;
	inode->ic_atime         = 0;
	inode->ic_ctime         = 0;

	for (index = 0; index < NDADDR; index++)
		inode->ic_db[index] = 0;

	for (index = 0; index < NIADDR; index++)
		inode->ic_ib[index] = 0;
}
