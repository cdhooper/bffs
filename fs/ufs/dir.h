/*  @(#)fsdir.h 8-Oct-96 CDH; 2.9 89/08/23 SMI; from UCB 4.5 82/11/13	*/

/*
 * A directory consists of some number of blocks of DIRBLKSIZ
 * bytes, where DIRBLKSIZ is chosen such that it can be transferred
 * to disk in a single atomic operation (e.g. 512 bytes on most machines).
 *
 * Each DIRBLKSIZ byte block contains some number of directory entry
 * structures, which are of variable length.  Each directory entry has
 * a struct direct at the front of it, containing its inode number,
 * the length of the entry, and the length of the name contained in
 * the entry.  These are followed by the name padded to a 4 byte boundary
 * with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 *
 * The macro DIRSIZ(dp) gives the amount of space required to represent
 * a directory entry.  Free space in a directory is represented by
 * entries which have dp->d_reclen > DIRSIZ(dp).  All DIRBLKSIZ bytes
 * in a directory block are claimed by the directory entries.  This
 * usually results in the last entry in a directory having a large
 * dp->d_reclen.  When entries are deleted from a directory, the
 * space is returned to the previous entry in the same directory
 * block by increasing its dp->d_reclen.  If the first entry of
 * a directory block is free, then its dp->d_ino is set to 0.
 * Entries other than the first in a directory do not normally have
 * dp->d_ino set to 0.
 */

#ifndef _UFS_DIR_H
#define	_UFS_DIR_H

#define DIRBLKSIZ	512

#define	MAXNAMLEN	255

/*
 * On-disk format of directory entry.  Note that for big endian, this
 * structure is exactly compatible with the old directory format.
 * For little endian (x86), additional work needs to be done to accommodate
 * the fact that d_type does not exist for the older filesystem format.
 */
struct	direct {
	u_long	d_ino;			/* inode number of entry */
	u_short	d_reclen;		/* length of this record */
	u_char	d_type;			/* file type, see below */
	u_char	d_namlen;		/* length of string in d_name */
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
};

struct	odirect {
	u_long	d_ino;			/* inode number of entry */
	u_short	d_reclen;		/* length of this record */
	u_short	d_namlen;		/* length of string in d_name */
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
};

typedef struct direct direct_t;


/*
 * File types
 */
#define DT_UNKNOWN	 0
#define DT_FIFO		 1
#define DT_CHR		 2
#define DT_DIR		 4
#define DT_BLK		 6
#define DT_REG		 8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		16

/*
 * The UFS_DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 4 byte boundary.
 *
 * The below two macros were taken from NetBSD 7.1 ufs/dir.h header file.
 * No endian swapping is needed because d_type and d_namelen are each one byte.
 */
#define UFS_DIRECTSIZ(namlen) \
        ((sizeof(struct direct) - (MAXNAMLEN+1)) + (((namlen)+1 + 3) &~ 3))

#define UFS_DIRSIZ(oldfmt, dp, needswap)   \
    (((oldfmt) && (needswap)) ?                 \
    UFS_DIRECTSIZ((dp)->d_type) : UFS_DIRECTSIZ((dp)->d_namlen))

#endif /* _UFS_DIR_H_ */
