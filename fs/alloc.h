#define bit_set(ptr, index)	ptr[(index) >> 3] |= 1<<((index) & 7)
#define bit_clr(ptr, index)	ptr[(index) >> 3] &= ~(1<<((index) & 7))
#define bit_val(ptr, index)	(ptr[(index) >> 3] & 1<<((index) & 7)) ? 1 : 0
#define bit_chk(ptr, index)	(ptr[(index) >> 3] & 1<<((index) & 7))

/* allocation optimization, time is default */
#define TIME	0
#define SPACE	1

#define FRAGS_PER_BLK   superblock->fs_frag
