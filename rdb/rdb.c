/*
 * Utility to show and edit Amiga disk RDB fields, copyright Chris Hooper
 *
 * This product is distributed as freeware with no warranties expressed or
 * implied.  There are no restrictions on distribution or application of
 * this program other than it may not be used in contribution with or
 * converted to an application under the terms of the GNU Public License
 * nor sold for profit greater than the cost of distribution.  Also, this
 * notice must accompany all redistributions and/or modifications of this
 * program.  Remember:  If you expect bugs, you should not be disappointed.
 *
 */

const char *version = "\0$VER: rdb 1.3 (21-Nov-2026) \xA9  Chris Hooper";

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <exec/types.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <exec/memory.h>
#include <devices/hardblocks.h>
#include <clib/dos_protos.h>

/* BCPL conversion functions */
#define BTOC(x) ((x)<<2)
#define CTOB(x) ((x)>>2)

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

#define strcasecmp stricmp

/* rdb_HighRDSKBlock is rdb_Reserved4[0] in older headers */
#define RDB_HIGHRDSKBLOCK(r) (((ULONG *) (r))[38])

struct FileSysStartupMsg *find_startup();
static long  blk_read(void *buf, ULONG blk);
static long  blk_write(void *buf, ULONG blk);
static int   break_abort(void);
static ULONG chain_limit(void);
static int   chain_loops(int which, ULONG *count, ULONG blk);
static ULONG chars_to_long(unsigned char *str);
static LONG  chksum(void *ptr);
static void  close_device(void);
static int   create_partition(void);
static int   create_rdb(int nonempty);
static int   build_rdb(struct RigidDiskBlock *r);
static int   get_geometry(struct DriveGeometry *dg);
static int   model_main(int mode, int found, int nonempty);
static void  do_creations(void);
static int   dofix(void *buf, ULONG blk, ULONG name);
static int   extract_seglist(int seglist, const char *name);
static void  filesystem_operation(int filesystem);
static int   fixsum(void *ptr, ULONG blk);
static void  freelist(struct LoadSegBlock *current);
static int   getans(void);
static int   scan_lseg_chain(int blk);
static int   name_matches(UBYTE *dname, const char *name);
static int   name_in_use(const char *name, const char *except);
static int   open_device(void);
static int   partition_operation(int partition, int parent);
static void  perr(const char *fmt, ...);
static int   print_bad_blocks(void);
static int   print_filesystem_info(void);
static void  print_help(void);
static void  print_id(ULONG name);
static int   print_partition_info(void);
static int   print_rdb_info(void);
static int   print_seglist(int seglist);
static void  print_usage(void);
static int   rigid_operation(void);

struct  IOExtTD *trackIO = NULL; /* device packet pointer */
struct  MsgPort *devport = NULL; /* device communication port */

char    *progname;              /* name of this program running */
char    *buffer = NULL;         /* buffer area for disk read */
struct  RigidDiskBlock *rdb;    /* rdb data pointer */
struct  BadBlockBlock *bad;     /* bad block info pointer */
struct  PartitionBlock *part;   /* partition data pointer */
struct  PartitionBlock *ppart;  /* partition data pointer */
struct  FileSysHeaderBlock *fs; /* filesystem data pointer */
struct  LoadSegBlock *seg;      /* loadseg data pointer */

#define NO      0
#define YES     1
#define ALL     2
#define QUIT    3

char    *disk_device = "some.device";
int     disk_unit = 0;
int     disk_flags = 0;
int     autofix = 0;
int     globalfix = 0;
ULONG   maxaccess = 0;
int     editpart = 0;
int     editrigid = 0;
int     newrdb = 0;
int     mode = 0;                /* MODE_* for -check, -fix, -edit */

#define MODE_NONE  0
#define MODE_CHECK 1
#define MODE_FIX   2
#define MODE_EDIT  3
int     createpart = 0;
int     promote = 0;
char    partname[32];
int     extractfs = 0;
int     rdb_blk = 0;
int     any_bstr = 0;           /* Some partition name is a BSTR */
int     any_ivs = 0;            /* Some partition name is a C string */
int     showseglist = 0;
int     promoted = 0;
int     partfound = 0;          /* -part / -promote partition was found */
int     scan_error = 0;         /* Some RDB chain could not be read */
int     exit_status = 0;
int     extract_count = 0;
ULONG   extractid = 0;


struct RigidDiskBlock newrigid =
{
    IDNAME_RIGIDDISK,                       /* ID                   */
    128,                                    /* SummedLongs          */
    0,                                      /* ChkSum               */
    7,                                      /* HostID               */
    512,                                    /* BlockBytes           */
    0x38,  /* NoReselect, Disk/Cntrlr Valid ** Flags                */
    -1,                                     /* BadBlockList         */
    -1,                                     /* PartitionList        */
    -1,                                     /* FileSysHeaderList    */
    -1,                                     /* DriveInit            */
    0,0,0, 0,0,0,                           /* Reserved1[6]         */
    610,                                    /* Cylinders            */
    17,                                     /* Sectors              */
    4,                                      /* Heads                */
    1,                                      /* Interleave           */
    612,                                    /* Park                 */
    0,0,0,                                  /* Reserved2[3]         */
    -1,                                     /* WritePreComp         */
    -1,                                     /* ReducedWrite         */
    -1,                                     /* StepRate             */
    0,0,0, 0,0,                             /* Reserved3[5]         */
    0,                                      /* RDBBlocksLo          */
    67,                                     /* RDBBlocksHi          */
    1,                                      /* LoCylinder           */
    610,                                    /* HiCylinder           */
    68,                                     /* CylBlocks            */
    0,                                      /* AutoParkSeconds      */
    0,0,                                    /* Reserved4[2]         */
    "none",                                 /* DiskVendor[8]        */
    "none",                                 /* DiskProduct[16]      */
    "none",                                 /* DiskRevision[4]      */
    "none",                                 /* ControllerVendor[8]  */
    "none",                                 /* ControllerProduct[16]*/
    "none",                                 /* ControllerRevision[4]*/
    0,0,0,0,0, 0,0,0,0,0                    /* Reserved5[10]        */
};

struct PartitionBlock newpart =
{
    IDNAME_PARTITION,                       /* ID           */
    128,                                    /* SummedLongs  */
    0,                                      /* ChkSum       */
    7,                                      /* HostID       */
    -1,                                     /* Next         */
    0,                                      /* Flags        */
    0, 0,                                   /* Reserved1[2] */
    0,                                      /* DevFlags     */
    "\004XXXX",                             /* DriveName    */
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,        /* Reserved2[15]*/
    17,                                     /* TableSize    */
    128,                                    /* SizeBlock    */
    0,                                      /* SecOrg       */
    4,                                      /* Surfaces     */
    1,                                      /* SecPerBlock  */
    17,                                     /* BlocksPerTrk */
    2,                                      /* Reserved     */
    0,                                      /* PreAlloc     */
    0,                                      /* Interleave   */
    2,                                      /* LowCyl       */
    610,                                    /* HighCyl      */
    10,                                     /* NumBuffers   */
    1,                                      /* BufMemType   */
    0x00ffffff,                             /* MaxTransfer  */
    0xfffffffe,                             /* Mask         */
    0,                                      /* BootPri      */
    0x58585858,  /* 'XXXX' */               /* DosType      */
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0         /* EReserved[15]*/
};

#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

#define PART    0
#define RIGID   1
#define FILESYS 2

#define INT     0
#define STR     1

typedef struct {
    const char *name;
    short offset;           /* Offset into RDB struct */
    char type;              /* 0=INT, 1=STR */
    char which;             /* 0=Partition, 1=RDB, 2=Filesystem */
    char size;              /* Maximum length of STR field */
} accept_info_t;

static accept_info_t accept[] =
/*  name                    offset   type   which  size */
{   "HostID",               3,       INT,   RIGID,  0,
    "BlockBytes",           4,       INT,   RIGID,  0,
    "RigidFlags",           5,       INT,   RIGID,  0,
    "BadBlockList",         6,       INT,   RIGID,  0,
    "PartitionList",        7,       INT,   RIGID,  0,
    "FileSysHeaderList",    8,       INT,   RIGID,  0,
    "DriveInit",            9,       INT,   RIGID,  0,
    "Cylinders",            16,      INT,   RIGID,  0,
    "Sectors",              17,      INT,   RIGID,  0,
    "Heads",                18,      INT,   RIGID,  0,
    "Interleave",           19,      INT,   RIGID,  0,
    "Park",                 20,      INT,   RIGID,  0,
    "WritePreComp",         24,      INT,   RIGID,  0,
    "ReducedWrite",         25,      INT,   RIGID,  0,
    "StepRate",             26,      INT,   RIGID,  0,
    "RDBBlocksLo",          32,      INT,   RIGID,  0,
    "RDBBlocksHi",          33,      INT,   RIGID,  0,
    "LoCylinder",           34,      INT,   RIGID,  0,
    "HiCylinder",           35,      INT,   RIGID,  0,
    "CylBlocks",            36,      INT,   RIGID,  0,
    "AutoParkSeconds",      37,      INT,   RIGID,  0,
    "DiskVendor",           40,      STR,   RIGID,  8,
    "DiskProduct",          42,      STR,   RIGID, 16,
    "DiskRevision",         46,      STR,   RIGID,  4,
    "ControllerVendor",     47,      STR,   RIGID,  8,
    "ControllerProduct",    49,      STR,   RIGID, 16,
    "ControllerRev",        53,      STR,   RIGID,  4,
    "Next",                 4,       INT,   PART,  0,
    "Flags",                5,       INT,   PART,  0,
    "DevFlags",             8,       INT,   PART,  0,
    "DriveName",            9,       STR,   PART, 31,
    "Surfaces",             32 + 3,  INT,   PART,  0,
    "BlocksPerTrack",       32 + 5,  INT,   PART,  0,
    "Reserved",             32 + 6,  INT,   PART,  0,
    "PreAlloc",             32 + 7,  INT,   PART,  0,
    "Interleave",           32 + 8,  INT,   PART,  0,
    "LowCyl",               32 + 9,  INT,   PART,  0,
    "HighCyl",              32 + 10, INT,   PART,  0,
    "NumBuffers",           32 + 11, INT,   PART,  0,
    "BufMemType",           32 + 12, INT,   PART,  0,
    "MaxTransfer",          32 + 13, INT,   PART,  0,
    "Mask",                 32 + 14, INT,   PART,  0,
    "BootPri",              32 + 15, INT,   PART,  0,
    "DosType",              32 + 16, INT,   PART,  0,
};

#define NUM_ACCEPT ARRAY_SIZE(accept)
static char changed[NUM_ACCEPT];

int
main(int argc, char *argv[])
{
    int     index;
    int     cur;
    int     argnum = 0;
    int     reportmax = 0;
    int     found;
    int     nonempty = 0;
    char    *equalspos;

    progname = argv[0];
    while (*progname)
        progname++;
    while (progname > argv[0]) {
        if ((*progname == '/') || (*progname == ':')) {
            progname++;
            break;
        } else
            progname--;
    }

    autofix = globalfix;

    for (index = 1; index < argc; index++) {
        if (argv[index][0] == '-') {
            argv[index]++;
            if (strcasecmp(argv[index], "avail") == 0 ||
                strcasecmp(argv[index], "a") == 0) {
                reportmax = 1;
            } else if (strcasecmp(argv[index], "extract") == 0 ||
                       strcasecmp(argv[index], "e") == 0) {
                extractfs = 1;
                index++;
                if (index == argc)
                    perr("extract requires the filesystem DosType");
                if (*argv[index] == '\'')
                    extractid = chars_to_long(argv[index] + 1);
                else
                    sscanf(argv[index], "%i", &extractid);
                if (extractid == 0)
                    perr("invalid extract DosType %s specified", argv[index]);
                printf("extract id %s = %08X '", argv[index], extractid);
                print_id(extractid);
                printf("'\n");
            } else if (strcasecmp(argv[index], "help") == 0 ||
                       strcasecmp(argv[index], "h") == 0) {
                print_help();
            } else if (strcasecmp(argv[index], "newpart") == 0 ||
                       strcasecmp(argv[index], "n") == 0) {
                if (editpart)
                    perr("The part command is implied by newpart");
                if (createpart)
                    perr("You may only create one partition at a time");
                if (promote)
                    perr("only one partition may be modified at a time");
                createpart = 1;
                index++;
                if (index == argc)
                    perr("newpart requires the new partition name");
                if (strlen(argv[index]) >= sizeof (partname))
                    perr("partition name %s is too long", argv[index]);
                strcpy(partname, argv[index]);
            } else if (strcasecmp(argv[index], "part") == 0 ||
                     strcasecmp(argv[index], "p") == 0) {
                if (editpart)
                    perr("only one partition may be modified at a time");
                if (createpart)
                    perr("The part command is implied by newpart");
                if (promote)
                    perr("only one partition may be modified at a time");
                editpart = 1;
                index++;
                if (index == argc)
                    perr("part requires the partition name");
                if (strlen(argv[index]) >= sizeof (partname))
                    perr("partition name %s is too long", argv[index]);
                strcpy(partname, argv[index]);
            } else if (strcasecmp(argv[index], "promote") == 0 ||
                       strcasecmp(argv[index], "pr") == 0) {
                if (editpart || createpart)
                    perr("only one partition may be modified at a time");
                if (promote)
                    perr("you may only promote one partition");
                promote = 1;
                index++;
                if (index == argc)
                        perr("promote requires the bootable partition name");
                if (strlen(argv[index]) >= sizeof (partname))
                    perr("partition name %s is too long", argv[index]);
                strcpy(partname, argv[index]);
            } else if (strcasecmp(argv[index], "rigid") == 0 ||
                       strcasecmp(argv[index], "r") == 0) {
                editrigid = 1;
            } else if (strcasecmp(argv[index], "newrdb") == 0) {
                newrdb = 1;
            } else if ((strcasecmp(argv[index], "check") == 0) ||
                       (strcasecmp(argv[index], "fix") == 0) ||
                       (strcasecmp(argv[index], "edit") == 0)) {
                if (mode != MODE_NONE)
                    perr("Only one of -check, -fix, and -edit may be given");
                mode = (tolower(argv[index][0]) == 'c') ? MODE_CHECK :
                       (tolower(argv[index][0]) == 'f') ? MODE_FIX :
                                                          MODE_EDIT;
            } else if (strcasecmp(argv[index], "seglist") == 0 ||
                       strcasecmp(argv[index], "s") == 0) {
                showseglist = 1;
            } else
                perr("Unknown parameter -%s", argv[index]);
        } else {
            if (equalspos = strchr(argv[index], '='))
                *equalspos = '\0';

            found = 0;
            for (cur = 0; cur < NUM_ACCEPT; cur++) {
                char *value;
                if (strcasecmp(accept[cur].name, argv[index]) != 0)
                    continue;
                if (equalspos) {
                    value = equalspos + 1;
                } else if (!found) {
                    if (index + 1 >= argc)
                        perr("%s requires a value", accept[cur].name);
                    value = argv[++index];
                } else {
                    value = argv[index];  /* already advanced */
                }
                if (!found)
                    printf("%s = %s\n", accept[cur].name, value);
                found = 1;
                if (accept[cur].type == STR) { /* it's a string */
                    char *dst;
                    if (strlen(value) > accept[cur].size)
                        perr("%s may be at most %d characters",
                             accept[cur].name, accept[cur].size);
                    if (accept[cur].which == PART)
                        dst = (char *) (((ULONG *) &newpart) +
                                        accept[cur].offset);
                    else
                        dst = (char *) (((ULONG *) &newrigid) +
                                        accept[cur].offset);
                    memset(dst, 0, accept[cur].size);
                    memcpy(dst, value, strlen(value));
                } else if (*value == '\'') {  /* check for 'XXXX' */
                    switch (accept[cur].which) {
                        case PART:
                            ((ULONG *) &newpart)[accept[cur].offset] =
                                       chars_to_long(value + 1);
                            break;
                        case RIGID:
                            ((ULONG *) &newrigid)[accept[cur].offset] =
                                       chars_to_long(value + 1);
                            break;
                    }
                } else { /* scan number */
                    switch (accept[cur].which) {
                        case PART:
                            sscanf(value, "%i", ((ULONG *)
                                   &newpart) + accept[cur].offset);
                            break;
                        case RIGID:
                            sscanf(value, "%i", ((ULONG *)
                                   &newrigid) + accept[cur].offset);
                            break;
                    }
                }
                changed[cur] = 1;
            }

            if (found)
                continue;

            if (equalspos)
                *equalspos = '=';

            switch (argnum) {
                case 0:
                    disk_device = argv[index];
                    if (!strchr(disk_device, '.'))
                        perr("Invalid device %s", argv[index]);
                    break;
                case 1:
                    if (!sscanf(argv[index], "%i", &disk_unit))
                        perr("Invalid unit %s", argv[index]);
                    break;
                case 2:
                    if (!sscanf(argv[index], "%i", &disk_flags))
                        perr("Invalid flags %s", argv[index]);
                    break;
                default:
                    perr("Unknown parameter %s", argv[index]);
            }
            argnum++;
        }
    }

    if (argnum == 0)
        print_usage();

    if (mode != MODE_NONE) {
        for (index = 0; index < NUM_ACCEPT; index++)
            if (changed[index])
                break;
        if (editpart || createpart || promote || editrigid || extractfs ||
            newrdb || (index < NUM_ACCEPT))
            perr("-check, -fix, and -edit may not be combined with other "
                 "options or keywords");
    }

    /*
     * A keyword such as Interleave exists for both the RDB and partitions.
     * Apply it only to the block type being edited.
     */
    for (index = 0; index < NUM_ACCEPT; index++) {
        if (!changed[index])
            continue;
        for (cur = 0; cur < NUM_ACCEPT; cur++) {
            if ((cur != index) &&
                (strcasecmp(accept[cur].name, accept[index].name) == 0))
                break;
        }
        if (cur == NUM_ACCEPT)
            continue;  /* Not ambiguous */
        if ((accept[index].which == RIGID) && !(editrigid || newrdb))
            changed[index] = 0;
        else if ((accept[index].which == PART) &&
                 !(editpart || createpart || promote))
            changed[index] = 0;
    }
    for (index = 0; index < NUM_ACCEPT; index++) {
        if (changed[index] && (accept[index].which == PART) &&
            !(editpart || createpart || promote)) {
            fprintf(stderr, "You must specify -part, -newpart, or -promote "
                    "to edit partition fields\n\n");
            exit_status = 1;
            break;
        }
    }

    if (open_device())
        exit(1);
    onbreak(break_abort);
    buffer = (char *) malloc(TD_SECTOR * 5);
    if (buffer == NULL) {
        fprintf(stderr, "Unable to allocate %d bytes\n", TD_SECTOR * 5);
        close_device();
        exit(1);
    }
    rdb   = (struct RigidDiskBlock *)     (buffer + TD_SECTOR * 0);
    part  = (struct PartitionBlock *)     (buffer + TD_SECTOR * 1);
    fs    = (struct FileSysHeaderBlock *) (buffer + TD_SECTOR * 2);
    seg   = (struct LoadSegBlock *)       (buffer + TD_SECTOR * 3);
    bad   = (struct BadBlockBlock *)      (buffer + TD_SECTOR * 4);
    ppart = (struct PartitionBlock *)     (buffer + TD_SECTOR * 4);
    /* Note that "ppart" shares the same RDB position as "bad" */

    for (index = 0; index < RDB_LOCATION_LIMIT; index++) {
        int pos;
        if (blk_read(rdb, index)) {
            nonempty++;  /* Unknown contents */
            continue;
        }
        if (rdb->rdb_ID == IDNAME_RIGIDDISK)
            break;
        for (pos = 0; pos < TD_SECTOR; pos++)
            if (((UBYTE *) rdb)[pos] != 0) {
                nonempty++;
                break;
            }
    }

    if (mode != MODE_NONE) {
        int rc = model_main(mode, (index < RDB_LOCATION_LIMIT) ? index : -1,
                            nonempty);
        free(buffer);
        close_device();
        exit(rc);
    }

    if (newrdb) {
        if (index < RDB_LOCATION_LIMIT) {
            fprintf(stderr, "An RDB already exists at block %d of %s unit %d\n",
                    index, disk_device, disk_unit);
            free(buffer);
            close_device();
            exit(1);
        }
        switch (create_rdb(nonempty)) {
            case 0:
                index = 0;      /* Continue with the new RDB */
                break;
            case -1:            /* Declined */
                free(buffer);
                close_device();
                exit(0);
            default:
                free(buffer);
                close_device();
                exit(1);
        }
    }

    if (index < RDB_LOCATION_LIMIT) {
        rdb_blk = index;
        if ((rdb->rdb_BlockBytes != TD_SECTOR) && (rdb->rdb_BlockBytes != 0)) {
            fprintf(stderr, "RDB at block %d has BlockBytes=%u; only %d-byte "
                    "blocks are supported\n", rdb_blk, rdb->rdb_BlockBytes,
                    TD_SECTOR);
            free(buffer);
            close_device();
            exit(1);
        }
        if (editpart) {
            /* Refuse a rename to the name of another partition */
            for (cur = 0; cur < NUM_ACCEPT; cur++)
                if (changed[cur] && (accept[cur].which == PART) &&
                    (accept[cur].type == STR))
                    break;
            if ((cur < NUM_ACCEPT) &&
                name_in_use((char *) newpart.pb_DriveName, partname)) {
                fprintf(stderr, "Partition %s already exists\n",
                        newpart.pb_DriveName);
                free(buffer);
                close_device();
                exit(1);
            }
        }
        if (print_rdb_info()) {
            index = RDB_LOCATION_LIMIT;  /* Skip creations */
            exit_status = 1;
        } else {
            if (print_bad_blocks())
                scan_error = 1;
            if (print_partition_info())
                scan_error = 1;
            if ((editpart || promote) && !partfound && !scan_error) {
                fprintf(stderr, "Partition %s not found\n", partname);
                exit_status = 1;
            }
            if (print_filesystem_info())
                scan_error = 1;
            if (extractfs && (extract_count == 0) && !scan_error) {
                fprintf(stderr, "No filesystem with DosType %08X found\n",
                        extractid);
                exit_status = 1;
            }
            if ((rdb->rdb_DriveInit != -1) &&
                scan_lseg_chain(rdb->rdb_DriveInit)) {
                fprintf(stderr, "DriveInit code chain could not be read\n");
                scan_error = 1;
            }
            if (RDB_HIGHRDSKBLOCK(rdb) > maxaccess &&
                RDB_HIGHRDSKBLOCK(rdb) <= rdb->rdb_RDBBlocksHi)
                maxaccess = RDB_HIGHRDSKBLOCK(rdb);
        }
    } else {
        fprintf(stderr, "No RDB label found on %s unit %d\n",
                disk_device, disk_unit);
        exit_status = 1;
    }

    if (reportmax)
        printf("Next block available=%u\n", maxaccess + 1);

    if (index < RDB_LOCATION_LIMIT)
        do_creations();

    free(buffer);
    close_device();
    exit((exit_status || scan_error) ? 1 : 0);
}

static int
open_device(void)
{
    /* create port to use when talking with handler */
    devport = CreatePort(NULL, 0);
    if (devport) {
        trackIO = (struct IOExtTD *)
                  CreateExtIO(devport, sizeof (struct IOExtTD));
    } else {
        trackIO = NULL;
    }

    if (trackIO == NULL) {
        fprintf(stderr, "Failed to create trackIO structure\n");
        if (devport != NULL)
            DeletePort(devport);
        devport = NULL;
        return (1);
    }

    if (OpenDevice(disk_device, disk_unit, (struct IORequest *)trackIO,
                   disk_flags)) {
        fprintf(stderr, "fatal: Unable to open %s unit %d.\n",
                disk_device, disk_unit);
        DeleteExtIO((struct IORequest *)trackIO);
        DeletePort(devport);
        trackIO = NULL;
        devport = NULL;
        return (1);
    }

    return (0);
}

static void
close_device(void)
{
    if (trackIO)
        CloseDevice((struct IORequest *)trackIO);
    if (devport)
        DeletePort(devport);
    if (trackIO)
        DeleteExtIO((struct IORequest *)trackIO);
}

static int use_td64 = -1;

static long
blk_read(void *buf, ULONG blk)
{
    ULONG high32 = blk >> (32 - TD_SECSHIFT);

    if (blk > maxaccess)
        maxaccess = blk;

    if (high32) {
        if (use_td64 == 0) {
            printf("Can't address blk %u with 32-bit only device\n", blk);
            return (1);
        }
        trackIO->iotd_Req.io_Command = TD_READ64;   /* read data */
        trackIO->iotd_Req.io_Actual  = blk >> (32 - TD_SECSHIFT);
    } else {
        trackIO->iotd_Req.io_Command = CMD_READ;    /* read data */
    }
    trackIO->iotd_Req.io_Length = TD_SECTOR;        /* one sector */
    trackIO->iotd_Req.io_Offset = blk << TD_SECSHIFT; /* sector addr */
    trackIO->iotd_Req.io_Data   = buf;              /* memory addr */

    if (DoIO((struct IORequest *)trackIO)) {
        if (high32 && use_td64 == -1) {
            use_td64 = 0;
            return (blk_read(buf, blk));
        }
        return (1);
    }
    if (high32 && use_td64 == -1)
        use_td64 = 1;

    return (0);
}

static long
blk_write(void *buf, ULONG blk)
{
    ULONG high32 = blk >> (32 - TD_SECSHIFT);

    if (blk > maxaccess)
        maxaccess = blk;

    if (high32) {
        if (use_td64 == 0) {
            printf("Can't address blk %u with 32-bit only device\n", blk);
            return (1);
        }
        trackIO->iotd_Req.io_Command = TD_WRITE64;  /* write data */
        trackIO->iotd_Req.io_Actual  = blk >> (32 - TD_SECSHIFT);
    } else {
        trackIO->iotd_Req.io_Command = CMD_WRITE;   /* write data */
    }
    trackIO->iotd_Req.io_Length = TD_SECTOR;        /* one sector */
    trackIO->iotd_Req.io_Offset = blk << TD_SECSHIFT; /* sector addr */
    trackIO->iotd_Req.io_Data   = buf;              /* memory addr */

    if (DoIO((struct IORequest *)trackIO)) {
        if (high32 && use_td64 == -1) {
            use_td64 = 0;
            return (blk_write(buf, blk));
        }
        return (1);
    }
    if (high32 && use_td64 == -1)
        use_td64 = 1;
    return (0);
}

static void
print_usage(void)
{
    int index;
    int cpos;
    int which = -1;

    fprintf(stderr, "%s\n"
        "Usage: %s [options] {disk.device} [unit] [flags]\n"
        "    -check            Report problems in the RDB\n"
        "    -fix              Fix problems in the RDB (asks before writing)\n"
        "    -edit             Interactive RDB and partition editor\n"
        "    -newrdb           Create a new Rigid Disk Block on a blank disk\n"
        "    -rigid            Edit fields in the Rigid Disk Block\n"
        "    -part NAME        Modify partition table parameters of NAME\n"
        "    -newpart NAME     Add new partition NAME to the RDB\n"
        "    -promote NAME     Make partition NAME first to be mounted\n"
        "    -extract DOSTYPE  Extract filesystem to a file \n"
        "    -avail            Report next available block in RDB\n"
        "    -seglist          Show seglist addresses for filesystems\n"
/*      "    -v                Verbose mode\n" */
        "    -help             Give more help\n"
        "    Other keywords:", version + 7, progname);
    cpos = 8;
    for (index = 0; index < NUM_ACCEPT; index++) {
        int len = strlen(accept[index].name) + 1;
        if ((cpos + len > 79) || (which != accept[index].which)) {
            cpos = 8;
            fprintf(stderr, "\n%*s", cpos, "");
            which = accept[index].which;
        }
        fprintf(stderr, " %s", accept[index].name);
        cpos += len;
    }
    fprintf(stderr, "\n");
    exit(1);
}

static void
print_help(void)
{
    fprintf(stderr,
        "%s is a program which will display and allow you to modify\n"
        "select parameters present in a disk device's Rigid Disk Block\n"
        "\n"
        "MAJOR WARNING: This program is hazardous if used improperly.\n"
        "               There are no safeguards when modifying partition\n"
        "               information.  Know what you are doing.\n"
        "\n"
        "For the RDB, the values of Flags are as follows:\n"
        "     1 = no disks after this on this controller\n"
        "     2 = no LUNs after this at this SCSI target\n"
        "     4 = no Target IDs after this on this SCSI bus\n"
        "     8 = don't bother trying reselection on this device\n"
        "    16 = rdb_Disk identification is valid\n"
        "    32 = rdb_Controller identification is valid\n"
        "\n"
        "For partitions, the values of Flags are as follows:\n"
        "     1 = Device is Bootable\n"
        "     2 = Do not Mount device on startup\n"
        "\n"
        "Example: Copy FastFileSystem from RDB of SCSI Lun 2 to a file\n"
        "     rdb -extract 0x444f5301 scsi.device 2\n"
        "\n"
        "I use this program only for insane uses; good luck!\n", progname);
    exit(1);
}

static int
break_abort(void)
{
    static int entered = 0;

    if (entered++)
        return (0);

    fprintf(stderr, "^C\n");

    if (buffer != NULL)
        free(buffer);
    close_device();

    exit(1);
    return (0);
}

/*
 * Maximum number of blocks in any RDB chain.  Used to stop looping
 * on a corrupt (circular) chain.
 */
static ULONG
chain_limit(void)
{
    ULONG hi = rdb->rdb_RDBBlocksHi;

    if ((hi < RDB_LOCATION_LIMIT) || (hi > 0xfffff))
        hi = 65535;
    return (hi + 1);
}

#define MAP_BAD  0
#define MAP_PART 1
#define MAP_FS   2
#define MAP_SEG  3
#define MAP_MAX_BLKS 65536

static UBYTE *walkmap[4];

/*
 * Call once per block while walking an RDB chain, with *count starting
 * at 0 for each walk.  Returns 1 if the chain loops: a block was already
 * visited in this walk, or the walk is longer than the RDB area.
 */
static int
chain_loops(int which, ULONG *count, ULONG blk)
{
    UBYTE *map;

    if (walkmap[which] == NULL)
        walkmap[which] = (UBYTE *) malloc(MAP_MAX_BLKS / 8);
    map = walkmap[which];
    if ((*count == 0) && (map != NULL))
        memset(map, 0, MAP_MAX_BLKS / 8);
    if ((*count)++ >= chain_limit())
        return (1);
    if ((map != NULL) && (blk < MAP_MAX_BLKS)) {
        if (map[blk >> 3] & (1 << (blk & 7)))
            return (1);
        map[blk >> 3] |= (1 << (blk & 7));
    }
    return (0);
}

/*
 * Walk a LoadSeg block chain without printing it, so that maxaccess
 * accounts for every block in use by the RDB.
 */
static int
scan_lseg_chain(int blk)
{
    ULONG count = 0;

    while (blk != -1) {
        if (chain_loops(MAP_SEG, &count, blk) || blk_read(seg, blk) ||
            (seg->lsb_ID != IDNAME_LOADSEG))
            return (1);
        blk = seg->lsb_Next;
    }
    return (0);
}

/*
 * Compare a partition DriveName (BSTR, or C string as written by some
 * old IVS software) with a name, ignoring case as AmigaDOS does.
 */
static int
name_matches(UBYTE *dname, const char *name)
{
    int len = strlen(name);
    int i;
    UBYTE *p;

    if (dname[0] < 32) {            /* BSTR */
        if (dname[0] != len)
            return (0);
        p = dname + 1;
    } else {                        /* C string */
        if (len >= 32)
            return (0);
        p = dname;
        if (p[len] != '\0')
            return (0);
    }
    for (i = 0; i < len; i++)
        if (tolower(p[i]) != tolower((UBYTE) name[i]))
            return (0);
    return (1);
}

/*
 * Returns 1 if a partition other than "except" is named "name".
 * Uses the ppart buffer.
 */
static int
name_in_use(const char *name, const char *except)
{
    int   blk = rdb->rdb_PartitionList;
    ULONG count = 0;

    while (blk != -1) {
        if (chain_loops(MAP_PART, &count, blk) || blk_read(ppart, blk) ||
            (ppart->pb_ID != IDNAME_PARTITION))
            return (0);  /* Reported later by the full scan */
        if (name_matches(ppart->pb_DriveName, name) &&
            !name_matches(ppart->pb_DriveName, except))
            return (1);
        blk = ppart->pb_Next;
    }
    return (0);
}

static int
print_rdb_info(void)
{
    autofix = globalfix;

    if (chksum(rdb) && dofix(rdb, rdb_blk, IDNAME_RIGIDDISK))
        return (1);
    printf("%s unit %d : ", disk_device, disk_unit);
    printf(" Blk=%d HostID=%d BlockBytes=%d Flags=0x%04x\n",
            rdb_blk, rdb->rdb_HostID, rdb->rdb_BlockBytes, rdb->rdb_Flags);

    printf("  ");
    if (rdb->rdb_Flags & RDBFF_LAST)
        printf("LastDisk ");
    if (rdb->rdb_Flags & RDBFF_LASTLUN)
        printf("LastLUN ");
    if (rdb->rdb_Flags & RDBFF_LASTTID)
        printf("LastTarget ");
    if (rdb->rdb_Flags & RDBFF_NORESELECT)
        printf("NoReselect ");
    if (rdb->rdb_Flags & RDBFF_DISKID)
        printf("DiskValid ");
    if (rdb->rdb_Flags & RDBFF_CTRLRID)
        printf("ControllerValid ");
    if (rdb->rdb_Flags)
        printf("\n  ");

    printf("BadBlockList=%d PartitionList=%d FileSysHeaderList=%d "
           "DriveInit=%d\n",
            rdb->rdb_BadBlockList, rdb->rdb_PartitionList,
            rdb->rdb_FileSysHeaderList, rdb->rdb_DriveInit);
    printf("  Cylinders=%d Sectors=%d Heads=%d Interleave=%d Park=%d "
           "StepRate=%d\n",
            rdb->rdb_Cylinders, rdb->rdb_Sectors, rdb->rdb_Heads,
            rdb->rdb_Interleave, rdb->rdb_Park, rdb->rdb_StepRate);
    printf("  WritePreComp=%d ReducedWrite=%d RDBBlocksLo=%d RDBBlocksHi=%d\n",
            rdb->rdb_WritePreComp, rdb->rdb_ReducedWrite,
            rdb->rdb_RDBBlocksLo, rdb->rdb_RDBBlocksHi);
    printf("  LoCylinder=%d HiCylinder=%d CylBlocks=%d AutoParkSeconds=%d\n",
            rdb->rdb_LoCylinder, rdb->rdb_HiCylinder, rdb->rdb_CylBlocks,
            rdb->rdb_AutoParkSeconds);
    printf("  DiskVendor=%.8s DiskProduct=%.16s DiskRevision=%.4s\n",
            rdb->rdb_DiskVendor, rdb->rdb_DiskProduct, rdb->rdb_DiskRevision);
    printf("  ControllerVendor=%.8s ControllerProduct=%.16s "
           "ControllerRev=%.4s\n",
            rdb->rdb_ControllerVendor, rdb->rdb_ControllerProduct,
            rdb->rdb_ControllerRevision);
    printf("\n");
    if (rigid_operation())
        exit_status = 1;
    return (0);
}

static int
print_partition_info(void)
{
    int parent = -1;
    int partition;
    ULONG count = 0;
    struct DosEnvec *env;

    autofix = globalfix;
    if ((partition = rdb->rdb_PartitionList) == -1) {
        printf("No partitions\n");
        return (0);
    }

    while (partition != -1) {
        if (chain_loops(MAP_PART, &count, partition)) {
            fprintf(stderr, "partition list loops; giving up\n");
            return (1);
        }
        if (blk_read(part, partition)) {
            fprintf(stderr, "partition block %d could not be read\n",
                    partition);
            return (1);
        }

        if (part->pb_ID != IDNAME_PARTITION) {
            fprintf(stderr, "block %d does not contain a valid partition!\n",
                    partition);
            return (1);
        }

        if (chksum(part) && dofix(part, partition, IDNAME_PARTITION))
            return (1);

        print_id(part->pb_ID);
        printf("  Blk=%-3d ", partition);

        if (part->pb_DriveName[0] < 32) {
            any_bstr = 1;
            printf("DriveName=%-4.*s", part->pb_DriveName[0],
                    part->pb_DriveName + 1);
        } else {
            any_ivs = 1;
            printf("DriveName=%-4.32s", part->pb_DriveName);
        }

        if (createpart && name_matches(part->pb_DriveName, partname)) {
            createpart = 0;
            exit_status = 1;
            fprintf(stderr, "Error, partition %s already exists\n",
                    partname);
        }
        printf(" DevFlags=%-4d HostID=%d Next=%-3d %s%s\n",
               part->pb_DevFlags, part->pb_HostID,
               part->pb_Next, (part->pb_Flags &
               PBFF_NOMOUNT) ? "      " : "Mount ",
               (part->pb_Flags & PBFF_BOOTABLE) ? "Boot" : "");
        env = (struct DosEnvec *) part->pb_Environment;
        printf("      Surfaces=%-3d BlocksPerTrack=%-3d Reserved=%08x "
               "PreAlloc=%08x\n",
               env->de_Surfaces, env->de_BlocksPerTrack,
               env->de_Reserved, env->de_PreAlloc);
        printf("      Interleave=%d LowCyl=%-5d HighCyl=%-5d NumBuffers=%-3d "
               "BufMemType=%d\n",
               env->de_Interleave, env->de_LowCyl, env->de_HighCyl,
               env->de_NumBuffers, env->de_BufMemType);
        printf("      MaxTransfer=%08x Mask=%08x BootPri=%-2d DosType=%08X '",
               env->de_MaxTransfer, env->de_Mask,
               env->de_BootPri, env->de_DosType);
        print_id(env->de_DosType);
        printf("'\n");

        if (partition_operation(partition, parent))
            return (1);  /* Quit or failed write */
        if (promoted)
            break;  /* List was reordered; stop walking it */
        parent = partition;
        partition = part->pb_Next;
    }
    printf("\n");
    return (0);
}

static int
print_filesystem_info(void)
{
    int filesystem;
    ULONG count = 0;

    autofix = globalfix;
    if ((filesystem = rdb->rdb_FileSysHeaderList) == -1) {
        printf("No filesystems\n");
        return (0);
    }

    while (filesystem != -1) {
        if (chain_loops(MAP_FS, &count, filesystem)) {
            fprintf(stderr, "filesystem list loops; giving up\n");
            return (1);
        }
        if (blk_read(fs, filesystem)) {
            fprintf(stderr, "filesystem block %d could not be read\n",
                    filesystem);
            return (1);
        }

        if (fs->fhb_ID != IDNAME_FILESYSHEADER) {
            fprintf(stderr, "block %d does not contain a valid filesystem!\n",
                    filesystem);
            return (1);
    }

        if (chksum(fs) && dofix(fs, filesystem, IDNAME_FILESYSHEADER))
            return (1);

        print_id(fs->fhb_ID);
        printf("  Blk=%-3d HostID=%d Next=%-3d Version=%-7d DosType=%08X '",
                filesystem, fs->fhb_HostID, fs->fhb_Next,
                fs->fhb_Version, fs->fhb_DosType);
        print_id(fs->fhb_DosType);
        printf("'\n      ");
#define COND_PRINT(POS, NAME)                                       \
    if (fs->fhb_PatchFlags & (1 << POS))                            \
        printf("%s=%d ", NAME, *(&fs->fhb_Type + POS))

        COND_PRINT(0, "Type");
        COND_PRINT(1, "Task");
        COND_PRINT(2, "Lock");
        COND_PRINT(3, "Handler");
        COND_PRINT(4, "StackSize");
        COND_PRINT(5, "Priority");
        COND_PRINT(6, "Startup");
        COND_PRINT(7, "SegListBlocks");
        COND_PRINT(8, "GlobVec");
        if (showseglist) {
            if (print_seglist(fs->fhb_SegListBlocks))
                scan_error = 1;
        } else {
            printf("\n");
            if (scan_lseg_chain(fs->fhb_SegListBlocks)) {
                fprintf(stderr, "seglist of filesystem at block %d could "
                        "not be read\n", filesystem);
                scan_error = 1;
            }
        }

        filesystem_operation(filesystem);

        filesystem = fs->fhb_Next;
    }
    printf("\n");
    return (0);
}

static ULONG
chars_to_long(unsigned char *str)
{
    int pos;
    ULONG value = 0;
    unsigned char ch;
    int index;

    for (index = 0; index < 4; index++) {
        if (*str == '\\') {
            str++;
            ch = 0;
            pos = 0;
            while (isdigit(*str)) {
                ch = (ch * 10 + (*str) - '0');
                str++;
                pos++;
                if (pos == 3)
                    break;
            }
        } else if ((*str == '\0') || (*str == '\'')) {
            fprintf(stderr, "DosType should be four characters\n");
            return (0);
        } else {
            ch = *str;
            str++;
        }
        value <<= 8;
        value += ch;
    }

    return (value);
}

static void
print_id(ULONG name)
{
    unsigned char ch;
    int index;

    for (index = 0; index < 4; index++) {
        ch = (name >> (8 * (3 - index))) & 255;
        if ((ch < 32) || (ch > 126) || (ch == '\\'))
            printf("\\%d", ch);
        else
            printf("%c", ch);
    }
}

static int
print_seglist(int seglist)
{
    int index = 0;
    ULONG count = 0;

    autofix = globalfix;
    if (seglist == -1) {
        printf("      No seglist\n");
        return (0);
    }
    while (seglist != -1) {
        if (chain_loops(MAP_SEG, &count, seglist)) {
            fprintf(stderr, "\nseglist loops; giving up\n");
            return (1);
        }
        if ((index % 11) == 0) {
            printf("\n");
            printf("      ");
            index = 0;
        }
        index++;
        if (blk_read(seg, seglist)) {
            fprintf(stderr, "\nloadseg block %d could not be read\n", seglist);
            return (1);
        }
        if (seg->lsb_ID != IDNAME_LOADSEG) {
            fprintf(stderr, "block %d does not contain a valid loadseg block\n",
                    seglist);
            return (1);
        }
        if (chksum(seg) && dofix(seg, seglist, IDNAME_LOADSEG))
            return (1);
        else
            printf("%5d ", seglist);
        seglist = seg->lsb_Next;
    }
    printf("\n");
    return (0);
}

static int
print_bad_blocks(void)
{
    int badblock;
    int index;
    int pairs;
    ULONG count = 0;

    autofix = globalfix;
    if ((badblock = rdb->rdb_BadBlockList) == -1) {
        printf("No bad blocks\n\n");
        return (0);
    }

    while (badblock != -1) {
        if (chain_loops(MAP_BAD, &count, badblock)) {
            fprintf(stderr, "bad block list loops; giving up\n");
            return (1);
        }
        if (blk_read(bad, badblock)) {
            fprintf(stderr, "badblock block %d could not be read\n",
                    badblock);
            return (1);
        }

        if (bad->bbb_ID != IDNAME_BADBLOCK) {
            fprintf(stderr, "block %d does not contain a valid badblock!\n",
                    badblock);
            return (1);
        }

        if (chksum(bad) && dofix(bad, badblock, IDNAME_BADBLOCK))
            return (1);

        print_id(bad->bbb_ID);
        printf("  Blk=%-3d HostID=%d Next=%d Reserved=%d",
               badblock, bad->bbb_HostID, bad->bbb_Next, bad->bbb_Reserved);
        pairs = ((LONG) bad->bbb_SummedLongs - 6) / 2;
        if (pairs > (TD_SECTOR / 4 - 6) / 2)
            pairs = (TD_SECTOR / 4 - 6) / 2;
        for (index = 0; index < pairs; index++) {
            if ((index % 4) == 0)
                printf("\n      ");
            printf("%6d->%-6d  ",
                   bad->bbb_BlockPairs[index].bbe_BadBlock,
                   bad->bbb_BlockPairs[index].bbe_GoodBlock);
        }
        printf("\n");

        badblock = bad->bbb_Next;
    }
    printf("\n");
    return (0);
}

static LONG
chksum(void *ptr)
{
    LONG size;
    LONG csum = 0;
    LONG *buf = (LONG *) ptr;

    size = buf[1];
    if ((size < 3) || (size > (TD_SECTOR >> 2))) {
        fprintf(stderr, "Invalid SummedLongs %d in block\n", size);
        return (1);
    }
    for (; size > 0; size--) {
        csum += *buf;
        buf++;
    }
    return (csum);
}

static int
fixsum(void *ptr, ULONG blk)
{
    LONG *temp;
    LONG size;
    LONG csum = 0;
    LONG *buf = (LONG *) ptr;

    temp = buf;
    size = buf[1];
    buf[2] = 0;

    if ((size < 3) || (size > (TD_SECTOR >> 2))) {
        fprintf(stderr, "Can not fix block %u: invalid SummedLongs %d\n",
                blk, size);
        return (1);
    }

    for (; size > 0; size--) {
        csum += *temp;
        temp++;
    }

    buf[2] = -csum;

    if (blk_write(buf, blk)) {
        fprintf(stderr, "fixed checksum could not be written!\n");
        return (1);
    }
    return (0);
}

static int
getans(void)
{
    int ch;
    int ret = 0;

    while ((ch = getchar()) != EOF) {
        if ((ch == 'n') || (ch == 'N')) {
            break;
        } else if ((ch == 'q') || (ch == 'Q')) {
            ret = 3;
            break;
        } else if ((ch == 'y') || (ch == 'Y')) {
            ret = 1;
            break;
        } else if ((ch == 'a') || (ch == 'A')) {
            ret = 2;
            break;
        }
    }

    if (ch == EOF)
        return (0);

    while ((ch = getchar()) != '\n')
        if (ch == EOF)
            return (0);
    return (ret);
}

static int
dofix(void *buf, ULONG blk, ULONG name)
{
    if (autofix) {
        printf("fixing block %d\n", blk);
        if (fixsum(buf, blk))
            return (1);
    } else {
        printf("\n");
        print_id(name);
        printf(": Block %d invalid, fix? (Yes/No/All/Quit)\n", blk);
        switch (getans()) {
            case NO:
                break;
            case QUIT:
                return (1);
            case ALL:
                autofix = 1;
                /* FALLTHROUGH */
            case YES:
                if (fixsum(buf, blk))
                    return (1);
                break;
        }
    }
    return (0);
}

static int
rigid_operation(void)
{
    char    *name;
    char    *newname;
    int     index;
    int     changeall = 0;
    int     count = 0;

    for (index = 0; index < NUM_ACCEPT; index++)
        if (changed[index] && accept[index].which == RIGID)
            count++;

    if (count == 0)
        return (0);

    if (!editrigid) {
        fprintf(stderr, "You must specify -rigid to edit RDB fields\n\n");
        return (1);
    }

    count = 0;
    for (index = 0; index < NUM_ACCEPT; index++)
        if (changed[index] && accept[index].which == RIGID) {
            printf("change %s from", accept[index].name);

            if (accept[index].type == STR) { /* it's a string */
                name = (char *) &((ULONG *) rdb)[accept[index].offset];
                newname = (char *) &((ULONG *) &newrigid)[accept[index].offset];

                printf(" %.*s to %.*s?\n", accept[index].size, name,
                       accept[index].size, newname);
            } else {                        /* it's a number */
                printf(" %d to %d?\n",
                       ((ULONG *)     rdb)[accept[index].offset],
                       ((ULONG *) &newrigid)[accept[index].offset]);
            }
            if (changeall)
                goto dochange;

            switch (getans()) {
                case NO:
                    continue;
                case QUIT:
                    return (1);
                case ALL:
                    changeall = 1;
                    /* FALLTHROUGH */
                case YES:
dochange:
                    count++;
                    if (accept[index].type == STR) { /* it's a string */
                        memcpy(name, newname, accept[index].size);
                    } else {                        /* it's a number */
                        ((ULONG *) rdb)[accept[index].offset] =
                              ((ULONG *) &newrigid)[accept[index].offset];
                    }
                    break;
            }
        }

    if (count)
        if (fixsum(rdb, rdb_blk))
            return (1);

    printf("\n");
    return (0);
}

static int
partition_operation(int partition, int parent)
{
    int   index;
    char *name;
    char *newname;
    int   changeall = 0;
    int   count = 0;
    int   bstr;

    if (!editpart && !promote)
        return (0);

    if (!name_matches(part->pb_DriveName, partname))
        return (0);
    bstr = (part->pb_DriveName[0] < 32);  /* else an IVS C string */
    partfound = 1;
    printf("found partition %s\n", partname);

    for (index = 0; index < NUM_ACCEPT; index++)
        if (changed[index] && accept[index].which == PART) {
            printf("change %s from", accept[index].name);

            if (accept[index].type == STR) {        /* it's a string */
                name = (char *) &((ULONG *) part)[accept[index].offset];
                newname = (char *) &((ULONG *) &newpart)[accept[index].offset];

                if (bstr)
                    printf(" %.*s to %s?\n", name[0], name + 1, newname);
                else
                    printf(" %s to %s?\n", name, newname);
            } else {                                /* it's a number */
                printf(" %d to %d?\n",
                       ((ULONG *)     part)[accept[index].offset],
                       ((ULONG *) &newpart)[accept[index].offset]);
            }
            if (changeall)
                goto dochange;

            switch (getans()) {
                case NO:
                    continue;
                case QUIT:
                    return (1);
                case ALL:
                    changeall = 1;
                    /* FALLTHROUGH */
                case YES:
dochange:
                    count++;
                    if (accept[index].type == STR) {/* it's a string */
                        int len = strlen(newname);
                        memset(name, 0, sizeof (part->pb_DriveName));
                        if (bstr) {
                            name[0] = len;
                            memcpy(name + 1, newname, len);
                        } else {    /* cursed IVS */
                            memcpy(name, newname, len);
                        }
                    } else {                        /* it's a number */
                        ((ULONG *) part)[accept[index].offset] =
                              ((ULONG *) &newpart)[accept[index].offset];
                    }
                    break;
            }
        }

    if (promote) {
        if (!(part->pb_Flags & PBFF_BOOTABLE)) {
            fprintf(stderr, "This partition may not be promoted unless the Bootable bit is set\n");
            exit_status = 1;
        } else if (partition == rdb->rdb_PartitionList) {
            fprintf(stderr, "This partition is already mounted first\n");
            exit_status = 1;
        } else {
            /* non-corrupting scheme:
                    1. Point parent of this partition's Next to oldchild
                    2. Point this partition's Next to newchild
                    3. Point rdb at this partition
            */
            printf("promote partition?\n");
            switch (getans()) {
                case NO:
                    goto skip_promote;
                case QUIT:
                    return (1);
            }

            /* 1. Point parent of this partition's Next to oldchild */
            if (parent == -1) {
                fprintf(stderr, "%s internal error, aborting changes\n",
                        progname);
                return (1);
            } else {
                if (blk_read(ppart, parent)) {
                    fprintf(stderr, "partition block %d could not be read\n",
                            parent);
                    return (1);
                }
                ppart->pb_Next = part->pb_Next;
                if (fixsum(ppart, parent))
                    return (1);
            }

            /* 2. Point this partition's Next to newchild */
            part->pb_Next = rdb->rdb_PartitionList;
            if (fixsum(part, partition))
                return (1);

            /* 3. Point rdb at this partition */
            rdb->rdb_PartitionList = partition;
            if (fixsum(rdb, rdb_blk))
                return (1);
            promoted = 1;

            count = 0;
        }
    }
skip_promote:

    if (count)
        if (fixsum(part, partition))
            return (1);
    return (0);
}

static void
filesystem_operation(int filesystem)
{
    char name[32];

    if (!extractfs)
        return;

    if (fs->fhb_DosType != extractid)
        return;
    extract_count++;

    printf("\nExtract this filesystem (%08x '", fs->fhb_DosType);
    print_id(fs->fhb_DosType);
    printf("') ?  (Yes/No)\n");
    switch (getans()) {
        case NO:
            return;
        case QUIT:
            exit_status = 1;
            return;
    }

    /* A second filesystem with the same DosType gets a .2 suffix, etc */
    if (extract_count == 1)
        sprintf(name, "%08X", fs->fhb_DosType);
    else
        sprintf(name, "%08X.%d", fs->fhb_DosType, extract_count);
    printf("Writing %s\n", name);
    if (extract_seglist(fs->fhb_SegListBlocks, name))
        exit_status = 1;
}

static int
extract_seglist(int seglist, const char *name)
{
    FILE *fp;
    struct LoadSegBlock *start = NULL;
    struct LoadSegBlock *current = NULL;
    struct LoadSegBlock *parent = NULL;
    int checkbad = 1;
    int len;
    ULONG count = 0;

    if (seglist == -1) {
        fprintf(stderr, "No seglist to extract for %s\n", name);
        return (1);
    }

    while (seglist != -1) {
        if (chain_loops(MAP_SEG, &count, seglist)) {
            fprintf(stderr, "seglist loops; truncating\n");
            break;
        }
        if (blk_read(seg, seglist)) {
            fprintf(stderr, "\nloadseg block %d could not be read\n", seglist);
            freelist(start);
            return (1);
        }
        if (checkbad && (seg->lsb_ID != IDNAME_LOADSEG)) {
            fprintf(stderr, "block %d does not contain a valid loadseg block\n",
                    seglist);
            printf("attempt to continue? (Yes/No/All)\n");
            switch (getans()) {
                case NO:
                    goto write_data;
                case QUIT:
                    freelist(start);
                    return (1);
                case ALL:
                    checkbad = 0;
                    break;
            }
        }
        current = (struct LoadSegBlock *) malloc(TD_SECTOR);
        if (current == NULL) {
            fprintf(stderr, "Unable to allocate memory buffer\n");
            freelist(start);
            return (1);
        }
        memcpy(current, seg, TD_SECTOR);
        current->lsb_Next = (ULONG) NULL;

        if (parent == NULL)
            start = current;
        else
            parent->lsb_Next = (ULONG) current;

        parent = current;

        seglist = seg->lsb_Next;
    }

    write_data:
    fp = fopen(name, "w");
    if (fp == NULL) {
        fprintf(stderr, "Unable to open output file %s\n", name);
        freelist(start);
        return (1);
    }

    current = start;
    while (current != NULL) {
        len = ((LONG) current->lsb_SummedLongs - 5) * 4;
        if (len > TD_SECTOR - 5 * 4)
            len = TD_SECTOR - 5 * 4;
        if (len > 0)
            fwrite(current->lsb_LoadData, len, 1, fp);
        current = (struct LoadSegBlock *) current->lsb_Next;
    }

    fclose(fp);
    sleep(1);
    freelist(start);
    return (0);
}

static void
freelist(struct LoadSegBlock *current)
{
    struct LoadSegBlock *temp;
    while (current != NULL) {
        temp = (struct LoadSegBlock *) current->lsb_Next;
        free(current);
        current = temp;
    }
}

/*
 * Returns 1 if the RDB keyword at the given longword offset was given
 * on the command line.
 */
static int
rigid_given(int offset)
{
    int index;

    for (index = 0; index < NUM_ACCEPT; index++)
        if (changed[index] && (accept[index].which == RIGID) &&
            (accept[index].offset == offset))
            return (1);
    return (0);
}

/*
 * Returns 1 if the partition keyword at the given longword offset was
 * given on the command line.
 */
static int
part_given(int offset)
{
    int index;

    for (index = 0; index < NUM_ACCEPT; index++)
        if (changed[index] && (accept[index].which == PART) &&
            (accept[index].offset == offset))
            return (1);
    return (0);
}

/*
 * Get the first and last block of a partition.  Returns 1 if the
 * geometry is invalid.
 */
static int
part_range(struct DosEnvec *env, ULONG *first, ULONG *last)
{
    ULONG cylblks = env->de_Surfaces * env->de_BlocksPerTrack;

    if ((cylblks == 0) || (env->de_HighCyl < env->de_LowCyl))
        return (1);
    *first = env->de_LowCyl * cylblks;
    *last  = (env->de_HighCyl + 1) * cylblks - 1;
    return (0);
}

#define RDB_AREA_MIN_BLKS 2048  /* Room for partitions and filesystems */

/*
 * Get the drive geometry from the device.  Returns 0 on success.
 */
static int
get_geometry(struct DriveGeometry *dg)
{
    memset(dg, 0, sizeof (*dg));
    trackIO->iotd_Req.io_Command = TD_GETGEOMETRY;
    trackIO->iotd_Req.io_Length  = sizeof (*dg);
    trackIO->iotd_Req.io_Data    = dg;
    trackIO->iotd_Req.io_Offset  = 0;
    if ((DoIO((struct IORequest *) trackIO) != 0) || (dg->dg_SectorSize == 0))
        return (1);
    return (0);
}

/*
 * Build a new Rigid Disk Block in r, using the drive geometry reported
 * by the device.  RDB keywords on the command line override any field.
 * Returns 0 on success and 1 on error.
 */
static int
build_rdb(struct RigidDiskBlock *r)
{
    struct DriveGeometry dg;
    ULONG  cyls;
    ULONG  heads;
    ULONG  secs;
    ULONG  cylblocks;
    ULONG  rdbcyls;
    ULONG *lrdb = (ULONG *) r;
    int    have_geom = 0;
    int    index;

    if (get_geometry(&dg) == 0) {
        have_geom = 1;
        printf("Drive geometry: SectorSize=%u TotalSectors=%u Cylinders=%u "
               "Heads=%u TrackSectors=%u\n", dg.dg_SectorSize,
               dg.dg_TotalSectors, dg.dg_Cylinders, dg.dg_Heads,
               dg.dg_TrackSectors);
        if (dg.dg_SectorSize != TD_SECTOR) {
            fprintf(stderr, "Sector size %u is not supported; only %d-byte "
                    "sectors\n", dg.dg_SectorSize, TD_SECTOR);
            return (1);
        }
    }

    heads = have_geom ? dg.dg_Heads : 0;
    secs  = have_geom ? dg.dg_TrackSectors : 0;
    cyls  = have_geom ? dg.dg_Cylinders : 0;
    if (rigid_given(18))
        heads = newrigid.rdb_Heads;
    if (rigid_given(17))
        secs = newrigid.rdb_Sectors;
    cylblocks = heads * secs;
    if (cylblocks == 0) {
        fprintf(stderr, "Drive geometry is not available; specify "
                "Cylinders, Sectors, and Heads\n");
        return (1);
    }
    if (have_geom && (dg.dg_TotalSectors != 0))
        cyls = dg.dg_TotalSectors / cylblocks;
    if (rigid_given(16))
        cyls = newrigid.rdb_Cylinders;

    rdbcyls = (RDB_AREA_MIN_BLKS + cylblocks - 1) / cylblocks;
    if (rdbcyls < 2)
        rdbcyls = 2;
    if (cyls <= rdbcyls) {
        fprintf(stderr, "Disk is too small: %u cylinders of %u blocks\n",
                cyls, cylblocks);
        return (1);
    }

    memset(r, 0, TD_SECTOR);
    r->rdb_ID                = IDNAME_RIGIDDISK;
    r->rdb_SummedLongs       = sizeof (struct RigidDiskBlock) / 4;
    r->rdb_HostID            = 7;
    r->rdb_BlockBytes        = TD_SECTOR;
    r->rdb_Flags             = 0;
    r->rdb_BadBlockList      = -1;
    r->rdb_PartitionList     = -1;
    r->rdb_FileSysHeaderList = -1;
    r->rdb_DriveInit         = -1;
    for (index = 10; index < 16; index++)   /* rdb_Reserved1[] */
        lrdb[index] = 0xffffffff;
    r->rdb_Cylinders         = cyls;
    r->rdb_Sectors           = secs;
    r->rdb_Heads             = heads;
    r->rdb_Interleave        = 1;
    r->rdb_Park              = cyls;
    r->rdb_WritePreComp      = cyls;
    r->rdb_ReducedWrite      = cyls;
    r->rdb_StepRate          = 3;
    r->rdb_RDBBlocksLo       = 0;
    r->rdb_RDBBlocksHi       = rdbcyls * cylblocks - 1;
    r->rdb_LoCylinder        = rdbcyls;
    r->rdb_HiCylinder        = cyls - 1;
    r->rdb_CylBlocks         = cylblocks;
    r->rdb_AutoParkSeconds   = 0;
    RDB_HIGHRDSKBLOCK(r)     = 0;

    /* Apply RDB keywords given on the command line */
    for (index = 0; index < NUM_ACCEPT; index++) {
        if (!changed[index] || (accept[index].which != RIGID))
            continue;
        if (accept[index].type == STR)
            memcpy(&lrdb[accept[index].offset],
                   &((ULONG *) &newrigid)[accept[index].offset],
                   accept[index].size);
        else
            lrdb[accept[index].offset] =
                ((ULONG *) &newrigid)[accept[index].offset];
        changed[index] = 0;  /* Consumed; rigid_operation() skips it */
    }

    if ((r->rdb_CylBlocks == 0) ||
        (r->rdb_LoCylinder * r->rdb_CylBlocks <= r->rdb_RDBBlocksHi) ||
        (r->rdb_HiCylinder < r->rdb_LoCylinder) ||
        (r->rdb_HiCylinder >= r->rdb_Cylinders) ||
        (r->rdb_RDBBlocksHi < RDB_LOCATION_LIMIT)) {
        fprintf(stderr, "Invalid RDB layout: CylBlocks=%u RDBBlocksHi=%u "
                "LoCylinder=%u HiCylinder=%u Cylinders=%u\n",
                r->rdb_CylBlocks, r->rdb_RDBBlocksHi,
                r->rdb_LoCylinder, r->rdb_HiCylinder,
                r->rdb_Cylinders);
        return (1);
    }

    printf("New RDB: Cylinders=%u Heads=%u Sectors=%u CylBlocks=%u "
           "RDBBlocks=%u-%u\n         Partitionable cylinders %u-%u\n",
           r->rdb_Cylinders, r->rdb_Heads, r->rdb_Sectors,
           r->rdb_CylBlocks, r->rdb_RDBBlocksLo, r->rdb_RDBBlocksHi,
           r->rdb_LoCylinder, r->rdb_HiCylinder);
    return (0);
}

/*
 * Create a new RDB in block 0 (-newrdb).  Returns 0 if the RDB was
 * written, -1 if the user declined, and 1 on error.
 */
static int
create_rdb(int nonempty)
{
    if (build_rdb(rdb))
        return (1);

    if (nonempty)
        printf("WARNING: %d of the first %d blocks are not empty; "
               "the disk may contain data\n", nonempty, RDB_LOCATION_LIMIT);
    printf("Write a new RDB to block 0 of %s unit %d? (Yes/No)\n",
           disk_device, disk_unit);
    switch (getans()) {
        case YES:
        case ALL:
            break;
        case QUIT:
            return (1);
        default:
            return (-1);
    }
    if (fixsum(rdb, 0))
        return (1);
    rdb_blk = 0;
    printf("RDB written\n\n");
    return (0);
}

static int
create_partition(void)
{
    ULONG newblk;
    ULONG count = 0;
    ULONG first;
    ULONG last;
    ULONG nfirst;
    ULONG nlast;
    ULONG ncylblks;
    struct DosEnvec *nenv;
    int previous = -1;
    int current;
    int len;

    /*
     * Fields not given on the command line default to the RDB geometry,
     * covering the whole partitionable area of the disk.
     */
    nenv = (struct DosEnvec *) newpart.pb_Environment;
    if (!part_given(32 + 3))
        nenv->de_Surfaces = rdb->rdb_Heads;
    if (!part_given(32 + 5))
        nenv->de_BlocksPerTrack = rdb->rdb_Sectors;
    ncylblks = nenv->de_Surfaces * nenv->de_BlocksPerTrack;
    if (ncylblks == 0) {
        fprintf(stderr, "Surfaces and BlocksPerTrack must not be 0\n");
        return (1);
    }
    if (!part_given(32 + 9))
        nenv->de_LowCyl = (rdb->rdb_LoCylinder * rdb->rdb_CylBlocks +
                           ncylblks - 1) / ncylblks;
    if (!part_given(32 + 10))
        nenv->de_HighCyl = (rdb->rdb_HiCylinder + 1) * rdb->rdb_CylBlocks /
                           ncylblks - 1;
    if (part_range(nenv, &nfirst, &nlast)) {
        fprintf(stderr, "Invalid partition range LowCyl=%u HighCyl=%u\n",
                nenv->de_LowCyl, nenv->de_HighCyl);
        return (1);
    }
    if ((nfirst < rdb->rdb_LoCylinder * rdb->rdb_CylBlocks) ||
        (nfirst <= rdb->rdb_RDBBlocksHi) ||
        (nlast >= (rdb->rdb_HiCylinder + 1) * rdb->rdb_CylBlocks)) {
        fprintf(stderr, "Partition blocks %u-%u are outside the "
                "partitionable area, blocks %u-%u\n", nfirst, nlast,
                rdb->rdb_LoCylinder * rdb->rdb_CylBlocks,
                (rdb->rdb_HiCylinder + 1) * rdb->rdb_CylBlocks - 1);
        return (1);
    }
    printf("New partition %s: LowCyl=%u HighCyl=%u (blocks %u-%u)\n",
           partname, nenv->de_LowCyl, nenv->de_HighCyl, nfirst, nlast);

    /*
     * Add partition to the end of list of partitions so that
     * AmigaDOS won't try to boot from it because it's first
     */
    newblk = maxaccess + 1;
    if (newblk > rdb->rdb_RDBBlocksHi) {
        fprintf(stderr, "No free block in RDB area (next=%u RDBBlocksHi=%u)\n",
                newblk, rdb->rdb_RDBBlocksHi);
        return (1);
    }

    /* Find the last partition in the list */
    current = rdb->rdb_PartitionList;
    while (current != -1) {
        if (chain_loops(MAP_PART, &count, current)) {
            fprintf(stderr, "partition list loops; giving up\n");
            return (1);
        }
        if (blk_read(part, current)) {
            fprintf(stderr, "partition block %d could not be read\n",
                    current);
            return (1);
        }

        if (part->pb_ID != IDNAME_PARTITION) {
            fprintf(stderr, "block %d does not contain a valid partition!\n",
                    current);
            return (1);
        }

        if (chksum(part) && dofix(part, current, IDNAME_PARTITION))
            return (1);

        if (!part_range((struct DosEnvec *) part->pb_Environment,
                        &first, &last) &&
            (first <= nlast) && (nfirst <= last)) {
            fprintf(stderr, "New partition blocks %u-%u overlap partition "
                    "at block %d (blocks %u-%u)\n", nfirst, nlast,
                    current, first, last);
            return (1);
        }

        previous = current;
        current = part->pb_Next;
    }
    if (previous != -1)
        memcpy(ppart, part, TD_SECTOR);

    printf("Create a New partition with the device name %s? (Yes/No)\n",
            partname);
    switch (getans()) {
        case NO:
            return (0);
        case QUIT:
            return (1);
    }

    /* Create the new partition block */
    len = strlen(partname);
    if (len > 31) {
        fprintf(stderr, "Partition name %s is too long\n", partname);
        return (1);
    }
    newpart.pb_Next = -1;
    memset(newpart.pb_DriveName, 0, sizeof (newpart.pb_DriveName));
    if (any_bstr || !any_ivs) {  /* BSTR unless only IVS names exist */
        newpart.pb_DriveName[0] = len;
        memcpy(newpart.pb_DriveName + 1, partname, len);
    } else {
        memcpy(newpart.pb_DriveName, partname, len);
    }
    memset(part, 0, TD_SECTOR);
    memcpy(part, &newpart, sizeof (newpart));
    if (fixsum(part, newblk))
        return (1);

    /* Link it in */
    if (previous == -1) {   /* then modify RDB (first partition in RDB) */
        rdb->rdb_PartitionList = newblk;
    } else {
        ppart->pb_Next = newblk;
        if (fixsum(ppart, previous))
            return (1);
    }
    if ((previous == -1) || (RDB_HIGHRDSKBLOCK(rdb) < newblk)) {
        if (RDB_HIGHRDSKBLOCK(rdb) < newblk)
            RDB_HIGHRDSKBLOCK(rdb) = newblk;
        if (fixsum(rdb, rdb_blk))
            return (1);
    }
    maxaccess = newblk;
    return (0);
}

static void
do_creations(void)
{
    if (rdb->rdb_ID != IDNAME_RIGIDDISK)
        return;

    if (!createpart)
        return;

    if (scan_error) {
        fprintf(stderr, "The RDB could not be read completely; "
                "not creating partition %s\n", partname);
        exit_status = 1;
        return;
    }
    if (create_partition())
        exit_status = 1;
}

/*
 * ====================================================================
 * In-memory RDB model, used by -check, -fix, and -edit.  Every block
 * of the RDB is read into memory, then checked or changed there, and
 * written back only on request.
 * ====================================================================
 */

#define OWN_FREE 0
#define OWN_RDSK 1
#define OWN_BADB 2
#define OWN_PART 3
#define OWN_FSHD 4
#define OWN_LSEG 5
#define OWN_INIT 6

static const char *own_name[] = {
    "free", "RDB", "bad block list", "partition list",
    "filesystem list", "filesystem code", "DriveInit code"
};

static ULONG own_id[] = {
    0, IDNAME_RIGIDDISK, IDNAME_BADBLOCK, IDNAME_PARTITION,
    IDNAME_FILESYSHEADER, IDNAME_LOADSEG, IDNAME_LOADSEG
};

typedef struct {
    ULONG blk;              /* Block number on disk */
    UBYTE own;              /* OWN_* */
    UBYTE dirty;            /* Must be written */
    ULONG d[TD_SECTOR / 4]; /* Block contents */
} mblk_t;

#define MAX_PARTS    64
#define MAX_FS       32
#define MAX_BAD      64
#define MAX_PROBLEMS 100

static mblk_t **mall = NULL;    /* Every block loaded or created */
static int    nall = 0;
static int    maxall = 0;
static int    m_rdb = -1;       /* mall index of the RDB, -1 if none */
static int    m_part[MAX_PARTS];
static int    m_nparts = 0;
static int    m_fs[MAX_FS];
static int    m_nfs = 0;
static int    m_seg_first[MAX_FS];
static int    m_seg_count[MAX_FS];
static int    m_bad[MAX_BAD];
static int    m_nbad = 0;
static UBYTE *m_owner = NULL;   /* OWN_* of each block < MAP_MAX_BLKS */
static ULONG  m_highest = 0;    /* Highest block used by the RDB */
static ULONG  m_disk_blocks = 0; /* Disk size, 0 if unknown */
static int    m_nonempty = 0;   /* Non-empty blocks in RDB probe area */

#define MR     ((struct RigidDiskBlock *) mall[m_rdb]->d)
#define MRL    (mall[m_rdb]->d)
#define MP(i)  ((struct PartitionBlock *) mall[m_part[i]]->d)
#define MPE(i) ((struct DosEnvec *) MP(i)->pb_Environment)
#define MF(i)  ((struct FileSysHeaderBlock *) mall[m_fs[i]]->d)

#define SEV_ERROR   'E'
#define SEV_WARNING 'W'

#define FIX_NONE     0
#define FIX_CSUM     1  /* Recompute checksum of mall[a] */
#define FIX_TRUNC    2  /* End chain at mall[a], or RDB field v if a < 0 */
#define FIX_SUMMED   3  /* Set SummedLongs of mall[a] to v */
#define FIX_HIGHRDSK 4  /* Set HighRDSKBlock to v */
#define FIX_HICYL    5  /* Set HiCylinder to v */
#define FIX_RENAME   6  /* Give partition mall[a] a unique name */

typedef struct {
    char  sev;
    char  fix;
    char  done;
    short a;
    ULONG v;
    char  msg[100];
} problem_t;

static problem_t prob[MAX_PROBLEMS];
static int       nprob = 0;
static int       nprob_load = 0;    /* Problems found while loading */

static void
add_problem(int sev, int fix, int a, ULONG v, const char *fmt, ...)
{
    char    buf[256];
    va_list va;

    if (nprob >= MAX_PROBLEMS)
        return;
    va_start(va, fmt);
    vsprintf(buf, fmt, va);
    va_end(va);
    prob[nprob].sev  = sev;
    prob[nprob].fix  = fix;
    prob[nprob].done = 0;
    prob[nprob].a    = a;
    prob[nprob].v    = v;
    strncpy(prob[nprob].msg, buf, sizeof (prob[nprob].msg) - 1);
    prob[nprob].msg[sizeof (prob[nprob].msg) - 1] = '\0';
    nprob++;
}

/* Format a DosType or block ID the way print_id() does */
static char *
id_str(ULONG id, char *buf)
{
    char  *p = buf;
    int    index;
    UBYTE  ch;

    for (index = 0; index < 4; index++) {
        ch = (id >> (8 * (3 - index))) & 255;
        if ((ch < 32) || (ch > 126) || (ch == '\\'))
            p += sprintf(p, "\\%d", ch);
        else
            *p++ = ch;
    }
    *p = '\0';
    return (buf);
}

/* Format a size given in 512-byte blocks */
static char *
size_str(ULONG blocks, char *buf)
{
    ULONG kb = blocks / 2;
    ULONG mb;

    if (kb < 10240) {
        sprintf(buf, "%uK", kb);
    } else {
        mb = kb / 1024;
        if (mb < 1024)
            sprintf(buf, "%u.%uM", mb, (kb % 1024) * 10 / 1024);
        else
            sprintf(buf, "%u.%uG", mb / 1024, (mb % 1024) * 10 / 1024);
    }
    return (buf);
}

/* Partition DriveName (BSTR or IVS C string) as a C string */
static char *
part_name_str(struct PartitionBlock *p, char *buf)
{
    UBYTE *n = p->pb_DriveName;
    int    len;

    if (n[0] < 32) {
        len = n[0];
        memcpy(buf, n + 1, len);
    } else {
        for (len = 0; (len < 31) && (n[len] != '\0'); len++)
            buf[len] = n[len];
    }
    buf[len] = '\0';
    return (buf);
}

static void
set_part_name(struct PartitionBlock *p, const char *name, int bstr)
{
    int len = strlen(name);

    memset(p->pb_DriveName, 0, sizeof (p->pb_DriveName));
    if (bstr) {
        p->pb_DriveName[0] = len;
        memcpy(p->pb_DriveName + 1, name, len);
    } else {
        memcpy(p->pb_DriveName, name, len);
    }
}

static int
mblk_add(ULONG blk, int own)
{
    mblk_t  *m;
    mblk_t **n;
    int      newmax;

    if (nall >= maxall) {
        newmax = (maxall == 0) ? 64 : maxall * 2;
        if (mall == NULL)
            n = (mblk_t **) malloc(newmax * sizeof (mblk_t *));
        else
            n = (mblk_t **) realloc(mall, newmax * sizeof (mblk_t *));
        if (n == NULL)
            return (-1);
        mall = n;
        maxall = newmax;
    }
    m = (mblk_t *) calloc(1, sizeof (mblk_t));
    if (m == NULL)
        return (-1);
    m->blk = blk;
    m->own = own;
    mall[nall] = m;
    return (nall++);
}

static void
mblk_drop_last(void)
{
    if (nall > 0)
        free(mall[--nall]);
}

static void
m_free(void)
{
    while (nall > 0)
        mblk_drop_last();
    if (m_owner != NULL)
        free(m_owner);
    m_owner = NULL;
    m_rdb = -1;
    m_nparts = 0;
    m_nfs = 0;
    m_nbad = 0;
    m_highest = 0;
}

static ULONG
m_csum(ULONG *d)
{
    ULONG sum = 0;
    ULONG pos;

    for (pos = 0; pos < d[1]; pos++)
        sum += d[pos];
    return (sum);
}

static void
m_setsum(ULONG *d)
{
    if ((d[1] < 3) || (d[1] > TD_SECTOR / 4))
        return;
    d[2] = 0;
    d[2] = -m_csum(d);
}

/* Check SummedLongs and checksum of a loaded block */
static void
m_check_block(int idx)
{
    ULONG *d   = mall[idx]->d;
    int    own = mall[idx]->own;

    if ((d[1] < 3) || (d[1] > TD_SECTOR / 4)) {
        if ((own == OWN_RDSK) || (own == OWN_PART) || (own == OWN_FSHD))
            add_problem(SEV_ERROR, FIX_SUMMED, idx, 64,
                        "block %u (%s) has invalid SummedLongs %u",
                        mall[idx]->blk, own_name[own], d[1]);
        else
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "block %u (%s) has invalid SummedLongs %u",
                        mall[idx]->blk, own_name[own], d[1]);
    } else if (m_csum(d) != 0) {
        add_problem(SEV_ERROR, FIX_CSUM, idx, 0,
                    "block %u (%s) has a bad checksum",
                    mall[idx]->blk, own_name[own]);
    }
}

/*
 * Load one chain of RDB blocks.  The chain starts at blk, which comes
 * from RDB field rdb_field (or -1 for a seglist).  The mall index of
 * each block is stored in list (if not NULL), and the number of blocks
 * in *count.  Loading stops at the first broken link.
 */
static void
m_load_chain(int own, ULONG blk, int rdb_field, int *list, int max,
             int *count)
{
    int    prev = -1;
    int    idx;
    int    fix = ((own == OWN_LSEG) || (own == OWN_INIT)) ? FIX_NONE :
                                                            FIX_TRUNC;
    ULONG *d;
    char   ib1[20];
    char   ib2[20];

    *count = 0;
    while (blk != 0xffffffff) {
        if (*count >= max) {
            add_problem(SEV_ERROR, fix, prev, rdb_field,
                        "the %s has more than %d entries",
                        own_name[own], max);
            return;
        }
        if ((blk < MAP_MAX_BLKS) && (m_owner[blk] != OWN_FREE)) {
            if (m_owner[blk] == own)
                add_problem(SEV_ERROR, fix, prev, rdb_field,
                            "the %s loops back to block %u",
                            own_name[own], blk);
            else
                add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                            "block %u is in both the %s and the %s", blk,
                            own_name[m_owner[blk]], own_name[own]);
            return;
        }
        idx = mblk_add(blk, own);
        if (idx < 0) {
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "out of memory loading the %s", own_name[own]);
            return;
        }
        d = mall[idx]->d;
        if (blk_read(d, blk)) {
            mblk_drop_last();
            add_problem(SEV_ERROR, fix, prev, rdb_field,
                        "block %u of the %s could not be read",
                        blk, own_name[own]);
            return;
        }
        if (d[0] != own_id[own]) {
            id_str(d[0], ib1);
            id_str(own_id[own], ib2);
            mblk_drop_last();
            add_problem(SEV_ERROR, fix, prev, rdb_field,
                        "block %u of the %s has ID '%s', not '%s'",
                        blk, own_name[own], ib1, ib2);
            return;
        }
        if ((blk < MR->rdb_RDBBlocksLo) || (blk > MR->rdb_RDBBlocksHi))
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "block %u of the %s is outside the RDB area",
                        blk, own_name[own]);
        if (blk < MAP_MAX_BLKS)
            m_owner[blk] = own;
        if (blk > m_highest)
            m_highest = blk;
        m_check_block(idx);
        if (list != NULL)
            list[*count] = idx;
        (*count)++;
        prev = idx;
        blk = d[4];  /* Next */
    }
}

/* Load the whole RDB at rdb_blk (already read into "rdb") */
static int
m_load(void)
{
    int index;
    int count;

    m_free();
    m_owner = (UBYTE *) calloc(MAP_MAX_BLKS, 1);
    if ((m_owner == NULL) || ((m_rdb = mblk_add(rdb_blk, OWN_RDSK)) < 0)) {
        fprintf(stderr, "Out of memory\n");
        return (1);
    }
    memcpy(MRL, rdb, TD_SECTOR);
    if (rdb_blk < MAP_MAX_BLKS)
        m_owner[rdb_blk] = OWN_RDSK;
    m_highest = rdb_blk;
    m_check_block(m_rdb);

    m_load_chain(OWN_BADB, MR->rdb_BadBlockList, 6, m_bad, MAX_BAD,
                 &m_nbad);
    m_load_chain(OWN_PART, MR->rdb_PartitionList, 7, m_part, MAX_PARTS,
                 &m_nparts);
    m_load_chain(OWN_FSHD, MR->rdb_FileSysHeaderList, 8, m_fs, MAX_FS,
                 &m_nfs);
    for (index = 0; index < m_nfs; index++) {
        m_seg_first[index] = nall;
        m_load_chain(OWN_LSEG, MF(index)->fhb_SegListBlocks, -1, NULL,
                     chain_limit(), &m_seg_count[index]);
    }
    m_load_chain(OWN_INIT, MR->rdb_DriveInit, 9, NULL, chain_limit(),
                 &count);
    nprob_load = nprob;
    return (0);
}

/* Is name used by a partition other than mall[except]? */
static int
m_name_used(const char *name, int except)
{
    int index;

    for (index = 0; index < m_nparts; index++)
        if ((m_part[index] != except) &&
            name_matches(MP(index)->pb_DriveName, name))
            return (1);
    return (0);
}

/* Validate a new partition name; prints the reason if it is bad */
static int
m_bad_name(const char *name, int except)
{
    if ((name[0] == '\0') || (strlen(name) > 31)) {
        printf("A name must be 1 to 31 characters\n");
        return (1);
    }
    if ((strchr(name, ':') != NULL) || (strchr(name, '/') != NULL)) {
        printf("A name may not contain ':' or '/'\n");
        return (1);
    }
    if (m_name_used(name, except)) {
        printf("A partition named %s already exists\n", name);
        return (1);
    }
    return (0);
}

/* Partition range in blocks; returns 1 if it can not be computed */
static int
m_part_range(int index, ULONG *first, ULONG *last)
{
    if (MPE(index)->de_SizeBlock != TD_SECTOR / 4)
        return (1);
    return (part_range(MPE(index), first, last));
}

static void
m_check(void)
{
    struct RigidDiskBlock *r = MR;
    struct PartitionBlock *p;
    struct DosEnvec       *e;
    ULONG  cb = r->rdb_CylBlocks;
    ULONG  hi = RDB_HIGHRDSKBLOCK(r);
    ULONG  area_first = 0;
    ULONG  area_last = 0;
    ULONG  disk_last = 0;
    ULONG  first[MAX_PARTS];
    ULONG  last[MAX_PARTS];
    char   valid[MAX_PARTS];
    char   name[40];
    char   name2[40];
    char   ib[20];
    int    index;
    int    other;

    /* RDB header */
    if (r->rdb_Heads * r->rdb_Sectors != cb)
        add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                    "RDB CylBlocks %u is not Heads %u x Sectors %u",
                    cb, r->rdb_Heads, r->rdb_Sectors);
    if (r->rdb_RDBBlocksHi < r->rdb_RDBBlocksLo)
        add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                    "RDBBlocksHi %u is below RDBBlocksLo %u",
                    r->rdb_RDBBlocksHi, r->rdb_RDBBlocksLo);
    if (cb == 0) {
        add_problem(SEV_ERROR, FIX_NONE, 0, 0, "RDB CylBlocks is 0");
    } else {
        area_first = r->rdb_LoCylinder * cb;
        area_last  = (r->rdb_HiCylinder + 1) * cb - 1;
        disk_last  = r->rdb_Cylinders * cb - 1;
        if ((m_disk_blocks != 0) && (disk_last >= m_disk_blocks)) {
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "the RDB describes %u blocks, but the disk has %u",
                        disk_last + 1, m_disk_blocks);
            disk_last = m_disk_blocks - 1;
        }
        if (area_first <= r->rdb_RDBBlocksHi)
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "LoCylinder %u (block %u) is inside the RDB area "
                        "(blocks %u-%u)", r->rdb_LoCylinder, area_first,
                        r->rdb_RDBBlocksLo, r->rdb_RDBBlocksHi);
        if (r->rdb_HiCylinder >= r->rdb_Cylinders)
            add_problem(SEV_ERROR, FIX_HICYL, 0, r->rdb_Cylinders - 1,
                        "HiCylinder %u is not below Cylinders %u",
                        r->rdb_HiCylinder, r->rdb_Cylinders);
        else if (r->rdb_HiCylinder < r->rdb_LoCylinder)
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "HiCylinder %u is below LoCylinder %u",
                        r->rdb_HiCylinder, r->rdb_LoCylinder);
    }
    if ((hi < m_highest) || (hi > r->rdb_RDBBlocksHi))
        add_problem(SEV_WARNING, FIX_HIGHRDSK, 0, m_highest,
                    "HighRDSKBlock is %u; the highest RDB block used is %u",
                    hi, m_highest);

    /* Partitions */
    for (index = 0; index < m_nparts; index++) {
        p = MP(index);
        e = MPE(index);
        valid[index] = 0;
        part_name_str(p, name);
        if (name[0] == '\0') {
            add_problem(SEV_ERROR, FIX_RENAME, m_part[index], 0,
                        "partition %d (block %u) has no name", index + 1,
                        mall[m_part[index]]->blk);
            sprintf(name, "#%d", index + 1);
        } else {
            if (p->pb_DriveName[0] >= 32)
                add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                            "partition %s name is an old IVS C string, "
                            "not a BSTR", name);
            for (other = 0; other < index; other++)
                if (name_matches(MP(other)->pb_DriveName, name)) {
                    add_problem(SEV_ERROR, FIX_RENAME, m_part[index], 0,
                                "partitions %d and %d are both named %s",
                                other + 1, index + 1, name);
                    break;
                }
        }
        if (e->de_TableSize < DE_DOSTYPE)
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s environment has only %u entries",
                        name, e->de_TableSize);
        if ((p->pb_Flags & PBFF_BOOTABLE) && (e->de_Reserved < 2))
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s is bootable but reserves %u boot "
                        "blocks", name, e->de_Reserved);
        if (e->de_NumBuffers == 0)
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s has NumBuffers 0", name);
        if (e->de_MaxTransfer == 0)
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s has MaxTransfer 0", name);
        if (e->de_Mask == 0)
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s has Mask 0", name);
        if ((e->de_DosType & 0xffffff00) != 0x444f5300) {
            for (other = 0; other < m_nfs; other++)
                if (MF(other)->fhb_DosType == e->de_DosType)
                    break;
            if (other == m_nfs)
                add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                            "partition %s DosType %s has no filesystem in "
                            "the RDB", name, id_str(e->de_DosType, ib));
        }
        if (e->de_SizeBlock != TD_SECTOR / 4) {
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s uses %u-byte blocks; its range was "
                        "not checked", name, e->de_SizeBlock * 4);
            continue;
        }
        if (part_range(e, &first[index], &last[index])) {
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "partition %s has invalid geometry (Surfaces=%u "
                        "BlocksPerTrack=%u LowCyl=%u HighCyl=%u)", name,
                        e->de_Surfaces, e->de_BlocksPerTrack,
                        e->de_LowCyl, e->de_HighCyl);
            continue;
        }
        valid[index] = 1;
        if (first[index] <= r->rdb_RDBBlocksHi)
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "partition %s (blocks %u-%u) overlaps the RDB area",
                        name, first[index], last[index]);
        else if ((cb != 0) && (first[index] < area_first))
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s starts before LoCylinder", name);
        if ((cb != 0) && (last[index] > disk_last))
            add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                        "partition %s (blocks %u-%u) extends past the end "
                        "of the disk", name, first[index], last[index]);
        else if ((cb != 0) && (last[index] > area_last))
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "partition %s extends past HiCylinder", name);
        for (other = 0; other < index; other++)
            if (valid[other] && (first[index] <= last[other]) &&
                (first[other] <= last[index]))
                add_problem(SEV_ERROR, FIX_NONE, 0, 0,
                            "partitions %s and %s overlap", name,
                            part_name_str(MP(other), name2));
    }

    /* Filesystems */
    for (index = 0; index < m_nfs; index++) {
        if (m_seg_count[index] == 0)
            add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                        "filesystem %s at block %u has no code",
                        id_str(MF(index)->fhb_DosType, ib),
                        mall[m_fs[index]]->blk);
        for (other = 0; other < index; other++)
            if ((MF(other)->fhb_DosType == MF(index)->fhb_DosType) &&
                (MF(other)->fhb_Version == MF(index)->fhb_Version))
                add_problem(SEV_WARNING, FIX_NONE, 0, 0,
                            "filesystems at blocks %u and %u are the same "
                            "DosType and version", mall[m_fs[other]]->blk,
                            mall[m_fs[index]]->blk);
    }
}

/* Print the problems found; returns the error and warning counts */
static void
m_report(int *errors, int *warnings)
{
    int index;

    *errors = 0;
    *warnings = 0;
    for (index = 0; index < nprob; index++) {
        printf("%s: %s%s\n",
               (prob[index].sev == SEV_ERROR) ? "Error" : "Warning",
               prob[index].msg,
               prob[index].done ? " (fixed)" :
               (prob[index].fix != FIX_NONE) ? " (fixable)" : "");
        if (prob[index].done)
            continue;
        if (prob[index].sev == SEV_ERROR)
            (*errors)++;
        else
            (*warnings)++;
    }
    if (nprob == 0)
        printf("No problems found\n");
    else
        printf("%d error%s, %d warning%s\n", *errors,
               (*errors == 1) ? "" : "s", *warnings,
               (*warnings == 1) ? "" : "s");
    if (nprob >= MAX_PROBLEMS)
        printf("(only the first %d problems are shown)\n", MAX_PROBLEMS);
}

/* Give partition mall[idx] a unique name based on its current name */
static void
m_unique_name(int idx)
{
    struct PartitionBlock *p = (struct PartitionBlock *) mall[idx]->d;
    char base[40];
    char name[48];
    int  num;

    part_name_str(p, base);
    if (base[0] == '\0')
        strcpy(base, "PART");
    base[24] = '\0';
    for (num = 1; num < 1000; num++) {
        sprintf(name, "%s_%d", base, num);
        if (!m_name_used(name, idx))
            break;
    }
    set_part_name(p, name, (p->pb_DriveName[0] < 32));
    printf("    partition renamed to %s\n", name);
}

/* Apply all fixable problems to the model; returns the number fixed */
static int
m_apply_fixes(void)
{
    problem_t *pr;
    int        index;
    int        count = 0;

    for (index = 0; index < nprob; index++) {
        pr = &prob[index];
        if ((pr->fix == FIX_NONE) || pr->done)
            continue;
        printf("Fixing: %s\n", pr->msg);
        switch (pr->fix) {
            case FIX_CSUM:
                mall[pr->a]->dirty = 1;
                break;
            case FIX_TRUNC:
                if (pr->a < 0) {
                    MRL[pr->v] = 0xffffffff;
                    mall[m_rdb]->dirty = 1;
                    printf("    the list is now empty\n");
                } else {
                    mall[pr->a]->d[4] = 0xffffffff;
                    mall[pr->a]->dirty = 1;
                    printf("    the list now ends at block %u\n",
                           mall[pr->a]->blk);
                }
                break;
            case FIX_SUMMED:
                mall[pr->a]->d[1] = pr->v;
                mall[pr->a]->dirty = 1;
                break;
            case FIX_HIGHRDSK:
                RDB_HIGHRDSKBLOCK(MR) = pr->v;
                mall[m_rdb]->dirty = 1;
                break;
            case FIX_HICYL:
                MR->rdb_HiCylinder = pr->v;
                mall[m_rdb]->dirty = 1;
                break;
            case FIX_RENAME:
                m_unique_name(pr->a);
                mall[pr->a]->dirty = 1;
                break;
        }
        pr->done = 1;
        count++;
    }
    return (count);
}

static int
m_dirty_count(void)
{
    int index;
    int count = 0;

    for (index = 0; index < nall; index++)
        if (mall[index]->dirty)
            count++;
    return (count);
}

/*
 * Write all changed blocks, after confirmation.  The RDB is written
 * last, so a new chain is complete before the RDB points to it.
 * Returns 0 if written, -1 if declined, and 1 on error.
 */
static int
m_write(void)
{
    int   index;
    int   pass;
    int   count = m_dirty_count();
    char  ib[20];

    if (count == 0) {
        printf("No changes to write\n");
        return (0);
    }
    printf("Blocks to write:");
    for (index = 0; index < nall; index++) {
        if (!mall[index]->dirty)
            continue;
        /* Show a run of consecutive blocks of the same type as a range */
        for (pass = index; (pass + 1 < nall) && mall[pass + 1]->dirty &&
                           (mall[pass + 1]->blk == mall[pass]->blk + 1) &&
                           (mall[pass + 1]->d[0] == mall[index]->d[0]);
             pass++)
            ;
        if (pass == index)
            printf(" %u(%s)", mall[index]->blk,
                   id_str(mall[index]->d[0], ib));
        else
            printf(" %u-%u(%s)", mall[index]->blk, mall[pass]->blk,
                   id_str(mall[index]->d[0], ib));
        index = pass;
    }
    printf("\nWrite %d block%s to %s unit %d? (Yes/No)\n", count,
           (count == 1) ? "" : "s", disk_device, disk_unit);
    switch (getans()) {
        case YES:
        case ALL:
            break;
        default:
            printf("Nothing written\n");
            return (-1);
    }
    for (pass = 0; pass < 2; pass++) {
        for (index = 0; index < nall; index++) {
            if (!mall[index]->dirty || ((index == m_rdb) != (pass == 1)))
                continue;
            m_setsum(mall[index]->d);
            if (blk_write(mall[index]->d, mall[index]->blk)) {
                fprintf(stderr, "Write of block %u failed\n",
                        mall[index]->blk);
                return (1);
            }
            mall[index]->dirty = 0;
        }
    }
    printf("%d block%s written\n", count, (count == 1) ? "" : "s");
    return (0);
}

/* ---------------------------- -edit ---------------------------------- */

static int
read_line(const char *prompt, char *buf, int len)
{
    char *ptr;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, len, stdin) == NULL) {
        buf[0] = '\0';
        printf("\n");
        return (1);
    }
    ptr = buf + strlen(buf);
    while ((ptr > buf) && isspace((UBYTE) ptr[-1]))
        *--ptr = '\0';
    for (ptr = buf; isspace((UBYTE) *ptr); ptr++)
        ;
    if (ptr != buf)
        memmove(buf, ptr, strlen(ptr) + 1);
    if (!IsInteractive(Input()))
        printf("%s\n", buf);  /* Echo input from a file */
    return (0);
}

/* Parse a number: decimal, 0x hex, negative, or 'XXXX' characters */
static int
parse_num(const char *str, ULONG *value)
{
    char *end;

    if (*str == '\0')
        return (1);
    if (*str == '\'') {
        *value = chars_to_long((unsigned char *) str + 1);
        return (*value == 0);
    }
    if (*str == '-')
        *value = (ULONG) strtol(str, &end, 0);
    else
        *value = strtoul(str, &end, 0);
    return ((end == str) || (*end != '\0'));
}

/* Parse a DosType: a number, 'XXXX', or characters such as DOS\3 */
static int
parse_dostype(const char *str, ULONG *value)
{
    if (isdigit((UBYTE) *str) || (*str == '\'') || (*str == '-'))
        return (parse_num(str, value));
    if (strlen(str) < 4)
        return (1);
    *value = chars_to_long((unsigned char *) str);
    return (*value == 0);
}

static int
parse_yesno(const char *str, ULONG *value)
{
    if ((strcasecmp(str, "y") == 0) || (strcasecmp(str, "yes") == 0) ||
        (strcmp(str, "1") == 0)) {
        *value = 1;
        return (0);
    }
    if ((strcasecmp(str, "n") == 0) || (strcasecmp(str, "no") == 0) ||
        (strcmp(str, "0") == 0)) {
        *value = 0;
        return (0);
    }
    return (1);
}

/*
 * Find the free regions of the partitionable area, in RDB cylinders.
 * Returns the number of regions.
 */
static int
m_gaps(ULONG *gs, ULONG *ge, int max)
{
    ULONG cb = MR->rdb_CylBlocks;
    ULONG f[MAX_PARTS];
    ULONG l[MAX_PARTS];
    ULONG first;
    ULONG last;
    ULONG cur;
    ULONG end;
    ULONG cs;
    ULONG ce;
    int   n = 0;
    int   ng = 0;
    int   index;
    int   pos;

    if ((cb == 0) || (MR->rdb_HiCylinder < MR->rdb_LoCylinder))
        return (0);

    /* Partition ranges, sorted by first block */
    for (index = 0; index < m_nparts; index++) {
        if (m_part_range(index, &first, &last))
            continue;
        for (pos = n; (pos > 0) && (f[pos - 1] > first); pos--) {
            f[pos] = f[pos - 1];
            l[pos] = l[pos - 1];
        }
        f[pos] = first;
        l[pos] = last;
        n++;
    }

    cur = MR->rdb_LoCylinder * cb;
    if (cur <= MR->rdb_RDBBlocksHi)
        cur = MR->rdb_RDBBlocksHi + 1;
    end = (MR->rdb_HiCylinder + 1) * cb - 1;
    if ((m_disk_blocks != 0) && (end >= m_disk_blocks))
        end = m_disk_blocks - 1;
    for (index = 0; index <= n; index++) {
        last = (index < n) ? f[index] : end + 1;  /* Next used block */
        if (last > end + 1)
            last = end + 1;
        if (last > cur) {
            cs = (cur + cb - 1) / cb;          /* Whole cylinders only */
            ce = last / cb;
            if ((ce > cs) && (ng < max)) {
                gs[ng] = cs;
                ge[ng] = ce - 1;
                ng++;
            }
        }
        if ((index < n) && (l[index] + 1 > cur))
            cur = l[index] + 1;
    }
    return (ng);
}

static void
m_print_table(void)
{
    struct RigidDiskBlock *r = MR;
    struct PartitionBlock *p;
    struct DosEnvec       *e;
    ULONG  gs[MAX_PARTS + 1];
    ULONG  ge[MAX_PARTS + 1];
    ULONG  first;
    ULONG  last;
    ULONG  bytes;
    char   sz[20];
    char   name[40];
    char   ib[20];
    int    index;
    int    seg;
    int    ng;

    printf("\nDisk %s unit %d", disk_device, disk_unit);
    if (m_disk_blocks != 0)
        printf(": %s, %u blocks", size_str(m_disk_blocks, sz),
               m_disk_blocks);
    printf("\nRDB at block %d: %u cylinders, %u heads, %u sectors "
           "(%u blocks/cylinder)\n", rdb_blk, r->rdb_Cylinders,
           r->rdb_Heads, r->rdb_Sectors, r->rdb_CylBlocks);
    printf("RDB area: blocks %u-%u; partitions may use cylinders %u-%u\n\n",
           r->rdb_RDBBlocksLo, r->rdb_RDBBlocksHi, r->rdb_LoCylinder,
           r->rdb_HiCylinder);

    if (m_nparts == 0) {
        printf("No partitions\n");
    } else {
        printf(" #  Name          First   Last     Size Boot  Pri Mount "
               "DosType  Buffers\n");
        for (index = 0; index < m_nparts; index++) {
            p = MP(index);
            e = MPE(index);
            if (m_part_range(index, &first, &last))
                strcpy(sz, "?");
            else
                size_str(last - first + 1, sz);
            printf("%2d  %-12.12s %6u %6u %8s  %s  %4d  %-3s  %-8s %6u\n",
                   index + 1, part_name_str(p, name), e->de_LowCyl,
                   e->de_HighCyl, sz,
                   (p->pb_Flags & PBFF_BOOTABLE) ? "*" : " ",
                   (int) e->de_BootPri,
                   (p->pb_Flags & PBFF_NOMOUNT) ? "no" : "yes",
                   id_str(e->de_DosType, ib), e->de_NumBuffers);
        }
    }

    if (m_nfs != 0) {
        printf("\nFilesystems in the RDB:\n");
        for (index = 0; index < m_nfs; index++) {
            bytes = 0;
            for (seg = 0; seg < m_seg_count[index]; seg++)
                bytes += (mall[m_seg_first[index] + seg]->d[1] - 5) * 4;
            printf("    %-8s version %u.%u, %u bytes\n",
                   id_str(MF(index)->fhb_DosType, ib),
                   MF(index)->fhb_Version >> 16,
                   MF(index)->fhb_Version & 0xffff, bytes);
        }
    }

    ng = m_gaps(gs, ge, MAX_PARTS + 1);
    if (ng == 0) {
        printf("\nNo free cylinders\n");
    } else {
        printf("\nFree cylinders:");
        for (index = 0; index < ng; index++)
            printf(" %u-%u", gs[index], ge[index]);
        printf("\n");
    }
}

/* Choose a partition; returns its index or -1 */
static int
m_select_part(void)
{
    char  line[40];
    char  pr[60];
    ULONG num;

    if (m_nparts == 0) {
        printf("There are no partitions\n");
        return (-1);
    }
    if (m_nparts == 1)
        sprintf(pr, "Partition number [1]: ");
    else
        sprintf(pr, "Partition number (1-%d): ", m_nparts);
    if (read_line(pr, line, sizeof (line)))
        return (-1);
    if ((line[0] == '\0') && (m_nparts == 1))
        return (0);
    if (parse_num(line, &num) || (num < 1) || (num > m_nparts)) {
        printf("Invalid partition number\n");
        return (-1);
    }
    return (num - 1);
}

/* Allocate a new block in the RDB area; returns 1 if none is free */
static int
m_alloc_block(ULONG *blk)
{
    ULONG b = m_highest;
    ULONG hi = RDB_HIGHRDSKBLOCK(MR);

    if ((hi > b) && (hi <= MR->rdb_RDBBlocksHi))
        b = hi;
    if (b < (ULONG) rdb_blk)
        b = rdb_blk;
    b++;
    if (b > MR->rdb_RDBBlocksHi) {
        printf("No free block in the RDB area (RDBBlocksHi=%u)\n",
               MR->rdb_RDBBlocksHi);
        return (1);
    }
    *blk = b;
    m_highest = b;
    return (0);
}

/*
 * Check partition index against the partitionable area and the other
 * partitions.  Prints the reason and returns 1 if it is not valid.
 */
static int
m_check_range(int index)
{
    struct RigidDiskBlock *r = MR;
    ULONG first;
    ULONG last;
    ULONG ofirst;
    ULONG olast;
    ULONG lo = r->rdb_LoCylinder * r->rdb_CylBlocks;
    ULONG hi = (r->rdb_HiCylinder + 1) * r->rdb_CylBlocks - 1;
    char  name[40];
    int   other;

    if (MPE(index)->de_SizeBlock != TD_SECTOR / 4)
        return (0);  /* Can not check */
    if (part_range(MPE(index), &first, &last)) {
        printf("Invalid geometry or cylinder range\n");
        return (1);
    }
    if (lo <= r->rdb_RDBBlocksHi)
        lo = r->rdb_RDBBlocksHi + 1;
    if ((m_disk_blocks != 0) && (hi >= m_disk_blocks))
        hi = m_disk_blocks - 1;
    if ((first < lo) || (last > hi)) {
        printf("Blocks %u-%u are outside the partitionable area (blocks "
               "%u-%u)\n", first, last, lo, hi);
        return (1);
    }
    for (other = 0; other < m_nparts; other++) {
        if ((other == index) || m_part_range(other, &ofirst, &olast))
            continue;
        if ((first <= olast) && (ofirst <= last)) {
            printf("Blocks %u-%u overlap partition %s\n", first, last,
                   part_name_str(MP(other), name));
            return (1);
        }
    }
    return (0);
}

static void
m_new_part(void)
{
    struct RigidDiskBlock *r = MR;
    struct PartitionBlock *p;
    struct DosEnvec       *e;
    ULONG  gs[MAX_PARTS + 1];
    ULONG  ge[MAX_PARTS + 1];
    ULONG  start;
    ULONG  end;
    ULONG  dostype;
    ULONG  blk;
    ULONG  num;
    ULONG  mult;
    char   line[80];
    char   pr[100];
    char   name[40];
    char   ib[20];
    char  *ptr;
    int    ng;
    int    gap;
    int    idx;

    if (m_nparts >= MAX_PARTS) {
        printf("Too many partitions\n");
        return;
    }
    if ((r->rdb_CylBlocks == 0) ||
        (r->rdb_Heads * r->rdb_Sectors != r->rdb_CylBlocks)) {
        printf("The RDB geometry is not valid; correct it first (r)\n");
        return;
    }
    ng = m_gaps(gs, ge, MAX_PARTS + 1);
    if (ng == 0) {
        printf("No free cylinders\n");
        return;
    }

    for (num = 0; num < 1000; num++) {
        sprintf(name, "DH%u", num);
        if (!m_name_used(name, -1))
            break;
    }
    sprintf(pr, "Name [%s]: ", name);
    if (read_line(pr, line, sizeof (line)))
        return;
    if (line[0] != '\0') {
        if (m_bad_name(line, -1))
            return;
        strcpy(name, line);
    }

    sprintf(pr, "First cylinder (%u-%u) [%u]: ", gs[0], ge[ng - 1], gs[0]);
    if (read_line(pr, line, sizeof (line)))
        return;
    start = gs[0];
    if ((line[0] != '\0') && parse_num(line, &start)) {
        printf("Invalid number\n");
        return;
    }
    for (gap = 0; gap < ng; gap++)
        if ((start >= gs[gap]) && (start <= ge[gap]))
            break;
    if (gap == ng) {
        printf("Cylinder %u is not free\n", start);
        return;
    }

    sprintf(pr, "Last cylinder or +size{K,M,G} (%u-%u) [%u]: ",
            start, ge[gap], ge[gap]);
    if (read_line(pr, line, sizeof (line)))
        return;
    end = ge[gap];
    if (line[0] == '+') {
        num = strtoul(line + 1, &ptr, 10);
        mult = 0;
        switch (toupper((UBYTE) *ptr)) {
            case 'K': mult = 2;       break;
            case 'M': mult = 2048;    break;
            case 'G': mult = 2097152; break;
        }
        if ((ptr == line + 1) || (mult == 0) || (num == 0) ||
            (ptr[1] != '\0')) {
            printf("Invalid size; use for example +100M\n");
            return;
        }
        num = (num * mult + r->rdb_CylBlocks - 1) / r->rdb_CylBlocks;
        end = start + num - 1;
    } else if ((line[0] != '\0') && parse_num(line, &end)) {
        printf("Invalid number\n");
        return;
    }
    if ((end < start) || (end > ge[gap])) {
        printf("Last cylinder must be %u-%u\n", start, ge[gap]);
        return;
    }

    dostype = (m_nfs != 0) ? MF(0)->fhb_DosType : 0x444f5303;
    sprintf(pr, "DosType [%s]: ", id_str(dostype, ib));
    if (read_line(pr, line, sizeof (line)))
        return;
    if ((line[0] != '\0') && parse_dostype(line, &dostype)) {
        printf("Invalid DosType; use for example DOS\\3 or 0x444f5303\n");
        return;
    }

    if (m_alloc_block(&blk))
        return;
    idx = mblk_add(blk, OWN_PART);
    if (idx < 0) {
        printf("Out of memory\n");
        return;
    }
    p = (struct PartitionBlock *) mall[idx]->d;
    e = (struct DosEnvec *) p->pb_Environment;
    p->pb_ID          = IDNAME_PARTITION;
    p->pb_SummedLongs = sizeof (struct PartitionBlock) / 4;
    p->pb_HostID      = r->rdb_HostID;
    p->pb_Next        = 0xffffffff;
    p->pb_Flags       = 0;
    p->pb_DevFlags    = 0;
    set_part_name(p, name, 1);
    e->de_TableSize      = DE_DOSTYPE;
    e->de_SizeBlock      = TD_SECTOR / 4;
    e->de_Surfaces       = r->rdb_Heads;
    e->de_SectorPerBlock = 1;
    e->de_BlocksPerTrack = r->rdb_Sectors;
    e->de_Reserved       = 2;
    e->de_LowCyl         = start;
    e->de_HighCyl        = end;
    e->de_NumBuffers     = 30;
    e->de_BufMemType     = 1;
    e->de_MaxTransfer    = 0x0001fe00;
    e->de_Mask           = 0x7ffffffe;
    e->de_BootPri        = 0;
    e->de_DosType        = dostype;
    mall[idx]->dirty = 1;
    if ((m_owner != NULL) && (blk < MAP_MAX_BLKS))
        m_owner[blk] = OWN_PART;

    /* Link it at the end of the partition list */
    if (m_nparts == 0) {
        r->rdb_PartitionList = blk;
    } else {
        MP(m_nparts - 1)->pb_Next = blk;
        mall[m_part[m_nparts - 1]]->dirty = 1;
    }
    m_part[m_nparts++] = idx;
    if (RDB_HIGHRDSKBLOCK(r) < blk)
        RDB_HIGHRDSKBLOCK(r) = blk;
    mall[m_rdb]->dirty = 1;
    printf("Partition %d (%s) created: cylinders %u-%u, %s\n", m_nparts,
           name, start, end,
           size_str((end - start + 1) * r->rdb_CylBlocks, ib));
}

static void
m_delete_part(void)
{
    char  name[40];
    ULONG next;
    int   index = m_select_part();
    int   pos;

    if (index < 0)
        return;
    next = MP(index)->pb_Next;
    if (index == 0) {
        MR->rdb_PartitionList = next;
        mall[m_rdb]->dirty = 1;
    } else {
        MP(index - 1)->pb_Next = next;
        mall[m_part[index - 1]]->dirty = 1;
    }
    mall[m_part[index]]->dirty = 0;  /* Unlinked; not written */
    printf("Partition %d (%s) deleted\n", index + 1,
           part_name_str(MP(index), name));
    for (pos = index; pos < m_nparts - 1; pos++)
        m_part[pos] = m_part[pos + 1];
    m_nparts--;
}

static const char *pfield[] = {
    "Name", "DosType", "Buffers", "Bootable", "Boot priority",
    "Automount", "First cylinder", "Last cylinder", "MaxTransfer", "Mask",
    "BufMemType", "Reserved blocks", "PreAlloc blocks", "Surfaces",
    "BlocksPerTrack", "DevFlags"
};
#define NPFIELDS 16

/* Address of a numeric partition field, or NULL */
static ULONG *
m_pfield_ptr(int index, int field)
{
    struct DosEnvec *e = MPE(index);

    switch (field) {
        case 2:  return (&e->de_NumBuffers);
        case 6:  return (&e->de_LowCyl);
        case 7:  return (&e->de_HighCyl);
        case 8:  return (&e->de_MaxTransfer);
        case 9:  return (&e->de_Mask);
        case 10: return (&e->de_BufMemType);
        case 11: return (&e->de_Reserved);
        case 12: return (&e->de_PreAlloc);
        case 13: return (&e->de_Surfaces);
        case 14: return (&e->de_BlocksPerTrack);
        case 15: return (&MP(index)->pb_DevFlags);
    }
    return (NULL);
}

static char *
m_pfield_str(int index, int field, char *buf)
{
    struct PartitionBlock *p = MP(index);
    struct DosEnvec       *e = MPE(index);
    char   ib[20];

    switch (field) {
        case 0:
            part_name_str(p, buf);
            break;
        case 1:
            sprintf(buf, "%s (0x%08X)", id_str(e->de_DosType, ib),
                    e->de_DosType);
            break;
        case 3:
            strcpy(buf, (p->pb_Flags & PBFF_BOOTABLE) ? "yes" : "no");
            break;
        case 4:
            sprintf(buf, "%d", (int) e->de_BootPri);
            break;
        case 5:
            strcpy(buf, (p->pb_Flags & PBFF_NOMOUNT) ? "no" : "yes");
            break;
        case 8:
        case 9:
            sprintf(buf, "0x%08X", *m_pfield_ptr(index, field));
            break;
        default:
            sprintf(buf, "%u", *m_pfield_ptr(index, field));
            break;
    }
    return (buf);
}

/* Set a partition field from text; prints the reason and returns 1 if bad */
static int
m_pfield_set(int index, int field, const char *val)
{
    struct PartitionBlock *p = MP(index);
    struct DosEnvec       *e = MPE(index);
    ULONG *ptr;
    ULONG  value;
    ULONG  old;

    switch (field) {
        case 0:
            if (m_bad_name(val, m_part[index]))
                return (1);
            set_part_name(p, val, (p->pb_DriveName[0] < 32));
            break;
        case 1:
            if (parse_dostype(val, &value)) {
                printf("Invalid DosType; use for example DOS\\3 or "
                       "0x444f5303\n");
                return (1);
            }
            e->de_DosType = value;
            break;
        case 3:
        case 5:
            if (parse_yesno(val, &value)) {
                printf("Answer yes or no\n");
                return (1);
            }
            if (field == 5)
                value = !value;  /* Automount is the NoMount flag */
            old = (field == 3) ? PBFF_BOOTABLE : PBFF_NOMOUNT;
            if (value)
                p->pb_Flags |= old;
            else
                p->pb_Flags &= ~old;
            break;
        case 4:
            if (parse_num(val, &value) || ((LONG) value < -128) ||
                ((LONG) value > 127)) {
                printf("Boot priority must be -128 to 127\n");
                return (1);
            }
            e->de_BootPri = (LONG) value;
            break;
        default:
            ptr = m_pfield_ptr(index, field);
            if (parse_num(val, &value)) {
                printf("Invalid number\n");
                return (1);
            }
            old = *ptr;
            *ptr = value;
            if (((field == 6) || (field == 7) || (field == 13) ||
                 (field == 14)) && m_check_range(index)) {
                *ptr = old;
                return (1);
            }
            if ((field == 2) && (value == 0))
                printf("Warning: 0 buffers\n");
            break;
    }
    mall[m_part[index]]->dirty = 1;
    return (0);
}

static void
m_edit_part(void)
{
    char  line[80];
    char  pr[100];
    char  cur[60];
    ULONG field;
    int   index = m_select_part();
    int   pos;

    int   list = 1;

    if (index < 0)
        return;
    for (;;) {
        if (list) {
            printf("\nPartition %d:\n", index + 1);
            for (pos = 0; pos < NPFIELDS; pos++)
                printf("%4d  %-16s %s\n", pos + 1, pfield[pos],
                       m_pfield_str(index, pos, cur));
            list = 0;
        }
        sprintf(pr, "Field to change (1-%d, l to list, Enter when done): ",
                NPFIELDS);
        if (read_line(pr, line, sizeof (line)) || (line[0] == '\0'))
            return;
        if ((tolower((UBYTE) line[0]) == 'l') || (line[0] == '?')) {
            list = 1;
            continue;
        }
        if (parse_num(line, &field) || (field < 1) || (field > NPFIELDS)) {
            printf("Invalid field number\n");
            continue;
        }
        field--;
        sprintf(pr, "%s [%s]: ", pfield[field],
                m_pfield_str(index, field, cur));
        if (read_line(pr, line, sizeof (line)))
            return;
        if ((line[0] != '\0') && (m_pfield_set(index, field, line) == 0))
            printf("%s is now %s\n", pfield[field],
                   m_pfield_str(index, field, cur));
    }
}

/* RDB field as text; list pointers of 0xffffffff are shown as -1 */
static char *
m_rfield_str(int which, char *buf)
{
    ULONG *ptr = &MRL[accept[which].offset];

    if (accept[which].type == STR)
        sprintf(buf, "%.*s", accept[which].size, (char *) ptr);
    else if (*ptr == 0xffffffff)
        strcpy(buf, "-1");
    else
        sprintf(buf, "%u", *ptr);
    return (buf);
}

static void
m_edit_rdb(void)
{
    char  line[80];
    char  pr[100];
    char  cur[40];
    int   map[NUM_ACCEPT];
    int   count = 0;
    int   index;
    int   which;
    int   list = 1;
    ULONG num;
    ULONG *ptr;

    for (index = 0; index < NUM_ACCEPT; index++)
        if (accept[index].which == RIGID)
            map[count++] = index;
    for (;;) {
        if (list) {
            printf("\nRDB at block %d:\n", rdb_blk);
            for (index = 0; index < count; index++) {
                which = map[index];
                printf("%4d  %-18s %s\n", index + 1, accept[which].name,
                       m_rfield_str(which, cur));
            }
            list = 0;
        }
        sprintf(pr, "Field to change (1-%d, l to list, Enter when done): ",
                count);
        if (read_line(pr, line, sizeof (line)) || (line[0] == '\0'))
            return;
        if ((tolower((UBYTE) line[0]) == 'l') || (line[0] == '?')) {
            list = 1;
            continue;
        }
        if (parse_num(line, &num) || (num < 1) || (num > count)) {
            printf("Invalid field number\n");
            continue;
        }
        which = map[num - 1];
        ptr = &MRL[accept[which].offset];
        sprintf(pr, "%s: ", accept[which].name);
        if (read_line(pr, line, sizeof (line)))
            return;
        if (line[0] == '\0')
            continue;
        if (accept[which].type == STR) {
            if (strlen(line) > accept[which].size) {
                printf("%s may be at most %d characters\n",
                       accept[which].name, accept[which].size);
                continue;
            }
            memset(ptr, 0, accept[which].size);
            memcpy(ptr, line, strlen(line));
        } else {
            if (parse_num(line, &num)) {
                printf("Invalid number\n");
                continue;
            }
            *ptr = num;
        }
        mall[m_rdb]->dirty = 1;
        printf("%s is now %s\n", accept[which].name, m_rfield_str(which, cur));
        printf("Note: partitions are not moved or resized; use c to check "
               "the result\n");
    }
}

/* Create a new RDB in memory (g command) */
static void
m_create_rdb(void)
{
    int idx;

    if (m_rdb >= 0) {
        printf("An RDB already exists at block %d\n", rdb_blk);
        return;
    }
    m_free();
    m_owner = (UBYTE *) calloc(MAP_MAX_BLKS, 1);
    idx = (m_owner == NULL) ? -1 : mblk_add(0, OWN_RDSK);
    if (idx < 0) {
        printf("Out of memory\n");
        m_free();
        return;
    }
    if (build_rdb((struct RigidDiskBlock *) mall[idx]->d)) {
        m_free();
        return;
    }
    m_rdb = idx;
    rdb_blk = 0;
    m_highest = 0;
    m_owner[0] = OWN_RDSK;
    mall[idx]->dirty = 1;
    nprob = 0;
    nprob_load = 0;
    if (m_nonempty)
        printf("WARNING: %d of the first %d blocks are not empty; the disk "
               "may contain data\n", m_nonempty, RDB_LOCATION_LIMIT);
    printf("New RDB created; it is written to disk by w\n");
}

/* Total bytes of code in filesystem index */
static ULONG
m_fs_bytes(int index)
{
    ULONG bytes = 0;
    int   seg;

    for (seg = 0; seg < m_seg_count[index]; seg++)
        bytes += (mall[m_seg_first[index] + seg]->d[1] - 5) * 4;
    return (bytes);
}

static void
m_list_fs(void)
{
    struct FileSysHeaderBlock *f;
    char  ib[20];
    char  ver[20];
    char  stack[16];
    char  users[40];
    char  name[40];
    int   index;
    int   part;

    if (m_nfs == 0) {
        printf("There are no filesystems in the RDB\n");
        return;
    }
    printf(" #  DosType   Version     Bytes  Header  Blocks  Stack   "
           "Used by\n");
    for (index = 0; index < m_nfs; index++) {
        f = MF(index);
        sprintf(ver, "%u.%u", f->fhb_Version >> 16, f->fhb_Version & 0xffff);
        if (f->fhb_PatchFlags & 0x10)
            sprintf(stack, "%u", f->fhb_StackSize);
        else
            strcpy(stack, "-");
        users[0] = '\0';
        for (part = 0; part < m_nparts; part++) {
            if (MPE(part)->de_DosType != f->fhb_DosType)
                continue;
            part_name_str(MP(part), name);
            if (strlen(users) + strlen(name) + 2 >= sizeof (users) - 4) {
                strcat(users, ", ...");
                break;
            }
            if (users[0] != '\0')
                strcat(users, ", ");
            strcat(users, name);
        }
        printf("%2d  %-9s %-8s %8u  %6u  %6d  %-6s  %s\n", index + 1,
               id_str(f->fhb_DosType, ib), ver, m_fs_bytes(index),
               mall[m_fs[index]]->blk, m_seg_count[index], stack,
               (users[0] != '\0') ? users : "-");
    }
}

/* Find "$VER: name major.minor" in a file; returns 0 if found */
static int
find_version(UBYTE *buf, ULONG len, ULONG *version)
{
    ULONG pos;
    ULONG major;
    ULONG minor;
    char *end;

    for (pos = 0; pos + 6 < len; pos++) {
        if (memcmp(buf + pos, "$VER: ", 6) != 0)
            continue;
        pos += 6;
        while ((pos < len) && (buf[pos] > ' '))     /* name */
            pos++;
        while ((pos < len) && (buf[pos] == ' '))
            pos++;
        if ((pos >= len) || !isdigit(buf[pos]))
            return (1);
        major = strtoul((char *) buf + pos, &end, 10);
        minor = (*end == '.') ? strtoul(end + 1, NULL, 10) : 0;
        *version = (major << 16) | (minor & 0xffff);
        return (0);
    }
    return (1);
}

/* Parse a version "major.minor" or "major"; returns 0 if valid */
static int
parse_version(const char *str, ULONG *version)
{
    char *end;
    ULONG major;
    ULONG minor = 0;

    if (!isdigit((UBYTE) *str))
        return (1);
    major = strtoul(str, &end, 10);
    if (*end == '.') {
        str = end + 1;
        if (!isdigit((UBYTE) *str))
            return (1);
        minor = strtoul(str, &end, 10);
    }
    if ((*end != '\0') || (major > 0xffff) || (minor > 0xffff))
        return (1);
    *version = (major << 16) | minor;
    return (0);
}

#define LSEG_DATA_LONGS (TD_SECTOR / 4 - 5)

static void
m_add_fs(void)
{
    struct FileSysHeaderBlock *f;
    struct LoadSegBlock       *ls;
    FILE  *fp;
    UBYTE *code = NULL;
    char   line[160];
    char   pr[120];
    char   ib[20];
    long   size;
    ULONG  dostype = 0;
    ULONG  version = 0;
    ULONG  stack = 0;
    ULONG  nlongs;
    ULONG  nblocks;
    ULONG  next;
    ULONG  blk;
    ULONG  fsblk;
    ULONG  first = 0;
    ULONG  count;
    ULONG  hi = RDB_HIGHRDSKBLOCK(MR);
    int    idx;
    int    prev = -1;
    int    seg_first;
    int    index;
    int    part;

    if (m_nfs >= MAX_FS) {
        printf("Too many filesystems\n");
        return;
    }
    if (read_line("Filesystem file (for example L:FastFileSystem): ", line,
                  sizeof (line)) || (line[0] == '\0'))
        return;
    fp = fopen(line, "r");
    if (fp == NULL) {
        printf("Can not open %s\n", line);
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if ((size < 8) || (size > 4 * 1024 * 1024)) {
        printf("%s is not a filesystem (size %ld)\n", line, size);
        fclose(fp);
        return;
    }
    code = (UBYTE *) calloc(1, size + 4);
    if (code == NULL) {
        printf("Out of memory\n");
        fclose(fp);
        return;
    }
    if (fread(code, 1, size, fp) != size) {
        printf("Read of %s failed\n", line);
        fclose(fp);
        free(code);
        return;
    }
    fclose(fp);
    if (*(ULONG *) code != 0x000003f3) {  /* HUNK_HEADER */
        printf("%s is not an AmigaOS executable\n", line);
        free(code);
        return;
    }

    /* DosType: default to one used by a partition but not in the RDB */
    for (part = 0; (part < m_nparts) && (dostype == 0); part++) {
        dostype = MPE(part)->de_DosType;
        for (index = 0; index < m_nfs; index++)
            if (MF(index)->fhb_DosType == dostype)
                dostype = 0;
        if ((dostype & 0xffffff00) == 0x444f5300)
            dostype = 0;        /* Probably in ROM */
    }
    if (dostype != 0)
        sprintf(pr, "DosType [%s]: ", id_str(dostype, ib));
    else
        sprintf(pr, "DosType (for example DOS\\3 or 0x50465303): ");
    if (read_line(pr, line, sizeof (line)))
        goto done;
    if (((line[0] != '\0') || (dostype == 0)) &&
        parse_dostype(line, &dostype)) {
        printf("Invalid DosType\n");
        goto done;
    }

    (void) find_version(code, size, &version);
    sprintf(pr, "Version [%u.%u]: ", version >> 16, version & 0xffff);
    if (read_line(pr, line, sizeof (line)))
        goto done;
    if ((line[0] != '\0') && parse_version(line, &version)) {
        printf("Invalid version; use for example 47.4\n");
        goto done;
    }
    for (index = 0; index < m_nfs; index++)
        if ((MF(index)->fhb_DosType == dostype) &&
            (MF(index)->fhb_Version == version)) {
            printf("Filesystem %d already has DosType %s version %u.%u\n",
                   index + 1, id_str(dostype, ib), version >> 16,
                   version & 0xffff);
            goto done;
        }

    if (read_line("Stack size [default]: ", line, sizeof (line)))
        goto done;
    if ((line[0] != '\0') && parse_num(line, &stack)) {
        printf("Invalid number\n");
        goto done;
    }

    /* Room in the RDB area for the header and the code blocks? */
    nlongs  = (size + 3) / 4;
    nblocks = (nlongs + LSEG_DATA_LONGS - 1) / LSEG_DATA_LONGS;
    next = m_highest;
    if ((hi > next) && (hi <= MR->rdb_RDBBlocksHi))
        next = hi;
    if (next < (ULONG) rdb_blk)
        next = rdb_blk;
    if (next + 1 + nblocks > MR->rdb_RDBBlocksHi) {
        printf("The filesystem needs %u blocks, but only %u are free in "
               "the RDB area\n", nblocks + 1, MR->rdb_RDBBlocksHi - next);
        goto done;
    }

    /* Header block */
    if (m_alloc_block(&fsblk) || ((idx = mblk_add(fsblk, OWN_FSHD)) < 0))
        goto done;
    f = (struct FileSysHeaderBlock *) mall[idx]->d;
    f->fhb_ID          = IDNAME_FILESYSHEADER;
    f->fhb_SummedLongs = sizeof (struct FileSysHeaderBlock) / 4;
    f->fhb_HostID      = MR->rdb_HostID;
    f->fhb_Next        = 0xffffffff;
    f->fhb_DosType     = dostype;
    f->fhb_Version     = version;
    f->fhb_PatchFlags  = 0x180;         /* SegListBlocks and GlobalVec */
    f->fhb_GlobalVec   = -1;            /* No BCPL global vector */
    if (stack != 0) {
        f->fhb_PatchFlags |= 0x10;
        f->fhb_StackSize = stack;
    }
    mall[idx]->dirty = 1;
    if (fsblk < MAP_MAX_BLKS)
        m_owner[fsblk] = OWN_FSHD;

    /* Code blocks */
    seg_first = nall;
    for (count = 0; count < nblocks; count++) {
        if (m_alloc_block(&blk) || ((index = mblk_add(blk, OWN_LSEG)) < 0)) {
            printf("Out of memory\n");
            /* Nothing is linked yet; don't write the blocks made so far */
            mall[idx]->dirty = 0;
            for (index = seg_first; index < nall; index++)
                mall[index]->dirty = 0;
            goto done;
        }
        ls = (struct LoadSegBlock *) mall[index]->d;
        ls->lsb_ID   = IDNAME_LOADSEG;
        ls->lsb_SummedLongs = 5 + ((nlongs - count * LSEG_DATA_LONGS >
                                    LSEG_DATA_LONGS) ? LSEG_DATA_LONGS :
                                   nlongs - count * LSEG_DATA_LONGS);
        ls->lsb_HostID = MR->rdb_HostID;
        ls->lsb_Next = 0xffffffff;
        memcpy(ls->lsb_LoadData, code + count * LSEG_DATA_LONGS * 4,
               (ls->lsb_SummedLongs - 5) * 4);
        mall[index]->dirty = 1;
        if (blk < MAP_MAX_BLKS)
            m_owner[blk] = OWN_LSEG;
        if (prev < 0)
            first = blk;
        else
            mall[prev]->d[4] = blk;
        prev = index;
    }
    f->fhb_SegListBlocks = first;

    /* Link the header at the end of the filesystem list */
    if (m_nfs == 0) {
        MR->rdb_FileSysHeaderList = fsblk;
    } else {
        MF(m_nfs - 1)->fhb_Next = fsblk;
        mall[m_fs[m_nfs - 1]]->dirty = 1;
    }
    m_fs[m_nfs] = idx;
    m_seg_first[m_nfs] = seg_first;
    m_seg_count[m_nfs] = nblocks;
    m_nfs++;
    if (RDB_HIGHRDSKBLOCK(MR) < m_highest)
        RDB_HIGHRDSKBLOCK(MR) = m_highest;
    mall[m_rdb]->dirty = 1;
    printf("Filesystem %d added: %s version %u.%u, %ld bytes in blocks "
           "%u-%u\n", m_nfs, id_str(dostype, ib), version >> 16,
           version & 0xffff, size, fsblk, m_highest);
done:
    free(code);
}

static void
m_del_fs(void)
{
    struct FileSysHeaderBlock *f;
    char  line[40];
    char  pr[60];
    char  ib[20];
    char  name[40];
    ULONG num;
    int   index;
    int   other;
    int   part;
    int   users = 0;

    if (m_nfs == 0) {
        printf("There are no filesystems in the RDB\n");
        return;
    }
    m_list_fs();
    if (m_nfs == 1)
        sprintf(pr, "Filesystem number [1]: ");
    else
        sprintf(pr, "Filesystem number (1-%d): ", m_nfs);
    if (read_line(pr, line, sizeof (line)))
        return;
    if ((line[0] == '\0') && (m_nfs == 1))
        num = 1;
    else if (parse_num(line, &num) || (num < 1) || (num > m_nfs)) {
        printf("Invalid filesystem number\n");
        return;
    }
    index = num - 1;
    f = MF(index);

    /* Warn if partitions need it and no other copy is in the RDB */
    for (other = 0; other < m_nfs; other++)
        if ((other != index) && (MF(other)->fhb_DosType == f->fhb_DosType))
            break;
    if (other == m_nfs) {
        for (part = 0; part < m_nparts; part++)
            if (MPE(part)->de_DosType == f->fhb_DosType) {
                printf("%s %s", (users++ == 0) ? "Used by partition" : ",",
                       part_name_str(MP(part), name));
            }
        if (users != 0) {
            printf("\nRemove filesystem %s anyway? (Yes/No)\n",
                   id_str(f->fhb_DosType, ib));
            if (getans() != YES)
                return;
        }
    }

    if (index == 0) {
        MR->rdb_FileSysHeaderList = f->fhb_Next;
        mall[m_rdb]->dirty = 1;
    } else {
        MF(index - 1)->fhb_Next = f->fhb_Next;
        mall[m_fs[index - 1]]->dirty = 1;
    }
    /* Unlinked blocks are not written */
    mall[m_fs[index]]->dirty = 0;
    for (other = 0; other < m_seg_count[index]; other++)
        mall[m_seg_first[index] + other]->dirty = 0;
    printf("Filesystem %d (%s version %u.%u) removed\n", index + 1,
           id_str(f->fhb_DosType, ib), f->fhb_Version >> 16,
           f->fhb_Version & 0xffff);
    for (other = index; other < m_nfs - 1; other++) {
        m_fs[other] = m_fs[other + 1];
        m_seg_first[other] = m_seg_first[other + 1];
        m_seg_count[other] = m_seg_count[other + 1];
    }
    m_nfs--;
}

static void
m_help(void)
{
    printf("\nCommands:\n");
    if (m_rdb < 0) {
        printf("   g   create a new RDB\n");
    } else {
        printf("   p   print the partition table\n"
               "   n   add a new partition\n"
               "   d   delete a partition\n"
               "   e   edit a partition's fields\n"
               "   r   edit RDB fields\n"
               "   l   list filesystems\n"
               "   a   add a filesystem from a file\n"
               "   x   remove a filesystem\n"
               "   c   check for problems\n"
               "   f   fix problems (in memory; w writes them)\n"
               "   w   write changes to disk and quit\n");
    }
    printf("   q   quit without saving changes\n"
           "   m   show this menu\n");
}

static int
m_edit(void)
{
    char line[80];
    int  errors;
    int  warnings;
    int  rc;

    printf("Changes are kept in memory until you write them with w.\n");
    m_help();
    for (;;) {
        if (read_line("\nCommand (m for help): ", line, sizeof (line))) {
            if ((m_rdb >= 0) && (m_dirty_count() != 0))
                printf("End of input: changes were not written\n");
            return (0);
        }
        if (line[0] == '\0')
            continue;
        if ((strchr("pndercfwlax", tolower((UBYTE) line[0])) != NULL) &&
            (m_rdb < 0)) {
            printf("There is no RDB; use g to create one\n");
            continue;
        }
        switch (tolower((UBYTE) line[0])) {
            case 'm':
                m_help();
                break;
            case 'p':
                m_print_table();
                break;
            case 'n':
                m_new_part();
                break;
            case 'd':
                m_delete_part();
                break;
            case 'e':
                m_edit_part();
                break;
            case 'r':
                m_edit_rdb();
                break;
            case 'g':
                m_create_rdb();
                if (m_rdb >= 0)
                    m_help();
                break;
            case 'l':
                m_list_fs();
                break;
            case 'a':
                m_add_fs();
                break;
            case 'x':
                m_del_fs();
                break;
            case 'c':
                nprob = nprob_load;
                m_check();
                m_report(&errors, &warnings);
                break;
            case 'f':
                nprob = nprob_load;
                m_check();
                if (m_apply_fixes() == 0)
                    printf("Nothing to fix\n");
                else
                    printf("Use w to write the fixes to disk\n");
                break;
            case 'w':
                rc = m_write();
                if (rc == 0)
                    return (0);
                if (rc > 0)
                    return (1);
                break;
            case 'q':
                if (m_dirty_count() != 0) {
                    printf("Quit without writing your changes? (Yes/No)\n");
                    if (getans() != YES)
                        break;
                }
                return (0);
            default:
                printf("Unknown command %s; m shows the commands\n", line);
                break;
        }
    }
}

/* Entry point for -check, -fix, and -edit */
static int
model_main(int mode, int found, int nonempty)
{
    struct DriveGeometry dg;
    int    errors;
    int    warnings;
    int    rc;

    m_nonempty = nonempty;
    if ((get_geometry(&dg) == 0) && (dg.dg_SectorSize == TD_SECTOR))
        m_disk_blocks = dg.dg_TotalSectors;

    if (found < 0) {
        if (mode != MODE_EDIT) {
            fprintf(stderr, "No RDB label found on %s unit %d\n",
                    disk_device, disk_unit);
            return (1);
        }
        printf("No RDB found on %s unit %d\n", disk_device, disk_unit);
        m_rdb = -1;
        return (m_edit());
    }

    rdb_blk = found;
    if ((rdb->rdb_BlockBytes != TD_SECTOR) && (rdb->rdb_BlockBytes != 0)) {
        fprintf(stderr, "RDB at block %d has BlockBytes=%u; only %d-byte "
                "blocks are supported\n", rdb_blk, rdb->rdb_BlockBytes,
                TD_SECTOR);
        return (1);
    }
    if (m_load())
        return (1);

    switch (mode) {
        case MODE_CHECK:
            printf("Checking the RDB at block %d of %s unit %d\n", rdb_blk,
                   disk_device, disk_unit);
            m_check();
            m_report(&errors, &warnings);
            return (errors ? 1 : 0);

        case MODE_FIX:
            printf("Checking the RDB at block %d of %s unit %d\n", rdb_blk,
                   disk_device, disk_unit);
            m_check();
            m_report(&errors, &warnings);
            if (m_apply_fixes() == 0) {
                if (nprob != 0)
                    printf("Nothing can be fixed automatically\n");
                return (errors ? 1 : 0);
            }
            rc = m_write();
            if (rc != 0)
                return (1);

            /* Check again what is now on disk */
            if (blk_read(rdb, rdb_blk))
                return (1);
            nprob = 0;
            if (m_load())
                return (1);
            m_check();
            printf("\nAfter fixing:\n");
            m_report(&errors, &warnings);
            return (errors ? 1 : 0);

        default:
            m_print_table();
            return (m_edit());
    }
}

static void
perr(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    exit(1);
}
