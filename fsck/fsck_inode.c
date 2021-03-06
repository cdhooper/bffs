/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
/*static char sccsid[] = "from: @(#)inode.c	8.4 (Berkeley) 4/18/94";*/
static char *rcsid = "$Id: inode.c,v 1.10 1994/06/14 22:50:40 mycroft Exp $";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ufs/dinode.h>
#include <ufs/dir.h>
#include <ufs/fs.h>
#ifdef unix
#ifndef SMALL
#include <pwd.h>
#endif
#else
#include <sys/stat.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "fsck.h"

static ino_t startinum;

ckinode(dp, idesc)
	struct dinode *dp;
	register struct inodesc *idesc;
{
	register daddr_t *ap;
	long ret, n, ndb, offset;
	struct dinode *dino;
	unsigned long remsize, sizepb;
	mode_t mode;

	if (idesc->id_fix != IGNORE)
		idesc->id_fix = DONTKNOW;
	idesc->id_entryno = 0;
#ifdef cdh
	idesc->id_filesize = dp->di_qsize;
#else
	idesc->id_filesize.val[0] = dp->di_size.val[0];
	idesc->id_filesize.val[1] = dp->di_size.val[1];
#endif
	mode = dp->di_mode & IFMT;
	if (mode == IFBLK || mode == IFCHR || (mode == IFLNK &&
#ifdef cdh
	    (dp->di_size < sblock.fs_maxsymlinklen ||
#else
	    (dp->di_size.val[1] < sblock.fs_maxsymlinklen ||
#endif
	     (sblock.fs_maxsymlinklen == 0 && dp->di_blocks == 0))))
		return (KEEPON);

	dino = (struct dinode *) malloc(sizeof(struct dinode));
        if (dino == NULL)
                errexit("unable to allocate memory\n");

/*	dino = *dp; */
	bcopy(dp, dino, sizeof(struct dinode));

	ndb = howmany(dino->di_size, sblock.fs_bsize);
	for (ap = &dino->di_db[0]; ap < &dino->di_db[NDADDR]; ap++) {
		if ((--ndb == 0) &&
		    ((offset = blkoff(&sblock, dino->di_size)) != 0))
			idesc->id_numfrags =
				numfrags(&sblock, fragroundup(&sblock, offset));
		else
			idesc->id_numfrags = sblock.fs_frag;
		if (*ap == 0)
			continue;
		idesc->id_blkno = *ap;
		if (idesc->id_type == ADDR)
			ret = (*idesc->id_func)(idesc);
		else
			ret = dirscan(idesc);
		if (ret & STOP) {
			free(dino);
			return (ret);
		}
	}
	idesc->id_numfrags = sblock.fs_frag;
	remsize = dino->di_size - sblock.fs_bsize * NDADDR;
	sizepb = sblock.fs_bsize;
	for (ap = &dino->di_ib[0], n = 1; n <= NIADDR; ap++, n++) {
		if (*ap) {
			idesc->id_blkno = *ap;
			ret = iblock(idesc, n, remsize);
			if (ret & STOP) {
				free(dino);
				return (ret);
			}
		}
		sizepb *= NINDIR(&sblock);
		remsize -= sizepb;
	}
	free(dino);
	return (KEEPON);
}

iblock(idesc, ilevel, isize)
	struct inodesc *idesc;
	long ilevel;
	long isize;
{
	register daddr_t *ap;
	register daddr_t *aplim;
	register struct bufarea *bp;
	int i, n, (*func)(void *), nif;
	unsigned long sizepb;
	extern int dirscan(), pass1check(struct inodesc *);
	char *buf;

	if (idesc->id_type == ADDR) {
		func = idesc->id_func;
		if (((n = (*func)(idesc)) & KEEPON) == 0)
			return (n);
	} else
		func = dirscan;
	if (chkrange(idesc->id_blkno, idesc->id_numfrags))
		return (SKIP);
	bp = getdatablk(idesc->id_blkno, sblock.fs_bsize);
	ilevel--;
	for (sizepb = sblock.fs_bsize, i = 0; i < ilevel; i++)
		sizepb *= NINDIR(&sblock);
	nif = howmany(isize, sizepb);
	if (nif > NINDIR(&sblock))
		nif = NINDIR(&sblock);

	buf = (char *) malloc(BUFSIZ);
        if (buf == NULL)
                errexit("unable to allocate memory\n");

	if ((idesc->id_func == pass1check) && (nif < NINDIR(&sblock))) {
		aplim = &bp->b_un.b_indir[NINDIR(&sblock)];
		for (ap = &bp->b_un.b_indir[nif]; ap < aplim; ap++) {
			if (*ap == 0)
				continue;
			(void)sprintf(buf, "PARTIALLY TRUNCATED INODE I=%lu",
				idesc->id_number);
			if (dofix(idesc, buf)) {
				*ap = 0;
				dirty(bp);
			}
		}
		flush(fswritefd, bp);
	}

	free(buf);

	aplim = &bp->b_un.b_indir[nif];
	for (ap = bp->b_un.b_indir; ap < aplim; ap++) {
		if (*ap) {
			idesc->id_blkno = *ap;
			if (ilevel == 0)
				n = (*func)(idesc);
			else
				n = iblock(idesc, ilevel, isize);
			if (n & STOP) {
				bp->b_flags &= ~B_INUSE;
				return (n);
			}
		}
		isize -= sizepb;
	}
	bp->b_flags &= ~B_INUSE;
	return (KEEPON);
}

/*
 * Check that a block in a legal block number.
 * Return 0 if in range, 1 if out of range.
 */
chkrange(blk, cnt)
	daddr_t blk;
	int cnt;
{
	register int c;

	if ((unsigned)(blk + cnt) > maxfsblock)
		return (1);
	c = dtog(&sblock, blk);
	if (blk < cgdmin(&sblock, c)) {
		if ((blk + cnt) > cgsblock(&sblock, c)) {
			if (debug) {
				printf("blk %ld < cgdmin %ld;",
				    blk, cgdmin(&sblock, c));
				printf(" blk + cnt %ld > cgsbase %ld\n",
				    blk + cnt, cgsblock(&sblock, c));
			}
			return (1);
		}
	} else {
		if ((blk + cnt) > cgbase(&sblock, c+1)) {
			if (debug)  {
				printf("blk %ld >= cgdmin %ld;",
				    blk, cgdmin(&sblock, c));
				printf(" blk + cnt %ld > sblock.fs_fpg %ld\n",
				    blk+cnt, sblock.fs_fpg);
			}
			return (1);
		}
	}
	return (0);
}

/*
 * General purpose interface for reading inodes.
 */
struct dinode *
ginode(inumber)
	ino_t inumber;
{
	daddr_t iblk;

	if (inumber < ROOTINO || inumber > maxino)
		errexit("bad inode number %d to ginode\n", inumber);
	if (startinum == 0 ||
	    inumber < startinum || inumber >= startinum + INOPB(&sblock)) {
		iblk = ino_to_fsba(&sblock, inumber);
		if (pbp != 0)
			pbp->b_flags &= ~B_INUSE;
		pbp = getdatablk(iblk, sblock.fs_bsize);
		startinum = (inumber / INOPB(&sblock)) * INOPB(&sblock);
	}
	return (&pbp->b_un.b_dinode[inumber % INOPB(&sblock)]);
}

/*
 * Special purpose version of ginode used to optimize first pass
 * over all the inodes in numerical order.
 */
ino_t nextino, lastinum;
long readcnt, readpercg, fullcnt, inobufsize, partialcnt, partialsize;
struct dinode *inodebuf;

struct dinode *
getnextinode(inumber)
	ino_t inumber;
{
	long size;
	daddr_t dblk;
	static struct dinode *dp;

	if (inumber != nextino++ || inumber > maxino)
		errexit("bad inode number %d to nextinode\n", inumber);
	if (inumber >= lastinum) {
		readcnt++;
		dblk = fsbtodb(&sblock, ino_to_fsba(&sblock, lastinum));
		if (readcnt % readpercg == 0) {
			size = partialsize;
			lastinum += partialcnt;
		} else {
			size = inobufsize;
			lastinum += fullcnt;
		}
		(void)bread((char *)inodebuf, dblk, size); /* ??? */
		dp = inodebuf;
	}
	return (dp++);
}

resetinodebuf()
{

	startinum = 0;
	nextino = 0;
	lastinum = 0;
	readcnt = 0;
	inobufsize = blkroundup(&sblock, INOBUFSIZE);
	fullcnt = inobufsize / sizeof(struct dinode);
	readpercg = sblock.fs_ipg / fullcnt;
	partialcnt = sblock.fs_ipg % fullcnt;
	partialsize = partialcnt * sizeof(struct dinode);
	if (partialcnt != 0) {
		readpercg++;
	} else {
		partialcnt = fullcnt;
		partialsize = inobufsize;
	}
	if (inodebuf == NULL &&
	    (inodebuf = (struct dinode *)malloc((unsigned)inobufsize)) == NULL)
		errexit("Cannot allocate space for inode buffer\n");
	while (nextino < ROOTINO)
		(void)getnextinode(nextino);
}

freeinodebuf()
{

	if (inodebuf != NULL)
		free((char *)inodebuf);
	inodebuf = NULL;
}

/*
 * Routines to maintain information about directory inodes.
 * This is built during the first pass and used during the
 * second and third passes.
 *
 * Enter inodes into the cache.
 */
#ifdef cdh
void
#endif
cacheino(dp, inumber)
	register struct dinode *dp;
	ino_t inumber;
{
	register struct inoinfo *inp;
	struct inoinfo **inpp;
	unsigned int blks;

	blks = howmany(dp->di_size, sblock.fs_bsize);
	if (blks > NDADDR)
		blks = NDADDR + NIADDR;
	inp = (struct inoinfo *)
		malloc(sizeof(*inp) + (blks - 1) * sizeof(daddr_t));
	if (inp == NULL)
		return;
	inpp = &inphead[inumber % numdirs];
	inp->i_nexthash = *inpp;
	*inpp = inp;
	inp->i_parent = (ino_t)0;
	inp->i_dotdot = (ino_t)0;
	inp->i_number = inumber;
	inp->i_isize = dp->di_size;
	inp->i_numblks = blks * sizeof(daddr_t);
	bcopy((char *)&dp->di_db[0], (char *)&inp->i_blks[0],
	    (size_t)inp->i_numblks);
	if (inplast == listmax) {
		listmax += 100;
		inpsort = (struct inoinfo **)realloc((char *)inpsort,
		    (unsigned)listmax * sizeof(struct inoinfo *));
		if (inpsort == NULL)
			errexit("cannot increase directory list");
	}
	inpsort[inplast++] = inp;
}

/*
 * Look up an inode cache structure.
 */
struct inoinfo *
getinoinfo(inumber)
	ino_t inumber;
{
	register struct inoinfo *inp;

	for (inp = inphead[inumber % numdirs]; inp; inp = inp->i_nexthash) {
		if (inp->i_number != inumber)
			continue;
		return (inp);
	}
	errexit("cannot find inode %d\n", inumber);
	return ((struct inoinfo *)0);
}

/*
 * Clean up all the inode cache structure.
 */
int
inocleanup()
{
	register struct inoinfo **inpp;

	if (inphead == NULL)
		return;
	for (inpp = &inpsort[inplast - 1]; inpp >= inpsort; inpp--)
		free((char *)(*inpp));
	free((char *)inphead);
	free((char *)inpsort);
	inphead = inpsort = NULL;
}

inodirty()
{
	dirty(pbp);
}

clri(idesc, type, flag)
	register struct inodesc *idesc;
	char *type;
	int flag;
{
	register struct dinode *dp;

	dp = ginode(idesc->id_number);
	if (flag == 1) {
		pwarn("%s %s", type,
		    (dp->di_mode & IFMT) == IFDIR ? "DIR" : "FILE");
		pinode(idesc->id_number);
	}
	if (preen || reply("CLEAR") == 1) {
		if (preen)
			printf(" (CLEARED)\n");
		n_files--;
		(void)ckinode(dp, idesc);
		clearinode(dp);
		statemap[idesc->id_number] = USTATE;
		inodirty();
	}
}

findname(idesc)
	struct inodesc *idesc;
{
	register struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino != idesc->id_parent)
		return (KEEPON);
	bcopy(dirp->d_name, idesc->id_name, (size_t)dirp->d_namlen + 1);
	return (STOP|FOUND);
}

findino(idesc)
	struct inodesc *idesc;
{
	register struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino == 0)
		return (KEEPON);
	if (strcmp(dirp->d_name, idesc->id_name) == 0 &&
	    dirp->d_ino >= ROOTINO && dirp->d_ino <= maxino) {
		idesc->id_parent = dirp->d_ino;
		return (STOP|FOUND);
	}
	return (KEEPON);
}

pinode(ino)
	ino_t ino;
{
	register struct dinode *dp;
	register char *p;
#ifdef unix
	struct passwd *pw;
	char *ctime();
#endif

	printf(" I=%lu ", ino);
	if (ino < ROOTINO || ino > maxino)
		return;
	dp = ginode(ino);
	printf(" OWNER=");
#ifdef unix
#ifndef SMALL
	if ((pw = getpwuid((int)dp->di_uid)) != 0)
		printf("%s ", pw->pw_name);
	else
#endif
#endif
		printf("%u ", (unsigned)dp->di_uid);
	printf("MODE=%o\n", dp->di_mode);
	if (preen)
		printf("%s: ", cdevname);
#ifdef cdh
	printf("SIZE=%u ", dp->di_size);
#else
	printf("SIZE=%qu ", dp->di_size);
#endif
	p = ctime(&(dp->di_mtime.ts_sec));
	printf("MTIME=%12.12s %4.4s ", &p[4], &p[20]);
}

blkerror(ino, type, blk)
	ino_t ino;
	char *type;
	daddr_t blk;
{

	pfatal("%ld %s I=%lu", blk, type, ino);
	printf("\n");
	switch (statemap[ino]) {

	case FSTATE:
		statemap[ino] = FCLEAR;
		return;

	case DSTATE:
		statemap[ino] = DCLEAR;
		return;

	case FCLEAR:
	case DCLEAR:
		return;

	default:
		errexit("BAD STATE %d TO BLKERR", statemap[ino]);
		/* NOTREACHED */
	}
}

/*
 * allocate an unused inode
 */
ino_t
allocino(request, type)
	ino_t request;
	int type;
{
	register ino_t ino;
	register struct dinode *dp;

	if (request == 0)
		request = ROOTINO;
	else if (statemap[request] != USTATE)
		return (0);
	for (ino = request; ino < maxino; ino++)
		if (statemap[ino] == USTATE)
			break;
	if (ino == maxino)
		return (0);
	switch (type & IFMT) {
	case IFDIR:
		statemap[ino] = DSTATE;
		break;
	case IFREG:
	case IFLNK:
		statemap[ino] = FSTATE;
		break;
	default:
		return (0);
	}
	dp = ginode(ino);
	dp->di_db[0] = allocblk((long)1);
	if (dp->di_db[0] == 0) {
		statemap[ino] = USTATE;
		return (0);
	}
	dp->di_mode = type;
	(void)time(&dp->di_atime.ts_sec);
	dp->di_mtime = dp->di_ctime = dp->di_atime;
	dp->di_size = sblock.fs_fsize;
	dp->di_blocks = btodb(sblock.fs_fsize);
	n_files++;
	inodirty();
	if (newinofmt)
		typemap[ino] = IFTODT(type);
	return (ino);
}

/*
 * deallocate an inode
 */
freeino(ino)
	ino_t ino;
{
	struct inodesc idesc;
	extern int pass4check();
	struct dinode *dp;

	bzero((char *)&idesc, sizeof(struct inodesc));
	idesc.id_type = ADDR;
	idesc.id_func = pass4check;
	idesc.id_number = ino;
	dp = ginode(ino);
	(void)ckinode(dp, &idesc);
	clearinode(dp);
	inodirty();
	statemap[ino] = USTATE;
	n_files--;
}
