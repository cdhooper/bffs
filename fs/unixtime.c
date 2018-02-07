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

#include <dos/dos.h>
#include <clib/dos_protos.h>
#include "stat.h"
#include "unixtime.h"

long GMT = -8;  /* GMT offset for localtime */

/*
 * unix_time_to_amiga_ds()
 *      Convert UNIX time to Amiga DateStamp format
 */
void
unix_time_to_amiga_ds(ULONG timeval, struct DateStamp *ds)
{
    timeval += GMT * 3600;   /* UTC offset */

    /* (1978 - 1970) * 365.25 = 2922 */
    ds->ds_Days   = timeval / 86400 - 2922;
    ds->ds_Minute = (timeval % 86400) / 60;
    ds->ds_Tick   = (timeval % 60) * TICKS_PER_SECOND;
}

/*
 * amiga_ds_to_unix_time()
 *      Convert Amiga DateStamp to UNIX time format (UTC seconds since 1970).
 */
ULONG
amiga_ds_to_unix_time(struct DateStamp *ds)
{
    ULONG timeval;

    timeval  = 2922 * 86400               /* Adjust 1970 -> 1978 */
               - GMT * 3600               /* UTC offset */
               + ds->ds_Days * 24 * 3600  /* Days */
               + ds->ds_Minute * 60       /* Minutes */
               + ds->ds_Tick / 50;        /* Seconds */

    return (timeval);
}

/*
 * unix_time()
 *      Acquire current system time in UNIX format (UTC seconds since 1970).
 */
ULONG
unix_time(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (amiga_ds_to_unix_time(&ds));
}
