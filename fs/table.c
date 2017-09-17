#include <dos30/dosextens.h>

#include "table.h"
#include "packets.h"

/*
	0 = do not check inhibit
	1 = check inhibit

	NON =	not implemented
	NNN =	NO_ARG,	NO_ARG,	NO_ARG (no parameters)
	NCN =	NO_ARG,	CHANGE,	NO_ARG
	CNN =	CHANGE,	NO_ARG,	NO_ARG
	CPN =	CHANGE,	PASS,	NO_ARG
	CCN =	CHANGE,	CHANGE,	NO_ARG
	CCP =	CHANGE,	CHANGE,	PASS
	CCC =	CHANGE,	CHANGE,	CHANGE
	ALL =	PASS,	PASS,	PASS,	PASS
	BAD =	Bad packet
*/

struct call_table packet_table[] = {
{ ACTION_LOCATE_OBJECT,	PLocateObject,	1, ALL, "LOCATE_OBJECT "},
{ ACTION_EXAMINE_NEXT,	PExamineNext,	1, ALL, "EXAMINE_NEXT  "},
{ ACTION_EXAMINE_OBJECT,PExamineObject,	1, ALL, "EXAMINE_OBJECT"},
{ ACTION_READ,		PRead,		1, ALL, "READ          "},
{ ACTION_WRITE,		PWrite,		1, ALL, "WRITE         "},
{ ACTION_FREE_LOCK,	PFreeLock,	1, CNN, "FREE_LOCK     "},
{ ACTION_FINDINPUT,	PFindInput,	1, ALL, "FINDINPUT     "},
{ ACTION_FINDOUTPUT,	PFindOutput,	1, ALL, "FINDOUTPUT    "},
{ ACTION_FINDUPDATE,	PFindInput,	1, ALL, "FINDUPDATE    "},
{ ACTION_SEEK,		PSeek,		1, ALL, "SEEK          "},
{ ACTION_END,		PEnd,		1, ALL, "END           "},
{ ACTION_DELETE_OBJECT,	PDeleteObject,	1, ALL, "DELETE_OBJECT "},
{ ACTION_CREATE_DIR,	PCreateDir,	1, ALL, "CREATE_DIR    "},
{ ACTION_IS_FILESYSTEM,	PIsFilesystem,	1, NNN, "IS_FILESYSTEM "},
{ ACTION_COPY_DIR,	PCopyDir,	1, CNN, "COPY_DIR      "},
{ ACTION_PARENT,	PParent,	1, CNN, "PARENT        "},
{ ACTION_DISK_INFO,	PDeviceInfo,	1, CNN, "DISK_INFO     "},
{ ACTION_SAME_LOCK,	PSameLock,	1, ALL, "SAME_LOCK     "},
{ ACTION_SET_PROTECT,	PSetProtect,	1, ALL, "SET_PROTECT   "},
{ ACTION_INHIBIT,	PInhibit,	0, ALL, "INHIBIT       "},
{ ACTION_WRITE_PROTECT,	PWriteProtect,	1, ALL, "WRITE_PROTECT "},
{ ACTION_MORE_CACHE,	PMoreCache,	1, ALL, "MORE_CACHE    "},
{ ACTION_RENAME_OBJECT,	PRenameObject,	1, ALL, "RENAME        "},
{ ACTION_RENAME_DISK,	PRenameDisk,	1, ALL, "RENAME_DISK   "},
{ ACTION_INFO,		PDeviceInfo,	1, NCN, "INFO          "},
{ ACTION_DISK_CHANGE,	PDiskChange,	1, NNN, "DISK_CHANGE   "},
{ ACTION_FLUSH,		PFlush,		1, NNN, "FLUSH         "},
{ ACTION_TIMER,		PFlush,		1, NNN, "TIMER         "},
{ ACTION_FORMAT,	PFormat,	1, ALL, "FORMAT        "},
{ ACTION_NIL,		PNil,		0, NNN, "NIL           "},
{ ACTION_DIE,		PDie,		0, NNN, "DIE           "},
{ ACTION_FS_STATS,	PFilesysStats,	0, NNN, "FS_STATS      "},
{ ACTION_CURRENT_VOLUME,PCurrentVolume,	1, NNN, "CURRENT_VOLUME"},
{ ACTION_SET_OWNER,	PSetOwner,	1, ALL, "SET_OWNER     "},

{ ACTION_CREATE_OBJECT,	PCreateObject,	1, ALL, "CREATE_OBJECT "},
{ ACTION_MAKE_LINK,	PMakeLink,	1, ALL, "MAKE_LINK     "},
{ ACTION_READ_LINK,	PReadLink,	1, ALL, "READ_LINK     "},
{ ACTION_SET_FILE_SIZE,	PSetFileSize,	1, ALL, "SET_FILE_SIZE "},
{ ACTION_SET_DATE,	PSetDate,	1, ALL, "SET_DATE      "},
{ ACTION_SET_DATES,	PSetDates,	1, ALL, "SET_DATES     "},

{ ACTION_EXAMINE_FH,	PExamineFh,	1, ALL, "EXAMINE_FH    "},

{ ACTION_DISK_TYPE,	Not_Implemented,0, NON, "DISK_TYPE     "},
{ ACTION_FH_FROM_LOCK,	Not_Implemented,0, NON, "FH_FROM_LOCK  "},
{ ACTION_COPY_DIR_FH,	Not_Implemented,0, NON, "COPY_DIR_FH   "},
{ ACTION_PARENT_FH,	Not_Implemented,0, NON, "PARENT_FH     "},
{ ACTION_EXAMINE_ALL,	Not_Implemented,0, NON, "EXAMINE_ALL   "},
{ ACTION_ADD_NOTIFY,	Not_Implemented,0, NON, "ADD_NOTIFY    "},
{ ACTION_REMOVE_NOTIFY,	Not_Implemented,0, NON, "REMOVE_NOTIFY "},
/*
{ ACTION_EXAMINE_FH,	Not_Implemented,0, NON, "EXAMINE_FH    "},

{ ACTION_DISK_TYPE,	PDiskType,	1, ALL, "DISK_TYPE     "},
{ ACTION_FH_FROM_LOCK,	PFhFromLock,	1, ALL, "FH_FROM_LOCK  "},
{ ACTION_COPY_DIR_FH,	PCopyDirFh,	1, ALL, "COPY_DIR_FH   "},
{ ACTION_PARENT_FH,	PParentFh,	1, ALL, "PARENT_FH     "},
{ ACTION_EXAMINE_ALL,	PExamineAll,	1, ALL, "EXAMINE_ALL   "},
{ ACTION_ADD_NOTIFY,	PAddNotify,	1, ALL, "ADD_NOTIFY    "},
{ ACTION_REMOVE_NOTIFY,	PRemoveNotify,	1, ALL, "REMOVE_NOTIFY "},
*/
{ ACTION_EVENT,		Not_Implemented,0, NON, "EVENT         "},
{ ACTION_SET_COMMENT,	Not_Implemented,0, NON, "SET_COMMENT   "},
{ ACTION_CHANGE_MODE,	Not_Implemented,0, NON, "CHANGE_MODE   "},
{ ACTION_LOCK_RECORD,	Not_Implemented,0, NON, "LOCK_RECORD   "},
{ ACTION_FREE_RECORD,	Not_Implemented,0, NON, "FREE_RECORD   "},

{ ACTION_WAIT_CHAR,	Not_Implemented,0, NON, "WAIT_CHAR     "},
{ ACTION_SCREEN_MODE,	Not_Implemented,0, NON, "SCREEN_MODE   "},
{ ACTION_WRITE_RETURN,	Not_Implemented,0, NON, "WRITE_RETURN  "},
{ ACTION_GET_BLOCK,	Not_Implemented,0, NON, "GET_BLOCK     "},
{ ACTION_SET_MAP,	Not_Implemented,0, NON, "SET_MAP       "},

{ LAST_PACKET,		NO_CALL,	0, BAD, "UNKNOWN_PACKET"}
};
