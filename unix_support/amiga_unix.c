/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * These routines were written to make it easier to port UNIX-specific
 * applications to AmigaDOS.
 */

#include <stdio.h>
#include <string.h>

/* UNIX includes */
#include </ucbinclude/sys/types.h>
#include </ucbinclude/sys/stat.h>       /* chmod, mknod, mkfifo */
#include </ucbinclude/sys/time.h>       /* utimes */

/* Amiga includes */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <exec/memory.h>
#include "/fs/bffs_dosextens.h"

#define ulong unsigned long
#include "amiga_unix.h"

#define CTOB(x) ((x)>>2)

int unix_debug = 0;

int
GetOwner(const char *name, ulong *owner)
{
    int                err = 0;
    BPTR               lock;
    FileInfoBlock_3_t *fib;

    lock = Lock(name, ACCESS_READ);
    if (lock == 0) {
        err = 1;
    } else {
        fib = (FileInfoBlock_3_t *)
              AllocMem(sizeof (FileInfoBlock_3_t), MEMF_PUBLIC);
        fib->fib_DiskKey = 0;
        fib->fib_OwnerUID = 0;
        fib->fib_OwnerGID = 0;
        if (!Examine(lock, (struct FileInfoBlock *)fib)) {
            err = 1;
            fprintf(stderr, "GetOwner: error examining %s\n", name);
        } else {
            *owner = (fib->fib_OwnerUID << 16) | fib->fib_OwnerGID;
        }
        FreeMem(fib, sizeof (FileInfoBlock_3_t));
    }

    return (err);
}

int
GetTimes(const char *name, void *atime, void *mtime, void *ctime)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    char                   buf[512];

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
    packet->sp_Pkt.dp_Arg1         = 5;     /* get last access */
    packet->sp_Pkt.dp_Arg2         = dlock;
    packet->sp_Pkt.dp_Arg3         = CTOB(buf);
    packet->sp_Pkt.dp_Arg4         = (ULONG) atime;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
        SetIoErr(packet->sp_Pkt.dp_Res2);
        err = 1;
/*      fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
    } else {
        packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
        packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
        packet->sp_Pkt.dp_Port         = replyport;
        packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
        packet->sp_Pkt.dp_Arg1         = 1;     /* get last modify */
        packet->sp_Pkt.dp_Arg2         = dlock;
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
            packet->sp_Pkt.dp_Arg1         = 3; /* creation time */
            packet->sp_Pkt.dp_Arg2         = dlock;
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

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

int
MakeNode(const char *name, ulong type, ulong device, ulong mode)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    char                   buf[512];

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_CREATE_OBJECT;
    packet->sp_Pkt.dp_Arg1         = dlock;
    packet->sp_Pkt.dp_Arg2         = CTOB(buf);
    packet->sp_Pkt.dp_Arg3         = type;
    packet->sp_Pkt.dp_Arg4         = device;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
        SetIoErr(packet->sp_Pkt.dp_Res2);
        err = 1;
/*      fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
    } else {
        packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
        packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
        packet->sp_Pkt.dp_Port         = replyport;
        packet->sp_Pkt.dp_Type         = ACTION_SET_PERMS;
        packet->sp_Pkt.dp_Arg1         = dlock;
        packet->sp_Pkt.dp_Arg2         = CTOB(buf);
        packet->sp_Pkt.dp_Arg3         = mode;
        packet->sp_Pkt.dp_Arg4         = device;

        PutMsg(msgport, (struct Message *) packet);

        WaitPort(replyport);
        GetMsg(replyport);

        if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
            SetIoErr(packet->sp_Pkt.dp_Res2);
            err = 1;
/*          fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
        }
    }

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

int
SetPerms(const char *name, ulong mode)
{
    int                    err;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    char                   buf[512];

    err = 0;
    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
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
        SetIoErr(packet->sp_Pkt.dp_Res2);
        err = 1;
/*      fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
    }

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

int
SetOwner(const char *name, ulong owner)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    char                   buf[512];

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_SET_OWNER;
    packet->sp_Pkt.dp_Arg1         = (LONG) NULL;
    packet->sp_Pkt.dp_Arg2         = dlock;
    packet->sp_Pkt.dp_Arg3         = CTOB(buf);
    packet->sp_Pkt.dp_Arg4         = owner;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
        SetIoErr(packet->sp_Pkt.dp_Res2);
        err = 1;
/*      fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
    }

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

int
PMakeLink(const char *dname, const char *sname, ulong type)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    BPTR                   lock;
    char                   buf[1024];
    char                   buf2[1024];

    msgport = (struct MsgPort *) DeviceProc(dname);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket),
                      MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, sname);
    buf[0] = strlen(sname);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_MAKE_LINK;
    packet->sp_Pkt.dp_Arg1         = dlock;
    packet->sp_Pkt.dp_Arg2         = CTOB(buf);

    if (type == 0) {        /* hard */
        lock = Lock(dname, ACCESS_READ);
        if (lock == 0) {
            FreeMem(packet, sizeof (struct StandardPacket));
            DeletePort(replyport);
            fprintf(stderr, "MakeLink: hard source %s not found!\n", dname);
            return (1);
        }
        packet->sp_Pkt.dp_Arg3 = lock;
    } else {                /* soft */
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

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

int
SetTimes(const char *name, unix_timeval_t *atime, unix_timeval_t *mtime,
         unix_timeval_t *ctime)
{
    int                    err = 0;
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    BPTR                   dlock;
    char                   buf[512];

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
        return (1);

    dlock = CurrentDir(0);
    CurrentDir(dlock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
        fprintf(stderr, "Unable to create reply port\n");
        return (1);
    }

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        DeletePort(replyport);
        return (1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
    packet->sp_Pkt.dp_Arg1         = 4;     /* set last access */
    packet->sp_Pkt.dp_Arg2         = dlock;
    packet->sp_Pkt.dp_Arg3         = CTOB(buf);
    packet->sp_Pkt.dp_Arg4         = (ULONG) atime;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
        SetIoErr(packet->sp_Pkt.dp_Res2);
        err = 1;
/*      fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2); */
    } else {
        packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
        packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
        packet->sp_Pkt.dp_Port         = replyport;
        packet->sp_Pkt.dp_Type         = ACTION_SET_TIMES;
        packet->sp_Pkt.dp_Arg1         = 0;     /* set last modify */
        packet->sp_Pkt.dp_Arg2         = dlock;
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
            packet->sp_Pkt.dp_Arg1         = 2; /* create time */
            packet->sp_Pkt.dp_Arg2         = dlock;
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

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (err);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

int
utimes(const char *file, const unix_timeval_t *tvp)
{
    unix_timeval_t tval[3];
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
    if (tvp != NULL) {      /* set defined times */
        tval[0].tv_sec  = tvp[0].tv_sec;
        tval[0].tv_usec = tvp[0].tv_usec;
        tval[1].tv_sec  = tvp[1].tv_sec;
        tval[1].tv_usec = tvp[1].tv_usec;
    }

    if (unix_debug) {
        fprintf(stderr, "utimes %s %d.%d %d.%d %d.%d\n", file,
                tval[0].tv_sec, tval[0].tv_usec,
                tval[1].tv_sec, tval[1].tv_usec,
                tval[2].tv_sec, tval[2].tv_usec);
    }
    return (SetTimes(file, &tval[0], &tval[1], &tval[2]));
}

int
chmod(char *path, mode_t mode)
{
    if (unix_debug)
        fprintf(stderr, "chmod %s %04o\n", path, mode);
    return (SetPerms(path, mode));
}

int
chown(char *path, int owner, gid_t group)
{
    int value;

    if (owner == -1) {
        if (group == -1) {
            GetOwner(path, &value);
        } else {
            GetOwner(path, &value);
            value = (value & ~65535) | (group & 65535);
        }
    } else {
        if (group == -1) {
            GetOwner(path, &value);
            value = (owner << 16) | value & 65535;
        } else {
            value = (owner << 16) | (group & 65535);
        }
    }

    if (unix_debug)
        fprintf(stderr, "chown %s %d %d (%d %d)\n", path,
                owner, group, value >> 16, value & 65535);
    return (SetOwner(path, value));
}

int
mknod(char *path, int mode, int dev)
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
            return (1);
    }

    if (unix_debug)
        fprintf(stderr, " %04x %x\n", mode & ~S_IFMT, dev);

    return (MakeNode(path, type, dev, mode & ~S_IFMT));
}

int
mkfifo(char *path, mode_t mode)
{
    if (unix_debug)
        fprintf(stderr, "mkfifo %s %04o\n", path, mode);

    return (MakeNode(path, ST_FIFO, 0, mode & ~S_IFMT));
}

int
link(char *path1, char *path2)
{
    if (unix_debug)
        fprintf(stderr, "link %s %s\n", path1, path2);

    return (PMakeLink(path1, path2, 0));
}

int
symlink(char *name1, char *name2)
{
    if (unix_debug)
        fprintf(stderr, "symlink %s %s\n", name1, name2);

    return (PMakeLink(name1, name2, 1));
}

int
checkstack(int minimum)
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
        return (1);
    }
    return (0);
}
