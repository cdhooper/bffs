/* ln
 *      This program is copyright (1994-2018) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: ln 1.2 (08-Feb-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dos.h>
#include <clib/dos_protos.h>

typedef unsigned long ulong;
#include "amiga_unix.h"

#define HARD 0
#define SOFT 1

static void
print_usage(char *progname)
{
    fprintf(stderr, "%s\n"
            "Usage:  %s [-s] <target> <newname> [<newname>...]\n",
            version + 7, progname);
    exit(1);
}

int
main(int argc, char *argv[])
{
    int index = 1;
    int type = HARD;
    char buf[68];
    char *src;

    if (argc < 3)
        print_usage(argv[0]);

    if (!strcmp(argv[index], "-s")) {
        type = SOFT;
        index++;
    }

    src = argv[index++];

    if (index >= argc)
        print_usage(argv[0]);

    for (; index < argc; index++)
        if (PMakeLink(src, argv[index], type)) {
            long err = IoErr();
            if (err == 0)
                strcpy(buf, "unknown reason");
            else
                Fault(err, "", buf, 67);
            fprintf(stderr, "Unable to link %s to %s - %d %s\n",
                    argv[index], src, err, buf);
        }

    exit(0);
}
