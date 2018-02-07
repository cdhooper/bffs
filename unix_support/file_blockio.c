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
#include <stdlib.h>
#include <fcntl.h>
#include "sys/types.h"

#undef DEBUG

extern int ifd;
extern int ofd;
extern int DEV_BSIZE;

/*
 * read a block from the file system
 */
#ifdef AMIGA
int
fbread(char *bf, daddr_t bno, int size)
#else
bread(char *bf, daddr_t bno, int size)
#endif
{
    int n;

#ifdef DEBUG
    printf("bread %d, buf=%x bno=%d size=%d ss=%d\n",
           ifd, bf, bno, size, DEV_BSIZE);
#endif
    if (lseek(ifd, (off_t)bno * DEV_BSIZE, 0) < 0) {
        printf("seek error: %ld\n", bno);
        perror("bread");
        exit(33);
    }
    n = read(ifd, bf, size);
    if (n != size) {
        printf("read error: %ld\n", bno);
        perror("bread");
        exit(34);
    }

    return (0);
}


/*
 * write a block to the file system
 */
#ifdef AMIGA
int
fbwrite(char *bf, daddr_t bno, int size)
#else
bwrite(char *bf, daddr_t bno, int size)
#endif
{
    int n;

    if (ofd < 0)
        return (0);

#ifdef DEBUG
    printf("bwrite %d, buf=%x bno=%d size=%d ss=%d\n",
           ofd, bf, bno, size, DEV_BSIZE);
#endif

    if (lseek(ofd, (off_t)bno * DEV_BSIZE, 0) < 0) {
        printf("seek error: %ld\n", bno);
        perror("bwrite");
        exit(35);
    }
    n = write(ofd, bf, size);
    if (n != size) {
        printf("write error: %ld\n", bno);
        perror("bwrite");
        exit(36);
    }

    return (0);
}
