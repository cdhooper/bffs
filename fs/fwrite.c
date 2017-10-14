#include <devices/trackdisk.h>

#include "config.h"

/* no need to go further if we are building the read only release */
#ifndef RONLY

#include "superblock.h"
#include "ufs/inode.h"
#include "fsmacros.h"
#include "file.h"
#include "alloc.h"
#include "cache.h"
#include "handler.h"
#include "dos30/dos.h"

extern ULONG phys_sectorsize;	/* physical disk sector size, from env */


/* file_write();
	o Given inode+sbyte+nbytes+buf, write file info from memory to disk
	There are four phases to this routine:
		0. If partial contained in frag, cache rwrite and return
		1. Cache rwrite tail of fs-frag (Sync up with fs-frag size)
		2. Write full frags
		3. Cache rwrite head of fs-frag
*/
ULONG file_write(inum, sbyte, nbytes, buf)
ULONG	inum;
ULONG	sbyte;
ULONG	nbytes;
char	*buf;
{
	struct	icommon *inode;
	ULONG	current;
	ULONG	spos, epos, sfrag, efrag;
	ULONG	nffrags, filesize;
	ULONG	faddr;
	int	read_last_frag = 1;
	int	start;
	int	blocks;
	int	ablocks;

	inode = inode_read(inum);
	filesize = IC_SIZE(inode);

	/* expand allocation if file will grow larger than current allocation */
	if (sbyte + nbytes > DISK32(inode->ic_blocks) * phys_sectorsize) {
		start  = DISK32(inode->ic_blocks) / NDSPF;
/*
		blocks = ((sbyte + nbytes + FBSIZE - 1) / phys_sectorsize -
			  DISK32(inode->ic_blocks)) / NDSPB;
*/

PRINT(("sbyte=%d nbytes=%d FBSIZE=%d ic_blocks=%d NDSPB=%d\n",
	sbyte, nbytes, FBSIZE, DISK32(inode->ic_blocks), NDSPB));
/*
		blocks = ((sbyte + nbytes + FBSIZE - 1) / FBSIZE -
			  DISK32(inode->ic_blocks)) / NDSPB;
*/
		blocks = (sbyte + nbytes + FBSIZE - 1 -
			  DISK32(inode->ic_blocks) * phys_sectorsize) / FBSIZE;

		PRINT(("fw: Expand %d (size=%d) at frag %d by %d blocks\n",
			inum, filesize, start, blocks));
		ablocks = file_blocks_add(inode, inum, start, blocks);

		if (ablocks == 0) {
			PRINT(("fwrite: unable to write anything!\n"));
			global.Res2 = ERROR_DISK_FULL;
			return(-1);
		}

		if (ablocks < blocks)
		    nbytes = (ULONG) ablocks * FBSIZE + filesize - sbyte;

		filesize = sbyte + nbytes;
		if (filesize > IC_SIZE(inode)) {
			inode = inode_modify(inum);
			IC_SETSIZE(inode, filesize);
			read_last_frag = 0;
		}
	} else if (sbyte + nbytes > filesize) {
		filesize = sbyte + nbytes;
		inode = inode_modify(inum);
		IC_SETSIZE(inode, filesize);
	}


	spos	= sbyte            % FSIZE;
	epos	= (sbyte + nbytes) % FSIZE;
	sfrag	= sbyte            / FSIZE;
	efrag	= (sbyte + nbytes) / FSIZE;
	nffrags	= efrag - sfrag;

	PRINT(("fw: sbyte=%d nbytes=%d start=%d:%d end=%d:%d\n",
		sbyte, nbytes, spos, sfrag, epos, efrag));

	/* Handle the possibility that the entire write is in one frag */
	if (sfrag == efrag) {
		PRINT(("pwrite: %d:%d to %d:%d\n", spos, sfrag, epos, efrag));
		inode = inode_read(inum);
		faddr = widisk_frag(sfrag, inode, inum);
		if (faddr == 0) {
			PRINT(("fwrite: No more space left on device\n"));
			global.Res2 = ERROR_DISK_FULL;
			return(-1);
		}

		/* do not preread frag from disk if nbytes == filesize */
		partial_frag_write(faddr, spos, epos - spos, buf,
				   nbytes != filesize);
		return(nbytes);
	}

	/* Handle the odd bytes at the beginning */
	current = 0;
	if (spos != 0) {
		inode = inode_read(inum);
		faddr = widisk_frag(sfrag, inode, inum);
		if (faddr == 0) {
			PRINT(("fw:s: No more space left on device\n"));
			global.Res2 = ERROR_DISK_FULL;
			return(-1);
		}
		PRINT(("start -- "));
		partial_frag_write(faddr, spos, FSIZE - spos, buf, 1);
		sfrag++;
		nffrags--;
		buf += FSIZE - spos;
	}

	/* Write main file frags */
	if (nffrags > 0) {
		faddr = file_frags_write(sfrag, nffrags, buf, inum);
		if (faddr != nffrags) {
			PRINT(("fw:m: No more space left on device\n"));
			return(FSIZE - spos + faddr * FSIZE);
		}
		buf += nffrags * FSIZE;
	}

	/* Handle the ending bytes, if any */
	if (epos) {
		inode = inode_read(inum);
		faddr = widisk_frag(efrag, inode, inum);
		if (faddr == 0) {
			PRINT(("fw:e: No more space left on device [ewrite]\n"));
			return(FSIZE - spos + nffrags * FSIZE);
		}
		PRINT(("end -- "));
		partial_frag_write(faddr, 0, epos, buf, read_last_frag);
	}

	return(nbytes);
}


int file_frags_write(sfrag, nfrags, buf, inum)
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
ULONG	inum;
{
    int     frag = 0;
    int     csfrag, cefrag, lfrag, bfrags;
    struct  icommon *inode;
    extern  ULONG tranmask;

    if (nfrags < 4) {
	while (frag < nfrags)  {
	    inode = inode_read(inum);
	    lfrag = widisk_frag(sfrag + frag, inode, inum);
	    if (lfrag == 0) {
		PRINT(("file_fw - no space left at frag=%d\n", frag));
		return(frag);
	    } else
		CopyMem(buf + frag * FSIZE, cache_frag_write(lfrag, 0), FSIZE);
	    PRINT(("ffw: (nf<4) frag=%d\n", frag));
	    frag++;
	}
	return(frag);
    }

    if (((ULONG) buf) & tranmask)
	return(file_ua_frags_write(sfrag, nfrags, buf, inum));


    inode = inode_read(inum);
    lfrag = widisk_frag(sfrag, inode, inum);

    if (lfrag)
        cache_invalidate(lfrag);

    PRINT(("ffw: gather frags=%d (%d to %d)\n",
	   nfrags, sfrag, sfrag + nfrags - 1));

    while ((frag < nfrags) && (lfrag != 0)) {
	csfrag = lfrag;
	inode = inode_read(inum);

	do {
	    cefrag = lfrag;
	    frag++;
	    if (frag < nfrags) {
		lfrag = widisk_frag(sfrag + frag, inode, inum);
		if (lfrag)
		    cache_invalidate(lfrag);
	    } else
		break;
	} while (lfrag == (cefrag + 1));

	bfrags = (cefrag - csfrag + 1);

	if (data_write(buf + (frag - bfrags) * FSIZE, csfrag, FSIZE * bfrags))
		PRINT2(("** data write fault for file_frags_write\n"));
    }

    return(frag);
}


/*
        ** We may want to eliminate caching these blocks, since
	they are written entirely...Reducing the possibility we
	will actually need them in the future...and increasing the
	possibility of inconveniently flushing our cache for us.
*/
int file_ua_frags_write(sfrag, nfrags, buf, inum)
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
ULONG	inum;
{
    int     frag = 0;
    int     lfrag;
    struct  icommon *inode;

    while (frag < nfrags)  {
	PRINT(("fufw: frag=%d maddr=%x\n", frag, buf));
	inode = inode_read(inum);
	lfrag = widisk_frag(sfrag + frag, inode, inum);
	if (lfrag == 0) {
		PRINT(("file_ua - no space left at frag=%d\n", frag));
		return(frag);
	} else
		CopyMem(buf + frag * FSIZE, cache_frag_write(lfrag, 0), FSIZE);
	frag++;
    }
    return(frag);
}

int partial_frag_write(frag, offset, size, buf, readcache)
ULONG	frag;
int	offset;
int	size;
char	*buf;
{
	char *ptr;
	PRINT(("pfw: frag=%d offset=%d size=%d\n", frag, offset, size));
	ptr = cache_frag_write(frag, readcache);
	if (ptr == NULL) {
		PRINT(("bad buffer (NULL) passed to partial_frag_write\n"));
		return(0);
	}
	CopyMem(buf, ptr + offset, size);
	return(size);
}

#endif
