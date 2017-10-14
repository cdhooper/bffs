#include <stdio.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos30/dos.h>
#include <dos30/dosextens.h>

#include <clib/alib_protos.h>
#include <clib/exec_protos.h>

#define STRING_LEN 256
#define BFFSDEBUG "BFFSDebug"

/*  Define our own message structure... */
struct dbMessage  {
    struct Message msg;
    char buf[STRING_LEN];
};

void
output_string(FILE *fp, char *str)
{
    if (fp != NULL) {
        fprintf(fp, "%s", str);
        fflush(fp);
    } else {
        PutStr(str);
    }
}

int main(argc, argv)
int argc;
char *argv[];
{
    struct MsgPort *incoming;
    ULONG signals;
    ULONG sigmask;
    struct dbMessage *message;
    FILE *fp;

    if (argc > 1)
	fp = fopen(argv[1], "w");
    else
	fp = NULL;

    Forbid();
    incoming = FindPort(BFFSDEBUG);
    Permit();
    if (incoming != NULL) {
        output_string(fp, "Debug port already open\n");
        goto clean_up;
    }

    /* Let's create a port that everyone can talk to... */
    incoming = CreatePort((UBYTE *)BFFSDEBUG, 10L);
    if (incoming == NULL) {
        output_string(fp, "Unable to create debug port\n");
        goto clean_up;
    }

    sigmask = (1 << incoming->mp_SigBit) | SIGBREAKF_CTRL_C;

    while ((signals = Wait(sigmask)) & ~SIGBREAKF_CTRL_C) {
	while (message = (struct dbMessage *) GetMsg(incoming)) {
            output_string(fp, message->buf);
	    FreeMem(message, sizeof(struct dbMessage));
	}
    }

    Forbid();
    while (incoming = FindPort(BFFSDEBUG)) {
	while (message = (struct dbMessage *)
		GetMsg(incoming))
	FreeMem(message, sizeof(struct dbMessage));
	DeletePort(incoming);
    }
    Permit();

clean_up:
    if (fp)
	fclose(fp);

    return(0);
}
