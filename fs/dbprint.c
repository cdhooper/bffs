#include <stdio.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos30/dos.h>
#include <dos30/dosextens.h>

#include <clib/alib_protos.h>
#include <clib/exec_protos.h>

#define STRING_LEN 256

/*  Define our own message structure... */
struct dbMessage  {
    struct Message bla;
    char buf[STRING_LEN];
};

int main(argc, argv)
int argc;
char *argv[];
{
    struct MsgPort *incoming;
    ULONG signals;
    ULONG sigmask;
    struct dbMessage *message;
    FILE *fp;

    /* Let's create a port that everyone can talk to... */
    incoming = CreatePort((UBYTE *)"BFFSDebug",10L);

    if (argc > 1)
	fp = fopen(argv[1], "w");
    else
	fp = NULL;

    sigmask = (1 << incoming->mp_SigBit) | SIGBREAKF_CTRL_C;

    while((signals = Wait(sigmask)) & ~SIGBREAKF_CTRL_C)
	while (message = (struct dbMessage *) GetMsg(incoming)) {
	    PutStr(message->buf);
/*
	    printf("%s", message->buf);
*/
	    if (fp) {
		fprintf(fp, "%s", message->buf);
		fflush(fp);
	    }
	    FreeMem(message, sizeof(struct dbMessage));
	};

    Forbid();
    while (incoming = FindPort("BFFSDebug")) {
	while (message = (struct dbMessage *)
		GetMsg(incoming))
	FreeMem(message, sizeof(struct dbMessage));
	DeletePort(incoming);
    }
    Permit();

    if (fp)
	fclose(fp);

    return(0);
}
