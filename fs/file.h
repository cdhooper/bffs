extern	int	it_size[NIADDR];	/* tree size for index blocks		 */
extern	int	unixflag;		/* follow Unix pathnames=1, AmigaDOS=0	 */
extern	int	write_pending;		/* fs modified, but cache not flushed    */

ULONG	idisk_blk();			/* Returns dsk frag addr given file blk #*/
ULONG	widisk_blk();			/* Returns dsk frag addr given file blk #*/
ULONG	cidisk_blk();			/* Returns dsk frag addr given file blk #*/
ULONG	block_allocate();		/* Returns dsk frag addr given file blk #*/
int	file_find();			/* Will return a lock, given path	 */
struct	icommon *inode_read();		/* Returns an inode structure from cache */
struct	icommon *inode_modify();	/* Returns an inode structure from cache (wr)*/
char	*nextslash();

#define I_DIR	0
#define I_FILE 	1
#define FSIZE	superblock->fs_fsize
#define FBSIZE	superblock->fs_bsize
#define FRAGS_PER_BLK	superblock->fs_frag

/* inode number to file system frag offset	*/
#define itofo(fs, x)	((x) & (INOPF(fs)-1))
/* inode number to frag number in block 	*/
#define itodo(fs, x)	(itoo(fs,x) / INOPF(fs))

/*  x % FBSIZE  */
#define blkoff(fs, x)	((x) & ~(fs)->fs_bmask)

/*  x % FSIZE  */
#define fragoff(fs, x)	((x) & ~(fs)->fs_fmask)

/*  x / FBSIZE  */
#define lblkno(fs, x)	((x) >> (fs)->fs_bshift)

/*  x / FSIZE  */
#define lfragno(fs, x)	((x) >> (fs)->fs_fshift)

/* Inodes per block */
#define INOPB(fs)	((fs)->fs_inopb)
/* Inodes per frag */
#define INOPF(fs)	((fs)->fs_inopb >> (fs)->fs_fragshift)


#ifndef NULL
#define NULL	0
#endif
