#include <exec/memory.h>
#include <exec/ports.h>
#include "config.h"

#ifdef DEBUG

#define	DEBUG
#define	DEBUG1
#define DEBUG2

#define STRING_LEN 256

struct dbMessage  {
    struct Message header;
    char buf[STRING_LEN];
};

#ifdef DEBUG
void debug(fmt, a, b, c, d, e, f, g)
char *fmt;
ULONG a, b, c, d, e, f, g;
{
    struct dbMessage	*message;
    struct MsgPort	*dbPort;

    Forbid();
    if (dbPort = (struct MsgPort *) FindPort("BFFSDebug")) {
	message = (struct dbMessage *)
		AllocMem(sizeof(struct dbMessage), MEMF_PUBLIC);
	if (message != NULL) {
		sprintf(message->buf, fmt, a, b, c, d, e, f, g);
		PutMsg(dbPort, message);
	}
    }
    Permit();
}
#else
void debug()
{
}
#endif DEBUG


#ifdef DEBUG1
void debug1(fmt, a, b, c, d, e, f, g)
char *fmt;
ULONG a, b, c, d, e, f, g;
{
    struct dbMessage	*message;
    struct MsgPort	*dbPort;

    Forbid();
    if (dbPort = (struct MsgPort *) FindPort("BFFSDebug")) {
	message = (struct dbMessage *)
		AllocMem(sizeof(struct dbMessage), MEMF_PUBLIC);
	if (message != NULL) {
		sprintf(message->buf, fmt, a, b, c, d, e, f, g);
		PutMsg(dbPort, message);
	}
    }
    Permit();
}
#else
void debug1()
{
}
#endif DEBUG1


#ifdef DEBUG2
void debug2(fmt, a, b, c, d, e, f, g)
char *fmt;
ULONG a, b, c, d, e, f, g;
{
    struct dbMessage	*message;
    struct MsgPort	*dbPort;

    Forbid();
    if (dbPort = (struct MsgPort *) FindPort("BFFSDebug")) {
	message = (struct dbMessage *)
		AllocMem(sizeof(struct dbMessage), MEMF_PUBLIC);
	if (message != NULL) {
		sprintf(message->buf, fmt, a, b, c, d, e, f, g);
		PutMsg(dbPort, message);
	}
    }
    Permit();
}
#else
void debug2()
{
}
#endif DEBUG2

#endif DEBUG
