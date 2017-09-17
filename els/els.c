/* els.c version 1.0
 *      This program is copyright (1994) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

/*----------------------------------------------------------------------*/
/*  Extended ls - shows user/group and appropriate UNIXish permissions  */
/*		  per AmigaDOS 3.0 extended FileInfoBlock information	*/
/*									*/
/*	By Chris Hooper (cdh@mtu.edu)  16-Jan-94			*/
/*									*/
/* Files "env:sys/passwd" and "env:sys/group" contain			*/
/*		name::id:::::						*/
/* Where all fields except name and id are ignored.			*/
/*									*/
/*----------------------------------------------------------------------*/


#include <stdio.h>
#include </dos30/dos.h>
#include </dos30/dosextens.h>
#include <dos/datetime.h>
#include <exec/memory.h>

#define ulong unsigned long
#define CTOB(x) ((x)>>2)



int sflag = 0;
int lflag = 1;
int gflag = 1;
int fflag = 2;

char *progname;
int initial = 1;

FILE *fp;
pfile_notread = 1;
gfile_notread = 1;
pfile_good = 0;
gfile_good = 0;
struct node {
	ulong id;
	char *name;
};

struct node *passwd[2048];
struct node *group[2048];
passwd_entries = 0;
group_entries = 0;


main(argc, argv)
int argc;
char *argv[];
{
	int index;

	progname = argv[0];

	if (argc == 1)
		listname("");

	for (index = 1; index < argc; index++)
		listname(argv[index]);

	exit(0);
}

listname(name)
char *name;
{
	ulong lock;
	struct FileInfoBlock *fib;

	lock = Lock(name, ACCESS_READ);
	if (lock == NULL) {
		fprintf(stderr, "Error locating %s\n", name);
		return(1);
	}

	fib = (struct FileInfoBlock *)
			AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC);
	fib->fib_OwnerUID = 0L;
	fib->fib_OwnerGID = 0L;
	if (!Examine(lock, fib))
		printf("Error examining %s\n", name);
	else if (print_fib(fib))
		print_fibnames(lock, fib);

	FreeMem(fib, sizeof(struct FileInfoBlock));
	UnLock(lock);
}

print_fib(fib)
struct FileInfoBlock *fib;
{
	int ret = 0;
	ulong p;

	putchar('\r');

	if (sflag)
		printf("%4d ", fib->fib_NumBlocks);

	switch(fib->fib_DirEntryType) {
	    case ST_ROOT:
		ret = 1;
		if (initial)
			return(ret);
		putchar('r');
		break;
	    case ST_USERDIR:
		ret = 1;
		if (initial)
			return(ret);
		putchar('d');
		break;
	    case ST_SOFTLINK:
		if (initial)
			return(ret);
		putchar('s');
		break;
	    case ST_LINKDIR:
		ret = 1;
		if (initial)
			return(ret);
		putchar('l');
		break;
	    case ST_FILE:
		putchar('-');
		break;
	    case ST_LINKFILE:
		putchar('=');
		break;
	    case ST_BDEVICE:
		putchar('b');
		break;
	    case ST_CDEVICE:
		putchar('c');
		break;
	    case ST_FIFO:
		putchar('f');
		break;
	    case ST_SOCKET:
		putchar('s');
		break;
	    default:
		ret = 1;
		putchar('?');
		break;
	}

	p = fib->fib_Protection;
	if (p & FIBF_ARCHIVE)
		if (p & FIBF_HIDDEN)
			putchar('A');
		else
			putchar('a');
	else
		if (p & FIBF_HIDDEN)
			putchar('h');
		else
			putchar('-');

	if (p & FIBF_SCRIPT)
		if (p & FIBF_PURE)
			putchar('S');
		else
			putchar('s');
	else
		if (p & FIBF_PURE)
			putchar('p');
		else
			putchar('-');
	print_perms(p);
	print_perms(p >> FIBB_GRP_DELETE);
	print_perms(p >> FIBB_OTR_DELETE);
	putchar(' ');

	print_owner((ulong) fib->fib_OwnerUID);
	if (gflag) {
		putchar(' ');
		print_group((ulong) fib->fib_OwnerGID);
	}
	printf("%8d ", fib->fib_Size);

	print_daytime(&fib->fib_Date);

 	printf("%.108s%c%.80s\n", fib->fib_FileName,
		((ret && fflag) ? '/' : ' '), fib->fib_Comment);

	return(ret);
}

print_fibnames(plock, pfib)
ulong plock;
struct FileInfoBlock *pfib;
{
	ulong lock;
	struct FileInfoBlock *fib;

	initial = 0;
	while (ExNext(plock, pfib))
		print_fib(pfib);
}

print_perms(p)
ulong p;
{
	if (!(p & FIBF_READ))
		putchar('r');
	else
		putchar('-');
	if (!(p & FIBF_WRITE))
		if (!(p & FIBF_DELETE))
			putchar('w');
		else
			putchar('W');
	else
		if (!(p & FIBF_DELETE))
			putchar('=');
		else
			putchar('-');
	if (!(p & FIBF_EXECUTE))
		putchar('x');
	else
		putchar('-');
}

print_daytime(dstamp)
struct DateStamp *dstamp;
{
	char datebuf[32];
	char timebuf[32];
	struct DateTime dtime;
	struct DateStamp todaystamp;

	dtime.dat_Stamp.ds_Days   = dstamp->ds_Days;
	dtime.dat_Stamp.ds_Minute = dstamp->ds_Minute;
	dtime.dat_Stamp.ds_Tick   = dstamp->ds_Tick;
	dtime.dat_Format	  = FORMAT_DOS;
	dtime.dat_Flags		  = 0x0;
	dtime.dat_StrDay	  = NULL;
	dtime.dat_StrDate	  = datebuf;
	dtime.dat_StrTime	  = timebuf;
	DateToStr(&dtime);
	DateStamp(&todaystamp);

	if (datebuf[0] == '0')		/* remove date leading zero */
		datebuf[0] = ' ';

	/* If it's over about nine months old, show year and no time */
	if (datebuf[0] == '-')
		printf("             ");
	else if ((dtime.dat_Stamp.ds_Days + 274 > todaystamp.ds_Days) &&
		 (dtime.dat_Stamp.ds_Days < todaystamp.ds_Days + 91))
		printf("%-3.3s %2.2s %5.5s ", datebuf + 3,
			datebuf, timebuf);
	else
		printf("%-3.3s %2.2s  %d%.2s ", datebuf + 3, datebuf,
			(datebuf[7] > '6') ? 19 : 20, datebuf + 7);
}

print_usage()
{
	fprintf(stderr, "usage:  %s filename [filename filename ...]\n",
		progname);
	fprintf(stderr, "        filename is the file to show\n");
	exit(1);
}

print_owner(uid)
ulong uid;
{
	char *ptr;
	char buf[32];

	switch (uid) {
		case 0:
			ptr = "root";	break;
		case 1:
			ptr = "daemon";	break;
		case 2:
			ptr = "sys";	break;
		case 3:
			ptr = "bin";	break;
		case 4:
			ptr = "uucp";	break;
		case 5:
			ptr = "operator"; break;
		case 6:
			ptr = "news";	break;
		case 7:
			ptr = "ingres";	break;
		case 9:
			ptr = "audit";	break;
		case 65534:
			ptr = "nobody";	break;
		case 1640:
		case 3232:
			ptr = "cdh";	break;
		default:
			ptr = buf;
			sprintf(buf, "%-7d", uid);
	}
	printf("%-8s", ptr);
}


print_group(gid)
ulong gid;
{
	char *ptr;
	char buf[10];

	if (gfile_notread) {
		gfile_good = 0;
		gfile_notread = 0;
	}

	if (gfile_good) {
		return;
	}

	switch (gid) {
		case 0:
			ptr = "wheel";	break;
		case 1:
			ptr = "daemon";	break;
		case 2:
			ptr = "kmem";	break;
		case 3:
			ptr = "bin";	break;
		case 4:
			ptr = "tty";	break;
		case 5:
			ptr = "operator"; break;
		case 6:
			ptr = "news";	break;
		case 8:
			ptr = "uucp";	break;
		case 9:
			ptr = "audit";	break;
		case 10:
			ptr = "staff";	break;
		case 20:
			ptr = "other";	break;
		case 65534:
			ptr = "nogroup"; break;
		default:
			ptr = buf;
			sprintf(buf, "%-8d", gid);
	}
	printf("%-8s", ptr);
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
