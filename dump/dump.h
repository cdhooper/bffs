/*	$NetBSD: dump.h,v 1.9 1995/03/18 14:54:57 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1993
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
 *
 *	@(#)dump.h	8.1 (Berkeley) 6/5/93
 */

#define MAXINOPB	(MAXBSIZE / sizeof(struct dinode))
#define MAXNINDIR	(MAXBSIZE / sizeof(daddr_t))

#ifdef MAIN
#define EXTERN
#else
#define EXTERN extern
#endif

#ifdef cdh
#define __P(x) x
#endif
/*
 * Dump maps used to describe what is to be dumped.
 */
EXTERN int	mapsize;	/* size of the state maps */
EXTERN char	*usedinomap;	/* map of allocated inodes */
EXTERN char	*dumpdirmap;	/* map of directories to be dumped */
EXTERN char	*dumpinomap;	/* map of files to be dumped */
/*
 * Map manipulation macros.
 */
#define	SETINO(ino, map) \
	map[(u_int)((ino) - 1) / NBBY] |=  1 << ((u_int)((ino) - 1) % NBBY)
#define	CLRINO(ino, map) \
	map[(u_int)((ino) - 1) / NBBY] &=  ~(1 << ((u_int)((ino) - 1) % NBBY))
#define	TSTINO(ino, map) \
	(map[(u_int)((ino) - 1) / NBBY] &  (1 << ((u_int)((ino) - 1) % NBBY)))

/*
 *	All calculations done in 0.1" units!
 */
/* all changed to EXTERN by cdh */
EXTERN char	*disk;		/* name of the disk file */
EXTERN char	*tape;		/* name of the tape file */
EXTERN char	*dumpdates;	/* name of the file containing dump date information*/
#ifndef AMIGA
EXTERN char	*temp;		/* name of the file for doing rewrite of dumpdates */
#endif
EXTERN char	lastlevel;	/* dump level of previous dump */
EXTERN char	level;		/* dump level of this dump */
EXTERN int	uflag;		/* update flag */
EXTERN int	diskfd;		/* disk file descriptor */
EXTERN int	tapefd;		/* tape file descriptor */
EXTERN int	pipeout;	/* true => output to standard output */
EXTERN ino_t	curino;		/* current inumber; used globally */
EXTERN int	newtape;	/* new tape flag */
extern int	density;	/* density in 0.1" units */
EXTERN long	tapesize;	/* estimated tape size, blocks */
EXTERN long	tsize;		/* tape size in 0.1" units */
EXTERN long	asize;		/* number of 0.1" units written on current tape */
EXTERN int	etapes;		/* estimated number of tapes */
EXTERN int	nonodump;	/* if set, do not honor UF_NODUMP user flags */

extern int	notify;		/* notify operator flag */
extern int	blockswritten;	/* number of blocks written on current tape */
extern int	tapeno;		/* current tape number */
EXTERN time_t	tstart_writing;	/* when started writing the first tape block */
EXTERN struct	fs *sblock;	/* the file system super block */
EXTERN char	sblock_buf[MAXBSIZE];
extern long	dev_bsize;	/* block size of underlying disk device */
EXTERN int	dev_bshift;	/* log2(dev_bsize) */
EXTERN int	tp_bshift;	/* log2(TP_BSIZE) */
#undef EXTERN

/* operator interface functions */
void	broadcast __P((char *message));
void	lastdump __P((int arg));	/* int should be char */
void	msg __P((const char *fmt, ...));
void	msgtail __P((const char *fmt, ...));
int	query __P((char *question));
void	quit __P((const char *fmt, ...));
void	set_operators __P((void));
void	timeest __P((void));
time_t	unctime __P((char *str));

/* mapping rouintes */
struct	dinode;
long	blockest __P((struct dinode *dp));
int	mapfiles __P((ino_t maxino, long *tapesize));
int	mapdirs __P((ino_t maxino, long *tapesize));

/* file dumping routines */
void	blksout __P((daddr_t *blkp, int frags, ino_t ino));
#ifdef AMIGA
#define bread local_bread
#endif
void	bread __P((daddr_t blkno, char *buf, int size));
void	dumpino __P((struct dinode *dp, ino_t ino));
void	dumpmap __P((char *map, int type, ino_t ino));
void	writeheader __P((ino_t ino));

/* tape writing routines */
int	alloctape __P((void));
void	close_rewind __P((void));
void	dumpblock __P((daddr_t blkno, int size));
void	startnewtape __P((int top));
void	trewind __P((void));
void	writerec __P((char *dp, int isspcl));

__dead void Exit __P((int status));
void	dumpabort __P((int signo));
void	getfstab __P((void));

char	*rawname __P((char *cp));
struct	dinode *getino __P((ino_t inum));

/* rdump routines */
#ifdef RDUMP
void	rmtclose __P((void));
int	rmthost __P((char *host));
int	rmtopen __P((char *tape, int mode));
int	rmtwrite __P((char *buf, int count));
#endif /* RDUMP */

void	interrupt __P((int signo));	/* in case operator bangs on console */

/*
 *	Exit status codes
 */
#define	X_FINOK		0	/* normal exit */
#define	X_REWRITE	2	/* restart writing from the check point */
#define	X_ABORT		3	/* abort dump; don't attempt checkpointing */

#define	OPGRENT	"operator"		/* group entry to notify */
#define DIALUP	"ttyd"			/* prefix for dialups */

struct	fstab *fstabsearch __P((char *key));	/* search fs_file and fs_spec */

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/*
 *	The contents of the file _PATH_DUMPDATES is maintained both on
 *	a linked list, and then (eventually) arrayified.
 */
struct dumpdates {
	char	dd_name[NAME_MAX+3];
	char	dd_level;
	time_t	dd_ddate;
};
struct dumptime {
	struct	dumpdates dt_value;
	struct	dumptime *dt_next;
};
extern struct	dumptime *dthead;	/* head of the list version */
extern int	nddates;		/* number of records (might be zero) */
extern int	ddates_in;		/* we have read the increment file */
extern struct	dumpdates **ddatev;	/* the arrayfied version */
void	initdumptimes __P((void));
void	getdumptime __P((void));
void	putdumptime __P((void));
#define	ITITERATE(i, ddp) \
	for (ddp = ddatev[i = 0]; i < nddates; ddp = ddatev[++i])

void	sig __P((int signo));

/*
 * Compatibility with old systems.
 */
#ifdef COMPAT
#include <sys/file.h>
#define	strchr(a,b)	index(a,b)
#define	strrchr(a,b)	rindex(a,b)
extern char *strdup(), *ctime();
extern int read(), write();
extern int errno;
#endif

#ifndef	_PATH_UTMP
#ifdef AMIGA
#define	_PATH_UTMP	"ram:utmp"
#else
#define	_PATH_UTMP	"/etc/utmp"
#endif
#endif
#ifndef	_PATH_FSTAB
#ifdef AMIGA
#define	_PATH_FSTAB	"devs:fstab"
#else
#define	_PATH_FSTAB	"/etc/fstab"
#endif
#endif

#ifdef UNIX_PORT
#ifndef cdh
extern char *calloc();
extern char *malloc();
#endif
extern long atol();
extern char *strcpy();
extern char *strncpy();
extern char *strcat();
extern time_t time();
extern void endgrent();
extern __dead void exit();
extern off_t lseek();
extern const char *strerror();
#endif

#ifdef cdh
struct fstab *getfsent(void);
#endif
