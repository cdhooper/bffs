#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <devices/timer.h>
#include <dos/filehandler.h>
#include <dos30/dosextens.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>

#include "debug.h"
#include "handler.h"
#include "packets.h"
#include "table.h"

#define NULL 0
#define RESTART_TIMER timerIO->tr_time.tv_secs  = timer_secs;	\
		      timerIO->tr_time.tv_micro = 0;		\
		      SendIO(timerIO)

extern struct InterruptData {
	struct	Task *sigTask;
	ULONG	sigMask;
} IntData;
extern	int cache_item_dirty;

struct	Process		*BFFSTask;
struct	MsgPort		*DosPort;
struct	DosPacket	*pack;
struct	DeviceNode	*DevNode;
struct	DeviceList	*VolNode;
struct	timerequest	*timerIO     = NULL;
struct	MsgPort		*timerPort   = NULL;
struct	global		global;
int	inhibited	= 0;
int	motor_is_on	= 0;
int	dev_openfail	= 1;
char	*handler_name	= NULL;
int	timing		= 0;
int	write_items	= 0;
int	write_delay	= 0;
int	receiving_packets = 1;
int	timer_secs	= 1;
int	timer_loops	= 10;

/* trying to reduce stack space consumption, these are for main */
ULONG  signal;
ULONG  dcsignal;
ULONG  dossignal;
ULONG  timersignal;
ULONG  receivedsignal;
struct Node **msghead;
int    msgtype;
int    entries = 0;

handler()
{
    int	   index   = 0;

    BFFSTask = (struct Process *) FindTask(NULL);
    DosPort  = &(BFFSTask->pr_MsgPort);

    InitStats();

    while (packet_table[entries].packet_type != LAST_PACKET)
	entries++;

    pack = WaitPkt();  /* wait for startup packet */

    NameHandler(BTOC(pack->dp_Arg1));

    DevNode = (struct DeviceNode *) BTOC(pack->dp_Arg3);
    DevNode->dn_Task = DosPort;

    if (open_timer()) {
	close_timer(0);
	PRINT(("cannot open timer, unable to continue\n"));
	receiving_packets = 0;
	goto ignore_packets;
    }

    if (open_dchange()) {
	close_dchange(0);
	close_timer(1);
	PRINT(("cannot open disk change service, unable to continue\n"));
	receiving_packets = 0;
	goto ignore_packets;
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

	    for (index = 0; index < entries; index++)
	        if (packet_table[index].packet_type == pack->dp_Type)
		    if (inhibited && packet_table[index].check_inhibit) {
			PRINT(("Sorry, we're inhibited.\n"));
			global.Res1 = DOSFALSE;
			global.Res2 = ERROR_NO_DISK;
			goto endpack;
		    } else
			break;

	    if (dev_openfail && packet_table[index].check_inhibit) {
	        open_filesystem();
	        if (dev_openfail) {
		    global.Res1 = DOSFALSE;
		    goto endpack;
	        }
	    }

	    PRINT(("%s 1=%d 2=%d 3=%d 4=%d\n",
		    packet_table[index].name, pack->dp_Arg1, pack->dp_Arg2,
		    pack->dp_Arg3, pack->dp_Arg4));

	    /* eventually make the incoming packet a global and have
		the functions get only the needed data from this global */

	    switch(packet_table[index].call_type) {
	        case ALL:
		    packet_table[index].routine(pack->dp_Arg1, pack->dp_Arg2,
					        pack->dp_Arg3, pack->dp_Arg4);
		    break;
	        case NNN:
		    packet_table[index].routine();
		    break;
	        case CNN:
		    packet_table[index].routine(BTOC(pack->dp_Arg1));
		    break;
	        case NCN:
		    packet_table[index].routine(BTOC(pack->dp_Arg2));
		    break;
	        case NON:
	        case BAD:
		    PRINT(("p=%d, c=%d n=%s 1=%d 2=%d 3=%d 4=%d unknown packet\n",
			    pack->dp_Type, packet_table[index].call_type,
			    packet_table[index].name, pack->dp_Arg1, pack->dp_Arg2,
			    pack->dp_Arg3, pack->dp_Arg4));
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_NOT_IMPLEMENTED;
		    break;
	        default:
		    PRINT(("ERROR! Unknown call %d\n", packet_table[index].call_type));
		    global.Res1 = DOSFALSE;
		    global.Res2 = ERROR_NOT_IMPLEMENTED;
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

    close_files();
    close_filesystem();
    close_dchange(1);
    close_timer(1);
    UnNameHandler();
    strcpy(stat->disk_type, "Quiescent");
    PRINT(("Filesystem quiescent\n"));

    while (1) {
	pack = WaitPkt();
	if (pack->dp_Type == ACTION_FS_STATS) {
		receiving_packets = 1;
		inhibited = 0;
		open_filesystem();
		if (open_timer()) {
			close_timer(0);
			PRINT(("cannot open timer\n"));
			receiving_packets = 0;
			continue;
		}
		if (open_dchange()) {
			close_dchange(0);
			PRINT(("cannot open disk change service\n"));
			receiving_packets = 0;
			continue;
		}
		goto endpack;
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

	if (OpenDevice(TIMERNAME, UNIT_VBLANK, timerIO, 0)) {
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
			WaitIO(timerIO);
		CloseDevice(timerIO);
	}

	if (timerPort) {
		DeletePort(timerPort);
		timerPort = NULL;
	}

	if (timerIO) {
		DeleteExtIO(timerIO);
		timerIO = NULL;
	}
}
