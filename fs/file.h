extern	int	unix_paths;	  /* follow Unix pathnames=1, AmigaDOS=0	*/
extern	int	write_pending;	  /* fs modified, but cache not flushed   	*/
extern	char	*linkname;	  /* resolved name of symlnk			*/
extern	int	read_link;	  /* 1=currently reading a link (packet.c)	*/

ULONG	idisk_blk();		  /* Returns dsk frag addr given file blk #	*/
ULONG	widisk_blk();		  /* Returns dsk frag addr given file blk #	*/
ULONG	cidisk_blk();		  /* Returns dsk frag addr given file blk #	*/
ULONG	block_allocate();	  /* Returns dsk frag addr given file blk #	*/
int	file_find();		  /* Will return a lock, given path		*/
struct	icommon *inode_read();	  /* Returns an inode structure from cache	*/
struct	icommon *inode_modify();  /* Returns an inode structure from cache (wr)	*/
char	*nextslash();
void	symlink_deallocate();	  /* Delete symlink given inum and inode size	*/

#define I_DIR	0
#define I_FILE 	1

#ifndef NULL
#define NULL	0
#endif
