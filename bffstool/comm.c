/* comm.c  (BFFStool)
 *      This program is copyright (1993 - 2017) Chris Hooper.  All code
 *      herein is freeware.  No portion of this code may be sold for profit.
 */

#include <stdio.h>
#include <string.h>
#include <dos/dosextens.h>
#include <libraries/dosextens.h>
#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/alib_protos.h>
#include <time.h>
#include "bffs_dosextens.h"
#include "comm.h"


struct MsgPort		 *fs = NULL;
struct MsgPort		 *tempfs = NULL;
extern struct fs	 *superblock;
struct cache_set	 *cache_stack;
extern struct DosLibrary *DOSBase;
extern char		 *filesystems[];

#define ID_BFFS_DISK    (0x42464653L)  /* 'BFFS' filesystem */
#define BTOC(x)	((x)<<2)

int
open_handler(char *name)
{
	char buf[64];
	int len;

/* dummy, this is for GetDeviceProc only!!
	if (fs)
		FreeDeviceProc(fs);
*/

	len = strlen(name);
	if (name[len - 1] != ':') {
		strcpy(buf, name);
		buf[len] = ':';
		buf[len + 1] = '\0';
		name = buf;
	}

	fs = DeviceProc(name);

	if (fs == NULL)
		return(1);

	return(0);
}

void
close_handler(void)
{
/* dummy, this is for GetDeviceProc only!!
	FreeDeviceProc(fs);
*/
	fs = NULL;
}

struct stat *
get_stat(void)
{
	struct MsgPort	      *replyport;
	struct StandardPacket *packet;
	struct stat	      *stat;

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport)
		return(NULL);

	packet = (struct StandardPacket *)
		 AllocMem(sizeof(struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

	if (!packet) {
		DeletePort(replyport);
		return(NULL);
	}

	packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
	packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
	packet->sp_Pkt.dp_Port         = replyport;
	packet->sp_Pkt.dp_Type         = ACTION_FS_STATS;

	PutMsg(fs, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSTRUE) {
		stat = (struct stat *) packet->sp_Pkt.dp_Res2;
		superblock  = (struct fs *) stat->superblock;
		cache_stack = (struct cache_set *)
				*((ULONG *) stat->cache_head);
	} else {
		fprintf(stderr, "Handler does not support FS_STATS\n");
		stat = NULL;
	}

	FreeMem(packet, sizeof(struct StandardPacket));
	DeletePort(replyport);

	return(stat);
}

int
sync_filesystem(void)
{
	struct MsgPort	      *replyport;
	struct StandardPacket *packet;

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
	packet->sp_Pkt.dp_Type         = ACTION_FLUSH;

	PutMsg(fs, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	FreeMem(packet, sizeof(struct StandardPacket));
	DeletePort(replyport);

	return(0);
}

int
get_filesystems(char *name)
{
	struct	DosInfo	   *info;
	struct	DeviceList *temp;
	int	index = 0;
	int	ret = -1;

	if ((name == NULL) || (name[0] == '\0'))
	    tempfs = NULL;
	else {
	    if (!strchr(name, ':'))
		strcat(name, ":");
	    tempfs = DeviceProc(name);
	}

	Forbid();

	info = (struct DosInfo *)
		    BTOC( ((struct RootNode *) DOSBase->dl_Root)->rn_Info);

	temp = (struct DeviceList *) BTOC(info->di_DevInfo);
	while (temp != NULL) {
	    if ((temp->dl_Type == DLT_VOLUME) &&
		(temp->dl_DiskType == ID_BFFS_DISK)) {
		filesystems[index] = ((char *) BTOC(temp->dl_Name)) + 1;
		if (tempfs && (tempfs == temp->dl_Task))
			ret = index;
		index++;
	    }
	    temp = (struct DeviceList *) BTOC(temp->dl_Next);
	}

	Permit();

	filesystems[index] = NULL;

/* dummy, this is for GetDeviceProc only!!
	if (tempfs)
		FreeDeviceProc(tempfs);
*/

	return(ret);
}
