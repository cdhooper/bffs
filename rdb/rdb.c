/*
 *
 * This product is distributed as freeware with no warrantees expressed or
 * implied.  There are no restrictions on distribution or application of
 * this program other than it may not be used in contribution with or
 * converted to an application under the terms of the GNU Public License
 * nor sold for profit greater than the cost of distribution.  Also, this
 * notice must accompany all redistributions and/or modifications of this
 * program.  Remember:  If you expect bugs, you should not be disappointed.
 *
 */

#include <stdio.h>
#include <exec/types.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <exec/memory.h>
#include <devices/hardblocks.h>

/* BCPL conversion functions */
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

#ifndef TD_SECTOR
#define TD_SECTOR 512
#endif

char *strchr();
struct FileSysStartupMsg *find_startup();
void break_abort();
ULONG chars_to_long();

struct	IOExtTD *trackIO = NULL; /* device packet pointer */
struct  MsgPort *devport = NULL; /* device communication port */

char	*version = "\0$VER: rdb 1.1 (20.Jan.94) © 1994 Chris Hooper";

char	*progname;		/* name of this program running */
char	*buffer	= NULL;		/* buffer area for disk read */
struct  RigidDiskBlock *rdb;	/* rdb data pointer */
struct  BadBlockBlock *bad;	/* bad block info pointer */
struct	PartitionBlock *part;	/* partition data pointer */
struct	PartitionBlock *ppart;	/* partition data pointer */
struct	FileSysHeaderBlock *fs;	/* filesystem data pointer */
struct	LoadSegBlock *seg;	/* loadseg data pointer */

#define NO	0
#define YES	1
#define ALL	2
#define QUIT	3

char	*disk_device = "some.device";
int	disk_unit = 0;
int	disk_flags = 0;
int	autofix = 0;
int	globalfix = 0;
int	maxaccess = 0;
int	editpart = 0;
int	editrigid = 0;
int	createpart = 0;
int	promote = 0;
char	partname[32];
int	extractfs = 0;
int	rdb_blk = 0;
int	bstr = 0;
int	showseglist = 0;
ULONG	extractid = 0;


struct RigidDiskBlock newrigid =
{
	IDNAME_RIGIDDISK,			/* ID			*/
	128,					/* SummedLongs		*/
	0,					/* ChkSum		*/
	7,					/* HostID		*/
	512,					/* BlockBytes		*/
	0x38,  /* NoReselect, Disk/Cntrlr Valid ** Flags		*/
	-1,					/* BadBlockList		*/
	-1,					/* PartitionList	*/
	-1,					/* FileSysHeaderList	*/
	-1,					/* DriveInit		*/
	0,0,0, 0,0,0,				/* Reserved1[6]		*/
	610,					/* Cylinders		*/
	17,					/* Sectors		*/
	4,					/* Heads		*/
	1,					/* Interleave		*/
	612,					/* Park			*/
	0,0,0,					/* Reserved2[3]		*/
	-1,					/* WritePreComp		*/
	-1,					/* ReducedWrite		*/
	-1,					/* StepRate		*/
	0,0,0, 0,0,				/* Reserved3[5]		*/
	0,					/* RDBBlocksLo		*/
	67,					/* RDBBlocksHi		*/
	1,					/* LoCylinder		*/
	610,					/* HiCylinder		*/
	68,					/* CylBlocks		*/
	0,					/* AutoParkSeconds	*/
	0,0,					/* Reserved4[2]		*/
	"none",					/* DiskVendor[8]	*/
	"none",					/* DiskProduct[16]	*/
	"none",					/* DiskRevision[4]	*/
	"none",					/* ControllerVendor[8]	*/
	"none",					/* ControllerProduct[16]*/
	"none",					/* ControllerRevision[4]*/
	0,0,0,0,0, 0,0,0,0,0			/* Reserved5[10]	*/
};

struct PartitionBlock newpart =
{
	IDNAME_PARTITION,			/* ID		*/
	128,					/* SummedLongs	*/
	0,					/* ChkSum	*/
	7,					/* HostID	*/
	-1,					/* Next		*/
	0,					/* Flags	*/
	0, 0,					/* Reserved1[2]	*/
	0,					/* DevFlags	*/
	"\004XXXX",				/* DriveName	*/
	0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,	/* Reserved2[15]*/
	17,					/* TableSize	*/
	128,					/* SizeBlock	*/
	0,					/* SecOrg	*/
	4,					/* Surfaces	*/
	1,					/* SecPerBlock	*/
	17,					/* BlocksPerTrk	*/
	0,					/* Reserved	*/
	0,					/* PreAlloc	*/
	0,					/* Interleave	*/
	2,					/* LowCyl	*/
	610,					/* HighCyl	*/
	10,					/* NumBuffers	*/
	1,					/* BufMemType	*/
	0x00ffffff,				/* MaxTransfer	*/
	0xfffffffe,				/* Mask		*/
	0,					/* BootPri	*/
	0x58585858,  /* 'XXXX' */		/* DosType	*/
	0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0		/* EReserved[15]*/
};

#define PART	0
#define RIGID	1
#define FILESYS	2

#define	INT	0
#define STR	1

struct accept_info {
	char *name;
	int offset;
	int type;
	int changed;
	int which;	/* 0=partition, 1=rdb, 2=filesystem */
} accept[] =
/*	name			offset	 type	change	which	*/
{	"HostID",		3,	 INT,	0,	RIGID,
	"BlockBytes",		4,	 INT,	0,	RIGID,
	"RigidFlags",		5,	 INT,	0,	RIGID,
	"BadBlockList",		6,	 INT,	0,	RIGID,
	"PartitionList",	7,	 INT,	0,	RIGID,
	"FileSysHeaderList",	8,	 INT,	0,	RIGID,
	"DriveInit",		9,	 INT,	0,	RIGID,
	"Cylinders",		16,	 INT,	0,	RIGID,
	"Sectors",		17,	 INT,	0,	RIGID,
	"Heads",		18,	 INT,	0,	RIGID,
	"Interleave",		19,	 INT,	0,	RIGID,
	"Park",			20,	 INT,	0,	RIGID,
	"WritePreComp",		24,	 INT,	0,	RIGID,
	"ReducedWrite",		25,	 INT,	0,	RIGID,
	"StepRate",		26,	 INT,	0,	RIGID,
	"RDBBlocksLo",		32,	 INT,	0,	RIGID,
	"RDBBlocksHi",		33,	 INT,	0,	RIGID,
	"LoCylinder",		34,	 INT,	0,	RIGID,
	"HiCylinder",		35,	 INT,	0,	RIGID,
	"CylBlocks",		36,	 INT,	0,	RIGID,
	"AutoParkSeconds",	37,	 INT,	0,	RIGID,
	"DiskVendor",		40,	 STR,	0,	RIGID,
	"DiskProduct",		42,	 STR,	0,	RIGID,
	"DiskRevision",		46,	 STR,	0,	RIGID,
	"ControllerVendor",	47,	 STR,	0,	RIGID,
	"ControllerProduct",	49,	 STR,	0,	RIGID,
	"ControllerRev",	53,	 STR,	0,	RIGID,
	"Next",			4,	 INT,	0,	PART,
	"Flags",		5,	 INT,	0,	PART,
	"DevFlags",		8,	 INT,	0,	PART,
	"DriveName",		9,	 STR,	0,	PART,
	"Surfaces",		32 + 3,  INT,	0,	PART,
	"BlocksPerTrack",	32 + 5,  INT,	0,	PART,
	"Reserved",		32 + 6,  INT,	0,	PART,
	"PreAlloc",		32 + 7,  INT,	0,	PART,
	"Interleave",		32 + 8,  INT,	0,	PART,
	"LowCyl",		32 + 9,  INT,	0,	PART,
	"HighCyl",		32 + 10, INT,	0,	PART,
	"NumBuffers",		32 + 11, INT,	0,	PART,
	"BufMemType",		32 + 12, INT,	0,	PART,
	"MaxTransfer",		32 + 13, INT,	0,	PART,
	"Mask",			32 + 14, INT,	0,	PART,
	"BootPri",		32 + 15, INT,	0,	PART,
	"DosType",		32 + 16, INT,	0,	PART,
	NULL,			0,	 0,	0,	0
};

main(argc, argv)
int argc;
char *argv[];
{
	int	index;
	int	cur;
	int	argnum = 0;
	int	reportmax = 0;
	char	*equalspos;

	progname = argv[0];
	while (*progname)
		progname++;
	while (progname > argv[0])
		if ((*progname == '/') || (*progname == ':')) {
			progname++;
			break;
		} else
			progname--;

	autofix = globalfix;

	for (index = 1; index < argc; index++) {
		if (argv[index][0] == '-') {
		    argv[index]++;
		    if (unstrcmp(argv[index], "avail") ||
			     unstrcmp(argv[index], "a")) {
			reportmax = 1;
		    } else if (unstrcmp(argv[index], "extract") ||
			     unstrcmp(argv[index], "e")) {
			extractfs = 1;
			index++;
			if (index == argc)
				perr("extract requires the filesystem DosType");
			if (*argv[index] == '\'')
				extractid = chars_to_long(argv[index] + 1);
			else
				sscanf(argv[index], "%i", &extractid);
			if (extractid == 0)
				perr("invalid extract DosType %s specified", argv[index]);
			printf("extract id %s = %08X '", argv[index], extractid);
			print_id(extractid);
			printf("'\n");
		    } else if (unstrcmp(argv[index], "help") ||
			     unstrcmp(argv[index], "h")) {
			print_help();
		    } else if (unstrcmp(argv[index], "newpart") ||
			     unstrcmp(argv[index], "n")) {
			if (editpart)
				perr("The part command is implied by newpart");
			if (createpart)
				perr("You may only create one partition at a time");
			if (promote)
				perr("only one partition may be modified at a time");
			createpart = 1;
			index++;
			if (index == argc)
				perr("newpart requires the new partition name");
			strcpy(partname, argv[index]);
		    } else if (unstrcmp(argv[index], "part") ||
			     unstrcmp(argv[index], "p")) {
			if (editpart)
				perr("only one partition may be modified at a time");
			if (createpart)
				perr("The part command is implied by newpart");
			if (promote)
				perr("only one partition may be modified at a time");
			editpart = 1;
			index++;
			if (index == argc)
				perr("part requires the partition name");
			strcpy(partname, argv[index]);
		    } else if (unstrcmp(argv[index], "promote") ||
			     unstrcmp(argv[index], "pr")) {
			if (editpart || createpart)
				perr("only one partition may be modified at a time");
			if (promote)
				perr("you may only promote one partition");
			promote = 1;
			index++;
			if (index == argc)
				perr("promote requires the bootable partition name");
			strcpy(partname, argv[index]);
		    } else if (unstrcmp(argv[index], "rigid") ||
			     unstrcmp(argv[index], "r")) {
			editrigid = 1;
		    } else if (unstrcmp(argv[index], "seglist") ||
			     unstrcmp(argv[index], "s")) {
			showseglist = 1;
		    } else
			perr("Unknown parameter -%s", argv[index]);
		} else {
		    if (equalspos = strchr(argv[index], '='))
			*equalspos = '\0';

		    for (cur = 0; accept[cur].name; cur++) {
			if (unstrcmp(accept[cur].name, argv[index])) {
			    if (equalspos)
				argv[index] = equalspos + 1;
			    else
				index++;
			    printf("%s = %s\n", accept[cur].name, argv[index]);
			    if (accept[cur].type == STR) /* it's a string */
				switch(accept[cur].which) {
				    case PART:
					strcpy(((ULONG *) &newpart) +
						accept[cur].offset, argv[index]);
					break;
				    case RIGID:
					strcpy(((ULONG *) &newrigid) +
						accept[cur].offset, argv[index]);
					break;
				}
			    else if (*argv[index] == '\'')  /* check for 'XXXX' */
				switch(accept[cur].which) {
				    case PART:
					((ULONG *) &newpart)[accept[cur].offset] =
						   chars_to_long(argv[index] + 1);
					break;
				    case RIGID:
					((ULONG *) &newrigid)[accept[cur].offset] =
						   chars_to_long(argv[index] + 1);
					break;
				}
			    else /* scan number */
				switch(accept[cur].which) {
				    case PART:
					sscanf(argv[index], "%i", ((ULONG *)
					       &newpart) + accept[cur].offset);
					break;
				    case RIGID:
					sscanf(argv[index], "%i", ((ULONG *)
					       &newrigid) + accept[cur].offset);
					break;
				}
			    accept[cur].changed = 1;
			    break;
			}
		    }

		    if (accept[cur].name)
			continue;

		    switch(argnum) {
			case 0:
			    disk_device = argv[index];
			    if (!strchr(disk_device, '.'))
				perr("Invalid device %s", argv[index]);
			    break;
			case 1:
			    if (!sscanf(argv[index], "%i", &disk_unit))
				perr("Invalid unit %s", argv[index]);
			    break;
			case 2:
			    if (!sscanf(argv[index], "%i", &disk_flags))
				perr("Invalid flags %s", argv[index]);
			    break;
			default:
			    perr("Unknown parameter %s", argv[index]);
		    }
		    argnum++;
		}
	}

	if (argnum == 0)
		print_usage();

	if (open_device())
		exit(1);
	onbreak(break_abort);
	buffer = (char *) malloc(TD_SECTOR * 5);
	if (buffer == NULL) {
		fprintf(stderr, "Unable to allocate %d bytes\n", TD_SECTOR);
		close_device();
		exit(1);
	}
	rdb   = (struct RigidDiskBlock *)     (buffer + TD_SECTOR * 0);
	part  = (struct PartitionBlock *)     (buffer + TD_SECTOR * 1);
	fs    = (struct FileSysHeaderBlock *) (buffer + TD_SECTOR * 2);
	seg   = (struct LoadSegBlock *)       (buffer + TD_SECTOR * 3);
	bad   = (struct BadBlockBlock *)      (buffer + TD_SECTOR * 4);
	ppart = (struct PartitionBlock *)     (buffer + TD_SECTOR * 4);
	/* Note that "ppart" shares the same RDB position as "bad" */

	for (index = 0; index < RDB_LOCATION_LIMIT; index++) {
		blk_read(rdb, index);
		if (rdb->rdb_ID == IDNAME_RIGIDDISK)
			break;
	}

	if (index < RDB_LOCATION_LIMIT) {
		rdb_blk = index;
		print_rdb_info();
		print_bad_blocks();
		print_partition_info();
		print_filesystem_info();
	} else
		fprintf(stderr, "No RDB label found on %s unit %d\n",
			disk_device, disk_unit);

	if (reportmax)
		printf("Next block available=%d\n", maxaccess + 1);

	if (index < RDB_LOCATION_LIMIT)
		do_creations();

	free(buffer);
	close_device();
}

open_device()
{
	/* create port to use when talking with handler */
	devport = CreatePort(NULL, 0);
	if (devport)
		trackIO = (struct IOExtTD *)
			CreateExtIO(devport, sizeof(struct IOExtTD));
	else {
		trackIO = NULL;
		DeletePort(devport);
	}

	if (trackIO == NULL) {
		fprintf(stderr, "Failed to create trackIO structure\n");
		return(1);
	}

	if (OpenDevice(disk_device, disk_unit, (struct IORequest *)trackIO,
			disk_flags)) {
		fprintf(stderr, "fatal: Unable to open %s unit %d.\n",
			disk_device, disk_unit);
		DeletePort(devport);
		DeleteExtIO((struct IORequest *)trackIO);
		return(1);
	}

	return(0);
}


close_device()
{
	if (trackIO)
		CloseDevice((struct IORequest *)trackIO);
	if (devport)
		DeletePort(devport);
	if (trackIO)
		DeleteExtIO((struct IORequest *)trackIO);
}

long
blk_read(void *buf, ULONG blk)
{
	if (blk > maxaccess)
		maxaccess = blk;

	trackIO->iotd_Req.io_Command = CMD_READ;	/* read data */
	trackIO->iotd_Req.io_Length = TD_SECTOR;	/* one sector */
	trackIO->iotd_Req.io_Offset = TD_SECTOR * blk;	/* sector addr */
	trackIO->iotd_Req.io_Data = buf;		/* memory addr */
	return (DoIO((struct IORequest *)trackIO));
}

long
blk_write(void *buf, ULONG blk)
{
	if (blk > maxaccess)
		maxaccess = blk;

	trackIO->iotd_Req.io_Command = CMD_WRITE;	/* write data */
	trackIO->iotd_Req.io_Length = TD_SECTOR;	/* one sector */
	trackIO->iotd_Req.io_Offset = TD_SECTOR * blk;	/* sector addr */
	trackIO->iotd_Req.io_Data = buf;		/* memory addr */
	return (DoIO((struct IORequest *)trackIO));
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


print_usage()
{
	int index;
	int len;
	int cpos;
	int which = RIGID;

	len = strlen(progname) + 8;
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "Usage: %s [options] {disk.device} [unit] [flags]\n", progname);
	fprintf(stderr, "%*s-rigid = edit fields in the Rigid Disk Block\n", len, "");
	fprintf(stderr, "%*s-part NAME = modify partition table parameters of NAME\n", len, "");
	fprintf(stderr, "%*s-newpart NAME = add new partition NAME to the RDB\n", len, "");
	fprintf(stderr, "%*s-promote NAME = make partition NAME first to be mounted\n", len, "");
	fprintf(stderr, "%*s-extract DOSTYPE = extract filesystem to a file \n", len, "");
	fprintf(stderr, "%*s-avail = report next available block in RDB\n", len, "");
	fprintf(stderr, "%*s-seglist = show seglist addresses for filesystems\n", len, "");
/*
	fprintf(stderr, "%*s-v = turn on verbose mode\n", len, "");
*/
	fprintf(stderr, "%*s-help = give more help\n", len, "");
	fprintf(stderr, "%*sOther keywords:\n", len, "");
	cpos = len + 1;
	fprintf(stderr, "%*s", cpos, "");
	for (index = 0; accept[index].name; index++) {
		cpos += strlen(accept[index].name) + 1;
		if ((cpos > 79) || (which != accept[index].which)) {
			fprintf(stderr, "\n%*s", len + 1, "");
			cpos = len + strlen(accept[index].name) + 2;
			which = accept[index].which;
		}
		fprintf(stderr, " %s", accept[index].name);
	}
	fprintf(stderr, "\n");
	exit(1);
}

print_help()
{
	fprintf(stderr, "%s is a program which will display and allow you to modify\n", progname);
	fprintf(stderr, "select parameters present in a disk device's Rigid Disk Block\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "MAJOR WARNING: This program can be quite hazardous if used improperly\n");
	fprintf(stderr, "               There are no idiot safeguards when modifying partition\n");
	fprintf(stderr, "               information.  Know what you are doing.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "For the RDB, the values of Flags are as follows:\n");
	fprintf(stderr, "	1 = no disks after this on this controller\n");
	fprintf(stderr, "	2 = no LUNs after this at this SCSI target\n");
	fprintf(stderr, "	4 = no Target IDs after this on this SCSI bus\n");
	fprintf(stderr, "	8 = don't bother trying reselection on this device\n");
	fprintf(stderr, "       16 = rdb_Disk identification is valid\n");
	fprintf(stderr, "       32 = rdb_Controller identification is valid\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "For partitions, the values of Flags are as follows:\n");
	fprintf(stderr, "	1 = Device is Bootable\n");
	fprintf(stderr, "	2 = Do not Mount device on startup\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Examples of usage:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "I use this program only for insane uses; no example will help you.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "More help will be available in the future.\n");
	exit(1);
}

void break_abort()
{
	static int entered = 0;

	if (entered++)
		return;

	fprintf(stderr, "^C\n");

	if (buffer != NULL)
		free(buffer);
	close_device();

	exit(1);
}

print_rdb_info()
{
	autofix = globalfix;

	if (chksum(rdb) && dofix(rdb_blk, rdb, IDNAME_RIGIDDISK))
		return(1);
	printf("%s unit %d : ", disk_device, disk_unit);
	printf(" Blk=%d HostID=%d BlockBytes=%d Flags=0x%04x\n",
		rdb_blk, rdb->rdb_HostID, rdb->rdb_BlockBytes, rdb->rdb_Flags);

	printf("  ");
	if (rdb->rdb_Flags & RDBFF_LAST)
		printf("LastDisk ");
	if (rdb->rdb_Flags & RDBFF_LASTLUN)
		printf("LastLUN ");
	if (rdb->rdb_Flags & RDBFF_LASTTID)
		printf("LastTarget ");
	if (rdb->rdb_Flags & RDBFF_NORESELECT)
		printf("NoReselect ");
	if (rdb->rdb_Flags & RDBFF_DISKID)
		printf("DiskValid ");
	if (rdb->rdb_Flags & RDBFF_CTRLRID)
		printf("ControllerValid ");
	if (rdb->rdb_Flags)
		printf("\n  ");

	printf("BadBlockList=%d PartitionList=%d FileSysHeaderList=%d DriveInit=%d\n",
		rdb->rdb_BadBlockList, rdb->rdb_PartitionList,
		rdb->rdb_FileSysHeaderList, rdb->rdb_DriveInit);
	printf("  Cylinders=%d Sectors=%d Heads=%d Interleave=%d Park=%d StepRate=%d\n",
		rdb->rdb_Cylinders, rdb->rdb_Sectors, rdb->rdb_Heads,
		rdb->rdb_Interleave, rdb->rdb_Park, rdb->rdb_StepRate);
	printf("  WritePreComp=%d ReducedWrite=%d RDBBlocksLo=%d RDBBlocksHi=%d\n",
		rdb->rdb_WritePreComp, rdb->rdb_ReducedWrite,
		rdb->rdb_RDBBlocksLo, rdb->rdb_RDBBlocksHi);
	printf("  LoCylinder=%d HiCylinder=%d CylBlocks=%d AutoParkSeconds=%d\n",
		rdb->rdb_LoCylinder, rdb->rdb_HiCylinder, rdb->rdb_CylBlocks,
		rdb->rdb_AutoParkSeconds);
	printf("  DiskVendor=%.8s DiskProduct=%.16s DiskRevision=%.4s\n",
		rdb->rdb_DiskVendor, rdb->rdb_DiskProduct, rdb->rdb_DiskRevision);
	printf("  ControllerVendor=%.8s ControllerProduct=%.16s ControllerRev=%.4s\n",
		rdb->rdb_ControllerVendor, rdb->rdb_ControllerProduct,
		rdb->rdb_ControllerRevision);
	printf("\n");
	rigid_operation();
}

print_partition_info()
{
	int parent = -1;
	int partition;
	struct DosEnvec *env;

	autofix = globalfix;
	if ((partition = rdb->rdb_PartitionList) == -1) {
		printf("No partitions\n");
		return(1);
	}

	while (partition != -1) {
		if (blk_read(part, partition)) {
			fprintf(stderr, "partition block %d could not be read\n",
				partition);
			return(1);
		}

		if (part->pb_ID != IDNAME_PARTITION) {
			fprintf(stderr, "block %d does not contain a valid partition!\n", partition);
			return(1);
		}

		if (chksum(part) && dofix(part, partition, IDNAME_PARTITION))
			return(1);

		print_id(part->pb_ID);
		printf("  Blk=%-3d ", partition);

		if (part->pb_DriveName[0] < 32) {
			bstr = 1;
			printf("DriveName=%-4.*s", part->pb_DriveName[0],
				part->pb_DriveName + 1);
		} else
			printf("DriveName=%-4.32s", part->pb_DriveName);

		if (createpart) {
		    if ((bstr && strncmp(part->pb_DriveName + 1,
					 partname, part->pb_DriveName[0])) ||
			(!bstr && !strcmp(part->pb_DriveName, partname))) {
			    createpart = 0;
			    fprintf(stderr, "Error, partition %s already exists\n",
				    partname);
			}
		}
		printf(" DevFlags=%-4d HostID=%d Next=%-3d %s%s\n",
			part->pb_DevFlags, part->pb_HostID,
			part->pb_Next, (part->pb_Flags &
			PBFF_NOMOUNT) ? "      " : "Mount ",
			(part->pb_Flags & PBFF_BOOTABLE) ? "Boot" : "");
		env = (struct DosEnvec *) part->pb_Environment;
		printf("      Surfaces=%-3d BlocksPerTrack=%-3d Reserved=%08x PreAlloc=%08x\n",
			env->de_Surfaces, env->de_BlocksPerTrack,
			env->de_Reserved, env->de_PreAlloc);
		printf("      Interleave=%d LowCyl=%-5d HighCyl=%-5d NumBuffers=%-3d BufMemType=%d\n",
			env->de_Interleave, env->de_LowCyl, env->de_HighCyl,
			env->de_NumBuffers, env->de_BufMemType);
		printf("      MaxTransfer=%08x Mask=%08x BootPri=%-2d DosType=%08X '",
			env->de_MaxTransfer, env->de_Mask,
			env->de_BootPri, env->de_DosType);
		print_id(env->de_DosType);
		printf("'\n");

		partition_operation(partition, parent);
		parent = partition;
		partition = part->pb_Next;
	}
	printf("\n");
	return(0);
}

print_filesystem_info()
{
	int filesystem;

	autofix = globalfix;
	if ((filesystem = rdb->rdb_FileSysHeaderList) == -1) {
		printf("No filesystems\n");
		return(1);
	}

	while (filesystem != -1) {
		if (blk_read(fs, filesystem)) {
			fprintf(stderr, "filesystem block %d could not be read\n",
				filesystem);
			return(1);
		}

		if (fs->fhb_ID != IDNAME_FILESYSHEADER) {
			fprintf(stderr, "block %d does not contain a valid filesystem!\n", filesystem);
			return(1);
		}

		if (chksum(fs) && dofix(fs, filesystem, IDNAME_FILESYSHEADER))
			return(1);

		print_id(fs->fhb_ID);
		printf("  Blk=%-3d HostID=%d Next=%-3d Version=%-7d DosType=%08X '",
			filesystem, fs->fhb_HostID, fs->fhb_Next,
			fs->fhb_Version, fs->fhb_DosType);
		print_id(fs->fhb_DosType);
		printf("'\n      ");
#define COND_PRINT(POS, NAME)						\
	if (fs->fhb_PatchFlags & (1 << POS))				\
		printf("%s=%d ", NAME, *(&fs->fhb_Type + POS))

		COND_PRINT(0, "Type");
		COND_PRINT(1, "Task");
		COND_PRINT(2, "Lock");
		COND_PRINT(3, "Handler");
		COND_PRINT(4, "StackSize");
		COND_PRINT(5, "Priority");
		COND_PRINT(6, "Startup");
		COND_PRINT(7, "SegListBlocks");
		COND_PRINT(8, "GlobVec");
		if (showseglist)
			print_seglist(fs->fhb_SegListBlocks);
		else
			printf("\n");

		filesystem_operation(filesystem);

		filesystem = fs->fhb_Next;
	}
	printf("\n");
	return(0);
}

ULONG chars_to_long(str)
unsigned char *str;
{
	int pos;
	ULONG value = 0;
	unsigned char ch;
	int index;

	for (index = 0; index < 4; index++) {
		if (*str == '\\') {
			str++;
			ch = 0;
			pos = 0;
			while (isdigit(*str)) {
				ch = (ch * 10 + (*str) - '0');
				str++;
				pos++;
				if (pos == 3)
					break;
			}
		} else if ((*str == '\0') || (*str == '\'')) {
			fprintf(stderr, "DosType should be four characters\n");
			return(0);
		} else {
			ch = *str;
			str++;
		}
		value <<= 8;
		value += ch;
	}

	return(value);
}

print_id(name)
ULONG name;
{
	unsigned char ch;
	int index;

	for (index = 0; index < 4; index++) {
		ch = (name >> (8 * (3 - index))) & 255;
		if ((ch < 32) || (ch > 'Z'))
			printf("\\%d", ch);
		else
			printf("%c", ch);
	}
}

print_seglist(seglist)
int seglist;
{
	int index = 0;

	autofix = globalfix;
	if (seglist == -1) {
		printf("      No seglist\n");
		return(1);
	}
	while (seglist != -1) {
		if ((index % 11) == 0) {
			printf("\n");
			printf("      ");
			index = 0;
		}
		index++;
		if (blk_read(seg, seglist)) {
			fprintf(stderr, "\nloadseg block %d could not be read\n",
				seglist);
			return(1);
		}
		if (seg->lsb_ID != IDNAME_LOADSEG) {
			fprintf(stderr, "block %d does not contain a valid loadseg block!\n", seglist);
			return(1);
		}
		if (chksum(seg) && dofix(seg, seglist, IDNAME_LOADSEG))
			return(1);
		else
			printf("%5d ", seglist);
		seglist = seg->lsb_Next;
	}
	printf("\n");
}

print_bad_blocks()
{
	int badblock;
	int index;

	autofix = globalfix;
	if ((badblock = rdb->rdb_BadBlockList) == -1) {
		printf("No bad blocks\n\n");
		return(1);
	}

	while (badblock != -1) {
		if (blk_read(bad, badblock)) {
			fprintf(stderr, "badblock block %d could not be read\n",
				badblock);
			return(1);
		}

		if (bad->bbb_ID != IDNAME_BADBLOCK) {
			fprintf(stderr, "block %d does not contain a valid badblock!\n", badblock);
			return(1);
		}

		if (chksum(bad) && dofix(bad, badblock, IDNAME_BADBLOCK))
			return(1);

		print_id(bad->bbb_ID);
		printf("  Blk=%-3d HostID=%d Next=%d Reserved=%d",
			badblock, bad->bbb_HostID, bad->bbb_Next, bad->bbb_Reserved);
		for (index = 0; index < (bad->bbb_SummedLongs - 6) / 2; index++) {
			if (index % 4)
				printf("\n      ");
			printf("%6d->%-6d  ",
			       bad->bbb_BlockPairs[index].bbe_BadBlock,
			       bad->bbb_BlockPairs[index].bbe_BadBlock);
		}
		printf("\n");

		badblock = bad->bbb_Next;
	}
	printf("\n");
}

LONG
chksum(void *ptr)
{
	LONG size;
	LONG csum = 0;
	LONG *buf = (LONG *) ptr;

	size = buf[1];
	if (size > (TD_SECTOR >> 2)) {
		fprintf(stderr, "Invalid size of csum structure\n");
		return(1);
	}
	for (; size > 0; size--) {
		csum += *buf;
		buf++;
	}
	return(csum);
}

int
fixsum(void *ptr, ULONG blk)
{
	LONG *temp;
	LONG size;
	LONG csum = 0;
	LONG *buf = (LONG *) ptr;

	temp = buf;
	size = buf[1];
	buf[2] = 0;

	if (size > (TD_SECTOR >> 2))
		return(1);

	for (; size > 0; size--) {
		csum += *temp;
		temp++;
	}

	buf[2] = -csum;

	if (blk_write(buf, blk)) {
		fprintf(stderr, "fixed checksum could not be written!\n");
		return(1);
	}
	return (0);
}

getans()
{
	char ch;
	int ret = 0;

	while ((ch = getchar()) != EOF)
		if ((ch == 'n') || (ch == 'N'))
			break;
		else if ((ch == 'q') || (ch == 'Q')) {
			ret = 3;
			break;
		} else if ((ch == 'y') || (ch == 'Y')) {
			ret = 1;
			break;
		} else if ((ch == 'a') || (ch == 'A')) {
			ret = 2;
			break;
		}

	if (ch == EOF)
		return(0);

	while ((ch = getchar()) != '\n')
		if (ch == EOF)
			return(0);
	return(ret);
}

int
dofix(void *buf, ULONG blk, ULONG name)
{
	if (autofix) {
		printf("fixing block %d\n", blk);
		if (fixsum(buf, blk))
			return(1);
	} else {
		printf("\n");
		print_id(name);
		printf(": Block %d invalid, fix? (Yes/No/All/Quit)\n", blk);
		switch(getans()) {
		    case NO:
			break;
		    case QUIT:
			return(1);
		    case ALL:
			autofix = 1;
		    case YES:
			if (fixsum(buf, blk))
				return(1);
		}
	}
	return(0);
}

rigid_operation()
{
	char	*name;
	char	*newname;
	int	index;
	int	changeall = 0;
	int	count = 0;

	for (index = 0; accept[index].name; index++)
	    if (accept[index].changed && accept[index].which == RIGID)
		count++;

	if (count == 0)
		return;

	if (!editrigid) {
		fprintf(stderr, "You must specify -rigid to edit RDB fields\n\n");
		return;
	}

	count = 0;
	for (index = 0; accept[index].name; index++)
	    if (accept[index].changed && accept[index].which == RIGID) {
		printf("change %s from", accept[index].name);

		if (accept[index].type == STR) { /* it's a string */
		    name = (char *) &((ULONG *) rdb)[accept[index].offset];
		    newname = (char *) &((ULONG *) &newrigid)[accept[index].offset];

		    if (bstr)
			printf(" %.*s to %s?\n", name[0], name + 1, newname);
		    else
			printf(" %s to %s?\n", name, newname);
		} else				/* it's a number */
			printf(" %d to %d?\n",
				((ULONG *)     rdb)[accept[index].offset],
				((ULONG *) &newrigid)[accept[index].offset]);
		if (changeall)
		    goto dochange;

		switch(getans()) {
		    case NO:
			continue;
		    case QUIT:
			return;
		    case ALL:
			changeall = 1;
		    case YES:
			dochange:
			count++;
			if (accept[index].type == STR) { /* it's a string */
			    if (bstr) {
				strcpy(name + 1, newname);
				name[0] = strlen(newname);
			    } else	/* cursed IVS */
				strcpy(name, newname);
			} else {			/* it's a number */
			    ((ULONG *) rdb)[accept[index].offset] =
				  ((ULONG *) &newrigid)[accept[index].offset];
			}
		}
	    }

	if (count)
	    if (fixsum(rdb, rdb_blk))
		return(1);

	printf("\n");
}

partition_operation(partition, parent)
int partition;
int parent;
{
	int	index;
	char	*name;
	char	*newname;
	int	changeall = 0;
	int	count = 0;

	if (!editpart && !promote)
		return;

	name = part->pb_DriveName;
	if (*name < 32) {		/* It's a BSTR */
		if (strncmp(name + 1, partname, name[0]))
			return;
		if (strlen(partname) > name[0])
			return;
		bstr = 1;
	} else				/* It's an IVS screwup  */
		if (strcmp(name, partname))
			return;
	printf("found partition %s\n", partname);

	for (index = 0; accept[index].name; index++)
	    if (accept[index].changed && accept[index].which == PART) {
		printf("change %s from", accept[index].name);

		if (accept[index].type == STR) {	/* it's a string */
		    name = (char *) &((ULONG *) part)[accept[index].offset];
		    newname = (char *) &((ULONG *) &newpart)[accept[index].offset];

		    if (bstr)
			printf(" %.*s to %s?\n", name[0], name + 1, newname);
		    else
			printf(" %s to %s?\n", name, newname);
		} else {				/* it's a number */
			printf(" %d to %d?\n",
				((ULONG *)     part)[accept[index].offset],
				((ULONG *) &newpart)[accept[index].offset]);
		}
		if (changeall)
		    goto dochange;

		switch(getans()) {
		    case NO:
			continue;
		    case QUIT:
			return;
		    case ALL:
			changeall = 1;
		    case YES:
			dochange:
			count++;
			if (accept[index].type == STR) {/* it's a string */
			    if (bstr) {
				strcpy(name + 1, newname);
				name[0] = strlen(newname);
			    } else	/* cursed IVS */
				strcpy(name, newname);
			} else {			/* it's a number */
			    ((ULONG *) part)[accept[index].offset] =
				  ((ULONG *) &newpart)[accept[index].offset];
			}
		}
	    }

	if (promote) {
	    if (!(part->pb_Flags & PBFF_BOOTABLE))
		fprintf(stderr, "This partition may not be promoted unless the Bootable bit is set\n");
	    else if (partition == rdb->rdb_PartitionList)
		fprintf(stderr, "This partition is already mounted first\n");
	    else {
		/* non-corrupting scheme:
			1. Point parent of this partition's Next to oldchild
			2. Point this partition's Next to newchild
			3. Point rdb at this partition
		*/
		printf("promote partition?\n");
		switch(getans()) {
		    case NO:
			return(0);
		    case QUIT:
			return(1);
		}

		/* 1. Point parent of this partition's Next to oldchild */
		if (parent == -1) {
		    fprintf(stderr, "%s internal error, aborting changes\n");
		    return(1);
		} else {
		    if (blk_read(ppart, parent)) {
			fprintf(stderr, "partition block %d could not be read\n",
				parent);
			return(1);
		    }
		    ppart->pb_Next = part->pb_Next;
		    if (fixsum(ppart, parent))
			return(1);
		}

		/* 2. Point this partition's Next to newchild */
		part->pb_Next = rdb->rdb_PartitionList;
		if (fixsum(part, partition))
		    return(1);

		/* 3. Point rdb at this partition */
		rdb->rdb_PartitionList = partition;
		if (fixsum(rdb, rdb_blk))
		    return(1);

		count = 0;
	    }
	}

	if (count)
	    if (fixsum(part, partition))
		return(1);
}

filesystem_operation(filesystem)
int filesystem;
{
	char name[32];

	if (!extractfs)
		return;

	if (fs->fhb_DosType != extractid)
		return;

	printf("\nExtract this filesystem (%08x '", fs->fhb_DosType);
	print_id(fs->fhb_DosType);
	printf("') ?  (Yes/No)\n");
	switch(getans()) {
	    case NO:
	    case QUIT:
		return;
	}

	sprintf(name, "%08X", fs->fhb_DosType);
	extract_seglist(fs->fhb_SegListBlocks, name);
}

extract_seglist(seglist, name)
int seglist;
char *name;
{
	FILE *fp;
	struct LoadSegBlock *start = NULL;
	struct LoadSegBlock *current = NULL;
	struct LoadSegBlock *parent = NULL;
	int checkbad = 1;

	if (seglist == -1) {
		fprintf(stderr, "No seglist to extract for %s\n", name);
		return;
	}

	while (seglist != -1) {
		if (blk_read(seg, seglist)) {
			fprintf(stderr, "\nloadseg block %d could not be read\n",
				seglist);
			freelist(start);
			return(1);
		}
		if (checkbad && (seg->lsb_ID != IDNAME_LOADSEG)) {
			fprintf(stderr, "block %d does not contain a valid loadseg block!\n", seglist);
			printf("attempt to continue? (Yes/No/All)\n");
			switch(getans()) {
			    case NO:
				goto write_data;
			    case QUIT:
				freelist(start);
				return;
			    case ALL:
				checkbad = 0;
			}
		}
		current = (struct LoadSegBlock *) malloc(TD_SECTOR);
		if (current == NULL) {
			fprintf(stderr, "Unable to allocate memory buffer\n");
			freelist(start);
			return(1);
		}
		memcpy(current, seg, TD_SECTOR);
		current->lsb_Next = (ULONG) NULL;

		if (parent == NULL)
			start = current;
		else
			parent->lsb_Next = (ULONG) current;

		parent = current;

		seglist = seg->lsb_Next;
	}

	write_data:
	fp = fopen(name, "w");
	if (fp == NULL) {
		fprintf(stderr, "Unable to open output file %s\n", name);
		return;
	}

	current = start;
	while (current != NULL) {
		fwrite(current->lsb_LoadData,
		       (current->lsb_SummedLongs - 5) * 4, 1, fp);
		current = (struct LoadSegBlock *) current->lsb_Next;
	}

	fclose(fp);
	sleep(1);
	freelist(start);
}

freelist(current)
struct LoadSegBlock *current;
{
	struct LoadSegBlock *temp;
	while (current != NULL) {
		temp = (struct LoadSegBlock *) current->lsb_Next;
		free(current);
		current = temp;
	}
}

create_partition()
{
	int newblk;
	int previous = -1;
	int current;

	printf("Create a New partition with the device name %s? (Yes/No)\n",
		partname);
	switch(getans()) {
	    case QUIT:
	    case NO:
		return;
	}

/*	Add partition to the end of list of partitions so that
 *	 AmigaDOS won't try to boot from it because it's first
 */
	/* first create the new partition block */
	newblk = maxaccess + 1;
	newpart.pb_Next = -1;
	if (bstr) {
		strcpy(newpart.pb_DriveName + 1, partname);
		newpart.pb_DriveName[0] = strlen(partname);
	} else
		strcpy(newpart.pb_DriveName, partname);
	memcpy(part, &newpart, TD_SECTOR);
	fixsum(part, newblk);


	current = rdb->rdb_PartitionList;
	while (current != -1) {
		if (blk_read(part, current)) {
			fprintf(stderr, "partition block %d could not be read\n",
				current);
			return(1);
		}

		if (part->pb_ID != IDNAME_PARTITION) {
			fprintf(stderr, "block %d does not contain a valid partition!\n", current);
			return(1);
		}

		if (chksum(part) && dofix(part, current, IDNAME_PARTITION))
			return(1);

		previous = current;
		current = part->pb_Next;
	}

	if (previous == -1) {	/* then modify RDB (first partition in RDB) */
		rdb->rdb_PartitionList = newblk;
		fixsum(rdb, rdb_blk);
	} else {
		part->pb_Next = newblk;
		fixsum(part, previous);
	}
}

do_creations()
{
	if (rdb->rdb_ID != IDNAME_RIGIDDISK)
		return;

	if (createpart)
		create_partition();
}

perr(s1, s2)
char *s1;
ULONG s2;
{
	fprintf(stderr, s1, s2);
	fprintf(stderr, "\n");
	exit(1);
}
