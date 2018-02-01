/*
 * fs_thrash
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
 * This test program creates, appends, seeks, reads, and deletes many
 * files in the filesystem by writing a random amount at a different
 * file offset each time.
 */

const char *version = "\0$VER: fs_thrash 1.0 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#ifdef AMIGA
#define MKDIR(x,y) mkdir(x)
#else
#define MKDIR(x,y) mkdir(x,y)
#include <signal.h>
#endif

#define OPS 3000000

char *path = NULL;

#define	CREATE
#define	READIT
#define	APPEND
#define	MODIFY
#define	DELETE
#define	NEWDIR
#define	STATIT


/* max files is 128 */
#define NAMEMASK 127

/* max dirs is 32 */
#define DIRMASK 31

/* max file size is near 512k */
#define FSIZEMASK 0x7ffff

#define BUFMASK 8191
#define BUFMAX  8192
unsigned char rbuffer[BUFMAX];
unsigned char wbuffer[BUFMAX];

char filename[64];
char dirname[64];

/* #define PRINTCLOCK(x,y) printdiv(x,y) */
#define PRINTCLOCK(x,y) printf("%d", x)

int creates = 0;
int modifies = 0;
int appends = 0;
int deletes = 0;
int newdirs = 0;
int stats = 0;
int reads = 0;
int verbose = 0;
unsigned long start;
unsigned long end;

FILE *fp = NULL;

void
pickname(void)
{
	sprintf(filename, "%s/%d", dirname, (rand() >> 8)  & NAMEMASK);
}

void
pickdir(void)
{
	sprintf(dirname, "%s/%d", path, (rand() >> 8) & DIRMASK);
}

void
open_it(char *type)
{
	pickname();
	fp = fopen(filename, type);
}

int
close_it(void)
{
	int rc = 0;
	if (fp == NULL)
		return (0);
	if (fclose(fp))
		rc = 0;  /* Not sure why, but I get close errors */
	fp = NULL;
	return (rc);
}

int
create(void)
{
	int rc = 0;
#ifdef CREATE
	size_t len = (rand() >> 4) & BUFMASK;
	creates++;
	if (verbose)
	    write(1, "c", 1);
	open_it("w");
	if (fp == NULL) {
		printf("\nCould not open %s for create\n", filename);
		return (1);
	}
	if (fwrite(wbuffer, len, 1, fp) < 0) {
		printf("\nWrite %s len %d failed\n", filename, len);
		rc = 1;
	} else if (fflush(fp) < 0) {
		printf("\nfflush %s len %d failed\n", filename, len);
		rc = 1;
	}
	if (close_it()) {
		printf("\nClose %s failed after write %d\n", filename, len);
		rc = 1;
	}
#endif
	return (rc);
}

int
modify(void)
{
	int rc = 0;
#ifdef MODIFY
	int    pos = (rand() >> 4) & FSIZEMASK;
	size_t len = (rand() >> 6) & BUFMASK;
	modifies++;
	if (verbose)
	    write(1, "m", 1);
	open_it("r+");
	if (fp == NULL)
		return (0);
	if (fseek(fp, pos, 0) >= 0) {
	    if (fwrite(wbuffer, len, 1, fp) < 0) {
		printf("\nWrite %s %d at %d failed\n", filename, len, pos);
		rc = 1;
	    } else if (fflush(fp) < 0) {
		printf("\nfflush %s len %d failed\n", filename, len);
		rc = 1;
	    }
	}
	if (close_it()) {
		printf("\nClose %s failed after write %d at %d\n",
			filename, len, pos);
		rc = 1;
	}
#endif
	return (rc);
}

int
append(void)
{
	int rc = 0;
#ifdef APPEND
	int len = (rand() >> 3) & BUFMASK;
	appends++;
	if (verbose)
	    write(1, "a", 1);
	open_it("a");
	if (fp == NULL) {
		printf("\nCould not open %s for append\n", filename);
		return;
	}
	if (fwrite(wbuffer, len, 1, fp) < 0) {
		printf("\nfwrite %s len %d failed\n", filename, len);
		rc = 1;
	} else if (fflush(fp) < 0) {
		printf("\nfflush %s len %d failed\n", filename, len);
		rc = 1;
	}
	if (close_it()) {
		printf("\nClose %s failed after write %d\n", filename, len);
		rc = 1;
	}
#endif
	return (rc);
}

int
delete(void)
{
	int rc = 0;
#ifdef DELETE
	deletes++;
	if (verbose)
	    write(1, "d", 1);
	pickname();
	(void) unlink(filename);
#endif
	return (rc);
}

int
makedir(void)
{
	int rc = 0;
#ifdef NEWDIR
	newdirs++;
	if (verbose)
	    write(1, "/", 1);
	pickdir();
	if (MKDIR(dirname, 0755) < 0)
		rc = 1;
#endif
	return (0);
}

int
statit(void)
{
	int rc = 0;
#ifdef STATIT
	if (verbose)
	    write(1, "s", 1);
	stats++;
	open_it("r");
	if (fp == NULL)
		return (0);
	if (close_it()) {
		printf("\nClose %s failed\n", filename);
		rc = 1;
	}
#endif
	return (rc);
}

int
readit(void)
{
	int rc = 0;
#ifdef READIT
	long   pos = (rand() >> 4) & FSIZEMASK;
	size_t len = (rand() >> 6) & BUFMASK;
	if (verbose)
	    write(1, "r", 1);
	reads++;
	open_it("r");
	if (fp == NULL)
		return (0);
	if ((fseek(fp, pos, 0) >= 0) &&
	    (fread(rbuffer, len, 1, fp) < 0)) {
		printf("\nRead %s %d at %d failed\n", filename, len, pos);
		rc = 1;
	}
	if (close_it()) {
		printf("\nClose %s failed after read %d at %d\n",
			filename, len, pos);
		rc = 1;
	}
#endif
	return (rc);
}

void
printdiv(int num, int den)
{
	int whole;
	int rem;

	whole = num / den;
	rem = num - whole * den;
	printf("%d.%02d", whole, 100 * rem / num);
}

void
printstats(void)
{
	int clock;
	int total = 0;

	clock = end - start;

	printf("\n");

#ifdef CREATE
	printf("create=");
	PRINTCLOCK(creates, clock);
	printf(" ");
	total += creates;
#endif
#ifdef MODIFY
	printf("modify=");
	PRINTCLOCK(modifies, clock);
	printf(" ");
	total += modifies;
#endif
#ifdef APPEND
	printf("append=");
	PRINTCLOCK(appends, clock);
	printf(" ");
	total += appends;
#endif
#ifdef DELETE
	printf("delete=");
	PRINTCLOCK(deletes, clock);
	printf(" ");
	total += deletes;
#endif
#ifdef NEWDIR
	printf("newdir=");
	PRINTCLOCK(newdirs, clock);
	printf(" ");
	total += newdirs;
#endif
#ifdef STATIT
	printf("stat=");
	PRINTCLOCK(stats, clock);
	printf(" ");
	total += stats;
#endif
#ifdef READIT
	printf("read=");
	PRINTCLOCK(reads, clock);
	printf(" ");
	total += reads;
#endif
	printf("\ntime=%d  total=%d ops [", clock, total);
	printdiv(total, clock);
	printf(" ops/sec]\n");
}

int
break_abort(void)
{
	time(&end);
	printstats();
	exit(0);
}

static void
usage(const char *progname)
{
    fprintf(stderr,
	    "Usage: %s [-v] <path>\n"
	    "    -v is verbose mode\n", progname);
    exit(1);
}

int
main(int argc, char *argv[])
{
	int arg;
	int index;
/*	srand(1); */
	time(&start);
	srand(start);

	for (arg = 1; arg < argc; arg++) {
	    if (*argv[arg] == '-') {
		char *ptr;
		for (ptr = argv[arg] + 1; *ptr != '\0'; ptr++)
		    if (*ptr == 'v') {
			verbose = 1;
		    } else {
			fprintf(stderr, "Unknown argument -%s\n", ptr);
			usage(argv[0]);
		    }
	    } else if (path != NULL) {
		fprintf(stderr,
			"Path \"%s\" specified -- \"%s\" is unknown\n",
			path, argv[arg]);
		usage(argv[0]);
	    } else {
		path = argv[arg];
	    }
	}
	if (path == NULL) {
	    fprintf(stderr, "%s: Directory to thrash must be provided\n",
		    argv[0]);
	    usage(argv[0]);
	}

	MKDIR(path, 0755);

#ifdef DELETE
	makedir();
#else
	pickdir();
	MKDIR(dirname, 0755);
#endif
	for (index = 0; index < BUFMAX; index++)
		wbuffer[index] = rand() & 0xff;

	time(&start);
#ifdef AMIGA
	onbreak(break_abort);
#else
	signal(SIGINT, break_abort);
#endif
	for (index = 0; index < OPS - 1; index++) {
		switch ((rand() >> 5) & 15) {
			case 0:
				if (create()) {
failure:
					printf("Failed: file=%s dir=%s",
						filename, dirname);
					index = OPS;
				}
				break;
			case 1:
			case 2:
				if (modify())
					goto failure;
				break;
			case 3:
			case 4:
			case 5:
				if (append())
					goto failure;
				break;
			case 6:
			case 7:
				if (delete())
					goto failure;
				break;
			case 8:
				if (makedir())
					goto failure;
				break;
			case 9:
			case 10:
			case 11:
			case 12:
				if (statit())
					goto failure;
				break;
			case 13:
			case 14:
			case 15:
				if (readit())
					goto failure;
				break;
		}
		chkabort();
	}
terminate:
	time(&end);
	printstats();
	exit(0);
}
