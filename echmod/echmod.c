/* echmod.c version 1.0
 *      This program is copyright (1996) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

char *version = "\0$VER: echmod 1.0 (11.Aug.96) © 1996 Chris Hooper";

#include <stdio.h>

/* UNIX includes */
#ifndef AMIGA
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <ufs/inode.h>

#else

#include <sys/dir.h>

/* #include </ucbinclude/sys/types.h> /* utimes, chown, mknod, mkfifo */
#include </ucbinclude/sys/stat.h>  /* chmod, mknod, mkfifo */
/* #include </ucbinclude/sys/time.h>  /* utimes */

#include </ucbinclude/ufs/dinode.h>  /* inode info */

/* Amiga includes */
#define DEVICES_TIMER_H
#define EXEC_TYPES_H

#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <exec/memory.h>

#define CTOB(x) ((x)>>2)
int has_GetSetPerms = 1;

#endif AMIGA


#define ulong unsigned long

#define MODE_SET 0
#define MODE_ADD 1
#define MODE_CLR 2

char *progname;
#define CHMODMASK 07777
#define ANDVALUE 077777
#define ORVALUE 0
ulong andvalue = ANDVALUE;
ulong orvalue = ORVALUE;
int force = 0;
int recursive = 0;
int givex = 0;
int verbose = 0;


/* mode change mask possibilities */
#define CMASK_NONE	00000
#define CMASK_USER	05700
#define CMASK_GROUP	03070
#define CMASK_OTHER	01007
#define CMASK_ALL	07777

#define ALL_EXEC	00111
#define ALL_WRITE	00222
#define ALL_READ	00444

#define SUID_SGID	(ISUID | ISGID)

struct node {
	char *filename;
	struct node *next;
};

struct node *flist = NULL;

int parse_mode();
int filemode();
int not_octal();
int ovalue();
void print_usage();
void add_dir();
void handle_file();
void flist_add();

/*
int isdigit();
int printf();
int sscanf();
char *strdup();
void *malloc();
int fprintf();
int free();
*/

int main(argc, argv)
int  argc;
char *argv[];
{
	int	index;
	char	*ptr;
	struct	node *temp;

	progname = argv[0];

	for (index = 1; index < argc; index++) {
	    if (*argv[index] == '-') {
		for (ptr = argv[index] + 1; *ptr != '\0'; ptr++)
		    switch (*ptr) {
			case 'f':
			    force++;
			    break;
			case 'R':
			    recursive++;
			    break;
			case 'v':
			    verbose++;
			    break;
			default:
			    goto not_flag;
		    }
	    } else
		break;
	}

	not_flag:

	if (argc - index < 2)
	    print_usage();

	parse_mode(argv[index]);

	if (verbose > 2)
	    printf("andvalue=0%o  orvalue=0%o\n",
		    (unsigned) andvalue, (unsigned) orvalue);

	for (index++; index < argc; index++) {
	    handle_file(argv[index]);
	    while (flist != NULL) {
		temp = flist;
		flist = flist->next;
		handle_file(temp->filename);
		free(temp->filename);
		free(temp);
	    }
	}

	exit(0);
}


void handle_file(filename)
char *filename;
{
	mode_t mode;
	mode_t tmode;
	mode_t newmode;

	mode = filemode(filename);
	if (mode == 0)
		return;

	tmode = mode & CHMODMASK;

	/* recompute new flags */
	newmode = ((tmode & andvalue) | orvalue);

	if (newmode & ALL_EXEC)	/* if any exec bits set */
		newmode |= givex;


	if (verbose)
	    if (verbose > 1)
	        printf("%s [%o -> %o]\n", filename, tmode, newmode);
	    else if (tmode != newmode)
	        printf("%s [%o == %o]\n", filename, tmode , newmode);

	/* if different than original, then
	   do chmod system call, else leave alone */
	if (tmode != newmode)
	    if (chmod(filename, newmode) && !force)
		fprintf(stderr, "%s: unable to change mode of %s to %o\n",
			progname, filename, newmode);

	/* if dir and recursive, then readdir,
	   stuff in list */
	if (recursive && ((mode & IFMT) == IFDIR))
	    add_dir(filename);
}


#ifdef AMIGA
int filemode(filename)
char *filename;
{
	ULONG	mode = 0;
	ULONG	lock;
	struct	FileInfoBlock *fib;
	ULONG	temp;

	if (has_GetSetPerms) {
		temp = GetPerms(filename, &mode);
		if (temp == ERROR_ACTION_NOT_KNOWN)
			has_GetSetPerms = 0;
		else
			return(mode);
	}

	lock = Lock(filename, ACCESS_READ);
	if (lock == NULL) {
		fprintf(stderr, "%s: Error locating %s\n", progname, filename);
		return(0);
	}

	fib = (struct FileInfoBlock *)
		AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC);
	fib->fib_OwnerUID = 0L;
	fib->fib_OwnerGID = 0L;
	if (!Examine(lock, fib)) {
		fprintf(stderr, "Error examining %s\n", filename);
		return(0);
	}

	temp = fib->fib_DirEntryType;
	mode |= (temp == ST_FILE)	   ? IFREG : 0;
	mode |= (temp == ST_LINKFILE)	   ? IFREG : 0;
	mode |= (temp == ST_ROOT)	   ? IFDIR : 0;
	mode |= (temp == ST_USERDIR)	   ? IFDIR : 0;
	mode |= (temp == ST_LINKDIR)	   ? IFLNK : 0;
	mode |= (temp == ST_SOFTLINK)	   ? IFLNK : 0;
	mode |= (temp == ST_BDEVICE)	   ? IFBLK : 0;
	mode |= (temp == ST_CDEVICE)	   ? IFCHR : 0;
	mode |= (temp == ST_SOCKET)	   ? IFSOCK : 0;
	mode |= (temp == ST_FIFO)	   ? IFIFO : 0;
/*
	mode |= (temp == ST_LIFO)	   ? ILIFO : 0;
*/
	mode |= (temp == ST_WHITEOUT)	   ? IFWHT : 0;

	temp = fib->fib_Protection;
/*
	mode |= (temp & FIBF_ARCHIVE)     ? 0 : (IEXEC >> 3);
*/
	mode |= (temp & FIBF_SCRIPT)      ? ISUID : 0;
	mode |= (temp & FIBF_HIDDEN)      ? ISGID : 0;
	mode |= (temp & FIBF_PURE)        ? ISVTX : 0;
	mode |= (temp & FIBF_READ)        ? 0 : IREAD;
	mode |= (temp & FIBF_WRITE)       ? 0 : IWRITE;
	mode |= (temp & FIBF_DELETE)      ? 0 : IWRITE;
	mode |= (temp & FIBF_EXECUTE)     ? 0 : IEXEC;
	mode |= (temp & FIBF_GRP_READ)    ? 0 : (IREAD  >> 3);
	mode |= (temp & FIBF_GRP_WRITE)   ? 0 : (IWRITE >> 3);
	mode |= (temp & FIBF_GRP_DELETE)  ? 0 : (IWRITE >> 3);
	mode |= (temp & FIBF_GRP_EXECUTE) ? 0 : (IEXEC  >> 3);
	mode |= (temp & FIBF_OTR_READ)    ? 0 : (IREAD  >> 6);
	mode |= (temp & FIBF_OTR_WRITE)   ? 0 : (IWRITE >> 6);
	mode |= (temp & FIBF_OTR_DELETE)  ? 0 : (IWRITE >> 6);
	mode |= (temp & FIBF_OTR_EXECUTE) ? 0 : (IEXEC  >> 6);

	FreeMem(fib, sizeof(struct FileInfoBlock));
	UnLock(lock);

	return(mode);
}
#else
int filemode(filename)
char *filename;
{
	struct stat statbuf;

	if (stat(filename, &statbuf))
		return(0);

	return(statbuf.st_mode);
}
#endif



void add_dir(dirname)
char *dirname;
{
	DIR *dirp;
#ifdef AMIGA
	struct direct *dp;
#else
	struct dirent *dp;
#endif
	char buf[512];
	int len;
	char *ptr;


	if (verbose > 2)
	    printf("add_dir %s\n", dirname);

	sprintf(buf, "%s/", dirname);
	len = strlen(buf);
	ptr = buf + len;

	dirp = opendir(dirname);
	for (dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
	    strcpy(ptr, dp->d_name);
	    if ((ptr[0] == '.') && ((ptr[1] == '\0') ||
				    ((ptr[1] == '.') && (ptr[2] == '\0'))))
		continue;
	    flist_add(buf);
	}
}



int parse_mode(mode)
char *mode;
{
	char *ptr;
	int mask_mode = MODE_SET;
	int cmask;
	int alldefault;

	if (isdigit(*mode)) {
		andvalue = ~CMASK_ALL;
		orvalue = ovalue(mode);
		return(0);
	}

	ptr = mode;

	parse_loop:
	cmask = CMASK_NONE;

	for (; *ptr != '\0'; ptr++)
	    switch(*ptr) {
		case 'u':
		    cmask |= CMASK_USER;
		    break;
		case 'g':
		    cmask |= CMASK_GROUP;
		    break;
		case 'o':
		    cmask |= CMASK_OTHER;
		    break;
		case '-':
		case '+':
		case '=':
		    goto parse_next;
		default:
		    fprintf(stderr, "invalid parameter %c in mode %s\n",
			    *ptr, mode);
		    print_usage();
	}
	fprintf(stderr, "no operator (=, -, or +) specified!\n");
	print_usage();

	parse_next:
	if (cmask == CMASK_NONE) {
	    cmask = CMASK_ALL;
	    alldefault = 1;
	} else
	    alldefault = 0;

	for (; *ptr != '\0'; ptr++)
	    switch(*ptr) {
		case '-':
		    mask_mode = MODE_CLR;
		    break;
		case '+':
		    mask_mode = MODE_ADD;
		    break;
		case '=':
		    mask_mode = MODE_SET;
		    andvalue = ~cmask;
		    break;
		case 'r':
		    /* Give read permission */
		    if (mask_mode == MODE_CLR)
			andvalue &= ~(ALL_READ & cmask);
		    else
			orvalue  |=  (ALL_READ & cmask);
		    break;
		case 'w':
		    /* Give write permission */
		    if (mask_mode == MODE_CLR)
			andvalue &= ~(ALL_WRITE & cmask);
		    else
			orvalue  |=  (ALL_WRITE & cmask);
		    break;
		case 'x':
		    /* Give execute permission */
		    if (mask_mode == MODE_CLR)
			andvalue &= ~(ALL_EXEC & cmask);
		    else
			orvalue  |=  (ALL_EXEC & cmask);
		    break;
		case 'X':
		    /* Give execute permission if the file is a directory
		     * or if there is execute permission for one of the
		     * other user classes.
		     */
		    if (mask_mode == MODE_CLR) {
			andvalue &= ~(ALL_EXEC & cmask);
			givex = 0;
		    } else
			givex = ALL_EXEC & cmask;
		    break;
		case 's':
		    /* Set owner or group ID. This is only useful with
		     * u or g.  Also, the set group ID bit of a directory
		     * may only be modified with `+' or `-'
		     */

		   /*
		    * If compatibility with chmod is desired over the more
		    * reasonable +s setting owner SUID bit, uncomment below.
		    *
		    if (alldefault)
			break;
		    */

		    if (alldefault)
			if (mask_mode == MODE_CLR)
			    andvalue &= ~(ISUID & cmask);
			else
			    orvalue  |=  (ISUID & cmask);
		    else
			if (mask_mode == MODE_CLR)
			    andvalue &= ~(SUID_SGID & cmask);
			else
			    orvalue  |=  (SUID_SGID & cmask);
		    break;
		case 't':
		    /* Set the sticky bit to save program text between
		     * processes.
		     */
		    if (mask_mode == MODE_CLR)
			andvalue &= ~ISVTX;
		    else
			orvalue  |=  ISVTX;
		    break;
		case ',':
		    /* Start another mode operation */
		    ptr++;
		    goto parse_loop;
		default:
		    fprintf(stderr, "invalid parameter %c in mode %s\n",
			    *ptr, mode);
		    print_usage();
	    }
	return(1);
}


int ovalue(mode)
char *mode;
{
	int value;

	if (not_octal(mode)) {
	    fprintf(stderr, "%s: invalid mode %s\n", progname, mode);
	    exit(1);
	}

	sscanf(mode, "%o", &value);
	return(value);
}

int not_octal(ptr)
char *ptr;
{
	if (*ptr == '\0')
		return(1);

	while (*ptr != '\0') {
		if ((*ptr < '0') || (*ptr > '7'))
		    return(1);
		ptr++;
	}

	return(0);
}

void print_usage()
{
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "usage:  %s options perms filename [filename filename ...]\n",
		progname);
	fprintf(stderr, "        options are of the set [-Rfv], where -R is recursive,\n");
	fprintf(stderr, "            -f is force, and -v is verbose.\n");
	fprintf(stderr, "        perms are either an octal value representing the value\n");
	fprintf(stderr, "            of the permissions to set, or is in character format\n");
	fprintf(stderr, "            [[ugo][-+=][rwxXst]]|####]\n");
	fprintf(stderr, "        filename is the file to change\n");
	exit(1);
}

void flist_add(filename)
char *filename;
{
	struct node *temp;

	temp = (struct node *) malloc(sizeof(struct node));
	temp->filename = (char *) strdup(filename);
	temp->next = flist;
	flist = temp;
}

#ifdef AMIGA

chmod(path, mode)
char *path;
mode_t mode;
{
	if (verbose > 1)
	    fprintf(stderr, "chmod %s %04o\n", path, mode);
	return(SetPerms(path, mode));
}



/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/


int SetPerms(name, mode)
char	*name;
ulong	mode;
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
	packet->sp_Pkt.dp_Type         = ACTION_SET_PERMS;
	packet->sp_Pkt.dp_Arg1         = dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);
	packet->sp_Pkt.dp_Arg3         = mode;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	    err = 1;
	    if (!force)
		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2);
        }

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
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

	return(err);
}

#endif AMIGA
