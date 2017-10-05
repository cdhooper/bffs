/* etouch.c version 1.0
 *      This program is copyright (1996) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

#include <stdio.h>
#include <dos/dos.h>

#include <devices/timer.h>

int cyear = 96;
ULONG ctime = 0;
char *version = "\0$VER: etouch 1.1 (7.Apr.96) © 1996 Chris Hooper";


int mdays[12] = {
	31, 28, 31, 30, 31, 30,
	31, 31, 30, 31, 30, 31
};

int smdays[12] = {
	  0,  31,  59,  90, 120, 151,
	181, 212, 243, 273, 304, 334
};

#define ACCESS_INVISIBLE 0

int main(argc, argv)
int  argc;
char *argv[];
{
	int index;
	struct Lock *lock;
	struct timeval tval[3];
	ULONG stime;
	char *name;
	int newfile;
	int mflag = 0;
	int aflag = 0;
	int cflag = 0;
	int pflag = 0;

	if (argc < 2)
		print_usage(argv[0]);

	getdate();
	stime = ctime;

	for (index = 1; index < argc; index++) {
		name = argv[index];
		if (*name == '-') {
			for (name++; *name != '\0'; name++) {
			    if (*name == 'a')
				aflag = !aflag;
			    else if (*name == 'm')
				mflag = !mflag;
			    else if (*name == 'c')
				cflag = !cflag;
			    else if (*name == 'p')
				pflag = !pflag;
			    else {
				fprintf(stderr, "Unknown parameter -%s\n", name);
				exit(1);
			    }
			}
			continue;
		}
		if (isdigit(*name)) {
			stime = datec(name);
/*			printf("ntime=%d\n", stime); */
			continue;
		}
		newfile = 0;
		lock = (struct Lock *) Lock(name, ACCESS_INVISIBLE);
		if (lock == NULL) {
		    lock = (struct Lock *) Open(name, ACCESS_READ);
		    if (lock == NULL) {
			lock = (struct Lock *) Open(name, MODE_NEWFILE);
			if (lock == NULL) {
				fprintf(stderr, "Error creating %s\n", name);
				continue;
			}
			newfile = 1;
		    }
		    Close(lock);
		} else
		    UnLock(lock);


		GetTimes(name, &tval[0], &tval[1], &tval[2]);

		if (pflag)
			printf("Get access=%d.%d modify=%d.%d create=%d.%d\n",
				tval[0].tv_secs, tval[0].tv_micro,
				tval[1].tv_secs, tval[1].tv_micro,
				tval[2].tv_secs, tval[2].tv_micro);

		if (aflag | (!mflag && !cflag)) {	/* set mtime */
			tval[0].tv_secs = stime;
			tval[0].tv_micro = 0;
		}
		if (mflag | (!aflag && !cflag)) {	/* set mtime */
			tval[1].tv_secs = stime;
			tval[1].tv_micro = 0;
		}
		if (cflag) {				/* set ctime */
			tval[2].tv_secs = stime;
			tval[2].tv_micro = 0;
		}

		if (pflag && (aflag | mflag | cflag))
			printf("Set access=%d.%d modify=%d.%d create=%d.%d\n",
				tval[0].tv_secs, tval[0].tv_micro,
				tval[1].tv_secs, tval[1].tv_micro,
				tval[2].tv_secs, tval[2].tv_micro);
		SetTimes(name, &tval[0], &tval[1], &tval[2]);
	}

	exit(0);
}

print_usage(progname)
char *progname;
{
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "usage:  %s [-amcp] [mmddhhmm[yy]] file [...]\n", progname);
	fprintf(stderr, " where   -a is set only access time\n");
	fprintf(stderr, " where   -m is set only modify time\n");
	fprintf(stderr, " where   -c is set only inode change time\n");
	fprintf(stderr, " where   -p print current times (do not modify)\n");
	exit(1);
}

datec(str)
char *str;
{
	char buf[10];
	int len;
	int month;
	int day;
	int hour;
	int minute;
	int year;
	int leap;
	ULONG value;

	len = strlen(str);
	if ((len != 8) && (len != 10)) {
		fprintf(stderr, "Invalid date format %s\n", str);
		return(0);
	}
	sprintf(buf, "%.2s", str     );  month   = atoi(buf);
	sprintf(buf, "%.2s", str +  2);  day     = atoi(buf);
	sprintf(buf, "%.2s", str +  4);  hour    = atoi(buf);
	sprintf(buf, "%.2s", str +  6);  minute  = atoi(buf);
	if (len == 10) {
		sprintf(buf, "%.2s", str +  8);
		year = atoi(buf);
	} else
		year = cyear;

	if (((year > 24) && (year < 70)) || (year < 0)) {
		fprintf(stderr, "Invalid year %d\n", year);
		return(1);
	}

	if (((year % 4) == 0) && ((year % 100) > 0))
		leap = 1;
	else
		leap = 0;

	if ((month > 12) || (month < 1)) {
		fprintf(stderr, "Invalid month %.2s\n", str);
		return(1);
	}

	if ((day > mdays[month]) || (day < 1)) {
		if ((month != 2) || (day != 29) || (!leap)) {
			fprintf(stderr, "Invalid day %.2s\n", str + 2);
			return(1);
		}
	}

	if ((hour > 23) || (hour < 0)) {
		fprintf(stderr, "Invalid hour %.2s\n", str + 4);
		return(1);
	}

	if ((minute > 59) || (minute < 0)) {
		fprintf(stderr, "Invalid minute %.2s\n", str + 6);
		return(1);
	}

	if (year >= 70)
		year -= 70;

	/* 24 * 60 * 60 = 86400 */
	value = ((int) (year * 365.25)) * 86400;	 /* year */
	value += (smdays[month - 1] + day - 1 + ((leap && (month > 2)) ? 1 : 0)) * 86400;
							/* day */
	value += (hour * 60 + minute) * 60;		/* time */

	return(value);
}

getdate()
{
	time(&ctime);
	ctime += 2922 * 86400;	/* Amiga - UNIX time difference */
	cyear = ctime / 86400 / 365.25 + 70;
	if (cyear > 99)
		cyear -= 100;
}
