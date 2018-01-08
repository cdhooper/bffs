/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#if 0
/* The following flags are set in the Makefile */
#undef	RONLY		/* Device is read only, only compile in read code   */
#define	DEBUG		/* Generate "debug" message code                    */
#undef	INTEL		/* Device has Intel byte ordering (ala Sun 386i fs) */
#undef	FAST		/* Remove "unnecessary" consistency checking        */
#endif

/* The following flags are not set in the Makefile */
#undef	SHOWDOTDOT	/* show . and .. on directory examines              */
#undef	NOPERMCHECK	/* never check file access permissions              */
#define	REMOVABLE	/* Device is removable - get change interrupts      */
#undef	TD_EXTENDED	/* Use extended trackdisk commands (to catch disk
			   changes which haven't been signaled yet).  This
			   works with very few devices and even causes
			   problems with scsi.device                        */


/* This is not implemented yet */
#undef	MUFS		/* enable muFS compatibility */


/*|-----------------------------------------------------|*
 *|       Nothing below should need to be changed       |*
 *|-----------------------------------------------------|*/

#undef DEBUG_ERRORS_ONLY

/*
 *	Set up the call mechanism for debug output
 */
#ifdef DEBUG
#ifdef DEBUG_ERRORS_ONLY
#	define PRINT(x)
#	define PRINT1(x)
#else
#	define PRINT(x)  debug0 x
#	define PRINT1(x) debug1 x
#endif
#	define PRINT2(x) debug2 x
void debug0(const char *fmt, ...);
void debug1(const char *fmt, ...);
void debug2(const char *fmt, ...);

extern int debug_level;
#else
#	define PRINT(x) /* (x) */
#	define PRINT1(x) /* (x) */
#	define PRINT2(x) /* (x) */
#endif


/*
 *	Define the byte ordering routines
 */

/* Little Endian first */
#ifdef INTEL
unsigned short	disk16(unsigned short x);
unsigned int	disk32(unsigned int x);

#	define	DISK16(x)	disk16(x)
#	define	DISK32(x)	disk32(x)
#	define	DISK64(x)	disk32(x.val[0])
#	define	DISK64SET(x,y)	x.val[0] = disk32(y)
#	define	IC_SIZE(inode)	disk32(inode->ic_size.val[0])
#	define	IC_SETSIZE(inode, size) inode->ic_size.val[0] = disk32(size)
#	define	IC_INCSIZE(inode, size) inode->ic_size.val[0] =		\
				disk32(disk32(inode->ic_size.val[0]) + size);

#else MOTOROLA
/* Big Endian is straight-forward on this architecture */
#	define	DISK16(x)	x
#	define	DISK32(x)	x
#	define	DISK64(x)	x.val[1]
#	define	DISK64SET(x,y)	x.val[1] = y
#	define	IC_SIZE(inode) (inode->ic_size.val[1])
#	define	IC_SETSIZE(inode, size) inode->ic_size.val[1] = size
#	define	IC_INCSIZE(inode, size) inode->ic_size.val[1] += size
#endif

extern int case_independent;	/* 1=case independent dir name searches */

#endif /* _CONFIG_H */
