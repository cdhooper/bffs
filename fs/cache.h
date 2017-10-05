extern	int cache_size;			/* max frags allowed in cache */
extern	int cache_cg_size;		/* max cgs in cg cache */
char	*cache_frag();			/* get buffer for specified frag */
char	*cache_frag_write();		/* get buffer for frag, mark dirty */
char	*cache_available();		/* get buffer if in cache, else NULL */
struct	cg	  *cache_cg();		/* get buffer for specified cg */
struct	cg	  *cache_cg_write();	/* get buffer for cg, mark dirty */
int	cache_frag_flush();		/* sync disk with specified frag */

/* This is the basic node for the cache routines */
struct cache_set {
	int blk;			/* disk frag address of node */
	int flags;			/* frag node flags (see below) */
	char *buf;			/* memory buffer address */
	struct cache_set *stack_up;	/* older cache nodes */
	struct cache_set *stack_down;	/* newer cache nodes */
	struct cache_set *hash_up;	/* hash chain parent */
	struct cache_set *hash_down;	/* hash chain child  */
	struct cache_set *next;		/* sorted order higher address */
};

/* cache frag node flags */
#define CACHE_CLEAN   0			/* frag clean	   */
#define CACHE_DIRTY   1			/* frag dirty	   */
#define CACHE_LOCKED  2			/* locked in cache */

int	cache_hash();			/* internal hash routine */
struct	cache_set *cache_node();	/* internal node lookup routine */
struct	cache_set *cache_alloc();	/* internal cache alloc routine */
struct	cache_set *cache_getnode();	/* internal LRU lookup routine */

#ifndef NULL
#define NULL 0
#endif
