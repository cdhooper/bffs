/* current file lookup description information */
struct global {
    ULONG   Pinum;
    ULONG   Poffset;
    ULONG   Res1;
    ULONG   Res2;
};

extern struct	MsgPort 	*dbPort;
extern struct	Process 	*BFFSTask;
extern struct	MsgPort 	*DosPort;
extern struct	DosLibrary	*DOSBase;
extern struct	DosPacket	*pack;
extern struct	DeviceList	*VolNode;
extern struct	global		global;
extern struct	stat		*stat;
extern struct	DeviceNode	*DevNode;

extern int	Found_dbPort;
extern int	inhibited;
extern int	dev_openfail;
extern int	receiving_packets;
extern int	motor_is_on;
extern int	cache_flush_pending;

/* C pointer to BCPL pointer */
#define CTOB(x) ((x)>>2)

/* BCPL pointer to C pointer */
#define BTOC(x) ((x)<<2)
