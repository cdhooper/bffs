#define MAXFRAG		8	/* superblock frsum count (max frags/block) */
#define oDEV_BSIZE 512

#include "sys/systypes.h"
#include "ufs/fs.h"


#define BOOT_BLOCK	0		/* Physical position of disk boot block	*/
#define SUPER_BLOCK	16		/* Position of superblock in fs		*/

#define FBSIZE bsize			/* fs basic block size */
#define FSIZE fsize			/* fs fragment block size */
extern long bsize;			/* fs basic block size */
extern long fsize;			/* fs fragment block size */

extern	struct	fs *superblock; 	/* current superblock		  */
