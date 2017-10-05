/* Little Endian */
#define	DISK16(x)	((((unsigned) x >> 8) & 255) | ((unsigned) (x & 255) << 8))
#define	DISK16SET(x, y)	x = ((unsigned) y >> 8) | ((unsigned) y << 8)
#define	DISK32(x)	(((unsigned) x >> 24) | ((unsigned) x << 24) | \
		  	 (((unsigned) x << 8) & (255 << 16)) |	 \
		  	 (((unsigned) x >> 8) & (255 << 8)))
#define	DISK32SET(x,y)	x = ((unsigned) y >> 24) | ((unsigned) y << 24) |\
		  	     (((unsigned) y << 8) & (255 << 16)) |	 \
		  	     (((unsigned) y >> 8) & (255 << 8))
#define	DISK64(x)	DISK32(x.val[0])
#define	DISK64SET(x,y)	DISK32SET(x.val[0], y)
#define	IC_SIZE(inode) DISK32(inode->ic_size.val[0])
#define	IC_SETSIZE(inode, size) inode->ic_size.val[0] = DISK32(size)
#define	IC_INCSIZE(inode, size) inode->ic_size.val[0] =		\
				DISK32(DISK32(inode->ic_size.val[0]) + size);
