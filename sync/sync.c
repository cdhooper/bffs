/*
 * sync
 *
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
 * Utility to sync filesystems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <clib/alib_protos.h>
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>

const char *version = "\0$VER: sync 1.0 (08-Feb-2018) © Chris Hooper";

#define BTOC(x) ((x)<<2)

static int vflag = 0;

static int
sync_filesystem(const char *path)
{
    struct MsgPort        *msgport;
    struct MsgPort        *replyport;
    struct StandardPacket *packet;
    int                    rc = 0;

    msgport = (struct MsgPort *) DeviceProc(path);
    if (msgport == NULL)
        return (1);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (replyport == NULL)
        return (1);

    packet = (struct StandardPacket *)
             AllocMem(sizeof (struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
        DeletePort(replyport);
        return (1);
    }

    packet->sp_Msg.mn_Node.ln_Name = (char *)&(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_FLUSH;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE)
        rc = 1;

    FreeMem(packet, sizeof (struct StandardPacket));
    DeletePort(replyport);

    return (rc);
}

typedef struct pathlist {
    char            *path;
    struct pathlist *next;
} pathlist_t;

static int
sync_all(void)
{
    int              rc   = 0;
    pathlist_t      *head = NULL;
    pathlist_t      *cur;
    struct Library  *dosbase;
    struct RootNode *rootnode;
    struct DosInfo  *dosinfo;
    struct DevInfo  *devinfo;

    dosbase  = OpenLibrary("dos.library", 0L);
    rootnode = ((struct DosLibrary *) dosbase)->dl_Root;
    dosinfo  = (struct DosInfo *) BTOC(rootnode->rn_Info);

    /* Generate list of devices */
    Forbid();
        devinfo = (struct DevInfo *) BTOC(dosinfo->di_DevInfo);
        while (devinfo != NULL) {
            if ((devinfo->dvi_Type == DLT_DEVICE) &&
                (devinfo->dvi_Task != 0)) {
                char *temp = (char *) BTOC(devinfo->dvi_Name);
                cur = malloc(sizeof (*cur));
                cur->path = malloc(*temp + 2);
                sprintf(cur->path, "%.*s:", *temp, temp + 1);
                cur->next = head;
                head = cur;
            }
            devinfo = (struct DeviceList *) BTOC(devinfo->dvi_Next);
        }
    Permit();
    CloseLibrary(dosbase);

    for (cur = head; cur != NULL; cur = cur->next) {
        int fail = sync_filesystem(cur->path);
        if (vflag)
            printf("%s %s\n", cur->path, fail ? "Failure" : "Success");
        rc |= fail;
    }

    cur = head;
    while (cur != NULL) {
        pathlist_t *next = cur->next;
        free(cur->path);
        free(cur);
        cur  = next;
    }
    return (rc);
}

static void
print_usage(const char *progname)
{
    printf("%s\n"
           "usage: %s [<options>] [<device>...]\n"
           "options:\n"
           "   -h = display this help text\n"
           "   -v = verbose mode\n",
           version + 7, progname);
    exit(1);
}

int
main(int argc, char *argv[])
{
    int arg;
    int rc = 0;
    int count = 0;

    for (arg = 1 ; arg < argc; arg++) {
        char *ptr = argv[arg];
        if (*ptr == '-') {
            while (*(++ptr) != '\0') {
                if (strcmp(ptr, "--help") == 0)
                    print_usage(argv[0]);
                switch (*ptr) {
                    case 'v':
                        vflag++;
                        break;
                    case 'h':
                        print_usage(argv[0]);
                    default:
                        printf("Unknown argument -%s\n\n", ptr);
                        print_usage(argv[0]);
                        break;
                }
            }
        } else {
            int fail = sync_filesystem(ptr);
            if (vflag)
                printf("%s %s\n", ptr, fail ? "Failure" : "Success");
            rc |= fail;
            count++;
        }
    }
    if (count == 0)
        exit(sync_all());

    exit(rc);
}
