/*	@(#)stat.h 2.19 90/01/24 SMI; from UCB 4.7 83/05/21	*/

/*
 * NOTE: changes to this file should also be made to xpg2include/sys/stat.h
 */

#ifndef	__sys_stat_h
#define	__sys_stat_h

#include <sys/types.h>

#ifdef AMIGA

typedef unsigned short mode_t;

#ifndef SYS_STAT_H
#define SYS_STAT_H

#ifndef LIBRARIES_DOS_H
#include <dos30/dos.h>
#include <libraries/dos.h>
#endif

#define S_IFMT	    0xF0000
#define S_IFREG     0x10000
#define S_IFDIR     0x20000
#define S_IFLNK     0x30000
#define S_IFCHR     0x40000
#define S_IFBLK     0x50000

/*
#define S_ISUID     0x08000
#define S_ISGID     0x04000
#define S_ISVTX     0x02000
*/
#define UNIX_IFMT   0170000
#define UNIX_IFCHR  0020000	/* char special */
#define UNIX_IFDIR  0040000	/* directory */
#define UNIX_IFBLK  0060000	/* block special */
#define UNIX_IFREG  0100000	/* regular file */
#define UNIX_IFLNK  0120000	/* soft link */
#define UNIX_IFSOCK 0140000	/* socket */
#define UNIX_IFWHT  0160000	/* whiteout */

#define S_ISUID     0004000
#define S_ISGID     0002000
#define S_ISVTX     0001000
#define S_IREAD     0000400
#define S_IWRITE    0000200
#define S_IEXEC     0000100



typedef long dev_t;
/* typedef unsigned long ino_t; */

struct stat {
    long    st_mode;
    long    st_size;
    long    st_blksize;     /*	not used, compat    */
    long    st_blocks;
    long    st_ctime;
    long    st_mtime;
    long    st_atime;	    /*	not used, compat    */
    long    st_dev;
    short   st_rdev;	    /*	not used, compat    */
    long    st_ino;
    short   st_uid;	    /*	not used, compat    */
    short   st_gid;	    /*	not used, compat    */
    short   st_nlink;	    /*	not used, compat    */
};

/*
extern int stat(const char *, struct stat *);
extern int fstat(int, struct stat *);
*/


/*
 *  dummy unix compat
 */

#define makedev(maj,min)    (((maj) << 8) | (min))
#define major(rdev)     (unsigned char)((rdev) >> 8)
#define minor(rdev)     (unsigned char)(rdev)

#endif


/* end amiga stat */

#else

/* unix stat */


/* typedef     long            time_t; */
struct	stat {
	dev_t	st_dev;
	ino_t	st_ino;
	mode_t	st_mode;
	short	st_nlink;
	uid_t	st_uid;
	gid_t	st_gid;
	dev_t	st_rdev;
	off_t	st_size;
	time_t	st_atime;
	int	st_spare1;
	time_t	st_mtime;
	int	st_spare2;
	time_t	st_ctime;
	int	st_spare3;
	long	st_blksize;
	long	st_blocks;
	long	st_spare4[2];
};


#define	_IFMT		0170000	/* type of file */
#define		_IFDIR	0040000	/* directory */
#define		_IFCHR	0020000	/* character special */
#define		_IFBLK	0060000	/* block special */
#define		_IFREG	0100000	/* regular */
#define		_IFLNK	0120000	/* symbolic link */
#define		_IFSOCK	0140000	/* socket */
#define		_IFIFO	0010000	/* fifo */

#define	S_ISUID		0004000	/* set user id on execution */
#define	S_ISGID		0002000	/* set group id on execution */


#ifndef	_POSIX_SOURCE
#define	S_ISVTX		0001000	/* save swapped text even after use */
#define	S_IREAD		0000400	/* read permission, owner */
#define	S_IWRITE 	0000200	/* write permission, owner */
#define	S_IEXEC		0000100	/* execute/search permission, owner */

#define	S_ENFMT 	0002000	/* enforcement-mode locking */

#define	S_IFMT		_IFMT
#define	S_IFDIR		_IFDIR
#define	S_IFCHR		_IFCHR
#define	S_IFBLK		_IFBLK
#define	S_IFREG		_IFREG
#define	S_IFLNK		_IFLNK
#define	S_IFSOCK	_IFSOCK
#define	S_IFIFO		_IFIFO
#define S_IFWHT  0160000	/* whiteout */
#endif	!_POSIX_SOURCE

#define	S_IRWXU 	0000700	/* rwx, owner */
#define		S_IRUSR	0000400	/* read permission, owner */
#define		S_IWUSR	0000200	/* write permission, owner */
#define		S_IXUSR	0000100	/* execute/search permission, owner */
#define	S_IRWXG		0000070	/* rwx, group */
#define		S_IRGRP	0000040	/* read permission, group */
#define		S_IWGRP	0000020	/* write permission, grougroup */
#define		S_IXGRP	0000010	/* execute/search permission, group */
#define	S_IRWXO		0000007	/* rwx, other */
#define		S_IROTH	0000004	/* read permission, other */
#define		S_IWOTH	0000002	/* write permission, other */
#define		S_IXOTH	0000001	/* execute/search permission, other */

#define	S_ISBLK(m)	(((m)&_IFMT) == _IFBLK)
#define	S_ISCHR(m)	(((m)&_IFMT) == _IFCHR)
#define	S_ISDIR(m)	(((m)&_IFMT) == _IFDIR)
#define	S_ISFIFO(m)	(((m)&_IFMT) == _IFIFO)
#define	S_ISREG(m)	(((m)&_IFMT) == _IFREG)
#ifndef	_POSIX_SOURCE
#define	S_ISLNK(m)	(((m)&_IFMT) == _IFLNK)
#define	S_ISSOCK(m)	(((m)&_IFMT) == _IFSOCK)
#define S_ISWHT(m)      ((m & 0170000) == 0160000)      /* whiteout */

#endif !_POSIX_SOURCE

#endif

#endif	/* !__sys_stat_h */
