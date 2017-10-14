#include <stdio.h>
#ifndef AMIGA
#include <signal.h>
#endif

#define OPS 300000

#ifdef AMIGA
char *path = "tdev:run";
#else
char *path = "run";
#endif

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
create(void)
{
	int rc = 0;
#ifdef CREATE
	size_t len = (rand() >> 4) & BUFMASK;
	creates++;
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
	write(1, "/", 1);
	pickdir();
	if (mkdir(dirname, 0755) < 0)
		rc = 1;
#endif
	return (0);
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
statit(void)
{
	int rc = 0;
#ifdef STATIT
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

void
break_abort(void)
{
	time(&end);
	printstats();
	exit(0);
}

int
main(int argc, char *argv[])
{
	int index;
/*	srand(1); */
	time(&start);
	srand(start);

	if (argc > 1)
		path = argv[1];
	else {
		fprintf(stderr, "%s: Directory to thrash must be provided\n",
			argv[0]);
		exit(1);
	}

	mkdir(path, 0755);

#ifdef DELETE
	makedir();
#else
	pickdir();
	mkdir(dirname, 0755);
#endif
	for (index = 0; index < BUFMAX; index++)
		wbuffer[index] = rand() & 0xff;

	time(&start);
#ifdef AMIGA
	onbreak(break_abort);
#else
	signal(SIGINT, break_abort);
#endif
	for (index = 0; index < OPS - 1; index++)
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
terminate:
	time(&end);
	printstats();
	exit(0);
}
