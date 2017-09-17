#include <exec/memory.h>
#include <exec/ports.h>
#include <debug.h>

#ifdef DEBUG

#define STRING_LEN 256

struct dbMessage  {
    struct Message bla;
    char buf[STRING_LEN];
};

void dbprint(fmt, a, b, c, d, e, f, g)
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

#endif
