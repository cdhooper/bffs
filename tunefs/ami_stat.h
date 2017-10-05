#define S_IFMT	    0xF0000
#define S_IFCHR     0x40000
#define S_IFBLK     0x50000

#define	S_ISCHR(m)	(((m)&S_IFMT) == S_IFCHR)
#define	S_ISBLK(m)	(((m)&S_IFMT) == S_IFBLK)
