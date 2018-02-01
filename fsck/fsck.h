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
 *
 *	from: @(#)fsck.h	8.1 (Berkeley) 6/5/93
 *	$Id: fsck.h,v 1.6 1994/06/08 19:00:21 mycroft Exp $
 */

#define	MAXDUP		10	/* limit on dup blks (per inode) */
#define	MAXBAD		10	/* limit on bad blks (per inode) */
#define	MAXBUFSPACE	40*1024	/* maximum space to allocate to buffers */
#define	INOBUFSIZE	56*1024	/* size of buffer to read inodes in pass1 */

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

#define	USTATE	01		/* inode not allocated */
#define	FSTATE	02		/* inode is file */
#define	DSTATE	03		/* inode is directory */
#define	DFOUND	04		/* directory found during descent */
#define	DCLEAR	05		/* directory is to be cleared */
#define	FCLEAR	06		/* file is to be cleared */

/*
 * buffer cache structure.
 */
struct bufarea {
	struct bufarea	*b_next;		/* free list queue */
	struct bufarea	*b_prev;		/* free list queue */
	daddr_t	b_bno;
	int	b_size;
	int	b_errs;
	int	b_flags;
	union {
		char	*b_buf;			/* buffer space */
		daddr_t	*b_indir;		/* indirect block */
		struct	fs *b_fs;		/* super block */
		struct	cg *b_cg;		/* cylinder group */
		struct	dinode *b_dinode;	/* inode block */
	} b_un;
	char	b_dirty;
};

#define	B_INUSE 1

#ifndef EXTERN
#define EXTERN extern
#endif

#define	MINBUFS		5	/* minimum number of buffers required */
EXTERN struct bufarea bufhead;		/* head of list of other blks in filesys */
EXTERN struct bufarea sblk;		/* file system superblock */
EXTERN struct bufarea cgblk;		/* cylinder group blocks */
EXTERN struct bufarea *pdirbp;		/* current directory contents */
EXTERN struct bufarea *pbp;		/* current inode block */
EXTERN struct bufarea *getdatablk();

#define	dirty(bp)	(bp)->b_dirty = 1
#define	initbarea(bp) \
	(bp)->b_dirty = 0; \
	(bp)->b_bno = (daddr_t)-1; \
	(bp)->b_flags = 0;

#define	sbdirty()	sblk.b_dirty = 1
#define	cgdirty()	cgblk.b_dirty = 1
#define	sblock		(*sblk.b_un.b_fs)
#define	cgrp		(*cgblk.b_un.b_cg)

enum fixstate {DONTKNOW, NOFIX, FIX, IGNORE};

struct inodesc {
	enum fixstate id_fix;	/* policy on fixing errors */
	int (*id_func)(void *);	/* function to be applied to blocks of inode */
	ino_t id_number;	/* inode number described */
	ino_t id_parent;	/* for DATA nodes, their parent */
	daddr_t id_blkno;	/* current block number being examined */
	int id_numfrags;	/* number of frags contained in block */
	quad_t id_filesize;	/* for DATA nodes, the size of the directory */
	int id_loc;		/* for DATA nodes, current location in dir */
	int id_entryno;		/* for DATA nodes, current entry number */
	struct direct *id_dirp;	/* for DATA nodes, ptr to current entry */
	char *id_name;		/* for DATA nodes, name to find or enter */
	char id_type;		/* type of descriptor, DATA or ADDR */
};
/* file types */
#define	DATA	1
#define	ADDR	2

/*
 * Linked list of duplicate blocks.
 *
 * The list is composed of two parts. The first part of the
 * list (from duplist through the node pointed to by muldup)
 * contains a single copy of each duplicate block that has been
 * found. The second part of the list (from muldup to the end)
 * contains duplicate blocks that have been found more than once.
 * To check if a block has been found as a duplicate it is only
 * necessary to search from duplist through muldup. To find the
 * total number of times that a block has been found as a duplicate
 * the entire list must be searched for occurences of the block
 * in question. The following diagram shows a sample list where
 * w (found twice), x (found once), y (found three times), and z
 * (found once) are duplicate block numbers:
 *
 *    w -> y -> x -> z -> y -> w -> y
 *    ^		     ^
 *    |		     |
 * duplist	  muldup
 */
struct dups {
	struct dups *next;
	daddr_t dup;
};
EXTERN struct dups *duplist;		/* head of dup list */
EXTERN struct dups *muldup;		/* end of unique duplicate dup block numbers */

/*
 * Linked list of inodes with zero link counts.
 */
struct zlncnt {
	struct zlncnt *next;
	ino_t zlncnt;
};
EXTERN struct zlncnt *zlnhead;		/* head of zero link count list */

/*
 * Inode cache data structures.
 */
EXTERN struct inoinfo {
	struct	inoinfo *i_nexthash;	/* next entry in hash chain */
	ino_t	i_number;		/* inode number of this entry */
	ino_t	i_parent;		/* inode number of parent */
	ino_t	i_dotdot;		/* inode number of `..' */
	size_t	i_isize;		/* size of inode */
	u_int	i_numblks;		/* size of block array in bytes */
	daddr_t	i_blks[1];		/* actually longer */
} **inphead, **inpsort;
EXTERN long numdirs, listmax, inplast;

EXTERN char	*cdevname;		/* name of device being checked */
EXTERN long	dev_bsize;		/* computed value of DEV_BSIZE */
EXTERN long	secsize;		/* actual disk sector size */
EXTERN char	nflag;			/* assume a no response */
EXTERN char	yflag;			/* assume a yes response */
EXTERN int	bflag;			/* location of alternate super block */
EXTERN int	debug;			/* output debugging info */
EXTERN int	cvtlevel;		/* convert to newer file system format */
EXTERN int	doinglevel1;		/* converting to new cylinder group format */
EXTERN int	doinglevel2;		/* converting to new inode format */
EXTERN int	newinofmt;		/* filesystem has new inode format */
EXTERN char	preen;			/* just fix normal inconsistencies */
extern char	hotroot;		/* checking root device */
EXTERN char	havesb;			/* superblock has been read */
EXTERN int	fsmodified;		/* 1 => write done to file system */
EXTERN int	fsreadfd;		/* file descriptor for reading file system */
EXTERN int	fswritefd;		/* file descriptor for writing file system */

EXTERN daddr_t	maxfsblock;		/* number of blocks in the file system */
EXTERN char	*blockmap;		/* ptr to primary blk allocation map */
EXTERN ino_t	maxino;			/* number of inodes in file system */
EXTERN ino_t	lastino;		/* last inode in use */
EXTERN char	*statemap;		/* ptr to inode state table */
EXTERN char	*typemap;		/* ptr to inode type table */
EXTERN short	*lncntp;		/* ptr to link count table */

EXTERN ino_t	lfdir;			/* lost & found directory inode number */
extern char	*lfname;		/* lost & found directory name */
extern int	lfmode;			/* lost & found directory creation mode */

EXTERN daddr_t	n_blks;			/* number of blocks in use */
EXTERN daddr_t	n_files;		/* number of files in use */

#define	clearinode(dp)	(*(dp) = zino)
EXTERN struct	dinode zino;

#define	setbmap(blkno)	setbit(blockmap, blkno)
#define	testbmap(blkno)	isset(blockmap, blkno)
#define	clrbmap(blkno)	clrbit(blockmap, blkno)

#define	STOP	0x01
#define	SKIP	0x02
#define	KEEPON	0x04
#define	ALTERED	0x08
#define	FOUND	0x10

/* time_t time(); */
struct dinode *ginode();
struct inoinfo *getinoinfo();
void getblk();
ino_t allocino();
int findino();

extern int DEV_BSIZE;
extern int DEV_BSHIFT;

#ifdef cdh
extern long totalreads;
#define btodb(bytes) ((unsigned)(bytes) >> 9)   /* calculates (bytes / 512) */

void errexit(const char *fmt, ...);
int checkfstab();
int pass1();
int pass1b();
int pass2();
int pass3();
int pass4();
int pass5();
int setup();
int docheck();
int inocleanup();
void pwarn();
void pfatal();
void cacheino();
#else
#define btodb(bytes)                    /* calculates (bytes / DEV_BSIZE) */ \
        ((unsigned)(bytes) >> DEV_BSHIFT)
#define dbtob(db)                       /* calculates (db * DEV_BSIZE) */ \
        ((unsigned)(db) << DEV_BSHIFT)

#endif

#ifdef AMIGA
typedef unsigned long ulong;
#include <amiga_unix.h>
#endif

#ifdef __linux
void sync(void);
typedef void (*sighandler_t) (int);
sighandler_t signal(int signum, sighandler_t handler);
#define SIGINT           2
#define SIGQUIT          3
#define SIG_IGN	((sighandler_t) 1)
#endif
