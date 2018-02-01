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
/*static char sccsid[] = "from: @(#)pass5.c	8.2 (Berkeley) 2/2/94";*/
static char *rcsid = "$Id: pass5.c,v 1.6 1994/06/08 19:00:30 mycroft Exp $";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <ufs/dinode.h>
#include <ufs/fs.h>
#include <string.h>
#include "fsck.h"

extern int verbose;

pass5()
{
	int c, blk, frags, basesize, sumsize, mapsize, savednrpos;
	register struct fs *fs = &sblock;
	register struct cg *cg = &cgrp;
	daddr_t dbase, dmax;
	register daddr_t d;
	register long i, j;
	struct csum *cs;
	struct csum cstotal;
	struct inodesc idesc[3];
	char *buf;
	register struct cg *newcg;
	struct ocg *ocg;
#ifdef cdh
	int imapsize = 0;
	int fmapsize = 0;
#endif

	buf = (char *) malloc(MAXBSIZE);
        if (buf == NULL)
                errexit("unable to allocate memory\n");
	newcg	= (struct cg *) buf;
	ocg	= (struct ocg *) buf;

	bzero((char *)newcg, (size_t)fs->fs_cgsize);
	newcg->cg_niblk = fs->fs_ipg;
	if (cvtlevel > 3) {
		if (fs->fs_maxcontig < 2 && fs->fs_contigsumsize > 0) {
			if (preen)
				pwarn("DELETING CLUSTERING MAPS\n");
			if (preen || reply("DELETE CLUSTERING MAPS")) {
				fs->fs_contigsumsize = 0;
				doinglevel1 = 1;
				sbdirty();
			}
		}
		if (fs->fs_maxcontig > 1) {
			char *doit = 0;

			if (fs->fs_contigsumsize < 1) {
				doit = "CREAT";
			} else if (fs->fs_contigsumsize < fs->fs_maxcontig &&
				   fs->fs_contigsumsize < FS_MAXCONTIG) {
				doit = "EXPAND";
			}
			if (doit) {
				i = fs->fs_contigsumsize;
				fs->fs_contigsumsize =
				    MIN(fs->fs_maxcontig, FS_MAXCONTIG);
				if (CGSIZE(fs) > fs->fs_bsize) {
					pwarn("CANNOT %s CLUSTER MAPS\n", doit);
					fs->fs_contigsumsize = i;
				} else if (preen ||
				    reply("CREATE CLUSTER MAPS")) {
					if (preen)
						pwarn("%sING CLUSTER MAPS\n",
						    doit);
					fs->fs_cgsize =
					    fragroundup(fs, CGSIZE(fs));
					doinglevel1 = 1;
					sbdirty();
				}
			}
		}
	}
#ifdef UFS_V1
	/* Below adapted from NetBSD 7.1.1 fsck */
	basesize = (int) &newcg->cg_space[0] - (int) (&newcg->cg_link);
        sumsize = 0;

	newcg->cg_btotoff = basesize;
	newcg->cg_boff =
	    newcg->cg_btotoff + fs->fs_cpg * sizeof(long);
	newcg->cg_iusedoff = newcg->cg_boff +
	    fs->fs_cpg * fs->fs_nrpos * sizeof(short);
	newcg->cg_freeoff =
	    newcg->cg_iusedoff + howmany(fs->fs_ipg, NBBY);
	bzero(cg_blksfree(newcg),
	      howmany(fs->fs_cpg * fs->fs_spc / NSPF(fs), NBBY));
	if (fs->fs_contigsumsize <= 0) {
		newcg->cg_nextfreeoff = newcg->cg_freeoff +
		    howmany(fs->fs_cpg * fs->fs_spc / NSPF(fs), NBBY);
	} else {
		newcg->cg_clustersumoff = newcg->cg_freeoff +
		    howmany(fs->fs_cpg * fs->fs_spc / NSPF(fs), NBBY) -
		    sizeof(long);
		newcg->cg_clustersumoff =
		    roundup(newcg->cg_clustersumoff, sizeof(long));
		newcg->cg_clusteroff = newcg->cg_clustersumoff +
		    (fs->fs_contigsumsize + 1) * sizeof(long);
		newcg->cg_nextfreeoff = newcg->cg_clusteroff +
		    howmany(fs->fs_cpg * fs->fs_spc / NSPB(fs), NBBY);
	}
	newcg->cg_magic = CG_MAGIC;
        mapsize = newcg->cg_nextfreeoff - newcg->cg_iusedoff;

	imapsize = (int) newcg->cg_freeoff - (int) newcg->cg_iusedoff;
	fmapsize = (int) newcg->cg_nextfreeoff - (int) newcg->cg_freeoff;

	if ((sblock.fs_flags & FS_FLAGS_UPDATED) == 0) {
#endif
	switch ((int)fs->fs_postblformat) {

	case FS_42POSTBLFMT:
		basesize = (char *)(&ocg->cg_btot[0]) - (char *)(&ocg->cg_link);
		sumsize = &ocg->cg_iused[0] - (char *)(&ocg->cg_btot[0]);
		mapsize = &ocg->cg_free[howmany(fs->fs_fpg, NBBY)] -
			(u_char *)&ocg->cg_iused[0];
#ifdef cdh
		imapsize = mapsize;
		fmapsize = 0;
#endif
		ocg->cg_magic = CG_MAGIC;
		savednrpos = fs->fs_nrpos;
		fs->fs_nrpos = 8;
		break;

	case FS_DYNAMICPOSTBLFMT:
		newcg->cg_btotoff =
		     &newcg->cg_space[0] - (u_char *)(&newcg->cg_link);
		newcg->cg_boff =
		    newcg->cg_btotoff + fs->fs_cpg * sizeof(long);
		newcg->cg_iusedoff = newcg->cg_boff +
		    fs->fs_cpg * fs->fs_nrpos * sizeof(short);
		newcg->cg_freeoff =
		    newcg->cg_iusedoff + howmany(fs->fs_ipg, NBBY);
		if (fs->fs_contigsumsize <= 0) {
			newcg->cg_nextfreeoff = newcg->cg_freeoff +
			    howmany(fs->fs_cpg * fs->fs_spc / NSPF(fs), NBBY);
		} else {
			newcg->cg_clustersumoff = newcg->cg_freeoff +
			    howmany(fs->fs_cpg * fs->fs_spc / NSPF(fs), NBBY) -
			    sizeof(long);
			newcg->cg_clustersumoff =
			    roundup(newcg->cg_clustersumoff, sizeof(long));
			newcg->cg_clusteroff = newcg->cg_clustersumoff +
			    (fs->fs_contigsumsize + 1) * sizeof(long);
			newcg->cg_nextfreeoff = newcg->cg_clusteroff +
			    howmany(fs->fs_cpg * fs->fs_spc / NSPB(fs), NBBY);
		}
		newcg->cg_magic = CG_MAGIC;
		basesize = (int) &newcg->cg_space[0] - (int) (&newcg->cg_link);
		sumsize = (int) newcg->cg_iusedoff - (int) newcg->cg_btotoff;
		mapsize = (int) newcg->cg_nextfreeoff - (int) newcg->cg_iusedoff;
#ifdef cdh
		imapsize = (int) newcg->cg_freeoff - (int) newcg->cg_iusedoff;
		fmapsize = (int) newcg->cg_nextfreeoff - (int) newcg->cg_freeoff;
		if (newcg->cg_magic != CG_MAGIC)
		    fmapsize = 0;
#endif
		break;

	default:
		errexit("UNKNOWN ROTATIONAL TABLE FORMAT %d\n",
			fs->fs_postblformat);
	}
#ifdef UFS_V1
	}
#endif
	bzero((char *)&idesc[0], sizeof idesc);
	for (i = 0; i < 3; i++) {
		idesc[i].id_type = ADDR;
		if (doinglevel2)
			idesc[i].id_fix = FIX;
	}
	bzero((char *)&cstotal, sizeof(struct csum));
	j = blknum(fs, fs->fs_size + fs->fs_frag - 1);
	for (i = fs->fs_size; i < j; i++)
		setbmap(i);
	for (c = 0; c < fs->fs_ncg; c++) {
#ifdef cdh
		chkabort();  /* Check for ^C entered */
		totalreads++;
#endif
		getblk(&cgblk, cgtod(fs, c), fs->fs_cgsize);

		if (!cg_chkmagic(cg))
			pfatal("CG %d: BAD MAGIC NUMBER\n", c);
		dbase = cgbase(fs, c);
		dmax = dbase + fs->fs_fpg;
		if (dmax > fs->fs_size)
			dmax = fs->fs_size;
		newcg->cg_time = cg->cg_time;
		newcg->cg_cgx = c;
#ifdef OLD_FSCK
		if (c == fs->fs_ncg - 1)
			newcg->cg_ncyl = fs->fs_ncyl % fs->fs_cpg;
		else
			newcg->cg_ncyl = fs->fs_cpg;
		newcg->cg_ndblk = dmax - dbase;
#else /* NetBSD 7.x fsck */
#if 1
		bcopy((char *)cg->cg_new_time, (char *)newcg->cg_new_time,
		      sizeof cg->cg_new_time);
		newcg->cg_initediblk = cg->cg_initediblk;
#endif
		newcg->cg_ndblk = dmax - dbase;
		if (c == fs->fs_ncg - 1) {
			/* Avoid fighting old fsck for this value.  Its never
			 * used outside of this check anyway.
			 */
			if ((fs->fs_flags & FS_FLAGS_UPDATED) == 0)
				newcg->cg_ncyl = fs->fs_ncyl % fs->fs_cpg;
			else
				newcg->cg_ncyl = howmany(newcg->cg_ndblk,
				    fs->fs_fpg / fs->fs_cpg);
		} else
			newcg->cg_ncyl = fs->fs_cpg;
		newcg->cg_niblk = fs->fs_ipg;
		newcg->cg_new_niblk = 0;
#endif
		if (fs->fs_contigsumsize > 0)
			newcg->cg_nclusterblks = newcg->cg_ndblk / fs->fs_frag;
		newcg->cg_cs.cs_ndir = 0;
		newcg->cg_cs.cs_nffree = 0;
		newcg->cg_cs.cs_nbfree = 0;
		newcg->cg_cs.cs_nifree = fs->fs_ipg;
		if (cg->cg_rotor < newcg->cg_ndblk)
			newcg->cg_rotor = cg->cg_rotor;
		else
			newcg->cg_rotor = 0;
		if (cg->cg_frotor < newcg->cg_ndblk)
			newcg->cg_frotor = cg->cg_frotor;
		else
			newcg->cg_frotor = 0;
#ifdef UFS_V1
                if (cg->cg_irotor >= 0 && cg->cg_irotor < fs->fs_ipg)
                        newcg->cg_irotor = cg->cg_irotor;
                else
                        newcg->cg_irotor = 0;
#else
		if (cg->cg_irotor < newcg->cg_niblk)
			newcg->cg_irotor = cg->cg_irotor;
		else
			newcg->cg_irotor = 0;
#endif
		bzero((char *)&newcg->cg_frsum[0], sizeof newcg->cg_frsum);
#ifdef cdh
		bzero((char *)&cg_blktot(newcg)[0],
		      (size_t)(sumsize + imapsize + fmapsize));
                bzero(cg_inosused(newcg), (size_t)(mapsize));
#else
		bzero((char *)&cg_blktot(newcg)[0],
		      (size_t)(sumsize + mapsize));
#endif
		if (fs->fs_postblformat == FS_42POSTBLFMT)
			ocg->cg_magic = CG_MAGIC;
		j = fs->fs_ipg * c;
		for (i = 0; i < fs->fs_ipg; j++, i++) {
			switch (statemap[j]) {

			case USTATE:
				break;

			case DSTATE:
			case DCLEAR:
			case DFOUND:
				newcg->cg_cs.cs_ndir++;
				/* fall through */

			case FSTATE:
			case FCLEAR:
				newcg->cg_cs.cs_nifree--;
				setbit(cg_inosused(newcg), i);
				break;

			default:
				if (j < ROOTINO)
					break;
				errexit("BAD STATE %d FOR INODE I=%d",
				    statemap[j], j);
			}
		}
		if (c == 0)
			for (i = 0; i < ROOTINO; i++) {
				setbit(cg_inosused(newcg), i);
				newcg->cg_cs.cs_nifree--;
			}

		for (i = 0, d = dbase;
		     d < dmax;
		     d += fs->fs_frag, i += fs->fs_frag) {

			frags = 0;
			for (j = 0; j < fs->fs_frag; j++) {
				if (testbmap(d + j))
					continue;
				setbit(cg_blksfree(newcg), i + j);
				frags++;
			}
			if (frags == fs->fs_frag) {
				newcg->cg_cs.cs_nbfree++;
#ifdef UFS_V1
				if (sumsize) {
				    j = cbtocylno(fs, i);
				    cg_blktot(newcg)[j]++;
				    cg_blks(fs, newcg, j)[cbtorpos(fs, i)]++;
				}
#else
				j = cbtocylno(fs, i);
				cg_blktot(newcg)[j]++;
				cg_blks(fs, newcg, j)[cbtorpos(fs, i)]++;
#endif
				if (fs->fs_contigsumsize > 0)
					setbit(cg_clustersfree(newcg),
					    i / fs->fs_frag);
			} else if (frags > 0) {
				newcg->cg_cs.cs_nffree += frags;
				blk = blkmap(fs, cg_blksfree(newcg), i);
				ffs_fragacct(fs, blk, newcg->cg_frsum, 1);
			}
		}
		if (fs->fs_contigsumsize > 0) {
			long *sump = cg_clustersum(newcg);
			u_char *mapp = cg_clustersfree(newcg);
			int map = *mapp++;
			int bit = 1;
			int run = 0;

			for (i = 0; i < newcg->cg_nclusterblks; i++) {
				if ((map & bit) != 0) {
					run++;
				} else if (run != 0) {
					if (run > fs->fs_contigsumsize)
						run = fs->fs_contigsumsize;
					sump[run]++;
					run = 0;
				}
				if ((i & (NBBY - 1)) != (NBBY - 1)) {
					bit <<= 1;
				} else {
					map = *mapp++;
					bit = 1;
				}
			}
			if (run != 0) {
				if (run > fs->fs_contigsumsize)
					run = fs->fs_contigsumsize;
				sump[run]++;
			}
		}
		cstotal.cs_nffree += newcg->cg_cs.cs_nffree;
		cstotal.cs_nbfree += newcg->cg_cs.cs_nbfree;
		cstotal.cs_nifree += newcg->cg_cs.cs_nifree;
		cstotal.cs_ndir += newcg->cg_cs.cs_ndir;
		cs = &fs->fs_cs(fs, c);
#ifdef cdh
		if (bcmp((char *)&newcg->cg_cs, (char *)cs, sizeof *cs) != 0) {
		    if (debug) {
			    printf("cg %d: nffree: %d/%d nbfree %d/%d"
				    " nifree %d/%d ndir %d/%d\n",
				    c, cs->cs_nffree,newcg->cg_cs.cs_nffree,
				    cs->cs_nbfree,newcg->cg_cs.cs_nbfree,
				    cs->cs_nifree,newcg->cg_cs.cs_nifree,
				    cs->cs_ndir,newcg->cg_cs.cs_ndir);
		    }
		    if (dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
			bcopy((char *)&newcg->cg_cs, (char *)cs, sizeof *cs);
			sbdirty();
		    }
		}
#else
		if (bcmp((char *)&newcg->cg_cs, (char *)cs, sizeof *cs) != 0 &&
		    dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
			bcopy((char *)&newcg->cg_cs, (char *)cs, sizeof *cs);
			sbdirty();
		}
#endif
		if (doinglevel1) {
			bcopy((char *)newcg, (char *)cg, (size_t)fs->fs_cgsize);
			cgdirty();
			continue;
		}

#ifdef cdh
		if (bcmp(cg_inosused(newcg), cg_inosused(cg), imapsize) != 0) {
		    bprnt(cg_inosused(newcg), cg_inosused(cg), imapsize, 'I');
		    if (dofix(&idesc[1], "BLK(S) MISSING IN CG INODE BIT MAPS")) {
			bcopy(cg_inosused(newcg), cg_inosused(cg),
			      (size_t)imapsize);
			cgdirty();
		    }
		}

		if (fmapsize &&
		    bcmp(cg_blksfree(newcg), cg_blksfree(cg), fmapsize) != 0) {
		    bprnt(cg_blksfree(newcg), cg_blksfree(cg), fmapsize, 'I');
		    if (debug) {
			int       pos;
			uint8_t *ptr1 = (uint8_t *)cg_blksfree(cg);
			uint8_t *ptr2 = (uint8_t *)cg_blksfree(newcg);
			printf("CG%x frags\n", c);
			for (pos = 0; pos < fmapsize; pos++) {
			    if (*ptr1 != *ptr2)
				printf(" %x:%02x!=%02x", pos, *ptr1, *ptr2);
			    ptr1++;
			    ptr2++;
			}
			printf("\n");
		    }
		    if (dofix(&idesc[1], "BLK(S) MISSING IN CG FRAG BIT MAPS")) {
			bcopy(cg_blksfree(newcg), cg_blksfree(cg),
			      (size_t)fmapsize);
			cgdirty();
		    }
		}
#else
		if (bcmp(cg_inosused(newcg), cg_inosused(cg), mapsize) != 0) {
		    bprnt(cg_inosused(newcg), cg_inosused(cg), mapsize, 'I');
		    if (dofix(&idesc[1], "BLK(S) MISSING IN CG INODE/FRAG BIT MAPS")) {
			bcopy(cg_inosused(newcg), cg_inosused(cg),
			      (size_t)mapsize);
			cgdirty();
		    }
		}
#endif

		newcg->cg_time = cg->cg_time;
#ifdef UFS_V1
		bcopy((char *)cg->cg_new_time, (char *)newcg->cg_new_time,
		      sizeof cg->cg_new_time);
		newcg->cg_new_niblk = cg->cg_new_niblk;
		newcg->cg_initediblk = cg->cg_initediblk;
#endif
		if (bcmp((char *)newcg, (char *)cg, basesize) != 0) {
		    bprnt((char *)newcg, (char *)cg, basesize, 'C');
#ifdef cdh
		    if (debug) {
			int       pos;
			uint32_t *ptr1 = (uint32_t *)cg;
			uint32_t *ptr2 = (uint32_t *)newcg;
			printf("CG%x summary\n", c);
			for (pos = 0; pos < basesize; pos += 4) {
			    if (*ptr1 != *ptr2)
				printf(" %x:%08x!=%08x", pos, *ptr1, *ptr2);
			    ptr1++;
			    ptr2++;
			}
			printf("\n");
			printf("CG offsets: cg_cs=%x cg_frsum=%x "
			       "cg_btotoff=%x cg_initediblk=%x\n",
			       offsetof (struct cg, cg_cs),
			       offsetof (struct cg, cg_frsum),
			       offsetof (struct cg, cg_btotoff),
			       offsetof (struct cg, cg_initediblk));
		    }
#endif
		    if (dofix(&idesc[2], "CYL GROUP SUMMARY INFORMATION BAD")) {
			bcopy((char *)newcg, (char *)cg, (size_t)basesize);
			bcopy((char *)&cg_blktot(newcg)[0],
			      (char *)&cg_blktot(cg)[0], (size_t)sumsize);
			cgdirty();
		    }
		}
		if (bcmp((char *)&cg_blktot(newcg)[0],
                            (char *)&cg_blktot(cg)[0], sumsize) != 0) {
		    bprnt((char *)&cg_blktot(newcg)[0],
			  (char *)&cg_blktot(cg)[0], sumsize, 'B');
#ifdef cdh
		    if (debug) {
			int       pos;
			uint32_t *ptr1 = (uint32_t *)&cg_blktot(cg)[0];
			uint32_t *ptr2 = (uint32_t *)&cg_blktot(newcg)[0];
			printf("CG%x blktot\n", c);
			for (pos = 0; pos < basesize; pos += 4) {
			    if (*ptr1 != *ptr2)
				printf(" %x:%08x!=%08x", pos, *ptr1, *ptr2);
			    ptr1++;
			    ptr2++;
			}
			printf("\n");
		    }
#endif
		    if (dofix(&idesc[2], "CG BLK TOTAL SUMMARY INFORMATION BAD")) {
			bcopy((char *)newcg, (char *)cg, (size_t)basesize);
			bcopy((char *)&cg_blktot(newcg)[0],
			      (char *)&cg_blktot(cg)[0], (size_t)sumsize);
			cgdirty();
		    }
		}
	}
	if (fs->fs_postblformat == FS_42POSTBLFMT)
		fs->fs_nrpos = savednrpos;
	if (verbose)
#ifdef UFS_V1
	    if (sblock.fs_flags & FS_FLAGS_UPDATED)
		printf("total: ondisk(ffree=%d,bfree=%d,ifree=%d,dir=%d)\n",
		       fs->fs_new_cstotal.cs_nffree[is_big_endian],
		       fs->fs_new_cstotal.cs_nbfree[is_big_endian],
		       fs->fs_new_cstotal.cs_nifree[is_big_endian],
		       fs->fs_new_cstotal.cs_ndir[is_big_endian]);
	    else
#endif
	    printf("total: ondisk(ffree=%d,bfree=%d,ifree=%d,dir=%d)\n",
		   fs->fs_cstotal.cs_nffree, fs->fs_cstotal.cs_nbfree,
		   fs->fs_cstotal.cs_nifree, fs->fs_cstotal.cs_ndir);

#ifdef UFS_V1
	if (sblock.fs_flags & FS_FLAGS_UPDATED)
	    if ((cstotal.cs_ndir !=
		 fs->fs_new_cstotal.cs_ndir[is_big_endian]) ||
		(cstotal.cs_nbfree !=
		 fs->fs_new_cstotal.cs_nbfree[is_big_endian]) ||
		(cstotal.cs_nifree !=
		 fs->fs_new_cstotal.cs_nifree[is_big_endian]) ||
		(cstotal.cs_nffree !=
		 fs->fs_new_cstotal.cs_nffree[is_big_endian])) {
		if (verbose)
		    printf("  != computed(ffree=%d,bfree=%d,ifree=%d,dir=%d)\n",
			    cstotal.cs_nffree, cstotal.cs_nbfree,
			    cstotal.cs_nifree, cstotal.cs_ndir);
		if (dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
		    cstotal.cs_ndir = fs->fs_new_cstotal.cs_ndir[is_big_endian];
		    cstotal.cs_nbfree =
			fs->fs_new_cstotal.cs_nbfree[is_big_endian];
		    cstotal.cs_nifree =
			fs->fs_new_cstotal.cs_nifree[is_big_endian];
		    cstotal.cs_nffree =
			fs->fs_new_cstotal.cs_nffree[is_big_endian];
		    fs->fs_ronly = 0;
		    fs->fs_fmod = 0;
		    sbdirty();
		}
	    }
	else if ((sblock.fs_flags & FS_FLAGS_UPDATED) == 0)
#endif
	if (bcmp((char *)&cstotal, (char *)&fs->fs_cstotal, sizeof *cs) != 0) {
	    if (verbose)
		printf("  != computed(ffree=%d,bfree=%d,ifree=%d,dir=%d)\n",
			cstotal.cs_nffree, cstotal.cs_nbfree,
			cstotal.cs_nifree, cstotal.cs_ndir);
	    if (dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
		bcopy((char *)&cstotal, (char *)&fs->fs_cstotal, sizeof *cs);
		fs->fs_ronly = 0;
		fs->fs_fmod = 0;
		sbdirty();
	    }
	}
	free(buf);
}

int bprnt(ptr1, ptr2, size, ch)
unsigned char *ptr1;
unsigned char *ptr2;
int size;
char ch;
{
	register int index;

	if (!verbose)
		return(1);

#ifdef REVERSE
	printf("%c  ondisk:", ch);
	for (ptr1 += size, index = 0; index < size; index++) {
		printf("%02x", ((int) (*ptr1)) & 255);
		ptr1--;
	}
	printf("\n%ccomputed:", ch);
	for (ptr2 += size, index = 0; index < size; index++) {
		printf("%02x", ((int) (*ptr2)) & 255);
		ptr2--;
	}
#else
	printf("%c  ondisk:", ch);
	for (index = 0; index < size; index++) {
		printf("%02x", ((int) (*ptr1)) & 255);
		ptr1++;
	}
	printf("\n%ccomputed:", ch);
	for (index = 0; index < size; index++) {
		printf("%02x", ((int) (*ptr2)) & 255);
		ptr2++;
	}
#endif
	printf("\n");
	return(1);
}
