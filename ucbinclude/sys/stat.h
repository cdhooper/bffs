/*	@(#)stat.h 2.19 90/01/24 SMI; from UCB 4.7 83/05/21	*/

#ifndef _UCB_SYS_STAT_H_
#define _UCB_SYS_STAT_H_

#include <sys/types.h>

#ifdef AMIGA
#ifndef LIBRARIES_DOS_H
#include <libraries/dos.h>
#endif
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

typedef unsigned short mode_t;

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
 *  dummy unix compat
 */

#define makedev(maj,min)    (((maj) << 8) | (min))
#define major(rdev)     (unsigned char)((rdev) >> 8)
#define minor(rdev)     (unsigned char)(rdev)

#endif /* _UCB_SYS_STAT_H_ */
