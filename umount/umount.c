/* umount
 *      This program is Copyright 2018 Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: umount 1.0 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>

#include </ucbinclude/sys/stat.h>    /* chmod, mknod, mkfifo */
#include </ucbinclude/ufs/dinode.h>  /* inode info */

#define DEVICES_TIMER_H
#define EXEC_TYPES_H

#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <exec/memory.h>
#include <clib/dos_protos.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>

#define TRUE  1
#define FALSE 0

static int do_dismount = 1;
static int quiet = 0;

static int
device_exists(const char *name)
{
    struct DosList *doslist;
    char           *pos;
    char            buf[512];
    int             len = strlen(name);

    if (len >= sizeof (buf) - 1)  {
	if (quiet < 3)
	    fprintf(stderr, "\"%s\" is invalid\n", name);
        return (0);
    }

    strcpy(buf, name);
    pos = strchr(buf, ':');
    if ((pos == NULL) || (pos[1] != '\0'))  {
	if (quiet < 3)
	    fprintf(stderr, "\"%s\" is invalid - "
		    "device name must end with colon\n", name);
        return (0);
    }
    *pos = '\0';

    if ((doslist = LockDosList(LDF_DEVICES | LDF_READ)) == NULL) {
	if (quiet < 3)
	    fprintf(stderr, "Failed to lock DOS List\n");
        return (0);
    }

    if (FindDosEntry(doslist, buf, LDF_DEVICES) == NULL) {
	UnLockDosList(LDF_DEVICES | LDF_READ);
	if (quiet < 3)
	    fprintf(stderr, "Failed to locate %s for die\n", name);
        return (0);
    }

    UnLockDosList(LDF_DEVICES | LDF_READ);
    return (1);
}

static int
device_dismount(const char *name)
{
    struct DosList *doslist;
    struct DosList *entry;
    char           *pos;
    char            buf[512];
    int             len = strlen(name);
    int	            removed = 0;
    int	            failed = 0;

    if (len >= sizeof (buf) - 1)  {
	fprintf(stderr, "\"%s\" is invalid\n", name);
        return (0);
    }

    strcpy(buf, name);
    pos = strchr(buf, ':');
    if ((pos == NULL) || (pos[1] != '\0'))  {
	if (quiet < 3)
	    fprintf(stderr, "\"%s\" is invalid - "
		    "device name must end with colon\n", name);
        return (0);
    }
    *pos = '\0';

    doslist = LockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_WRITE);
    if (doslist == NULL) {
	if (quiet < 3)
	    fprintf(stderr, "Failed to lock DOS List\n");
        return (0);
    }

    while ((entry = FindDosEntry(doslist, buf, LDF_DEVICES)) != NULL) {
	if (RemDosEntry(entry)) {
	    removed++;
	} else {
	    if (failed++ > 10)
		break;
	}
    }
    UnLockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_WRITE);

    if ((removed == 0) && (failed == 0)) {
	if (quiet < 2)
	    fprintf(stderr, "Failed to locate %s for dismount\n", name);
        return (0);
    }

    if ((quiet < 1) && (removed))
	printf("Dismounted %s\n", name);
    if ((quiet < 2) && (removed == 0))
	printf("Failed to dismount %s\n", name);
    return (1);
}


static int
Action_Die(const char *name)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    char                   buf[512];

    if (device_exists(name) == FALSE)
	return (1);

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL) {
	if (quiet < 3)
	    fprintf(stderr, "Failed to open MessagePort of %s\n", name);
        return (1);
    }

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
	if (quiet < 3)
	    fprintf(stderr, "Failed to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
         AllocMem(sizeof(struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
	if (quiet < 3)
	    fprintf(stderr, "Failed to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_DIE;
    packet->sp_Pkt.dp_Arg1         = 0;
    packet->sp_Pkt.dp_Arg2         = 0;
    packet->sp_Pkt.dp_Arg3         = 0;
    packet->sp_Pkt.dp_Arg4         = 0;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	if (quiet < 2)
	    fprintf(stderr, "DIE failed for %s: %d\n",
		    name, packet->sp_Pkt.dp_Res2);
	err = 1;
    }

    FreeMem(packet, sizeof(struct StandardPacket));
    DeletePort(replyport);

    if (quiet < 1)
	printf("DIE sent to %s\n", name);

    return (err);
}

static void
print_usage(const char *progname)
{
    fprintf(stderr,
            "%s\n"
            "usage:  %s [-dq] <device>\n"
            "        This program sends ACTION_DIE to the specified device,"
		" which for most\n"
	    "        filesystems will quiesce all operation.\n"
	    "        -d  don't dismount the device\n",
	    "        -q  quiet (don't display on success)\n",
            version + 8, progname);
    exit(1);
}

int
main(int argc, char *argv[])
{
    int arg;
    int rc = 0;

    if (argc < 2)
	print_usage(argv[0]);

    for (arg = 1; arg < argc; arg++) {
	const char *name = argv[arg];
	if (*name == '-') {
	    while (*(++name) != '\0') {
		if (*name == 'd')
		    do_dismount = !do_dismount;
		else if (*name == 'q')
		    quiet++;
		else
		    print_usage(argv[0]);
	    }
	}
    }

    for (arg = 1; arg < argc; arg++) {
	const char *name = argv[arg];
	if (*name != '-') {
	    rc |= Action_Die(name);
	    if (do_dismount)
		rc |= device_dismount(name);
	}
    }

    exit(rc);
}
