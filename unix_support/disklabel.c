/*
 * Copyright (c) 1983, 1987 Regents of the University of California.
 * All rights reserved.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)disklabel.c	5.17 (Berkeley) 2/23/91";
#endif /* LIBC_SCCS and not lint */

#define DKTYPENAMES
#include <stdio.h>
#include "sys/param.h"
#include "ufs/fs.h"
#include "sys/disklabel.h"
#include "paths.h"
#include <sys/file.h>

static	char *dgetstr();
static	dgetent();
static	dnamatch();
static	dgetnum();
static	dgetflag();
static	gettype();
static	error();
char bootarea[BBSIZE];

/* next block added by cdh */
#define errno 0
#define EFTYPE 0
extern char bootarea[];
extern int  DEV_BSIZE;
int force_autolabel = 0;

struct disklabel *
getdiskbyname(name)
/* const removed by cdh */
	char *name;
{
	static struct	disklabel disk;
	static char	boot[BUFSIZ];
	char	*buf;
	char	*localbuf;
	char	*cp, *cq;	/* can't be register */
	register struct	disklabel *dp = &disk;
	register struct partition *pp;
	char	p, max, psize[3], pbsize[3],
		pfsize[3], poffset[3], ptype[3];
	u_long	*dx;

	buf = (char *) malloc(BUFSIZ);
	if (buf == NULL) {
		fprintf(stderr, "fatal: unable to allocate memory\n");
		exit(2);
	}

	if (dgetent(buf, name) <= 0) {
		free(buf);
		return ((struct disklabel *)0);
	}

	localbuf = (char *) malloc(BUFSIZ);
	if (localbuf == NULL) {
		fprintf(stderr, "fatal: unable to allocate memory\n");
		exit(2);
	}

	bzero((char *)&disk, sizeof(disk));
	/*
	 * typename
	 */
	cq = dp->d_typename;
	cp = buf;
	while (cq < dp->d_typename + sizeof(dp->d_typename) - 1 &&
	    (*cq = *cp) && *cq != '|' && *cq != ':')
		cq++, cp++;
	*cq = '\0';
	/*
	 * boot name (optional)  xxboot, bootxx
	 */
	cp = boot;
	dp->d_boot0 = dgetstr("b0", &cp);
	dp->d_boot1 = dgetstr("b1", &cp);
	cp = localbuf;
	cq = dgetstr("ty", &cp);
	if (cq && strcmp(cq, "removable") == 0)
		dp->d_flags |= D_REMOVABLE;
	else  if (cq && strcmp(cq, "simulated") == 0)
		dp->d_flags |= D_RAMDISK;
	if (dgetflag("sf"))
		dp->d_flags |= D_BADSECT;

#define getnumdflt(field, dname, dflt) \
	{ int f = dgetnum(dname); \
	(field) = f == -1 ? (dflt) : f; }

	getnumdflt(dp->d_secsize, "se", DEV_BSIZE);
	dp->d_ntracks = dgetnum("nt");
	dp->d_nsectors = dgetnum("ns");
	dp->d_ncylinders = dgetnum("nc");
	cq = dgetstr("dt", &cp);
	if (cq)
		dp->d_type = gettype(cq, dktypenames);
	else
		getnumdflt(dp->d_type, "dt", 0);
	getnumdflt(dp->d_secpercyl, "sc", dp->d_nsectors * dp->d_ntracks);
	getnumdflt(dp->d_secperunit, "su", dp->d_secpercyl * dp->d_ncylinders);
	getnumdflt(dp->d_rpm, "rm", 3600);
	getnumdflt(dp->d_interleave, "il", 1);
	getnumdflt(dp->d_trackskew, "sk", 0);
	getnumdflt(dp->d_cylskew, "cs", 0);
	getnumdflt(dp->d_headswitch, "hs", 0);
	getnumdflt(dp->d_trkseek, "ts", 0);
	getnumdflt(dp->d_bbsize, "bs", BBSIZE);
	getnumdflt(dp->d_sbsize, "sb", SBSIZE);
	strcpy(psize, "px");
	strcpy(pbsize, "bx");
	strcpy(pfsize, "fx");
	strcpy(poffset, "ox");
	strcpy(ptype, "tx");
	max = 'a' - 1;
	pp = &dp->d_partitions[0];
	for (p = 'a'; p < 'a' + MAXPARTITIONS; p++, pp++) {
		psize[1] = pbsize[1] = pfsize[1] = poffset[1] = ptype[1] = p;
		pp->p_size = dgetnum(psize);
		if (pp->p_size == -1)
			pp->p_size = 0;
		else {
			pp->p_offset = dgetnum(poffset);
			getnumdflt(pp->p_fsize, pfsize, 0);
			if (pp->p_fsize)
				pp->p_frag = dgetnum(pbsize) / pp->p_fsize;
			getnumdflt(pp->p_fstype, ptype, 0);
			if (pp->p_fstype == 0 && (cq = dgetstr(ptype, &cp)))
				pp->p_fstype = gettype(cq, fstypenames);
			max = p;
		}
	}
	dp->d_npartitions = max + 1 - 'a';
	(void)strcpy(psize, "dx");
	dx = dp->d_drivedata;
	for (p = '0'; p < '0' + NDDATA; p++, dx++) {
		psize[1] = p;
		getnumdflt(*dx, psize, 0);
	}
	dp->d_magic = DISKMAGIC;
	dp->d_magic2 = DISKMAGIC;
	free(buf);
	free(localbuf);
	return (dp);
}

#include <ctype.h>

static	char *tbuf;
static	char *dskip();
static	char *ddecode();

/*
 * Get an entry for disk name in buffer bp,
 * from the diskcap file.  Parse is very rudimentary;
 * we just notice escaped newlines.
 */
static
dgetent(bp, name)
	char *bp, *name;
{
	register char *cp;
	register int c;
	register int i = 0, cnt = 0;
	char *ibuf;
	int tf;

	tbuf = bp;
	tf = open(_PATH_DISKTAB, 0);
	if (tf < 0) {
		perror(_PATH_DISKTAB);
/*		error(errno); */
		return (-1);
	}

	ibuf = (char *) malloc(BUFSIZ);
	if (ibuf == NULL) {
		fprintf(stderr, "fatal: unable to allocate memory\n");
		exit(2);
	}

	for (;;) {
		cp = bp;
		for (;;) {
			if (i == cnt) {
				cnt = read(tf, ibuf, BUFSIZ); /* disktab file */
				if (cnt <= 0) {
					perror("read");
/*					error(errno); */
					close(tf);
					free(ibuf);
					return (0);
				}
				i = 0;
			}
			c = ibuf[i++];
			if (c == '\n') {
				if (cp > bp && cp[-1] == '\\'){
					cp--;
					continue;
				}
				break;
			}
			if (cp >= bp+BUFSIZ) {
				error(EFTYPE);
				break;
			} else
				*cp++ = c;
		}
		*cp = 0;

		/*
		 * The real work for the match.
		 */
		if (dnamatch(name)) {
			close(tf);
			free(ibuf);
			return (1);
		}
	}
	free(ibuf);
}

/*
 * Dnamatch deals with name matching.  The first field of the disktab
 * entry is a sequence of names separated by |'s, so we compare
 * against each such name.  The normal : terminator after the last
 * name (before the first field) stops us.
 */
static
dnamatch(np)
	char *np;
{
	register char *Np, *Bp;

	Bp = tbuf;
	if (*Bp == '#')
		return (0);
	for (;;) {
		for (Np = np; *Np && *Bp == *Np; Bp++, Np++)
			continue;
		if (*Np == 0 && (*Bp == '|' || *Bp == ':' || *Bp == 0))
			return (1);
		while (*Bp && *Bp != ':' && *Bp != '|')
			Bp++;
		if (*Bp == 0 || *Bp == ':')
			return (0);
		Bp++;
	}
}

/*
 * Skip to the next field.  Notice that this is very dumb, not
 * knowing about \: escapes or any such.  If necessary, :'s can be put
 * into the diskcap file in octal.
 */
static char *
dskip(bp)
	register char *bp;
{

	while (*bp && *bp != ':')
		bp++;
	if (*bp == ':')
		bp++;
	return (bp);
}

/*
 * Return the (numeric) option id.
 * Numeric options look like
 *	li#80
 * i.e. the option string is separated from the numeric value by
 * a # character.  If the option is not found we return -1.
 * Note that we handle octal numbers beginning with 0.
 */
static
dgetnum(id)
	char *id;
{
	register int i, base;
	register char *bp = tbuf;

	for (;;) {
		bp = dskip(bp);
		if (*bp == 0)
			return (-1);
		if (*bp++ != id[0] || *bp == 0 || *bp++ != id[1])
			continue;
		if (*bp == '@')
			return (-1);
		if (*bp != '#')
			continue;
		bp++;
		base = 10;
		if (*bp == '0')
			base = 8;
		i = 0;
		while (isdigit(*bp))
			i *= base, i += *bp++ - '0';
		return (i);
	}
}

/*
 * Handle a flag option.
 * Flag options are given "naked", i.e. followed by a : or the end
 * of the buffer.  Return 1 if we find the option, or 0 if it is
 * not given.
 */
static
dgetflag(id)
	char *id;
{
	register char *bp = tbuf;

	for (;;) {
		bp = dskip(bp);
		if (!*bp)
			return (0);
		if (*bp++ == id[0] && *bp != 0 && *bp++ == id[1]) {
			if (!*bp || *bp == ':')
				return (1);
			else if (*bp == '@')
				return (0);
		}
	}
}

/*
 * Get a string valued option.
 * These are given as
 *	cl=^Z
 * Much decoding is done on the strings, and the strings are
 * placed in area, which is a ref parameter which is updated.
 * No checking on area overflow.
 */
static char *
dgetstr(id, area)
	char *id, **area;
{
	register char *bp = tbuf;

	for (;;) {
		bp = dskip(bp);
		if (!*bp)
			return (0);
		if (*bp++ != id[0] || *bp == 0 || *bp++ != id[1])
			continue;
		if (*bp == '@')
			return (0);
		if (*bp != '=')
			continue;
		bp++;
		return (ddecode(bp, area));
	}
}

/*
 * Tdecode does the grung work to decode the
 * string capability escapes.
 */
static char *
ddecode(str, area)
	register char *str;
	char **area;
{
	register char *cp;
	register int c;
	register char *dp;
	int i;

	cp = *area;
	while ((c = *str++) && c != ':') {
		switch (c) {

		case '^':
			c = *str++ & 037;
			break;

		case '\\':
			dp = "E\033^^\\\\::n\nr\rt\tb\bf\f";
			c = *str++;
nextc:
			if (*dp++ == c) {
				c = *dp++;
				break;
			}
			dp++;
			if (*dp)
				goto nextc;
			if (isdigit(c)) {
				c -= '0', i = 2;
				do
					c <<= 3, c |= *str++ - '0';
				while (--i && isdigit(*str));
			}
			break;
		}
		*cp++ = c;
	}
	*cp++ = 0;
	str = *area;
	*area = cp;
	return (str);
}

#define strcasecmp stricmp
static
gettype(t, names)
	char *t;
	char **names;
{
	register char **nm;

	for (nm = names; *nm; nm++)
		if (strcasecmp(t, *nm) == 0)
			return (nm - names);
	if (isdigit(*t))
		return (atoi(t));
	return (0);
}

/* next routine added by cdh */
char *strerror(err)
int err;
{
	static char unknown[20];
	strcpy(unknown, " entry not found");
	return(unknown);
}

static
error(err)
	int err;
{
	char *p;

	(void)fprintf(stderr, "disklabel: ", 12);
	(void)fprintf(stderr, _PATH_DISKTAB, sizeof(_PATH_DISKTAB) - 1);
	p = strerror(err);
	(void)fprintf(stderr, p, strlen(p));
	(void)fprintf(stderr, "\n", 1);
}

/*
 * Fetch disklabel for disk.
 */
int readlabel(lpp)
	struct disklabel *lpp;
{
	struct disklabel *lp;

	if (bread(bootarea, 0, BBSIZE))
		goto autosize_partition;

	if (force_autolabel)
	    goto autosize_partition;
	for (lp = (struct disklabel *)bootarea;
	    lp <= (struct disklabel *)(bootarea + BBSIZE - sizeof(*lp));
	    lp = (struct disklabel *)((char *)lp + 16)) {
		if (lp->d_magic == DISKMAGIC &&
		    lp->d_magic2 == DISKMAGIC) break;
		if (lp > (struct disklabel *)(bootarea+BBSIZE-sizeof(*lp)) ||
	    	    lp->d_magic != DISKMAGIC || lp->d_magic2 != DISKMAGIC)
			goto autosize_partition;
	}

	bcopy(lp, lpp, sizeof(struct disklabel));
	return(0);

	autosize_partition:
	force_autolabel = 0;
#ifdef AMIGA
	if (dio_label(lpp))
		if (file_label(lpp))
			return(1);
	return(0);
#else
	if (file_label(lpp))
		return(1);
	return(0);
#endif
}

writelabel(boot, lp)
	char *boot;
	register struct disklabel *lp;
{
	register int i;
	int flag;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = 0;

	if (bwrite(boot, 0, lp->d_bbsize)) {
		perror("write");
		return (1);
	}
	return (0);
}
