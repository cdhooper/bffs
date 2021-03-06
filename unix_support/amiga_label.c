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
#include <dos/dosextens.h>
#define _SYS_TIME_H_
#include <exec/types.h>
#include <sys/types.h>
#include <sys/disklabel.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/filehandler.h>

char    *strchr();
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

extern  char *dos_dev_name;
extern  char *disk_device;
extern  int   disk_unit;
extern  int   disk_flags;


#define BBSIZE 8192
#define SBSIZE 8192

int
dio_label(label)
struct disklabel *label;
{
    struct  DosLibrary *DosBase;
    struct  RootNode *rootnode;
    struct  DosInfo *dosinfo;
    struct  DevInfo *devinfo;
    struct  FileSysStartupMsg *startup;
    struct  DosEnvec *envec;
    char    *devname;
    char    *lookfor;
    char    *pos;
    int     notfound = 1;
    int     index;
    int     tempsize;

    if (dos_dev_name == NULL)
        return (1);

    lookfor = dos_dev_name;
    if ((pos = strchr(lookfor, ':')) != NULL)
        *pos = '\0';

    DosBase = (struct DosLibrary *) OpenLibrary("dos.library", 0L);
    if (DosBase == NULL)
        return (1);

    rootnode= DosBase->dl_Root;
    dosinfo = (struct DosInfo *) BTOC(rootnode->rn_Info);
    devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

    while (devinfo != NULL) {
        devname = (char *) BTOC(devinfo->dvi_Name);
        if (stricmp(devname + 1, lookfor) == 0) {
            notfound = 0;
            break;
        }
        devinfo = (struct DevInfo *) BTOC(devinfo->dvi_Next);
    }

    if (notfound) {
        fprintf(stderr, "%s: is not mounted.\n", lookfor);
        return (1);
    }

    startup = (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
    envec   = (struct DosEnvec *) BTOC(startup->fssm_Environ);

    printf("Autosizing disk label:  ");
    printf("sectors=%d tracks=%d cyl=%d\n", envec->de_BlocksPerTrack,
            envec->de_Surfaces, envec->de_HighCyl - envec->de_LowCyl + 1);

    label->d_magic          = DISKMAGIC;
    if (!strcmp("trackdisk.device", disk_device) ||
        !strcmp("mfm.device", disk_device) ||
        !strcmp("messydisk.device", disk_device)) {
        label->d_type   = DTYPE_FLOPPY;
        strcpy(label->d_typename, "CBM Floppy");
    } else if (!strcmp("hddisk.device", disk_device) && disk_unit < 3) {
        label->d_type   = DTYPE_ST506;
        strcpy(label->d_typename, "CBM 2090/2090A");
    } else {
        if (!strcmp("scsi.device", disk_device)) {
            strcpy(label->d_typename, "CBM SCSI/IDE");
        } else if (!strcmp("gvpscsi.device", disk_device)) {
            strcpy(label->d_typename, "GVP");
        } else {
            strcpy(label->d_typename, disk_device);
            for (lookfor = label->d_typename; *lookfor; lookfor++)
                if (*lookfor == '.')
                    *lookfor = '\0';
        }
        label->d_type = DTYPE_SCSI;
    }
    label->d_subtype     = 0;
    label->d_packname[0] = 0;

    tempsize = envec->de_SizeBlock * sizeof (long);

    switch (tempsize) {
        case 16384:
        case 8192:
        case 4096:
        case 2048:
        case 1024:
        case 512:
            label->d_secsize = tempsize;
            break;
        default:
            label->d_secsize        = TD_SECTOR;
            fprintf(stderr, "Warning: physical sector size %d ignored\n",
                    tempsize);
            break;
    }

    label->d_nsectors       = envec->de_BlocksPerTrack;
    label->d_ntracks        = envec->de_Surfaces;
    label->d_ncylinders     = envec->de_HighCyl - envec->de_LowCyl + 1;
    label->d_secpercyl      = envec->de_BlocksPerTrack * envec->de_Surfaces;
    label->d_secperunit     = label->d_secpercyl * label->d_ncylinders;

    label->d_sparespertrack = 0;
    label->d_sparespercyl   = 0;
    label->d_acylinders     = 0;
    label->d_rpm            = 3600;
    label->d_interleave     = envec->de_Interleave;
    if (label->d_interleave == 0)
        label->d_interleave++;
    label->d_trackskew      = 0;
    label->d_cylskew        = 0;
    label->d_headswitch     = 0;
    label->d_trkseek        = 0;
    label->d_flags          = 0;
    for (index = 0; index < NDDATA; index++)
        label->d_drivedata[index] = 0;
    for (index = 0; index < NSPARE; index++)
        label->d_spare[index] = 0;
    label->d_magic2         = DISKMAGIC;
    label->d_checksum       = 0;

    label->d_npartitions    = 1;
    label->d_bbsize         = BBSIZE;
    label->d_sbsize         = SBSIZE;

    for (index = 0; index < MAXPARTITIONS; index++) {
        label->d_partitions[index].p_size   = 0;
        label->d_partitions[index].p_offset = 0;
        label->d_partitions[index].p_fsize  = 0;
        label->d_partitions[index].p_fstype = 0;
        label->d_partitions[index].p_frag   = 0;
        label->d_partitions[index].p_cpg    = 0;
    }

    label->d_partitions[0].p_size = label->d_secperunit;

    CloseLibrary(DosBase);

    return (0);
}
