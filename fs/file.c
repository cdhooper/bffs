#include <exec/memory.h>
#include <dos30/dos.h>
#include <string.h>

#include "debug.h"
#include "handler.h"
#include "ufs.h"
#include "file.h"
#include "cache.h"
#include "alloc.h"
#include "sys/param.h"

#define MAX_PATH_LEVEL_LEN 8191
#define DIRNAMELEN 107


int	resolve_symlinks = 1;	/* 1=resolve sym links for AmigaDOS */
int	unixflag = 1;		/* 1=Use Unix pathnames 0=AmigaDOS pathnames */



/* file_find()
 *	This routine, given a parental inode number and pathname, will
 *	return the inode number of the basename within the path of the
 *	pathname.  If no file is found, the return inode number is zero.
 *
 *	This is a focal routine for the filesystem, as it is the main
 *	interface to the path resolution routines.
 */
int file_find(inum, name)
int inum;
char *name;
{
	int	finum;
	int	pinum;
	int	namelen;
	int	templen;
	char	*strend;
	char	*current;
	char	*filename;
	char	*next;
	char	*buf;
	struct	icommon	*inode;
	int	passes = 0;

	namelen = strlen(name) + 1;
	filename = (char *) AllocMem(namelen, MEMF_PUBLIC);
	if (filename == NULL) {
		PRINT(("file_find: unable to allocate %d bytes!\n", namelen));
		return(0);
	}
	strcpy(filename, name);

	finum = inum;
	current = filename;

	if (unixflag && (*current == '/')) {
		finum = 2;
		current++;
	}

	strend = current + strlen(current);
	if (current == strend)
		finum = file_name_find(finum, ".");

	while (current < strend) {
		next = nextslash(current);

		pinum = finum;
		if ((finum = file_name_find(pinum, current)) == NULL)
			break;

		inode = inode_read(finum);
		if ((inode->ic_mode & IFMT) == IFLNK) {  /* resolve sym link */
			if (!resolve_symlinks) {
				global.Res2 = ERROR_IS_SOFT_LINK;
				return(0);
			}
			templen	= IC_SIZE(inode) + strlen(next) + 1;
			if (templen > MAX_PATH_LEVEL_LEN) {
				global.Res2 = ERROR_IS_SOFT_LINK;
				return(0);
			}
			buf = (char *) AllocMem(templen, MEMF_PUBLIC);
			if (buf == NULL) /* error: ran into symlink and no memory */
				break;
			file_read(finum, 0, IC_SIZE(inode), buf);

			inode = inode_read(finum);
			strcpy(buf + IC_SIZE(inode) + 1, next);
			buf[IC_SIZE(inode)] = '/';
			FreeMem(filename, namelen);
			namelen	 = templen;
			filename = buf;

			/* transition back to root or parent directory */
			if (*buf == '/') {
				if (unixflag)
					finum = 2;
				else
					finum = file_name_find(pinum, "..");
				next = buf + 1;
			} else if (*buf == ':') {
				finum = 2;
				next = buf + 1;
			} else {
				next  = buf;  /* which will set current = buf */
				finum = pinum;
			}

			strend = filename + strlen(filename);

		} else if (inode->ic_mode & IFCHR)
			break;

		current = next;
		if (passes++ > 127) {
			PRINT(("file_find: Too many levels"));
			finum = 0;
			global.Res2 = ERROR_TOO_MANY_LEVELS;
			break;
		}
	}

	FreeMem(filename, namelen);
	return(finum);
}

char *nextslash(str)
char *str;
{
	while (*str != '\0')
		if (*str == '/') {
			*str = '\0';
			return(str + 1);
		} else
			str++;
	return(str);
}

int file_name_find(pinum, name)
ULONG	pinum;
char	*name;
{
	int inum;

	if (*name == '\0')
		if (unixflag)
			inum = pinum;
		else
			inum = dir_name_search(pinum, "..");
	else {
		if ((*name == '.') && (*(name + 1) == '\0'))
			inum = pinum;
		else
			inum = dir_name_search(pinum, name);
		global.Pinum = pinum;
	}

	if ((*name == '\0') || ((*name == '.') && ((*(name + 1) == '\0') ||
	     ((*(name + 1) == '.') && (*(name + 2) == '\0')))) ) {
		global.Pinum	= dir_name_search(inum, "..");
		global.Poffset	= dir_inum_search(global.Pinum, inum);
	}

	return(inum);
}

streqv(str1, str2)
char	*str1;
char	*str2;
{
	int count = 0;

	while (*str1 != '\0') {
		if ((*str1 != *str2) && ((*str1 | 32) != (*str2 | 32)))
			return(0);
		str1++;
		str2++;
		if (++count == DIRNAMELEN)
			return(0);
	}
	if (*str2 == '\0')
		return(1);
	else
		return(0);
}

/* old main loop
		if (*str1 != *str2) {
			ch1 = ((*str1 >= 'A') && (*str1 <= 'Z')) ?
				(*str1 | 32) : *str1;
			ch2 = ((*str2 >= 'A') && (*str2 <= 'Z')) ?
				(*str2 | 32) : *str2;
			if ((ch1 | 32) != (ch2 | 32))
				return(0);
		}
*/




/* file_deallocate()
	This routine will deallocate all blocks (including index blocks) for
	the file pointed to by the specified inode number.  The inode pointers
	will be cleared as the inode is deallocated.
*/
ULONG *level_buffer[4];

file_deallocate(inum)
ULONG inum;
{
	int	index;
	ULONG	tofrags;
	int	ffrags;
	struct	icommon *inode;
	int	block;

	inode = inode_modify(inum);

#ifndef FAST
	if (inode == NULL) {
		PRINT(("file_deallocate: bad buffer from inode_modify\n"));
		return(0);
	}
#endif

/*	PRINT(("file_deallocate %d\n", inum)); */

	tofrags = numfrags(superblock, fragroundup(superblock, IC_SIZE(inode)));

	/* if file is larger, allocation is granular only to entire fs blocks */
	if (tofrags / FRAGS_PER_BLK < NDADDR) {
		ffrags  = fragnum(superblock, tofrags);
		block   = idisk_frag(tofrags - ffrags, inode);

/*
		PRINT(("i=%d size=%d blocks=%d\n", inum, IC_SIZE(inode),
			inode->ic_blocks));
		PRINT(("tofrags=%d ffrags=%d block=%d\n", tofrags, ffrags, block));
*/

		inode = inode_modify(inum);
		if (ffrags && block) {
			inode->ic_db[tofrags / FRAGS_PER_BLK] = 0;
			frags_deallocate(block, ffrags);
		}
	}

	for (index = 0; index < NDADDR; index++)
		if (inode->ic_db[index]) {
/*			PRINT(("deleting block %d\n", inode->ic_db[index])); */
			block_deallocate(inode->ic_db[index]);
			inode->ic_db[index] = 0;
		}

	for (index = 0; index < NIADDR; index++) {
		level_buffer[index] = (ULONG *) AllocMem(FBSIZE, MEMF_PUBLIC);
		if (level_buffer[index] == NULL) {
			PRINT(("file_deallocate: Unable to allocate temp space\n"));
			for (index--; index >= 0; index--) {
				FreeMem(level_buffer[index], FBSIZE);
				level_buffer[index] = NULL;
			}
			return(0);
		}
		if (inode->ic_ib[index]) {
			iblock_deallocate(index, inode->ic_ib[index]);
			block_deallocate(inode->ic_ib[index]);
		}
	}

	for (index = 0; index < NIADDR; index++) {
		FreeMem(level_buffer[index], FBSIZE);
		level_buffer[index] = NULL;
		inode->ic_ib[index] = 0;
	}

	return(1);
}


iblock_deallocate(level, fragaddr)
int level;
ULONG fragaddr;
{
	int index;
	int deallocated = 0;

	PRINT(("iblock_deallocate: level=%d fragaddr=%d\n", level, fragaddr));
	for (index = 0; index < FRAGS_PER_BLK; index++)
		cache_frag_flush(fragaddr + index);

	if (data_read(level_buffer[level], fragaddr, FBSIZE))
		PRINT(("** iblock_deallocate: bad block read\n"));

	for (index = 0; index < NINDIR(superblock); index++)
	    if (level_buffer[level][index]) {
		if (level > 0)
		    deallocated += iblock_deallocate(level - 1,
						     level_buffer[level][index]);
		block_deallocate(level_buffer[level][index]);
		deallocated++;
	    }
	return(deallocated);
}


/* file_blocks_add()
	This routine will add more filesystem blocks to the allocation for
	the specified inode.  The number of filesystem blocks actually
	allocated will be returned to the calling routine.

	Policy is as follows:
	o  If first block in the file, allocate a block near the inode
	   in the inode's cg.
	o  If direct block in the inode, allocate closest to the
	   previous block.
	o  If first indirect block in the inode, allocate in another cg.
	o  If indirect block in the inode, allocate closest to the
	   previous indirect block.

	NEEDS WORK to complete the policy....should allocate from another
	cg (one which has the fewest blocks allocated) if stepping across
	the line from direct blocks to first indirect index block.
*/
file_blocks_add(inode, inum, startfrag, blocks)
struct	icommon *inode;		/* pointer to inode */
ULONG	inum;			/* inode number */
ULONG	startfrag;		/* starting frag address of block */
ULONG	blocks;			/* number of filesystem blocks to allocate */
{
	int	index;
	ULONG	prev_frag = 0;
	int	cgx;
	ULONG	blockgot;
	struct	cg *mycg;

/*	PRINT(("file_blocks_add i=%d start=%d blocks=%d\n",
		inum, startfrag, blocks)); */

	if (startfrag)
		for (index = startfrag - 1; index >= 0; index--)
			if (prev_frag = idisk_frag(index, inode))
				break;

	if (prev_frag == 0)
		prev_frag = itod(superblock, inum);

	cgx  = dtog(superblock, prev_frag);
	mycg = cache_cg_write(cgx);
	for (index = 0; index < blocks; index++) {
		/* check already alloced */
		if (idisk_frag(startfrag + index * FRAGS_PER_BLK, inode))
			continue;
		blockgot = block_allocate(prev_frag);
		if (blockgot == 0) {
			PRINT(("file_blocks_add: failed to allocate all requested blocks\n"));
			break;
		}
		inode = inode_modify(inum);
		inode->ic_blocks += NSPB(superblock);
		cidisk_frag(startfrag + index * FRAGS_PER_BLK, inum, blockgot);
		prev_frag = blockgot;
	}
	return(index);
}


/* file_block_extend()
	This routine will, given an inode, assure the last block is allocated
	as a full block.  This simplifies and speeds the allocation policies.
	If the machine crashes before the file is closed, fsck must be run to
	recover space.

	This scheme of allocation is bad if the filesystem must rapidly open
	and close files, due to the copying involved in moving blocks.  A
	sufficient cache will greatly alleviate the overhead because it can
	dynamically remap blocks before they are written to media.

	This is not all that bad.  The number of blocks allocated to the file
	will remain high after a crash, but the next time the file is opened
	and closed for write, it will be cleaned up automatically.

	If the number of frags allocated to the file is an even multiple of
	the number of blocks (meaning no frags), then nothing will be done
	to the file.
*/
file_block_extend(inum)
ULONG	inum;
{
	struct	icommon *inode;
	int	index;
	ULONG	cgx;
	ULONG	dblk;
	ULONG	fblk;
	ULONG	fspill;
	ULONG	nblk;
	struct	cg *mycg;

	inode	= inode_read(inum);
	fspill	= (inode->ic_blocks / NSPF(superblock)) % FRAGS_PER_BLK;

PRINT(("fbe: %d blks=%d fspill=%d\n", inum, inode->ic_blocks, fspill));

	if (fspill == 0)
		return;		/* block is already full size */

	fblk = inode->ic_blocks / NSPF(superblock) - fspill;

	dblk	= idisk_frag(fblk, inode);
	cgx	= dtog(superblock, dblk);
	mycg	= cache_cg(cgx);


	PRINT(("fbe: last file block=%d, frag_addr=%d, fspill=%d; cg=%d\n",
		fblk, dblk, fspill, cgx));


	if (((dblk % FRAGS_PER_BLK) == 0) &&	/* starting on frag boundary? */
	     rest_is_free(cg_blksfree(mycg), dtogd(superblock, dblk))) {
		PRINT(("%d in use, allocating %d more in block %d\n",
			fspill, FRAGS_PER_BLK - fspill, dblk));
		frags_allocate(dblk, FRAGS_PER_BLK - fspill);
	} else {	/* we must move (fspill) fragments to a new block */
		if (fblk)
			nblk = block_allocate(idisk_frag(fblk - 1, inode));
		else
			nblk = block_allocate(itod(superblock, inum));
		if (nblk == 0) {
			PRINT(("Unable to allocate space for WRITE!\n"));
			return;
		}
		for (index = 0; index < fspill; index++)
			cache_frag_move(nblk + index, dblk + index);
		/* point inode at new spot */
		cidisk_frag(fblk, inum, nblk);
		frags_deallocate(dblk, fspill);
	}

	inode = inode_modify(inum);
	inode->ic_blocks += ((FRAGS_PER_BLK - fspill) * NSPF(superblock));
}


extern ULONG fragno[];
extern ULONG ptrnum[];

/* file_blocks_deallocate()
 *	This routine will deallocate file blocks starting at the
 *	specified BLOCK address.  WARNING: This routine does not
 *	handle odd blocks at the end of a file.
 */
file_blocks_deallocate(inum)
ULONG inum;
{
	int	index;
	int	level;
	struct	icommon *inode;
	ULONG	blkstart;
	ULONG	*temp;
	ULONG	fragcur;
	ULONG	ptrcur;
	ULONG	deallocated = 0;
	ULONG	curblk;
	ULONG	curdelblk;
	ULONG	startblk;

	inode = inode_modify(inum);
	startblk = (IC_SIZE(inode) + FBSIZE - 1) / FBSIZE;

	for (blkstart = startblk; blkstart < NDADDR; blkstart++) {
	    if (inode->ic_db[blkstart]) {
		PRINT(("removing dir %d addr %d\n",
			blkstart, inode->ic_db[blkstart]));
		block_deallocate(inode->ic_db[blkstart]);
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

	PRINT(("now we have our start: l=%d\n", level));

	for (index = 0; index < NIADDR; index++) {
	    level_buffer[index] = (ULONG *) AllocMem(FBSIZE, MEMF_PUBLIC);
	    if (level_buffer[index] == NULL) {
		PRINT(("fbd: Unable to allocate temp space\n"));
		for (index--; index >= 0; index--) {
		    FreeMem(level_buffer[index], FBSIZE);
		    level_buffer[index] = NULL;
		}
		return(0);
	    }
	}

	/* if we start deallocating in direct blocks, just deallocate
	   all of the trees */
	if (startblk < NDADDR) {
	    level = 0;
	    goto deallocate_trees;
	}

	/* algorithm: seek last file block, along the way deallocating
	   other trees which are higher than the current index.  At last
	   file block, deallocate all blocks in that level greater than
	   that file block position.  iblock_deallocate routine will
	   record the number of blocks actually deallocated for us */

	curblk = inode->ic_ib[level - 1];
	if (curblk == 0)
	    goto deallocate_trees;

	index = level;
	do {
	    index--;

	    ptrcur = ptrnum[index] + 1;
	    for (fragcur = fragno[index]; fragcur < FRAGS_PER_BLK; fragcur++) {
		temp = (ULONG *) cache_frag_write(curblk + fragcur, 1);
		for (; ptrcur < NINDIR(superblock) / FRAGS_PER_BLK; ptrcur++) {
		    if (temp[ptrcur]) {
			curdelblk = temp[ptrcur];
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
	    if (temp == NULL)
		return(0);
#endif
	    curblk  = temp[ptrnum[index]];
	    if (curblk == 0)
		goto deallocate_trees;

	} while (index > 0);

	block_deallocate(curblk);
	temp[ptrnum[index]] = 0;
	deallocated++;

	deallocate_trees:	/* deallocate other inode trees */

	PRINT(("dealloc trees, start=%d\n", level));
	for (index = level; index < NIADDR; index++)
	    if (inode->ic_ib[index]) {
		deallocated += iblock_deallocate(index, inode->ic_ib[index]);
		block_deallocate(inode->ic_ib[index]);
		deallocated++;
		inode->ic_ib[index] = 0;
	    }

	for (index = 0; index < NIADDR; index++) {
	    FreeMem(level_buffer[index], FBSIZE);
	    level_buffer[index] = NULL;
	}

	inode = inode_modify(inum);
	inode->ic_blocks -= (NSPB(superblock) * deallocated);

	PRINT(("deallocated final=%d fblks=%d\n", deallocated, inode->ic_blocks));
}

/* file_block_retract()
 *	This routine will retract retract the size of the file to the size
 *	specified in the inode.
 *	o  The file must have its last block completely allocated before calling
 *	o  The amount of space to deallocate must exist within one filesystem
 *		block.  You should call file_blocks_deallocate() if you wish to
 *		deal with more than just the last file block.
 *	o  This routine automatically relocated partial blocks as necessary.
 *	o  If the size of the file exceeds that which can be recorded solely
 *		by the inode direct addresses, then this trimming is not done.
 *		This is per the apparent (read: undocumented) BSD FFS (and fsck)
 *		specification.
 */
file_block_retract(inum)
int	inum;
{
	ULONG	fsize;
	ULONG	ffrags;
	ULONG	tofrags;
	ULONG	dblk;
	ULONG	fdblk;
	ULONG	nblk;
	ULONG	pblk;
	ULONG	ndblk;
	ULONG	nblkfrags;
	int	index;
	int	cgx;
	int	level;
	struct	cg *mycg;
	struct	icommon *inode;


	PRINT(("file_block_retract(): i=%d\n", inum));
	inode	= inode_modify(inum);
	fsize	= IC_SIZE(inode);
	tofrags	= numfrags(superblock, fragroundup(superblock, fsize));
	ffrags	= fragnum(superblock, tofrags);

	PRINT(("fsize=%d total=%d frag_spill=%d\n", fsize, tofrags, ffrags));

	if ((ffrags == 0) || (tofrags > FRAGS_PER_BLK * NDADDR))
	    return;

	inode	= inode_modify(inum);
	inode->ic_blocks -= (NSPF(superblock) * (FRAGS_PER_BLK - ffrags));

	fdblk	= idisk_frag(blknum(superblock, tofrags), inode);
	cgx	= dtog(superblock, fdblk);
	mycg	= cache_cg_write(cgx);
	dblk	= dtogd(superblock, fdblk);
	if (tofrags < FRAGS_PER_BLK)
	    pblk = dtogd(superblock, itod(superblock, inum));
	else
	    pblk = dtogd(superblock, idisk_frag(fdblk - FRAGS_PER_BLK, inode));

/*
	PRINT(("inum=%d cg=%d d=%d p=%d\n", inum, cgx, dblk, pblk));
	PRINT(("attempting to relocate partial block size=%d\n", ffrags));
*/

	nblk = block_fragblock_find(mycg, pblk, ffrags);
	if (nblk != -1) {
	    PRINT(("will realloc to block %d\n", nblk));

	    nblkfrags	= nblk % FRAGS_PER_BLK;
	    ndblk	= nblk + cgbase(superblock, cgx);

	    /* if found = full block, leave data where it is, dealloc extra */
	    if ((nblkfrags == 0) && (block_avail(cg_blksfree(mycg), nblk))) {
		PRINT(("got full block, don't want it\n"));
		goto dealloc_extra;
	    }

	    if (ndblk - nblkfrags == fdblk - fdblk % FRAGS_PER_BLK) {
		/* should not happen because entire block should be
		   allocated to file at this point */
		PRINT(("INCON: got same block %d\n", ndblk - nblkfrags));
		goto dealloc_extra;
	    }

	    /* allocate frags of that new block */
	    frags_allocate(ndblk, ffrags);

	    /* do cache block moves */
	    for (index = 0; index < ffrags; index++)
		cache_frag_move(ndblk + index, fdblk + index);

	    /* free old block */
	    block_deallocate(fdblk);

	    /* point inode at new location */
	    cidisk_frag(tofrags - ffrags, inum, ndblk);

	    return;
	}

	dealloc_extra:
/*	PRINT(("freeing frags in %d, ic_blocks=%d\n", dblk, inode->ic_blocks)); */

	frags_deallocate(fdblk + ffrags, FRAGS_PER_BLK - ffrags);
}

/* frag_expand()
	This routine will use the allocation policies to expand the specified
	inum by one fragment.  This routine is called by the directory routines
	to give more directory space.  This routine returns the newly allocated
	frag address (or 0 if it was not possible to allocate an address)

	Policy:
	o  If next frag is free and does not spill into another block, then
	   alloc and return that frag
	o  If last block is full block (of just this inode's data), allocate
	   just one frag for new address
	o  Is next frag available in the current block?
	   This implies that there _is_ a next frag in the current block.
	   o  If so, then allocate it and set file size - done
	   o  If not, then find/allocate new fragments (size in old block + 1)
	      and move old frags plus new frag to this new location.
	      Deallocate frags used of old location
*/
ULONG frag_expand(inum)
ULONG inum;
{
	int	index;
	ULONG	cgx;
	ULONG	dblk;
	ULONG	fblk;
	ULONG	nblk;
	ULONG	fspill;
	ULONG	startoff;
	ULONG	cgblk;
	ULONG	newblk;
	int	count;
	struct	cg *mycg;
	struct	icommon *inode;

/*	PRINT(("frag expand: inum=%d\n", inum)); */

	inode	= inode_modify(inum);
	fspill	= (inode->ic_blocks / NSPF(superblock)) % FRAGS_PER_BLK;
	fblk	=  inode->ic_blocks / NSPF(superblock) - fspill;
	dblk	= idisk_frag(fblk, inode);
	cgx	= dtog(superblock, dblk);
	mycg	= cache_cg_write(cgx);

/*	PRINT(("frgexp: i=%d size=%d last=%d spill=%d diskaddr=%d cg=%d\n", inum,
		IC_SIZE(inode), fblk, fspill, dblk, cgx)); */

	startoff = dblk % FRAGS_PER_BLK;
	cgblk	 = dblk - cgbase(superblock, cgx);

	if (fspill == 0) {
	    /* need to alloc one new frag in another block */
	    if (fblk)
		count = block_fragblock_find(mycg, cgblk, 1);
	    else
		count = block_fragblock_find(mycg, itod(superblock, inum) -
					      cgbase(superblock, cgx), 1);

	    if (count != -1) {			/* found frag */
		newblk = count + cgbase(superblock, cgx);
		frags_allocate(newblk, 1);
	    } else {				/* alloc block elsewhere */
		if (dblk)
		    newblk = block_allocate(dblk);
		else
		    newblk = block_allocate(itod(superblock, inum));
		if (newblk)
		    frags_deallocate(newblk + 1, FRAGS_PER_BLK - 1);
	    }

	    if (newblk) {
		inode = inode_modify(inum);
		inode->ic_blocks      += NSPF(superblock);
		IC_INCSIZE(inode, FSIZE);

		if (fblk)
		    cidisk_frag(fblk, inum, newblk);
		else
		    cidisk_frag(0, inum, newblk);
	    } else
		PRINT(("unable to allocate block - no space free!\n"));

	    return(newblk);

	/* possibly allocate fragment in existing block */
	} else if (startoff + fspill < FRAGS_PER_BLK)  {
	    PRINT(("Looking for more space in current block\n"));
	    if (bit_val(cg_blksfree(mycg), cgblk + fspill)) {
		frags_allocate(dblk + fspill, 1);

		inode = inode_modify(inum);
		inode->ic_blocks      += NSPF(superblock);
		IC_INCSIZE(inode, FSIZE);

		return(dblk + fspill);
	    }
	}

	/* situation: fspill != 0, but the block is full.  This means that
			file is sharing space with other files in same block
	   solution:  allocate frags + 1 and copy contents of old to the new
			place.  free the space allocated in the old block */

#ifndef FAST
	if (dblk == 0) {
		PRINT(("** error, dblk was 0, but trying to move frags!\n"));
		return(0);
	}
#endif

	if (fspill + 1 == FRAGS_PER_BLK)
	    count = -1;
	else
	    count = block_fragblock_find(mycg, cgblk, fspill + 1);

	if (count != -1) {				/* found frag */
	    newblk = count + cgbase(superblock, cgx);
	    frags_allocate(newblk, fspill + 1);
	} else {				/* alloc block elsewhere */
	    newblk = block_allocate(dblk);
	    if (newblk)
		frags_deallocate(newblk + fspill + 1, FRAGS_PER_BLK - fspill - 1);
	    else
		PRINT(("failed to allocate a new block - no space\n"));
	}

	if (newblk) {
		inode = inode_modify(inum);
		inode->ic_blocks      += NSPF(superblock);
		IC_INCSIZE(inode, FSIZE);

		for (index = 0; index < fspill; index++)
			cache_frag_move(newblk + index, dblk + index);

		frags_deallocate(dblk, fspill);
		cidisk_frag(fblk, inum, newblk);
	} else {
		PRINT(("unable to allocate block - no space free\n"));
		return(0);
	}

	return(newblk + fspill);
}
