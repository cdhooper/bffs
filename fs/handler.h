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

#ifndef _HANDLER_H
#define _HANDLER_H

/* current file lookup description information */
struct global {
    ULONG   Pinum;
    ULONG   Poffset;
    ULONG   Res1;
    ULONG   Res2;
};

extern struct Process     *BFFSTask;
extern struct MsgPort     *DosPort;
extern struct DosLibrary  *DOSBase;
extern struct DosPacket   *pack;
extern struct DeviceList  *VolNode;
extern struct stat        *stat;
extern struct DeviceNode  *DevNode;
extern struct global       global;

extern int		   inhibited;
extern int		   dev_openfail;
extern int		   receiving_packets;
extern int		   motor_is_on;

/* C pointer to BCPL pointer */
#ifdef CTOB
#undef CTOB
#endif
#define CTOB(x) (((unsigned long) x)>>2)

/* BCPL pointer to C pointer */
#ifdef BTOC
#undef BTOC
#endif
#define BTOC(x) ((void *)(((unsigned long) x)<<2))

/* Calculate number of elements in an array */
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

#endif /* _HANDLER_H */
