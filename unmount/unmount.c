/* unmount.c version 1.0
 *      This program is Copyright 2017 Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

char *version = "\0$VER: unmount 1.0 © 17-Oct-2017 Chris Hooper";

#include <stdio.h>
#include <sys/dir.h>

#include </ucbinclude/sys/stat.h>    /* chmod, mknod, mkfifo */
#include </ucbinclude/ufs/dinode.h>  /* inode info */

#define DEVICES_TIMER_H
#define EXEC_TYPES_H

#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <exec/memory.h>

char *progname;

static int
Action_Die(const char *name)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    struct Lock           *dlock;
    char                   buf[512];

    msgport = (struct MsgPort *) DeviceProc(name, NULL);
    if (msgport == NULL)
        return(1);

    dlock = (struct Lock *) CurrentDir(NULL);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return(1);
    }

    packet = (struct StandardPacket *)
         AllocMem(sizeof(struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return(1);
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
	fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2);
	err = 1;
    }

    FreeMem(packet, sizeof(struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

static void
print_usage()
{
    fprintf(stderr,
            "%s\n"
            "usage:  %s <device>\n"
            "        This program will send an ACTION_DIE packet to the\n"
            "        specified device, which for most filesystems will\n"
            "        quiesce all operation.\n",
            version + 8, progname);
    exit(1);
}

int
main(int argc, char *argv[])
{
    int arg;

    progname = argv[0];
    if (argc < 2)
	print_usage();

    for (arg = 1; arg < argc; arg++)
	Action_Die(argv[arg]);

    exit(0);
}
