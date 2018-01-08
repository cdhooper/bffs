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
#include <stdarg.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <clib/exec_protos.h>
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

int debug_level = 0;

static void
debug(int this_level, const char *fmt, va_list va)
{
    struct MsgPort   *dbport;
    struct dbMessage *message;

    if (debug_level > this_level)
	return;

    Forbid();
    if ((dbport = (struct MsgPort *) FindPort("BFFSDebug")) != NULL) {
	message = (struct dbMessage *)
		AllocMem(sizeof(struct dbMessage), MEMF_PUBLIC);
	if (message != NULL) {
	    vsprintf(message->buf, fmt, va);
	    PutMsg(dbport, &message->header);
	}
    }
    Permit();
}

void
debug0(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    debug(0, fmt, va);
    va_end(va);
}

void
debug1(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    debug(1, fmt, va);
    va_end(va);
}

void
debug2(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    debug(2, fmt, va);
    va_end(va);
}
#endif /* DEBUG */
