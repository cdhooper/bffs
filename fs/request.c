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
#include <exec/memory.h>
#include <clib/intuition_protos.h>
#include <intuition/intuition.h>

#include "config.h"
#include "request.h"

typedef struct IntuiText IntuiText_t;

#if 0
IntuiText_t youmust = {1,2,3,  3, 7,NULL, "You must replace volume",  NULL};
IntuiText_t indrive = {1,2,3,  3,23,NULL, "in trackdisk,0          ", NULL};
IntuiText_t dircorr = {1,2,3,  3, 7,NULL, "Run fsck!  Directory",     NULL};
IntuiText_t fscorru = {1,2,3,  3, 7,NULL, "Run fsck!  Filesystem",    NULL};
IntuiText_t corrcon = {1,2,3,  3,23,NULL, "corrupt. Continue anyway?",NULL};
#endif

IntuiText_t blkmess = {1,2,3,  3, 7,NULL, "Volume BF0, Block       ", NULL};
IntuiText_t rderror = {1,2,3,  3,23,NULL, "has a Read error",         NULL};
IntuiText_t wrerror = {1,2,3,  3,23,NULL, "has a Write error",        NULL};

IntuiText_t bffsbug = {1,2,3,  3, 7,NULL, "BFFS Internal error on",   NULL};
IntuiText_t contath = {1,2,3,  3,23,NULL, "Author code [          ]", NULL};

IntuiText_t txtname = {1,2,3,  3,15,NULL, "                        ", NULL};

IntuiText_t postext = {1,2,3, 10,64,NULL, "Retry",                    NULL};
IntuiText_t negtext = {1,2,3,150,64,NULL, "Cancel",                   NULL};
IntuiText_t okytext = {1,2,3,150,64,NULL, "Okay",                     NULL};

#define inittext(text, str, pos)                                \
        text.FrontPen = 1;              text.BackPen = 2;       \
        text.DrawMode = 3;              text.LeftEdge = 3;      \
        text.TopEdge = 7 + 8 * pos;     text.ITextFont = NULL;  \
        text.IText = str;               text.NextText = NULL;

extern char *handler_name;

BOOL
do_request(int type, ULONG data1, ULONG data2, char *str)
{
    PRINT2(("autorequest %d\n", type));

    switch (type) {
        case REQUEST_BFFS_INTERNAL:
            bffsbug.NextText = &txtname;
            txtname.NextText = &contath;
            return (AutoRequest(NULL, &bffsbug, NULL,
                    &okytext, 0, 0, 180, 80));
        case REQUEST_YOU_MUST:
            bffsbug.NextText = &txtname;
            txtname.NextText = &contath;
            return (AutoRequest(NULL, &bffsbug, NULL,
                    &okytext, 0, 0, 180, 80));
            break;
        case REQUEST_BLOCK_BAD_R:
            blkmess.NextText = &txtname;
            txtname.NextText = &rderror;
            sprintf(blkmess.IText + 7, "%s, Block", handler_name);
            sprintf(txtname.IText, "%d, Size %d", data1, data2);
            return (AutoRequest(NULL, &blkmess, &postext,
                    &negtext, 0, 0, 180, 80));
            break;
        case REQUEST_BLOCK_BAD_W:
            blkmess.NextText = &txtname;
            txtname.NextText = &wrerror;
            sprintf(blkmess.IText + 7, "%s, Block", handler_name);
            sprintf(txtname.IText, "%d, Size %d", data1, data2);
            return (AutoRequest(NULL, &blkmess, &postext,
                    &negtext, 0, 0, 180, 80));
            break;
        case REQUEST_RUN_FSCK_D:
            bffsbug.NextText = &txtname;
            txtname.NextText = &contath;
            return (AutoRequest(NULL, &bffsbug, NULL,
                    &okytext, 0, 0, 180, 80));
            break;
        case REQUEST_RUN_FSCK_F:
            bffsbug.NextText = &txtname;
            txtname.NextText = &contath;
            return (AutoRequest(NULL, &bffsbug, NULL,
                    &okytext, 0, 0, 180, 80));
            break;
    }
}
