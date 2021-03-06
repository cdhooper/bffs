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
#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#define BBSIZE 8192
#define SBSIZE 8192

#define ulong unsigned long

struct node {
    ulong value;
    struct node *next;
} *factorlist;

static void pick_sec(ulong size, ulong *rtrk, ulong *rsec, ulong *rcyl);
static ulong getvalue(ulong rangelow, ulong rangehigh);
static ulong getrest(void);
static int isqrt(ulong value);
static void insert(ulong value);

extern int DEV_BSIZE;

extern char *special;
extern char *progname;

int
file_label(struct disklabel *label)
{
    int index;
    unsigned long sectors;
    struct stat statbuf;

    printf("Autosizing file label:  ");
    if (stat(special, &statbuf)) {
        fprintf(stderr, "stat failed for %s\n", special);
        return (1);
    }

    sectors = statbuf.st_size / DEV_BSIZE;
    printf("[count=%d] ", sectors);

    label->d_magic          = DISKMAGIC;
    label->d_type           = DTYPE_SCSI;
    strcpy(label->d_typename, "SCSI");
    label->d_subtype        = 0;
    label->d_packname[0]    = 0;
    label->d_secsize        = DEV_BSIZE;
    pick_sec(sectors, &label->d_ntracks,
             &label->d_nsectors, &label->d_ncylinders);
    label->d_secpercyl      = label->d_nsectors * label->d_ntracks;
    label->d_secperunit     = label->d_secpercyl * label->d_ncylinders;
    label->d_sparespertrack = 0;
    label->d_sparespercyl   = 0;
    label->d_acylinders     = 0;
    label->d_rpm            = 3600;
    label->d_interleave     = 1;

    label->d_trackskew      = 0;
    label->d_cylskew        = 0;
    label->d_headswitch     = 0;
    label->d_trkseek        = 0;
    label->d_flags          = 0;
    for (index = 0; index < NDDATA; index++)
        label->d_drivedata[index] = 0;
    for (index = 0; index < NSPARE; index++)
        label->d_spare[index] = 0;
    label->d_magic2         = DISKMAGIC;
    label->d_checksum       = 0;

    label->d_npartitions    = 1;
    label->d_bbsize         = BBSIZE;
    label->d_sbsize         = SBSIZE;

    for (index = 0; index < MAXPARTITIONS; index++) {
        label->d_partitions[index].p_size   = 0;
        label->d_partitions[index].p_offset = 0;
        label->d_partitions[index].p_fsize  = 0;
        label->d_partitions[index].p_fstype = 0;
        label->d_partitions[index].p_frag   = 0;
        label->d_partitions[index].p_cpg    = 0;
    }

    label->d_partitions[0].p_size = label->d_secperunit;

    return (0);
}


static void
pick_sec(ulong size, ulong *rtrk, ulong *rsec, ulong *rcyl)
{
    ulong index;
    ulong stop;
    ulong trk = 1;
    ulong sec = 1;
    ulong cyl = 1;
    ulong temp = 1;

    factorlist = NULL;
    stop = isqrt(size);

    for (index = 2; index <= stop; index++) {
top:
        if (((size / index) * index) == size) {
            insert(index);
            size /= index;
            if (size)
                goto top;
        }
        if (index > size)
            break;
    }

    if (size > 1)
        insert(size);

    /* now that we have the factors, divvy them out */
    sec = getvalue(2, 99);
    trk = getvalue(2, 15);
    cyl = getvalue(8, 64);
    if (trk == 1)
        trk *= getvalue(2, 64);

    temp = getvalue(2, 15);
    if (trk * temp < 16) {
        trk *= temp;
        temp = getvalue(2, 15);
        if (sec * temp < 99) {
            sec *= temp;
            if (trk * sec < stop) {
                temp = getvalue(2, 4);
                if (trk * temp < 16) {
                    trk *= temp;
                    temp = getvalue(2, 8);
                    sec *= temp;
                } else {
                    sec *= temp;
                }
            }
        } else {
            cyl *= temp;
        }
    } else if (sec * temp < 99) {
        sec *= temp;
    } else {
        cyl *= temp;
    }

    cyl *= getrest();

    printf("cyl=%d trk=%d sec=%d\n", cyl, trk, sec);

    *rtrk = trk;
    *rsec = sec;
    *rcyl = cyl;
}

static int
isqrt(ulong value)
{
    ulong index;
    int cur;
    int spin;

    index = value >> 1;
    cur = index;

    for (spin = 0; spin < 5; spin++) {
        while ((index * index > value) && cur) {
            cur >>= 1;
            index -= cur;
        }

        while ((index * index < value) && cur) {
            cur >>= 1;
            index += cur;
        }
    }

    return (index);
}

static void
insert(ulong value)
{
    struct node *ptr;
    ptr = (struct node *) malloc(sizeof (struct node));

    if (ptr == NULL) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    ptr->value = value;
    ptr->next = factorlist;
    factorlist = ptr;
}

static ulong
getvalue(ulong rangelow, ulong rangehigh)
{
    ulong value = 1;
    struct node *parent;
    struct node *current;

    current = factorlist;
    parent = NULL;

    while ((current != NULL) && (current->value > rangehigh)) {
        parent = current;
        current = current->next;
    }

    if ((current != NULL) && (current->value >= rangelow)) {
        value = current->value;
        if (parent == NULL)
            factorlist = current->next;
        else
            parent->next = current->next;

        free(current);
    }

    return (value);
}

static ulong
getrest(void)
{
    ulong value = 1;
    struct node *temp;
    struct node *current;

    current = factorlist;

    while (current != NULL) {
        temp = current;
        value *= current->value;
        current = current->next;
        free(temp);
    }

    factorlist = NULL;
    return (value);
}
