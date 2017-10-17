#include <devices/trackdisk.h>

#include "config.h"
#include "ufs.h"
#include "superblock.h"
#include "ufs/inode.h"
#include "fsmacros.h"
#include "file.h"
#include "handler.h"

/* file_read();
	o Given inode+sbyte+nbytes+buf, read file info from disk to memory
	There are four phases to this routine:
		0. If partial contained in frag, read and return
		1. Read tail of fs-frag (Sync up with fs-frag size)
		2. Read full frags
		3. Read head of fs-frag
*/

char *cache_frag();
char *cache_available();


ULONG file_read(inum, sbyte, nbytes, buf)
ULONG	inum;
ULONG	sbyte;
ULONG	nbytes;
char	*buf;
{
	struct	icommon *inode;
	ULONG	current;
	ULONG	spos, epos, sfrag, efrag;
	ULONG	nffrags, filesize;

	inode = inode_read(inum);
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

	PRINT(("sbyte=%d nbytes=%d start=%d:%d end=%d:%d poff=%d\n", sbyte,
		nbytes, sfrag, spos, efrag, epos, poffset));

	/* Handle the possibility that the entire read is in one frag */
	if (sfrag == efrag) {


PRINT(("part read: %d:%d to %d:%d\n", spos, sfrag, epos, efrag));

		return(partial_frag_read(ridisk_frag(sfrag, inode),
			spos, nbytes, buf));
	}

	/* Handle the odd bytes at the beginning */
	current = 0;
	if (spos != 0) {
		if (partial_frag_read(ridisk_frag(sfrag, inode), spos,
					FSIZE - spos, buf) == 0)
			return(0);
		sfrag++;
		nffrags--;
		buf += FSIZE - spos;
	}


	/* Read main file frags */
	if (nffrags > 0) {
		file_frags_read(inode, sfrag, nffrags, buf, inum);
		buf += nffrags * FSIZE;
	}

	/* Handle the ending bytes, if any */
	PRINT(("efrag=%d epos=%d\n", efrag, epos));
	if (epos) {
	    inode = inode_read(inum);
	    return(nbytes - epos +
		   partial_frag_read(ridisk_frag(efrag, inode), 0, epos, buf));
	}

	return(nbytes);
}

int file_frags_read(inode, sfrag, nfrags, buf, inum)
struct	icommon *inode;
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
int	inum;
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
	return(file_ua_frags_read(inode, sfrag, nfrags, buf, inum));

    inode = inode_read(inum);
    for (frag = 0; frag < nfrags; frag++) {
	lfrag = ridisk_frag(sfrag + frag, inode);
	if ((lfrag = ridisk_frag(sfrag + frag, inode)) == 0) {
	    if (csfrag) {
		if (data_read(bstart, csfrag,
			      FSIZE * (cefrag - csfrag + 1)))
			PRINT2(("** data read fault for file_frags_read\n"));
		csfrag = 0;
		cefrag = 0;
	    }
	    ZeroMem(buf + frag * FSIZE, FSIZE);
	} else if (caddr = cache_available(lfrag)) {	/* if in cache */
	    if (csfrag) {
		if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1)))
			PRINT2(("** data read fault for file_frags_read\n"));
		csfrag = 0;
		cefrag = 0;
	    }
	    CopyMem(caddr, buf + frag * FSIZE, FSIZE);
	} else if (lfrag == cefrag + 1) {
	    cefrag = lfrag;
	} else {
	    if (csfrag)
		if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1)))
			PRINT2(("** data read fault for file_frags_read\n"));
	    inode = inode_read(inum);
	    csfrag = lfrag;
	    cefrag = lfrag;
	    bstart = buf + frag * FSIZE;
	}
    }

    if (csfrag)
	if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1)))
		PRINT2(("** data read fault for file_frags_read\n"));


PRINT(("ffr: cs=%d ce=%d csf=%d cef=%d maddr=%x\n", csfrag, cefrag,
	(bstart - buf) / FSIZE, frag + sfrag, bstart));

    return(frag * FSIZE);
}


/*
    ** We may want to eliminate caching these blocks, since
    they are read in their entirety..Reducing the possibility
    we will actually need them in the future...and increasing
    the possibility of inconveniently cycling our cache for us.
*/
int file_ua_frags_read(inode, sfrag, nfrags, buf, inum)
struct	icommon *inode;
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
int	inum;
{
    int frag = 0;
    int lfrag;
    char *fragbuf;
    char *current;

    current = buf;
    PRINT(("ua_frags_read %x\n", buf));
    while (frag < nfrags)  {
	lfrag = ridisk_frag(sfrag + frag, inode);
	if (lfrag == 0) /* we have a hole */
		ZeroMem(current, FSIZE);
	else {
		PRINT(("part f=%d c=%x\n", frag, current));
		if (fragbuf = cache_frag(lfrag))
			CopyMem(fragbuf, current, FSIZE);
		else {
			PRINT(("file_us_frags_read broken!\n"));
			break;
		}
		inode = inode_read(inum);
	}
	current += FSIZE;
	frag++;
    }
    return(current - buf);
}

ZeroMem(buf, len)
ULONG *buf;
ULONG len;
{
	ULONG *end;
	end = buf + (len >> 2);
	while (buf < end)
		*(buf++) = 0;
}

int partial_frag_read(frag, offset, size, buf)
ULONG	frag;
int	offset;
int	size;
char	*buf;
{
	char *fragbuf;

	PRINT(("part f=%d o=%d s=%d b=%x\n", frag, offset, size, buf));
	if (fragbuf = cache_frag(frag)) {
		CopyMem(fragbuf + offset, buf, size);
		return(size);
	}
	return(0);
}
