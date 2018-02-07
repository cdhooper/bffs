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
 */

#ifndef _PACKETS_H
#define _PACKETS_H

/* DOS lock with extensions of parent inum and parent offset */
typedef struct BFFSLock {
    BPTR            fl_Link;      /* next Dos Lock */
    LONG            fl_Key;       /* inode number */
    LONG            fl_Access;    /* 0=shared */
    struct MsgPort *fl_Task;      /* this handler's DosPort */
    BPTR            fl_Volume;    /* volume node of this handler */
    ULONG           fl_Pinum;     /* number of the parent inode */
    ULONG           fl_Poffset;   /* offset in parent where we can find entry */
    struct BFFSfh  *fl_Fileh;     /* filehandle (if exists) for this lock */
} BFFSLock_t;

/* File handle, used for file IO */
typedef struct BFFSfh {
    BFFSLock_t *lock;             /* pointer to lock defining filehandle */
    ULONG       current_position; /* current file byte to be accessed */
    ULONG       maximum_position; /* largest file byte accessed */
    int         access_mode;      /* 0=file has only been read so far */
    int         truncate_mode;    /* truncate at maximum position on close */
    int         real_inum;        /* actual inode number to use for I/O */
} BFFSfh_t;


/* Packet routines - specified by table code */
void PUnimplemented(void);
void PLocateObject(void);
void PExamineObject(void);
void PExamineNext(void);
void PFindInput(void);
void PFindOutput(void);
void PRead(void);
void PWrite(void);
void PSeek(void);
void PEnd(void);
void PParent(void);
void PDeviceInfo(void);
void PDeleteObject(void);
void PMoreCache(void);
void PRenameDisk(void);
void PSetProtect(void);
void PGetPerms(void);
void PSetPerms(void);
void PCurrentVolume(void);
void PCopyDir(void);
void PInhibit(void);
void PDiskChange(void);
void PFormat(void);
void PWriteProtect(void);
void PIsFilesystem(void);
void PDie(void);
void PFlush(void);
void PSameLock(void);
void PRenameObject(void);
void PCreateDir(void);
void PFilesysStats(void);
void PCreateObject(void);
void PMakeLink(void);
void PReadLink(void);
void PSetFileSize(void);
void PSetDate(void);
void PSetDates(void);
void PSetTimes(void);
void PSetOwner(void);
void PDiskType(void);
void PFhFromLock(void);
void PCopyDirFh(void);
void PParentFh(void);
void PExamineFh(void);
void PExamineAll(void);
void PAddNotify(void);
void PRemoveNotify(void);
void PNil(void);
void PFileSysStats(void);
void PFreeLock(void);
void PGetDiskFSSM(void);
void PFreeDiskFSSM(void);

/* key is or'd with MSb on first examine */
#define MSb 1<<31

/* C pointer to BCPL pointer */
#ifdef CTOB
#undef CTOB
#endif
#define CTOB(x) (((unsigned long) (x))>>2)

/* BCPL pointer to C pointer */
#ifdef BTOC
#undef BTOC
#endif
#define BTOC(x) ((void *)(((unsigned long) (x))<<2))

/* access modes */
#define MODE_READ       0
#define MODE_WRITE      1

/* truncate modes */
#define MODE_UPDATE     0
#define MODE_TRUNCATE   1

void FreeLock(BFFSLock_t *lock);

extern int og_perm_invert;  /* invert permissions on other/group */
extern int link_comments;   /* add link info to FIB comments */
extern int inode_comments;  /* add inode info to FIB comments */

#endif /* _PACKETS_H */
