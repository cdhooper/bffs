struct stat {
	ULONG	magic;			/* 'BFF\0' for BFFS FileSystem R0 */
	ULONG	size;			/* structure size (in longs)	  */
	ULONG	phys_sectorsize;	/* physical device sector size */
	ULONG   superblock;		/* updated only on packet request */
	ULONG   cache_head;		/* updated only on packet request */
	ULONG   cache_hash;		/* updated only on packet request */
	ULONG   *cache_size;		/* updated only on packet request */
	ULONG   *cache_cg_size;		/* updated only on packet request */
	ULONG   *cache_item_dirty;	/* updated only on packet request */
	ULONG   *cache_alloced;		/* updated only on packet request */
	ULONG   *disk_poffset;		/* updated only on packet request */
	ULONG   *disk_pmax;		/* updated only on packet request */
	ULONG   *unix_paths;		/* updated only on packet request */
	ULONG   *resolve_symlinks;      /* updated only on packet request */
	ULONG   *case_dependent;        /* updated only on packet request */
	ULONG   *link_comments;		/* updated only on packet request */
	ULONG   *inode_comments;	/* updated only on packet request */
	ULONG   *cache_used;		/* updated only on packet request */
	ULONG   *timer_secs;		/* updated only on packet request */
	ULONG   *timer_loops;		/* updated only on packet request */
	LONG	*GMT;			/* updated only on packet request */
	ULONG   *minfree;		/* updated only on packet request */
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
};

struct cache_set {
	int blk;
	int flags;
	char *buf;
	struct cache_set *stack_up;
	struct cache_set *stack_down;
	struct cache_set *hash_up;
	struct cache_set *hash_down;
	struct cache_set *next;
};

/* Cach Hashtable */
#define HASH_SIZE       16      /* lines */

#define BTOC(x) ((x)<<2)

extern struct stat *stat;
extern struct MsgPort *fs;
extern struct cache_set *cache_stack;
extern struct cache_set **cache_hash;

int open_handler(char *name);
void close_handler(void);
int get_filesystems(char *name);
int sync_filesystem(void);
struct stat *get_stat(void);
