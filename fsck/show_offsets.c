#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <ufs/fs.h>

#define P(cname, name) printf("%s=%d\n", cname, &(fp->name))

main()
{
	struct fs *fp = NULL;

	P("fs_qbmask",fs_qbmask);
	P("fs_qfmask",fs_qfmask);
	P("fs_postblformat",fs_postblformat);
	P("fs_nrpos",fs_nrpos);
	P("fs_postbloff",fs_postbloff);
	P("fs_rotbloff",fs_rotbloff);
	P("fs_magic",fs_magic);
	P("fs_space[1]",fs_space[1]);
}

#ifdef NOT
	fs_qbmask;              /* ~fs_bmask - for use with quad size */
        quad    fs_qfmask;              /* ~fs_fmask - for use with quad size */
        long    fs_postblformat;        /* format of positional layout tables */
        long    fs_nrpos;               /* number of rotaional positions */
        long    fs_postbloff;           /* (short) rotation block list head */
        long    fs_rotbloff;            /* (u_char) blocks for each rotation */
        long    fs_magic;               /* magic number */
        u_char  fs_space[1];
#endif
