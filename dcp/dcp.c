/*
 * File/Device copier by Chris Hooper (cdh@mtu.edu) on  5-Jul-93  v1.0
 *						       14-Oct-93  v1.1
 *						       08-Nov-93  v1.2
 *						       27-Jan-96  v1.21
 *
 *	This program can copy files to devices and devices to files, etc.
 *
 * Usage:
 *    dcp {source} {dest} [-m number of blocks] [-b buffer size] [-v verbose]
 *			  [-ss source start block] [-ds dest start block]
 *			  [-d dryrun] [-h help]
 *
 * This product is distributed as freeware with no warrantees expressed or
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

#include <stdio.h>
#include <exec/types.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
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
#define tNONE 	0
#define tDEVICE	1
#define tFILE 	2
#define tSTDIO 	3
#define tNIL 	4

#define dREAD  0
#define dWRITE 1

/* BCPL conversion functions */
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

/* quick parameter parser */
#define ACCEPT(x,y)								\
	if (!strcmp(argv[index], y)) {						\
	    index++;								\
	    if (index < argc) {							\
	        x = atoi(argv[index]);						\
	        if (x < 1) {							\
		    fprintf(stderr, "number required for %s parameter\n", y);	\
		    exit(1);							\
	        }								\
	    } else {								\
	        fprintf(stderr, "number required for %s parameter\n", y);	\
	        exit(1);							\
	    }									\
	}


char *strchr();
struct FileSysStartupMsg *find_startup();
void break_abort();


struct file_info {
	char	*name;		  /* filename as specified on cmdline */
	int	dtype;		  /* NONE, DEVICE, FILE, or STDIO */
	FILE	*strIO;		  /* stream file pointer */
	struct	IOExtTD *trackIO; /* device packet pointer */
	ULONG	currentblk;	  /* current read/write block in device/file */
	ULONG	maxblk;		  /* maximum allowed read/write blk in dev/file */
};


struct file_info in;		/* internal input file descriptor */
struct file_info out;		/* internal output file descriptor */

char	*progname;		/* name of this program running */
char	iobuf[128];		/* buffer for line-oriented I/O */
char	*version = "1.21";	/* program revision */
char	*revdate = "27-Jan-96";	/* program revision date */
int	last_read_blocks;	/* number of blocks last read */
char	*buffer	= NULL;		/* buffer area for copy operations */
int	buffer_blocks = 2000;	/* size of buffer area */
int	verbose       = 0;	/* verbose mode  - default = off */
int	dryrun        = 0;	/* dryrun mode   - default = off */
ULONG	maxblk        = 0;	/* maximum block - default = none */
ULONG	source_start  = 0;	/* src start blk - default = zero */
ULONG	dest_start    = 0;	/* dst start blk - default = zero */


main(argc, argv)
int argc;
char *argv[];
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
		else ACCEPT(buffer_blocks, "-B")
		else ACCEPT(source_start, "-ss")
		else ACCEPT(source_start, "-SS")
		else ACCEPT(dest_start, "-sd")
		else ACCEPT(dest_start, "-SD")
		else ACCEPT(dest_start, "-ds")
		else ACCEPT(dest_start, "-DS")
		else if ((argv[index][1] == 'v') ||
			 (argv[index][1] == 'V'))
		    verbose++;
		else if ((argv[index][1] == 'h') ||
			 (argv[index][1] == 'H'))
		    print_help();
		else if ((argv[index][1] == 'd') ||
			 (argv[index][1] == 'D'))
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

	set_limits(&in, &out);

	if ((in.dtype != tNONE) && (out.dtype != tNONE))
		while (do_io(&in, dREAD)) {
			chkabort();
			if (do_io(&out, dWRITE) == 0)
				break;
			chkabort();
		}

	if (verbose)
		fprintf(stderr, "\n");

	do_close(&in);
	do_close(&out);

	FreeMem(buffer, buffer_blocks * TD_SECTOR);

	exit(0);
}

do_open(fdes, dtype)
struct file_info *fdes;
int dtype;
{
	int len;
	char *pos;
	ULONG temp;

	fdes->currentblk = 0;		/* start at beginning */
	fdes->maxblk = 0;		/* no maximum block */

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
	    } else {
		fdes->dtype = tFILE;
		if (verbose) {
		    fprintf(stderr, "file %-40s", fdes->name);
		    fprintf(stderr, "start=%-7d  ", fdes->currentblk);
		    fprintf(stderr, "end=%-7d\n", fdes->maxblk);
		}
	    }
	} else {				/* must be a device */
	    struct FileSysStartupMsg *startup;
	    struct DosEnvec *envec;
	    char *disk_device;
	    int disk_unit, disk_flags;

	    /* first, check to make sure it's not the null device */
	    if (unstrcmp(fdes->name, "nil:")) {
		fdes->dtype	 = tNIL;
		fdes->currentblk = 0;
		fdes->maxblk     = 0;
		if (verbose)
			fprintf(stderr, "Nil:\n");
		if (fdes->dtype == dREAD)
			ZeroMem(buffer, buffer_blocks * TD_SECTOR);
		return;
	    }

	    /* create port to use when talking with handler */
	    temp = CreatePort(0, 0);
	    if (temp)
	        fdes->trackIO = (struct IOExtTD *)
				CreateExtIO(temp, sizeof(struct IOExtTD));
	    else {
		fdes->trackIO = NULL;
		DeletePort(temp);
	    }

	    if (fdes->trackIO == NULL) {
		fprintf(stderr, "Failed to create trackIO %s%s\n",
			"structure for ", fdes->name);
		fdes->dtype = tNONE;
		return;
	    }

	    if (pos) {
		char *pos2;

		disk_device = fdes->name;
		disk_flags  = 0;

		*pos = '\0';

		if (!isalpha(*(pos + 1))) {
		    disk_unit = atoi(pos + 1);
		    pos2 = strchr(pos + 1, ",");
		    if (pos2)
			if (!isalpha(*(pos2 + 1)))
				disk_flags = atoi(pos2 + 1);
			else {
				fprintf(stderr, "Error opening device %s for %s; flags=%s; flags must be an integer!\n", fdes->name, (dtype == dREAD) ? "read" : "write", pos2 + 1);
				fdes->dtype = tNONE;
				return;
			}
		} else {
		    fprintf(stderr, "Error opening device %s for %s; you must specify a unit number!\n", fdes->name, (dtype == dREAD) ? "read" : "write");
		    fdes->dtype = tNONE;
		    return;
		}
		fdes->currentblk = 0;
		fdes->maxblk     = 0;
	    } else {
		startup = find_startup(fdes->name);
		if (startup == NULL) {
		    fprintf(stderr, "Error opening device %s for %s; does not exist\n",
			    fdes->name, (dtype == dREAD) ? "read" : "write");
		    fdes->dtype = tNONE;
		    return;
		}

/*
printf("doing assignments\n");
*/
		disk_device      = ((char *) BTOC(startup->fssm_Device)) + 1;
		disk_unit        = startup->fssm_Unit;
		disk_flags       = startup->fssm_Flags;
		envec            = (struct DosEnvec *) BTOC(startup->fssm_Environ);
/*
printf("doing more assignments\n");
*/
		fdes->currentblk = envec->de_LowCyl * envec->de_Surfaces *
				   envec->de_BlocksPerTrack;
		fdes->maxblk     = (envec->de_HighCyl + 1) * envec->de_Surfaces *
				   envec->de_BlocksPerTrack;
	    }

	    if (verbose) {
		char buf[40];
		sprintf(buf, "%s=%s", fdes->name, disk_device);
		fprintf(stderr, "dev  %-21s ", buf);
		fprintf(stderr, "unit=%-2d  ", disk_unit);
		fprintf(stderr, "flag=%-2d  ", disk_flags);
		fprintf(stderr, "start=%-7d  ", fdes->currentblk);
		fprintf(stderr, "end=%-7d\n", fdes->maxblk);
	    }

/*
printf("opening device %s\n", disk_device);
*/
	    if (OpenDevice(disk_device, disk_unit, fdes->trackIO, disk_flags)) {
		fprintf(stderr, "fatal: Unable to open %s unit %d for %s.\n",
			disk_device, disk_unit, fdes->name);
		DeletePort(fdes->trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO(fdes->trackIO);
		return;
	    }
/*
printf("done opening device\n");
*/

	    fdes->trackIO->iotd_Req.io_Command  = (dtype == dREAD) ?
						CMD_READ : CMD_WRITE;
	    fdes->trackIO->iotd_Req.io_Data	    = buffer;

	    fdes->dtype = tDEVICE;

	    if (pos)
		*pos = ',';
	}
}


set_limits(in, out)
struct file_info *in;
struct file_info *out;
{
	if (source_start)
		in->currentblk += source_start;

	if (dest_start)
		out->currentblk += dest_start;

	if (maxblk) {
		if ((in->maxblk - in->currentblk > maxblk) ||
		    (in->maxblk == 0))
			in->maxblk = maxblk - in->currentblk;
		if ((out->maxblk - out->currentblk > maxblk) ||
		    (out->maxblk == 0))
			out->maxblk = maxblk - out->currentblk;
	}

	if (in->maxblk && (in->currentblk > in->maxblk))
		in->dtype = tNONE;

	if (out->maxblk && (out->currentblk > out->maxblk))
		out->dtype = tNONE;

	if (!dryrun && (in->dtype == tFILE) && (in->dtype != tNONE))
		fseek(in->strIO, in->currentblk * TD_SECTOR, SEEK_SET);

	if (!dryrun && (out->dtype == tFILE) && (out->dtype != tNONE))
		fseek(out->strIO, out->currentblk * TD_SECTOR, SEEK_SET);
}

struct FileSysStartupMsg *find_startup(name)
char *name;
{
	struct	DosLibrary *DosBase;
	struct	RootNode *rootnode;
	struct	DosInfo *dosinfo;
	struct	DevInfo *devinfo;
	struct	DosEnvec *envec;
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
		if (unstrcmp(devname + 1, name)) {
			notfound = 0;
			break;
		}
		devinfo	= (struct DevInfo *) BTOC(devinfo->dvi_Next);
	}

	if (notfound) {
		fprintf(stderr, "%s: is not mounted.\n", name);
		exit(1);
	}

	startup	= (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
	CloseLibrary(DosBase);
	return(startup);
}

/* unsigned string compare */
int unstrcmp(str1, str2)
char *str1;
char *str2;
{
	while (*str1 != '\0') {
		if (*str2 == '\0')
			break;
		else if (*str1 != *str2)
			if ((*str1 >= 'A') && (*str1 <= 'Z')) {
				if ((*str2 >= 'A') && (*str2 <= 'Z'))
					break;
				else if (*str1 != *str2 + 'A' - 'a')
					break;
			} else {
				if ((*str2 >= 'a') && (*str2 <= 'a'))
					break;
				else if (*str1 != *str2 - 'A' + 'a')
					break;
			}
		str1++;
		str2++;
	}
	if (*str1 == *str2)
		return(1);
	else
		return(0);
}

do_close(fdes)
struct file_info *fdes;
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
		    DoIO(fdes->trackIO);

		    CloseDevice(fdes->trackIO);
		    DeletePort(fdes->trackIO->iotd_Req.io_Message.mn_ReplyPort);
		    DeleteExtIO(fdes->trackIO);
		}
		break;
	}
}


do_io(fdes, dtype)
struct file_info *fdes;
int dtype;
{
	int io_left = 0;
	int last_write_blks;

	if (dtype == dREAD) {
		io_left = buffer_blocks;

		if (fdes->maxblk)
			if ((fdes->maxblk - fdes->currentblk) < io_left)
				io_left = fdes->maxblk - fdes->currentblk;
#ifdef DEBUG
		fprintf(stderr, "tran=R blocks=%d cur=%d max=%d left=%d\n",
			buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
		if (io_left < 1)
			return(0);
	} else {
		io_left = last_read_blocks;

		if (fdes->maxblk)
			if ((fdes->maxblk - fdes->currentblk) < io_left)
				io_left = fdes->maxblk - fdes->currentblk;
#ifdef DEBUG
		fprintf(stderr, "tran=W blocks=%d cur=%d max=%d left=%d\n",
			buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
		if (io_left < 1)
			return(0);
	}

	if (verbose) {
		setvbuf(stderr, NULL, _IONBF, 0);
		fprintf(stderr, "%s at %-7d  count=%d%5s\r",
			(dtype == dREAD) ? "Read " : "Write",
			fdes->currentblk, io_left, "");
#ifdef DEBUG
		fprintf(stderr, "\n");
#endif
		setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));
	}

	if (dtype == dREAD)
		last_read_blocks = io_left;

	if (!dryrun) switch(fdes->dtype) {
	    case tDEVICE:
		fdes->trackIO->iotd_Req.io_Length = io_left * TD_SECTOR;
		fdes->trackIO->iotd_Req.io_Offset = fdes->currentblk * TD_SECTOR;
		DoIO(fdes->trackIO);
		break;
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
			return(0);
		    }
		}
		break;
	    case tNIL:
		break;
	}

	fdes->currentblk += io_left;

	return(1);

#ifdef NOTHING
	if ((fdes->dtype == tDEVICE) && !dryrun) {
	    fdes->trackIO->iotd_Req.io_Length = io_left * TD_SECTOR;
	    fdes->trackIO->iotd_Req.io_Offset = fdes->currentblk * TD_SECTOR;
	    DoIO(fdes->trackIO);
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
		return(0);
	    }
	} else if (dtype == dREAD)
		last_read_blocks = io_left;

	fdes->currentblk += io_left;

	return(1);
#endif
}


print_usage()
{
	int len;

	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	len = strlen(progname) + 9;
	fprintf(stderr, "%s %s by Chris Hooper on %s\n",
		progname, version, revdate);
	setvbuf(stderr, NULL, _IONBF, 0);
	fprintf(stderr, "Usage: %s [options] {source} {dest}\n", progname);
	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));
	fprintf(stderr, "%*s -b = specify buffer size\n", len, "");
	fprintf(stderr, "%*s -m = specify maximum blocks to transfer\n", len, "");
	fprintf(stderr, "%*s-ss = specify source block number at which to start\n", len, "");
	fprintf(stderr, "%*s-ds = specify destination block number at which to start\n", len, "");
	fprintf(stderr, "%*s -v = turn on verbose mode\n", len, "");
	fprintf(stderr, "%*s -h = give more help\n", len, "");
	exit(1);
}

print_help()
{
	setvbuf(stderr, iobuf, _IOLBF, sizeof(iobuf));

	fprintf(stderr, "%s is a program which will does block copies from the specified source\n", progname);
	fprintf(stderr, "to the specified destination.  The source and destination may each\n");
	fprintf(stderr, "independently be either a file or a block device.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "The source and destination are specified as filenames.  A device is\n");
	fprintf(stderr, "differentiated from a file as having a colon (:) as the last character\n");
	fprintf(stderr, "of the filename.  A device may also be specified by giving the Amiga\n");
	fprintf(stderr, "device information, separated by commas (,).  The source is always\n");
	fprintf(stderr, "specified first.  WARNING: A comma makes it a device!");
	fprintf(stderr, "\n");
	fprintf(stderr, "Examples of usage:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Copy file \"unix_fs\" from dh0: and overwrite df0: with that file\n");
	fprintf(stderr, "\t%s dh0:unix_fs df0:\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Copy 8192 blocks from raw SCSI disk, unit 6 starting at block 16384\n");
	fprintf(stderr, "and write that to file \"junk\" in current directory\n");
	fprintf(stderr, "\t%s -ss 16384 -m 8192 scsi.device,6 junk\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Copy partition BSDR: to a file \"BSD_root\"\n");
	fprintf(stderr, "\t%s bsdr: BSD_root\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Copy first five blocks from partition BAK: to block 1532 in partition TEST:\n");
	fprintf(stderr, "\t%s -m 5 -ds 1532 BAK: Test:\n", progname);
	exit(1);
}

void break_abort()
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


ZeroMem(buf, len)
ULONG *buf;
ULONG len;
{
	ULONG *end;
	end = buf + (len >> 2);
	while (buf < end)
		*(buf++) = 0;
}
