#ifndef _UCB_DIRENT_H_
#define _UCB_DIRENT_H_

/*
 * The kernel defines the format of directory entries returned by
 * the getdirentries(2) system call.
 */
#include <sys/dirent.h>

/* definitions for library routines operating on directories. */
#ifdef cdh
#define DIRBLKSIZ	DEV_BSIZE
#else
#define	DIRBLKSIZ	1024
#endif

/* structure describing an open directory. */
typedef struct _dirdesc {
	int	dd_fd;		/* file descriptor associated with directory */
	long	dd_loc;		/* offset in current buffer */
	long	dd_size;	/* amount of data returned by getdirentries */
	char	*dd_buf;	/* data buffer */
	int	dd_len;		/* size of data buffer */
	long	dd_seek;	/* magic cookie returned by getdirentries */
	long	dd_rewind;	/* magic cookie for rewinding */
} DIR;

#include <sys/cdefs.h>

struct dirent *readdir(DIR *);

#endif /* _UCB_DIRENT_H_ */
