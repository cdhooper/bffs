/*	@(#)types.h 2.31 89/11/09 SMI; from UCB 7.1 6/4/86	*/

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef	__UCB_SYS_TYPES_H_
#define	__UCB_SYS_TYPES_H_

#include <sys/time.h>

/*
 * Basic system types.
 */

#ifndef cdh
#include <sys/stdtypes.h>		/* ANSI & POSIX types */
#endif


#ifndef	_POSIX_SOURCE

#ifndef cdh
#include <sys/sysmacros.h>
#endif

#define	quad		quad_t
#define u_quad		u_quad_t

typedef	unsigned char	u_char;
typedef	unsigned short	u_short;
typedef	unsigned int	u_int;
typedef	unsigned long	u_long;
typedef	unsigned short	ushort;		/* System V compatibility */
typedef	unsigned int	uint;		/* System V compatibility */
#endif	/* !_POSIX_SOURCE */

typedef	struct	_quad_t { long val[2]; } quad_t;
typedef	struct	_u_quad_t { unsigned long val[2]; } u_quad_t;
typedef	long	daddr_t;
typedef	char *	caddr_t;
typedef	unsigned long	ino_t;
#ifndef AMIGA
typedef	short	dev_t;
#endif
typedef	long	off_t;
typedef	unsigned short	uid_t;
typedef	unsigned short	gid_t;
#define __uid_t_defined
#define __gid_t_defined

#define	NBBY	8		/* number of bits in a byte */

#ifndef	howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif


#endif /* __UCB_SYS_TYPES_H_ */
