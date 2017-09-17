/* chown.c version 1.0
 *      This program is copyright (1994) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */
#include <stdio.h>
#include <dos/dos.h>
#include </dos30/dosextens.h>
#include <exec/memory.h>

#define ulong unsigned long
#define CTOB(x) ((x)>>2)

char *progname;

main(argc, argv)
int  argc;
char *argv[];
{
	int	index;
	ulong	type;
	ulong	owner;
	ulong	group;

	progname = argv[0];

	if (argc < 3)
		print_usage();

	index = 1;
	if (sscanf(argv[index], "%d.%d", &owner, &group) != 2) {
		fprintf(stderr, "Invalid owner.group in %s\n", argv[index]);
		exit(1);
	}

	if (!isdigit(*argv[index]) || (owner < 0) || (owner > 65535)) {
		fprintf(stderr, "Invalid owner in %s\n", argv[index]);
		print_usage();
	}

	if ((group < 0) || (group > 65535)) {
		fprintf(stderr, "Invalid group in %s\n", argv[index]);
		print_usage();
	}

	for (index++;  index < argc; index++)
	    if (SetOwner(argv[index], (owner << 16) | group))
		fprintf(stderr, "Unable to set owner/group of %s\n", argv[index]);

	exit(0);
}

print_usage()
{
	fprintf(stderr, "usage:  %s owner.group filename [filename filename ...]\n",
		progname);
	fprintf(stderr, "        owner is a integer from 0 to 65535\n");
	fprintf(stderr, "        group is a integer from 0 to 65535 and is not optional\n");
	fprintf(stderr, "        filename is the file to own\n");
	exit(1);
}

int SetOwner(name, owner)
char	*name;
ulong	owner;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*lock;
	char			buf[512];

	msgport = (struct Process *) DeviceProc(name, NULL);
	if (msgport == NULL)
		return(1);

	lock = (struct Lock *) CurrentDir(NULL);
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
	packet->sp_Pkt.dp_Arg1         = NULL;
	packet->sp_Pkt.dp_Arg2         = lock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = owner;

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
