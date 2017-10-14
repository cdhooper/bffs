#undef	INTEL		/* Device has Intel byte ordering (ala Sun 386i fs) */
#undef	RONLY		/* Device is read only, only compile in read code   */
#undef	SHOWDOTDOT	/* show . and .. on directory examines              */
#undef	FAST		/* Remove "unnecessary" consistency checking        */
#undef	NOPERMCHECK	/* never check file access permissions              */
#define	REMOVABLE	/* Device is removable - get change interrupts      */
#undef	EXTENDED	/* Use extended trackdisk commands (to catch disk
			   changes which haven't been signaled yet).  This
			   works with very few devices and even causes
			   problems with scsi.device                        */
#define	DEBUG		/* Generate "debug" message code                    */


/* These are not implemented yet */
#undef	SHARED		/* timed cache invalidate for sharing fs between
			   different filesystems or machines */
#undef	MUFS		/* enable muFS compatibility */


/*|-----------------------------------------------------|*
 *|       Nothing below should need to be changed       |*
 *|-----------------------------------------------------|*/


/*
 *	Set up the call mechanism for debug output
 */
#ifdef DEBUG
#	define PRINT(x)  debug x
#	define PRINT1(x) debug1 x
#	define PRINT2(x) debug2 x
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
unsigned short	disk16();
unsigned int	disk32();

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
