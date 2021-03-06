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

const char *version = "\0$VER: fsck 1.13 (08-Feb-2018) ? UCB";

#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1980, 1986, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
/*static char sccsid[] = "from: @(#)main.c	8.2 (Berkeley) 1/23/94";*/
static char *rcsid = "$Id: main.c,v 1.13 1994/06/08 19:00:24 mycroft Exp $";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <ufs/dinode.h>
#include <ufs/fs.h>
#ifdef cdh
#define EXTERN
#else
#include <fstab.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "fsck.h"
#ifdef __linux
#include <unistd.h>
#include <getopt.h>
#endif

#ifdef cdh
static int argtoi(int flag, char *req, char *str, int base);
#endif
void	catch(), catchquit(), voidquit();
int	returntosingle;
int	verbose = 0;

int
main(argc, argv)
	int	argc;
	char	*argv[];
{
	int ch;
	int ret, maxrun = 0;
	extern int docheck(), checkfilesys();
	extern char *optarg, *blockcheck();
	extern int optind;

#ifdef AMIGA
#ifndef _DCC
	if (dio_checkstack(30000))
	    exit(1);
#endif
#else
	sync();
#endif
	while ((ch = getopt(argc, argv, "vdpnNyYb:c:l:m:")) != EOF) {
		switch (ch) {
		case 'v':
			verbose++;
			break;
		case 'p':
			preen++;
			break;

		case 'b':
			bflag = argtoi('b', "number", optarg, 10);
			printf("Alternate super block location: %d\n", bflag);
			break;

		case 'c':
			cvtlevel = argtoi('c', "conversion level", optarg, 10);
			break;

		case 'd':
			debug++;
			break;

		case 'l':
			maxrun = argtoi('l', "number", optarg, 10);
			break;

		case 'm':
			lfmode = argtoi('m', "mode", optarg, 8);
			if (lfmode &~ 07777)
				errexit("bad mode to -m: %o\n", lfmode);
			printf("** lost+found creation mode %o\n", lfmode);
			break;

		case 'n':
		case 'N':
			nflag++;
			yflag = 0;
			break;

		case 'y':
		case 'Y':
			yflag++;
			nflag = 0;
			break;

		default:
usage:
#ifdef cdh
			fprintf(stderr, "BSD 4.4 fsck usage: %s [-vpbcdlmny] <device>\n", argv[0]);
#else
			fprintf(stderr, "BSD 4.4 fsck usage: %s -[vpbcdlmny]\n", argv[0]);
#endif
			fprintf(stderr, "   b=sblock #   p=preen     y=always yes    n=always no         c=convert format\n");
			fprintf(stderr, "   l=maxrun     v=verbose   d=debug         m=lost+found mode\n");
			errexit("-%c is not an option\n", ch);
		}
	}
	argc -= optind;
	argv += optind;
#ifdef unix
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, catch);
	if (preen)
		(void)signal(SIGQUIT, catchquit);
#endif
	if (argc) {
		while (argc-- > 0) {
#ifdef cdh
			(void)checkfilesys(*argv++, 0, 0L, 0);
#ifdef AMIGA
			dio_inhibit(0);
			dio_close();
#endif
#else
			(void)checkfilesys(blockcheck(*argv++), 0, 0L, 0);
#endif
		}
		exit(0);
	}
#ifdef AMIGA
	ch = ' ';
	argv -= optind;
	goto usage;
#else
	ret = checkfstab(preen, maxrun, docheck, checkfilesys);
#endif
	if (returntosingle)
		exit(2);
	exit(ret);
}

int
argtoi(flag, req, str, base)
	int flag;
	char *req, *str;
	int base;
{
	char *cp;
	int ret;

	ret = (int)strtol(str, &cp, base);
	if (cp == str || *cp)
		errexit("-%c flag requires a %s\n", flag, req);
	return (ret);
}

/*
 * Determine whether a filesystem should be checked.
 */
int
docheck(fsp)
	register struct fstab *fsp;
{
#ifdef cdh
	return (1);
#else
	if (strcmp(fsp->fs_vfstype, "ufs") ||
	    (strcmp(fsp->fs_type, FSTAB_RW) &&
	     strcmp(fsp->fs_type, FSTAB_RO)) ||
	    fsp->fs_passno == 0)
		return (0);
	return (1);
#endif
}

/*
 * Check the specified filesystem.
 */
/* ARGSUSED */
int
checkfilesys(filesys, mntpt, auxdata, child)
	char *filesys, *mntpt;
	long auxdata;
{
	daddr_t n_ffree, n_bfree;
	struct dups *dp;
	struct zlncnt *zlnp;
	int cylno;
#ifdef UFS_V1
	unsigned long n_ifree;
#endif

#ifdef unix
	if (preen && child)
		(void)signal(SIGQUIT, voidquit);
#endif
	cdevname = filesys;
	if (debug && preen)
		pwarn("starting\n");
	if (setup(filesys) == 0) {
		if (preen)
			pfatal("CAN'T CHECK FILE SYSTEM.");
		return (0);
	}
	/*
	 * 1: scan inodes tallying blocks used
	 */
	if (preen == 0) {
		printf("** Last Mounted on %s\n", sblock.fs_fsmnt);
		if (hotroot)
			printf("** Root file system\n");
		printf("** Phase 1 - Check Blocks and Sizes\n");
	}
	pass1();

	/*
	 * 1b: locate first references to duplicates, if any
	 */
	if (duplist) {
		if (preen)
			pfatal("INTERNAL ERROR: dups with -p");
		printf("** Phase 1b - Rescan For More DUPS\n");
		pass1b();
	}

	/*
	 * 2: traverse directories from root to mark all connected directories
	 */
	if (preen == 0)
		printf("** Phase 2 - Check Pathnames\n");
	pass2();

	/*
	 * 3: scan inodes looking for disconnected directories
	 */
	if (preen == 0)
		printf("** Phase 3 - Check Connectivity\n");
	pass3();

	/*
	 * 4: scan inodes looking for disconnected files; check reference counts
	 */
	if (preen == 0)
		printf("** Phase 4 - Check Reference Counts\n");
	pass4();

	/*
	 * 5: check and repair resource counts in cylinder groups
	 */
	if (preen == 0)
		printf("** Phase 5 - Check Cyl groups\n");
	pass5();

	/*
	 * print out summary statistics
	 */
#ifdef UFS_V1
	if (sblock.fs_flags & FS_FLAGS_UPDATED) {
	    n_ffree = sblock.fs_new_cstotal.cs_nffree[is_big_endian];
	    n_bfree = sblock.fs_new_cstotal.cs_nbfree[is_big_endian];
	    n_ifree = sblock.fs_new_cstotal.cs_nifree[is_big_endian];
	} else {
	    n_ffree = sblock.fs_cstotal.cs_nffree;
	    n_bfree = sblock.fs_cstotal.cs_nbfree;
	    n_ifree = sblock.fs_cstotal.cs_nifree;
	}
#else
	n_ffree = sblock.fs_cstotal.cs_nffree;
	n_bfree = sblock.fs_cstotal.cs_nbfree;
#endif
	pwarn("%ld files, %ld used, %ld free ",
	    n_files, n_blks, n_ffree + sblock.fs_frag * n_bfree);
	printf("(%ld frags, %ld blocks, %d.%d%% fragmentation)\n",
	    n_ffree, n_bfree, (n_ffree * 100) / sblock.fs_dsize,
	    ((n_ffree * 1000 + sblock.fs_dsize / 2) / sblock.fs_dsize) % 10);
#ifdef UFS_V1
	if (debug &&
	    (n_files -= maxino - ROOTINO - n_ifree))
		printf("%ld files missing\n", n_files);
#else
	if (debug &&
	    (n_files -= maxino - ROOTINO - sblock.fs_cstotal.cs_nifree))
		printf("%ld files missing\n", n_files);
#endif
	if (debug) {
		n_blks += sblock.fs_ncg *
			(cgdmin(&sblock, 0) - cgsblock(&sblock, 0));
		n_blks += cgsblock(&sblock, 0) - cgbase(&sblock, 0);
		n_blks += howmany(sblock.fs_cssize, sblock.fs_fsize);
		if (n_blks -= maxfsblock - (n_ffree + sblock.fs_frag * n_bfree))
			printf("%ld blocks missing\n", n_blks);
		if (duplist != NULL) {
			printf("The following duplicate blocks remain:");
			for (dp = duplist; dp; dp = dp->next)
				printf(" %ld,", dp->dup);
			printf("\n");
		}
		if (zlnhead != NULL) {
			printf("The following zero link count inodes remain:");
			for (zlnp = zlnhead; zlnp; zlnp = zlnp->next)
				printf(" %lu,", zlnp->zlncnt);
			printf("\n");
		}
	}
	zlnhead = (struct zlncnt *)0;
	duplist = (struct dups *)0;
	muldup = (struct dups *)0;
	inocleanup();
	if (fsmodified) {
		(void)time(&sblock.fs_time);
		sbdirty();
	}
	if (cvtlevel && sblk.b_dirty) {
		/*
		 * Write out the duplicate super blocks
		 */
		for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
			bwrite((char *)&sblock,
			    fsbtodb(&sblock, cgsblock(&sblock, cylno)), SBSIZE);
	}
	ckfini();
	free(blockmap);
	free(statemap);
	free((char *)lncntp);
	if (!fsmodified)
		return (0);
	if (!preen)
		printf("\n***** FILE SYSTEM WAS MODIFIED *****\n");
#ifndef cdh
	if (hotroot) {
		struct statfs stfs_buf;
		/*
		 * We modified the root.  Do a mount update on
		 * it, unless it is read-write, so we can continue.
		 */
		if (statfs("/", &stfs_buf) == 0) {
			long flags = stfs_buf.f_flags;
			struct ufs_args args;
			int ret;

			if (flags & MNT_RDONLY) {
				args.fspec = 0;
				args.export.ex_flags = 0;
				args.export.ex_root = 0;
				flags |= MNT_UPDATE | MNT_RELOAD;
				ret = mount(MOUNT_UFS, "/", flags, &args);
				if (ret == 0)
					return(0);
			}
		}
		if (!preen)
			printf("\n***** REBOOT NOW *****\n");
		sync();
		return (4);
	}
#endif
	return (0);
}
