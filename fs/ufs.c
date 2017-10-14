#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/trackdisk.h>
#include <devices/hardblocks.h>
#include <dos/filehandler.h>

/*
#define SYS_TYPES_H
*/

#ifdef GCC
#include <proto/exec.h>
#include <inline/dos.h>
struct MsgPort *CreatePort(UBYTE *name, long pri);
#else
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#endif

#include "config.h"
#include "superblock.h"
#include "handler.h"
#include "ufs.h"
#include "ufs/sun_label.h"
#include "ufs/bsd_label.h"
#include "fsmacros.h"
#include "file.h"
#include "stat.h"
#include "request.h"

#define S static

S struct IOExtTD *trackIO = NULL;/* trackdisk IORequest for data	  */
S struct IOExtTD *dchangeIO = NULL; /* trackdisk IORequest for disk change */
S struct MsgPort *devport = NULL;/* pointer to port for comm. with device  */
S struct MsgPort *dchangePort = NULL; /* port for disk change notification */
S char	*cgsummary_base;	/* memory copy of cg summary information  */
S int	no_Sun_label;		/* 1 = Sun disk label not found		  */
S int	no_BSD_label;		/* 1 = BSD disk label not found		  */
S int	env_setup = 1;		/* set up the ufs environment (once)	  */
S char    *TRACKDISK = "trackdisk.device"; /* use this device for changeint */
S char    *MESSYDISK = "messydisk.device"; /* if this device is specified   */
S ULONG	superblock_sector = SUPER_BLOCK; /* default superblock location   */

struct	fs *superblock = NULL; 	/* current superblock			  */
int	bsd44fs = 0;		/* 0 = 4.3 BSD fs, 1 = 4.4 BSD fs	  */
int	openerror = 1;		/* 1 = OpenDevce failed			  */
int	physical_ro = 0;	/* disk is physically write-protected	  */
int	fs_partition;		/* current partition			  */
ULONG	fs_lfmask;		/* global for faster access, ~fmask	  */
ULONG	fs_lbmask;		/* global for faster access, ~bmask	  */
ULONG	phys_sectorsize = 512;	/* physical disk sector size, from env	  */
ULONG	poffset;		/* computed byte offset for fs byte zero  */
ULONG	pmax;			/* computed byte maximum for fs byte zero */
ULONG	tranmask = 0;		/* device DMA transfer mask		  */
long	fsize;			/* superblock filesystem fragment size	  */
long	bsize;			/* superblock filesystem block size	  */

extern	ULONG GMT;		/* GMT time offset			  */
extern	char *handler_name;	/* device name handler was started as     */

/* interrupt handler declarations for disk change routines */
extern VOID   IntHandler(VOID);
struct InterruptData {	/* stack space for int handler (data) */
	struct	Task *sigTask;
	ULONG	sigMask;
} IntData;
struct Interrupt     IntHand;	/* exec interrupt struct for int handler */

int    dcsigBit = 0;		/* signal bit in use for disk changes */
int    dcsubmitted = 0;		/* whether a disk change interrupt IO
				   request has been submitted */


/* data_read()
 *	This routine is the only interface to read the raw disk
 *	data which is used by the filesystem.
 *
 *	   buf = memory buffer address (where to put data)
 *	   num = starting disk fragment (fragment size specified in FSIZE)
 *	  size = number of bytes to read from the disk
 */
int data_read(buf, num, size)
char *buf;
int num;
{
	int index;

	motor_is_on = 1;

#ifdef EXTENDED
	trackIO->iotd_Req.io_Command = ETD_READ; 	/* set IO Command */
#else
	trackIO->iotd_Req.io_Command = CMD_READ; 	/* set IO Command */
#endif
	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to read into */
	trackIO->iotd_Req.io_Offset  = num * FSIZE + poffset;

	PRINT(("data_read from faddr=%d sector=%d size=%d\n",
		num, trackIO->iotd_Req.io_Offset / phys_sectorsize, size));

	UPSTAT(direct_reads);
	UPSTATVALUE(direct_read_bytes, size);

#ifndef FAST
	if ((trackIO->iotd_Req.io_Offset > pmax) || (num < 0)) {
		PRINT2(("read byte %d is out of range for partition\n",
			trackIO->iotd_Req.io_Offset));
		return(1);
	}
#endif

	if (DoIO((struct IORequest *) trackIO)) {
	    PRINT2(("drf faddr=%d sector=%d size=%d\n", num,
		    trackIO->iotd_Req.io_Offset / phys_sectorsize, size));
	    while (do_request(REQUEST_BLOCK_BAD_R,
			      num * FSIZE / phys_sectorsize, size))
		for (index = 0; index < 4; index++)
		    if (!DoIO((struct IORequest *) trackIO))
			return(0);
		    else
			PRINT2(("drf faddr=%d sector=%d size=%d\n", num,
				trackIO->iotd_Req.io_Offset / phys_sectorsize,
				size));
	    return(1);
	}

	return(0);
}

/* data_write()
 *	This routine is the only interface to write the raw disk
 *	data which is used by the filesystem.
 *
 *	   buf = memory buffer address (where to get data)
 *	   num = starting disk fragment (fragment size specified in FSIZE)
 *	  size = number of bytes to write to the disk
 */
#ifndef RONLY
int data_write(buf, num, size)
char *buf;
int num;
{
	int index;

	motor_is_on = 1;

#ifdef EXTENDED
	trackIO->iotd_Req.io_Command = ETD_WRITE; 	/* set IO Command */
#else
	trackIO->iotd_Req.io_Command = CMD_WRITE; 	/* set IO Command */
#endif
	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to read into */
	trackIO->iotd_Req.io_Offset  = num * FSIZE + poffset;

	PRINT(("data_write to faddr=%d sector=%d size=%d\n", num,
		trackIO->iotd_Req.io_Offset / phys_sectorsize, size));

	UPSTAT(direct_writes);
	UPSTATVALUE(direct_write_bytes, size);

#ifndef FAST
	if ((trackIO->iotd_Req.io_Offset > pmax) || (num < 0)) {
		PRINT2(("write byte %d is out of range for partition\n",
			trackIO->iotd_Req.io_Offset));
		return(1);
	}
#endif

	if (DoIO((struct IORequest *) trackIO)) {
	    PRINT2(("dwf faddr=%d sector=%d size=%d\n", num,
		    trackIO->iotd_Req.io_Offset / phys_sectorsize, size));
	    while (do_request(REQUEST_BLOCK_BAD_W, num *
			      FSIZE / phys_sectorsize, size))
		for (index = 0; index < 4; index++)
		    if (!DoIO((struct IORequest *) trackIO))
			return(0);
		    else
			PRINT2(("dwf faddr=%d sector=%d size=%d\n", num,
				trackIO->iotd_Req.io_Offset / phys_sectorsize,
				size));
	    return(1);
	}

	return(0);
}
#endif


/* open_ufs()
 *	This routine will open the driver for the media device
 *	and initialize all structures used to communicate with
 *	that device.
 */
int open_ufs()
{
	ULONG flags;
	ULONG temp;
	extern int resolve_symlinks;
	extern int case_independent;
	extern int unix_paths;
	extern int link_comments;
	extern int inode_comments;
	extern int og_perm_invert;
	extern int minfree;
	extern char *version;
	unsigned char	temp_gmt;


	/* first report the handler version */
	sprintf(stat->handler_version, "%.4s", version + 22);


#	ifdef INTEL
		strcat(stat->handler_version, "I");
#	endif


#	ifdef RONLY
		strcat(stat->handler_version, "R");
#	endif


#	ifdef SHOWDOTDOT
		strcat(stat->handler_version, ".");
#	endif


#	ifdef FAST
		strcat(stat->handler_version, "F");
#	endif


#	ifdef NOPERMCHECK
		strcat(stat->handler_version, "P");
#	endif


#	ifdef REMOVABLE
		strcat(stat->handler_version, "r");
#	endif


#	ifdef EXTENDED
		strcat(stat->handler_version, "E");
#	endif


#	ifdef DEBUG
		strcat(stat->handler_version, "D");
#	endif


#	ifdef SHARED
		strcat(stat->handler_version, "S");
#	endif


#	ifdef MUFS
		strcat(stat->handler_version, "M");
#	endif



	stat->disk_type[0] = '\0';


	/* CreatePort and pass the result to CreateExtIO */
	openerror = 0;
	if (devport = CreatePort("BFFSdisk_data", 0))
	    if (trackIO = (struct IOExtTD *)
		CreateExtIO(devport, sizeof(struct IOExtTD)) )
		if (OpenDevice(DISK_DEVICE, DISK_UNIT,
			       (struct IORequest *) trackIO, DISK_FLAGS))
		    openerror = 1;
		else
		    goto good_open;

	close_ufs();
	sprintf(stat->disk_type, "%s:%cd%d Dev?", handler_name,
		tolower(*(DISK_DEVICE)), DISK_UNIT);
	return(1);

	good_open:
	openerror = 0;

#ifdef REMOVABLE
	/* check to see if there is a disk in the drive... */
	trackIO->iotd_Req.io_Command = TD_CHANGESTATE;

	/* if request succeeds and NO disk present, give up */
	if (!DoIO((struct IORequest *) trackIO) && trackIO->iotd_Req.io_Actual) {
		PRINT(("no disk present, closing ufs\n"));
		close_ufs();
		sprintf(stat->disk_type, "%s:%cd%d Dsk?", handler_name,
			tolower(*(DISK_DEVICE)), DISK_UNIT);
		return(1);
	}
#endif

	/* check to see if disk in drive is write-protected */
	trackIO->iotd_Req.io_Command = TD_PROTSTATUS;

	if (!(DoIO((struct IORequest *) trackIO) && !(trackIO->iotd_Req.io_Error)))
		physical_ro = trackIO->iotd_Req.io_Actual;
	else
		physical_ro = 0;


#ifdef EXTENDED
	/* get the current change number of the disk */
	trackIO->iotd_Req.io_Command = TD_CHANGENUM; 	/* set IO Command */

	if (DoIO(trackIO))
	    PRINT(("was unable to get change count\n"));
	else {
	    trackIO->iotd_Count    = trackIO->iotd_Req.io_Actual;
	    trackIO->iotd_SecLabel = NULL;
	    PRINT(("changenum is %d\n", trackIO->iotd_Req.io_Actual));
	}
#endif

	if (env_setup) {
		env_setup = 0;
		tranmask = ~ENVIRONMENT->de_Mask;
		flags	 =  ENVIRONMENT->de_PreAlloc;
		temp	 =  ENVIRONMENT->de_SizeBlock * sizeof(long);
		switch (temp) {
		     case 512:
		     case 1024:
		     case 2048:
		     case 4096:
		     case 8192:
		     case 16384:
			phys_sectorsize = temp;
			break;
		     default:
			PRINT2(("Invalid RDB sector size of %d\n", temp));
		}
		pmax = ENVIRONMENT->de_BlocksPerTrack * phys_sectorsize *
		       (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces;

		resolve_symlinks ^= (flags) & 1;
		case_independent ^= (flags >> 1) & 1;
		unix_paths	 ^= (flags >> 2) & 1;
		link_comments	 ^= (flags >> 3) & 1;
		inode_comments	 ^= (flags >> 4) & 1;
		og_perm_invert	 ^= (flags >> 5) & 1;
		minfree		 ^= (flags >> 6) & 1;
		temp_gmt	  = (flags >> 8) & 255;
		GMT		  = *((char *) &temp_gmt);

		PRINT(("tm=%x rs=%d ci=%d up=%d lc=%d ",
			tranmask, ENVIRONMENT->de_Reserved, case_independent,
			unix_paths, link_comments));
		PRINT(("ic=%d pi=%d mf=%d gmt=%d\n",
			inode_comments, og_perm_invert, minfree, temp_gmt));

	}

	/* must be reset for every disk change */
	superblock_sector = SUPER_BLOCK * 512 / phys_sectorsize;

	return(0);
}


/* close_ufs()
 *	This routine performs the complement operation of open_ufs()
 *	It shuts down the device and deallocates the associated data
 *	structures.
 */
int close_ufs()
{
	if (openerror)
		PRINT(("Failed to open device.\n"));
	else if (trackIO)
		CloseDevice((struct IORequest *) trackIO);

	if (devport) {
		DeletePort(devport);
		devport = 0;
	}

	if (trackIO) {
		DeleteExtIO(trackIO);
		trackIO = NULL;
	}
}


/* superblock_read()
 *	This routine is used to read the superblock from the disk
 *	at the specified partition.  It calls partition_info_read()
 *	to set the disk poffset to the proper location.
 *	Consistency checking is done on the superblock to ensure
 *	that it is valid before returning.
 */
int superblock_read(partition)
int partition;
{
	int	index	   = 0;
	int	csize 	   = 1;
	int	ssector	   = 0;
	struct fs *temp_sb;

	if (partition_info_read(partition))
		fs_partition = 0;

	superblock = NULL;
	temp_sb = (struct fs *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (temp_sb == NULL) {
		PRINT(("superblock_read: unable to allocate %d bytes!\n",
			phys_sectorsize));
		return(1);
	}

	for (ssector = 0; ssector < 128; ssector++) {
	    switch (ssector) {	/* this implements an incremental guessing
				   algorithm which hit me during a nightmare */
		case 5:
			ssector = 16;
			break;
		case 17:
			ssector = 48;
			break;
		case 49:
			ssector = 112;
			break;
		case 113:
			ssector = 240;
			break;
	    }
	    FSIZE = phys_sectorsize;
	    if (data_read(temp_sb, superblock_sector, phys_sectorsize)) {
		PRINT2(("** data read fault for superblock_read\n"));
		break;
	    }

	    FBSIZE = DISK32(temp_sb->fs_bsize);
	    if ((FBSIZE == 4096)  || (FBSIZE == 8192) ||
		(FBSIZE == 16384) || (FBSIZE == 32768)) {
		superblock = (struct fs *) AllocMem(FBSIZE, MEMF_PUBLIC);
		if (superblock == NULL)
		    PRINT2(("superblock_read: could not allocate %d bytes\n",
			   FBSIZE));
		break;
#ifdef DEBUG
	    } else {
		PRINT(("Superblock not found at sector %d\n", ssector +
			superblock_sector));
	        if ((FBSIZE != 4096)  && (FBSIZE != 8192) &&
		    (FBSIZE != 16384) && (FBSIZE != 32768))
			PRINT(("bsize %d ", FBSIZE));
		PRINT(("\n"));
#endif
	    }
	    poffset += phys_sectorsize;
	}

	if (superblock == NULL) {
		FreeMem(temp_sb, phys_sectorsize);
		return(1);
	}

	FSIZE = phys_sectorsize;
	data_read(superblock, superblock_sector, FBSIZE);
	FreeMem(temp_sb, phys_sectorsize);
	FSIZE = DISK32(superblock->fs_fsize);


PRINT(("part=%d fs_bsize=%d fs_size=%d frags x %d bytes/frag = %dk\n",
	fs_partition, FBSIZE, DISK32(superblock->fs_size), FSIZE,
	DISK32(superblock->fs_size) * FSIZE / 1024));
PRINT(("cginfo=%d len=%d ncg=%d fpg=%d cgsize=%d ileave=%d\n",
	DISK32(superblock->fs_csaddr), DISK32(superblock->fs_cssize),
	DISK32(superblock->fs_ncg), DISK32(superblock->fs_fpg),
	DISK32(superblock->fs_cgsize), DISK32(superblock->fs_interleave)));
PRINT(("csaddr=%d cssize=%d\n", DISK32(superblock->fs_csaddr),
	DISK32(superblock->fs_cssize)));
PRINT(("fsid=%08x%08x\n", DISK32(superblock->fs_id)));

	if (DISK32(superblock->fs_magic) != FS_MAGIC) {
		PRINT(("Superblock for partition %d not found (magic).\n",
			fs_partition));
		goto bad_superblock;
	}

	if (cgsummary_read()) {
		PRINT(("error reading cg summary info\n"));
		goto bad_superblock;
	}

	fblkshift = DISK32(superblock->fs_fragshift);

/* ORIGINAL
	fblkmask  = 1;
	for (index = 1; index < fblkshift; index <<= 1) {
		fblkmask <<= 1;
		fblkmask  |= 1;
	}

	pfragshift = 1;
	pfragmask  = 1;
	csize = DISK32(superblock->fs_nindir) >> fblkshift;
	for (index = 2; index < csize; index <<= 1) {
		pfragshift++;
		pfragmask <<= 1;
		pfragmask  |= 1;
	}
*/

	fblkmask  = 0;
	for (index = 0; index < fblkshift; index++) {
		fblkmask <<= 1;
		fblkmask  |= 1;
	}

	pfragshift = 0;
	pfragmask  = 0;
	csize = DISK32(superblock->fs_nindir) >> fblkshift;
	for (index = 1; index < csize; index <<= 1) {
		pfragshift++;
		pfragmask <<= 1;
		pfragmask  |= 1;
	}

	PRINT(("fblkshift=%d fblkmask=%d pfragshift=%d pfragmask=%d\n",
		fblkshift, fblkmask, pfragshift, pfragmask));

#	ifdef RONLY
		superblock->fs_ronly = 1;     /* Sorry, read only  */
#	else
		superblock->fs_ronly = 0;     /* Start it out Read/Write */
#	endif

	if (physical_ro)
		superblock->fs_ronly = 1;

	superblock->fs_fmod  = 0;      /* superblock clean flag */
	superblock->fs_clean = 0;      /* superblock clean unmount flag */

	if (strlen(stat->disk_type))
		sprintf(stat->disk_type + strlen(stat->disk_type),
			"%cd%d%c", tolower(*(DISK_DEVICE)), DISK_UNIT,
			fs_partition + 'a');
	else
		sprintf(stat->disk_type, "%s:%cd%d%c", handler_name,
			tolower(*(DISK_DEVICE)), DISK_UNIT,
			fs_partition + 'a');

	/* determine filesystem type and patch up if older */
	ffs_oldfscompat();

	return(0);

	bad_superblock:
	FreeMem(superblock, FBSIZE);
	superblock = NULL;
	return(1);
}


/* cgsummary_read()
 *	This routine is used by superblock_read() to get the
 *	associated cylinder group summary information off the
 *	disk and set up pointers in the superblock to that data.
 */
cgsummary_read()
{
	char *space;
	int blks;
	int index;

	space = cgsummary_base =
		(char *) AllocMem(DISK32(superblock->fs_cssize), MEMF_PUBLIC);
	if (cgsummary_base == NULL) {
		PRINT(("Unable to allocate %d bytes for superblock cg summary info\n",
			DISK32(superblock->fs_cssize)));
		return(1);
	}

	if (data_read(cgsummary_base, DISK32(superblock->fs_csaddr),
		      DISK32(superblock->fs_cssize)))
		PRINT(("data read fault for superblock cg summary info\n"));

	blks = howmany(DISK32(superblock->fs_cssize), FBSIZE);

	for (index = 0; index < blks; index++) {	/* count up blocks */
		superblock->fs_csp[index] = (struct csum *) space;
		space += FBSIZE;
	}
	return(0);
}


/* partition_info_read()
 *	This routine will call the various disk label sensing routines
 *	to get the start address of the specified partition.  If no
 *	disk labels exist, this routine returns an error code.
 */
int partition_info_read(partition)
int partition;
{
	int ret = 0;

	poffset = ENVIRONMENT->de_LowCyl * ENVIRONMENT->de_Surfaces *
		  ENVIRONMENT->de_BlocksPerTrack * phys_sectorsize;

	FSIZE = phys_sectorsize;

	if (sun_label_read(partition) && bsd44_label_read(partition))
		ret = 1;		/* could not read either */

	return(ret);
}


#ifdef 0		/* doesn't work yet, don't include it */
/* rdb_label_read()
 *	This routine will read an Amiga RDB disk label and return
 *	the specified partition.
 *	THIS ROUTINE HAS NOT YET BEEN IMPLEMENTED.
 */
rdb_label_read(new_partition)
int new_partition;
{
	ULONG	rdb_pos;
	ULONG	old_poffset;
	ULONG	partblk;
	struct	RigidDiskBlock *rdb;
	struct	PartitionBlock *pblock;

	PRINT(("rdb_label_read\n"));
	old_poffset = poffset;
	poffset = 0;

	rdb = (struct RigidDiskBlock *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (rdb == NULL) {
		PRINT(("rdb_label_read: unable to allocate %d bytes!\n",
			phys_sectorsize));
		return(1);
	}
	for (rdb_pos = 0; rdb_pos < RDB_LOCATION_LIMIT; rdb_pos++) {
	    data_read(rdb, rdb_pos, phys_sectorsize);
	    if (rdb->rdb_ID == IDNAME_RIGIDDISK) {
		ENVIRONMENT->de_Surfaces = rdb->rdb_Heads;
		ENVIRONMENT->de_BlocksPerTrack = rdb->rdb_Sectors;
		ENVIRONMENT->de_LowCyl = 0;
		PRINT(("rdb(%d): surf=%d bpt=%d\n", rdb_pos, rdb->rdb_Heads,
			rdb->rdb_Sectors));

		pblock = (struct PartitionBlock *) rdb;
		partblk = rdb->rdb_PartitionList;
		do {
		    data_read(pblock, partblk, phys_sectorsize);
		    PRINT(("blk=%d name=%*s next=%d\n", partblk,
			   pblock->pb_DriveName[0], pblock->pb_DriveName + 1,
			   pblock->pb_Next));
		    partblk = pblock->pb_Next;
		} while (pblock->pb_Next != -1);

		poffset = ENVIRONMENT->de_LowCyl * ENVIRONMENT->de_Surfaces *
			  ENVIRONMENT->de_BlocksPerTrack * phys_sectorsize;
		FreeMem(rdb, phys_sectorsize);
		return(0);
	    }
	}

	FreeMem(rdb, phys_sectorsize);
	poffset = old_poffset;

	return(1);
}
#endif


/* sun_label_read()
 *	This routine is used to sense a Sun disk label and set the
 *	partition offset (poffset) to the appropriate value to get
 *	the specified partition.
 */
sun_label_read(new_partition)
int new_partition;
{
	struct	sun_label *label;	/* disk partition table */
	int	part_off;
	int	part_end;

	if (no_Sun_label)
		return(1);

	if (new_partition & 8) {
		no_Sun_label = 1;
		return(1);
	}

	new_partition &= 7;

	part_off = poffset;

	label = (struct sun_label *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (label == NULL) {
		PRINT(("sun_label_read: unable to allocate %d bytes!\n",
			phys_sectorsize));
		return(1);
	}
	if (data_read(label, BOOT_BLOCK, phys_sectorsize)) {
		PRINT2(("** data read fault for Sun label\n"));
		no_Sun_label = 1;
		FreeMem(label, phys_sectorsize);
		return(1);
	}

	poffset = part_off +
		  DISK32((label->bb_part[new_partition]).fs_start_cyl) *
		  DISK32(label->bb_heads) * DISK32(label->bb_nspt) * phys_sectorsize;

	part_end = (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces *
		    ENVIRONMENT->de_BlocksPerTrack * phys_sectorsize;

	if (DISK32(label->bb_magic) != SunBB_MAGIC) {
		poffset = part_off;
		FreeMem(label, phys_sectorsize);
		no_Sun_label = 1;
		return(1);
	}

	if ((label->bb_part[new_partition].fs_size == 0) ||
	    (DISK32(label->bb_part[new_partition].fs_size) * phys_sectorsize >
	     part_end - part_off)) {
		PRINT(("bad values for Sun disk label partition=%d\n",
			new_partition));
		PRINT(("handler_begin=%d handler_end=%d size=%d\n", part_off,
			part_end, part_end - part_off));
		PRINT(("part_begin=%d part_end=%d size=%d\n", poffset, poffset +
			DISK32(label->bb_part[new_partition].fs_size) *
			phys_sectorsize,
			DISK32(label->bb_part[new_partition].fs_size)));
		poffset = part_off;
		FreeMem(label, phys_sectorsize);
		no_Sun_label = 1;
		return(1);
	}
	PRINT(("Found SunOS label.\n"));

	fs_partition = new_partition;
	strcat(stat->disk_type, "Sun ");

	FreeMem(label, phys_sectorsize);
	return(0);
}


/* bsd44_label_read()
 *	This routine is used to sense a BSD44 disk label and set the
 *	partition offset (poffset) to the appropriate value to get
 *	the specified partition.
 */
bsd44_label_read(new_partition)
int new_partition;
{
	struct	bsd44_label *label;	/* disk partition table */
	ULONG	*buffer;
	ULONG	part_off;
	ULONG	part_end;
	int	fs_offset;
	int	rc = 1;

	if (no_BSD_label)
		return(1);

	if (new_partition & 16) {
		no_BSD_label = 1;
		return(1);
	}

	new_partition &= 7;
	part_off = poffset;

	buffer = (ULONG *) AllocMem(phys_sectorsize, MEMF_PUBLIC);

	if (buffer == NULL) {
		PRINT(("bsd44_label_read: unable to allocate %d bytes!\n",
			phys_sectorsize));
		return(1);
	}

	if (rc = data_read(buffer, BOOT_BLOCK, phys_sectorsize)) {
		PRINT2(("** data read fault for bsd label\n"));
		no_BSD_label = 1;
		goto retvalue;
	}

	for (fs_offset = 0; fs_offset < phys_sectorsize / sizeof(ULONG); fs_offset++) {
		label = (struct bsd44_label *) (buffer + fs_offset);
		if ((DISK32(label->bb_magic)  == B44BB_MAGIC) &&
		    (DISK32(label->bb_magic2) == B44BB_MAGIC))
			goto good_value;
	}

	no_BSD_label = 1;
	goto retvalue;

	good_value:
	poffset += DISK32(label->bb_part[new_partition].fs_start_sec) * phys_sectorsize;

	part_end = (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces *
		    ENVIRONMENT->de_BlocksPerTrack * phys_sectorsize;

	if ((label->bb_part[new_partition].fs_size == 0) ||
	    (DISK32(label->bb_part[new_partition].fs_size) *
	     DISK32(label->bb_secsize) > part_end - part_off)) {
		PRINT(("bad values for BSD44 disk label partition=%d\n",
			new_partition));
		PRINT(("handler_begin=%d handler_end=%d size=%d\n", part_off,
			part_end, part_end - part_off));
		PRINT(("part_begin=%d part_end=%d size=%d\n", poffset, poffset +
			DISK32(label->bb_part[new_partition].fs_size) *
			DISK32(label->bb_secsize),
			DISK32(label->bb_part[new_partition].fs_size) *
			DISK32(label->bb_secsize)));
		poffset = part_off;
		no_BSD_label = 1;
		goto retvalue;
	}

	PRINT(("Found BSD44 label, using partition=%d\n", new_partition));
/*
	PRINT(("handler_begin=%d handler_end=%d size=%d\n", part_off,
		part_end, part_end - part_off));
	PRINT(("part_begin=%d part_end=%d size=%d\n", poffset,
		poffset + label->bb_part[new_partition].fs_size *
		DISK32(label->bb_secsize),
		DISK32(label->bb_part[new_partition].fs_size) *
		DISK32(label->bb_secsize)));
*/

	fs_partition = new_partition;
	strcat(stat->disk_type, "BSD ");
	rc = 0;

	retvalue:
	FreeMem(buffer, phys_sectorsize);
	return(rc);
}


/* find_superblock()
 *	This routine will first attempt to locate the superblock
 *	in the environment specified partition.  If it is not found
 *	there and the partition has a disk label, the other
 *	partitions will be searched for a superblock.
 */
int find_superblock()
{
	int part;

	no_Sun_label = 0;
	no_BSD_label = 0;

	if (superblock_read(ENVIRONMENT->de_Reserved)) {
		if (no_Sun_label && no_BSD_label) {
			PRINT(("Superblock not found at beginning of partition\n"));
			return(2);
		}
		if (!no_Sun_label || !no_BSD_label) {
		    PRINT(("s=%d b=%d\n", no_Sun_label, no_BSD_label));
		    PRINT(("scanning other partitions\n"));
		    for (part = 0; superblock_read(part); part++) {
			if (part == MAX_PART) {
				PRINT(("Giving up.\n"));
				return(2);
			}
		    }
		}
	}
	return(0);
}


/* motor_off()
 *	This routine turns the drive motor and light off.
 *	It is called when there are no outstanding packets and
 *	there is no data dirty in the cache.
 */
int motor_off()
{
	motor_is_on = 0;

	if (trackIO == NULL)
		return;

#ifdef REMOVABLE
	trackIO->iotd_Req.io_Command	= TD_MOTOR;
	trackIO->iotd_Req.io_Flags	= 0x0;	/* Motor status */
	trackIO->iotd_Req.io_Length	= 0;	/* define 0=off, 1=on */
	DoIO((struct IORequest *) trackIO);
#endif
}


/* motor_on()
 *	This routine turns on the disk motor.  It is called
 *	when there is dirty data in the cache.  The drive
 *	otherwise gets turned on whenever there is data read
 *	from or written to the disk (automatically).
 */
int motor_on()
{
	motor_is_on = 1;

	if (trackIO == NULL)
		return;

#ifdef REMOVABLE
	trackIO->iotd_Req.io_Command	= TD_MOTOR;
	trackIO->iotd_Req.io_Flags	= 0x0;	/* Motor status */
	trackIO->iotd_Req.io_Length	= 1;	/* define 0=off, 1=on */
	DoIO((struct IORequest *) trackIO);
#endif
}


#ifndef RONLY
/* superblock_flush()
 *	This routine will write the superblock out to disk only if
 *	it was modified since the last call to superblock_flush()
 */
superblock_flush()
{
	time_t	timeval;

	if (superblock->fs_fmod) {
	    PRINT(("superblock flush\n"));
	    time(&timeval);

	    /* 2922 = clock birth difference in days, 86400 = seconds in a day */
            /* 18000 = clock birth difference remainder  5 * 60 * 60 */
	    superblock->fs_time = DISK32(timeval + 2922 * 86400 - GMT * 3600);
	    superblock->fs_fmod = 0;

	    if (data_write(superblock, lfragno(superblock,
			   superblock_sector * phys_sectorsize), FBSIZE))
		PRINT2(("** data write fault for superblock"));

	    if (data_write(cgsummary_base, DISK32(superblock->fs_csaddr),
			   DISK32(superblock->fs_cssize)))
		PRINT2(("** data write fault for superblock cg summary info\n"));
	}
}
#endif


/* superblock_destroy()
 *	This routine will deallocate the space consumed by the
 *	active superblock and its associated cylinder group
 *	summary information.
 */
superblock_destroy()
{
	if (cgsummary_base)
		FreeMem(cgsummary_base, DISK32(superblock->fs_cssize));
	if (superblock)
		FreeMem(superblock, FBSIZE);

	superblock	= NULL;
	cgsummary_base	= NULL;
}


/* open_dchange()
 *	This routine opens up the interrupt mechanism for
 *	disk change on removable media devices.
 */
open_dchange()
{
#ifdef REMOVABLE
	if (!(dchangePort = CreateMsgPort())) {
		PRINT(("unable to createport for dchangeIO\n"));
		return(1);
	}

	if (!(dchangeIO = (struct IOExtTD *)
	      CreateIORequest(dchangePort, sizeof(struct IOExtTD)))) {
		PRINT(("unable to CreateIORequest for dchangeIO\n"));
		return(1);
	}

	/* hack for floppy devices which do not support the
	   TD_REMCHANGEINT packet correctly */

	if (!strcmp(DISK_DEVICE, MESSYDISK)) {
	    if (OpenDevice(TRACKDISK, DISK_UNIT, (struct IORequest *) dchangeIO,
		DISK_FLAGS)) {
		PRINT(("unable to open dchange device %s\n", DISK_DEVICE));
		return(1);
	    }
	} else if (OpenDevice(DISK_DEVICE, DISK_UNIT,
			      (struct IORequest *) dchangeIO, DISK_FLAGS)) {
	    PRINT(("unable to open dchange device %s\n", DISK_DEVICE));
	    return(1);
	}

	if ((dcsigBit = AllocSignal(-1L)) == 0) {
		PRINT(("unable to allocate signal bit for disk change\n"));
		return(1);
	}

	/* set up data segment for routine (routine to signal, bit to set) */
	IntData.sigTask = FindTask(0L);
	IntData.sigMask = 1L << dcsigBit;

	PRINT(("Inttask=%08x Intmask=%08x\n", IntData.sigTask, IntData.sigMask));

	/* Init the Node structure to something nice */
	IntHand.is_Node.ln_Name = "BFFSdiskchange_int";
	IntHand.is_Node.ln_Type = NT_INTERRUPT;
	IntHand.is_Node.ln_Pri  = 0;
	IntHand.is_Code	    	= &IntHandler;	/* code pointer */
	IntHand.is_Data	    	= &IntData;	/* data pointer */

	dchangeIO->iotd_Req.io_Command	= TD_ADDCHANGEINT;
	dchangeIO->iotd_Req.io_Flags	= 0;
	dchangeIO->iotd_Req.io_Length	= sizeof(struct Interrupt);
	dchangeIO->iotd_Req.io_Data	= &IntHand;

	SendIO((struct IORequest *) dchangeIO);

	PRINT(("db request submitted\n"));
	dcsubmitted = 1;
	return(0);
#else
	IntData.sigMask = 0;
#endif
}


/* close_dchange()
 *	This routine closes the interrupt mechanism for
 *	disk change on removable media devices.
 */
close_dchange(normal)
int normal;
{
#ifdef REMOVABLE
	/* send packet to check if disk is removable first */

	if (normal && dchangeIO) {
		if (dcsubmitted) {
			dchangeIO->iotd_Req.io_Command = TD_REMCHANGEINT;
			DoIO((struct IORequest *) dchangeIO);
		}

		if (dcsigBit) {
			FreeSignal(dcsigBit);
			dcsigBit = 0;
		}

		CloseDevice((struct IORequest *) dchangeIO);
	}

	if (dchangeIO) {
		DeleteIORequest(dchangeIO);
		dchangeIO = NULL;
	}

	if (dchangePort) {
		DeleteMsgPort(dchangePort);
		dchangePort = NULL;
	}
#endif
}


ffs_oldfscompat()
{
	if (DISK32(superblock->fs_nsect) > DISK32(superblock->fs_npsect))
		superblock->fs_npsect = superblock->fs_nsect;

        if (DISK32(superblock->fs_interleave) < 1)
		superblock->fs_interleave = DISK32(1);

        if (DISK32(superblock->fs_postblformat) == FS_42POSTBLFMT)
                superblock->fs_nrpos = DISK32(8);

	switch (DISK32(superblock->fs_inodefmt)) {
	    case 0:
		PRINT(("BSD 4.2 FFS\n"));
		goto bsd43;
	    case FS_42POSTBLFMT:
		PRINT(("BSD 4.2 PostBL FFS\n"));
		goto bsd43;
	    case FS_DYNAMICPOSTBLFMT:
		PRINT(("BSD 4.3 FFS\n"));
		bsd43:
		superblock->fs_maxfilesize.val[0] = 0x0;
		superblock->fs_maxfilesize.val[1] = 0x0;
		DISK64SET(superblock->fs_maxfilesize, 0xffffffff);

                superblock->fs_qbmask.val[0] = 0xffffffff;
                superblock->fs_qbmask.val[1] = 0xffffffff;
		fs_lbmask = ~DISK32(superblock->fs_bmask);
                DISK64SET(superblock->fs_qbmask, fs_lbmask);

                superblock->fs_qfmask.val[0] = 0xffffffff;
                superblock->fs_qfmask.val[1] = 0xffffffff;
		fs_lfmask = ~DISK32(superblock->fs_fmask);
                DISK64SET(superblock->fs_qfmask, fs_lfmask);
		bsd44fs = 0;
		break;
	    case FS_44INODEFMT:
		PRINT(("BSD 4.4 FFS\n"));
		PRINT(("inodefmt=%d\n", DISK32(superblock->fs_inodefmt)));
		fs_lfmask = DISK64(superblock->fs_qfmask);
		fs_lbmask = DISK64(superblock->fs_qbmask);
		bsd44fs = 1;
		break;
	    default:
		PRINT(("** Unknown inode format!\n"));
		superblock->fs_ronly = 1;
		break;
	}


	PRINT(("lfmask=%08x lbmask=%08x\n", fs_lfmask, fs_lbmask));
}
