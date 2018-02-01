#include <stdio.h>
#include <string.h>
#include <dos/dosextens.h>
#include <fcntl.h>
#include <stdlib.h>

#define _SYS_TIME_H_
#include <exec/types.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/filehandler.h>
#include <exec/memory.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>

typedef long daddr_t;

#include <file_blockio.h>

#undef DEBUG

#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

#define TD_READ64     24
#define TD_WRITE64    25
#define TD_SEEK64     26
#define TD_FORMAT64   27


char         *dos_dev_name     = NULL;
static char   disk_device_buf[100];
char         *disk_device      = disk_device_buf;
int           disk_unit        = 0;
static int    disk_flags       = 0;
static int    device_direct    = 0;
int           DEV_BSIZE        = 512;
int           DEV_BSHIFT       = 9;
static int    inhibited        = 0;
static ULONG  psectoffset      = 0; /* Start sector for partition */
static ULONG  psectmax         = 0; /* One sector beyond partition end */
static struct IOExtTD *trackIO = NULL;
static int    use_td64         = -1;

static int dio_init(void);
int dio_inhibit(int inhibit);
void error_exit(int rc);

int
bread(char *buf, daddr_t blk, long size)
{
	ULONG sectoff = blk + psectoffset;
	ULONG high32 = sectoff >> (32 - DEV_BSHIFT);
#ifdef DEBUG
	printf("bread: blk=%d size=%d BSIZE=%d buf=%08x poffset=%u loc=%u\n",
		blk, size, DEV_BSIZE, buf, psectoffset, sectoff);
#endif
	if (dos_dev_name == NULL)
		return (fbread(buf, blk, size));

	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to use */
	trackIO->iotd_Req.io_Offset  = sectoff << DEV_BSHIFT;
	if (high32) {
	    if (use_td64 == 0) {
failed_io64:
		printf("Can't address blk %u with 32-bit only device\n",
		       sectoff);
		return (1);
	    }
	    trackIO->iotd_Req.io_Command = TD_READ64;	/* set IO Command */
            trackIO->iotd_Req.io_Actual  = high32;
            /* io_Actual has upper 32 bits of byte offset */
	} else {
	    trackIO->iotd_Req.io_Command = CMD_READ;	/* set IO Command */
	}

	if (sectoff + (size + DEV_BSIZE - 1) / DEV_BSIZE > psectmax) {
	    printf("Attempted to read past the end of the partition: "
		   "block=%d size=%d\n", blk, size);
	    return (1);
	}

	if (DoIO((struct IORequest *) trackIO)) {
            if (high32 && use_td64 == -1) {
                use_td64 = 0;
                return (bread(buf, blk, size));
            }
	    return (1);
	}
        if (high32 && use_td64 == -1) {
#ifdef DEBUG
            printf("Used TD64 for addr %x:%08x\n",
		   high32, trackIO->iotd_Req.io_Offset);
#endif
            use_td64 = 1;
        }
	return (0);
}

int
bwrite(char *buf, daddr_t blk, long size)
{
	ULONG sectoff = blk + psectoffset;
	ULONG high32 = sectoff >> (32 - DEV_BSHIFT);
#ifdef DEBUG
	printf("bwrite: blk=%d size=%d BSIZE=%d buf=%08x loc=%u\n",
		blk, size, DEV_BSIZE, buf, sectoff);
#endif
	if (dos_dev_name == NULL) {
	    if (fbwrite(buf, blk, size))
		error_exit(1);
	    return (0);
	}

	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to use */
	trackIO->iotd_Req.io_Offset  = sectoff << DEV_BSHIFT;
	if (high32) {
	    if (use_td64 == 0) {
		printf("Can't address blk %u with 32-bit only device\n",
		       sectoff);
		error_exit(1);
	    }
	    trackIO->iotd_Req.io_Command = TD_WRITE64;	/* set IO Command */
            trackIO->iotd_Req.io_Actual  = high32;
            /* io_Actual has upper 32 bits of byte offset */
	} else {
	    trackIO->iotd_Req.io_Command = CMD_WRITE;	/* set IO Command */
	}

	if (sectoff + (size + DEV_BSIZE - 1) / DEV_BSIZE > psectmax) {
	    printf("Attempted to write past the end of the partition: "
		   "block=%d size=%d\n", blk, size);
	    error_exit(1);
	}

	if (DoIO((struct IORequest *) trackIO)) {
            if (high32 && use_td64 == -1) {
                /* Turn off TD64 and try again */
                use_td64 = 0;
                return (bwrite(buf, blk, size));
            }
	    error_exit(1);
	}
	return (0);
}

static void
dio_set_bsize(int bsize)
{
	int	tempshift = 9;
	switch (bsize) {
	    case 16384:	tempshift++;
	    case 8192:	tempshift++;
	    case 4096:	tempshift++;
	    case 2048:	tempshift++;
	    case 1024:	tempshift++;
	    case 512:
		DEV_BSIZE  = bsize;
		DEV_BSHIFT = tempshift;
		break;
	    default:
		fprintf(stderr, "Warning: physical sector size %d ignored\n",
			bsize);
	}
}

void
dio_assign_bsize(int bsize)
{
	dio_set_bsize(bsize);
}

int
dio_open(char *name)
{
	if (dos_dev_name != NULL)
		return (1);

	dos_dev_name = name;

	if (dio_init()) {
		dos_dev_name = NULL;
		return (1);
	}

	if (!(trackIO = (struct IOExtTD *) CreateExtIO(CreatePort(0, 0),
					       sizeof (struct IOExtTD)) )) {
		fprintf(stderr, "fatal: Failed to create trackIO structure.\n");
		exit(1);
	}

	if (OpenDevice(disk_device, disk_unit, (struct IORequest *)trackIO,
			disk_flags)) {
		fprintf(stderr, "Fatal: Unable to open %s unit %d\n",
			disk_device, disk_unit);
		DeletePort(trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO((struct IORequest *)trackIO);
		exit(1);
	}

	return (0);
}

int
dio_close(void)
{
	if (dos_dev_name == NULL)
		return (0);

	while (inhibited)
		dio_inhibit(0);

	if (trackIO != NULL) {
		trackIO->iotd_Req.io_Command    = TD_MOTOR;
		trackIO->iotd_Req.io_Flags      = 0x0;  /* Motor status */
		trackIO->iotd_Req.io_Length     = 0;    /* 0=off, 1=on */
		DoIO((struct IORequest *)trackIO);

		CloseDevice((struct IORequest *)trackIO);
		DeletePort(trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO((struct IORequest *)trackIO);
		trackIO = NULL;
	}
	dos_dev_name = NULL;
	return (0);

}


static int
dio_init(void)
{
    char              *devname;
    char              *lookfor;
    char              *pos;
    int                notfound = 1;
    int                tempsize;

    lookfor = dos_dev_name;
    if (((pos = strchr(lookfor, ':')) == NULL) || (pos[1] != '\0')) {
	int e = 0;
	/* Check for device driver and LUN */
	if (strstr(lookfor, ".device,") == NULL)
	    return (1);
	disk_device = disk_device_buf;
	strcpy(disk_device, "scsi.device");
	disk_unit = 0;
	disk_flags = 0;
	DEV_BSIZE = 512;
	DEV_BSHIFT = 9;
	psectoffset = 0;
	psectmax = -1;
	if (sscanf(lookfor, "%[^,],%n%i%n,%n%i%n,%n%i%n,%n%i%n,%n%i%n",
		   disk_device, &e, &disk_unit, &e, &e, &disk_flags, &e, &e,
		   &DEV_BSIZE, &e, &e, &psectoffset, &e, &e, psectmax) < 1) {
	    fprintf(stderr, "Bad device name or unit number\n");
bad_device_name:
	    fprintf(stderr, "Specify <diskdriver.device>,<lun>"
			"[,<flags>,<sectsize>,<sectstart>,<sectend>]\n"
		    "Example: scsi.device,2 - "
			"use C= SCSI driver, LUN 2, entire disk\n"
		    "Example: scsi.device,2,0,512,20480,65536 - "
			"use 32MB partition starting at 10MB\n");
	    return (1);
	}
	if (lookfor[e] != '\0') {
	    printf("Failed to scan %s\n"
		   "%*s^\n", lookfor, e + 15, "");
	    goto bad_device_name;
	}
	dio_set_bsize(DEV_BSIZE);
	device_direct = 1;
    } else {
	/* Device name terminated with a colon */
	struct FileSysStartupMsg *startup;
	struct DosLibrary *dosbase;
	struct RootNode   *rootnode;
	struct DosInfo    *dosinfo;
	struct DevInfo    *devinfo;
	struct DosEnvec   *envec;
	struct MsgPort    *task = NULL;
	*pos = '\0';

	dosbase  = (struct DosLibrary *) OpenLibrary("dos.library", 0L);
	rootnode = dosbase->dl_Root;
	dosinfo  = (struct DosInfo *) BTOC(rootnode->rn_Info);

	Forbid();
	    devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

	    while (devinfo != NULL) {
		devname = (char *) BTOC(devinfo->dvi_Name);
		if (stricmp(devname + 1, lookfor) == 0) {
		    if (devinfo->dvi_Type == DLT_DEVICE) {
			notfound = 0;
			break;
		    }
		    if (devinfo->dvi_Type == DLT_VOLUME) {
			/* Need to look up device which matches this volume */
			task = devinfo->dvi_Task;
			notfound = 2;
			break;
		    }
		    printf("Device %s is not of the necessary type\n",
			   devname + 1);
		    notfound = 2;
		}
		devinfo = (struct DevInfo *) BTOC(devinfo->dvi_Next);
	    }
	Permit();

	if (notfound == 2) {
	    /*
	     * Do another pass looking for a Device with the same task
	     * as the volume which was specified.
	     */
	    Forbid();
		devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

		while (devinfo != NULL) {
		    if ((devinfo->dvi_Type == DLT_DEVICE) &&
		        (devinfo->dvi_Task == task)) {
			devname = (char *) BTOC(devinfo->dvi_Name);
			sprintf(dos_dev_name, "%s:", devname + 1);
			notfound = 0;
			break;
		    }
		    devinfo = (struct DevInfo *) BTOC(devinfo->dvi_Next);
		}
	    Permit();
	}
	if (notfound) {
		if (notfound == 1)
		    fprintf(stderr, "%s: is not mounted.\n", lookfor);
		CloseLibrary((struct Library *)dosbase);
		exit(1);
	}

	startup	= (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
	strcpy(disk_device, ((char *) BTOC(startup->fssm_Device)) + 1);
	disk_unit	= startup->fssm_Unit;
	disk_flags	= startup->fssm_Flags;

	envec		= (struct DosEnvec *) BTOC(startup->fssm_Environ);

	tempsize	= envec->de_SizeBlock * sizeof (long);
	psectoffset	= envec->de_LowCyl * envec->de_Surfaces *
			  envec->de_BlocksPerTrack;
	psectmax		= (envec->de_HighCyl + 1) * envec->de_Surfaces *
			  envec->de_BlocksPerTrack;
	CloseLibrary((struct Library *)dosbase);

	dio_set_bsize(tempsize);

#ifdef DEBUG
	printf("low=%d\n", envec->de_LowCyl);
	printf("high=%d\n", envec->de_HighCyl);
	printf("blks/track=%d\n", envec->de_BlocksPerTrack);
	printf("heads=%d\n", envec->de_Surfaces);
	printf("DosType=%08x\n", envec->de_DosType);
#endif
    }
#ifdef DEBUG
    printf("device=%s\n", disk_device);
    printf("unit=%d\n", disk_unit);
    printf("flags=%d\n", disk_flags);
    printf("poffset=%u\n", psectoffset);
    printf("pmax=%u\n", psectmax);
#endif

    return (0);
}


int
dio_checkstack(int minimum)
{
	int ssize;

	if (Cli())
		ssize = ((struct CommandLineInterface *) Cli())->cli_DefaultStack * 4;
	else
		ssize = ((struct Process *) FindTask(NULL))->pr_StackSize;

	if (ssize < minimum) {
		fprintf(stderr, "fatal: this program requires %d bytes of stack;\n",
			minimum);
		fprintf(stderr, "       it was given only %d bytes.\n", ssize);
		return (1);
	} else
		return (0);
}


int
dio_inhibit(int inhibit)
{
	struct MsgPort	      *handler;
	struct MsgPort        *replyport;
	struct StandardPacket *packet;
	char   buf[64];
	char   *name = dos_dev_name;
	int    len;

	if (dos_dev_name == NULL)
		return (0);

	if (device_direct)
		return (0);  /* Going direct to device (no filesystem) */

	if (trackIO == NULL)
		return (0);

	len = strlen(name);
        if (name[len - 1] != ':') {
                strcpy(buf, name);
                buf[len] = ':';
                buf[len + 1] = '\0';
                name = buf;
        }
	handler = (struct MsgPort *) DeviceProc(name);
	if (handler == NULL) {
	    if (inhibit)
		fprintf(stderr, "** Unable to inhibit handler for %s\n", name);
	    return (1);
	}

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport)
		return (1);

	packet = (struct StandardPacket *)
		 AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

	if (!packet) {
		DeletePort(replyport);
		return (1);
	}

	packet->sp_Msg.mn_Node.ln_Name = (char *)&(packet->sp_Pkt);
	packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
	packet->sp_Pkt.dp_Port         = replyport;
	packet->sp_Pkt.dp_Type         = ACTION_INHIBIT;
	packet->sp_Pkt.dp_Arg1         = inhibit;

	PutMsg(handler, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	FreeMem(packet, sizeof (struct StandardPacket));
	DeletePort(replyport);

	if (inhibit)
	    inhibited++;
	else
	    inhibited--;
	return (0);
}
