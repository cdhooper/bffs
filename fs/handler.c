#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <devices/timer.h>
#include <dos/filehandler.h>
#include <dos30/dosextens.h>

#ifdef GCC
#include <inline/dos.h>
struct MsgPort *CreatePort(UBYTE *name, long pri);
ULONG SysBase;
int __nocommandline = 1; /* Disable commandline parsing */
/* int __initlibraries = 0;  * Disable auto-library-opening */
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

#define RESTART_TIMER timerIO->tr_time.tv_secs  = timer_secs;	\
		      timerIO->tr_time.tv_micro = 0;		\
		      SendIO(&timerIO->tr_node)

extern struct InterruptData {
	struct	Task *sigTask;
	ULONG	sigMask;
} IntData;
extern	int cache_item_dirty;

static struct	Process		*BFFSTask;
static struct	timerequest	*timerIO     = NULL;
static struct	MsgPort		*timerPort   = NULL;
static int	write_items	= 0;
static int	write_delay	= 0;

struct	MsgPort		*DosPort;
struct	DosPacket	*pack;
struct	DeviceNode	*DevNode;
struct	DeviceList	*VolNode;
struct	global		global;
int	inhibited	= 0;
int	motor_is_on	= 0;
int	dev_openfail	= 1;
char	*handler_name	= NULL;
int	timing		= 0;
int	receiving_packets = 1;

extern int timer_secs;  /* delay seconds after write for cleanup */
extern int timer_loops; /* maximum delays before forced cleanup */
extern char *version;	/* BFFS version string */


/* attempt to reduce stack space consumption, these are for main */


void
handler(void)
{
    int    index = 0;

    ULONG  signal;
    ULONG  dcsignal;
    ULONG  dossignal;
    ULONG  timersignal;
    ULONG  receivedsignal;
    struct Node **msghead;
    int    msgtype;
    packet_func_t pkt_func;
    const char   *pkt_name;
    int	          check_inhibit;

#ifdef GCC
    SysBase = *((ULONG *) 4);
#endif
    BFFSTask = (struct Process *) FindTask(NULL);
    DosPort  = &(BFFSTask->pr_MsgPort);

    InitStats();

    PRINT(("%s\n", version + 7));

    pack = WaitPkt();  /* wait for startup packet */

    NameHandler(BTOC(pack->dp_Arg1));

    DevNode = (struct DeviceNode *) BTOC(pack->dp_Arg3);
    DevNode->dn_Task = DosPort;

    if (open_timer()) {
	close_timer(0);
	PRINT2(("cannot open timer, unable to continue\n"));
	receiving_packets = 0;
	goto ignore_packets;
    }

    if (open_dchange()) {
	close_dchange(0);
/*
 *  Don't consider it fatal that dchange is not supported
 *
	close_timer(1);
	PRINT2(("cannot open disk change service, unable to continue\n"));
	receiving_packets = 0;
	goto ignore_packets;
*/
    }

    dossignal	= 1L << DosPort->mp_SigBit;
    timersignal	= 1L << timerPort->mp_SigBit;
    dcsignal	= IntData.sigMask;
    signal	= dossignal | timersignal | dcsignal;

    timerIO->tr_node.io_Command = TR_ADDREQUEST;
    msghead = &(BFFSTask->pr_MsgPort.mp_MsgList.lh_Head);

    timing = 0;
    msgtype = 5;

    goto interpacket;

    while (receiving_packets) {
	receivedsignal = Wait(signal);
	msgtype = (*msghead)->ln_Type;

	if (receivedsignal & timersignal) {
		/* see below for the timer policy implemented */
		if (cache_item_dirty > write_items) {
		    if (write_delay <= 0) {
			PRINT(("flush, motor off\n"));
			PFlush();
			timing = 0;
		    } else {
			PRINT(("write delay %d\n", write_delay));
			write_items = cache_item_dirty;
			write_delay--;
			RESTART_TIMER;
			timing = 1;
		    }
		} else {
		    if (cache_item_dirty)
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
	        PRINT1(("%-14s 1=%d 2=%d 3=%d 4=%d\n", pkt_name, pack->dp_Arg1,
			pack->dp_Arg2, pack->dp_Arg3, pack->dp_Arg4));

		if (dev_openfail && check_inhibit)
		    if (open_filesystem())
			goto endpack;

		pkt_func();
		goto endpack;
	    } else {
		PRINT1(("p=%d E 1=%d 2=%d 3=%d 4=%d unknown packet\n",
			pack->dp_Type, pack->dp_Arg1, pack->dp_Arg2,
			pack->dp_Arg3, pack->dp_Arg4));

		global.Res1 = DOSFALSE;
		global.Res2 = ERROR_ACTION_NOT_KNOWN;
	    }

	    endpack:

	    /* strategy: If the motor is on, start timer to shut it off
	     *	Once the timer expires, if there have been writes,
	     *		restart the timer again.  If the timer expires
	     *		a second time, then flush the buffer regardless.
	     *		Turn the motor off once there are no dirty buffers
	     */

	    if (motor_is_on || cache_item_dirty) {
		if (!motor_is_on)	/* cache_item_dirty implied */
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

    /* according to the RKM, it's not wise to disappear completely, so we
	will "just say no" to anything sent to us.  Eventually, wait for
	all locks to be freed, then exit. */

    close_dchange(1);
    close_timer(1);
    UnNameHandler();
    strcpy(stat->disk_type, "Quiescent");
    PRINT(("Filesystem quiescent\n"));

    while (1) {
	pack = WaitPkt();
	switch (pack->dp_Type) {
	    case ACTION_FS_STATS:
		inhibited = 0;
		if (open_filesystem()) {
			PRINT(("cannot open filesystem\n"));
			continue;
		}
		if (open_timer()) {
			close_timer(0);
			PRINT(("cannot open timer\n"));
			continue;
		}
		if (open_dchange()) {
			close_dchange(0);
/*
 *  Don't consider it fatal that dchange is not supported
 *
			PRINT(("cannot open disk change service\n"));
			continue;
*/
		}
		receiving_packets = 1;
		goto endpack;
	    case ACTION_FREE_LOCK:
		FreeLock((struct BFFSLock *) BTOC(pack->dp_Arg1));
		goto ignore_packets;
	    case ACTION_END: {
		struct BFFSfh *fileh = (struct BFFSfh *) pack->dp_Arg1;
		FreeLock(fileh->lock);
		FreeMem(fileh, sizeof(struct BFFSfh));
		goto ignore_packets;
	    }
	}
	PRINT(("dead: p=%d, 1=%d 2=%d 3=%d 4=%d\n", pack->dp_Type,
		pack->dp_Arg1, pack->dp_Arg2, pack->dp_Arg3, pack->dp_Arg4));
	ignore_packets:
	ReplyPkt(pack, DOSFALSE, ERROR_ACTION_NOT_KNOWN);
    }
}

open_timer()
{
	if (!(timerPort = CreatePort("BFFSsync_timer", 0))) {
		PRINT(("unable to createport for timerIO\n"));
		return(1);
	}


	if (!(timerIO = (struct timerequest *)
			CreateExtIO(timerPort, sizeof(struct timerequest)))) {
		PRINT(("unable to createextio for timerIO\n"));
		return(1);
	}

	if (OpenDevice(TIMERNAME, UNIT_VBLANK, &timerIO->tr_node, 0)) {
		PRINT(("unable to open timer device %s\n", TIMERNAME));
		return(1);
	}

	return(0);
}

close_timer(normal)
int normal;
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
