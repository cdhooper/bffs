#undef	DEBUG		/* Produce "dbprint" information */
#undef	FAST		/* Remove "unnecessary" checking */
#undef	INTEL		/* Device has Intel byte ordering */
#undef	RONLY		/* Device is read only */
#define	REMOVABLE	/* Device is removable - get change interrupts */
#undef	EXTENDED	/* Use extended trackdisk commands (to catch disk
			   changes which haven't been signaled yet) */


/*|-----------------------------------------------------|*
 *|       Nothing below should need to be changed       |*
 *|-----------------------------------------------------|*/


/*
 *	Set up the call mechanism for dbprint
 */
#ifdef DEBUG
#	define PRINT(x) dbprint x
#else
#	define PRINT(x) /* (x) */
#endif


/*
 *	Define the byte ordering routines
 */
#ifdef INTEL
#	define	DISK16(x) (((unsigned) x >> 8) | ((unsigned) x << 8))
#	define	DISK32(x) (((unsigned) x >> 24) | ((unsigned) x << 24) | \
			   (((unsigned) x << 8) & (255 << 16)) |	 \
			   (((unsigned) x >> 8) & (255 << 8)))
#	define	IC_SIZE(inode) DISK32(inode->ic_size.val[0])
#	define	IC_SETSIZE(inode, size) inode->ic_size.val[0] = DISK32(size)
#	define	IC_INCSIZE(inode, size) inode->ic_size.val[0] += DISK32(size)
#else
#	define	DISK16(x) (x)
#	define	DISK32(x) (x)
#	define	IC_SIZE(inode) (inode->ic_size.val[1])
#	define	IC_SETSIZE(inode, size) inode->ic_size.val[1] = size
#	define	IC_INCSIZE(inode, size) inode->ic_size.val[1] += size
#endif


/*
 *	Update statistics only when FAST mode is off
 */
#ifdef FAST
#	define UPSTAT(x) /* (x) */
#	define UPSTATVALUE(x,y) /* (x,y) */
#else
#	define UPSTAT(x) stat->x++
#	define UPSTATVALUE(x,y) stat->x += y
#endif


/*
 *	This is the information structure used by bffstool
 *	in order to snoop on the filesystem
 */
struct stat {
	ULONG	magic;			/* 'BFF\0' for BFFS FileSystem R0 */
	ULONG	size;			/* structure size (in longs)      */
	ULONG	unused;			/* checksum - not used            */
	ULONG	superblock;		/* updated only on packet request */
	ULONG	cache_head;		/* updated only on packet request */
	ULONG	cache_hash;		/* updated only on packet request */
	ULONG	*cache_size;		/* updated only on packet request */
	ULONG	*cache_cg_size;		/* updated only on packet request */
	ULONG	*cache_item_dirty;	/* updated only on packet request */
	ULONG	*cache_alloced;		/* updated only on packet request */
	ULONG	*disk_poffset;		/* updated only on packet request */
	ULONG	*disk_pmax;		/* updated only on packet request */
	ULONG	*unixflag;		/* updated only on packet request */
	ULONG	*resolve_symlinks;	/* updated only on packet request */
	ULONG	*case_independent;	/* updated only on packet request */
	ULONG	*dir_comments;		/* updated only on packet request */
	ULONG	*dir_comments2;		/* updated only on packet request */
	ULONG	*cache_used;		/* updated only on packet request */
	ULONG	*timer_secs;		/* updated only on packet request */
	ULONG	*timer_loops;		/* updated only on packet request */
	LONG	*GMT;			/* updated only on packet request */
	ULONG	*minfree;		/* updated only on packet request */
	ULONG	*og_perm_invert;	/* updated only on packet request */
	ULONG	hit_data_read;		/* cache stats */
	ULONG	hit_data_write;
	ULONG	miss_data_read;
	ULONG	miss_data_write;
	ULONG	hit_cg_read;		/* cg cache stats */
	ULONG	hit_cg_write;
	ULONG	miss_cg_read;
	ULONG	miss_cg_write;
	ULONG	cache_invalidates;
	ULONG	cache_locked_frags;
	ULONG	cache_moves;
	ULONG	cache_flushes;
	ULONG	cache_destroys;
	ULONG	cache_force_writes;
	ULONG	cache_cg_flushes;
	ULONG	cache_cg_force_writes;
	ULONG	direct_reads;		/* reads DMA'ed directly to/from memory */
	ULONG	direct_writes;
	ULONG	direct_read_bytes;
	ULONG	direct_write_bytes;
	ULONG	locates;		/* packets */
	ULONG	examines;
	ULONG	examinenexts;
	ULONG	flushes;
	ULONG	findinput;
	ULONG	findoutput;
	ULONG	reads;
	ULONG	writes;
	ULONG	renames;
	char	handler_version[32];
	char	disk_type[32];
	ULONG	gmt_shift;
};
