/* Dos lock with extensions of parent inum and parent offset */
struct BFFSLock  {
	BPTR	fl_Link;	  /* next Dos Lock */
	LONG	fl_Key;		  /* inode number */
	LONG	fl_Access;	  /* 0=shared */
	struct	MsgPort *fl_Task; /* this handler's DosPort */
	BPTR	fl_Volume;	  /* volume node of this handler */
	ULONG	fl_Pinum;	  /* number of the parent inode */
	ULONG	fl_Poffset;	  /* offset in parent where we can find entry */
	struct	BFFSfh *fl_Fileh; /* filehandle (if exists) for this lock */
};

/* file handle, used for file IO */
struct BFFSfh  {
	struct	BFFSLock *lock;	  /* pointer to lock defining filehandle */
	ULONG	current_position; /* current file byte to be accessed */
	ULONG	maximum_position; /* largest file byte accessed */
	int	access_mode;	  /* 0=file has only been read so far */
	int	truncate_mode;	  /* truncate at maximum position on close */
	int	real_inum;	  /* actual inode number to use for I/O */
};


/* Packet routines - specified by table code */
void PUnimplemented();
void PLocateObject();

void PExamineObject(void);
void PExamineNext(void);
void PFindInput();
void PFindOutput();
void PRead();
void PWrite();
void PSeek();
void PEnd();
void PParent();
void PDeviceInfo();
void PDeleteObject();
void PMoreCache();
void PRenameDisk();
void PSetProtect();
void PGetPerms();
void PSetPerms();
void PCurrentVolume();
void PCopyDir();
void PInhibit();
void PDiskChange();
void PFormat();
void PWriteProtect();
void PIsFilesystem();
void PDie();
void PFlush();
void PSameLock();
void PRenameObject();
void PCreateDir();
void PFilesysStats();
void PCreateObject();
void PMakeLink();
void PReadLink();
void PSetFileSize();
void PSetDate();
void PSetDates();
void PSetTimes();
void PSetOwner();
void PDiskType();
void PFhFromLock();
void PCopyDirFh();
void PParentFh();
void PExamineFh();
void PExamineAll();
void PAddNotify();
void PRemoveNotify();
void PNil();
void PFileSysStats();
void PFreeLock();
void PGetDiskFSSM();
void PFreeDiskFSSM();

/* key is or'd with MSb on first examine */
#define MSb 1<<31

/* C pointer to BCPL pointer */
#ifdef CTOB
#undef CTOB
#endif
#define CTOB(x) (((unsigned long) x)>>2)

/* BCPL pointer to C pointer */
#ifdef BTOC
#undef BTOC
#endif
#define BTOC(x) (((unsigned long) x)<<2)

/* access modes */
#define MODE_READ	0
#define MODE_WRITE	1

/* truncate modes */
#define MODE_UPDATE	0
#define MODE_TRUNCATE	1
