/*
 * fs_file_verify
 *
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
 *
 * This test program creates and reads back files to verify they contain
 * the previously written content.
 */

const char *version = "\0$VER: file_verify 1.0 (08-Feb-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef AMIGA
#define MKDIR(x,y) mkdir(x)
#else
#define MKDIR(x,y) mkdir(x,y)
#endif

#define LOOPS 60

#define BUF_SIZE 8192

#define MAX_FILE_SIZE 512  /* Default maximum file size in KB */
typedef unsigned char uint8_t;
typedef unsigned int  uint32_t;

uint32_t writesize_seed;
uint32_t readsize_seed;
uint32_t deletesize_seed;

char    *path = NULL;
int      dflag = 0;                /* Read follows write by N files */
int      dflag_count = 0;          /* Distance between wriite and read/verify */
int      kflag = 0;                /* Do KB size files */
int      cflag_count = LOOPS;      /* Loops of write and read/verify */
uint32_t mflag_k = MAX_FILE_SIZE;  /* Maximum file size (in KB) */
int      oflag = 0;                /* Overlap between write andd read */
int      oflag_count = 0;          /* Overlap between write and read/verify */
int      vflag = 0;                /* Verify */

uint32_t msize;                    /* Maximum file size (in bytes) */

uint8_t *buf1;
uint8_t *buf2;

/* LFSR random seed values */
static uint32_t rand_seed;

static uint32_t
rand32(void)
{
    rand_seed = (rand_seed * 25173) + 13849;

    return (rand_seed);
}

static void
srand32(uint32_t seed)
{
    rand_seed = seed;
}


static void
print_usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [-kv] [-m <size>]  <path>\n"
            "    -c <count> is the number of files to create\n"
            "    -d <count> is number of files behind verify follows write\n"
            "               -d 1 is default; the -d flag overrides a "
            "previous -o flag\n"
            "    -k         is write files in multiples of kilobytes\n"
            "    -m <size>  is maximum file size in KB\n"
            "    -o <count> is number of files that write and read overlap\n"
            "               The -o flag overrides a previous -d flag\n"
            "    -v         is verbose mode\n", progname);
    exit(1);
}

static void
fill_buf(uint8_t *buf, uint32_t size)
{
    for (; size > 4; size -= 4) {
        *((uint32_t *) buf) = rand32();
        buf += 4;
    }
    for (; size > 0; size--)
        *(buf++) = rand32() & 0xff;
}

static int
file_create(void)
{
    uint32_t size;
    uint32_t file_size;
    uint32_t temp;
    char filename[128];
    FILE *fp;

    srand32(writesize_seed);
    writesize_seed = rand32();
    if (kflag)
        file_size = writesize_seed * 1024;
    else
        file_size = writesize_seed;

    file_size %= msize;
    sprintf(filename, "%s/%d", path, file_size);
    if (vflag)
        printf("write  size=%-8d %s\n", file_size, filename);
    fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("Failed to open %s for write\n", filename);
        return (1);
    }

    srand32(file_size);
    temp = file_size;
    while (temp > 0) {
        size = temp;
        if (size > BUF_SIZE)
            size = BUF_SIZE;
        fill_buf(buf1, size);
        if (fwrite(buf1, size, 1, fp) != 1) {
            printf("Failed to write %s size %u at %u\n",
                   filename, size, file_size - temp);
            fclose(fp);
            return (1);
        }
        temp -= size;
    }
    fclose(fp);
    return (0);
}

static int
file_verify(void)
{
    uint32_t size;
    uint32_t file_size;
    uint32_t temp;
    char filename[128];
    FILE *fp;

    srand32(readsize_seed);
    readsize_seed = rand32();
    if (kflag)
        file_size = readsize_seed * 1024;
    else
        file_size = readsize_seed;

    file_size %= msize;
    sprintf(filename, "%s/%d", path, file_size);
    if (vflag)
        printf("read   size=%-8d %s\n", file_size, filename);
    fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Failed to open %s for read\n", filename);
        return (1);
    }

    srand32(file_size);
    temp = file_size;
    while (temp > 0) {
        size = temp;
        if (size > BUF_SIZE)
            size = BUF_SIZE;
        fill_buf(buf1, size);
        if (fread(buf2, size, 1, fp) != 1) {
            printf("Failed to read %s size %u at %u\n",
                   filename, size, file_size - temp);
            fclose(fp);
            return (1);
        }
        if (memcmp(buf1, buf2, size) != 0) {
            uint8_t *ptr1 = buf1;
            uint8_t *ptr2 = buf2;
            uint32_t pos;
            int      count = 0;

            printf("Miscompare in verify %s %u (size %u)\n",
                   filename, file_size - temp, size);
            for (pos = 0; pos < size; pos++) {
                if (*ptr1 != *ptr2) {
                    if (count < 20)
                        printf(" %x:%02x!=%02x", pos, *ptr2, *ptr1);
                    else if (count == 20)
                        printf(" ...");
                    count++;
                }
                ptr1++;
                ptr2++;
            }
            printf("\n");
            fclose(fp);
            return (1);
        }
        temp -= size;
    }
    fclose(fp);
    return (0);
}

static int
file_delete(void)
{
    uint32_t file_size;
    char filename[128];

    srand32(deletesize_seed);
    deletesize_seed = rand32();
    if (kflag)
        file_size = deletesize_seed * 1024;
    else
        file_size = deletesize_seed;

    file_size %= msize;
    sprintf(filename, "%s/%d", path, file_size);
    if (vflag)
        printf("delete size=%-8d %s\n", file_size, filename);
    return (unlink(filename));
}

int
main(int argc, char *argv[])
{
    int i;
    int arg;
    uint32_t *vptr;
    uint32_t loops;

    buf1 = malloc(BUF_SIZE);
    buf2 = malloc(BUF_SIZE);

    for (arg = 1; arg < argc; arg++) {
        if (*argv[arg] == '-') {
            char *ptr;
            for (ptr = argv[arg] + 1; *ptr != '\0'; ptr++)
                switch (*ptr) {
                    case 'c':
                        vptr = &cflag_count;
                        goto scan_value;
                    case 'd':
                        dflag++;
                        oflag = 0;
                        vptr = &dflag_count;
                        goto scan_value;
                    case 'k':
                        kflag++;
                        break;
                    case 'm':
                        vptr = &mflag_k;
scan_value:
                        if (++arg >= argc) {
                            printf("-%c requires an argument\n", *ptr);
                            exit(1);
                        }
                        if (sscanf(argv[arg], "%d", vptr) != 1) {
                            printf("Invalid argument \"%s\" for -%c\n",
                                   argv[arg], *ptr);
                            exit(1);
                        }
                        break;
                    case 'o':
                        oflag++;
                        dflag = 0;
                        vptr = &oflag_count;
                        goto scan_value;
                    case 'v':
                        vflag++;
                        break;
                    default:
                        fprintf(stderr, "Unknown argument -%s\n", ptr);
                        print_usage(argv[0]);
                }
        } else if (path != NULL) {
            fprintf(stderr,
                    "Path \"%s\" specified -- \"%s\" is unknown\n",
                    path, argv[arg]);
            print_usage(argv[0]);
        } else {
            path = argv[arg];
        }
    }

    if (path == NULL) {
        fprintf(stderr, "%s: Directory to verify must be provided\n",
                argv[0]);
        print_usage(argv[0]);
    }

    msize = mflag_k * 1024;

    MKDIR(path, 0755);

    srand(time(NULL));
    writesize_seed  = rand();
    readsize_seed   = writesize_seed;
    deletesize_seed = writesize_seed;

    loops = cflag_count;

    if (dflag && (dflag_count == 0))
        dflag_count = 1;

    if (oflag) {
        dflag = 1;
        dflag_count = loops - oflag_count;
    }

    if (dflag)
        loops += dflag_count - 1;

    for (i = 0; i < loops; i++) {
        if (i < cflag_count)
            if (file_create())
                break;
        if (i + 1 >= dflag_count)
            if (file_verify())
                exit(1);
        chkabort();  /* Check for break - ^C */
    }

    if (i < loops)
        exit(1);

    for (i = 0; i < cflag_count; i++)
        if (file_delete())
            exit(1);

    if (i < cflag_count)
        exit(1);
    exit(0);
}
