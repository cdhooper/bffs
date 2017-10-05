/* els.c version 1.1
 *      This program is copyright (1994,1996) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

char *version = "\0$VER: els 1.1 (15.Aug.96) © 1996 Chris Hooper";


/*----------------------------------------------------------------------*/
/*  Extended ls - shows user/group and appropriate UNIXish permissions  */
/*		  per AmigaDOS 3.0 extended FileInfoBlock information	*/
/*									*/
/*	By Chris Hooper (cdh@mtu.edu)  16-Jan-94			*/
/*			Last revision: 15-Aug-96			*/
/*									*/
/* Files "env:sys/passwd" and "env:sys/group" contain			*/
/*		name::id:::::						*/
/* Where all fields except name and id are ignored.			*/
/*									*/
/*----------------------------------------------------------------------*/


#include <stdio.h>
#include <sys/dir.h>
#include </dos30/dos.h>
#include </dos30/dosextens.h>
#include <dos/datetime.h>
#include <exec/memory.h>

#include <sys/types.h>
#include <ufs/dinode.h>
#include <sys/stat.h>


#define ulong unsigned long
#define CTOB(x) ((x)>>2)



int NOSORTflag = 0;
int ONEflag = 0;
int aflag = 0;
int Aflag = 0;
int Bflag = 0;
int cflag = 0;
int dflag = 0;
int fflag = 0;
int Fflag = 0;
int iflag = 0;
int gflag = 0;
int lflag = 0;
int nflag = 0;
int pflag = 0;
int Rflag = 0;
int rflag = 0;
int sflag = 0;
int tflag = 0;
int uflag = 0;

char *progname;
int has_GetPerms = 1;
int has_GetTimes = 1;
int line_pos = 0;
int max_namelength = 0;
int listed = 0;
int isfirst = 1;

FILE *fp;
int pfile_notread = 1;
int gfile_notread = 1;
int pfile_good = 0;
int gfile_good = 0;
struct pnode {
	ulong id;
	char *name;
};

struct pnode *passwd[2048];
struct pnode *group[2048];
int passwd_entries = 0;
int group_entries = 0;

struct recursive_node {
	char *filename;
	struct recursive_node *next;
};

struct sort_node {
	char  *name;
	ulong  mode;
	ulong  date;
	struct FileInfoBlock *fib;
	struct sort_node *next;
};

struct recursive_node *rlist = NULL;
struct recursive_node *rlist_tail = NULL;
struct sort_node *sortlist = NULL;
struct sort_node *sortlist_tail = NULL;

void add_dir();
void rlist_add();
void rlist_empty();
void sortlist_add();
void sortlist_sortadd();
void sortlist_empty();


int main(argc, argv)
int argc;
char *argv[];
{
	int index;
	char *ptr;

	progname = argv[0];

	for (index = 1; index < argc; index++) {
	    ptr = argv[index];
	    if (*ptr == '-')
		for (ptr++; *ptr != '\0'; ptr++)
		    switch(*ptr) {
			case '1':
			    ONEflag++;
			    break;
			case 'a':
			    aflag++;
			    break;
			case 'A':
			    Aflag++;
			    break;
			case 'B':
			    Bflag++;
			    break;
			case 'c':
			    cflag++;
			    break;
			case 'd':
			    dflag++;
			    break;
			case 'f':
			case 'F':
			    Fflag++;
			    break;
			case 'g':
			    gflag++;
			    break;
			case 'h':
			case 'v':
			    print_usage();
			case 'i':
			    iflag++;
			    break;
			case 'l':
			    lflag++;
			    break;
			case 'n':
			    nflag++;
			    break;
			case 'p':
			    pflag++;
			    break;
			case 'r':
			    rflag++;
			    break;
			case 'R':
			    Rflag++;
			    break;
			case 's':
			    sflag++;
			    break;
			case 't':
			    tflag++;
			    break;
			case 'u':
			    uflag++;
			    break;
			default:
			    fprintf(stderr, "flag %c not implemented\n", *ptr);
			    break;
		    }
	    else {
		rlist_add(ptr);
		listed++;
	    }
	}

	if (listed == 0)
	    listname("");

	rlist_empty();

	exit(0);
}

listname(name)
char *name;
{
	ulong lock;
	struct FileInfoBlock *fib;
	struct Lock *lock;
	int ret;
	int temp;
	ulong fmode = 0;

	max_namelength = 1;

	lock = (struct Lock *) Lock(name, ACCESS_READ);
	if (lock == NULL) {
	    fprintf(stderr, "%s not found\n", name);
	    return(1);
	}

	fib = (struct FileInfoBlock *)
			AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC);
	fib->fib_OwnerUID = 0L;
	fib->fib_OwnerGID = 0L;
	if (!Examine(lock, fib))
	    printf("Error examining %s\n", name);
	else {
	    if (has_GetPerms)
		if (GetPerms(name, &fmode) == ERROR_ACTION_NOT_KNOWN)
		    has_GetPerms = 0;

	    if (!dflag &&
		    ((has_GetPerms && fmode && mode_is_directory(fmode)) ||
		     (!has_GetPerms && fib_is_directory(fib))) )
		print_fibnames(lock, fib, name);
	    else if ((print_fib(fib, fmode) == 1) && Rflag)
		rlist_add(name);
	}

	FreeMem(fib, sizeof(struct FileInfoBlock));
	UnLock(lock);
}

#define UNIX_READ  04
#define UNIX_WRITE 02
#define UNIX_EXEC  01

mode_is_directory(mode)
ulong mode;
{
	return((mode & IFMT) == IFDIR);
}

fib_is_directory(fib)
struct FileInfoBlock *fib;
{
	switch(fib->fib_DirEntryType) {
	    case ST_ROOT:
	    case ST_USERDIR:
	    case ST_SOFTLINK:
	    case ST_LINKDIR:
		return(1);
	}
	return(0);
}

print_fib(fib, fmode)
struct FileInfoBlock *fib;
ulong fmode;
{
	int ret = 0;
	ulong p;
	char echar = ' ';
	char pchar = ' ';
	char tchar = '-';
	int showchar;
	int dotfile = 0;
	int is_executable = 0;
	char buf[80];

	if (!aflag && (fib->fib_FileName[0] == '.'))
	    return(0);

	if (ONEflag)
	    putchar('\r');

	if (line_pos)
	    if (line_pos > 80 - max_namelength) {
		putchar('\n');
		line_pos = 0;
	    } else
		putchar(' ');

	if (iflag)
	    printf("%6d ", fib->fib_DiskKey);

	if (sflag)
	    printf("%4d ", fib->fib_NumBlocks);

	if (has_GetPerms && fmode && !Bflag) {
	    switch(fmode & IFMT) {
		case IFDIR:
		    ret = 1;
		    pchar = 'd';
		    echar = '/';
		    break;
		case IFLNK:
		    ret = 1;
		    pchar = 'l';
		    echar = '@';
		    break;
		case IFREG:
		    pchar = '-';
		    break;
		case IFIFO:
		    pchar = 'p';
		    echar = '&';
		    break;
		case IFCHR:
		    pchar = 'c';
		    break;
		case IFBLK:
		    pchar = 'b';
		    break;
		case IFSOCK:
		    pchar = 's';
		    echar = '=';
		    break;
		case IFWHT:
		    pchar = 'w';
		    break;
		default:
		    pchar = '?';
		    break;
	    }



	    if (lflag) {
		putchar(pchar);

		if (Aflag) {
		    if (fmode & ISUID)
			tchar = 's';
		    else
			tchar = '-';

		    if (fmode & ISGID)
			if (fmode & ISVTX)
			    tchar = 'T';
			else
			    tchar = 'g';
		    else if (fmode & ISVTX)
			tchar = 't';
		    else
			tchar = '-';

		    putchar(tchar);
		}

		putchar((fmode >> 6) & UNIX_READ  ? 'r' : '-');
		putchar((fmode >> 6) & UNIX_WRITE ? 'w' : '-');
		if (!Aflag && ((fmode & ISUID) == ISUID))
		    putchar(((fmode & ~IFMT) >> 6) & UNIX_EXEC  ? 's' : 'S');
		else
		    putchar((fmode >> 6) & UNIX_EXEC  ? 'x' : '-');


		putchar((fmode >> 3) & UNIX_READ  ? 'r' : '-');
		putchar((fmode >> 3) & UNIX_WRITE ? 'w' : '-');
		if (!Aflag && ((fmode & ISGID) == ISGID))
		    putchar((fmode >> 3) & UNIX_EXEC  ? 's' : 'S');
		else
		    putchar((fmode >> 3) & UNIX_EXEC  ? 'x' : '-');

		putchar(fmode & UNIX_READ  ? 'r' : '-');
		putchar(fmode & UNIX_WRITE ? 'w' : '-');
		if (!Aflag && ((fmode & ISVTX) == ISVTX))
		    putchar(fmode & UNIX_EXEC  ? 't' : 'T');
		else
		    putchar(fmode & UNIX_EXEC  ? 'x' : '-');
	    }

	    is_executable = (fmode >> 6) & UNIX_EXEC;

	} else {
	    switch(fib->fib_DirEntryType) {
		case ST_ROOT:
		    ret = 1;
		    pchar = 'd';
		    echar = '/';
		    break;
		case ST_USERDIR:
		    ret = 1;
		    pchar = 'd';
		    echar = '/';
		    break;
		case ST_SOFTLINK:
		    pchar = 'l';
		    echar = '@';
		    break;
		case ST_LINKDIR:
		    ret = 1;
		    pchar = 'l';
		    echar = '/';
		    break;
		case ST_FILE:
		    pchar = '-';
		    break;
		case ST_LINKFILE:
		    pchar = '=';
		    echar = '@';
		    break;
		case ST_BDEVICE:
		    pchar = 'b';
		    break;
		case ST_CDEVICE:
		    pchar = 'c';
		    break;
		case ST_FIFO:
		    pchar = 'p';
		    echar = '&';
		    break;
		case ST_SOCKET:
		    pchar = 's';
		    echar = '=';
		    break;
		case ST_WHITEOUT:
		    pchar = 'w';
		    break;
		default:
		    pchar = '?';
		    break;
	    }

	    p = fib->fib_Protection;

	    if (lflag) {
	        putchar(pchar);

	        if (Aflag) {
		    if (p & FIBF_SCRIPT)
		        if (p & FIBF_PURE)
			    tchar = 'S';
		        else
			    tchar = 's';
		    else
		        if (p & FIBF_PURE)
			    tchar = 'p';
		        else
			    tchar = '-';

		    if (p & FIBF_ARCHIVE)
		        if (p & FIBF_HIDDEN)
			    tchar = 'A';
		        else
			    tchar = 'a';
		    else
		        if (p & FIBF_HIDDEN)
			    tchar = 'h';
		        else
			    tchar = '-';
	            putchar(tchar);
	        }

	        print_amiga_rwx(p, ((!Aflag && (p & FIBF_SCRIPT)) ? 's' : '-'));

	        print_amiga_rwx(p >> FIBB_GRP_DELETE,
				((!Aflag && (p & FIBF_HIDDEN)) ? 's' : '-'));
	        print_amiga_rwx(p >> FIBB_OTR_DELETE,
				((!Aflag && (p & FIBF_PURE)) ? 't' : '-'));
	    }
	    is_executable = !(p & FIBF_EXECUTE);
	}

	if (lflag) {
	    putchar(' ');

	    print_owner((ulong) fib->fib_OwnerUID);
	    if (gflag) {
		putchar(' ');
		print_group((ulong) fib->fib_OwnerGID);
	    }
	    printf("%8d ", fib->fib_Size);

	    print_daytime(&fib->fib_Date);
	}

	if ((pchar == '-') && is_executable)
	    echar = '*';

	showchar = Fflag | (pflag && (echar == '/'));
	if (lflag)
 	    printf("%.108s%c%.80s\n", fib->fib_FileName,
		    (showchar ? echar : ' '), fib->fib_Comment);
	else {
	    sprintf(buf, "%s%c", fib->fib_FileName,
		    (showchar ? echar : ' '));

 	    printf("%-*s", max_namelength + 1, buf);

	    if (ONEflag)
		putchar('\n');
	    else
		line_pos += (max_namelength + 2);
	}

	return(ret);
}

print_fibnames(plock, pfib, name)
ulong plock;
struct FileInfoBlock *pfib;
char *name;
{
	ulong lock;
	ulong fmode = 0;
	struct FileInfoBlock *fib;
	char buf[1024];
	char ec = '\0';
	char *ptr;

	if (line_pos) {
	    line_pos = 0;
	    printf("\n\n");
	}

	if (isfirst)
	    isfirst = 0;
	else
	    putchar('\n');

	if (((listed > 1) || Rflag) && *name)
	    printf("[%s]\n", name);

	while (ExNext(plock, pfib)) {
	    for (ptr = name; *ptr != '\0'; ptr++)
		ec = *ptr;
	    switch (ec) {
		case '\0':
		    strcpy(buf, pfib->fib_FileName);
		    break;
		case ':':
		case '/':
		    sprintf(buf, "%s%s", name, pfib->fib_FileName);
		    break;
		default:
		    sprintf(buf, "%s/%s", name, pfib->fib_FileName);
	    }
	    if (has_GetPerms)
		if (GetPerms(buf, &fmode) == ERROR_ACTION_NOT_KNOWN)
		    has_GetPerms = 0;

	    sortlist_add(buf, fmode, pfib);
	}
	sortlist_empty();

	if (line_pos) {
	    line_pos = 0;
	    putchar('\n');
	}

}

print_amiga_rwx(p, x)
ulong p;
char x;
{
	char pchar;

	putchar(p & FIBF_READ        ? '-' : 'r');

	if (p & FIBF_DELETE)
	    putchar(p & FIBF_WRITE   ? '-' : 'W');
	else
	    putchar(p & FIBF_WRITE   ? '=' : 'w');

	if (x == '-')
	    putchar(p & FIBF_EXECUTE ? '-' : 'x');
	else
	    putchar(p & FIBF_EXECUTE ? (x + 'A' - 'a') : x);
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


print_owner(uid)
ulong uid;
{
	char *ptr;
	char buf[32];

	ptr = buf;

	if (nflag)
	    sprintf(buf, "%-8d", uid);
	else
	    switch (uid) {
		case 0:     ptr = "root";	break;
		case 1:     ptr = "daemon";	break;
		case 2:     ptr = "sys";	break;
		case 3:     ptr = "bin";	break;
		case 4:     ptr = "uucp";	break;
		case 5:     ptr = "operator";	break;
		case 6:     ptr = "news";	break;
		case 7:     ptr = "ingres";	break;
		case 9:     ptr = "audit";	break;
		case 65534: ptr = "nobody";	break;
		case 1640:  ptr = "cdh";	break;
		default:
			sprintf(buf, "%-7d", uid);
	    }

	printf("%-8s", ptr);
}


print_group(gid)
ulong gid;
{
	char *ptr;
	char buf[16];

	if (gfile_notread) {
		gfile_good = 0;
		gfile_notread = 0;
	}

	if (gfile_good) {
		return;
	}

	ptr = buf;
	if (nflag)
	    sprintf(buf, "%-8d", gid);
	else
	    switch (gid) {
		case 0:     ptr = "wheel";	break;
		case 1:     ptr = "daemon";	break;
		case 2:     ptr = "kmem";	break;
		case 3:     ptr = "bin";	break;
		case 4:     ptr = "tty";	break;
		case 5:     ptr = "operator";	break;
		case 6:     ptr = "news";	break;
		case 8:     ptr = "uucp";	break;
		case 9:     ptr = "audit";	break;
		case 10:    ptr = "staff";	break;
		case 20:    ptr = "other";	break;
		case 65534: ptr = "nogroup";	break;
		default:
			sprintf(buf, "%-8d", gid);
	    }
	printf("%-8s", ptr);
}

int GetPerms(name, mode)
char	*name;
ulong	*mode;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

	msgport = (struct Process *) DeviceProc(name, NULL);
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
	packet->sp_Pkt.dp_Type         = ACTION_GET_PERMS;
	packet->sp_Pkt.dp_Arg1         = dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	    err = packet->sp_Pkt.dp_Res2;
	    *mode = 0;
        } else
	    *mode = packet->sp_Pkt.dp_Res2;

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

/*
printf("file=%s perms=%o\n", name, *mode);
*/

	return(err);
}



/* which = 1 for last modify
 * which = 3 for last inode change
 * which = 5 for last access
 */
int GetTimes(name, which, timevalue)
char	*name;
int	which;
ulong	*timevalue;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

/*
printf("GetTimes %s %d %d\n", name, which, timevalue);
*/

	msgport = (struct Process *) DeviceProc(name, NULL);
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
	packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
	packet->sp_Pkt.dp_Arg1         = which;
	packet->sp_Pkt.dp_Arg2         = dlock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = (ULONG) timevalue;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE)
	    err = packet->sp_Pkt.dp_Res2;


        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}



void sortlist_add(filename, mode, fib)
char *filename;
ulong mode;
struct FileInfoBlock *fib;
{
	struct sort_node *temp;
	struct timeval gtime;
	int namelen;

/*
	printf("sortlist_add %s %o\n", filename, mode);
*/

	temp = (struct sort_node *)
			AllocMem(sizeof(struct sort_node), MEMF_PUBLIC);

	namelen = strlen(fib->fib_FileName);
	if (namelen > max_namelength)
	    max_namelength = namelen;

	temp->name = (char *)
			AllocMem(strlen(filename) + 1, MEMF_PUBLIC);
	strcpy(temp->name, filename);
	if (has_GetTimes && (uflag | cflag)) {
	    gtime.tv_sec = 0;
	    if (uflag) {
		if (GetTimes(filename, 5, &gtime) == ERROR_ACTION_NOT_KNOWN)
		    has_GetTimes = 0;
	    } else if (cflag) {
		if (GetTimes(filename, 3, &gtime) == ERROR_ACTION_NOT_KNOWN)
		    has_GetTimes = 0;
	    }
	    if (has_GetTimes && gtime.tv_sec) {
		fib->fib_Date.ds_Days   = gtime.tv_sec / 86400 - 2922;
		fib->fib_Date.ds_Minute = (gtime.tv_sec % 86400) / 60;
		fib->fib_Date.ds_Tick   = (gtime.tv_sec % 60) * TICKS_PER_SECOND;
	    }
	}


	temp->mode = mode;
	temp->date = fib->fib_Date.ds_Days * 24 * 60 * 60 +
		     fib->fib_Date.ds_Minute * 60 +
		     fib->fib_Date.ds_Tick / TICKS_PER_SECOND;
	temp->fib = (struct FileInfoBlock *)
			AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC);
	memcpy(temp->fib, fib, sizeof(struct FileInfoBlock));
	temp->next = NULL;

	sortlist_sortadd(temp);
}

void sortlist_sortadd(node)
struct sort_node *node;
{
	int temp;
	struct sort_node *parent = NULL;
	struct sort_node *current;

	if (NOSORTflag) {
	    if (sortlist_tail == NULL)
		sortlist = node;
	    else
		sortlist_tail->next = node;
	    sortlist_tail = node;
	    return;
	}

/*
	printf("*%s=%d\n", node->name, node->date);
*/

	for (current = sortlist; current != NULL; current = current->next) {
	    if (tflag) {
		if (rflag) {
		    if (current->date >= node->date)
			break;
		} else
		    if (current->date <= node->date)
			break;
	    } else {
		temp = strcompare(current->name, node->name) *
			         (rflag ? -1 : 1);
		if (temp >= 0)
			break;
	    }
	    parent = current;
	}

	if (parent == NULL)
		sortlist = node;
	else
		parent->next = node;
	node->next = current;

/*
	for (current = sortlist; current != NULL; current = current->next)
	    printf("%s=%d ", current->name, current->date);
	printf("\n");
*/
}


/* Return values:
 *    -1 is str1 < str2
 *     0 is str1 = str2
 *     1 is str1 > str2
 */
int strcompare(str1, str2)
char *str1;
char *str2;
{
	char ch1;
	char ch2;

	while (*str1 != '\0') {
	    ch1 = *str1;
	    ch2 = *str2;

	    if (ch2 == '\0')	/* str2 ends first */
		return(1);

	    if ((ch1 >= 'A') && (ch1 <= 'Z'))
		ch1 -= ('A' - 'a');
	    if ((ch2 >= 'A') && (ch2 <= 'Z'))
		ch2 -= ('A' - 'a');

	    if (ch1 < ch2)
		return(-1);

	    if (ch1 > ch2)
		return(1);

	    str1++;
	    str2++;
	}

	if (*str2 == '\0')	/* both end same time */
	    return(0);

	return(-1);		/* str1 ends first */
}

void sortlist_empty()
{
	struct sort_node *temp;

	while (sortlist != NULL) {
	    temp = sortlist;
	    sortlist = sortlist->next;

	    if ((print_fib(temp->fib, temp->mode) == 1) && Rflag)
		rlist_add(temp->name);

	    FreeMem(temp->fib, sizeof(struct FileInfoBlock));
	    FreeMem(temp->name, strlen(temp->name) + 1);
	    FreeMem(temp, sizeof(struct sort_node));
	}
	sortlist_tail = NULL;
}

void rlist_add(filename)
char *filename;
{
	struct recursive_node *node;

/*
	printf("rlist_add %s\n", filename);
*/

	node = (struct recursive_node *) malloc(sizeof(struct recursive_node));
	node->filename = (char *) strdup(filename);
	node->next = NULL;

	if (rlist == NULL)
	    rlist = node;
	else
	    rlist_tail->next = node;
	rlist_tail = node;
}

void rlist_empty()
{
	struct recursive_node *temp;

	while (rlist != NULL) {
		temp = rlist;
		rlist = rlist->next;
		listname(temp->filename);
		free(temp->filename);
		free(temp);
	}
	rlist_tail = NULL;
	if (line_pos) {
		line_pos = 0;
		putchar('\n');
	}
}



print_usage()
{
/* -l flag is long listing
 * -f flag is do not sort and turn on -a, turn off -l
 * -1 is list names in a single column
 */
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "usage: %s [-acdFghinprRstu] [filename [ filename ... ]]\n", progname);
	fprintf(stderr, "	where -a is show files which begin with a `.'\n");
	fprintf(stderr, "	      -A is show AmigaDOS (not UNIX) permissions\n");
	fprintf(stderr, "	      -c is sort by last inode change time\n");
	fprintf(stderr, "	      -d is do not examine directory contents\n");
	fprintf(stderr, "	      -F is show file sizes\n");
	fprintf(stderr, "	      -g is show file groups\n");
	fprintf(stderr, "	      -i is show file inode number\n");
	fprintf(stderr, "	      -h is show this help\n");
	fprintf(stderr, "	      -l is show long listing\n");
	fprintf(stderr, "	      -n is show numerical userid and group\n");
	fprintf(stderr, "	      -p is put slash after directory names\n");
	fprintf(stderr, "	      -r is reverse sort order\n");
	fprintf(stderr, "	      -R is show recursive listing of directory\n");
	fprintf(stderr, "	      -s is show file sizes in blocks\n");
	fprintf(stderr, "	      -t is sort by last modify time\n");
	fprintf(stderr, "	      -u is sort by last access time\n");
	exit(0);
}



