#define S_IFMT	    0xF0000
#define S_IFREG     0x10000
#define S_IFDIR     0x20000
#define S_IFLNK     0x30000
#define S_IFCHR     0x40000
#define S_IFBLK     0x50000
#define S_IFSOCK    0xD0000        /* socket */
#define S_IFWHT     0xE0000        /* whiteout */
/* #define S_IFIFO     0x08000        fifo? */


#define	S_ISCHR(m)	(((m)&S_IFMT) == S_IFCHR)	/* char spec */
#define	S_ISBLK(m)	(((m)&S_IFMT) == S_IFBLK)	/* block spec */
#define S_ISDIR(m)      (((m)&S_IFMT) == S_IFDIR)	/* dir */
#define S_ISREG(m)      (((m)&S_IFMT) == S_IFREG)	/* regular */
#define S_ISLNK(m)      (((m)&S_IFMT) == S_IFLNK)	/* symlink */
#define S_ISSOCK(m)     (((m)&S_IFMT) == S_IFSOCK)	/* socket */
#define S_ISWHT(m)      (((m)&S_IFMT) == S_IFWHT)	/* whiteout */
/* #define S_ISFIFO(m)  (((m)&S_IFMT) == S_IFIFO)	   fifo */
