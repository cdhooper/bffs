#include <stdio.h>
#include <dos/dosextens.h>
#define _SYS_TIME_H_
#include <exec/types.h>
#include "sys/types.h"
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/filehandler.h>
#include <exec/memory.h>

#include <fcntl.h>

char	*strchr();
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

char	*dos_dev_name = NULL;
extern	int	dev_bsize;

char	*disk_device	= "scsi.device";
int	disk_unit	= 0;
int	disk_flags	= 0;

int	inhibited = 0;
ULONG	poffset	= 0;
ULONG	pmax	= 0;
struct	IOExtTD	*trackIO = NULL;

int bread(buf, blk, size)
char	*buf;
daddr_t	blk;
long	size;
{
/*
	printf("bread: blk=%d size=%d bs=%d buf=%08x poffset=%d\n", blk, size, dev_bsize, buf, poffset);
*/
	if (dos_dev_name == NULL)
		return(fbread(buf, blk, size));

	trackIO->iotd_Req.io_Command = CMD_READ;	/* set IO Command */
	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to use */

	trackIO->iotd_Req.io_Offset = blk * dev_bsize + poffset;

	if (trackIO->iotd_Req.io_Offset + size > pmax) {
		printf("Attempted to read past the end of the partition, block=%d size=%d\n", blk, size);
		return(1);
	}

	return(DoIO(trackIO));
}

int bwrite(buf, blk, size)
char	*buf;
daddr_t	blk;
long	size;
{
/*
	printf("bwrite: blk=%d size=%d bs=%d buf=%08x poffset=%d\n", blk, size, dev_bsize, buf, poffset);
*/
	if (dos_dev_name == NULL)
		return(fbwrite(buf, blk, size));

	trackIO->iotd_Req.io_Command = CMD_WRITE;	/* set IO Command */
	trackIO->iotd_Req.io_Length  = size;		/* #bytes to read */
	trackIO->iotd_Req.io_Data    = buf;		/* buffer to use */

	trackIO->iotd_Req.io_Offset = blk * dev_bsize + poffset;

	if (trackIO->iotd_Req.io_Offset + size > pmax) {
		printf("Attempted to write past the end of the partition, block=%d size=%d\n", blk, size);
		return(1);
	}

	return(DoIO(trackIO));
}

int dio_open(name)
char *name;
{
	if (dos_dev_name != NULL)
		return(1);

	dos_dev_name = name;

	if (dio_init())
		return(1);

	if (!(trackIO = (struct IOExtTD *) CreateExtIO(CreatePort(0, 0),
					       sizeof(struct IOExtTD)) )) {
		fprintf(stderr, "fatal: Failed to create trackIO structure.\n");
		exit(1);
	}

	if (OpenDevice(disk_device, disk_unit, trackIO, disk_flags)) {
		fprintf(stderr, "Fatal: Unable to open %s unit %d.\n",
			disk_device, disk_unit);
		DeletePort(trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO(trackIO);
		exit(1);
	}
	return(0);
}

dio_close()
{
	if (dos_dev_name == NULL)
		return(0);

	if (inhibited)
		dio_inhibit(0);

	if (trackIO) {
		trackIO->iotd_Req.io_Command    = TD_MOTOR;
		trackIO->iotd_Req.io_Flags      = 0x0;  /* Motor status */
		trackIO->iotd_Req.io_Length     = 0;    /* 0=off, 1=on */
		DoIO(trackIO);

		CloseDevice(trackIO);
		DeletePort(trackIO->iotd_Req.io_Message.mn_ReplyPort);
		DeleteExtIO(trackIO);
		trackIO = NULL;
	}
	dos_dev_name = NULL;
	return(0);

}


int dio_init()
{
	struct	DosLibrary *DosBase;
	struct	RootNode *rootnode;
	struct	DosInfo *dosinfo;
	struct	DevInfo *devinfo;
	struct	FileSysStartupMsg *startup;
	struct	DosEnvec *envec;
	char	*devname;
	char	*lookfor;
	char	*pos;
	int	notfound = 1;

	lookfor = dos_dev_name;
	if ((pos = strchr(lookfor, ':')) != NULL)
		*pos = '\0';

	DosBase = (struct DosLibrary *) OpenLibrary("dos.library", 0L);

	rootnode= DosBase->dl_Root;
	dosinfo = (struct DosInfo *) BTOC(rootnode->rn_Info);
	devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

	while (devinfo != NULL) {
		devname	= (char *) BTOC(devinfo->dvi_Name);
		if (unstrcmp(devname+1, lookfor) &&
		    (devinfo->dvi_Type == DLT_DEVICE)) {
			notfound = 0;
			break;
		}
		devinfo	= (struct DevInfo *) BTOC(devinfo->dvi_Next);
	}

	if (notfound) {
		fprintf(stderr, "%s: is not mounted.\n", lookfor);
		CloseLibrary(DosBase);
		exit(1);
	}

	startup	= (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
	disk_device	= ((char *) BTOC(startup->fssm_Device)) + 1;
	disk_unit	= startup->fssm_Unit;
	disk_flags	= startup->fssm_Flags;

	envec		= (struct DosEnvec *) BTOC(startup->fssm_Environ);
	poffset		= envec->de_LowCyl * envec->de_Surfaces *
			  envec->de_BlocksPerTrack * TD_SECTOR;
	pmax		= (envec->de_HighCyl + 1) * envec->de_Surfaces *
			  envec->de_BlocksPerTrack * TD_SECTOR;
/*
	printf("pmax=%d TD=%d \n", pmax, TD_SECTOR);
	printf("unit=%d\n", disk_unit);
	printf("device=%s\n", disk_device);
	printf("flags=%d\n", disk_flags);
	printf("poffset=%d\n", poffset);

	printf("low=%d\n", envec->de_LowCyl);
	printf("high=%d\n", envec->de_HighCyl);
	printf("blks/track=%d\n", envec->de_BlocksPerTrack);
	printf("heads=%d\n", envec->de_Surfaces);
	printf("DosType=%08x\n", envec->de_DosType);
*/

	CloseLibrary(DosBase);
}

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


/*
dio_checkstack(minimum)
int minimum;
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
		return(1);
	} else
		return(0);
}
*/

dio_inhibit(inhibit)
int inhibit;
{
	struct MsgPort	      *handler;
	struct MsgPort        *replyport;
	struct StandardPacket *packet;
	char   buf[64];
	char   *name = dos_dev_name;
	int    len;

	if (dos_dev_name == NULL)
		return(0);

	if (trackIO == NULL)
		return(0);

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
	    return(1);
	}

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport)
		return(1);

	packet = (struct StandardPacket *)
		 AllocMem(sizeof(struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

	if (!packet) {
		DeletePort(replyport);
		return(1);
	}

	packet->sp_Msg.mn_Node.ln_Name = (char *)&(packet->sp_Pkt);
	packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
	packet->sp_Pkt.dp_Port         = replyport;
	packet->sp_Pkt.dp_Type         = ACTION_INHIBIT;
	packet->sp_Pkt.dp_Arg1         = inhibit;

	PutMsg(handler, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	FreeMem(packet, sizeof(struct StandardPacket));
	DeletePort(replyport);

	inhibited = inhibit;
	return(0);
}
