#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/vnode.h"
#include "ufs/fs.h"
#include "ufs/inode.h"
#include "ufs/fsdir.h"

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
#define MAX_BLOCK_SIZE	32768		/* Internal disk block buffer size	*/
#define MAX_PART	8		/* Number disk partitions supported	*/
#define SunBB_MAGIC	0xDABE		/* Sun Boot block magic number		*/
#define B44BB_MAGIC	0x82564557	/* BSD 44 Boot block magic number	*/


/* This structure was also created purely from examination of a Sun boot sector.
   I guarantee nothing. */
struct sun_label {
	char	bb_mfg_label[416];	/* Written by label cmd in format	*/
	u_long	bb_reserved1;		/* Dunno				*/
	u_short bb_rpm; 		/* Disk rotational speed per minute	*/
	u_short bb_ncyl;		/* Total disk cylinders			*/
	u_long	bb_reserved2;		/* Dunno 				*/
	u_long	bb_hw_interleave;	/* Hardware sector interleave		*/
	u_short bb_nfscyl;		/* Cylinders usable for filesystems	*/
	u_short bb_ncylres;		/* Number of reserved cylinders		*/
	u_short bb_heads;		/* Number of heads per cylinder		*/
	u_short bb_nspt;		/* Number of sectors per track		*/
	u_long	bb_reserved3;		/* Dunno				*/
	struct	sun_part_info {
		u_long fs_start_cyl;	/* Offset in disk cyl for filesystem	*/
		u_long fs_size;		/* Size in hw disk blocks		*/
	} bb_part[MAX_PART];
	u_short bb_magic;		/* magic number - 0xDABE		*/
	u_short bb_csum;		/* checksum				*/
};

struct bsd44_label {
	u_long	bb_magic;
	short	bb_dtype;
	short	bb_dsubtype;
	char	bb_mfg_label[16];
	char	bb_packname[16];

	u_long	bb_secsize;		/* Number of bytes per sector		*/
	u_long	bb_nspt;		/* Number of sectors per track		*/
	u_long	bb_heads;		/* Number of tracks per cylinder	*/
	u_long	bb_ncyl;		/* Total disk cylinders			*/
	u_long	bb_nspc;		/* Number of sectors per cylinder	*/
	u_long	bb_nsec;		/* Total disk sectors			*/

	u_short	bb_spare_spt;		/* Spares per track			*/
	u_short	bb_spare_spc;		/* Spares per cylinder			*/
	u_long	bb_spare_cyl;		/* Total spare cylinders		*/

	u_short bb_rpm; 		/* Disk rotational speed per minute	*/
	u_short	bb_hw_interleave;	/* Hardware sector interleave		*/
	u_short	bb_trackskew;		/* Hardware track skew			*/
	u_short	bb_cylskew;		/* Hardware cylinder skew		*/
	u_long	bb_headswitch;		/* Hardware head switch time (µsec)	*/
	u_long	bb_trkseek;		/* Hardware track-track seek time (µsec)*/
	u_long	bb_flags;		/* Hardware device flags		*/

	u_long	bb_drive_data[5];	/* drive data				*/
	u_long	bb_spares[5];		/* reserved spare info			*/
	u_long	bb_magic2;		/* boot block magic number		*/
	u_short	bb_checksum;		/* boot block checksum			*/

	u_short	bb_npartitions;		/* number of partitions			*/
	u_long	bb_bbsize;		/* size of boot area			*/
	u_long	bb_sbsize;		/* size of superblock			*/
	struct	bsd44_part_info {
		u_long	fs_size;	/* Size in hw disk blocks		*/
		u_long	fs_start_sec;	/* Partition start offset in sectors	*/
		u_long	fs_fsize;	/* Filesystem fragment size		*/
		u_char	fs_type;	/* Filesystem type			*/
		u_char	fs_frag;	/* Filesystem frags per block		*/
		u_short	fs_cpg;		/* Filesystem cylinders per group	*/
	} bb_part[MAX_PART];
};

extern	int	fs_partition;		/* current partition number	  */
extern	struct	fs *superblock; 	/* current superblock		  */
extern	struct	IOExtTD *trackIO;	/* pointer to trackdisk IORequest */
extern	struct	DosEnvec *Environment;	/* dos environment	 	  */
extern	ULONG	poffset;		/* disk partition start offset	  */
extern	ULONG	tranmask;		/* device DMA transfer mask	  */

extern	ULONG	pfragshift;		/* pointers in a frag		  */
extern	ULONG	pfragmask;		/* mask current pointer in frag	  */
extern	ULONG	fblkshift;		/* frags in a block		  */
extern	ULONG	fblkmask;		/* mask current frag in block	  */

#ifndef NULL
#define NULL 0
#endif
