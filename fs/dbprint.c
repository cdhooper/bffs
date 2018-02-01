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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <exec/ports.h>
#include <dos/dos.h>

#define STRING_LEN 256
#define BFFSDEBUG "BFFSDebug"

const char *version = "\0$VER: dbprint 1.1 (19-Jan-2018) © Chris Hooper";

/*  Define our own message structure... */
struct dbMessage  {
    struct Message msg;
    char buf[STRING_LEN];
};

int
break_abort(void)
{
    struct dbMessage *message;
    struct MsgPort   *incoming;

    Forbid();
    while (incoming = FindPort(BFFSDEBUG)) {
	while (message = (struct dbMessage *)
	    GetMsg(incoming))
	FreeMem(message, sizeof(struct dbMessage));
	DeletePort(incoming);
    }
    Permit();

printf("clean\n");
    exit(0);
}

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

int
main(int argc, char *argv[])
{
    struct dbMessage *message;
    struct MsgPort   *incoming;
    FILE *fp;
    ULONG signals;
    ULONG sigmask;
    int force = 0;

    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
	argc--;
	argv++;
	force++;
    }
    if (argc > 1)
	fp = fopen(argv[1], "w");
    else
	fp = NULL;

    onbreak(break_abort);
    Forbid();
    incoming = FindPort(BFFSDEBUG);
    Permit();
    if (incoming != NULL) {
	if (!force) {
	    output_string(fp, "Debug port already open\n");
	    goto clean_up;
	}
    } else {
	/* Let's create a port that everyone can talk to... */
	incoming = CreatePort((UBYTE *)BFFSDEBUG, 10L);
	if (incoming == NULL) {
	    output_string(fp, "Unable to create debug port\n");
	    goto clean_up;
	}
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

    exit(0);
}
