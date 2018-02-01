/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <time.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/trackdisk.h>
#include <devices/hardblocks.h>
#include <dos/filehandler.h>

#ifdef GCC
#include <proto/exec.h>
#include <inline/dos.h>
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
#include "unixtime.h"

#ifdef TD_EXTENDED
#define TD_CMD_READ  ETD_READ
#define TD_CMD_WRITE ETD_WRITE
#else
#define TD_CMD_READ  CMD_READ
#define TD_CMD_WRITE CMD_WRITE
#endif

#define TD_READ64     24
#define TD_WRITE64    25
#define TD_SEEK64     26
#define TD_FORMAT64   27

#define S static

#ifdef GCC
struct MsgPort *CreatePort(UBYTE *name, long pri);
#endif

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

#ifdef BOTHENDIAN
int    is_big_endian = 0;
#endif

struct	fs *superblock = NULL;	/* current superblock			  */
int	bsd44fs = 0;		/* 0 = 4.3 BSD fs, 1 = 4.4 BSD fs	  */
int	openerror = 1;		/* 1 = OpenDevce failed			  */
int	physical_ro = 0;	/* disk is physically write-protected	  */
int	fs_partition;		/* current partition			  */
ULONG	fs_lfmask;		/* global for faster access, ~fmask	  */
ULONG	fs_lbmask;		/* global for faster access, ~bmask	  */
ULONG	phys_sectorsize = 512;	/* physical disk sector size, from env	  */
ULONG	phys_sectorshift = 9;	/* shift value for physical sector size   */
ULONG	psectoffset;		/* disk partition start sector            */
ULONG	psectmax;		/* disk partition end sector              */
ULONG	tranmask = 0;		/* device DMA transfer mask		  */
long	fsize;			/* superblock filesystem fragment size	  */
long	bsize;			/* superblock filesystem block size	  */

extern	char *handler_name;	/* device name handler was started as     */

/* interrupt handler declarations for disk change routines */
extern VOID   IntHandler(VOID);
intdata_t	     IntData;   /* data segment info for interrupt handler */
struct Interrupt     IntHand;	/* exec interrupt struct for int handler */

int    dcsigBit = 0;		/* signal bit in use for disk changes */
int    dcsubmitted = 0;		/* whether a disk change interrupt IO
				   request has been submitted */
int    use_td64 = 0;


/* data_read()
 *	This routine is the only interface to read the raw disk
 *	data which is used by the filesystem.
 *
 *	   buf = memory buffer address (where to put data)
 *	   num = starting disk fragment (fragment size specified in FSIZE)
 *	  size = number of bytes to read from the disk
 *
 *      Zero is returned on success.  Non-zero is returned on failure.
 */
int
data_read(void *buf, ULONG num, ULONG size)
{
	int index;
	ULONG sectoff = FSIZE / phys_sectorsize * num + psectoffset;

	motor_is_on = 1;

	trackIO->iotd_Req.io_Length  = size;       /* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;        /* buffer to read into */
	trackIO->iotd_Req.io_Offset  = sectoff << phys_sectorshift;
	if (use_td64) {
	    trackIO->iotd_Req.io_Command = TD_READ64; /* set IO Command */
	    trackIO->iotd_Req.io_Actual  = sectoff >> (32 - phys_sectorshift);
	    /* io_Actual has upper 32 bits of byte offset */
	} else {
	    trackIO->iotd_Req.io_Command = TD_CMD_READ; /* set IO Command */
	}

	PRINT(("data_read from faddr=%u sector=%u size=%d\n",
	       num, sectoff, size));

	UPSTAT(direct_reads);
	UPSTATVALUE(direct_read_bytes, size);

#ifndef FAST
	if ((sectoff >= psectmax) || (num < 0)) {
		PRINT2(("read sector %u [frag %d, bytes %d] is out of range for partition\n",
			sectoff, num, size));
		return(1);
	}
#endif

	if (DoIO((struct IORequest *) trackIO)) {
	    PRINT2(("drf faddr=%d sector=%d size=%d\n", num, sectoff, size));
	    if (use_td64 == -1) {
		/* Turn off TD64 and try again */
		use_td64 = 0;
		return (data_read(buf, num, size));
	    }
	    while (do_request(REQUEST_BLOCK_BAD_R, sectoff, size, ""))
		for (index = 0; index < 4; index++)
		    if (!DoIO((struct IORequest *) trackIO))
			return(0);
		    else
			PRINT2(("drf faddr=%d sector=%d size=%d\n", num,
				sectoff, size));
	    return(1);
	}
	if (use_td64 == -1) {
	    PRINT(("Using TD64\n"));
	    use_td64 = 1;
	}

	return(0); /* Success */
}

/* data_write()
 *	This routine is the only interface to write the raw disk
 *	data which is used by the filesystem.
 *
 *	   buf = memory buffer address (where to get data)
 *	   num = starting disk fragment (fragment size specified in FSIZE)
 *	  size = number of bytes to write to the disk
 *
 *      Zero is returned on success.  Non-zero is returned on failure.
 */
#ifndef RONLY
int
data_write(void *buf, ULONG num, ULONG size)
{
	int   index;
	ULONG sectoff = FSIZE / phys_sectorsize * num + psectoffset;

	if (superblock->fs_ronly)
	    return (1);

	motor_is_on = 1;

	trackIO->iotd_Req.io_Length  = size;       /* #bytes to write */
	trackIO->iotd_Req.io_Data    = buf;        /* source data */
	trackIO->iotd_Req.io_Offset  = sectoff << phys_sectorshift;

	if (use_td64) {
	    trackIO->iotd_Req.io_Command = TD_WRITE64;
	    trackIO->iotd_Req.io_Actual  = sectoff >> (32 - phys_sectorshift);
	    /* io_Actual has upper 32 bits of byte offset */
	} else {
	    trackIO->iotd_Req.io_Command = TD_CMD_WRITE;
	}

	PRINT(("data_write to faddr=%u sector=%u size=%d\n",
	       num, sectoff, size));

	UPSTAT(direct_writes);
	UPSTATVALUE(direct_write_bytes, size);

#ifndef FAST
	if ((sectoff >= psectmax) || (num < 0)) {
		PRINT2(("write sector %u [frag %d, bytes %d] is out of range for partition\n",
			sectoff, num, size));
		return(1);
	}
#endif

	if (DoIO((struct IORequest *) trackIO)) {
	    PRINT2(("dwf faddr=%d sector=%d size=%d\n", num, sectoff, size));
	    while (do_request(REQUEST_BLOCK_BAD_W, sectoff, size, ""))
		for (index = 0; index < 4; index++)
		    if (!DoIO((struct IORequest *) trackIO))
			return(0);
		    else
			PRINT2(("dwf faddr=%d sector=%d size=%d\n", num,
				sectoff, size));
	    return(1);
	}

	return(0); /* Success */
}
#endif

/* close_device()
 *	This routine performs the complement operation of open_device()
 *	It shuts down the device and deallocates the associated data
 *	structures.
 */
void
close_device(void)
{
	if (openerror)
		PRINT2(("Failed to open device.\n"));
	else if (trackIO)
		CloseDevice((struct IORequest *) trackIO);

	if (devport) {
		DeletePort(devport);
		devport = 0;
	}

	if (trackIO) {
		DeleteExtIO((struct IORequest *) trackIO);
		trackIO = NULL;
	}
}

/* open_device()
 *	This routine will open the driver for the media device
 *	and initialize all structures used to communicate with
 *	that device.
 */
int
open_device(void)
{
	ULONG flags;
	ULONG temp;
	extern int resolve_symlinks;
	extern int link_comments;
	extern int inode_comments;
	extern int og_perm_invert;
	extern int minfree;
	unsigned char	temp_gmt;
	char  *ptr;

	sprintf(stat->handler_version, "%.4s", version + 22);
	ptr = stat->handler_version + 4;
#	ifdef BOTHENDIAN
	    *(ptr++) = 'B';
#	endif

#	ifdef RONLY
	    *(ptr++) = 'R';
#	endif

#	ifdef SHOWDOTDOT
	    *(ptr++) = '.';
#	endif

#	ifdef FAST
	    *(ptr++) = 'F';
#	endif

#	ifdef NOPERMCHECK
	    *(ptr++) = 'P';
#	endif

#	ifdef REMOVABLE
	    *(ptr++) = 'r';
#	endif

#	ifdef TD_EXTENDED
	    *(ptr++) = 'E';
#	endif

#	ifdef DEBUG
	    *(ptr++) = 'D';
#	endif

#	ifdef MUFS
	    *(ptr++) = 'M';
#	endif
	*ptr = '\0';

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

	close_device();
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
		PRINT2(("no disk present, closing ufs\n"));
		close_device();
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


#ifdef TD_EXTENDED
	/* get the current change number of the disk */
	trackIO->iotd_Req.io_Command = TD_CHANGENUM;	/* set IO Command */

	if (DoIO(trackIO))
	    PRINT2(("was unable to get change count\n"));
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
		     default:
			PRINT2(("Invalid RDB sector size of %d\n", temp));
			temp = 512;
			/* FALLTHROUGH */
		     case 16384:
		     case 8192:
		     case 4096:
		     case 2048:
		     case 1024:
		     case 512:
			phys_sectorsize = temp;
			phys_sectorshift = 0;
			while ((temp >>= 1) != 0)
			    phys_sectorshift++;
			break;
		}
		psectmax = ENVIRONMENT->de_BlocksPerTrack *
		       (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces;

		resolve_symlinks = (flags) & 1;
		case_dependent   = (flags >> 1) & 1;
		unix_paths	 = (flags >> 2) & 1;
		link_comments	 = (flags >> 3) & 1;
		inode_comments	 = (flags >> 4) & 1;
		og_perm_invert	 = (flags >> 5) & 1;
		minfree		 = (flags >> 6) & 1;
		temp_gmt	 = (flags >> 8) & 255;
		GMT		 = *((char *) &temp_gmt);

		PRINT(("tm=%x rs=%d cdep=%d up=%d lc=%d ",
			tranmask, ENVIRONMENT->de_Reserved, case_dependent,
			unix_paths, link_comments));
		PRINT(("ic=%d pi=%d mf=%d gmt=%d\n",
			inode_comments, og_perm_invert, minfree, GMT));

	}

	/* must be reset for every disk change */
	superblock_sector = SUPER_BLOCK * 512 / phys_sectorsize;
	use_td64 = -1;  /* auto-detect TD64 */

	return(0);
}

/* sun_label_read()
 *	This routine is used to sense a Sun disk label and set the
 *	partition offset (psectoffset) to the appropriate value to get
 *	the specified partition.
 */
static int
sun_label_read(int new_partition)
{
	struct	sun_label *label;	/* disk partition table */
	ULONG	part_off;
	ULONG	part_end;

	if (no_Sun_label)
		return(1);

	if (new_partition & 8) {
		no_Sun_label = 1;
		return(1);
	}

	new_partition &= 7;

	part_off = psectoffset;

	label = (struct sun_label *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (label == NULL) {
		PRINT2(("sun_label_read: unable to allocate %d bytes\n",
			phys_sectorsize));
		return(1);
	}
	if (data_read(label, BOOT_BLOCK, phys_sectorsize)) {
		PRINT2(("** data read fault for Sun label\n"));
		no_Sun_label = 1;
		FreeMem(label, phys_sectorsize);
		return(1);
	}

	psectoffset = part_off +
		  DISK32((label->bb_part[new_partition]).fs_start_cyl) *
		  DISK32(label->bb_heads) * DISK32(label->bb_nspt);

	part_end = (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces *
		    ENVIRONMENT->de_BlocksPerTrack;

	if (DISK32(label->bb_magic) != SunBB_MAGIC) {
fail_sun_label:
		no_Sun_label = 1;
		psectoffset = part_off;
		FreeMem(label, phys_sectorsize);
		return(1);
	}

	if ((label->bb_part[new_partition].fs_size == 0) ||
	    (DISK32(label->bb_part[new_partition].fs_size) >
	     part_end - part_off)) {
		PRINT(("bad values for Sun disk label partition=%d\n",
			new_partition));
		PRINT(("handler_begin=%u handler_end=%u size=%u\n", part_off,
			part_end, part_end - part_off));
		PRINT(("part_begin=%d part_end=%d size=%d\n", psectoffset,
			psectoffset +
			DISK32(label->bb_part[new_partition].fs_size),
			DISK32(label->bb_part[new_partition].fs_size)));
		goto fail_sun_label;
	}
	PRINT(("Found SunOS label.\n"));

	fs_partition = new_partition;
	strcpy(stat->disk_type, "Sun ");

	FreeMem(label, phys_sectorsize);
	return(0);
}

/* bsd44_label_read()
 *	This routine is used to sense a BSD44 disk label and set the
 *	partition offset (psectoffset) to the appropriate value to get
 *	the specified partition.
 */
static int
bsd44_label_read(int new_partition)
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
	part_off = psectoffset;

	buffer = (ULONG *) AllocMem(phys_sectorsize, MEMF_PUBLIC);

	if (buffer == NULL) {
		PRINT2(("bsd44_label_read: unable to allocate %d bytes\n",
			phys_sectorsize));
		return(1);
	}

	if (rc = data_read(buffer, BOOT_BLOCK, phys_sectorsize)) {
		PRINT2(("** data read fault for bsd label\n"));
		no_BSD_label = 1;
		goto retvalue;
	}

	for (fs_offset = 0; fs_offset < phys_sectorsize - sizeof(*label); fs_offset += 16) {
		label = (struct bsd44_label *) (buffer + fs_offset);
#ifdef BOTHENDIAN
		/* Auto-detect endian */
		if ((DISK32(label->bb_magic)  == B44BB_MAGSWAP) &&
		    (DISK32(label->bb_magic2) == B44BB_MAGSWAP))
		    is_big_endian = !is_big_endian;
#endif
		if ((DISK32(label->bb_magic)  == B44BB_MAGIC) &&
		    (DISK32(label->bb_magic2) == B44BB_MAGIC))
			goto good_value;
	}

	no_BSD_label = 1;
	goto retvalue;

	good_value:
	psectoffset += DISK32(label->bb_part[new_partition].fs_start_sec);

	part_end = (ENVIRONMENT->de_HighCyl + 1) * ENVIRONMENT->de_Surfaces *
		    ENVIRONMENT->de_BlocksPerTrack;

	if ((label->bb_part[new_partition].fs_size == 0) ||
	    (DISK32(label->bb_part[new_partition].fs_size) *
	     DISK32(label->bb_secsize) > part_end - part_off)) {
		PRINT(("bad values for BSD44 disk label partition=%d\n",
			new_partition));
		PRINT(("handler_begin=%u handler_end=%u size=%u\n", part_off,
			part_end, part_end - part_off));
		PRINT(("part_begin=%d part_end=%d size=%d\n", psectoffset,
			psectoffset +
			DISK32(label->bb_part[new_partition].fs_size) *
			DISK32(label->bb_secsize) / phys_sectorsize,
			DISK32(label->bb_part[new_partition].fs_size) *
			DISK32(label->bb_secsize)));
		psectoffset = part_off;
		no_BSD_label = 1;
		goto retvalue;
	}

	PRINT(("Found BSD44 label, using partition=%d\n", new_partition));
/*
	PRINT(("handler_begin=%u handler_end=%u size=%u\n", part_off,
		part_end, part_end - part_off));
	PRINT(("part_begin=%d part_end=%d size=%d\n", psectoffset,
		psectoffset + label->bb_part[new_partition].fs_size *
		DISK32(label->bb_secsize) / phys_sectorsize,
		DISK32(label->bb_part[new_partition].fs_size) *
		DISK32(label->bb_secsize)));
*/

	fs_partition = new_partition;
	strcpy(stat->disk_type, "BSD ");
	rc = 0;

	retvalue:
	FreeMem(buffer, phys_sectorsize);
	return(rc);
}

/* partition_info_read()
 *	This routine will call the various disk label sensing routines
 *	to get the start address of the specified partition.  If no
 *	disk labels exist, this routine returns an error code.
 */
static int
partition_info_read(int partition)
{
	int ret = 0;

	psectoffset = ENVIRONMENT->de_LowCyl * ENVIRONMENT->de_Surfaces *
		      ENVIRONMENT->de_BlocksPerTrack;

	FSIZE = phys_sectorsize;

	if (sun_label_read(partition) && bsd44_label_read(partition))
		ret = 1;		/* could not read either */

	return(ret);
}

/* cgsummary_read()
 *	This routine is used by superblock_read() to get the
 *	associated cylinder group summary information off the
 *	disk and set up pointers in the superblock to that data.
 */
static int
cgsummary_read(void)
{
	char *space;
	int blks;
	ULONG cssize = DISK32(superblock->fs_cssize);
	ULONG csaddr;

	space = cgsummary_base = (char *) AllocMem(cssize, MEMF_PUBLIC);
	if (cgsummary_base == NULL) {
		PRINT2(("Unable to allocate %u bytes for cg summary info\n",
			cssize));
		return(1);
	}

	if (superblock->fs_flags & FS_FLAGS_UPDATED)
	    csaddr = DISK32(superblock->fs_new_csaddr[is_big_endian]);
	else
	    csaddr = DISK32(superblock->fs_csaddr);

	if (data_read(cgsummary_base, csaddr, cssize)) {
		PRINT2(("data read fault for superblock cg summary info\n"));
		superblock->fs_ronly = 1;
		return(0);
	}

	blks = howmany(cssize, FBSIZE);

	superblock->fs_csp = (struct csum *) space;
	return(0);
}

static void
ffs_oldfscompat(void)
{
	if (DISK32(superblock->fs_nsect) > DISK32(superblock->fs_npsect))
		superblock->fs_npsect = superblock->fs_nsect;

        if (DISK32(superblock->fs_interleave) < 1)
		superblock->fs_interleave = DISK32(1);

        if (DISK32(superblock->fs_postblformat) == FS_42POSTBLFMT)
                superblock->fs_nrpos = DISK32(8);

#ifdef BOTHENDIAN
	if (is_big_endian == 0)
	    PRINT(("x86 "));
#endif
	switch (DISK32(superblock->fs_inodefmt)) {
	    case 0:
		PRINT(("BSD 4.2 inode format\n"));
		goto bsd43;
	    case FS_42POSTBLFMT:
		PRINT(("BSD 4.2 PostBL inode format\n"));
		goto bsd43;
	    case FS_DYNAMICPOSTBLFMT:
		PRINT(("BSD 4.3 inode format\n"));
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
		PRINT(("BSD 4.4 inode\n"));
		fs_lfmask = DISK64(superblock->fs_qfmask);
		fs_lbmask = DISK64(superblock->fs_qbmask);
		bsd44fs = 1;
		break;
	    default:
		PRINT2(("** Unknown inode format: %u\n",
		        DISK32(superblock->fs_inodefmt)));
		superblock->fs_ronly = 1;
		break;
	}

	PRINT(("lfmask=%08x lbmask=%08x\n", fs_lfmask, fs_lbmask));
}

#ifdef BOTHENDIAN
/* Auto-detect endian by checking fragsize * frags_per_block = blocksize */
static void
superblock_detect_endian(struct fs *sb)
{
    int try;

    if (sb->fs_bsize == 0)
	return;

    for (try = 0; try < 2; try++) {
	ULONG bsize = DISK32(sb->fs_bsize);
	ULONG fsize = DISK32(sb->fs_fsize);
	if (((1 << DISK32(sb->fs_bshift)) == bsize) &&
	    ((1 << DISK32(sb->fs_fshift)) == fsize) &&
	    (fsize * DISK32(sb->fs_frag) == bsize)) {
	    return;
	}
	/* Try "other" endian */
	is_big_endian = !is_big_endian;
    }
}
#endif

/* superblock_read()
 *	This routine is used to read the superblock from the disk
 *	at the specified partition.  It calls partition_info_read()
 *	to set the disk psectoffset to the proper location.
 *	Consistency checking is done on the superblock to ensure
 *	that it is valid before returning.
 */
int
superblock_read(int partition)
{
	int	index	   = 0;
	int	csize	   = 1;
	ULONG	ssector	   = 0;
	struct fs *temp_sb;

	if (partition_info_read(partition))
		fs_partition = 0;

	superblock = NULL;
	temp_sb = (struct fs *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (temp_sb == NULL) {
		PRINT2(("superblock_read: unable to allocate %d bytes\n",
			phys_sectorsize));
		return(1);
	}

	for (ssector = 0; ssector < 128; ssector++) {
	    switch (ssector) {	/* this implements an incremental guessing
				   algorithm which hit me during a nightmare */
		case 5:
			ssector = 16;
			break;
		case 18:
			ssector = 48;
			break;
		case 49:
			ssector = 64;
			break;
		case 65:
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
#ifdef BOTHENDIAN
	    superblock_detect_endian(temp_sb);
#endif

	    FBSIZE = DISK32(temp_sb->fs_bsize);
	    if ((FBSIZE == 4096)  || (FBSIZE == 8192) ||
		(FBSIZE == 16384) || (FBSIZE == 32768)) {
		superblock = (struct fs *) AllocMem(FBSIZE, MEMF_PUBLIC);
		if (superblock == NULL)
		    PRINT2(("superblock_read: could not allocate %u bytes\n",
			   FBSIZE));
		break;
#ifdef DEBUG
	    } else {
		PRINT2(("Superblock not found at sector %u\n", ssector +
			superblock_sector));
	        if ((FBSIZE != 0)     && (FBSIZE != 0xffffffff) &&
	            (FBSIZE != 4096)  && (FBSIZE != 8192) &&
		    (FBSIZE != 16384) && (FBSIZE != 32768))
			PRINT2(("bsize %d ", FBSIZE));
		PRINT2(("\n"));
#endif
	    }
	    psectoffset++;
	}

	if (superblock == NULL) {
		FreeMem(temp_sb, phys_sectorsize);
		return(1);
	}

	data_read(superblock, superblock_sector, FBSIZE);
	FreeMem(temp_sb, phys_sectorsize);
	FSIZE = DISK32(superblock->fs_fsize);

	superblock->fs_ronly = 0;     /* Start it out Read/Write */

	PRINT(("part=%d fs_bsize=%d fs_size=%u frags x %d bytes/frag = %uk\n",
		fs_partition, FBSIZE, DISK32(superblock->fs_size), FSIZE,
		FSIZE / 512 * DISK32(superblock->fs_size) / 2));
	PRINT(("csaddr=%d cssize=%d ncg=%d fpg=%d cgsize=%d ileave=%d\n",
		DISK32(superblock->fs_csaddr), DISK32(superblock->fs_cssize),
		DISK32(superblock->fs_ncg), DISK32(superblock->fs_fpg),
		DISK32(superblock->fs_cgsize),
		DISK32(superblock->fs_interleave)));

	switch (DISK32(superblock->fs_magic)) {
	    case FS_UFS1_MAGIC:
		break;
	    case FS_UFS2_MAGIC:
		PRINT2(("UFS V2 filesystem (partition %u) is not supported\n",
		        fs_partition));
		goto bad_superblock;
	    default:
		PRINT2(("Invalid magic in superblock for partition %d: %08x\n",
			fs_partition, DISK32(superblock->fs_magic)));
		goto bad_superblock;
	}
	if (superblock->fs_flags & (FS_FLAGS_SUJ | FS_FLAGS_MULTILEVEL |
				    FS_FLAGS_GJOURNAL)) {
	    PRINT2(("Unsupported FFS v2 filesystem (flags %02x)\n",
		    (unsigned char) superblock->fs_flags));
	    superblock->fs_ronly = 1;
	}

	if (cgsummary_read()) {
		PRINT2(("error reading cg summary info\n"));
		goto bad_superblock;
	}

	fblkshift = DISK32(superblock->fs_fragshift);
	fblkmask  = (1 << fblkshift) - 1;

	csize = DISK32(superblock->fs_nindir) >> fblkshift;
	pfragshift = 0;
	for (index = 1; index < csize; index <<= 1)
		pfragshift++;
	pfragmask = (1 << pfragshift) - 1;

	PRINT(("fblkshift=%u fblkmask=%x pfragshift=%u pfragmask=%x\n",
		fblkshift, fblkmask, pfragshift, pfragmask));

#ifdef RONLY
	superblock->fs_ronly = 1;     /* Sorry, read only  */
#else
	if (physical_ro)
		superblock->fs_ronly = 1;
#endif
	superblock->fs_fmod  = 0;      /* superblock synced with disk flag */
	superblock->fs_clean = 0;      /* superblock clean unmount flag */

	sprintf(stat->disk_type + strlen(stat->disk_type),
		"%s:%cd%d%c", handler_name, tolower(*(DISK_DEVICE)), DISK_UNIT,
		fs_partition + 'a');

	/* determine filesystem type and patch up if older */
	ffs_oldfscompat();

	return(0);

	bad_superblock:
	FreeMem(superblock, FBSIZE);
	superblock = NULL;
	return(1);
}


#if 0		/* doesn't work yet, don't include it */
/* rdb_label_read()
 *	This routine will read an Amiga RDB disk label and return
 *	the specified partition.
 *
 *	THIS ROUTINE IS NOT FULLY IMPLEMENTED.
 */
rdb_label_read(new_partition)
int new_partition;
{
	ULONG	rdb_pos;
	ULONG	old_psectoffset;
	ULONG	partblk;
	struct	RigidDiskBlock *rdb;
	struct	PartitionBlock *pblock;

	PRINT(("rdb_label_read\n"));
	old_psectoffset = psectoffset;
	psectoffset = 0;

	rdb = (struct RigidDiskBlock *) AllocMem(phys_sectorsize, MEMF_PUBLIC);
	if (rdb == NULL) {
		PRINT2(("rdb_label_read: unable to allocate %d bytes\n",
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

		psectoffset = ENVIRONMENT->de_LowCyl *
			      ENVIRONMENT->de_Surfaces *
			      ENVIRONMENT->de_BlocksPerTrack;
		FreeMem(rdb, phys_sectorsize);
		return(0);
	    }
	}

	FreeMem(rdb, phys_sectorsize);
	psectoffset = old_psectoffset;
	return(1);
}
#endif

/* find_superblock()
 *	This routine will first attempt to locate the superblock
 *	in the environment specified partition.  If it is not found
 *	there and the partition has a disk label, the other
 *	partitions will be searched for a superblock.
 */
int
find_superblock(void)
{
	int part;

	no_Sun_label = 0;
	no_BSD_label = 0;

	if (superblock_read(ENVIRONMENT->de_Reserved)) {
		if (no_Sun_label && no_BSD_label) {
			PRINT2(("Superblock not found at beginning of partition\n"));
			return(2);
		}
		if (!no_Sun_label || !no_BSD_label) {
		    PRINT(("s=%d b=%d\n", no_Sun_label, no_BSD_label));
		    PRINT(("scanning other partitions\n"));
		    for (part = 0; superblock_read(part); part++) {
			if (part == MAX_PART) {
				PRINT2(("Giving up.\n"));
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
void
motor_off(void)
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
void
motor_on(void)
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
int
superblock_flush(void)
{
	ULONG csaddr;

	if ((superblock->fs_fmod == 0) || (superblock->fs_ronly))
	    return (0);

	PRINT(("superblock flush\n"));

	superblock->fs_time = DISK32(unix_time());
	superblock->fs_fmod = 0;

	if (superblock->fs_flags & FS_FLAGS_UPDATED) {
	    csaddr = DISK32(superblock->fs_new_csaddr[is_big_endian]);
	    superblock->fs_new_time[is_big_endian] = superblock->fs_time;
	} else {
	    csaddr = DISK32(superblock->fs_csaddr);
	}

	if (data_write(superblock, lfragno(superblock,
		       superblock_sector * phys_sectorsize), FBSIZE))
	    PRINT2(("** data write fault for superblock\n"));

	if (data_write(cgsummary_base, csaddr, DISK32(superblock->fs_cssize)))
	    PRINT2(("** data write fault for superblock cg summary info\n"));
	return (1);
}
#endif


/* superblock_destroy()
 *	This routine will deallocate the space consumed by the
 *	active superblock and its associated cylinder group
 *	summary information.
 */
void
superblock_destroy(void)
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
int
open_dchange(void)
{
#ifdef REMOVABLE
	if (!(dchangePort = CreateMsgPort())) {
		PRINT2(("unable to createport for dchangeIO\n"));
		return(1);
	}

	if (!(dchangeIO = (struct IOExtTD *)
	      CreateIORequest(dchangePort, sizeof(struct IOExtTD)))) {
		PRINT2(("unable to CreateIORequest for dchangeIO\n"));
		return(1);
	}

	/* hack for floppy devices which do not support the
	   TD_REMCHANGEINT packet correctly */

	if (!strcmp(DISK_DEVICE, MESSYDISK)) {
	    if (OpenDevice(TRACKDISK, DISK_UNIT, (struct IORequest *) dchangeIO,
		DISK_FLAGS)) {
cant_open:
		PRINT2(("unable to open dchange device %s\n", DISK_DEVICE));
		return(1);
	    }
	} else if (OpenDevice(DISK_DEVICE, DISK_UNIT,
			      (struct IORequest *) dchangeIO, DISK_FLAGS)) {
	    goto cant_open;
	}

	if ((dcsigBit = AllocSignal(-1L)) == 0) {
		PRINT2(("unable to allocate signal bit for disk change\n"));
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
	IntHand.is_Code	        = IntHandler;	/* code pointer */
	IntHand.is_Data	        = &IntData;	/* data pointer */

	dchangeIO->iotd_Req.io_Command	= TD_ADDCHANGEINT;
	dchangeIO->iotd_Req.io_Flags	= 0;
	dchangeIO->iotd_Req.io_Length	= sizeof(struct Interrupt);
	dchangeIO->iotd_Req.io_Data	= &IntHand;

	SendIO((struct IORequest *) dchangeIO);

	PRINT(("db request submitted\n"));
	dcsubmitted = 1;
#else
	IntData.sigMask = 0;
#endif
	return(0);
}


/* close_dchange()
 *	This routine closes the interrupt mechanism for
 *	disk change on removable media devices.
 */
void
close_dchange(int normal)
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

#ifdef BOTHENDIAN

unsigned short
disk16(unsigned short x)
{
    if (is_big_endian)
	return (x);

    /*
     * This could be faster in assembly as:
     *     rol.w #8, d0
     */
    return(((x >> 8) & 255) | ((x & 255) << 8));
}

unsigned long
disk32(unsigned long x)
{
    if (is_big_endian)
	return (x);

    /*
     * This could be faster in assembly as:
     *     rol.w #8, d0
     *     swap d1
     *     rol.w $8, d0
     */
    return((x >> 24) | (x << 24) |
	   ((x << 8) & (255 << 16)) |
	   ((x >> 8) & (255 << 8)));
}
#endif
