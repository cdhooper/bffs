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
/*static char sccsid[] = "from: @(#)setup.c	8.2 (Berkeley) 2/21/94";*/
static char *rcsid = "$Id: setup.c,v 1.11 1994/06/29 11:01:37 ws Exp $";
#endif /* not lint */

/* amiga debug added */
#include <stdio.h>

#define DKTYPENAMES
#include <sys/param.h>
#include <sys/time.h>
#include <ufs/dinode.h>
#include <ufs/fs.h>
#include <sys/stat.h>
#ifndef cdh
#include <sys/ioctl.h>
#endif
#include <sys/disklabel.h>
#include <sys/file.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fsck.h"


struct bufarea asblk;
#define altsblock (*asblk.b_un.b_fs)
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

struct	disklabel *getdisklabel();

#ifdef AMIGA
void
error_exit(int rc)
{
	dio_inhibit(0);
	dio_close();
	exit(rc);
}

int
break_abort(void)
{
	write(2, "^C\n", 3);
	error_exit(1);
}
#endif

setup(dev)
	char *dev;
{
#ifdef OLD_CS
	long cg, size, asked, i, j;
#else
	long cg, size, asked, i;
#endif
	long bmapsize;
	struct disklabel *lp;
	off_t sizepb;
#ifndef AMIGA
	struct stat statb;
#endif
	struct fs proto;

#ifdef AMIGA
	onbreak(break_abort);
	if (dio_open(dev) == 0) {
		fsreadfd = -1;
		if (preen == 0)
			printf("\n");
		dio_inhibit(1);
		goto opened;
	}
	if ((fsreadfd = open(dev, O_RDWR)) < 0) {
		Perror("Can't open %s", dev);
		return (0);
	}

	if (preen == 0)
		printf("** %s", dev);

	if (nflag == 0)
		fswritefd = fsreadfd;
	else
		fswritefd = -1;
#else
	havesb = 0;
	fswritefd = -1;
	if (stat(dev, &statb) < 0) {
		Perror("Can't stat %s", dev);
		return (0);
	}
#ifndef cdh
	if ((statb.st_mode & S_IFMT) != S_IFCHR) {
		pfatal("%s is not a character device", dev);
		if (reply("CONTINUE") == 0)
			return (0);
	}
#endif

	if ((fsreadfd = open(dev, O_RDONLY)) < 0) {
		Perror("Can't open %s", dev);
		return (0);
	}

	if (preen == 0)
		printf("** %s", dev);
	if (nflag || (fswritefd = open(dev, O_WRONLY)) < 0) {
		fswritefd = -1;
		if (preen)
			pfatal("NO WRITE ACCESS");
		printf(" (NO WRITE)");
	}
#endif

	if (preen == 0)
		printf("\n");
	opened:
	fsmodified = 0;
	lfdir = 0;
	initbarea(&sblk);
	initbarea(&asblk);
#ifdef cdh
	sblk.b_un.b_buf = malloc(MAXBSIZE);
	asblk.b_un.b_buf = malloc(MAXBSIZE);
#else
	sblk.b_un.b_buf = malloc(SBSIZE);
	asblk.b_un.b_buf = malloc(SBSIZE);
#endif
	if (sblk.b_un.b_buf == NULL || asblk.b_un.b_buf == NULL)
		errexit("cannot allocate space for superblock\n");
	if (lp = getdisklabel((char *)NULL, fsreadfd))
		dev_bsize = secsize = lp->d_secsize;
	else
		dev_bsize = secsize = DEV_BSIZE;
#ifdef cdh
	dio_assign_bsize(dev_bsize);
	dev_bsize = secsize = DEV_BSIZE;
	dirblk_setup();
#endif
	/*
	 * Read in the superblock, looking for alternates if necessary
	 */
	if (readsb(1) == 0) {
		if (bflag || preen || calcsb(dev, fsreadfd, &proto) == 0)
			return(0);
		if (reply("LOOK FOR ALTERNATE SUPERBLOCKS") == 0)
			return (0);
		for (cg = 0; cg < proto.fs_ncg; cg++) {
			bflag = fsbtodb(&proto, cgsblock(&proto, cg));
			if (readsb(0) != 0)
				break;
		}
		if (cg >= proto.fs_ncg) {
			printf("%s %s\n%s %s\n%s %s\n",
				"SEARCH FOR ALTERNATE SUPER-BLOCK",
				"FAILED. YOU MUST USE THE",
				"-b OPTION TO FSCK TO SPECIFY THE",
				"LOCATION OF AN ALTERNATE",
				"SUPER-BLOCK TO SUPPLY NEEDED",
				"INFORMATION; SEE fsck(8).");
			return(0);
		}
		pwarn("USING ALTERNATE SUPERBLOCK AT %d\n", bflag);
	}
	maxfsblock = sblock.fs_size;
	maxino = sblock.fs_ncg * sblock.fs_ipg;
	/*
	 * Check and potentially fix certain fields in the super block.
	 */
	if (sblock.fs_optim != FS_OPTTIME && sblock.fs_optim != FS_OPTSPACE) {
		pfatal("UNDEFINED OPTIMIZATION IN SUPERBLOCK");
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_optim = FS_OPTTIME;
			sbdirty();
		}
	}
	if ((sblock.fs_minfree < 0 || sblock.fs_minfree > 99)) {
		pfatal("IMPOSSIBLE MINFREE=%d IN SUPERBLOCK",
			sblock.fs_minfree);
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_minfree = 10;
			sbdirty();
		}
	}
	if (sblock.fs_interleave < 1 ||
	    sblock.fs_interleave > sblock.fs_nsect) {
		pwarn("IMPOSSIBLE INTERLEAVE=%d IN SUPERBLOCK",
			sblock.fs_interleave);
		sblock.fs_interleave = 1;
		if (preen)
			printf(" (FIXED)\n");
		if (preen || reply("SET TO DEFAULT") == 1) {
			sbdirty();
			dirty(&asblk);
		}
	}
	if (sblock.fs_npsect < sblock.fs_nsect ||
	    sblock.fs_npsect > sblock.fs_nsect*2) {
		pwarn("IMPOSSIBLE NPSECT=%d IN SUPERBLOCK",
			sblock.fs_npsect);
		sblock.fs_npsect = sblock.fs_nsect;
		if (preen)
			printf(" (FIXED)\n");
		if (preen || reply("SET TO DEFAULT") == 1) {
			sbdirty();
			dirty(&asblk);
		}
	}
	if (sblock.fs_inodefmt >= FS_44INODEFMT) {
		newinofmt = 1;
	} else {
		sblock.fs_qbmask.val[0] = 0xffffffff;
		sblock.fs_qbmask.val[1] = 0xffffffff;
		sblock.fs_qfmask.val[0] = 0xffffffff;
		sblock.fs_qfmask.val[1] = 0xffffffff;
		S32(sblock.fs_qbmask) = ~sblock.fs_bmask;
		S32(sblock.fs_qfmask) = ~sblock.fs_fmask;
		newinofmt = 0;
	}
	/*
	 * Convert to new inode format.
	 */
	if (cvtlevel >= 2 && sblock.fs_inodefmt < FS_44INODEFMT) {
		if (preen)
			pwarn("CONVERTING TO NEW INODE FORMAT\n");
		else if (!reply("CONVERT TO NEW INODE FORMAT"))
			return(0);
		doinglevel2++;
		sblock.fs_inodefmt = FS_44INODEFMT;
		sizepb = sblock.fs_bsize;
		sblock.fs_maxfilesize.val[0] = 0;
		sblock.fs_maxfilesize.val[1] = 0;
#ifdef cdh
		/* This is messy because DICE C doesn't have 64-bit types */
		sblock.fs_maxfilesize.val[0] = 0;
		sblock.fs_maxfilesize.val[1] = sblock.fs_bsize * NDADDR - 1;
		int size_bits   = 0;
		int nindir_bits = 0;
		for (sizepb = 1; sizepb < sblock.fs_bsize; sizepb <<= 1)
			size_bits++;
		for (sizepb = 1; sizepb < NINDIR(&sblock); sizepb <<= 1)
			nindir_bits++;
		for (i = 0; i < NIADDR; i++) {
		    size_bits += nindir_bits;
		    if (size_bits < 32)
			sblock.fs_maxfilesize.val[1] |= (1 << size_bits);
		    else
			sblock.fs_maxfilesize.val[0] |= (1 << (size_bits - 32));
		}
#else
		S32(sblock.fs_maxfilesize) = sblock.fs_bsize * NDADDR - 1;
		for (i = 0; i < NIADDR; i++) {
			sizepb *= NINDIR(&sblock);
			sblock.fs_maxfilesize += sizepb;
		}
#endif
		sblock.fs_maxsymlinklen = MAXSYMLINKLEN;
		sblock.fs_qbmask.val[0] = 0xffffffff;
		sblock.fs_qbmask.val[1] = 0xffffffff;
		sblock.fs_qfmask.val[0] = 0xffffffff;
		sblock.fs_qfmask.val[1] = 0xffffffff;
		S32(sblock.fs_qbmask) = ~sblock.fs_bmask;
		S32(sblock.fs_qfmask) = ~sblock.fs_fmask;
		sbdirty();
		dirty(&asblk);
	}
	/*
	 * Convert to new cylinder group format.
	 */
	if (cvtlevel >= 1 && sblock.fs_postblformat == FS_42POSTBLFMT) {
		if (preen)
			pwarn("CONVERTING TO NEW CYLINDER GROUP FORMAT\n");
		else if (!reply("CONVERT TO NEW CYLINDER GROUP FORMAT"))
			return(0);
		doinglevel1++;
		sblock.fs_postblformat = FS_DYNAMICPOSTBLFMT;
		sblock.fs_nrpos = 8;
#ifdef UFS_V1
		sblock.fs_postbloff =
		    (char *)(&sblock.fs_maxbsize) -
		    (char *)(&sblock.fs_link);
#else
		sblock.fs_postbloff =
		    (char *)(&sblock.fs_opostbl[0][0]) -
		    (char *)(&sblock.fs_link);
#endif
		sblock.fs_rotbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_link);
		sblock.fs_cgsize =
			fragroundup(&sblock, CGSIZE(&sblock));
		sbdirty();
		dirty(&asblk);
	}
	if (asblk.b_dirty && !bflag) {
		bcopy((char *)&sblock, (char *)&altsblock,
			(size_t)sblock.fs_sbsize);
		flush(fswritefd, &asblk);
	}
	/*
	 * read in the summary info.
	 */
	asked = 0;
#ifdef OLD_CS
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		size = sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize;
		sblock.fs_csp[j] = (struct csum *)calloc(1, (unsigned)size);
		if (bread((char *)sblock.fs_csp[j],
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    size) != 0 && !asked) {
			pfatal("BAD SUMMARY INFORMATION");
			if (reply("CONTINUE") == 0)
				errexit("");
			asked++;
		}
	}
#else
	size = sblock.fs_cssize;
	sblock.fs_csp = (struct csum *) calloc(1, (unsigned)size);
	if (sblock.fs_csp == NULL) {
		printf("cannot alloc %u bytes for csp\n", size);
		goto badsb;
	}

	if (bread((char *)sblock.fs_csp,
	    fsbtodb(&sblock, sblock.fs_csaddr), size) != 0 && !asked) {
		pfatal("BAD SUMMARY INFORMATION");
		if (reply("CONTINUE") == 0)
			errexit("");
		asked++;
	}
#endif
	/*
	 * allocate and initialize the necessary maps
	 */
	bmapsize = roundup(howmany(maxfsblock, NBBY), sizeof(short));
	blockmap = calloc((unsigned)bmapsize, sizeof (char));
	if (blockmap == NULL) {
		printf("cannot alloc %u bytes for blockmap\n",
		    (unsigned)bmapsize);
		goto badsb;
	}
	statemap = calloc((unsigned)(maxino + 1), sizeof(char));
	if (statemap == NULL) {
		printf("cannot alloc %u bytes for statemap\n",
		    (unsigned)(maxino + 1));
		goto badsb;
	}
	typemap = calloc((unsigned)(maxino + 1), sizeof(char));
	if (typemap == NULL) {
		printf("cannot alloc %u bytes for typemap\n",
		    (unsigned)(maxino + 1));
		goto badsb;
	}
	lncntp = (short *)calloc((unsigned)(maxino + 1), sizeof(short));
	if (lncntp == NULL) {
		printf("cannot alloc %u bytes for lncntp\n",
		    (unsigned)(maxino + 1) * sizeof(short));
		goto badsb;
	}
#ifdef UFS_V1
	if (sblock.fs_flags & FS_FLAGS_UPDATED) {
	    numdirs = sblock.fs_new_cstotal.cs_ndir[is_big_endian];
	} else {
	    numdirs = sblock.fs_cstotal.cs_ndir;
	}
#else
	numdirs = sblock.fs_cstotal.cs_ndir;
#endif
	inplast = 0;
	listmax = numdirs + 10;
	inpsort = (struct inoinfo **)calloc((unsigned)listmax,
	    sizeof(struct inoinfo *));
	inphead = (struct inoinfo **)calloc((unsigned)numdirs,
	    sizeof(struct inoinfo *));
	if (inpsort == NULL || inphead == NULL) {
		printf("cannot alloc %u bytes for inphead\n",
		    (unsigned)numdirs * sizeof(struct inoinfo *));
		goto badsb;
	}
	bufinit();
	return (1);

badsb:
	ckfini();
	return (0);
}

/*
 * Read in the super block and its summary info.
 */
readsb(listerr)
	int listerr;
{
#ifdef Amiga
	daddr_t super = bflag ? bflag : SBOFF / DEV_BSIZE;
#else
	daddr_t super = bflag ? bflag : SBOFF / dev_bsize;
#endif

	if (bread((char *)&sblock, super, (long)SBSIZE) != 0)
		return (0);
	sblk.b_bno = super;
	sblk.b_size = SBSIZE;
	/*
	 * run a few consistency checks of the super block
	 */
	if (sblock.fs_magic != FS_MAGIC)
		{ badsb(listerr, "MAGIC NUMBER WRONG"); return (0); }
	if (sblock.fs_ncg < 1)
		{ badsb(listerr, "NCG OUT OF RANGE"); return (0); }
	if (sblock.fs_cpg < 1)
		{ badsb(listerr, "CPG OUT OF RANGE"); return (0); }
	if (sblock.fs_ncg * sblock.fs_cpg < sblock.fs_ncyl ||
	    (sblock.fs_ncg - 1) * sblock.fs_cpg >= sblock.fs_ncyl)
		{ badsb(listerr, "NCYL LESS THAN NCG*CPG"); return (0); }
#ifdef cdh
	if (sblock.fs_sbsize > MAXBSIZE)
#else
	if (sblock.fs_sbsize > SBSIZE)
#endif
		{ badsb(listerr, "SIZE PREPOSTEROUSLY LARGE"); return (0); }
	/*
	 * Compute block size that the filesystem is based on,
	 * according to fsbtodb, and adjust superblock block number
	 * so we can tell if this is an alternate later.
	 */
	super *= dev_bsize;
	dev_bsize = sblock.fs_fsize / fsbtodb(&sblock, 1);
	sblk.b_bno = super / dev_bsize;
	if (bflag) {
		havesb = 1;
		return (1);
	}
	/*
	 * Set all possible fields that could differ, then do check
	 * of whole super block against an alternate super block.
	 * When an alternate super-block is specified this check is skipped.
	 */
#ifdef cdh
	totalreads++;
#endif
	getblk(&asblk, cgsblock(&sblock, sblock.fs_ncg - 1), sblock.fs_sbsize);
	if (asblk.b_errs)
		return (0);
	altsblock.fs_link = sblock.fs_link;
	altsblock.fs_rlink = sblock.fs_rlink;
	altsblock.fs_time = sblock.fs_time;
	altsblock.fs_cstotal = sblock.fs_cstotal;
	altsblock.fs_cgrotor = sblock.fs_cgrotor;
	altsblock.fs_fmod = sblock.fs_fmod;
	altsblock.fs_clean = sblock.fs_clean;
	altsblock.fs_ronly = sblock.fs_ronly;
	altsblock.fs_flags = sblock.fs_flags;
	altsblock.fs_maxcontig = sblock.fs_maxcontig;
	altsblock.fs_minfree = sblock.fs_minfree;
	altsblock.fs_optim = sblock.fs_optim;
	altsblock.fs_rotdelay = sblock.fs_rotdelay;
	altsblock.fs_maxbpg = sblock.fs_maxbpg;
#ifdef OLD_CS
	bcopy((char *)sblock.fs_csp, (char *)altsblock.fs_csp,
		sizeof sblock.fs_csp);
#else
	bcopy((char *)sblock.fs_ocsp, (char *)altsblock.fs_ocsp,
		sizeof sblock.fs_ocsp);
	altsblock.fs_contigdirs = sblock.fs_contigdirs;
	altsblock.fs_csp = sblock.fs_csp;
	altsblock.fs_maxcluster = sblock.fs_maxcluster;
	altsblock.fs_active = sblock.fs_active;
#endif
#ifdef UFS_V1
#define offsetof(T,memb) ((size_t)&(((T *)0)->memb)-(size_t)((T *)0))
	/*
	 * If this is a new format filesystem, then also copy the fields
	 * which used to be fs_opostbl[][].
	 */
	if (altsblock.fs_flags & FS_FLAGS_UPDATED) {
	    if (debug)
		printf("Superblock is BSD V1\n");

	    bcopy((char *)&sblock.fs_maxbsize, (char *)&altsblock.fs_maxbsize,
		  offsetof(struct fs, fs_contigsumsize) -
		  offsetof(struct fs, fs_maxbsize));
	}
#endif
	bcopy((char *)sblock.fs_fsmnt, (char *)altsblock.fs_fsmnt,
		sizeof sblock.fs_fsmnt);
#ifndef UFS_V1
	/* Below is from old filesystem structure */
	bcopy((char *)sblock.fs_sparecon, (char *)altsblock.fs_sparecon,
		sizeof sblock.fs_sparecon);
#endif
	/*
	 * The following should not have to be copied.
	 */
	altsblock.fs_fsbtodb = sblock.fs_fsbtodb;
	altsblock.fs_interleave = sblock.fs_interleave;
	altsblock.fs_npsect = sblock.fs_npsect;
	altsblock.fs_nrpos = sblock.fs_nrpos;
	altsblock.fs_qbmask = sblock.fs_qbmask;
	altsblock.fs_qfmask = sblock.fs_qfmask;
	altsblock.fs_state = sblock.fs_state;
	altsblock.fs_maxfilesize = sblock.fs_maxfilesize;
	if (bcmp((char *)&sblock, (char *)&altsblock, (int)sblock.fs_sbsize)) {
		badsb(listerr,
		"VALUES IN SUPER BLOCK DISAGREE WITH THOSE IN FIRST ALTERNATE");
#ifdef cdh
	    if (debug) {
		printf("   BSize=0x%x Primary_SB=0x%x [0x%x], Alt_SB=0x%x\n",
			dev_bsize, super / dev_bsize, sblk.b_bno,
                        cgsblock(&sblock, sblock.fs_ncg - 1));
		printf("   fsize:bsize  %05x:%05x %05x:%05x\n"
		       "   npsect          %08x    %08x\n"
		       "   nspf            %8u    %8u\n",
		       sblock.fs_fsize, sblock.fs_bsize,
		       altsblock.fs_fsize, altsblock.fs_bsize,
		       sblock.fs_npsect, altsblock.fs_npsect,
		       sblock.fs_nspf, altsblock.fs_nspf);
		printf("   diffs:");
		{
		    unsigned char *ptr1 = (unsigned char *) &sblock;
		    unsigned char *ptr2 = (unsigned char *) &altsblock;
		    int   pos;
		    int   diffs = 0;
		    for (pos = 0; pos < sizeof (sblock); pos++) {
			if (*ptr1 != *ptr2) {
			    if (diffs++ > 10)
				break;
			    printf(" %x:%02x!=%02x", pos,  *ptr1,  *ptr2);
			}
			ptr1++;
			ptr2++;
		    }
		}

		printf("\nsuperblock offsets (for debug):\n"
		       "   fs_optim=%x fs_fsmnt=%x fs_cgrotor=%x fs_csp=%x "
		       "fs_maxbsize=%x\n"
		       "   fs_new_cstotal=%x fs_new_time=%x fs_snapinum=%x "
		       "fs_sparecon32=%x\n"
		       "   fs_contigsumsize=%x fs_inodefmt=%x fs_magic=%x\n",
		       offsetof(struct fs, fs_optim),
		       offsetof(struct fs, fs_fsmnt),
		       offsetof(struct fs, fs_cgrotor),
		       offsetof(struct fs, fs_csp),
		       offsetof(struct fs, fs_maxbsize),
		       offsetof(struct fs, fs_new_cstotal),
		       offsetof(struct fs, fs_new_time),
		       offsetof(struct fs, fs_snapinum),
		       offsetof(struct fs, fs_sparecon32),
		       offsetof(struct fs, fs_contigsumsize),
		       offsetof(struct fs, fs_inodefmt),
		       offsetof(struct fs, fs_magic));
		}
#endif
		return (0);
	}
	havesb = 1;
	return (1);
}

badsb(listerr, s)
	int listerr;
	char *s;
{

	if (!listerr)
		return;
	if (preen)
		printf("%s: ", cdevname);
	pfatal("BAD SUPER BLOCK: %s\n", s);
}

#ifdef unix
char *strchr();
#endif
#define index(x,y) strchr(x,y)

/*
 * Calculate a prototype superblock based on information in the disk label.
 * When done the cgsblock macro can be calculated and the fs_ncg field
 * can be used. Do NOT attempt to use other macros without verifying that
 * their needed information is available!
 */
calcsb(dev, devfd, fs)
	char *dev;
	int devfd;
	register struct fs *fs;
{
	register struct disklabel *lp;
	register struct partition *pp;
	register char *cp;
	int i;

	cp = index(dev, '\0') - 1;
	if (cp == (char *)-1 || (*cp < 'a' || *cp > 'h') && !isdigit(*cp)) {
		pfatal("%s: CANNOT FIGURE OUT FILE SYSTEM PARTITION\n", dev);
		return (0);
	}
	lp = getdisklabel(dev, devfd);
	if (isdigit(*cp))
		pp = &lp->d_partitions[0];
	else
		pp = &lp->d_partitions[*cp - 'a'];
	if (pp->p_fstype != FS_BSDFFS) {
		pfatal("%s: NOT LABELED AS A BSD FILE SYSTEM (%s)\n",
			dev, pp->p_fstype < FSMAXTYPES ?
			fstypenames[pp->p_fstype] : "unknown");
		return (0);
	}
	bzero((char *)fs, sizeof(struct fs));
	fs->fs_fsize = pp->p_fsize;
	fs->fs_frag = pp->p_frag;
	fs->fs_cpg = pp->p_cpg;
	fs->fs_size = pp->p_size;
	fs->fs_ntrak = lp->d_ntracks;
	fs->fs_nsect = lp->d_nsectors;
	fs->fs_spc = lp->d_secpercyl;
	fs->fs_nspf = fs->fs_fsize / lp->d_secsize;
	fs->fs_sblkno = roundup(
		howmany(lp->d_bbsize + lp->d_sbsize, fs->fs_fsize),
		fs->fs_frag);
	fs->fs_cgmask = 0xffffffff;
	for (i = fs->fs_ntrak; i > 1; i >>= 1)
		fs->fs_cgmask <<= 1;
	if (!POWEROF2(fs->fs_ntrak))
		fs->fs_cgmask <<= 1;
	fs->fs_cgoffset = roundup(
		howmany(fs->fs_nsect, NSPF(fs)), fs->fs_frag);
	fs->fs_fpg = (fs->fs_cpg * fs->fs_spc) / NSPF(fs);
	fs->fs_ncg = howmany(fs->fs_size / fs->fs_spc, fs->fs_cpg);
	for (fs->fs_fsbtodb = 0, i = NSPF(fs); i > 1; i >>= 1)
		fs->fs_fsbtodb++;
	dev_bsize = lp->d_secsize;
	return (1);
}

struct disklabel *
getdisklabel(s)
	char *s;
{
	static struct disklabel lab;
	struct disklabel *lp;
	char *bootarea;

	/* following code replaces the ioctl */
#ifdef cdh
	bootarea = (char *) malloc(MAXBSIZE);
#else
	bootarea = (char *) malloc(BBSIZE);
#endif
	if (bootarea == NULL) {
		printf("Unable to allocate %s bytes for label buffer", BBSIZE);
		exit(1);
	}

	if (bread(bootarea, 0, BBSIZE))
		Perror("Error reading disk label.");
	for (lp = (struct disklabel *)bootarea;
	     lp <= (struct disklabel *)(bootarea + BBSIZE - sizeof(*lp));
	     lp = (struct disklabel *)((char *)lp + 16))
		if (lp->d_magic == DISKMAGIC && lp->d_magic2 == DISKMAGIC)
			break;
	if (lp > (struct disklabel *)(bootarea+BBSIZE-sizeof(*lp)) ||
	    lp->d_magic != DISKMAGIC || lp->d_magic2 != DISKMAGIC) {
		/* lp = (struct disklabel *)(bootarea + LABELOFFSET); */

		printf("No disk label.\n");
		if (s == NULL) {
			free(bootarea);
			return ((struct disklabel *)NULL);
		}
		errexit("%s: Disk label is required.\n", s);
	}
	bcopy(lp, &lab, sizeof(struct disklabel));

	free(bootarea);
	return (&lab);
}
