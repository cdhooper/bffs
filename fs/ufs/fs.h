/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)fs.h 2.16 89/08/23 SMI	8-Oct-96 CDH
 */

/*
 * Each disk drive contains some number of file systems.
 * A file system consists of a number of cylinder groups.
 * Each cylinder group has inodes and data.
 *
 * A file system is described by its super-block, which in turn
 * describes the cylinder groups.  The super-block is critical
 * data and is replicated in each cylinder group to protect against
 * catastrophic loss.  This is done at `newfs' time and the critical
 * super-block data does not change, so the copies need not be
 * referenced further unless disaster strikes.
 *
 * For file system fs, the offsets of the various blocks of interest
 * are given in the super block as:
 *	[fs->fs_sblkno]		Super-block
 *	[fs->fs_cblkno]		Cylinder group block
 *	[fs->fs_iblkno]		Inode blocks
 *	[fs->fs_dblkno]		Data blocks
 * The beginning of cylinder group cg in fs, is given by
 * the ``cgbase(fs, cg)'' macro.
 *
 * The first boot and super blocks are given in absolute disk addresses.
 * The byte-offset forms are preferred, as they don't imply a sector size.
 */

#ifndef _UFS_FS_H
#define	_UFS_FS_H

#define howmany(x, y)   (((x)+((y)-1))/(y))
#define quad            quad_t
typedef struct          _quad_t { long val[2]; } quad_t;
typedef short           dev_t;
typedef long            off_t;
typedef long            daddr_t;
typedef unsigned long   utime_t;
typedef unsigned char   u_char;
typedef unsigned long   ino_t;
typedef unsigned long   u_long;
typedef unsigned short  u_short;
typedef unsigned short  uid_t;
typedef unsigned short  gid_t;

#define	BBSIZE		8192
#define	SBSIZE		8192
#define	BBOFF		((off_t)(0))
#define	SBOFF		((off_t)(BBOFF + BBSIZE))
#define	BBLOCK		((daddr_t)(0))

/*
 * Addresses stored in inodes are capable of addressing fragments
 * of `blocks'. File system blocks of at most size MAXBSIZE can
 * be optionally broken into 2, 4, or 8 pieces, each of which is
 * addressible; these pieces may be DEV_BSIZE, or some multiple of
 * a DEV_BSIZE unit.
 *
 * Large files consist of exclusively large data blocks.  To avoid
 * undue wasted disk space, the last data block of a small file may be
 * allocated as only as many fragments of a large block as are
 * necessary.  The file system format retains only a single pointer
 * to such a fragment, which is a piece of a single large block that
 * has been divided.  The size of such a fragment is determinable from
 * information in the inode, using the ``blksize(fs, ip, lbn)'' macro.
 *
 * The file system records space availability at the fragment level;
 * to determine block availability, aligned fragments are examined.
 *
 * The root inode is the root of the file system.
 * Inode 0 can't be used for normal purposes and
 * historically bad blocks were linked to inode 1,
 * thus the root inode is 2. (inode 1 is no longer used for
 * this purpose, however numerous dump tapes make this
 * assumption, so we are stuck with it)
 */
#define	ROOTINO		((ino_t)2)	/* i number of all roots */
#define	LOSTFOUNDINO    (ROOTINO + 1)

/*
 * MINBSIZE is the smallest allowable block size.
 * In order to insure that it is possible to create files of size
 * 2^32 with only two levels of indirection, MINBSIZE is set to 4096.
 * MINBSIZE must be big enough to hold a cylinder group block,
 * thus changes to (struct cg) must keep its size within MINBSIZE.
 * Note that super blocks are always of size SBSIZE,
 * and that both SBSIZE and MAXBSIZE must be >= MINBSIZE.
 */
#define	MINBSIZE	4096

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 * The limit on the amount of summary information per file system
 * is defined by MAXCSBUFS. It is currently parameterized for a
 * maximum of two million cylinders.
 */
#define	MAXMNTLEN 512
#define	MAXCSBUFS 32

/*
 * Per cylinder group information; summarized in blocks allocated
 * from first cylinder group data blocks.  These blocks have to be
 * read in from fs_csaddr (size fs_cssize) in addition to the
 * super block.
 *
 * N.B. sizeof (struct csum) must be a power of two in order for
 * the ``fs_cs'' macro to work (see below).
 */
struct csum {
	long	cs_ndir;	/* number of directories */
	long	cs_nbfree;	/* number of free blocks */
	long	cs_nifree;	/* number of free inodes */
	long	cs_nffree;	/* number of free frags */
};

/*
 * Super block for a file system.
 */
#define FS_UFS1_MAGIC   0x011954        /* UFS1 fast filesystem magic number */
#define FS_UFS2_MAGIC   0x19540119      /* UFS2 fast filesystem magic number */

struct	fs {
	struct	fs *fs_link;		/* linked list of file systems */
	struct	fs *fs_rlink;		/* used for incore super blocks */
	daddr_t	fs_sblkno;		/* addr of super-block in filesys */
	daddr_t	fs_cblkno;		/* offset of cyl-block in filesys */
	daddr_t	fs_iblkno;		/* offset of inode-blocks in filesys */
	daddr_t	fs_dblkno;		/* offset of first data after cg */
	long	fs_cgoffset;		/* cylinder group offset in cylinder */
	long	fs_cgmask;		/* used to calc mod fs_ntrak */
	utime_t	fs_time;		/* last time written */
	long	fs_size;		/* number of blocks in fs */
	long	fs_dsize;		/* number of data blocks in fs */
	long	fs_ncg;			/* number of cylinder groups */
	long	fs_bsize;		/* size of basic blocks in fs */
	long	fs_fsize;		/* size of frag blocks in fs */
	long	fs_frag;		/* number of frags in a block in fs */
/* these are configuration parameters */
	long	fs_minfree;		/* minimum percentage of free blocks */
	long	fs_rotdelay;		/* num of ms for optimal next block */
	long	fs_rps;			/* disk revolutions per second */
/* these fields can be computed from the others */
	long	fs_bmask;		/* ``blkoff'' calc of blk offsets */
	long	fs_fmask;		/* ``fragoff'' calc of frag offsets */
	long	fs_bshift;		/* ``lblkno'' calc of logical blkno */
	long	fs_fshift;		/* ``numfrags'' calc number of frags */
/* these are configuration parameters */
	long	fs_maxcontig;		/* max number of contiguous blks */
	long	fs_maxbpg;		/* max number of blks per cyl group */
/* these fields can be computed from the others */
	long	fs_fragshift;		/* block to frag shift */
	long	fs_fsbtodb;		/* fsbtodb and dbtofsb shift constant */
	long	fs_sbsize;		/* actual size of super block */
	long	fs_csmask;		/* csum block offset */
	long	fs_csshift;		/* csum block number */
	long	fs_nindir;		/* value of NINDIR */
	long	fs_inopb;		/* value of INOPB */
	long	fs_nspf;		/* value of NSPF */
/* yet another configuration parameter */
	long	fs_optim;		/* optimization preference, see below */
/* these fields are derived from the hardware */
	long	fs_npsect;		/* # sectors/track including spares */
	long	fs_interleave;		/* hardware sector interleave */
	long	fs_trackskew;		/* sector 0 skew, per track */
/* a unique id for this filesystem (currently unused and unmaintained) */
/* In 4.3 Tahoe this space is used by fs_headswitch and fs_trkseek */
/* Neither of those fields is used in the Tahoe code right now but */
/* there could be problems if they are.				   */
	long	fs_id[2];		/* file system id */
/* sizes determined by number of cylinder groups and their sizes */
	daddr_t fs_csaddr;		/* blk addr of cyl grp summary area */
	long	fs_cssize;		/* size of cyl grp summary area */
	long	fs_cgsize;		/* cylinder group size */
/* these fields are derived from the hardware */
	long	fs_ntrak;		/* tracks per cylinder */
	long	fs_nsect;		/* sectors per track */
	long	fs_spc;			/* sectors per cylinder */
/* this comes from the disk driver partitioning */
	long	fs_ncyl;		/* cylinders in file system */
/* these fields can be computed from the others */
	long	fs_cpg;			/* cylinders per group */
	long	fs_ipg;			/* inodes per group */
	long	fs_fpg;			/* blocks per group * fs_frag */
/* this data must be re-computed after crashes */
	struct	csum fs_cstotal;	/* cylinder summary information */
/* these fields are cleared at mount time */
	char	fs_fmod;		/* super block modified flag */
	char	fs_clean;		/* file system is clean flag */
	char	fs_ronly;		/* mounted read-only flag */
	char	fs_flags;		/* currently unused flag */
	char	fs_fsmnt[MAXMNTLEN];	/* name mounted on */
/* these fields retain the current block allocation info */
	long	fs_cgrotor;		/* last cg searched */
	struct	csum *fs_csp[MAXCSBUFS]; /* list of fs_cs info buffers */
	long	fs_cpc;			/* cyl per cycle in postbl */
	short	fs_opostbl[16][8];	/* old rotation block list head */
	long	fs_sparecon[50];	/* reserved for future constants */
	long	fs_contigsumsize;	/* size of cluster summary array */
	long	fs_maxsymlinklen;	/* max length of an internal symlink */
	long	fs_inodefmt;		/* format of on-disk inodes */
	quad	fs_maxfilesize;		/* maximum representable file size */
	quad	fs_qbmask;		/* ~fs_bmask - for use with quad size */
	quad	fs_qfmask;		/* ~fs_fmask - for use with quad size */
	long	fs_state;		/* validate fs_clean field */
	long	fs_postblformat;	/* format of positional layout tables */
	long	fs_nrpos;		/* number of rotaional positions */
	long	fs_postbloff;		/* (short) rotation block list head */
	long	fs_rotbloff;		/* (u_char) blocks for each rotation */
	long	fs_magic;		/* magic number */
	u_char	fs_space[1];		/* list of blocks for each rotation */
/* actually longer */
};
/*
 * Preference for optimization.
 */
#define	FS_OPTTIME	0	/* minimize allocation time */
#define	FS_OPTSPACE	1	/* minimize disk fragmentation */

/*
 * Filesystem flags (used in versions newer then BSD 4.4)
 */
#define FS_FLAGS_DOSOFTDEP  0x02  /* filesystem using soft dependencies */
#define FS_FLAGS_SUJ        0x08  /* filesystem using journaled softupdates */
#define FS_FLAGS_ACLS       0x10  /* filesystem has ACLs enabled */
#define FS_FLAGS_MULTILEVEL 0x20  /* filesystem is MAC multi-level */
#define FS_FLAGS_GJOURNAL   0x40  /* filesystem is gjournaled */
#define FS_FLAGS_UPDATED    0x80  /* flags have moved to new location */

/*
 * Rotational layout table format types
 */
#define	FS_42POSTBLFMT		-1	/* 4.2BSD rotational table format */
#define	FS_DYNAMICPOSTBLFMT	1	/* dynamic rotational table format */
#define FS_44INODEFMT		2	/* 4.4BSD inode format */

/*
 * Cylinder group block for a file system.
 */
#define	CG_MAGIC	0x090255
struct	cg {
	struct	cg *cg_link;		/* linked list of cyl groups */
	long	cg_magic;		/* magic number */
	utime_t	cg_time;		/* time last written */
	long	cg_cgx;			/* we are the cgx'th cylinder group */
	short	cg_ncyl;		/* number of cyl's this cg */
	short	cg_niblk;		/* number of inode blocks this cg */
	long	cg_ndblk;		/* number of data blocks this cg */
	struct	csum cg_cs;		/* cylinder summary information */
	long	cg_rotor;		/* position of last used block */
	long	cg_frotor;		/* position of last used frag */
	long	cg_irotor;		/* position of last used inode */
	long	cg_frsum[MAXFRAG];	/* counts of available frags */
	long	cg_btotoff;		/* (long) block totals per cylinder */
	long	cg_boff;		/* (short) free block positions */
	long	cg_iusedoff;		/* (char) used inode map */
	long	cg_freeoff;		/* (u_char) free block map */
	long	cg_nextfreeoff;		/* (u_char) next available space */
	long    cg_clustersumoff;	/* (long) counts of avail clusters */
	long    cg_clusteroff;		/* (char) free cluster map */
	long    cg_nclusterblks;	/* number of clusters this cg */
	long	cg_sparecon[13];	/* reserved for future use */
	u_char	cg_space[1];		/* space for cylinder group maps */
/* actually longer */
};

/*
 * The following structure is defined
 * for compatibility with old file systems.
 */
struct	ocg {
	struct	ocg *cg_link;		/* linked list of cyl groups */
	struct	ocg *cg_rlink;		/* used for incore cyl groups */
	utime_t	cg_time;		/* time last written */
	long	cg_cgx;			/* we are the cgx'th cylinder group */
	short	cg_ncyl;		/* number of cyl's this cg */
	short	cg_niblk;		/* number of inode blocks this cg */
	long	cg_ndblk;		/* number of data blocks this cg */
	struct	csum cg_cs;		/* cylinder summary information */
	long	cg_rotor;		/* position of last used block */
	long	cg_frotor;		/* position of last used frag */
	long	cg_irotor;		/* position of last used inode */
	long	cg_frsum[8];		/* counts of available frags */
	long	cg_btot[32];		/* block totals per cylinder */
	short	cg_b[32][8];		/* positions of free blocks */
	char	cg_iused[256];		/* used inode map */
	long	cg_magic;		/* magic number */
	u_char	cg_free[1];		/* free block map */
/* actually longer */
};

#endif /* _UFS_FS_H */
