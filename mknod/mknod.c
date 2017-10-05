/* mknod.c version 1.1
 *	This program is copyright (1994, 1996) Chris Hooper.  All code
 *	herein is freeware.  No portion of this code may be sold for profit.
 */
#include <stdio.h>
#include <dos/dos.h>
#include </dos30/dosextens.h>
#include <exec/memory.h>

#define TYPE_BLOCK	0
#define TYPE_CHARACTER	1

#define ulong unsigned long
#define CTOB(x) ((x)>>2)

char *version = "\0$VER: mknod 1.1 (17.Aug.96) © 1996 Chris Hooper";
char *progname;

main(argc, argv)
int  argc;
char *argv[];
{
	int	index;
	ulong	type;
	ulong	major;
	ulong	minor;

	progname = argv[0];

	if (argc < 5)
		print_usage();

	index = 1;
	if (!strcmp(argv[index], "c"))
		type = TYPE_CHARACTER;
	else if (!strcmp(argv[index], "b"))
		type = TYPE_BLOCK;
	else {
		fprintf(stderr, "Invalid device %s\n", argv[index]);
		print_usage();
	}

	index++;
	major = atoi(argv[index]);
	if (!isdigit(*argv[index]) || (major < 0) || (major > 16777215)) {
		fprintf(stderr, "Invalid major number %s\n", argv[index]);
		print_usage();
	}

	index++;
	minor = atoi(argv[index]);
	if (!isdigit(*argv[index]) || (minor < 0) || (minor > 255)) {
		fprintf(stderr, "Invalid minor number %s\n", argv[index]);
		print_usage();
	}

	for (index++;  index < argc; index++)
	    if (MakeNode(argv[index], type, (major << 8) | minor))
		fprintf(stderr, "Unable to create node %s\n", argv[index]);

	exit(0);
}

print_usage()
{
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "usage:  %s type major minor devname [devname devname ...]\n",
		progname);
	fprintf(stderr, "        type is b (block) or c (character)\n");
	fprintf(stderr, "        major is device number (0-16777215)\n");
	fprintf(stderr, "        minor is minor device number (0-255)\n");
	fprintf(stderr, "        devname is name to call device node\n");
	exit(1);
}

int MakeNode(name, type, device)
char	*name;
ulong	type;
ulong	device;
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
	packet->sp_Pkt.dp_Type         = ACTION_CREATE_OBJECT;
	packet->sp_Pkt.dp_Arg1         = lock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);
	packet->sp_Pkt.dp_Arg3         = (type ? ST_CDEVICE : ST_BDEVICE);
	packet->sp_Pkt.dp_Arg4         = device;

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
