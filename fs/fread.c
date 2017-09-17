#include <devices/trackdisk.h>

#include "debug.h"
#include "ufs.h"
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

ULONG file_read(inum, sbyte, nbytes, buf)
ULONG	inum;
ULONG	sbyte;
ULONG	nbytes;
char	*buf;
{
	struct	icommon *inode;
	ULONG	current;
	ULONG	spos, epos, sfrag, efrag;
	ULONG	nffrags, fsize;

	inode = inode_read(inum);
	fsize = IC_SIZE(inode);

	if (sbyte > fsize)
		return(-1);
	if ((nbytes + sbyte) > fsize)
		nbytes = (ULONG) fsize - sbyte;

	/* Read initial */
	spos	= fragoff(superblock, sbyte);
	epos	= fragoff(superblock, sbyte + nbytes);
	sfrag	= lfragno(superblock, sbyte);
	efrag	= lfragno(superblock, sbyte + nbytes);
	nffrags	= efrag - sfrag;

/*
	PRINT(("sbyte=%d nbytes=%d start=%d:%d end=%d:%d poff=%d\n", sbyte,
		nbytes, sfrag, spos, efrag, epos, poffset));
*/

	/* Handle the possibility that the entire read is in one frag */
	if (sfrag == efrag) {

/*
PRINT(("part read: %d:%d to %d:%d\n", spos, sfrag, epos, efrag));
*/

		partial_frag_read(idisk_frag(sfrag, inode), spos, epos - spos, buf);
		return(nbytes);
	}

	/* Handle the odd bytes at the beginning */
	current = 0;
	if (spos != 0) {
		partial_frag_read(idisk_frag(sfrag, inode), spos, FSIZE - spos, buf);
		sfrag++;
		nffrags--;
		buf += FSIZE - spos;
	}

	/* Read main file frags */
	if (nffrags > 0) {
		file_frags_read(inode, sfrag, nffrags, buf);
		buf += nffrags * FSIZE;
	}

	/* Handle the ending bytes, if any */
	if (epos)
		partial_frag_read(idisk_frag(efrag, inode), 0, epos, buf);

	return(nbytes);
}

int file_frags_read(inode, sfrag, nfrags, buf)
struct	icommon *inode;
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
{
    ULONG frag = 0;
    ULONG csfrag, cefrag, lfrag;
    char  *bstart;

    if (((ULONG) buf) & tranmask)
	return(file_ua_frags_read(inode, sfrag, nfrags, buf));

    if (nfrags == 0)
	return(0);

    lfrag = idisk_frag(sfrag + frag, inode);
    if (lfrag)
	cache_frag_flush(lfrag);

    while (frag < nfrags)  {
	csfrag = lfrag;
	cefrag = lfrag;
	bstart = buf + frag * FSIZE;

	frag++;
	while (frag < nfrags) {
	    for (lfrag = 0; lfrag == 0; frag++)
		if ((lfrag = idisk_frag(sfrag + frag, inode)) == 0)
			ZeroMem(buf + frag * FSIZE, FSIZE);
		else
			cache_frag_flush(lfrag);

	    if (lfrag == cefrag + 1)
		cefrag = lfrag;
	    else {
		frag--;
		break;
	    }
	}
	if (data_read(bstart, csfrag, FSIZE * (cefrag - csfrag + 1)))
		PRINT(("** data read fault for file_frags_read\n"));

/*
PRINT(("ffr: cs=%d ce=%d csf=%d cef=%d maddr=%x\n", csfrag, cefrag,
	(bstart - buf) / FSIZE, frag + sfrag, bstart));
*/
    }

    return(frag * FSIZE);
}


/*
        ** We may want to eliminate caching these blocks, since
	they are read in their entirety..Reducing the possibility
	we will actually need them in the future...and increasing
	the possibility of conveniently flushing our cache for us.
*/
int file_ua_frags_read(inode, sfrag, nfrags, buf)
struct	icommon *inode;
ULONG	sfrag;
ULONG	nfrags;
char	*buf;
{
    int frag = 0;
    int lfrag;

    while (frag < nfrags)  {
	lfrag = idisk_frag(sfrag + frag, inode);
	if (lfrag == 0) /* we have a hole */
		ZeroMem(buf + frag * FSIZE, FSIZE);
	else
		CopyMem(cache_frag(lfrag), buf + frag * FSIZE, FSIZE);
	frag++;
    }
    return(frag * FSIZE);
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
	CopyMem(cache_frag(frag) + offset, buf, size);
	return(size);
}
