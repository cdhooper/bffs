/*
 * This program test creates many sparse files in the filesystem
 * by writing a random amount at the start, then seeking a random
 * offset and writing another random amount.
 *
 * Note that this program will fail on current BFFS and Amiga FFS
 * as well.  Neither of these filesystems support sparse files.
 * BFFS could be enhanced to support them in the future.
 */
#include <stdio.h>
#include <fcntl.h>
#ifndef AMIGA
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#endif

#define OPS 30

#ifdef AMIGA
char *path = "tdev:run";
#else
char *path = "run";
#endif

#define	DELETE

#define NAMEMASK 127

/* max file size is near 512k */
#define FSIZEMASK 0x7ffff

#define BUFMASK 8191
#define BUFMAX  8192
unsigned char wbuffer[BUFMAX];

char filename[64];
char dirname[64];

/* #define PRINTCLOCK(x,y) printdiv(x,y) */
#define PRINTCLOCK(x,y) printf("%d", x)

int creates = 0;
int deletes = 0;
int verbose = 1;
unsigned long start;
unsigned long end;

void
pickname(void)
{
	sprintf(filename, "%s/%d", path, (rand() >> 8) & NAMEMASK);
}

int
createfile(void)
{
	int fd;
	int rc = 0;
	size_t start_len = (rand() >> 4) & BUFMASK;
	size_t tail_len  = (rand() >> 4) & BUFMASK;
	long   tail_pos  = (rand() >> 4) & FSIZEMASK;
	creates++;
	pickname();

	if (!verbose)
		write(1, "c", 1);
	printf("open(%s)", filename);
	fd = open(filename, O_CREAT | O_RDWR);
	if (fd < 0) {
		printf("\nCould not open %s for create\n", filename);
		return (1);
	}

	printf("write(%d)", start_len);
	if (write(fd, wbuffer, start_len) < 0) {
		printf("\nWrite %s len %d at 0 failed\n", filename, start_len);
		rc = 1;
		goto failure;
	}
	printf("seek(%u)", tail_pos);
	if (lseek(fd, tail_pos, SEEK_SET) < 0) {
		printf("\nlseek %d failed\n", filename, tail_pos);
		rc = 1;
		goto failure;
	}
	printf("write(%u)", tail_len);
	if (write(fd, wbuffer, tail_len) < 0) {
		printf("\nWrite %s len %d at %d failed\n",
			filename, tail_len, tail_pos);
		rc = 1;
	}
failure:
	if (close(fd) < 0) {
		printf("\nClose %s failed after write %d\n",
			filename, tail_len);
		rc = 1;
	}
	return (rc);
}

int
delete(void)
{
	int rc = 0;
#ifdef DELETE
	deletes++;
	write(1, "d", 1);
	pickname();
	(void) unlink(filename);
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

	printf("create=");
	PRINTCLOCK(creates, clock);
	printf(" ");
	total += creates;

#ifdef DELETE
	printf("delete=");
	PRINTCLOCK(deletes, clock);
	printf(" ");
	total += deletes;
#endif
	printf("\ntime=%d  total=%d ops [", clock, total);
	printdiv(total, clock);
	printf(" ops/sec]\n");
}

void
#ifdef AMIGA
break_abort(void)
#else
break_abort(int sig)
#endif
{
	time(&end);
	printstats();
	exit(0);
}

int
main(int argc, char *argv[])
{
	int index;
	time(&start);
#if 0
	srand(start);
#endif

	if (argc > 1)
		path = argv[1];
	else {
		fprintf(stderr, "%s: Must provide directory for sparse files\n",
			argv[0]);
		exit(1);
	}

	mkdir(path, 0755);

	for (index = 0; index < BUFMAX; index++)
		wbuffer[index] = rand() & 0xff;

	time(&start);
#ifdef AMIGA
	onbreak(break_abort);
#else
	signal(SIGINT, break_abort);
#endif
	for (index = 0; index < OPS - 1; index++) {
		if (createfile()) {
			printf("Failed: file=%s", filename);
			index = OPS;
			break;
		}
	}

	time(&end);
	printstats();
	exit(0);
}
