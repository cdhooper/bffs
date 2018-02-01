/*
 * File/Device copier by Chris Hooper (amiga@cdh.eebugs.com) on
 *      5-Jul-1993  v1.0
 *     14-Oct-1993  v1.1
 *     08-Nov-1993  v1.2
 *     27-Jan-1996  v1.21
 *     19-Jan-2018  v1.3
 *
 * This program can copy files to Amiga devices and devices to files, etc.
 *
 * Usage:
 *    dcp {source} {dest} [-m number of blocks] [-b buffer size] [-v verbose]
 *			  [-ss source start block] [-ds dest start block]
 *			  [-d dryrun] [-h help]
 *
 * This product is distributed as freeware with no warranties expressed or
 * implied.  There are no restrictions on distribution or application of
 * this program other than it may not be used in contribution with or
 * converted to an application under the terms of the GNU Public License
 * nor sold for profit greater than the cost of distribution.  Also, this
 * notice must accompany all redistributions and/or modifications of this
 * program.  Remember:  If you expect bugs, you should not be disappointed.
 *
 *
 * Thanks to Christian Brandel for code correction  isdigit() -> !isalpha()
 */

const char *version = "\0$VER: dcp 1.3 (19-Jan-2018) � Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <exec/types.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <exec/io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <exec/memory.h>

/*	#define DEBUG(x) fprintf(stderr, x)	*/

/* Possible file types
 *	tNONE	0 = Could not be opened
 *	tDEVICE 1 = Block device opened
 *	tFILE	2 = Regular AmigaDOS file opened
 *	tSTDIO	3 = STDIO channel specified
 *	tNIL	4 = No output device
 */
#define tNONE   0
#define tDEVICE 1
#define tFILE   2
#define tSTDIO  3
#define tNIL    4

#define dREAD   0
#define dWRITE  1

#ifndef TD_SECTOR
#define TD_SECTOR   512
#endif
#ifndef TD_SECSHIFT
#define TD_SECSHIFT 9
#endif

#define TD_READ64   24
#define TD_WRITE64  25
#define TD_SEEK64   26
#define TD_FORMAT64 27

/* BCPL conversion functions */
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

/* quick parameter parser */
#define ACCEPT(x,y)							\
	if (stricmp(argv[index], y) == 0) {				\
	    index++;							\
	    if (index < argc) {						\
		if (sscanf(argv[index], "%d", &x) != 1) {		\
		    fprintf(stderr, "number required for %s parameter\n", y); \
		    exit(1);						\
	        }							\
	    } else {							\
	        fprintf(stderr, "number required for %s parameter\n", y); \
	        exit(1);						\
	    }								\
	}


struct file_info {
    char    *name;            /* filename as specified on cmdline */
    int     dtype;            /* NONE, DEVICE, FILE, or STDIO */
    FILE    *strIO;           /* stream file pointer */
    struct  IOExtTD *trackIO; /* device packet pointer */
    ULONG   currentblk;       /* current read/write block in device/file */
    ULONG   maxblk;           /* maximum allowed read/write blk in dev/file */
    int     use_td64;         /* 0=device doesn't support TD64 extensions */
};


struct file_info in;		/* internal input file descriptor */
struct file_info out;		/* internal output file descriptor */

char	*progname;		/* name of this program running */
char	iobuf[128];		/* buffer for line-oriented I/O */
char	*revdate = "01-Jan-2018"; /* program revision date */
int	last_read_blocks;	/* number of blocks last read */
char	*buffer	= NULL;		/* buffer area for copy operations */
int	buffer_blocks = 2000;	/* size of buffer area */
int	verbose       = 0;	/* verbose mode  - default = off */
int	dryrun        = 0;	/* dryrun mode   - default = off */
int	open_failure  = 0;	/* a device or file failed to open */
ULONG	maxblk        = 0;	/* maximum block - default = none */
ULONG	source_start  = 0;	/* src start blk - default = zero */
ULONG	dest_start    = 0;	/* dst start blk - default = zero */

static void
print_usage(void)
{
	int len;

	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	len = strlen(progname) + 9;
	fprintf(stderr, "%s %.3s by Chris Hooper on %.11s\n",
		progname, version + 11, version + 16);
	setvbuf(stderr, NULL, _IONBF, 0);

	fprintf(stderr, "Usage: %s [options] {source} {dest}\n", progname);
	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	fprintf(stderr, "%*s -b = specify buffer size\n"
		"%*s -m = specify maximum blocks to transfer\n"
		"%*s-ss = specify source block number at which to start\n"
		"%*s-ds = specify destination block number at which to start\n"
		"%*s -v = turn on verbose mode\n"
		"%*s -h = give more help\n",
		len, "", len, "", len, "", len, "", len, "", len, "");
	exit(1);
}

static void
print_help(void)
{
	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	fprintf(stderr,
	    "%s is a program which will does block copies from the specified source\n"
	    "to the specified destination.  The source and destination may each\n"
	    "independently be either a file or a block device.\n"
	    "\n"
	    "The source and destination are specified as filenames.  A device is\n"
	    "differentiated from a file as having a colon (:) as the last character\n"
	    "of the filename.  A device may also be specified by giving the Amiga\n"
	    "device information, separated by commas (,).  The source is always\n"
	    "specified first.  WARNING: A comma makes it a device!"
	    "\n"
	    "Examples of usage:\n"
	    "\n"
	    "Copy file \"unix_fs\" from dh0: and overwrite df0: with that file\n"
	    "\t%s dh0:unix_fs df0:\n"
	    "\n"
	    "Copy 8192 blocks from raw SCSI disk, unit 6 starting at block 16384\n"
	    "and write that to file \"junk\" in current directory\n"
	    "\t%s -ss 16384 -m 8192 scsi.device,6 junk\n"
	    "\n"
	    "Copy partition BSDR: to a file \"BSD_root\"\n"
	    "\t%s bsdr: BSD_root\n"
	    "\n"
	    "Copy first five blocks from partition BAK: to block 1532 in partition TEST:\n"
	    "\t%s -m 5 -ds 1532 BAK: Test:\n",
	    progname, progname, progname, progname, progname);
	exit(1);
}

static void
do_close(struct file_info *fdes)
{
	switch (fdes->dtype) {
	    case tNIL:
	    case tSTDIO:
		break;
	    case tFILE:
		if (!dryrun)
		    fclose(fdes->strIO);
		break;
	    case tDEVICE:
		if (fdes->trackIO != NULL) {
		    /* first, shut the motor off */
		    fdes->trackIO->iotd_Req.io_Command = TD_MOTOR;
		    fdes->trackIO->iotd_Req.io_Flags   = 0x0;  /* Motor status */
		    fdes->trackIO->iotd_Req.io_Length  = 0;    /* 0=off, 1=on */
		    DoIO((struct IORequest *)fdes->trackIO);

		    CloseDevice((struct IORequest *)fdes->trackIO);
		    DeletePort(fdes->trackIO->iotd_Req.io_Message.mn_ReplyPort);
		    DeleteExtIO((struct IORequest *)fdes->trackIO);
		}
		break;
	}
}

/* do_io() performs I/O to or from a device.  It returns 0 on success and
 *	   non-zero on failure.
 */
static int
do_io(struct file_info *fdes, int dtype)
{
	int io_left = 0;
	int last_write_blks;

	if (dtype == dREAD) {
		io_left = buffer_blocks;

		if (fdes->maxblk)
			if ((fdes->maxblk - fdes->currentblk) < io_left)
				io_left = fdes->maxblk - fdes->currentblk;
#undef DEBUG
#ifdef DEBUG
		fprintf(stderr, "tran=R blocks=%d cur=%u max=%u left=%d\n",
			buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
		if (io_left < 1)
			return(1);
	} else {
		io_left = last_read_blocks;

		if (fdes->maxblk)
			if ((fdes->maxblk - fdes->currentblk) < io_left)
				io_left = fdes->maxblk - fdes->currentblk;
#ifdef DEBUG
		fprintf(stderr, "tran=W blocks=%d cur=%u max=%u left=%d\n",
			buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
		if (io_left < 1)
			return(1);
	}

	if (verbose && (fdes->dtype != tNIL)) {
		setvbuf(stderr, NULL, _IONBF, 0);
		fprintf(stderr, "%s at %-8d  count=%d%5s\r",
			(dtype == dREAD) ? "Read " : "Write",
			fdes->currentblk, io_left, "");
#ifdef DEBUG
		fprintf(stderr, "\n");
#endif
		setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));
	}

	if (dtype == dREAD)
		last_read_blocks = io_left;

	if (dryrun) {
	    fdes->currentblk += io_left;
	    return (0);
	}

	switch (fdes->dtype) {
	    case tDEVICE: {
		ULONG blk    = fdes->currentblk;
		ULONG high32 = blk >> (32 - TD_SECSHIFT);

		if (high32) {
		    if (fdes->use_td64 == 0) {
			printf("Can't address blk %u with 32-bit only "
			       "device\n", blk);
			return (1);
		    }
		    fdes->trackIO->iotd_Req.io_Command = TD_READ64;
		    fdes->trackIO->iotd_Req.io_Actual =
						blk >> (32 - TD_SECSHIFT);
		}
		if (dtype == dREAD) {
		    if (high32)
			fdes->trackIO->iotd_Req.io_Command = TD_READ64;
		    else
			fdes->trackIO->iotd_Req.io_Command = CMD_READ;
		} else {
		    if (high32)
			fdes->trackIO->iotd_Req.io_Command = TD_WRITE64;
		    else
			fdes->trackIO->iotd_Req.io_Command = CMD_WRITE;
		}
		fdes->trackIO->iotd_Req.io_Length = io_left * TD_SECTOR;
		fdes->trackIO->iotd_Req.io_Offset = blk << TD_SECSHIFT;
		if (DoIO((struct IORequest *) fdes->trackIO)) {
		    if (high32 && fdes->use_td64 == -1) {
			fdes->use_td64 = 0;
			return (do_io(fdes, dtype));
		    }
		    return (1);
		}
		if (high32 && fdes->use_td64 == -1)
		    fdes->use_td64 = 1;
		break;
	    }
	    case tFILE:
	    case tSTDIO:
		if (dtype == dREAD)
		    last_read_blocks = fread(buffer, TD_SECTOR, io_left, fdes->strIO);
		else {
		    last_write_blks = fwrite(buffer, TD_SECTOR, io_left, fdes->strIO);
		    if (last_write_blks != io_left) {
			if (last_write_blks >= 0)
			    fprintf(stderr, "destination full before source exhausted.\n");
			else
			    fprintf(stderr, "write did not finish normally: %d of %d blocks written\n", last_write_blks, io_left);
			return(1);
		    }
		}
		break;
	    case tNIL:
		break;
	}

	fdes->currentblk += io_left;

	return(0);

#ifdef NOTHING
	if ((fdes->dtype == tDEVICE) && !dryrun) {
	    fdes->trackIO->iotd_Req.io_Length = io_left * TD_SECTOR;
	    fdes->trackIO->iotd_Req.io_Offset = fdes->currentblk * TD_SECTOR;
	    DoIO((struct IORequest *)fdes->trackIO);
	    if (dtype == dREAD)
		last_read_blocks = io_left;
	} else if (!dryrun) {			/* Must be either file or stdio */
	    if (dtype == dREAD)
		last_read_blocks = fread(buffer, TD_SECTOR, io_left, fdes->strIO);
	    else if ((last_write_blks = fwrite(buffer, TD_SECTOR, io_left,
					       fdes->strIO)) != io_left) {
		if (last_write_blks >= 0)
		    fprintf(stderr, "destination full before source exhausted.\n");
		else
		    fprintf(stderr, "failure: write did not finish normally\n");
		return(1);
	    }
	} else if (dtype == dREAD)
		last_read_blocks = io_left;

	fdes->currentblk += io_left;

	return(0);
#endif
}

int
break_abort(void)
{
	static int entered = 0;

	if (entered++)
		return;

	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));
	fprintf(stderr, "^C\n");

	do_close(&in);
	do_close(&out);

	if (buffer != NULL)
		FreeMem(buffer, buffer_blocks * TD_SECTOR);

	exit(1);
}

static struct FileSysStartupMsg *
find_startup(char *name)
{
	struct	DosLibrary *DosBase;
	struct	RootNode *rootnode;
	struct	DosInfo *dosinfo;
	struct	DevInfo *devinfo;
	static  struct FileSysStartupMsg *startup;
	char	*devname;
	char	*pos;
	int	notfound = 1;

	if ((pos = strchr(name, ':')) != NULL)
		*pos = '\0';

	DosBase = (struct DosLibrary *) OpenLibrary("dos.library", 0L);

	rootnode= DosBase->dl_Root;
	dosinfo = (struct DosInfo *) BTOC(rootnode->rn_Info);
	devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

	while (devinfo != NULL) {
		devname	= (char *) BTOC(devinfo->dvi_Name);
		if (stricmp(devname + 1, name) == 0) {
			notfound = 0;
			break;
		}
		devinfo	= (struct DevInfo *) BTOC(devinfo->dvi_Next);
	}

	if (notfound) {
	    startup = NULL;
	    open_failure++;
	} else {
	    startup = (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
	}

	CloseLibrary((struct Library *)DosBase);
	return(startup);
}

static void
do_open(struct file_info *fdes, int dtype)
{
	int len;
	char *pos;
	struct MsgPort *temp;

	fdes->currentblk = 0;		/* start at beginning */
	fdes->maxblk = 0;		/* no maximum block */
	fdes->use_td64 = -1;		/* probe for TD64 if required */

	if (verbose)
		if (dtype == dREAD)
			fprintf(stderr, "R ");
		else
			fprintf(stderr, "W ");

	if (!strcmp(fdes->name, "-")) {		/* stdio */
		fdes->dtype = tSTDIO;
		if (dtype == dREAD)
			fdes->strIO = stdin;
		else
			fdes->strIO = stdout;
		if (verbose)
			fprintf(stderr, "STDIO %s\n", fdes->name);
		return;
	}

	len = strlen(fdes->name);
	if (len == 0) {
		fprintf(stderr, "Error opening file for %s.\n",
			(dtype == dREAD) ? "read" : "write");
		fdes->dtype = tNONE;
		return;
	}

	pos = strchr(fdes->name, ',');

	if (!pos && (fdes->name[len - 1] != ':')) {	/* must be a file */
	    if (dtype == dREAD) {
		struct stat stat_buf;
		if (!stat(fdes->name, &stat_buf))
		    fdes->maxblk = (stat_buf.st_size + TD_SECTOR - 1) / TD_SECTOR;
	    }
	    if (!dryrun)
	        fdes->strIO = fopen(fdes->name, (dtype == dREAD) ? "r" : "w");
	    if ((fdes->strIO == NULL) && !dryrun) {
		fprintf(stderr, "Error opening file %s for %s.\n",
			fdes->name, (dtype == dREAD) ? "read" : "write");
		fdes->dtype = tNONE;
		open_failure++;
	    } else {
		fdes->dtype = tFILE;
		if (verbose) {
		    fprintf(stderr, "file %-40s", fdes->name);
		    fprintf(stderr, "start=%-7u  ", fdes->currentblk);
		    fprintf(stderr, "end=%-7u\n", fdes->maxblk);
		}
	    }
	} else {				/* must be a device */
	    struct FileSysStartupMsg *startup;
	    struct DosEnvec *envec;
	    char *disk_device;
	    int disk_unit, disk_flags;

	    /* first, check to make sure it's not the null device */
	    if (stricmp(fdes->name, "nil:") == 0) {
		fdes->dtype	 = tNIL;
		fdes->currentblk = 0;
		fdes->maxblk     = 0;
		if (verbose)
			fprintf(stderr, "Nil:\n");
		if (fdes->dtype == dREAD)
			memset(buffer, 0, buffer_blocks * TD_SECTOR);
		return;
	    }

	    /* create port to use when talking with handler */
	    temp = CreatePort(0, 0);
	    if (temp == NULL) {
		fprintf(stderr, "Failed to create port for %s\n", fdes->name);
		fdes->dtype = tNONE;
		open_failure++;
		return;
	    }

	    fdes->trackIO = (struct IOExtTD *)
			    CreateExtIO(temp, sizeof(struct IOExtTD));
	    if (fdes->trackIO == NULL) {
		fprintf(stderr, "Failed to create trackIO %s%s\n",
			"structure for ", fdes->name);
		DeletePort(temp);
		fdes->dtype = tNONE;
		open_failure++;
		return;
	    }

	    if (pos) {
		char *pos2;

		disk_device = fdes->name;
		disk_flags  = 0;

		*pos = '\0';

		if (!isalpha(*(pos + 1))) {
		    disk_unit = atoi(pos + 1);
		    pos2 = strchr(pos + 1, ',');
		    if (pos2)
			if (!isalpha(*(pos2 + 1))) {
				disk_flags = atoi(pos2 + 1);
			} else {
				fprintf(stderr, "Error opening device %s: for %s; flags=%s; flags must be an integer!\n", fdes->name, (dtype == dREAD) ? "read" : "write", pos2 + 1);
				open_failure++;
				goto failed_open;
			}
		} else {
		    fprintf(stderr, "Error opening device %s for %s; you must specify a unit number!\n", fdes->name, (dtype == dREAD) ? "read" : "write");
		    open_failure++;
		    goto failed_open;
		}
		fdes->currentblk = 0;
		fdes->maxblk     = 0;
	    } else {
		startup = find_startup(fdes->name);
		if (startup == NULL) {
		    fprintf(stderr, "Error opening device %s for %s; does not exist\n",
			    fdes->name, (dtype == dREAD) ? "read" : "write");
		    open_failure++;
		    goto failed_open;
		}

		disk_device      = ((char *) BTOC(startup->fssm_Device)) + 1;
		disk_unit        = startup->fssm_Unit;
		disk_flags       = startup->fssm_Flags;
		envec            = (struct DosEnvec *) BTOC(startup->fssm_Environ);
		fdes->currentblk  = envec->de_LowCyl * envec->de_Surfaces *
				    envec->de_BlocksPerTrack;
		fdes->maxblk      = (envec->de_HighCyl + 1) *
		                    envec->de_Surfaces *
		                    envec->de_BlocksPerTrack;
		if (envec->de_SizeBlock * 4 > TD_SECTOR)
		    fdes->maxblk *= (envec->de_SizeBlock * 4 / TD_SECTOR);
		else
		    fdes->maxblk /= (TD_SECTOR / envec->de_SizeBlock / 4);
	    }

	    if (verbose) {
		char buf[40];
		sprintf(buf, "%s=%s", fdes->name, disk_device);
		fprintf(stderr, "dev  %-21s ", buf);
		fprintf(stderr, "unit=%-2d  ", disk_unit);
		fprintf(stderr, "flag=%-2d  ", disk_flags);
		fprintf(stderr, "start=%-7u  ", fdes->currentblk);
		fprintf(stderr, "end=%-7u\n", fdes->maxblk);
	    }

	    if (OpenDevice(disk_device, disk_unit,
	                   (struct IORequest *)fdes->trackIO, disk_flags)) {
		fprintf(stderr, "fatal: Unable to open %s unit %d for %s.\n",
			disk_device, disk_unit, fdes->name);
failed_open:
		DeletePort(fdes->trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO((struct IORequest *)fdes->trackIO);
		return;
	    }

	    fdes->trackIO->iotd_Req.io_Command = (dtype == dREAD) ?
	                                         CMD_READ : CMD_WRITE;
	    fdes->trackIO->iotd_Req.io_Data    = buffer;
	    fdes->dtype = tDEVICE;

	    if (pos)
		*pos = ',';
	}
}

static void
set_limits(struct file_info *in, struct file_info *out)
{
	if (source_start)
		in->currentblk += source_start;

	if (dest_start)
		out->currentblk += dest_start;

	if (maxblk) {
		if ((in->maxblk - in->currentblk > maxblk) ||
		    (in->maxblk == 0))
			in->maxblk = in->currentblk + maxblk;
		if ((out->maxblk - out->currentblk > maxblk) ||
		    (out->maxblk == 0))
			out->maxblk = out->currentblk + maxblk;
	}

	if (in->maxblk && (in->currentblk > in->maxblk))
		open_failure++;

	if (out->maxblk && (out->currentblk > out->maxblk))
		open_failure++;

	if (!dryrun && (in->dtype == tFILE) && (in->dtype != tNONE))
		fseek(in->strIO, in->currentblk * TD_SECTOR, SEEK_SET);

	if (!dryrun && (out->dtype == tFILE) && (out->dtype != tNONE))
		fseek(out->strIO, out->currentblk * TD_SECTOR, SEEK_SET);
}

int
main(int argc, char *argv[])
{
	int names = 0;
	int index;

	progname = argv[0];

	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	if (argc == 1)
		print_usage();

	for (index = 1; index < argc; index++) {
	    if ((argv[index][0] == '-') && (argv[index][1] != '\0')) {
		ACCEPT(maxblk, "-m")
		else ACCEPT(maxblk, "-M")
		else ACCEPT(buffer_blocks, "-b")
		else ACCEPT(source_start, "-ss")
		else ACCEPT(dest_start, "-sd")
		else ACCEPT(dest_start, "-ds")
		else if (tolower(argv[index][1]) == 'v')
		    verbose++;
		else if (tolower(argv[index][1]) == 'h')
		    print_help();
		else if (tolower(argv[index][1]) == 'd')
		    dryrun++;
		else {
		    fprintf(stderr, "Unrecognized option %s\n",
			    argv[index]);
		    print_help();
		}
	    } else {
		names++;
		if (names == 1)
		    in.name  = argv[index];
		else if (names == 2)
		    out.name = argv[index];
		else {
		    fprintf(stderr, "Unknown parameter %s\n", argv[index]);
		    print_usage();
		}
	    }
	}

	if (names != 2) {
		printf("Source and destination are required for %s\n", progname);
		exit(1);
	}

	if (maxblk && (buffer_blocks > maxblk))
		buffer_blocks = maxblk;

	while ((buffer == NULL) && (buffer_blocks)) {
		buffer = (char *) AllocMem(buffer_blocks *
						 TD_SECTOR, MEMF_PUBLIC);
		if (buffer == NULL)
			buffer_blocks >>= 1;
	}
	memset(buffer, 0, buffer_blocks * TD_SECTOR);

	if (buffer == NULL) {
		fprintf(stderr, "Unable to allocate even 1 block for copy buffer!\n");
		exit(1);
	}

	onbreak(break_abort);

	do_open(&in,  dREAD);
	do_open(&out, dWRITE);

	if (!open_failure)
	    set_limits(&in, &out);

	if (!open_failure) {
	    if ((in.dtype != tNONE) && (out.dtype != tNONE))
		    while (do_io(&in, dREAD) == 0) {
			    chkabort();
			    if (do_io(&out, dWRITE))
				    break;
			    chkabort();
		    }

	    if (verbose)
		    fprintf(stderr, "\n");
	}

	do_close(&in);
	do_close(&out);

	FreeMem(buffer, buffer_blocks * TD_SECTOR);

	exit(0);
}
