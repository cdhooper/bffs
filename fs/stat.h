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
	ULONG	*link_comments;		/* updated only on packet request */
	ULONG	*inode_comments;	/* updated only on packet request */
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
