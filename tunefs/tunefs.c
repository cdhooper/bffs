/*	$NetBSD: tunefs.c,v 1.10 1995/03/18 15:01:31 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

const char *version = "\0$VER: tunefs 1.10 (08-Feb-2018) ? UCB";

#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)tunefs.c	8.2 (Berkeley) 4/19/94";
#else
static char rcsid[] = "$NetBSD: tunefs.c,v 1.10 1995/03/18 15:01:31 cgd Exp $";
#endif
#endif /* not lint */

/*
 * tunefs: change layout parameters to an existing file system.
 */
#include <sys/param.h>
#include <sys/stat.h>

#ifdef cdh
#include <ufs/fs.h>
#else
#include <ufs/ffs/fs.h>
#endif

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#ifdef AMIGA
#include "ami_stat.h"
#include <amiga_device.h>
int break_abort(void);
void error_exit(int err);
#else
#include <fstab.h>
#endif
#include <stdio.h>
#include <paths.h>
#include <stdlib.h>
#include <unistd.h>


/* the optimization warning string template */
#define	OPTWARN	"should optimize for %s with minfree %s %d%%"

union {
	struct	fs sb;
	char pad[MAXBSIZE];
} sbun;
#define	sblock sbun.sb

int fi;
long dev_bsize = 1;

#ifdef AMIGA
void fbwrite(char *, daddr_t, int);
int fbread(char *, daddr_t, int);
#else
void bwrite(char *, daddr_t, int);
int bread(char *, daddr_t, int);
#endif
void getsb(struct fs *, char *);
void usage __P((void));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	char *cp, *special, *name;
#ifndef AMIGA
	struct stat st;
	struct fstab *fs;
	char device[MAXPATHLEN];
#endif
	int i;
	int Aflag = 0;
	char *chg[2];

	argc--, argv++;
	if (argc < 2)
		usage();
	special = argv[argc - 1];
#ifndef AMIGA
	fs = getfsfile(special);
	if (fs)
		special = fs->fs_spec;
again:
	if (stat(special, &st) < 0) {
		if (*special != '/') {
			if (*special == 'r')
				special++;
			(void)sprintf(device, "%s/%s", _PATH_DEV, special);
			special = device;
			goto again;
		}
		err(1, "%s", special);
	}

	if (!S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
#ifdef cdh
		fprintf(stderr, "warning: %s: not a block or character device\n", special);
#else
		errx(10, "%s: not a block or character device", special);
#endif
#endif

	getsb(&sblock, special);
	for (; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
		for (cp = &argv[0][1]; *cp; cp++)
			switch (*cp) {

			case 'A':
				Aflag++;
				continue;

			case 'a':
				name = "maximum contiguous block count";
				if (argc < 1)
					errx(10, "-a: missing %s", name);
				argc--, argv++;
				i = atoi(*argv);
				if (i < 1)
					errx(10, "%s must be >= 1 (was %s)",
					    name, *argv);
				warnx("%s changes from %d to %d",
				    name, sblock.fs_maxcontig, i);
				sblock.fs_maxcontig = i;
				continue;

			case 'd':
				name =
				   "rotational delay between contiguous blocks";
				if (argc < 1)
					errx(10, "-d: missing %s", name);
				argc--, argv++;
				i = atoi(*argv);
				warnx("%s changes from %dms to %dms",
				    name, sblock.fs_rotdelay, i);
				sblock.fs_rotdelay = i;
				continue;

			case 'e':
				name =
				  "maximum blocks per file in a cylinder group";
				if (argc < 1)
					errx(10, "-e: missing %s", name);
				argc--, argv++;
				i = atoi(*argv);
				if (i < 1)
					errx(10, "%s must be >= 1 (was %s)",
					    name, *argv);
				warnx("%s changes from %d to %d",
				    name, sblock.fs_maxbpg, i);
				sblock.fs_maxbpg = i;
				continue;

			case 'm':
				name = "minimum percentage of free space";
				if (argc < 1)
					errx(10, "-m: missing %s", name);
				argc--, argv++;
				i = atoi(*argv);
				if (i < 0 || i > 99)
					errx(10, "bad %s (%s)", name, *argv);
				warnx("%s changes from %d%% to %d%%",
				    name, sblock.fs_minfree, i);
				sblock.fs_minfree = i;
				if (i >= MINFREE &&
				    sblock.fs_optim == FS_OPTSPACE)
					warnx(OPTWARN, "time", ">=", MINFREE);
				if (i < MINFREE &&
				    sblock.fs_optim == FS_OPTTIME)
					warnx(OPTWARN, "space", "<", MINFREE);
				continue;

			case 'o':
				name = "optimization preference";
				if (argc < 1)
					errx(10, "-o: missing %s", name);
				argc--, argv++;
				chg[FS_OPTSPACE] = "space";
				chg[FS_OPTTIME] = "time";
				if (strcmp(*argv, chg[FS_OPTSPACE]) == 0)
					i = FS_OPTSPACE;
				else if (strcmp(*argv, chg[FS_OPTTIME]) == 0)
					i = FS_OPTTIME;
				else
					errx(10, "bad %s (options are `space' or `time')",
					    name);
				if (sblock.fs_optim == i) {
					warnx("%s remains unchanged as %s",
					    name, chg[i]);
					continue;
				}
				warnx("%s changes from %s to %s",
				    name, chg[sblock.fs_optim], chg[i]);
				sblock.fs_optim = i;
				if (sblock.fs_minfree >= MINFREE &&
				    i == FS_OPTSPACE)
					warnx(OPTWARN, "time", ">=", MINFREE);
				if (sblock.fs_minfree < MINFREE &&
				    i == FS_OPTTIME)
					warnx(OPTWARN, "space", "<", MINFREE);
				continue;

			default:
				usage();
			}
	}
	if (argc != 1)
		usage();
	bwrite((char *)&sblock, (daddr_t)SBOFF / dev_bsize, SBSIZE);
	if (Aflag)
		for (i = 0; i < sblock.fs_ncg; i++)
			bwrite((char *)&sblock,
			       fsbtodb(&sblock, cgsblock(&sblock, i)), SBSIZE);
#ifdef AMIGA
	if (fi >= 0)
		close(fi);
	error_exit(0);
#else
	close(fi);
	exit(0);
#endif
}

void
usage()
{

	fprintf(stderr, "Usage: tunefs tuneup-options special-device\n");
	fprintf(stderr, "where tuneup-options are:\n");
	fprintf(stderr, "\t-a maximum contiguous blocks\n");
	fprintf(stderr, "\t-d rotational delay between contiguous blocks\n");
	fprintf(stderr, "\t-e maximum blocks per file in a cylinder group\n");
	fprintf(stderr, "\t-m minimum percentage of free space\n");
	fprintf(stderr, "\t-o optimization preference (`space' or `time')\n");
	exit(2);
}

void
getsb(fs, file)
	register struct fs *fs;
	char *file;
{

#ifdef AMIGA
	extern int DEV_BSIZE;
	if (dio_open(file) == 0) {
		onbreak(break_abort);
		dio_inhibit(1);
		fi = -1;
	} else {
		fi = open(file, 2);
		if (fi < 0)
			err(3, "cannot open %s", file);
	}
	dev_bsize = DEV_BSIZE;
	if (bread((char *) fs, (daddr_t)SBOFF / dev_bsize, SBSIZE))
		err(4, "%s: bad super block", file);
#else
	fi = open(file, 2);
	if (fi < 0)
		err(3, "cannot open %s", file);
	if (bread((char *) fs, (daddr_t)SBOFF, SBSIZE))
		err(4, "%s: bad super block", file);
#endif
	if (fs->fs_magic != FS_MAGIC)
		err(5, "%s: bad magic number", file);
	dev_bsize = fs->fs_fsize / fsbtodb(fs, 1);
#ifdef AMIGA
	dio_assign_bsize(dev_bsize);
#endif
}

#ifdef AMIGA
void fbwrite(buf, blk, size)
#else
void bwrite(buf, blk, size)
#endif
	daddr_t blk;
	char *buf;
	int size;
{

	if (lseek(fi, (off_t)blk * dev_bsize, SEEK_SET) < 0)
		err(6, "FS SEEK");
	if (write(fi, buf, size) != size)
		err(7, "FS WRITE");
}

#ifdef AMIGA
int fbread(buf, blk, cnt)
#else
int bread(buf, blk, cnt)
#endif
	daddr_t blk;
	char *buf;
	int cnt;
{
	int i;

	if (lseek(fi, (off_t)blk * dev_bsize, SEEK_SET) < 0)
		return(1);
	if ((i = read(fi, buf, cnt)) != cnt) {
		for(i=0; i<sblock.fs_bsize; i++)
			buf[i] = 0;
		return (1);
	}
	return (0);
}
