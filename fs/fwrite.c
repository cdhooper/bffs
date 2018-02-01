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

#include <clib/exec_protos.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>

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
#include "inode.h"
#include "ufs.h"

extern ULONG phys_sectorsize;	/* physical disk sector size, from env */

/*
 *       ** We may want to eliminate caching these blocks, since
 *	they are written entirely...Reducing the possibility we
 *	will actually need them in the future...and increasing the
 *	possibility of inconveniently flushing our cache for us.
*/
static int
file_ua_frags_write(ULONG sfrag, ULONG nfrags, char *buf, ULONG inum)
{
    int frag;
    int lfrag;

    for (frag = 0; frag < nfrags; frag++) {
	PRINT(("fufw: frag=%d maddr=%x\n", frag, buf));
	lfrag = widisk_frag(sfrag + frag, inum);
	if (lfrag == 0) {
		PRINT(("file_ua - no space left at frag=%d\n", frag));
		return(frag);
	} else {
		char *ptr = cache_frag_write(lfrag, 0);
#ifndef FAST
		if (ptr == NULL) {
		    PRINT2(("file_ua_frags_write: cache_write gave NULL\n"));
		    return(0);
		}
#endif
		if ((ULONG)buf & 0x3)
		    CopyMem(buf + frag * FSIZE, ptr, FSIZE);
		else
		    CopyMemQuick(buf + frag * FSIZE, ptr, FSIZE);
	}
    }
    return(frag);
}

static int
file_frags_write(ULONG sfrag, ULONG nfrags, char *buf, ULONG inum)
{
    int     frag;
    int     csfrag, cefrag, lfrag, bfrags;
    extern  ULONG tranmask;

    if (nfrags < 4) {
	for (frag = 0; frag < nfrags; frag++)  {
	    lfrag = widisk_frag(sfrag + frag, inum);
	    if (lfrag == 0) {
		PRINT(("file_fw - no space left at frag=%d\n", frag));
		return(frag);
	    } else {
		char *ptr = cache_frag_write(lfrag, 0);
#ifndef FAST
		if (ptr == NULL) {
		    PRINT2(("file_frags_write: cache_write gave NULL\n"));
		    return(0);
		}
#endif
		CopyMemQuick(buf + frag * FSIZE, ptr, FSIZE);
	    }
	    PRINT(("ffw: (nf<4) frag=%d lf=%x\n", frag, lfrag));
	}
	return(frag);
    }

    if (((ULONG) buf) & tranmask)
	return(file_ua_frags_write(sfrag, nfrags, buf, inum));


    lfrag = widisk_frag(sfrag, inum);

    if (lfrag)
        cache_invalidate(lfrag);

    PRINT(("ffw: gather %u frags (%u to %u)\n",
	   nfrags, sfrag, sfrag + nfrags - 1));

    frag = 0;
    while ((frag < nfrags) && (lfrag != 0)) {
	csfrag = lfrag;

	do {
	    cefrag = lfrag;
	    frag++;
	    if (frag < nfrags) {
		lfrag = widisk_frag(sfrag + frag, inum);
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

static int
partial_frag_write(ULONG frag, int offset, int size, char *buf, int readcache)
{
	char *ptr;
	PRINT(("pfw: frag=%d offset=%d size=%d\n", frag, offset, size));
	ptr = cache_frag_write(frag, readcache);
#ifndef FAST
	if (ptr == NULL) {
		PRINT2(("partial_frag_write: cache_write gave NULL\n"));
		return(0);
	}
#endif
	CopyMem(buf, ptr + offset, size);
	return(size);
}


/* file_write()
 *	o Given inode+sbyte+nbytes+buf, write file info from memory to disk
 *	There are four phases to this routine:
 *		0. If partial contained in frag, cache rwrite and return
 *		1. Cache rwrite tail of fs-frag (Sync up with fs-frag size)
 *		2. Write full frags
 *		3. Cache rwrite head of fs-frag
 * This function returns -1 on error; otherwise it returns the number of
 * bytes written.
 */
ULONG
file_write(ULONG inum, ULONG sbyte, ULONG nbytes, char *buf)
{
	struct	icommon *inode;
	ULONG	current;
	ULONG	spos, epos, sfrag, efrag;
	ULONG	nffrags, filesize;
	ULONG	faddr;
	ULONG	start;
	ULONG	blocks;
	ULONG	ablocks;
	int	read_last_frag = 1;

	inode = inode_read(inum);
#ifndef FAST
	if (inode == NULL) {
	    PRINT2(("filewrite: inode_read gave NULL\n"));
	    return (-1);
	}
#endif
	filesize = IC_SIZE(inode);

	/* expand allocation if file will grow larger than current size */
	if (sbyte + nbytes > filesize) {
		start  = filesize / FSIZE;

		PRINT(("fw expand: sbyte=%u nbytes=%u ic_size=%u\n",
		       sbyte, nbytes, filesize));
		blocks = (sbyte + nbytes + FBSIZE - 1 - filesize) / FBSIZE;

		PRINT(("fwrite: Expand %u (size=%u) at frag %u by %u blocks\n",
			inum, filesize, start, blocks));
		ablocks = file_blocks_add(inode, inum, start, blocks);

		if (ablocks == 0) {
			PRINT2(("fwrite: unable to write anything\n"));
			global.Res2 = ERROR_DISK_FULL;
			return(-1);
		}

		if (ablocks < blocks)
		    nbytes = (ULONG) ablocks * FBSIZE + filesize - sbyte;

		filesize = sbyte + nbytes;
		if (filesize > IC_SIZE(inode)) {
			inode = inode_modify(inum);
#ifndef FAST
			if (inode == NULL) {
			    PRINT2(("fwrite: inode_modify gave NULL\n"));
			    return(-1);
			}
#endif
			SET_IC_SIZE(inode, filesize);
			read_last_frag = 0;
		}
	} else if (sbyte + nbytes > filesize) {
		filesize = sbyte + nbytes;
		inode = inode_modify(inum);
#ifndef FAST
		if (inode == NULL) {
		    PRINT2(("fwrite: inode_modify gave NULL\n"));
		    return(-1);
		}
#endif
		SET_IC_SIZE(inode, filesize);
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
		faddr = widisk_frag(sfrag, inum);
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
		faddr = widisk_frag(sfrag, inum);
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
		faddr = widisk_frag(efrag, inum);
		if (faddr == 0) {
			PRINT(("fw:e: No more space left on device [ewrite]\n"));
			return(FSIZE - spos + nffrags * FSIZE);
		}
		PRINT(("end -- "));
		partial_frag_write(faddr, 0, epos, buf, read_last_frag);
	}

	return(nbytes);
}
#endif
