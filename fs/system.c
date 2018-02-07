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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/exec_protos.h>

#include "config.h"
#include "handler.h"
#include "superblock.h"
#include "ufs.h"
#include "ufs/inode.h"
#include "ufs/dir.h"
#include "fsmacros.h"
#include "packets.h"
#include "stat.h"
#include "bffs_dosextens.h"
#include "system.h"
#include "file.h"
#include "inode.h"
#include "dir.h"
#include "cache.h"
#include "unixtime.h"

#define ACCESS_INVISIBLE 0

extern int   fs_partition;
extern int   unix_paths;
extern char *handler_name;

char        *volumename;
struct stat *stat = NULL;

static void
fsname(struct DosInfo *info)
{
    int                len;
    char              *name;
    char              *fsmnt = superblock->fs_fsmnt;
    ULONG              count = 0;
    struct DeviceList *tmp;

    if (superblock == NULL) {
        PRINT2(("** superblock is null in fsname()\n"));
        sprintf(volumename, "%cNULL", 4);
        return;
    }
    PRINT(("fs last mounted as %s\n", superblock->fs_fsmnt));

    for (name = fsmnt; *name != '\0'; name++)
        if ((name - fsmnt >= MAXMNTLEN) || (*name < ' '))
            goto invalid_last_mount;

    while ((*name == '/' || *name == '\0') && (name > fsmnt))
        name--;
    while (name > fsmnt) {
        if (*name == '/')
            break;
        else
            name--;
    }
    if (*name == '/')
        name++;

    strcpy(volumename + 1, name);
    if (*name == '\0') {
invalid_last_mount:
        strcpy(volumename + 1, "BFFS0");
        volumename[strlen(volumename + 1)] = 'a' + fs_partition;
        len = strlen(volumename + 1) + 1;

        /* rename if already exists */
        Forbid();
search_again:
            tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
            while (tmp != NULL)
                if (streqv(((char *) BTOC(tmp->dl_Name)) + 1, volumename + 1)) {
                    sprintf(volumename + len, "%u", count++);
                    goto search_again;
                } else {
                    tmp = (struct DeviceList *) BTOC(tmp->dl_Next);
                }
        Permit();
    }

    volumename[0] = strlen(volumename + 1);
}

void
NewVolNode(void)
{
    struct DosInfo *info;

    if (VolNode != NULL) {
        PRINT2(("Attempted to create new volume node without releasing old\n"));
        return;
    }

    info = (struct DosInfo *)
           BTOC(((struct RootNode *) DOSBase->dl_Root)->rn_Info);

    VolNode = (struct DeviceList *)
              AllocMem(sizeof (struct DeviceList), MEMF_PUBLIC);

    if (VolNode == NULL) {
        PRINT2(("NewVolNode: unable to allocate %d bytes\n",
                sizeof (struct DeviceList)));
        return;
    }

    volumename = (char *) AllocMem(MAXMNTLEN, MEMF_PUBLIC);
    if (volumename == NULL) {
        PRINT2(("NewVolNode: unable to allocate %d bytes\n", MAXMNTLEN));
        FreeMem(VolNode, sizeof (struct DeviceList));
        VolNode = NULL;
        return;
    }

    fsname(info);

    unix_time_to_amiga_ds(DISK32(superblock->fs_time), &VolNode->dl_VolumeDate);
    VolNode->dl_Type     = DLT_VOLUME;
    VolNode->dl_Task     = DosPort;
    VolNode->dl_Lock     = 0;
    VolNode->dl_LockList = 0;

    /*
     * Randell Jesup said (22-Dec-1991) that if we want Workbench to recognize
     * BFFS, it might need to fake ID_DOS_DISK in response to ACTION_INFO.  It
     * needs to be faked for ACTION_DISK_INFO, not here.
     */
    VolNode->dl_DiskType = ID_BFFS_DISK;

    VolNode->dl_unused   = 0;
    VolNode->dl_Name     = CTOB(volumename);

    Forbid();
        VolNode->dl_Next = info->di_DevInfo;
        info->di_DevInfo = (BPTR) CTOB(VolNode);
    Permit();
}

void
RemoveVolNode(void)
{
    int                notremoved = 1;
    struct DeviceList *current;
    struct DeviceList *parent;
    struct DosInfo    *info;

    if (VolNode == NULL) {
        PRINT(("VolNode already removed\n"));
        return;
    }

    info   = (struct DosInfo *)
             BTOC(((struct RootNode *) DOSBase->dl_Root)->rn_Info);
    parent = NULL;

    Forbid();
        current = (struct DeviceList *) BTOC(info->di_DevInfo);
        while (current != NULL) {
            if (current == VolNode) {
                current->dl_Task = NULL;
                if (parent == NULL)
                    info->di_DevInfo = current->dl_Next;
                else
                    parent->dl_Next  = current->dl_Next;
                notremoved = 0;
            } else
                parent = current;
            current = (struct DeviceList *) BTOC(current->dl_Next);
        }
    Permit();

    if (notremoved)
        PRINT2(("Unable to find our VolNode to remove.\n"));

    FreeMem(BTOC(VolNode->dl_Name), MAXMNTLEN);
    FreeMem(VolNode, sizeof (struct DeviceList));

    VolNode = NULL;
}

void
close_files(void)
{
#ifndef RONLY
    BFFSLock_t *lock = NULL;

    if (VolNode == 0) {
        PRINT2(("volume was not opened\n"));
        return;
    }

    PRINT(("close_files\n"));
    lock = (BFFSLock_t *) BTOC(VolNode->dl_LockList);
    while (lock != NULL) {
        if (lock->fl_Fileh && (lock->fl_Fileh->access_mode == MODE_WRITE)) {
            PRINT(("write closing i=%d\n", lock->fl_Key));
            truncate_file(lock->fl_Fileh);
            lock->fl_Fileh->access_mode = MODE_READ;
        }
        lock = (BFFSLock_t *) BTOC(lock->fl_Link);
    }
#endif
}


#ifndef RONLY
void
truncate_file(BFFSfh_t *fileh)
{
    icommon_t *inode;

    if (fileh->access_mode == MODE_WRITE) {
        inode = inode_modify(fileh->real_inum);
        inode->ic_mtime = DISK32(unix_time());
        if (fileh->truncate_mode == MODE_TRUNCATE) {
            if (IC_SIZE(inode) > fileh->maximum_position) {
                SET_IC_SIZE(inode, fileh->maximum_position);
                file_blocks_deallocate(fileh->real_inum);
            }
            fileh->truncate_mode = MODE_UPDATE;
        }
        file_block_retract(fileh->real_inum);
        fileh->access_mode = MODE_READ;
    }
}
#endif

BFFSLock_t *
CreateLock(ULONG key, int mode, ULONG pinum, ULONG poffset)
{
    BFFSLock_t *lock   = NULL;
    int         access = 0;

    if (VolNode == NULL) {
        global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
        PRINT1(("device is not mounted\n"));
        return (NULL);
    }

    lock = (BFFSLock_t *) BTOC(VolNode->dl_LockList);
    while (lock != NULL) {
        if (lock->fl_Key == key) {
            access = lock->fl_Access;
            break;
        } else {
            lock = (BFFSLock_t *) BTOC(lock->fl_Link);
        }
    }

    if ((lock != NULL) && (access == 0))
        PRINT(("access on invisible lock\n"));

    switch (mode) {
        case EXCLUSIVE_LOCK:
            if (access) {  /* somebody else has a lock on it... */
                global.Res2 = ERROR_OBJECT_IN_USE;
                PRINT(("exclusive: lock is already exclusive\n"));
                return (NULL);
            }
            break;
        case ACCESS_INVISIBLE:
            if (access)
                PRINT(("invisible: access on locked file\n"));
            break;
        default:  /* C= told us if it's not EXCLUSIVE, it's SHARED */
            if (access == EXCLUSIVE_LOCK) {
                global.Res2 = ERROR_OBJECT_IN_USE;
                PRINT(("exclusive: lock is already shared\n"));
                return (NULL);
            }
            break;
    }

    lock = (BFFSLock_t *) AllocMem(sizeof (BFFSLock_t), MEMF_PUBLIC);
    if (lock == NULL) {
        global.Res2 = ERROR_NO_FREE_STORE;
        return (NULL);
    }

    lock->fl_Key        = key;
    lock->fl_Access     = mode;
    lock->fl_Task       = DosPort;
    lock->fl_Volume     = CTOB(VolNode);
    lock->fl_Pinum      = pinum;
    lock->fl_Poffset    = poffset;
    lock->fl_Fileh      = NULL;

#ifdef CREATELOCK_DEBUG
    PRINT(("CreateLock: inum=%d pinum=%d poffset=%d type=%s\n", key, pinum,
           poffset, ((mode == EXCLUSIVE_LOCK) ? "exclusive" : "shared")));
#endif

    Forbid();
        lock->fl_Link = VolNode->dl_LockList;
        VolNode->dl_LockList = CTOB(lock);
    Permit();

    return (lock);
}

int
ResolveColon(char *name)
{
    struct DosInfo    *info;
    struct DeviceList *tmp;
    BFFSLock_t        *fl;
    char              *pos;

    pos = strchr(name, ':');
    if (pos == NULL)
        return (0);
    if (pos == name)
        return (2);

    *pos = '\0';

    if ((streqv(volumename + 1, name)) || (streqv(handler_name, name))) {
        *pos = ':';
        return (2);
    }

    info = (struct DosInfo *)
           BTOC(((struct RootNode *) DOSBase->dl_Root)->rn_Info);

    tmp = (struct DeviceList *) BTOC(info->di_DevInfo);
    while (tmp != NULL) {
        if (tmp->dl_Type == 1) {
            PRINT(("%.*s ", ((char *) BTOC(tmp->dl_Name))[0],
                   ((char *) BTOC(tmp->dl_Name)) + 1));
            if (strnicmp(((char *) BTOC(tmp->dl_Name)) + 1, name,
                         ((char *) BTOC(tmp->dl_Name))[0]) == 0) {
                PRINT(("<- "));
                *pos = ':';
                fl = (BFFSLock_t *) BTOC(tmp->dl_Lock);
                global.Pinum    = fl->fl_Pinum;
                global.Poffset  = fl->fl_Poffset;
                return (fl->fl_Key);
            }
        }
        tmp = (struct DeviceList *) BTOC(tmp->dl_Next);
    }

    PRINT(("\n"));
    *pos = ':';
    return (2);
}

void
stats_init(void)
{
    int    current;
    int    size;
    ULONG *ptr;

    if (stat == NULL) {
        stat = (struct stat *) AllocMem(sizeof (struct stat), MEMF_PUBLIC);
        if (stat == NULL) {
            PRINT2(("** not enough memory for stats\n"));
            return;
        }
    }

    size = sizeof (struct stat) / sizeof (ULONG);
    current = size;
    ptr = (ULONG *) stat;

    for (; current; current--, ptr++)
        *ptr = 0;

    stat->size = size;
    stat->magic = ('B' << 24) | ('F' << 16) | ('F' << 8) | '\0';
}

#undef PARSE_DEBUG
#ifdef PARSE_DEBUG
#define PDPRINT(x) PRINT(x)
#else
#define PDPRINT(x)
#endif

/*
 * parent_parse()
 *      This routine will, given a BFFSLock and a path relative to that lock,
 *      return the parent inode number and component name in the parent inode
 *      directory.  If the return value from this function (inode number) is
 *      zero, then the path is unparseable.
 */
ULONG
parent_parse(BFFSLock_t *lock, char *path, char **name)
{
    ULONG  pinum = 0;
    int    slash = 0;
    char  *part;
    char  *sname;

    PRINT(("parent_parse l=%d path=%s\n", lock, path));

    if (pinum = ResolveColon(path)) {
        if ((sname = strchr(path, ':')) == NULL)
            pinum = 2;
        else
            sname++;
    } else {
        sname = path;
        if (lock)
            pinum = lock->fl_Key;
        else
            pinum = 2;
    }

    for (part = sname; *part != '\0'; part++);

    for (; part > sname; part--) {
        if ((*part == '/') || (*part == ':')) {
            slash = ((*part == ':') ? 2 : 1);
            *part = '\0';
            part++;
            break;
        }
    }

    PDPRINT(("part=%s sname=%s (dif=%d) pinum=%d   ",
             part, sname, part - sname, pinum));

    if (sname != part) {
        pinum = file_find(pinum, sname);
        if (!unix_paths && (part > sname + 1) && (*(part - 2) == '/'))
            pinum = dir_name_search(pinum, "..");
    } else if (*part == '/') {
        PDPRINT(("searching for parent of /\n"));
        if (unix_paths)
            pinum = 2;
        else
            pinum = dir_name_search(pinum, "..");
        PDPRINT(("parent pinum found is %d\n", pinum));
        part++;
    }

    if (slash)
        *(part - 1) = ((slash == 2) ? ':' : '/');

    *name = part;

    PDPRINT(("part=%s pinum=%d name=%s\n\n", part, pinum, *name));

    return (pinum);
}


/*
 * path_parse()
 *      This routine will, given a BFFSLock and a path relative to that lock,
 *      return the inode number of the file (or directory) pointed to by that
 *      combination.
 */
ULONG
path_parse(BFFSLock_t *lock, char *path)
{
    char    *sname;
    ULONG   inum = 0;

    PDPRINT(("path_parse l=%d path=%s addr=%x\n", lock, path, path));

    if (inum = ResolveColon(path)) {
        if (sname = strchr(path, ':')) {
            sname++;
        } else {
            sname = path;
            inum = 2;
        }
    } else {
        sname = path;
        if (lock) {
            inum            = lock->fl_Key;
            global.Pinum    = lock->fl_Pinum;
            global.Poffset  = lock->fl_Poffset;
        } else {
            inum = 2;
        }
    }

    return (file_find(inum, sname));
}

int
open_filesystem(void)
{
    if (inhibited) {
        global.Res1 = DOSFALSE;
        global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
        return (1);
    }

    PRINT(("open filesystem\n"));

    if (dev_openfail == 1)
        dev_openfail = open_device();
    if (dev_openfail != 1) {
        PRINT(("reading superblock\n"));
        dev_openfail = find_superblock();
        if (dev_openfail)
            strcpy(stat->disk_type, "No fs");
    } else {
        global.Res2 = ERROR_DEVICE_NOT_MOUNTED;
    }

    if (dev_openfail) {
        global.Res1 = DOSFALSE;
        global.Res2 = ERROR_NOT_A_DOS_DISK;
    } else {
        open_cache();
        RemoveVolNode();
        NewVolNode();
    }
    motor_off();
    return (0);
}

void
close_filesystem(void)
{
    if (inhibited)
        return;

    PRINT(("close_filesystem\n"));

    if (superblock) {
#ifndef RONLY
        cache_flush();
        cache_cg_flush();
        superblock->fs_clean = 1;  /* unmounted clean */
        superblock->fs_fmod++;
        superblock_flush();
#endif
        close_cache();
        superblock_destroy();
        strcpy(stat->disk_type, "Unmounted");
    }
    if (motor_is_on)
        motor_off();
    close_device();
    dev_openfail = 1;

    RemoveVolNode();
}
