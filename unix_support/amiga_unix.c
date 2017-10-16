/* amiga_unix.c version 1.0
 *	This program is copyright (1996) Chris Hooper.  All code herein
 *	is freeware.  No portion of this code may be sold for profit.
 *
 * These routines were written for easier porting of UNIX-specific
 * applications to AmigaDOS.  Careful, they're full of magic goo.
 */

#include <stdio.h>

/* UNIX includes */
#include </ucbinclude/sys/types.h>	/* utimes, chown, mknod, mkfifo */
#include </ucbinclude/sys/stat.h>	/* chmod, mknod, mkfifo */
#include </ucbinclude/sys/time.h>	/* utimes */

/* Amiga includes */
#define DEVICES_TIMER_H
#define EXEC_TYPES_H

#include <dos30/dos.h>
#include <dos30/dosextens.h>
#include <exec/memory.h>

#define ulong unsigned long
#define CTOB(x) ((x)>>2)

int unix_debug = 0;

int utimes(file, tvp)
const char *file;
const struct timeval *tvp;
{
	struct timeval tval[3];
	int ctime;

	time(&ctime);
	ctime += 2922 * 86400;  /* Amiga - UNIX time difference */
	tval[0].tv_sec  = ctime;
	tval[0].tv_usec = 0;
	tval[1].tv_sec  = ctime;
	tval[1].tv_usec = 0;
	tval[2].tv_sec  = ctime;
	tval[2].tv_usec = 0;

	GetTimes(file, &tval[0], &tval[1], &tval[2]);
	if (tvp != NULL) {	/* set defined times */
	    tval[0].tv_sec  = tvp[0].tv_sec;
	    tval[0].tv_usec = tvp[0].tv_usec;
	    tval[1].tv_sec  = tvp[1].tv_sec;
	    tval[1].tv_usec = tvp[1].tv_usec;
	}

	if (unix_debug)
	    fprintf(stderr, "utimes %s %d.%d %d.%d %d.%d\n", file,
		    tval[0].tv_sec, tval[0].tv_usec,
		    tval[1].tv_sec, tval[1].tv_usec,
		    tval[2].tv_sec, tval[2].tv_usec);
	return(SetTimes(file, &tval[0], &tval[1], &tval[2]));
}

chmod(path, mode)
char *path;
mode_t mode;
{
	if (unix_debug)
	    fprintf(stderr, "chmod %s %04o\n", path, mode);
	return(SetPerms(path, mode));
}

chown(path, owner, group)
char *path;
int owner;
gid_t group;
{
	int value;

	if (owner == -1) {
	    if (group == -1)
		GetOwner(path, &value);
	    else {
		GetOwner(path, &value);
		value = (value & ~65535) | (group & 65535);
	    }
        } else {
	    if (group == -1) {
		GetOwner(path, &value);
		value = (owner << 16) | value & 65535;
	    } else
		value = (owner << 16) | (group & 65535);
	}

	if (unix_debug)
	    fprintf(stderr, "chown %s %d %d (%d %d)\n", path,
		    owner, group, value >> 16, value & 65535);
	return(SetOwner(path, value));
}

mknod(path, mode, dev)
char *path;
int mode;
int dev;
{
	int type;

	if (unix_debug)
	    fprintf(stderr, "mknod %s ", path);
	switch (mode & UNIX_IFMT) {
	    case UNIX_IFCHR:
		if (unix_debug)
		    fprintf(stderr, "C");
		type = ST_CDEVICE;
		break;
	    case UNIX_IFBLK:
		if (unix_debug)
		    fprintf(stderr, "B");
		type = ST_BDEVICE;
		break;
	    case UNIX_IFREG:
		if (unix_debug)
		    fprintf(stderr, "R");
		type = ST_FILE;
		break;
	    case UNIX_IFWHT:
		if (unix_debug)
		    fprintf(stderr, "W");
		type = ST_WHITEOUT;
		break;
	    default:
		if (unix_debug)
		    fprintf(stderr, "\n");
		fprintf(stderr, "Invalid mode %x for mknod of %s %x!\n",
			mode, path, dev);
		return(1);
	}

	if (unix_debug)
	    fprintf(stderr, " %04x %x\n", mode & ~S_IFMT, dev);

	return(MakeNode(path, type, dev, mode & ~S_IFMT));
}

mkfifo(path, mode)
char *path;
mode_t mode;
{
	if (unix_debug)
	    fprintf(stderr, "mkfifo %s %04o\n", path, mode);

	return(MakeNode(path, ST_FIFO, 0, mode & ~S_IFMT));
}

link(path1, path2)
char *path1;
char *path2;
{
	if (unix_debug)
	    fprintf(stderr, "link %s %s\n", path1, path2);

	return(PMakeLink(path1, path2, 0));
}

symlink(name1, name2)
char *name1;
char *name2;
{
	if (unix_debug)
	    fprintf(stderr, "symlink %s %s\n", name1, name2);

	return(PMakeLink(name1, name2, 1));
}

checkstack(minimum)
int minimum;
{
	int ssize;

	if (Cli())
		ssize = ((struct CommandLineInterface *) Cli())->cli_DefaultStack * 4;
	else
		ssize = ((struct Process *) FindTask(NULL))->pr_StackSize;

	if (ssize < minimum) {
		fprintf(stderr, "fatal: this program requires %d bytes of stack;\n",
			minimum);
		fprintf(stderr, "       it was given only %d bytes.\n", ssize);
		return(1);
	} else
		return(0);
}


/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

int MakeNode(name, type, device, mode)
char	*name;
ulong	type;
ulong	device;
ulong	mode;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

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
	packet->sp_Pkt.dp_Type         = ACTION_CREATE_OBJECT;
	packet->sp_Pkt.dp_Arg1         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);
	packet->sp_Pkt.dp_Arg3         = type;
	packet->sp_Pkt.dp_Arg4         = device;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
/*		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        } else {
		packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
		packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
		packet->sp_Pkt.dp_Port         = replyport;
		packet->sp_Pkt.dp_Type         = ACTION_SET_PERMS;
		packet->sp_Pkt.dp_Arg1         = (LONG) dlock;
		packet->sp_Pkt.dp_Arg2         = CTOB(buf);
		packet->sp_Pkt.dp_Arg3         = mode;
		packet->sp_Pkt.dp_Arg4         = device;


		PutMsg(msgport, (struct Message *) packet);

		WaitPort(replyport);
		GetMsg(replyport);

		if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
			SetIoErr(packet->sp_Pkt.dp_Res2);
			err = 1;
/*			fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
		}
	}

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}


int SetPerms(name, mode)
char	*name;
ulong	mode;
{
	int			err;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

	err = 0;
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
	packet->sp_Pkt.dp_Type         = ACTION_SET_PERMS;
	packet->sp_Pkt.dp_Arg1         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);
	packet->sp_Pkt.dp_Arg3         = mode;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
/*		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        }

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}


int SetOwner(name, owner)
char	*name;
ulong	owner;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

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
	packet->sp_Pkt.dp_Type         = ACTION_SET_OWNER;
	packet->sp_Pkt.dp_Arg1         = (LONG) NULL;
	packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = owner;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
/*		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        }

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}

int GetOwner(name, owner)
char	*name;
ulong	*owner;
{
	int			err = 0;
	struct Lock		*lock;
	struct FileInfoBlock	*fib;

	lock = (struct Lock *) Lock(name, ACCESS_READ);
	if (lock == NULL)
	    err = 1;
	else {
	    fib = (struct FileInfoBlock *)
		  AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC);
	    fib->fib_DiskKey = 0;
	    fib->fib_OwnerUID = 0;
	    fib->fib_OwnerGID = 0;
	    if (!Examine(lock, fib)) {
		err = 1;
		fprintf(stderr, "GetOwner: error examining %s\n", name);
	    } else
		*owner = (fib->fib_OwnerUID << 16) | fib->fib_OwnerGID;
	    FreeMem(fib, sizeof(struct FileInfoBlock));
	}

	return(err);
}


int SetTimes(name, atime, mtime, ctime)
char	*name;
ulong	*atime;
ulong	*mtime;
ulong	*ctime;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

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
	packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
	packet->sp_Pkt.dp_Arg1         = 4;	/* set last access */
	packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = (ULONG) atime;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
/*		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        } else {
		packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
		packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
		packet->sp_Pkt.dp_Port         = replyport;
		packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
		packet->sp_Pkt.dp_Arg1         = 0;	/* set last modify */
		packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
		packet->sp_Pkt.dp_Arg3         = CTOB(buf);
		packet->sp_Pkt.dp_Arg4         = (ULONG) mtime;

		PutMsg(msgport, (struct Message *) packet);

		WaitPort(replyport);
		GetMsg(replyport);

		if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
			SetIoErr(packet->sp_Pkt.dp_Res2);
			err = 1;
		} else {
			packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
			packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
			packet->sp_Pkt.dp_Port         = replyport;
			packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
			packet->sp_Pkt.dp_Arg1         = 2;	/* set create time */
			packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
			packet->sp_Pkt.dp_Arg3         = CTOB(buf);
			packet->sp_Pkt.dp_Arg4         = (ULONG) ctime;

			PutMsg(msgport, (struct Message *) packet);

			WaitPort(replyport);
			GetMsg(replyport);

			if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
				SetIoErr(packet->sp_Pkt.dp_Res2);
				err = 1;
			}
		}
	}

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}

int GetTimes(name, atime, mtime, ctime)
char	*name;
ulong	*atime;
ulong	*mtime;
ulong	*ctime;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	char			buf[512];

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
	packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
	packet->sp_Pkt.dp_Arg1         = 5;	/* get last access */
	packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg3         = CTOB(buf);
	packet->sp_Pkt.dp_Arg4         = (ULONG) atime;

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
/*		fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        } else {
		packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
		packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
		packet->sp_Pkt.dp_Port         = replyport;
		packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
		packet->sp_Pkt.dp_Arg1         = 1;	/* get last modify */
		packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
		packet->sp_Pkt.dp_Arg3         = CTOB(buf);
		packet->sp_Pkt.dp_Arg4         = (ULONG) mtime;

		PutMsg(msgport, (struct Message *) packet);

		WaitPort(replyport);
		GetMsg(replyport);

		if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
			SetIoErr(packet->sp_Pkt.dp_Res2);
			err = 1;
		} else {
			packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
			packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
			packet->sp_Pkt.dp_Port         = replyport;
			packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
			packet->sp_Pkt.dp_Arg1         = 3;	/* get create time */
			packet->sp_Pkt.dp_Arg2         = (LONG) dlock;
			packet->sp_Pkt.dp_Arg3         = CTOB(buf);
			packet->sp_Pkt.dp_Arg4         = (ULONG) ctime;

			PutMsg(msgport, (struct Message *) packet);

			WaitPort(replyport);
			GetMsg(replyport);

			if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
				SetIoErr(packet->sp_Pkt.dp_Res2);
				err = 1;
			}
		}
	}

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}

int PMakeLink(dname, sname, type)
char	*dname;
char	*sname;
ulong	type;
{
	int			err = 0;
	struct MsgPort		*msgport;
	struct MsgPort		*replyport;
	struct StandardPacket	*packet;
	struct Lock		*dlock;
	struct Lock		*lock;
	char			buf[1024];
	char			buf2[1024];

	msgport = (struct MsgPort *) DeviceProc(dname, NULL);
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

	strcpy(buf + 1, sname);
	buf[0] = strlen(sname);

	packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
	packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
	packet->sp_Pkt.dp_Port         = replyport;
	packet->sp_Pkt.dp_Type         = ACTION_MAKE_LINK;
	packet->sp_Pkt.dp_Arg1         = (LONG) dlock;
	packet->sp_Pkt.dp_Arg2         = CTOB(buf);

	if (type == 0) {	/* hard */
		lock = (struct Lock *) Lock(dname, ACCESS_READ);
		if (lock == NULL) {
			FreeMem(packet, sizeof(struct StandardPacket));
			DeletePort(replyport);
			fprintf(stderr, "MakeLink: hard source %s not found!\n", dname);
			return(1);
		}
		packet->sp_Pkt.dp_Arg3 = (LONG) lock;
	} else {		/* soft */
		strcpy(buf2 + 1, dname);
		buf2[0] = strlen(dname);
		packet->sp_Pkt.dp_Arg3 = CTOB(buf2);
	}
	packet->sp_Pkt.dp_Arg4         = type; /* 0=hard, 1=soft */

	PutMsg(msgport, (struct Message *) packet);

	WaitPort(replyport);
	GetMsg(replyport);

	if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
		SetIoErr(packet->sp_Pkt.dp_Res2);
		err = 1;
	}

	if (type == 0)
		UnLock(lock);

        FreeMem(packet, sizeof(struct StandardPacket));
        DeletePort(replyport);

	return(err);
}
