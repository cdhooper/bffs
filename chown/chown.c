/* chown
 *      This program is copyright (1994-2018) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: chown 1.2 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#ifdef AMIGA
#include <dos/dos.h>
#include </dos30/dosextens.h>
#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/dos_protos.h>
#define CTOB(x) ((x)>>2)

#else /* !AMIGA */
#include <unistd.h>
#endif

#define ulong unsigned long

static void
print_usage(const char *progname)
{
	fprintf(stderr,
		"%s\n"
		"usage:  %s owner.group filename [filename filename ...]\n"
		"        owner is an integer from 0 to 65535\n"
		"        group is an integer from 0 to 65535 and is not optional\n"
		"        filename is the file to change\n",
		version + 7, progname);
	exit(1);
}

#ifdef AMIGA
static int
chown(const char *name, ulong owner, ulong group)
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	BPTR			lock;
	char			buf[512];
	ulong			ownergroup = (owner << 16) | group;

	msgport = (struct MsgPort *) DeviceProc(name);
	if (msgport == NULL)
		return(1);

	lock = CurrentDir(0);
	CurrentDir(lock);

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
	packet->sp_Pkt.dp_Type         = ACTION_SET_OWNER;
	packet->sp_Pkt.dp_Arg1         = (LONG) NULL;
	packet->sp_Pkt.dp_Arg2         = lock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = ownergroup;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		err = 1;
		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2);
        }

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}
#endif

int
main(int argc, char *argv[])
{
	int	index;
	ulong	owner;
	ulong	group;

	if (argc < 3)
		print_usage(argv[0]);

	index = 1;
	if (sscanf(argv[index], "%d.%d", &owner, &group) != 2) {
		fprintf(stderr, "Invalid owner.group in %s\n", argv[index]);
		exit(1);
	}

	if (!isdigit(*argv[index]) || (owner < 0) || (owner > 65535)) {
		fprintf(stderr, "Invalid owner in %s\n", argv[index]);
		print_usage(argv[0]);
	}

	if ((group < 0) || (group > 65535)) {
		fprintf(stderr, "Invalid group in %s\n", argv[index]);
		print_usage(argv[0]);
	}

	for (index++;  index < argc; index++)
	    if (chown(argv[index], owner, group))
		fprintf(stderr, "Unable to set owner/group of %s\n", argv[index]);

	exit(0);
}
