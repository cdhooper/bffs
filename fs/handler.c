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

#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <devices/timer.h>
#include <dos/filehandler.h>
#ifdef GCC
#include <inline/dos.h>
#else
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#endif
#include "config.h"
#include "handler.h"
#include "packets.h"
#include "table.h"
#include "stat.h"
#include "bffs_dosextens.h"
#include "ufs.h"
#include "system.h"

#define RESTART_TIMER timerIO->tr_time.tv_secs  = timer_secs;   \
                      timerIO->tr_time.tv_micro = 0;            \
                      SendIO(&timerIO->tr_node)

extern int         cache_item_dirty;
extern int         timer_secs;  /* delay seconds after write for cleanup */
extern int         timer_loops; /* maximum delays before forced cleanup */

struct MsgPort    *DosPort;
struct DosPacket  *pack;
struct DeviceNode *DevNode;
struct DeviceList *VolNode;
struct global      global;
int                inhibited         = 0;
int                motor_is_on       = 0;
int                dev_openfail      = 1;
int                timing            = 0;
int                receiving_packets = 1;
char              *handler_name      = NULL;

static struct Process     *BFFSTask;
static struct timerequest *timerIO     = NULL;
static struct MsgPort     *timerPort   = NULL;
static int                 write_items = 0;
static int                 write_delay = 0;

#ifdef GCC
struct MsgPort *CreatePort(UBYTE *name, long pri);
ULONG SysBase;
int __nocommandline = 1; /* Disable commandline parsing */
/* int __initlibraries = 0;  * Disable auto-library-opening */
#endif

static void
strlower(char *str)
{
    while (*str != '\0') {
        if ((*str >= 'A') && (*str <= 'Z'))
            *str += ('a' - 'A');
        str++;
    }
}

static void
NameHandler(const char *name)
{
    const char *pos;
    if (handler_name != NULL)
        return;

handler_top:
    handler_name = (char *) AllocMem(name[0], MEMF_PUBLIC);
    if (handler_name == NULL) {
        PRINT2(("NameHandler: unable to allocate %d bytes\n", name[0]));
        goto handler_top;
    }
    strncpy(handler_name, name + 1, name[0] - 1);
    handler_name[name[0] - 1] = '\0';
    if (pos = strchr(handler_name, ':'))
        *pos = '\0';
    strlower(handler_name);
    PRINT(("handler=\"%s\"\n", handler_name));
}

static void
UnNameHandler(void)
{
    if (handler_name) {
        FreeMem(handler_name, strlen(handler_name) + 1);
        handler_name = NULL;
    }
}

static int
open_timer(void)
{
    if (!(timerPort = CreatePort("BFFSsync_timer", 0))) {
        PRINT2(("Unable to CreatePort for timerIO\n"));
        return (1);
    }


    if (!(timerIO = (struct timerequest *)
                    CreateExtIO(timerPort, sizeof (struct timerequest)))) {
        PRINT2(("Unable to CreateExtIO for timerIO\n"));
        return (1);
    }

    if (OpenDevice(TIMERNAME, UNIT_VBLANK, &timerIO->tr_node, 0)) {
        PRINT2(("Unable to open timer device %s\n", TIMERNAME));
        return (1);
    }

    return (0);
}

static void
close_timer(int normal)
{
    if (normal && timerIO) {
        if (timing)
            WaitIO(&timerIO->tr_node);
        CloseDevice(&timerIO->tr_node);
    }

    if (timerPort) {
        DeletePort(timerPort);
        timerPort = NULL;
    }

    if (timerIO) {
        DeleteExtIO(&timerIO->tr_node);
        timerIO = NULL;
    }
}

void
handler(void)
{
    ULONG         signalval;
    ULONG         dcsignal;
    ULONG         dossignal;
    ULONG         timersignal;
    ULONG         receivedsignal;
    int           index = 0;
    int           msgtype;
    int           check_inhibit;
    packet_func_t pkt_func;
    const char   *pkt_name;
    struct Node **msghead;

#ifdef GCC
    SysBase = *((ULONG *) 4);
#endif
    BFFSTask = (struct Process *) FindTask(NULL);
    DosPort  = &(BFFSTask->pr_MsgPort);

    stats_init();

    PRINT2(("%s\n", version + 7));

    pack = WaitPkt();  /* wait for startup packet */

    NameHandler(BTOC(pack->dp_Arg1));

    DevNode = (struct DeviceNode *) BTOC(pack->dp_Arg3);
    DevNode->dn_Task = DosPort;

    if (open_timer()) {
        close_timer(0);
        PRINT2(("Cannot open timer, unable to continue\n"));
        receiving_packets = 0;
        goto ignore_packets;
    }

    if (open_dchange()) {
        close_dchange(0);
        PRINT(("Cannot open disk change service\n"));
/*
 *  Don't consider it fatal that dchange is not supported
 *
 *      close_timer(1);
 *      receiving_packets = 0;
 *      goto ignore_packets;
 */
    }

    dossignal   = 1L << DosPort->mp_SigBit;
    timersignal = 1L << timerPort->mp_SigBit;
    dcsignal    = IntData.sigMask;
    signalval   = dossignal | timersignal | dcsignal;

    timerIO->tr_node.io_Command = TR_ADDREQUEST;
    msghead = &(BFFSTask->pr_MsgPort.mp_MsgList.lh_Head);

    timing = 0;
    msgtype = 5;

    goto interpacket;

    while (receiving_packets) {
        receivedsignal = Wait(signalval);
        msgtype = (*msghead)->ln_Type;

        if (receivedsignal & timersignal) {
            /* see below for the timer policy implemented */
            if (cache_item_dirty > write_items) {
                if (write_delay <= 0) {
                    PRINT(("Flush, motor off\n"));
                    PFlush();
                    timing = 0;
                } else {
                    write_items = cache_item_dirty;
                    write_delay--;
                    RESTART_TIMER;
                    timing = 1;
                }
            } else {
                PFlush();
                timing = 0;
            }
        }

        if (receivedsignal & dcsignal)
            PDiskChange();

        if (msgtype == 0) {   /* no DOS messages waiting */
            if ((timing == 0) && motor_is_on)
                motor_off();
            continue;
        }

        /* spin to receive max five (msgtype) packets at a time */
        while (msgtype-- && (*msghead)->ln_Type) {
            pack = WaitPkt();

interpacket:

            global.Res1 = DOSTRUE;
            global.Res2 = 0L;

            pkt_func = packet_lookup(pack->dp_Type, &pkt_name, &check_inhibit);
            if (pkt_func != NULL) {
                PRINT(("%-14s 1=%d 2=%d 3=%d 4=%d\n", pkt_name, pack->dp_Arg1,
                        pack->dp_Arg2, pack->dp_Arg3, pack->dp_Arg4));

                if (dev_openfail && check_inhibit)
                    if (open_filesystem())
                        goto endpack;

                pkt_func();
                goto endpack;
            } else {
                PRINT1(("Unknown Packet %d 1=%d 2=%d 3=%d 4=%d\n",
                        pack->dp_Type, pack->dp_Arg1, pack->dp_Arg2,
                        pack->dp_Arg3, pack->dp_Arg4));

                global.Res1 = DOSFALSE;
                global.Res2 = ERROR_ACTION_NOT_KNOWN;
            }

endpack:

            /*
             * Strategy: If the motor is on, start timer to shut it off.
             * Once the timer expires, if there have been writes,
             *          restart the timer again.  If the timer expires
             *          a second time, then flush the buffer regardless.
             *          Turn the motor off once there are no dirty buffers
             */

            if (motor_is_on || cache_item_dirty) {
                if (!motor_is_on)       /* cache_item_dirty implied */
                    motor_on();
                write_delay = timer_loops;
                if (!timing) {
                    write_items = cache_item_dirty;
                    RESTART_TIMER;
                    timing = 1;
                }
            }

            ReplyPkt(pack, global.Res1, global.Res2);
        }
    }

    close_files();
    close_filesystem();
    RemoveVolNode();

    /*
     * According to the RKM, it's not wise to disappear completely, so we
     * will "just say no" to anything sent to us.  Eventually, wait for
     * all locks to be freed, then exit.
     */

    close_dchange(1);
    close_timer(1);
    UnNameHandler();
    strcpy(stat->disk_type, "Quiescent");
    PRINT(("Filesystem quiescent\n"));
    inhibited = 1;

    while (1) {
        pack = WaitPkt();
        switch (pack->dp_Type) {
            case ACTION_FS_STATS:
                inhibited = 0;
                if (open_filesystem()) {
                    PRINT2(("Cannot open filesystem\n"));
                    continue;
                }
                NameHandler("unknown:");
                if (open_timer()) {
                    close_timer(0);
                    PRINT2(("Cannot open timer\n"));
                    continue;
                }
                if (open_dchange())
                    close_dchange(0);
                receiving_packets = 1;
                goto endpack;
            case ACTION_FREE_LOCK:
                FreeLock((BFFSLock_t *) BTOC(pack->dp_Arg2));
                goto ignore_packets;
            case ACTION_END:
                PEnd();
                goto ignore_packets;
        }
        PRINT(("dead: p=%d, 1=%d 2=%d 3=%d 4=%d\n", pack->dp_Type,
                pack->dp_Arg1, pack->dp_Arg2, pack->dp_Arg3, pack->dp_Arg4));
ignore_packets:
        ReplyPkt(pack, DOSFALSE, ERROR_ACTION_NOT_KNOWN);
    }
}
