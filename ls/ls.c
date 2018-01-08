/*
 * This program is Copyright by Chris Hooper.  All code herein is
 * freeware.  No portion of this program may be sold for profit except
 * by written permission of Chris Hooper.  This copyright notice
 * must be retained in all derivations.
 *
 * --------------------------------------------------------------------
 *
 * Extended ls - shows user/group and appropriate UNIXish permissions
 *                per AmigaDOS 3.0 extended FileInfoBlock information
 *
 *      By Chris Hooper (amiga@cdh.eebugs.com)  1994-01-16 - 2018-01-19
 *
 * Files "env:sys/passwd" and "env:sys/group" contain
 *              name::id:::::
 * Where all fields except name and id are ignored.
 */

const char *version = "\0$VER: ls 1.2 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/datetime.h>
#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/alib_protos.h>
#include <bffs_dosextens.h>

#include <sys/types.h>
#include <ufs/dinode.h>
#include <sys/stat.h>
#include <string.h>
#include </include/stdlib.h>

#define MAX_COLUMNS 80

#define ulong unsigned long
#define	BTOC(p, t) ((t)(((long)p)<<2))
#define CTOB(p) (((long)p)>>2)
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

#define UNIX_READ  04
#define UNIX_WRITE 02
#define UNIX_EXEC  01

typedef struct {
        ULONG   fa_type;
        ULONG   fa_mode;
        ULONG   fa_nlink;
        ULONG   fa_uid;
        ULONG   fa_gid;
        ULONG   fa_size;
        ULONG   fa_blocksize;
        ULONG   fa_rdev;
        ULONG   fa_blocks;
        ULONG   fa_fsid;
        ULONG   fa_fileid;
        ULONG   fa_atime;
        ULONG   fa_atime_us;
        ULONG   fa_mtime;
        ULONG   fa_mtime_us;
        ULONG   fa_ctime;
        ULONG   fa_ctime_us;
} fileattr_t;


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
int Tflag = 0;
int uflag = 0;

int has_ExObject = 1;
int has_GetPerms = 1;
int has_GetTimes = 1;
int max_namelength = 0;
int listed = 0;
int isfirst = 1;
int printed = 0;

int pfile_was_read = 0;
int gfile_was_read = 0;
typedef struct pnode {
    ulong         id;
    char         *name;
    struct pnode *next;
} pnode_t;

pnode_t *passwd_head = NULL;
pnode_t *group_head = NULL;

typedef struct recursive_node {
	char *filename;
	struct recursive_node *next;
} recursive_node_t;

typedef struct sort_node {
	char  *name;
	char  *shortname;
	ulong  mode;
	ulong		   date;
	FileInfoBlock_3_t *fib;
	fileattr_t        *fattr;
	struct sort_node  *next;
} sort_node_t;

recursive_node_t *rlist         = NULL;
recursive_node_t *rlist_tail    = NULL;
sort_node_t      *sortlist      = NULL;
sort_node_t      *sortlist_tail = NULL;

static void rlist_add(const char *filename);
static void rlist_process(void);
static void sortlist_add(const char *filename, const char *shortname,
			 ulong mode, FileInfoBlock_3_t *fib, fileattr_t *fattr);
static void sortlist_process(void);
static void listname(const char *name);
static int  strcompare(const char *str1, const char *str2);
static void print_fibnames(BPTR plock, FileInfoBlock_3_t *pfib,
			   const char *name);
static int  print_fib(FileInfoBlock_3_t *fib, ulong fmode, fileattr_t *fattr,
		      int width, const char *name);
static int  PExObject(BPTR lock, FileInfoBlock_3_t *fib, fileattr_t *attr,
		      int next);
static int  GetPerms(const char *name, ulong *mode);
static int  GetTimes(const char *name, int which, unix_timeval_t *timevalue);

static int
mode_is_directory(ulong mode)
{
    switch (mode & IFMT) {
	case IFDIR:
	case IFLNK:
	    return (1);
	default:
	    return (0);
    }
}

static int
fib_is_directory(FileInfoBlock_3_t *fib)
{
    switch (fib->fib_DirEntryType) {
	case ST_ROOT:
	case ST_USERDIR:
	case ST_SOFTLINK:
	case ST_LINKDIR:
	    return (1);
    }
    return (0);
}

static void
listname(const char *name)
{
    FileInfoBlock_3_t *fib;
    BPTR               lock;
    ulong              fmode = 0;
    fileattr_t        *fattr = NULL;

    max_namelength = 1;

    lock = Lock((char *)name, ACCESS_READ);
    if (lock == 0) {
	fprintf(stderr, "%s not found\n", name);
	return;
    }

    fib = (FileInfoBlock_3_t *)
		    AllocMem(sizeof (FileInfoBlock_3_t), MEMF_PUBLIC);
    if (fib == NULL)
	goto alloc_failure;

    fib->fib_OwnerUID = 0L;
    fib->fib_OwnerGID = 0L;
    if (has_ExObject) {
	fattr = (fileattr_t *) AllocMem(sizeof (fileattr_t), MEMF_PUBLIC);
	if (fattr != NULL) {
	    if (PExObject(lock, fib, fattr, 0) == 0) {
		/* (fattr != NULL) only in this case */
		fmode = fattr->fa_mode;
	    } else {
		FreeMem(fattr, sizeof (fileattr_t));
		fattr = NULL;
	    }
	}
    }
    if ((fattr == NULL) && !Examine(lock, (struct FileInfoBlock *) fib)) {
	printf("Error examining %s\n", name);
	goto examine_failure;
    }

    if (fattr == NULL) {
	has_ExObject = 0;  /* ExObject() failed this time or before */
	if (has_GetPerms)
	    if (GetPerms(name, &fmode) == ERROR_ACTION_NOT_KNOWN)
		has_GetPerms = 0;
    }

    if (!dflag &&
	((has_GetPerms && fmode && mode_is_directory(fmode)) ||
	 (!has_GetPerms && fib_is_directory(fib))) ) {
	print_fibnames(lock, fib, name);
    } else if ((print_fib(fib, fmode, fattr, 0, name) == 1) && Rflag) {
	rlist_add(name);
    } else {
	printf("\n");
    }

examine_failure:
    if (fattr != NULL)
	FreeMem(fattr, sizeof (fileattr_t));
    FreeMem(fib, sizeof (FileInfoBlock_3_t));
alloc_failure:
    UnLock(lock);
}

static char
get_unix_type_flags(ulong fmode, char *echar, int *ret)
{
    char pchar;

    switch (fmode & IFMT) {
	case IFDIR:
	    pchar  = 'd';
	    *echar = '/';
	    *ret   = 1;
	    break;
	case IFLNK:
	    pchar  = 'l';
	    *echar = '@';
	    *ret   = 1;
	    break;
	case IFREG:
	    pchar = '-';
	    break;
	case IFIFO:
	    pchar  = 'p';
	    *echar = '&';
	    break;
	case IFCHR:
	    pchar = 'c';
	    break;
	case IFBLK:
	    pchar = 'b';
	    break;
	case IFSOCK:
	    pchar  = 's';
	    *echar = '=';
	    break;
	case IFWHT:
	    pchar = 'w';
	    break;
	default:
	    pchar = '?';
	    break;
    }
    return (pchar);
}

static char
get_amigaos_type_flags(ulong type, char *echar, int *ret)
{
    char pchar;

    switch (type) {
	case ST_ROOT:
	    pchar  = 'd';
	    *echar = '/';
	    *ret   = 1;
	    break;
	case ST_USERDIR:
	    pchar  = 'd';
	    *echar = '/';
	    *ret   = 1;
	    break;
	case ST_SOFTLINK:
	    pchar  = 'l';
	    *echar = '@';
	    break;
	case ST_LINKDIR:
	    pchar  = 'l';
	    *echar = '/';
	    *ret   = 1;
	    break;
	case ST_FILE:
	    pchar = '-';
	    break;
	case ST_LINKFILE:
	    pchar  = '=';
	    *echar = '@';
	    break;
	case ST_BDEVICE:
	    pchar = 'b';
	    break;
	case ST_CDEVICE:
	    pchar = 'c';
	    break;
	case ST_FIFO:
	    pchar  = 'p';
	    *echar = '&';
	    break;
	case ST_SOCKET:
	    pchar  = 's';
	    *echar = '=';
	    break;
	case ST_WHITEOUT:
	    pchar = 'w';
	    break;
	default:
	    pchar = '?';
	    break;
    }
    return (pchar);
}

static void
read_passwd_group(pnode_t **head, const char *const *files, int count)
{
    int             cur;
    APTR            save_ptr;
    struct Process *pr;

    /* Prevent requesters from popping up */
    pr = (struct Process *)FindTask(NULL);
    save_ptr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)~0;
    for (cur = 0; cur < count; cur++) {
	FILE *fp = fopen(files[cur], "r");
	if (fp != NULL) {
	    char buf[80];
	    char name[16];
	    ulong id;
	    while (fgets(buf, sizeof (buf), fp) != NULL) {
		if ((strchr(buf, '|') &&
		     sscanf(buf, "%[^|]\|%[^|]\|%u", name, buf, &id) == 3) ||
		    (strchr(buf, ':') &&
		     sscanf(buf, "%[^:]:%u", name, &id) == 2)) {
		    pnode_t *temp = malloc(sizeof (*temp));
		    temp->name = strdup(name);
		    temp->id = id;
		    temp->next = *head;
		    *head = temp;
		}
	    }
	    fclose(fp);
	}
    }
    pr->pr_WindowPtr = save_ptr;  /* Restore requesters */
}

static void
print_owner(ulong uid)
{
    const char * const pfiles[] = {
	"S:passwd",
	"DEVS:passwd",
	"INET:db/passwd",
	"ENV:passwd",
    };

    if (!pfile_was_read) {
	read_passwd_group(&passwd_head, pfiles, ARRAY_SIZE(pfiles));
	pfile_was_read = 1;
    }

    if (nflag) {
show_uid:
	printf("%-8lu", uid);
    } else if (passwd_head != NULL) {
	pnode_t *temp;

	for (temp = passwd_head; temp != NULL; temp = temp->next)
	    if (temp->id == uid)
		break;

	if (temp == NULL)
	    goto show_uid;

	printf("%-8s", temp->name);
    } else {
	char *ptr;
	switch (uid) {
	    case 0:     ptr = "root";     break;
	    case 1:     ptr = "daemon";   break;
	    case 2:     ptr = "sys";      break;
	    case 3:     ptr = "bin";      break;
	    case 4:     ptr = "uucp";     break;
	    case 5:     ptr = "operator"; break;
	    case 6:     ptr = "news";     break;
	    case 7:     ptr = "ingres";   break;
	    case 9:     ptr = "audit";    break;
	    case 65534: ptr = "nobody";   break;
	    default:
		goto show_uid;
	}
	printf("%-8s", ptr);
    }

}

static void
print_group(ulong gid)
{
    const char * const gfiles[] = {
	"S:group",
	"DEVS:group",
	"INET:db/group",
	"ENV:group",
    };

    if (!gfile_was_read) {
	read_passwd_group(&group_head, gfiles, ARRAY_SIZE(gfiles));
	gfile_was_read = 1;
    }

    if (nflag) {
show_gid:
	printf("%-8lu", gid);
    } else if (group_head != NULL) {
	pnode_t *temp;

	for (temp = group_head; temp != NULL; temp = temp->next)
	    if (temp->id == gid)
		break;

	if (temp == NULL)
	    goto show_gid;

	printf("%-8s", temp->name);
    } else {
	char *ptr;
	switch (gid) {
	    case 0:     ptr = "wheel";    break;
	    case 65534: ptr = "nogroup";  break;
	    default:
		goto show_gid;
	}
	printf("%-8s", ptr);
    }
}

static void
print_amiga_rwx(ulong p, char x)
{
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

static const int mdays[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

static void
print_daytime(struct DateStamp *dstamp)
{
    char datebuf[32];
    char timebuf[32];
    struct DateTime dtime;
    struct DateStamp todaystamp;

    dtime.dat_Stamp.ds_Days   = dstamp->ds_Days;
    dtime.dat_Stamp.ds_Minute = dstamp->ds_Minute;
    dtime.dat_Stamp.ds_Tick   = dstamp->ds_Tick;
    if (Tflag)
	dtime.dat_Format      = FORMAT_CDN;
    else
	dtime.dat_Format      = FORMAT_DOS;
    dtime.dat_Flags           = 0x0;
    dtime.dat_StrDay          = NULL;
    dtime.dat_StrDate         = datebuf;
    dtime.dat_StrTime         = timebuf;
    DateToStr(&dtime);
    DateStamp(&todaystamp);

    if (datebuf[0] == '0')		/* remove date leading zero */
	datebuf[0] = ' ';

    if (datebuf[0] == '-') {
show_blank:
	printf("             ");
    } else if (Tflag) {
	int day;
	int month;
	int year;
	if (sscanf(datebuf, "%d-%d-%d", &day, &month, &year) != 3)
	    goto show_blank;
	if (year >= 70)
	    year += 1900;
	else
	    year += 2000;
	printf("%4d-%02d-%02d %s ", year, month, day, timebuf);
    } else if ((dtime.dat_Stamp.ds_Days + 274 > todaystamp.ds_Days) &&
	       (dtime.dat_Stamp.ds_Days < todaystamp.ds_Days + 91)) {
	/* It's over about nine months old; show year and no time */
	printf("%-3.3s %2.2s %5.5s ", datebuf + 3,
		datebuf, timebuf);
    } else {
	printf("%-3.3s %2.2s  %d%.2s ", datebuf + 3, datebuf,
		(datebuf[7] > '6') ? 19 : 20, datebuf + 7);
    }
}

static int
print_fib(FileInfoBlock_3_t *fib, ulong fmode, fileattr_t *fattr, int width,
	  const char *name)
{
    int   is_dir = 0;
    char  echar  = ' ';
    char  pchar  = ' ';
    char  tchar   = '-';
    int   showchar;
    int   is_executable = 0;
    int   unix_perms = (has_GetPerms || (fattr != NULL));
    int   cur_width = 0;
    char  buf[80];

    printed = 1;
    if (iflag) {
	printf("%6ld ", fib->fib_DiskKey);
	cur_width += 7;
    }

    if (sflag) {
	printf("%4ld ", fib->fib_NumBlocks);
	cur_width += 5;
    }

    if (unix_perms && fmode && !Bflag) {
	/* UNIX format permission string */
	pchar = get_unix_type_flags(fmode, &echar, &is_dir);

	if (lflag || gflag) {
	    putchar(pchar);

	    if (Aflag) {
		/* Add Amiga-specific type characters */
		ulong p = fib->fib_Protection;
		if (p & FIBF_SCRIPT)
		    if (p & FIBF_PURE)
			tchar = 'S';
		    else
			tchar = 's';
		else if (p & FIBF_PURE)
		    tchar = 'p';
		else
		    tchar = '-';
		putchar(tchar);

		if (p & FIBF_HIDDEN)
		    if (p & FIBF_ARCHIVE)
			tchar = 'H';
		    else
			tchar = 'h';
		else if (p & FIBF_ARCHIVE)
		    tchar = 'a';
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
	/* AmigaDOS format permissions */
	ulong p = fib->fib_Protection;

	pchar = get_amigaos_type_flags(fib->fib_DirEntryType, &echar,
				       &is_dir);

	if (lflag || gflag) {
	    putchar(pchar);

	    if (Aflag) {
		if (p & FIBF_SCRIPT)
		    if (p & FIBF_PURE)
			tchar = 'S';
		    else
			tchar = 's';
		else if (p & FIBF_PURE)
		    tchar = 'p';
		else
		    tchar = '-';

		putchar(tchar);
		if (p & FIBF_HIDDEN)
		    if (p & FIBF_ARCHIVE)
			tchar = 'H';
		    else
			tchar = 'h';
		else if (p & FIBF_ARCHIVE)
		    tchar = 'a';
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

    if (lflag || gflag) {
	putchar(' ');

	print_owner((ulong) fib->fib_OwnerUID);
	if (gflag) {
	    putchar(' ');
	    print_group((ulong) fib->fib_OwnerGID);
	}
	if (((pchar == 'b') || (pchar == 'c')) && (fattr != NULL)) {
	    ulong rdev = fattr->fa_rdev;
	    sprintf(buf, "%lu, %lu", rdev >> 8, rdev & 0xff);
	    printf("%9s ", buf);
	} else {
	    printf("%9ld ", fib->fib_Size);
	}

	print_daytime(&fib->fib_Date);
    }

    if ((pchar == '-') && is_executable)
	echar = '*';

    showchar = Fflag | (pflag && (echar == '/'));
    if (lflag || gflag) {
	int len;
	printf("%.108s%c%.80s", fib->fib_FileName,
		(showchar ? echar : ' '), fib->fib_Comment);
	if ((fib->fib_DirEntryType == ST_SOFTLINK) &&
	    ((len = strlen(name)) < sizeof (buf))) {
	    BPTR lock;
	    strcpy(buf, name);
	    while (len-- > 0) {
		char ch = buf[len];
		if ((ch == ':') || (ch == '/'))
		    break;
		buf[len] = '\0';
	    }
	    lock = Lock(buf, ACCESS_READ);
	    if (lock != 0) {
		struct MsgPort *port = BTOC(lock, struct FileLock *)->fl_Task;

		if (ReadLink(port, lock, fib->fib_FileName, buf,
			     sizeof (buf) - 1) > 0) {
		    buf[sizeof (buf) - 1] = '\0';
		    printf("-> %s", buf);
		}
		UnLock(lock);
	    }
	}
    } else {
	sprintf(buf, "%.108s%c", fib->fib_FileName,
		(showchar ? echar : ' '));

	printf("%-*s", width - cur_width, buf);
    }

    return (is_dir);
}

static void
print_fibnames(BPTR plock, FileInfoBlock_3_t *pfib, const char *name)
{
    ulong fmode = 0;
    char buf[1024];
    char ec = '\0';
    const char *ptr;
    fileattr_t *fattr;

    if (((listed > 1) || Rflag) && *name) {
	/* If already printed (anything), first emit a newline */
	if (printed)
	    putchar('\n');
	printf("[%s]\n", name);
    }

    while (1) {
	fattr = NULL;
	if (has_ExObject) {
	    fattr = (fileattr_t *)
		    AllocMem(sizeof (fileattr_t), MEMF_PUBLIC);
	    if (fattr != NULL) {
		if (PExObject((BPTR)plock, pfib, fattr, 1) == 0) {
		    /* (fattr != NULL) only in this case */
		    fmode = fattr->fa_mode;
		} else {
		    FreeMem(fattr, sizeof (fileattr_t));
		    fattr = NULL;
		}
	    }
	}

	if ((fattr == NULL) &&
	    (ExNext(plock, (struct FileInfoBlock *) pfib) == 0))
	    break;  /* No more directory entries: exit loop */

	if (fattr == NULL) {
	    has_ExObject = 0;  /* ExObject() failed this time or before */
	    if (has_GetPerms)
		if (GetPerms(name, &fmode) == ERROR_ACTION_NOT_KNOWN)
		    has_GetPerms = 0;
	}

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
		break;
	}

	if (aflag || (pfib->fib_FileName[0] != '.'))
	    sortlist_add(buf, pfib->fib_FileName, fmode, pfib, fattr);
    }
    while (sortlist != NULL)
	sortlist_process();
}

static int
PExObject(BPTR lock, FileInfoBlock_3_t *fib, fileattr_t *attr, int next)
{
	int			rc = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;

	msgport = BTOC(lock, struct FileLock *)->fl_Task;
	if (msgport == NULL)
		return (1);

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport) {
		fprintf(stderr, "Unable to create reply port\n");
		return(1);
	}

	packet = (struct StandardPacket *)
		 AllocMem(sizeof (struct StandardPacket),
			  MEMF_CLEAR | MEMF_PUBLIC);

	if (packet == NULL) {
		fprintf(stderr, "Unable to allocate memory\n");
		DeletePort(replyport);
		return(1);
	}

	packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
	packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
	packet->sp_Pkt.dp_Port         = replyport;
	if (next)
	    packet->sp_Pkt.dp_Type     = ACTION_EX_NEXT;
	else
	    packet->sp_Pkt.dp_Type     = ACTION_EX_OBJECT;
	packet->sp_Pkt.dp_Arg1         = lock;
	packet->sp_Pkt.dp_Arg2         = CTOB(fib);
	packet->sp_Pkt.dp_Arg3         = (ULONG) attr;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	    rc = packet->sp_Pkt.dp_Res2;
	} else {
	    ULONG len = fib->fib_FileName[0];
	    memcpy(fib->fib_FileName, fib->fib_FileName + 1, len);
	    fib->fib_FileName[len] = '\0';
	}

        FreeMem(packet, sizeof (struct StandardPacket));
        DeletePort(replyport);

	return (rc);
}

static int
GetPerms(const char *name, ulong *mode)
{
	int			rc = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

	msgport = (struct MsgPort *) DeviceProc((char *)name);
	if (msgport == NULL)
		return(1);

	dlock = (struct Lock *) CurrentDir(0);
	CurrentDir((BPTR)dlock);

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport) {
		fprintf(stderr, "Unable to create reply port\n");
		return(1);
	}

	packet = (struct StandardPacket *)
		 AllocMem(sizeof (struct StandardPacket),
			  MEMF_CLEAR | MEMF_PUBLIC);

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
	packet->sp_Pkt.dp_Arg1         = (ULONG) dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	    rc = packet->sp_Pkt.dp_Res2;
	    *mode = 0;
        } else
	    *mode = packet->sp_Pkt.dp_Res2;

        FreeMem(packet, sizeof (struct StandardPacket));
        DeletePort(replyport);

	return(rc);
}


/* which = 1 for last modify
 * which = 3 for last inode change
 * which = 5 for last access
 */
static int
GetTimes(const char *name, int which, unix_timeval_t *timevalue)
{
	int			rc = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

	msgport = (struct MsgPort *) DeviceProc((char *)name);
	if (msgport == NULL)
		return(1);

	dlock = (struct Lock *) CurrentDir(0);
	CurrentDir((BPTR)dlock);

	replyport = (struct MsgPort *) CreatePort(NULL, 0);
	if (!replyport) {
		fprintf(stderr, "Unable to create reply port\n");
		return(1);
	}

	packet = (struct StandardPacket *)
		 AllocMem(sizeof (struct StandardPacket),
			  MEMF_CLEAR | MEMF_PUBLIC);

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
	packet->sp_Pkt.dp_Arg2         = (ULONG) dlock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = (ULONG) timevalue;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE)
	    rc = packet->sp_Pkt.dp_Res2;

        FreeMem(packet, sizeof (struct StandardPacket));
        DeletePort(replyport);

	return(rc);
}

static void
sortlist_sortadd(sort_node_t *node)
{
	int temp;
	sort_node_t *parent = NULL;
	sort_node_t *current;

	if (fflag) {
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
		/* XXX: replace with stricmp()? */
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

static void
sortlist_add(const char *filename, const char *shortname, ulong mode,
	     FileInfoBlock_3_t *fib, fileattr_t *fattr)
{
	sort_node_t *temp;
	int namelen;

/*
	printf("sortlist_add %s %o\n", filename, mode);
*/

	temp = (sort_node_t *) AllocMem(sizeof (sort_node_t), MEMF_PUBLIC);
	if (temp == NULL)
	    return;

	namelen = strlen(fib->fib_FileName);
	if (namelen > max_namelength)
	    max_namelength = namelen;

	temp->name = (char *) AllocMem(strlen(filename) + 1, MEMF_PUBLIC);
	if (temp->name == NULL) {
	    FreeMem(temp, sizeof (sort_node_t));
	    return;
	}
	strcpy(temp->name, filename);

	temp->shortname = (char *) AllocMem(strlen(shortname) + 1, MEMF_PUBLIC);
	if (temp->shortname == NULL) {
	    FreeMem(temp->name, strlen(temp->name) + 1);
	    FreeMem(temp, sizeof (sort_node_t));
	    return;
	}
	strcpy(temp->shortname, shortname);

	if (uflag | cflag) {
	    unix_timeval_t gtime;
	    int type = cflag ? 3 : 5;

	    gtime.tv_sec = 0;

	    if (cflag && (fattr != NULL) && (fattr->fa_ctime != 0)) {
		/* Show last inode change time */
		gtime.tv_sec  = fattr->fa_ctime;
		gtime.tv_usec = fattr->fa_ctime_us;
	    } else if (uflag && (fattr != NULL) && (fattr->fa_atime != 0)) {
		/* Show last access time */
		gtime.tv_sec  = fattr->fa_atime;
		gtime.tv_usec = fattr->fa_atime_us;
	    } else if (has_GetTimes &&
		   GetTimes(filename, type, &gtime) == ERROR_ACTION_NOT_KNOWN) {
		has_GetTimes = 0;
	    }

	    if (gtime.tv_sec != 0) {
		fib->fib_Date.ds_Days   = gtime.tv_sec / 86400 - 2922;
		fib->fib_Date.ds_Minute = (gtime.tv_sec % 86400) / 60;
		fib->fib_Date.ds_Tick   = (gtime.tv_sec % 60) * TICKS_PER_SECOND;
	    }
	}

	temp->mode = mode;
	temp->date = fib->fib_Date.ds_Days * 24 * 60 * 60 +
		     fib->fib_Date.ds_Minute * 60 +
		     fib->fib_Date.ds_Tick / TICKS_PER_SECOND;
	temp->fib = (FileInfoBlock_3_t *)
			AllocMem(sizeof (FileInfoBlock_3_t), MEMF_PUBLIC);
	if (temp->fib == NULL) {
	    FreeMem(temp->shortname, strlen(temp->shortname) + 1);
	    FreeMem(temp->name, strlen(temp->name) + 1);
	    FreeMem(temp, sizeof (sort_node_t));
	    return;
	}
	memcpy(temp->fib, fib, sizeof (FileInfoBlock_3_t));
	temp->fattr = fattr;
	temp->next = NULL;

	sortlist_sortadd(temp);
}

/* Return values:
 *    -1 is str1 < str2
 *     0 is str1 = str2
 *     1 is str1 > str2
 *
 * XXX: replace with stricmp()?
 */
static int
strcompare(const char *str1, const char *str2)
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

static int
list_count(void)
{
    sort_node_t *temp;
    int          count = 0;

    for (temp = sortlist; temp != NULL; temp = temp->next)
	count++;
    return (count);
}

static int
get_widths(int per_column, int add_width, int *widths)
{
    sort_node_t *temp;
    int          count = 0;
    int		 sum   = 0;

    *widths = 0;
    for (temp = sortlist; temp != NULL; temp = temp->next) {
	int len = strlen(temp->shortname) + add_width;
	if (*widths < len)
	    *widths = len;
	if (++count == per_column) {
	    count = 0;
	    sum += *widths;
	    widths++;
	    *widths = 0;
	}
    }
    if (count != per_column)
	sum += *widths;
    return (sum);
}

static void
free_sortnode(sort_node_t *node)
{
    if (node->fattr != NULL)
	FreeMem(node->fattr, sizeof (fileattr_t));
    FreeMem(node->fib, sizeof (FileInfoBlock_3_t));
    FreeMem(node->name, strlen(node->name) + 1);
    FreeMem(node->shortname, strlen(node->shortname) + 1);
    FreeMem(node, sizeof (sort_node_t));
}

static void
sortlist_process(void)
{
    sort_node_t *temp;
    sort_node_t *parent;
    int widths[MAX_COLUMNS];
    int columns;
    int count = list_count();
    int add_width = 2;
    int total_width;
    int	column;
    int per_column;
    int	is_dir;

    if (count == 0)
	return;

    if (lflag || gflag || ONEflag) {
	columns = 1;
    } else {
	if (iflag)
	    add_width += 7;  /* inode number */
	if (pflag || Fflag)
	    add_width++;     /* dir or file type flag */
	if (sflag)
	    add_width += 5;  /* block size flag */

	columns = (count < MAX_COLUMNS) ? count : MAX_COLUMNS;
	if (columns == 0)
	    columns = 1;
	/*
	 * The below should be much smarter, such as binary search to get
	 * close (within 4), then walk from there.
	 */
	for (; columns > 1; columns--) {
	    per_column = (count + columns - 1) / columns;
	    total_width = get_widths(per_column, add_width, widths);
	    if (total_width <= MAX_COLUMNS)
		break;
	}

	per_column = (count + columns - 1) / columns;
	(void) get_widths(per_column, add_width, widths);
    }

    parent   = NULL;
    temp     = sortlist;
    column   = 0;
    while (count--) {
	sort_node_t *next = temp->next;
	if (parent == NULL)
	    sortlist = next;
	else
	    parent->next = next;

	is_dir = print_fib(temp->fib, temp->mode, temp->fattr, widths[column],
			   temp->name);
	if (is_dir && Rflag)
	    rlist_add(temp->name);

	free_sortnode(temp);

	if (++column == columns) {
first_column:
	    parent = NULL;
	    column = 0;
	    temp   = sortlist;
	    per_column--;
	    putchar('\n');
	} else {
	    int cur;
	    for (cur = 1, temp = next; cur < per_column; cur++) {
		if (temp == NULL)
		    break;
		parent = temp;
		temp = temp->next;
	    }
	    if (temp == NULL)
		goto first_column;
	}
    }
    if (column > 0)
	putchar('\n');
}

static void
rlist_add(const char *filename)
{
    recursive_node_t *node;

    node = (recursive_node_t *) malloc(sizeof (recursive_node_t));
    node->filename = (char *) strdup(filename);
    node->next = NULL;

    if (rlist == NULL)
	rlist = node;
    else
	rlist_tail->next = node;
    rlist_tail = node;
}

static void
rlist_process(void)
{
    recursive_node_t *temp;

    while (rlist != NULL) {
	temp = rlist;
	rlist = rlist->next;
	if (aflag || (temp->filename[0] != '.'))
	    listname(temp->filename);
	free(temp->filename);
	free(temp);
    }
    rlist_tail = NULL;
}

static void
print_usage(const char *progname)
{
    fprintf(stderr, "%s\n"
	    "usage: %s [-aAcdFghilnprRstu1] [filename...]\n"
	    "       where -a is show files which begin with a '.'\n"
	    "             -A is show AmigaDOS (not UNIX) permissions\n"
	    "             -c is sort by last inode change time\n"
	    "             -d is do not examine directory contents\n"
	    "             -f is do not sort, show all entries\n"
	    "             -F is add indicator for file type\n"
	    "             -g is show file groups\n"
	    "             -h is show this help\n"
	    "             -i is show file inode number\n"
	    "             -l is show long listing\n"
	    "             -n is show numerical userid and group\n"
	    "             -p is put slash after directory names\n"
	    "             -r is reverse sort order\n"
	    "             -R is show recursive listing of directory\n"
	    "             -s is show file sizes in blocks\n"
	    "             -t is sort by last modify time\n"
	    "             -T is show full time (yyyy-mm-dd hh:mm:ss)\n"
	    "             -u is sort by last access time\n"
	    "             -1 is list one file per line\n",
	    version + 7, progname);
    exit(0);
}

/*
 * sortlist_or_rlist_add() - Add the specified name to either the sortlist
 *			     (if a file) or the rlist (if a directory).
 *			     This is done for argument names entered at the
 *			     command line.
 */
static void
sortlist_or_rlist_add(const char *name)
{
    BPTR               lock;
    FileInfoBlock_3_t *fib;
    fileattr_t	      *fattr = NULL;
    ulong	       fmode = 0;

    /* Always reset this flag when handling a new path from the cmdline */
    has_ExObject = 1;

    fib = (FileInfoBlock_3_t *)
	   AllocMem(sizeof (FileInfoBlock_3_t), MEMF_PUBLIC);
    lock = Lock((char *)name, ACCESS_READ);
    if (lock == 0) {
	fprintf(stderr, "%s not found\n", name);
	return;
    }
    if (has_ExObject) {
	fattr = (fileattr_t *) AllocMem(sizeof (fileattr_t), MEMF_PUBLIC);
	if (fattr != NULL) {
	    if (PExObject(lock, fib, fattr, 0) == 0) {
		fmode = fattr->fa_mode;
	    } else {
		FreeMem(fattr, sizeof (fileattr_t));
		fattr = NULL;
	    }
	}
    }
    if (fattr == NULL) {
	if (!Examine(lock, (struct FileInfoBlock *) fib)) {
	    printf("Error examining %s\n", name);
	    UnLock(lock);
	    return;
	}

	has_ExObject = 0;  /* ExObject() failed this time or before */
	if (has_GetPerms)
	    if (GetPerms(name, &fmode) == ERROR_ACTION_NOT_KNOWN)
		has_GetPerms = 0;
    }
    UnLock(lock);

    if (dflag) {
	sortlist_add(name, fib->fib_FileName, fmode, fib, fattr);
	return;  /* Don't process as directories */
    }

    /* If this is a directory, add it to the recursive list */
    if (((fmode != 0) && mode_is_directory(fmode)) || fib_is_directory(fib)) {
	/* Found a directory */
	rlist_add(name);
	if (fattr != NULL)
	    FreeMem(fattr, sizeof (fileattr_t));
	FreeMem(fib, sizeof (FileInfoBlock_3_t));
    } else {  /* Otherwise, treat it as a file in the sort list */
	sortlist_add(name, fib->fib_FileName, fmode, fib, fattr);
    }
}


int
main(int argc, char *argv[])
{
    int index;
    char *ptr;

    for (index = 1; index < argc; index++) {
	ptr = argv[index];
	if (*ptr != '-') {
	    sortlist_or_rlist_add(ptr);
	    listed++;
	    continue;
	}
	for (ptr++; *ptr != '\0'; ptr++) {
	    switch (*ptr) {
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
		    Rflag = 0;  /* -d cancels -R */
		    break;
		case 'f':
		    fflag++;
		    aflag++;  /* Also show all entries */
		    break;
		case 'F':
		    Fflag++;
		    break;
		case 'g':
		    gflag++;
		    break;
		case 'h':
		case 'v':
		    print_usage(argv[0]);
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
		    if (!dflag)
			Rflag++;
		    break;
		case 's':
		    sflag++;
		    break;
		case 't':
		    tflag++;
		    break;
		case 'T':
		    Tflag++;
		    break;
		case 'u':
		    uflag++;
		    break;
		default:
		    fprintf(stderr, "flag %c not implemented\n", *ptr);
		    break;
	    }
	}
    }

    if (listed == 0)
	rlist_add("");

    while (sortlist != NULL)
	sortlist_process();
    rlist_process();

    exit(0);
}
