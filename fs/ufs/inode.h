/* 8-Oct-96 CDH */

/*
 * The I node is the focus of all local file activity in UNIX.
 * There is a unique inode allocated for each active file,
 * each current directory, each mounted-on file, each mapping,
 * and the root.  An inode is `named' by its dev/inumber pair.
 * Data in icommon is read in from permanent inode on volume.
 */

#ifndef	_UFS_INODE_H
#define	_UFS_INODE_H

#include <ufs/fs.h>

#define	NDADDR	12		/* direct addresses in inode */
#define	NIADDR	3		/* indirect addresses in inode */
#define	FSL_SIZE (NDADDR+NIADDR-1) * sizeof (daddr_t)
				/* max fast symbolic name length is 56 */

struct	icommon {
	u_short	ic_mode;	/*  0: mode and type of file */
	short	ic_nlink;	/*  2: number of links to file */

	uid_t	ic_ouid;	/*  4: owner's user id */
	gid_t	ic_ogid;	/*  6: owner's group id */
	quad	ic_size;	/*  8: number of bytes in file */

	utime_t	ic_atime;	/* 16: time last accessed */
	long	ic_atime_ns;
	utime_t	ic_mtime;	/* 24: time last modified */
	long	ic_mtime_ns;
	utime_t	ic_ctime;	/* 32: last time inode changed */
	long	ic_ctime_ns;

	daddr_t	ic_db[NDADDR];	/* 40: disk block addresses */
	daddr_t	ic_ib[NIADDR];	/* 88: indirect blocks */
	long	ic_flags;	/* 100: status, currently unused */
	long	ic_blocks;	/* 104: blocks actually held */
	long	ic_gen;		/* 108: generation number */
	u_long	ic_nuid;	/* 112: File owner. */
	u_long	ic_ngid;	/* 116: File group. */
	long	dl_spare[2];	/* 120: Reserved; currently unused */
};



struct	dinode {
	union {
		struct	icommon di_icom;
		char	di_size[128];
	} di_un;
};

#define	i_mode		i_ic.ic_mode
#define	i_nlink		i_ic.ic_nlink
#define	i_uid		i_ic.ic_uid
#define	i_gid		i_ic.ic_gid
/* ugh! -- must be fixed */
#if	defined(vax) || defined(i386)
#define	i_size		i_ic.ic_size.val[0]
#endif
#if	defined(mc68000) || defined(sparc)
#define	i_size		i_ic.ic_size.val[1]
#endif
#define	i_db		i_ic.ic_db
#define	i_ib		i_ic.ic_ib
#define	i_atime		i_ic.ic_atime
#define	i_mtime		i_ic.ic_mtime
#define	i_ctime		i_ic.ic_ctime
#define	i_blocks	i_ic.ic_blocks
#define	i_rdev		i_ic.ic_db[0]
#define	i_gen		i_ic.ic_gen
#define	i_nextr		i_un.if_nextr
#define	i_socket	i_un.is_socket
#define	i_forw		i_chain[0]
#define	i_back		i_chain[1]
#define	i_freef		i_fr.if_freef
#define	i_freeb		i_fr.if_freeb
#define	i_delayoff	i_ic.ic_delayoff	/* XXX */
#define	i_delaylen	i_ic.ic_delaylen	/* XXX */
#define	i_nextrio	i_ic.ic_nextrio		/* XXX */
#define	i_writes	i_ic.ic_writes		/* XXX */

#define	di_ic		di_un.di_icom
#define	di_mode		di_ic.ic_mode
#define	di_nlink	di_ic.ic_nlink
#define	di_uid		di_ic.ic_uid
#define	di_gid		di_ic.ic_gid
#if	defined(vax) || defined(i386)
#define	di_size		di_ic.ic_size.val[0]
#endif
#if	defined(mc68000) || defined(sparc)
#define	di_size		di_ic.ic_size.val[1]
#endif
#define	di_db		di_ic.ic_db
#define	di_ib		di_ic.ic_ib
#define	di_atime	di_ic.ic_atime
#define	di_mtime	di_ic.ic_mtime
#define	di_ctime	di_ic.ic_ctime
#define	di_rdev		di_ic.ic_db[0]
#define	di_blocks	di_ic.ic_blocks
#define	di_gen		di_ic.ic_gen

/* modes */
#define	IFMT		0170000		/* type of file */
#define	IFIFO		0010000		/* named pipe (fifo) */
#define	IFCHR		0020000		/* character special */
#define	IFDIR		0040000		/* directory */
#define	IFBLK		0060000		/* block special */
#define	IFREG		0100000		/* regular */
#define	IFLNK		0120000		/* symbolic link */
#define	IFSOCK		0140000		/* socket */
#define	IFWHT		0160000		/* whiteout entry */

#define	ISUID		04000		/* set user id on execution */
#define	ISGID		02000		/* set group id on execution */
#define	ISVTX		01000		/* save swapped text even after use */
#define	IREAD		0400		/* read, write, execute permissions */
#define	IWRITE		0200
#define	IEXEC		0100

#endif	/* _UFS_INODE_H */
