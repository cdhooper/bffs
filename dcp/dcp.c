/*
 * File/Device copier by Chris Hooper (amiga@cdh.eebugs.com) on
 *      5-Jul-1993  v1.0
 *     14-Oct-1993  v1.1
 *     08-Nov-1993  v1.2
 *     27-Jan-1996  v1.21
 *     08-Feb-2018  v1.3
 *      6-Mar-2022  v1.4
 *
 * This program can copy files to Amiga devices and devices to files, etc.
 *
 * Usage:
 *    dcp {source} {dest} [-m <number of blocks>] [-b <buffer size>]
 *                        [-ss <source start block>] [-ds <dest start block>]
 *                        [-d dryrun] [-h help] [-a async] [-v verbose]
 *
 * This product is distributed as freeware with no warranties expressed or
 * implied. There are no restrictions on distribution or application of
 * this program other than it may not be used in contribution with or
 * converted to an application under the terms of the GNU Public License
 * nor sold for profit greater than the cost of distribution. Also, this
 * notice must accompany all redistributions and/or modifications of this
 * program. Remember: If you expect bugs, you should not be disappointed.
 *
 *
 * 1996 thanks to Christian Brandel for code correction  isdigit() -> !isalpha()
 */

const char *version = "\0$VER: dcp 1.4 (06-Mar-2022) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <exec/types.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/dos_protos.h>
#include <exec/io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <exec/memory.h>
#include <lib/muldiv.h>

/* Possible file types
 *      tNONE   0 = Could not be opened
 *      tDEVICE 1 = Block device opened
 *      tFILE   2 = Regular AmigaDOS file opened
 *      tSTDIO  3 = STDIO channel specified
 *      tNIL    4 = No output device
 */
#define tNONE   0
#define tDEVICE 1
#define tFILE   2
#define tSTDIO  3
#define tNIL    4

#define dREAD   0
#define dWRITE  1

#ifndef TD_SECTOR
#define TD_SECTOR   512
#endif
#ifndef TD_SECSHIFT
#define TD_SECSHIFT 9
#endif

#define TD_READ64   24
#define TD_WRITE64  25
#define TD_SEEK64   26
#define TD_FORMAT64 27

/* BCPL conversion functions */
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)


struct file_info {
    char    *name;               /* filename as specified on cmdline */
    FILE    *strIO;              /* stream file pointer */
    struct  IOExtTD *trackIO[2]; /* device packet pointer */
    ULONG   startblk;            /* start read/write block in device/file */
    ULONG   currentblk;          /* current read/write block in device/file */
    ULONG   maxblk;              /* max allowed read/write blk in dev/file */
    ULONG   blocksize;           /* device physical block size */
    ULONG   blockshift;          /* bits to shift for device block size */
    ULONG   blocks_per_bufblock; /* device physical blocks per buffer block */
    UBYTE   dtype;               /* NONE, DEVICE, FILE, or STDIO */
    UBYTE   io_pending[2];       /* 1=need IO completion wait before using */
    UBYTE   warned;              /* user warned of failure already */
    UBYTE   maxblk_devlimit;     /* maximum block number limited by device */
};

struct file_info in;               /* internal input file descriptor */
struct file_info out;              /* internal output file descriptor */

char    *progname;                 /* name of this program running */
char    iobuf[128];                /* buffer for line-oriented I/O */
int     last_read_blocks[2];       /* number of blocks last read */
char    *buffer[2];                /* buffer area for copy operations */
int     buffer_blocks = 2000;      /* size of buffer area */
int     async         = 0;         /* async dbuffer - default = off */
int     async_mode    = 0;         /* async mode    - 0=awrite, 1=aboth */
int     verbose       = 0;         /* verbose mode  - default = off */
int     dryrun        = 0;         /* dryrun mode   - default = off */
int     report_perf   = 0;         /* show perf #   - default = off */
int     buf_blocksize = TD_SECTOR; /* minimum transfer size */
ULONG   maxblk        = 0;         /* maximum block - default = none */
ULONG   source_start  = 0;         /* src start blk - default = zero */
ULONG   dest_start    = 0;         /* dst start blk - default = zero */

/* quick parameter parser */
#define ACCEPT(var, match) accept_arg(argc, argv, ptr, &index, &var, match)

static int
accept_arg(int argc, char *argv[], char *ptr, int *index, int *var,
           const char *match)
{
    char *ptre = strchr(ptr, '=');
    if (ptre != NULL)
        *ptre = '\0';

    if (stricmp(ptr, match) == 0) {
        if (ptre != NULL) {
            *ptre = '=';
            ptr = ptre + 1;
        } else {
            (*index)++;
            ptr = argv[*index];
        }
        if ((*index > argc) ||
            (sscanf(ptr, "%d", var) != 1)) {
            fprintf(stderr, "number required for -%s parameter\n", match);
            exit(1);
        }
        return (1);
    }
    if (ptre != NULL)
        *ptre = '=';
    return (0);
}

static void
print_usage(void)
{
    fprintf(stderr,
            "%s %.3s by Chris Hooper on %.11s\n"
            "Usage: %s [options] {source} {dest}\n"
            "\t  -a     = asynchronous write (-aa = full asynchronous)\n"
            "\t  -b <n> = buffer size in blocks\n"
            "\t  -m <n> = maximum blocks to transfer\n"
            "\t -ds <n> = destination block number at which to start\n"
            "\t -ss <n> = source block number at which to start\n"
            "\t-dbs <n> = destination block size in bytes\n"
            "\t-sbs <n> = source block size in bytes\n"
            "\t  -r     = report performance\n"
            "\t  -v     = verbose mode\n"
            "\t  -h     = give more help\n",
            progname, version + 11, version + 16, progname);
    exit(1);
}

static void
print_help(void)
{
    fprintf(stderr,
        "%s is a utility which does block copies from the specified"
        " source\n"
        "to the specified destination. The source and destination may each\n"
        "independently be either a file or a block device.\n"
        "\n"
        "The source and destination are specified as filenames. A device is\n"
        "differentiated from a file as having a colon (:) as the last"
        " character\n"
        "of the filename. A device may also be specified by giving the Amiga\n"
        "device information, separated by commas (,). The source is always\n"
        "specified first. WARNING: A comma makes it a device!"
        "\n"
        "Examples of usage:\n"
        "\n"
        "Copy file \"unix_fs\" from dh0: and overwrite df0: with that file:\n"
        "\t%s dh0:unix_fs df0:\n"
        "\n"
        "Copy 8192 blocks from raw SCSI disk, unit 6 starting at block 16384\n"
        "and write that to file \"junk\" in current directory:\n"
        "\t%s -ss 16384 -m 8192 scsi.device,6 junk\n"
        "\n"
        "Copy partition BSDR: to a file \"BSD_root\" in current directory:\n"
        "\t%s bsdr: BSD_root\n"
        "\n"
        "Copy first five blocks from partition BAK: to block 1532 in"
        " partition TEST:\n"
        "\t%s -m 5 -ds 1532 BAK: Test:\n",
        progname, progname, progname, progname, progname);
    exit(1);
}

static int
do_wait(struct file_info *fdes, int bufnum)
{
    int rc = 0;

    switch (fdes->dtype) {
        case tNONE:  /* Open failed */
        case tNIL:
        case tSTDIO:
        case tFILE:
            /* These are all synchronous */
            break;
        case tDEVICE:
            if (fdes->io_pending[bufnum]) {
                fdes->io_pending[bufnum] = 0;
                rc = WaitIO((struct IORequest *) fdes->trackIO[bufnum]);
                if (rc != 0) {
                    UWORD cmd = fdes->trackIO[bufnum]->iotd_Req.io_Command;
                    int was_read = ((cmd == CMD_READ) || (cmd == TD_READ64));
                    fprintf(stderr, "Error on %s: %u at blk %u\n",
                            was_read ? "read" : "write", rc, fdes->currentblk);
                }
            }
            break;
    }
    return (rc);
}

static void
do_close(struct file_info *fdes)
{
    switch (fdes->dtype) {
        case tNONE:  /* Open failed */
        case tNIL:
        case tSTDIO:
            break;
        case tFILE:
            if (!dryrun)
                fclose(fdes->strIO);
            break;
        case tDEVICE:
            if (fdes->trackIO[0] != NULL) {
                /* Device was previously opened */
                struct MsgPort *temp;
                int i;

                for (i = 0; i <= async; i++)
                    do_wait(fdes, i);

                /* first shut the motor off */
                fdes->trackIO[0]->iotd_Req.io_Command = TD_MOTOR;
                fdes->trackIO[0]->iotd_Req.io_Flags   = 0x0;  /* Motor status */
                fdes->trackIO[0]->iotd_Req.io_Length  = 0;    /* 0=off, 1=on */
                DoIO((struct IORequest *)fdes->trackIO[0]);

                CloseDevice((struct IORequest *)fdes->trackIO[0]);

                temp = fdes->trackIO[0]->iotd_Req.io_Message.mn_ReplyPort;
                for (i = 0; i <= async; i++)
                    DeleteExtIO((struct IORequest *)fdes->trackIO[i]);

                DeletePort(temp);
                fdes->trackIO[0] = NULL;
                fdes->trackIO[1] = NULL;
            }
            break;
    }
}

/*
 * do_io() performs I/O to or from a device. It returns 0 on success and
 *         non-zero on failure.
 */
static int
do_io(struct file_info *fdes, int rwtype, int bufnum)
{
    int io_left = 0;
    int last_write_blks;

    if (rwtype == dREAD) {
        /* Read from file/device */
        io_left = buffer_blocks * fdes->blocks_per_bufblock;

        if (fdes->maxblk != 0)
            if (io_left > (fdes->maxblk - fdes->currentblk))
                io_left = fdes->maxblk - fdes->currentblk;
#undef DEBUG
#ifdef DEBUG
        fprintf(stderr, "tran=R blocks=%d cur=%u max=%u left=%d\n",
                buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
    } else {
        /* Write to file/device */
        io_left = last_read_blocks[bufnum] * fdes->blocks_per_bufblock;

        if (fdes->maxblk != 0)
            if (io_left > (fdes->maxblk - fdes->currentblk))
                io_left = fdes->maxblk - fdes->currentblk;
#ifdef DEBUG
        fprintf(stderr, "tran=W blocks=%d cur=%u max=%u left=%d\n",
                buffer_blocks, fdes->currentblk, fdes->maxblk, io_left);
#endif
    }
    if (io_left < 1) {
        /* Nothing more to transfer */
        if (fdes->maxblk_devlimit)
            return (1);
        else
            return (-1);
    }

    if (verbose) {
        char ch = '\r';
        if (rwtype == dWRITE)
            ch = ' ';
        fprintf(stderr, "%c%s at %-8d  count=%-8d",
                ch, (rwtype == dREAD) ? "Read " : "Write",
                fdes->currentblk, io_left);
        fflush(stderr);
#ifdef DEBUG
        fprintf(stderr, "\n");
#endif
    }

    if (rwtype == dREAD)
        last_read_blocks[bufnum] = io_left / fdes->blocks_per_bufblock;

    if (dryrun) {
        fdes->currentblk += io_left;
        return (0);
    }

    switch (fdes->dtype) {
        case tDEVICE: {
            ULONG blk    = fdes->currentblk;
            ULONG high32 = blk >> (32 - fdes->blockshift);
            UWORD cmd;

            if (high32) {
                fdes->trackIO[bufnum]->iotd_Req.io_Actual = high32;
                if (rwtype == dREAD)
                    cmd = TD_READ64;
                else
                    cmd = TD_WRITE64;
            } else {
                fdes->trackIO[bufnum]->iotd_Req.io_Actual = 0;
                if (rwtype == dREAD)
                    cmd = CMD_READ;
                else
                    cmd = CMD_WRITE;
            }
            fdes->trackIO[bufnum]->iotd_Req.io_Command = cmd;
            fdes->trackIO[bufnum]->iotd_Req.io_Data = buffer[bufnum];
            fdes->trackIO[bufnum]->iotd_Req.io_Length = io_left << fdes->blockshift;
            fdes->trackIO[bufnum]->iotd_Req.io_Offset = blk << fdes->blockshift;

            if (async) {
                SendIO((struct IORequest *) fdes->trackIO[bufnum]);
                (fdes->io_pending)[bufnum] = 1;
                break;
            }
            if ((DoIO((struct IORequest *) fdes->trackIO[bufnum])) != 0) {
                if (fdes->warned == 0) {
                    fdes->warned = 1;
                    fprintf(stderr, "Blk %u I/O failure\n", blk);
                    if (high32)
                        fprintf(stderr, "Device might not support TD64\n");
                }
                return (1);
            }
            break;
        }
        case tFILE:
        case tSTDIO:
            if (rwtype == dREAD) {
                last_read_blocks[bufnum] =
                        fread(buffer[bufnum], fdes->blocksize, io_left,
                              fdes->strIO) / fdes->blocks_per_bufblock;
            } else {
                last_write_blks = fwrite(buffer[bufnum], fdes->blocksize,
                                         io_left, fdes->strIO);
                if (last_write_blks != io_left) {
                    if (last_write_blks >= 0) {
                        fprintf(stderr,
                                "destination full before source exhausted.\n");
                    } else {
                        fprintf(stderr, "write did not finish normally: "
                                "%d of %d blocks written\n",
                                last_write_blks, io_left);
                    }
                    return (1);
                }
            }
            break;
        case tNIL:
            break;
        case tNONE:  /* Open failed */
            break;
    }

    fdes->currentblk += io_left;

    return (0);
}

static struct FileSysStartupMsg *
find_startup(char *name)
{
    struct FileSysStartupMsg *startup;
    struct DosLibrary        *DosBase;
    struct RootNode          *rootnode;
    struct DosInfo           *dosinfo;
    struct DevInfo           *devinfo;
    char                     *devname;
    char                     *pos;
    int                       notfound = 1;

    if ((pos = strchr(name, ':')) != NULL)
        *pos = '\0';

    DosBase  = (struct DosLibrary *) OpenLibrary("dos.library", 0L);

    rootnode = DosBase->dl_Root;
    dosinfo  = (struct DosInfo *) BTOC(rootnode->rn_Info);
    devinfo  = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);

    while (devinfo != NULL) {
        devname = (char *) BTOC(devinfo->dvi_Name);
        if (stricmp(devname + 1, name) == 0) {
            notfound = 0;
            break;
        }
        devinfo = (struct DevInfo *) BTOC(devinfo->dvi_Next);
    }

    if (notfound) {
        startup = NULL;
    } else {
        startup = (struct FileSysStartupMsg *) BTOC(devinfo->dvi_Startup);
        if (startup == NULL) {
            /* Try searching for a volume which matches the task pointer */
            APTR task = devinfo->dvi_Task;
            devinfo  = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);
            while (devinfo != NULL) {
                if ((devinfo->dvi_Task == task) &&
                    (devinfo->dvi_Type == DLT_DEVICE))
                    break;
                devinfo = (struct DevInfo *) BTOC(devinfo->dvi_Next);
            }
            if (devinfo != NULL)
                startup = (struct FileSysStartupMsg *)
                    BTOC(devinfo->dvi_Startup);
        }
    }

done:
    CloseLibrary((struct Library *)DosBase);

    if (pos != NULL)
        *pos = ':';

    return (startup);
}

static void
print_error_opening(const char *name, int rwtype, const char *typename)
{
    fprintf(stderr, "Error opening %s%s for %s; ",
            typename, name, (rwtype == dREAD) ? "read" : "write");
}

static void
get_drive_geometry(struct file_info *fdes, ULONG *maxblk, ULONG *sectorsize)
{
    struct DriveGeometry dg;

    fdes->trackIO[0]->iotd_Req.io_Command = TD_GETGEOMETRY;
    fdes->trackIO[0]->iotd_Req.io_Length  = sizeof (dg);
    fdes->trackIO[0]->iotd_Req.io_Data    = &dg;
    if (DoIO((struct IORequest *)fdes->trackIO[0]) == 0) {
#ifdef DEBUG
        fprintf(stderr, "Drive geometry\n"
                "  SectorSize=%u\n"
                "  TotalSectors=%u\n"
                "  Cylinders=%u\n"
                "  CylSectors=%u\n"
                "  Heads=%u\n"
                "  TrackSectors=%u\n"
                "  BufMemType=%u\n"
                "  DeviceType=%u\n"
                "  Flags=%u\n"
                "  Reserved=%u\n",
                dg.dg_SectorSize, dg.dg_TotalSectors, dg.dg_Cylinders,
                dg.dg_CylSectors, dg.dg_Heads, dg.dg_TrackSectors,
                dg.dg_BufMemType, dg.dg_DeviceType, dg.dg_Flags,
                dg.dg_Reserved);
#endif
        if (dg.dg_TotalSectors != 0)
            *maxblk = dg.dg_TotalSectors;
        if (dg.dg_SectorSize != 0)
            *sectorsize = dg.dg_SectorSize;
    }
}

static void
set_maxblk_startblk(struct file_info *fdes, ULONG geom_maxblk,
                    ULONG geom_blocksize, ULONG req_blocksize)
{
    if (geom_blocksize != 0)
        fdes->blocksize = geom_blocksize;
    if (geom_maxblk != 0)
        fdes->maxblk = geom_maxblk;
    if (req_blocksize != 0) {
        /* Adjust addresses per difference in requested block size */
        if (fdes->blocksize > req_blocksize) {
            fdes->maxblk     *= (fdes->blocksize / req_blocksize);
            fdes->startblk   *= (fdes->blocksize / req_blocksize);
        } else {
            fdes->maxblk     /= (req_blocksize / fdes->blocksize);
            fdes->startblk   /= (req_blocksize / fdes->blocksize);
        }

        /* Take block size from user request */
        fdes->blocksize = req_blocksize;
    }
}

static int
do_open(struct file_info *fdes, int rwtype, int req_blocksize)
{
    int len;
    char *pos;
    struct MsgPort *temp;
    ULONG geom_maxblk = 0;
    ULONG geom_blocksize = 0;

    fdes->blocksize = req_blocksize; /* Start with user requested size */
    if (fdes->blocksize == 0)
        fdes->blocksize = TD_SECTOR;

    if (verbose)
        if (rwtype == dREAD)
            fprintf(stderr, "R ");
        else
            fprintf(stderr, "W ");

    if (!strcmp(fdes->name, "-")) {         /* stdio */
        fdes->dtype = tSTDIO;
        if (rwtype == dREAD)
            fdes->strIO = stdin;
        else
            fdes->strIO = stdout;
        if (verbose)
            fprintf(stderr, "STDIO %s\n", fdes->name);
        return (0);
    }

    len = strlen(fdes->name);
    if (len == 0) {
        print_error_opening(fdes->name, rwtype, "file ");
        fprintf(stderr, "empty name\n");
        fdes->dtype = tNONE;
        return (1);
    }

    pos = strchr(fdes->name, ',');

    if (!pos && (fdes->name[len - 1] != ':')) {     /* must be a file */
        if (rwtype == dREAD) {
            struct stat stat_buf;
            if (stat(fdes->name, &stat_buf) == 0) {
                geom_maxblk = (stat_buf.st_size + TD_SECTOR - 1) / TD_SECTOR;
                geom_blocksize = TD_SECTOR;
                fdes->maxblk_devlimit = 1;
            }
        }
        set_maxblk_startblk(fdes, geom_maxblk, geom_blocksize, req_blocksize);

        if (!dryrun)
            fdes->strIO = fopen(fdes->name, (rwtype == dREAD) ? "r" : "w");
        if ((fdes->strIO == NULL) && !dryrun) {
            print_error_opening(fdes->name, rwtype, "file ");
            fprintf(stderr, "errno %d\n", errno);
            fdes->dtype = tNONE;
            return (1);
        } else {
            fdes->dtype = tFILE;
            if (verbose) {
                fprintf(stderr, "file %-38s start=%-8u end=%-8u bs=%u\n",
                        fdes->name, fdes->startblk, fdes->maxblk,
                        fdes->blocksize);
            }
        }
    } else {                                /* must be a device */
        struct FileSysStartupMsg *startup;
        struct DosEnvec *envec;
        char *disk_device;
        int disk_unit, disk_flags;
        int i;

        /* first, check to make sure it's not the null device */
        if (stricmp(fdes->name, "nil:") == 0) {
            fdes->dtype      = tNIL;
            if (verbose)
                fprintf(stderr, "Nil:\n");
            set_maxblk_startblk(fdes, 0, 0, req_blocksize);
            return (0);
        }

        /* create port to use when talking with handler */
        temp = CreatePort(0, 0);
        if (temp == NULL) {
            fprintf(stderr, "Failed to create port for %s\n", fdes->name);
            fdes->dtype = tNONE;
            return (1);
        }

        for (i = 0; i <= async; i++) {
            fdes->trackIO[i] = (struct IOExtTD *)
                               CreateExtIO(temp, sizeof (struct IOExtTD));
            if (fdes->trackIO[i] == NULL) {
                fprintf(stderr, "Failed to create trackIO %s%s\n",
                        "structure for ", fdes->name);
                if (i == 1)
                    DeleteExtIO((struct IORequest *)fdes->trackIO[0]);
                fdes->trackIO[0] = NULL;
                DeletePort(temp);
                fdes->dtype = tNONE;
                return (1);
            }
        }

        if (pos != NULL) {
            /* Has a comma in the name */
            char *pos2;

            disk_device = fdes->name;
            disk_flags  = 0;

            *pos = '\0';

            if (!isalpha(*(pos + 1))) {
                disk_unit = atoi(pos + 1);
                pos2 = strchr(pos + 1, ',');
                if (pos2 != NULL) {
                    if (!isalpha(*(pos2 + 1))) {
                        char *pos3 = strchr(pos2 + 1, ',');
                        disk_flags = atoi(pos2 + 1);
                        if (pos3 != NULL) {
                            if (!isalpha(*(pos2 + 1))) {
                                if (req_blocksize == 0)
                                    req_blocksize = atoi(pos3 + 1);
                            } else {
                                print_error_opening(fdes->name, rwtype, "");
                                fprintf(stderr, "block size %s must be "
                                        "an integer!\n", pos3 + 1);
                                goto failed_open;
                            }
                        }
                    } else {
                        print_error_opening(fdes->name, rwtype, "");
                        fprintf(stderr, "flags %s must be an integer!\n",
                                pos2 + 1);
                        goto failed_open;
                    }
                }
            } else {
                print_error_opening(fdes->name, rwtype, "");
                fprintf(stderr, "you must specify a unit number!\n");
                goto failed_open;
            }
        } else {
            startup = find_startup(fdes->name);
            if (startup == NULL) {
                print_error_opening(fdes->name, rwtype, "device ");
                fprintf(stderr, "does not exist\n");
                goto failed_open;
            }

            disk_device      = ((char *) BTOC(startup->fssm_Device)) + 1;
            disk_unit        = startup->fssm_Unit;
            disk_flags       = startup->fssm_Flags;
            envec            = (struct DosEnvec *) BTOC(startup->fssm_Environ);
            fdes->startblk   = envec->de_LowCyl * envec->de_Surfaces *
                               envec->de_BlocksPerTrack;
            geom_maxblk      = (envec->de_HighCyl + 1) *
                               envec->de_Surfaces *
                               envec->de_BlocksPerTrack;
            geom_blocksize   = envec->de_SizeBlock * 4;

            fdes->maxblk_devlimit = 1;
        }

        for (i = 0; i <= async; i++) {
            if (OpenDevice(disk_device, disk_unit,
                           (struct IORequest *)fdes->trackIO[i], disk_flags)) {
                struct MsgPort *temp;
                fprintf(stderr, "fatal: Unable to open %s unit %d for %s.\n",
                        disk_device, disk_unit, fdes->name);
                if (i == 1)
                    CloseDevice((struct IORequest *)fdes->trackIO[0]);
failed_open:
                temp = fdes->trackIO[0]->iotd_Req.io_Message.mn_ReplyPort;
                for (i = 0; i <= async; i++) {
                    DeleteExtIO((struct IORequest *)fdes->trackIO[i]);
                    fdes->trackIO[i] = NULL;
                }
                DeletePort(temp);
                return (1);
            }
        }

        if (((req_blocksize == 0) && (geom_blocksize == 0)) ||
             ((maxblk == 0) && (geom_maxblk == 0))) {
            get_drive_geometry(fdes, &geom_maxblk, &geom_blocksize);
        }
        set_maxblk_startblk(fdes, geom_maxblk, geom_blocksize, req_blocksize);
        if (verbose) {
            char buf[120];
            if (strcmp(fdes->name, disk_device) == 0)
                strcpy(buf, disk_device);
            else
                sprintf(buf, "%s=%s", fdes->name, disk_device);

            fprintf(stderr, "dev  %-20s unit=%-2d flag=%-3d start=%-8u "
                    "end=%-8u bs=%u\n", buf, disk_unit, disk_flags,
                    fdes->startblk, fdes->maxblk, fdes->blocksize);
        }

        fdes->dtype = tDEVICE;

        if (pos)
            *pos = ',';
    }
    return (0);
}

static unsigned int
compute_shift(unsigned int value)
{
    int shift = 0;
    for (shift = 0; (value != 0) && ((value & 1) == 0); value >>= 1)
        shift++;
    return (shift);
}

static int
set_limits(struct file_info *in, struct file_info *out)
{
    ULONG iblks;
    ULONG oblks;

    if (in->blocksize == 0)
        in->blocksize = buf_blocksize;
    if (out->blocksize == 0)
        out->blocksize = buf_blocksize;
    if (buf_blocksize < in->blocksize)
        buf_blocksize = in->blocksize;
    if (buf_blocksize < out->blocksize)
        buf_blocksize = out->blocksize;
    in->blocks_per_bufblock = buf_blocksize / in->blocksize;
    out->blocks_per_bufblock = buf_blocksize / out->blocksize;
    in->blockshift = compute_shift(in->blocksize);
    out->blockshift = compute_shift(out->blocksize);

    if (source_start != 0)
        in->startblk += source_start;

    if (dest_start != 0)
        out->startblk += dest_start;

    in->currentblk  = in->startblk;
    out->currentblk = out->startblk;

    if (maxblk != 0) {
        /*
         * Only one of "in" or "out" should set maxblk.
         * Usually "in" is selected, unless it is Nil:
         */
        if (((in->maxblk - in->startblk > maxblk) ||
             (in->maxblk == 0)) && (in->dtype != tNIL)) {
            in->maxblk = in->startblk + maxblk;
            in->maxblk_devlimit = 0;
        } else if ((out->maxblk - out->startblk > maxblk) ||
                   (out->maxblk == 0)) {
            out->maxblk = out->startblk + maxblk;
            out->maxblk_devlimit = 0;
        }
    }

    iblks = (in->maxblk == 0) ? 0 :
            ((in->maxblk - in->startblk) / in->blocks_per_bufblock);
    oblks = (out->maxblk == 0) ? 0 :
            ((out->maxblk - out->startblk) / out->blocks_per_bufblock);

    if (maxblk && (buffer_blocks > maxblk))
        buffer_blocks = maxblk;

    if ((buffer_blocks > iblks) && (iblks != 0))
        buffer_blocks = iblks;
    if ((buffer_blocks > oblks) && (oblks != 0))
        buffer_blocks = oblks;

    if ((in->maxblk && (in->startblk >= in->maxblk)) ||
        (out->maxblk && (out->startblk >= out->maxblk)))
        return (1);
    return (0);
}

static unsigned int
read_system_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);  /* Measured latency is ~250us on A3000 A3640 */
    return ((unsigned int) (ds.ds_Minute) * 60 * TICKS_PER_SECOND + ds.ds_Tick);
}

static int
io_loop(void)
{
    int rc = 0;
    if (async == 0) {
        /*
         * Synchronous reader, synchronous writer
         *
         * 1. Read[0] & Wait_read[0]
         * 2. Write[0] & Wait_write[0]
         * 3. Repeat
         */
        while ((rc = do_io(&in, dREAD, 0)) == 0) {
            chkabort();
            if ((rc = do_io(&out, dWRITE, 0)) != 0)
                break;
            chkabort();
        }
        if (rc < 0)
            rc = 0;
    } else if (async_mode == 0) {
        /*
         * Synchronous reader, asynchronous writer
         *
         * 1. Wait_write[0]  (does nothing if no write scheduled)
         * 2. Read[0] & Wait_read[0]
         * 3. Write[0]
         * 4. Wait_write[1]  (does nothing if no write scheduled)
         * 5. Read[1] & Wait_read[1]
         * 6. Repeat
         *
         * Final: Wait_write[last_read]
         */
        int curbuf = 0;
        while ((rc = do_wait(&out, curbuf)) == 0) {
            chkabort();
            if (do_io(&in, dREAD, curbuf))
                break;  /* Normal loop termination */
            if ((rc = do_wait(&in, curbuf)) != 0)
                break;
            chkabort();
            if ((rc = do_io(&out, dWRITE, curbuf)) != 0)
                break;
            curbuf ^= 1;
        }
        curbuf ^= 1;
        rc |= do_wait(&out, curbuf);
    } else {
        /*
         * Asynchronous reader, asynchronous writer
         *
         * 1. Read[0]
         * 2. Wait_write[1] & Read[1]
         * 3. Wait_read[0] & Write[0]
         * 4. Wait_read[1] & Write[1]
         * 5. Wait_write[0] & Read[0]
         * 6. Repeat from 2
         *
         * Final: Wait_write[2nd-to-last_read]
         *        Write[last_read]
         *        Wait_write[last_read]
         */
        int curbuf = 0;
        int wp[2];
        wp[0] = 0;
        wp[1] = 0;
        if (do_io(&in, dREAD, curbuf))
            return (1);
        wp[curbuf] = 1;
        chkabort();
        curbuf ^= 1;
        while ((rc = do_wait(&out, curbuf)) == 0) {
            chkabort();
            if (do_io(&in, dREAD, curbuf) != 0)
                break;  /* Normal loop termination */
            wp[curbuf] = 1;
            curbuf ^= 1;
            if ((rc = do_wait(&in, curbuf)) != 0)
                break;
            chkabort();
            if (wp[curbuf]) {
                wp[curbuf] = 0;
                if ((rc = do_io(&out, dWRITE, curbuf)) != 0)
                    break;
            }
            chkabort();
        }
        /* Finish trailing write */
        curbuf ^= 1;
        if (wp[curbuf]) {
            if (verbose && (last_read_blocks[curbuf ^ 1] != 0))
                fprintf(stderr, "\n%33s", "");
            rc |= do_wait(&in, curbuf);
            rc |= do_io(&out, dWRITE, curbuf);
            rc |= do_wait(&out, curbuf);
        }
    }
    if (verbose)
        fprintf(stderr, "\n");
    return (rc);
}

static int
alloc_buffers(void)
{
    ULONG flags = MEMF_PUBLIC;
    buffer[0] = NULL;
    buffer[1] = NULL;

    if (in.dtype == tNIL)
        flags |= MEMF_CLEAR;

    while ((buffer[0] == NULL) && (buffer_blocks > 0)) {
        buffer[0] = (char *) AllocMem(buffer_blocks * buf_blocksize, flags);
        if (buffer[0] == NULL) {
            buffer_blocks >>= 1;
            continue;
        }
        if (async) {
            /* Allocate second buffer the same size as first buffer */
            buffer[1] = (char *) AllocMem(buffer_blocks * buf_blocksize, flags);
            if (buffer[1] == NULL) {
                FreeMem(buffer[0], buffer_blocks * buf_blocksize);
                buffer[0] = NULL;
                buffer_blocks >>= 1;
                continue;
            }
        }
    }

    if (buffer[0] == NULL) {
        fprintf(stderr, "Unable to allocate space for copy buffer!\n");
        return (1);
    }
    return (0);
}

static void
free_buffers(void)
{
    int i;
    for (i = 0; i <= async; i++) {
        if (buffer[i] != NULL)
            FreeMem(buffer[i], buffer_blocks * buf_blocksize);
        buffer[i] = NULL;
    }
}

int
break_abort(void)
{
    static int entered = 0;

    if (entered++)
        return;

    fprintf(stderr, "^C\n");

    do_close(&in);
    do_close(&out);
    free_buffers();

    exit(1);
}

int
main(int argc, char *argv[])
{
    int names = 0;
    int good_finish = 0;
    int index;
    unsigned int stime;
    unsigned int etime;
    unsigned int source_blocksize = 0;
    unsigned int dest_blocksize   = 0;

    progname = argv[0];

    memset(&in, 0, sizeof (in));
    memset(&out, 0, sizeof (out));

    if (argc == 1)
        print_usage();

    for (index = 1; index < argc; index++) {
        char *ptr = argv[index];
        if ((ptr[0] == '-') && (ptr[1] != '\0')) {
            while (*(++ptr) != '\0') {
                if (ACCEPT(maxblk, "m") ||
                    ACCEPT(maxblk, "M") ||
                    ACCEPT(buffer_blocks, "b") ||
                    ACCEPT(source_start, "ss") ||
                    ACCEPT(source_start, "seek") ||     /* like dd */
                    ACCEPT(dest_start, "sd") ||
                    ACCEPT(dest_start, "ds") ||
                    ACCEPT(dest_start, "skip") ||       /* like dd */
                    ACCEPT(source_blocksize, "sbs") ||
                    ACCEPT(source_blocksize, "ibs") ||  /* like dd */
                    ACCEPT(dest_blocksize, "dbs") ||
                    ACCEPT(dest_blocksize, "obs")) {    /* like dd */
                    break;
                } else if (tolower(*ptr) == 'v') {
                    verbose++;
                } else if (tolower(*ptr) == 'a') {
                    if (async)
                        async_mode++;
                    async = 1;
                } else if (tolower(*ptr) == 'h') {
                    print_help();
                    exit(0);
                } else if (tolower(*ptr) == 'r') {
                    report_perf++;
                } else if (tolower(*ptr) == 'd') {
                    dryrun++;
                } else {
                    fprintf(stderr, "Unrecognized option %s\n", ptr);
                    print_help();
                }
            }
        } else {
            names++;
            if (names == 1) {
                in.name  = ptr;
            } else if (names == 2) {
                out.name = ptr;
            } else {
                fprintf(stderr, "Unknown parameter %s\n", ptr);
                print_usage();
            }
        }
    }

    if (names != 2) {
        fprintf(stderr, "Source and destination are required\n");
        exit(1);
    }

    onbreak(break_abort);

    if (do_open(&in,  dREAD, source_blocksize) ||
        do_open(&out, dWRITE, dest_blocksize) ||
        set_limits(&in, &out) ||
        alloc_buffers()) {
        goto close_finish;
    }

    if (!dryrun && (in.dtype == tFILE))
        if (fseek(in.strIO, in.startblk * in.blocksize, SEEK_SET) != 0)
            goto close_finish;

    if (!dryrun && (out.dtype == tFILE))
        if (fseek(out.strIO, out.startblk * out.blocksize, SEEK_SET) != 0)
            goto close_finish;

    if ((in.dtype == tNONE) || (out.dtype == tNONE))
        goto close_finish;
    stime = read_system_ticks();
    io_loop();

    good_finish = 1;

close_finish:
    do_close(&in);
    do_close(&out);

    etime = read_system_ticks();

    free_buffers();

    if (good_finish && (verbose || report_perf)) {
        unsigned int total;  /* KB */
        unsigned int rep = total;
        unsigned int ttime;
        unsigned int tsec;
        unsigned int trem;
        unsigned int txblks = out.currentblk - out.startblk;
        char c1 = 'K';
        char c2 = 'K';

        total = MulDivU(txblks, out.blocksize, 1024);  /* KB */
        rep = total;
        if (rep > 10000) {
            rep /= 1000;
            c1 = 'M';
        }
        if (etime < stime)
            etime += 24 * 60 * 60 * TICKS_PER_SECOND;  /* Next day */
        ttime = etime - stime;
        if (ttime == 0)
            ttime = 1;
        tsec = ttime / TICKS_PER_SECOND;
        trem = ttime % TICKS_PER_SECOND;

        if ((total / ttime) >= (100000 / TICKS_PER_SECOND)) {
            /* Transfer rate > 100 MB/sec */
            total /= 1000;
            c2 = 'M';
        }
        if (total < 10000000) {
            /* Transfer < 10 MB (or 10 GB) */
            total = total * TICKS_PER_SECOND / ttime;
        } else {
            /* Change math order to avoid 32-bit overflow */
            ttime /= TICKS_PER_SECOND;
            if (ttime == 0)
                ttime = 1;
            total /= ttime;
        }
        fprintf(stderr, "%u %cB transfered in %u.%02u sec: %u %cB/sec\n",
                rep, c1, tsec, trem * 100 / TICKS_PER_SECOND, total, c2);
    }
    exit(0);
}
