#define ID_BFFS_DISK	(0x42464653L)  /* 'BFFS' filesystem */
/*
#define ID_BFFS_DISK	ID_FFS_DISK
#define ID_BFFS_DISK	ID_OFS_DISK
*/

#define STARTUPMSG	((struct FileSysStartupMsg *) BTOC(DevNode->dn_Startup))
#define ENVIRONMENT	((struct DosEnvec *) BTOC(STARTUPMSG->fssm_Environ))

#define DISK_DEVICE	((char *) BTOC(STARTUPMSG->fssm_Device)) + 1
#define DISK_UNIT 	STARTUPMSG->fssm_Unit
#define DISK_FLAGS	STARTUPMSG->fssm_Flags

#define BOOT_BLOCK	0		/* Physical position of disk boot block	*/
#define SUPER_BLOCK	16		/* Position of superblock in fs		*/
#define MAX_PART	8		/* Number disk partitions supported	*/


extern	ULONG	phys_sectorsize;	/* physical disk sector size, env */
extern	int	fs_partition;		/* current partition number	  */
extern	struct	fs *superblock; 	/* current superblock		  */
extern	struct	IOExtTD *trackIO;	/* pointer to trackdisk IORequest */
extern	ULONG	poffset;		/* disk partition start offset	  */
extern	ULONG	tranmask;		/* device DMA transfer mask	  */

extern	ULONG	pfragshift;		/* pointers in a frag		  */
extern	ULONG	pfragmask;		/* mask current pointer in frag	  */
extern	ULONG	fblkshift;		/* frags in a block		  */
extern	ULONG	fblkmask;		/* mask current frag in block	  */
extern  int	bsd44fs;		/* 0 = 4.3 BSD, 1 = 4.4 BSD	  */
extern  ULONG	fs_lfmask;		/* quick mask, bits flipped ala q */
extern  ULONG	fs_lbmask;		/* quick mask, bits flipped ala q */

#ifndef NULL
#define NULL 0
#endif

#define MAXSYMLINKLEN (12 + 3) * 4 - 1
