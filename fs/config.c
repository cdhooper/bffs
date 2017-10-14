/* alloc.c */
#include "alloc.h"
int optimization = TIME;	/* filesystem write optimization method */

/* cache.c */
int     cache_size    = 32;	/* initial cache frags if 0 in mountlist */
int     cache_cg_size = 4;	/* number of cylinder group fsblock buffers */

/* dir.c */
int case_independent = 0;	/* 1=always case independent dir name searches */

/* file.c */
int     resolve_symlinks = 0;	/* 1=always resolve sym links for AmigaDOS */
int     unix_paths = 0;		/* 1=always Unix pathnames 0=AmigaDOS pathnames */
int     read_link = 0;          /* 1=attempt to resolve links for OS (sneaky) */

/* handler.c */
int     timer_secs  = 1;	/* delay seconda after write for cleanup */
int     timer_loops = 12;	/* maximum delays before forced cleanup */
char    *version    = "\0$VER: BFFSFileSystem 1.6 (13.Oct.2017) © Chris Hooper";
