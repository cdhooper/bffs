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

#include <string.h>
#include <devices/trackdisk.h>
#include <clib/exec_protos.h>

#include "config.h"
#include "ufs.h"
#include "superblock.h"
#include "ufs/inode.h"
#include "fsmacros.h"
#include "file.h"
#include "handler.h"
#include "inode.h"
#include "cache.h"

/*
 *  ** We may want to eliminate caching these blocks, since
 *  they are read in their entirety..Reducing the possibility
 *  we will actually need them in the future...and increasing
 *  the possibility of inconveniently cycling our cache for us.
 */
static int
file_ua_frags_read(ULONG sfrag, ULONG nfrags, char *buf, int inum)
{
    int   frag;
    int   lfrag;
    char *fragbuf;
    char *current;

    current = buf;
    PRINT(("ua_frags_read %x\n", buf));
    for (frag = 0; frag < nfrags; frag++) {
	lfrag = ridisk_frag(sfrag + frag, inum);
	if (lfrag == 0) { /* we have a hole */
		memset(current, 0, FSIZE);
	} else {
		PRINT(("part f=%d c=%x\n", frag, current));
		fragbuf = cache_frag(lfrag);
#ifndef FAST
		if (fragbuf == NULL) {
			PRINT2(("file_us_frags_read broken inum=%d frag=%d lfrag=%d sfrag=%d\n", inum, frag, lfrag, sfrag));
			return (0);
		}
#endif
		CopyMemQuick(fragbuf, current, FSIZE);
	}
	current += FSIZE;
    }
    return(current - buf);
}


static int
file_frags_read(ULONG sfrag, ULONG nfrags, char *buf, int inum)
{
    register ULONG lfrag;
    register ULONG frag;
    register ULONG cefrag = 0;
    ULONG csfrag = 0;
    char  *bstart;
    char  *caddr;

    if (nfrags == 0)
	return(0);

    if ((nfrags < 4) || (((ULONG) buf) & tranmask))  /* cached reads */
	return(file_ua_frags_read(sfrag, nfrags, buf, inum));

    for (frag = 0; frag < nfrags; frag++) {
	lfrag = ridisk_frag(sfrag + frag, inum);
	if (lfrag == 0) {
	    if (csfrag) {
		if (data_read(bstart, csfrag,
			      FSIZE * (cefrag - csfrag + 1))) {
		    PRINT2(("** data read fault for file_frags_read\n"));
		    return (0);
		}
		csfrag = 0;
		cefrag = 0;
	    }
	    memset(buf + frag * FSIZE, 0, FSIZE);
	} else if (caddr = cache_available(lfrag)) {	/* if in cache */
	    if (csfrag) {
		if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1))) {
		    PRINT2(("** data read fault for file_frags_read\n"));
		    return (0);
		}
		csfrag = 0;
		cefrag = 0;
	    }
	    CopyMemQuick(caddr, buf + frag * FSIZE, FSIZE);
	} else if (lfrag == cefrag + 1) {
	    cefrag = lfrag;
	} else {
	    if (csfrag)
		if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1))) {
		    PRINT2(("** data read fault for file_frags_read\n"));
		    return (0);
		}
	    csfrag = lfrag;
	    cefrag = lfrag;
	    bstart = buf + frag * FSIZE;
	}
    }

    if (csfrag) {
	if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1))) {
	    PRINT2(("** data read fault for file_frags_read\n"));
	    return (0);
	}
    }


    PRINT(("ffr: cs=%d ce=%d csf=%d cef=%d maddr=%x\n", csfrag, cefrag,
	    (bstart - buf) / FSIZE, frag + sfrag, bstart));

    return(frag * FSIZE);
}

static int
partial_frag_read(ULONG frag, int offset, int size, char *buf)
{
	char *fragbuf;

	PRINT(("part f=%d o=%d s=%d b=%x\n", frag, offset, size, buf));
	if (fragbuf = cache_frag(frag)) {
		CopyMem(fragbuf + offset, buf, size);
		return(size);
	}
	return(0);
}

/* file_read()
 *	o Given inode+sbyte+nbytes+buf, read file info from disk to memory
 *	There are four phases to this routine:
 *		0. If partial contained in frag, read and return
 *		1. Read tail of fs-frag (Sync up with fs-frag size)
 *		2. Read full frags
 *		3. Read head of fs-frag
 */
ULONG
file_read(ULONG inum, ULONG sbyte, ULONG nbytes, char *buf)
{
	struct	icommon *inode;
	ULONG	current;
	ULONG	spos, epos, sfrag, efrag;
	ULONG	nffrags, filesize;

	inode = inode_read(inum);
#ifndef FAST
	if (inode == NULL) {
	    PRINT2(("fileread: inode_read gave NULL\n"));
	    return (-1);
	}
#endif
	filesize = IC_SIZE(inode);

	if (sbyte > filesize)
		return(-1);
	if ((nbytes + sbyte) > filesize)
		nbytes = (ULONG) filesize - sbyte;

	/* Read initial */
	spos	= fragoff(superblock, sbyte);
	epos	= fragoff(superblock, sbyte + nbytes);
	sfrag	= lfragno(superblock, sbyte);
	efrag	= lfragno(superblock, sbyte + nbytes);
	nffrags	= efrag - sfrag;

	PRINT(("sbyte=%d nbytes=%d start=%d:%d end=%d:%d psectoff=%d\n", sbyte,
		nbytes, sfrag, spos, efrag, epos, psectoffset));

	/* Handle the possibility that the entire read is in one frag */
	if (sfrag == efrag) {
		PRINT(("file part read: %d:%d to %d:%d\n",
			spos, sfrag, epos, efrag));

		return(partial_frag_read(ridisk_frag(sfrag, inum),
			spos, nbytes, buf));
	}

	/* Handle the odd bytes at the beginning */
	current = 0;
	if (spos != 0) {
		if (partial_frag_read(ridisk_frag(sfrag, inum), spos,
					FSIZE - spos, buf) == 0)
			return(0);
		sfrag++;
		nffrags--;
		buf += FSIZE - spos;
	}


	/* Read main file frags */
	if (nffrags > 0) {
		if (file_frags_read(sfrag, nffrags, buf, inum) == 0)
		    return (0);
		buf += nffrags * FSIZE;
	}

	/* Handle the ending bytes, if any */
	PRINT(("efrag=%d epos=%d\n", efrag, epos));
	if (epos) {
	    return(nbytes - epos +
		   partial_frag_read(ridisk_frag(efrag, inum), 0, epos, buf));
	}

	return(nbytes);
}
