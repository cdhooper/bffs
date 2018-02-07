/* touch
 *      This program is Copyright 2018 Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit
 *      without prior written approval of the author.
 */

const char *version = "\0$VER: touch 1.2 (08-Feb-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dos/dos.h>
#include <clib/dos_protos.h>
#include <devices/timer.h>
#include <sys/time.h>
typedef unsigned long ulong;
#include <amiga_unix.h>

#undef DEBUG_DATE

int cyear;  /* assigned in getdate() */
ULONG ctime_val = 0;

const int mdays[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

const int smdays[12] = {
      0,  31,  59,  90, 120, 151,
    181, 212, 243, 273, 304, 334
};

#define ACCESS_INVISIBLE 0  /* BFFS-specific; maps to SHARED_LOCK in FFS */

static void
print_usage(const char *progname)
{
    fprintf(stderr,
            "%s\n"
            "usage:  %s [-acmp] [[cc]yy][mmddhhmm[.ss]] file [...]\n"
            "\t\t-a is set only access time\n"
            "\t\t-c is set only inode change time\n"
            "\t\t-C is just create file\n"
            "\t\t-m is set only modify time\n"
            "\t\t-p print current times (do not modify)\n",
            version + 7, progname);
    exit(1);
}

static ULONG
datec(const char *str)
{
    int len;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int year;
    int leap;
    ULONG value;
    char *dot;
    int pos = 0;
    int pre_len = 0;
    int count;
    const char *substr = str;

    len = strlen(str);
#if 0
    if ((len <= 8) || (len > 12)) {
        fprintf(stderr, "Invalid date format %s\n", str);
        return (0);
    }
#endif
    dot = strchr(str, '.');
    if (dot != NULL)
        len = dot - str;

    pos = 0;
    if (len > 10) {
        count = sscanf(substr, "%4d%n", &year, &pos);
        if (pos != 4)
            goto invalid_time;
        pre_len  = pos;
        substr  += pre_len;
        len     -= pre_len;
    } else if (len > 8) {
        count = sscanf(substr, "%2d%n", &year, &pos);
        if (pos != 2)
            goto invalid_time;
        pre_len  = pos;
        substr  += pre_len;
        len     -= pre_len;
        if (year >= 70)
            year += 1900;
        else
            year += 2000;
    } else {
        year = cyear;
    }

    pos = 0;
    count = sscanf(substr, "%2d%n%2d%n%2d%n%2d%n.%n%2d%n", &month, &pos, &day,
                   &pos, &hour, &pos, &minute, &pos, &pos, &second, &pos);
    if ((count < 4) || substr[pos] != '\0') {
invalid_time:
        printf("Invalid time string: %s\n%*s^\n",
               str, pos + substr - str + 21, "");
        exit(1);
    }
    if (len < 8) {
        pos = len + pre_len;
        substr = str;
        goto invalid_time;
    }
    if (count < 4)
        second = 0;

#ifdef DEBUG_DATE
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           year, month, day, hour, minute, second);
#endif
    if ((year < 1970) || (year > 2050)) {
        fprintf(stderr, "Invalid year %04d\n", year);
        exit(1);
    }

    if (((year % 4) == 0) && ((year % 100) > 0))
        leap = 1;
    else
        leap = 0;

    if ((month > 12) || (month < 1)) {
        fprintf(stderr, "Invalid month %02d\n", month);
        exit(1);
    }

    if ((day > mdays[month - 1]) || (day < 1)) {
        if ((month != 2) || (day != 29) || (!leap)) {
            fprintf(stderr, "Invalid day %02d\n", day);
            exit(1);
        }
    }

    if ((hour > 23) || (hour < 0)) {
        fprintf(stderr, "Invalid hour %02d\n", hour);
        exit(1);
    }

    if ((minute > 59) || (minute < 0)) {
        fprintf(stderr, "Invalid minute %02d\n", minute);
        exit(1);
    }


    /* 24 * 60 * 60 = 86400 */
    year -= 1970;
    value = ((int) (year * 365.25)) * 86400;     /* year */
    value += (smdays[month - 1] + day - 1 +      /* day */
             ((leap && (month > 2)) ? 1 : 0)) * 86400;
    value += (hour * 60 + minute) * 60 + second; /* hours + minutes + seconds */

    return (value);
}

/*
 * amiga_ds_to_unix_time()
 *      Convert Amiga DateStamp to UNIX time format (UTC seconds since 1970).
 */
ULONG
amiga_ds_to_unix_time(struct DateStamp *ds)
{
    ULONG timeval;

    timeval  = 2922 * 86400               /* Adjust 1970 -> 1978 */
               + ds->ds_Days * 24 * 3600  /* Days */
               + ds->ds_Minute * 60       /* Minutes */
               + ds->ds_Tick / 50;        /* Seconds */

    return (timeval);
}

/* unix_time()
 *      Acquire current system time in UNIX format (UTC seconds since 1970).
 */
static ULONG
unix_time(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (amiga_ds_to_unix_time(&ds));
}

static void
getdate(void)
{
    ctime_val = unix_time();
    cyear = ctime_val / 86400 / 365.25 + 70;
    if (cyear > 99)
        cyear -= 100;
}

int
main(int argc, char *argv[])
{
    int index;
    BPTR lock;
    struct timeval tval[3];
    ULONG stime;
    char *name;
    int newfile;
    int mflag = 0;
    int aflag = 0;
    int cflag = 0;
    int Cflag = 0;
    int pflag = 0;
    int set_all = 0;
    int rc = 0;

    if (argc < 2)
            print_usage(argv[0]);

    getdate();
    stime = ctime_val;

    for (index = 1; index < argc; index++) {
        name = argv[index];
        if (*name == '-') {
            for (name++; *name != '\0'; name++) {
                if (*name == 'a')
                    aflag = !aflag;
                else if (*name == 'c')
                    cflag = !cflag;
                else if (*name == 'C')
                    Cflag = !Cflag;
                else if (*name == 'm')
                    mflag = !mflag;
                else if (*name == 'p')
                    pflag = !pflag;
                else {
                    fprintf(stderr, "Unknown parameter -%s\n", name);
                    exit(1);
                }
            }
            continue;
        }
        if (!aflag && !mflag && !cflag && !pflag)
            set_all = 1;
        else
            set_all = 0;

        if (isdigit(name[0]) && isdigit(name[1])) {
            stime = datec(name);
#ifdef DEBUG_DATE
            printf("ntime=%d\n", stime);
#endif
            continue;
        }
        newfile = 0;
        lock = Lock(name, ACCESS_INVISIBLE);
        if (lock == 0) {
            lock = Open(name, ACCESS_READ);
            if (lock == 0) {
                lock = Open(name, MODE_NEWFILE);
                if (lock == 0) {
                    fprintf(stderr, "Error creating %s\n", name);
                    continue;
                }
                newfile = 1;
            }
            Close(lock);
        } else
            UnLock(lock);

        if (Cflag)
            continue;

        if (GetTimes(name, &tval[0], &tval[1], &tval[2])) {
            printf("Failed to get time of %s\n", name);
            rc = 1;
            continue;
        }

        if (pflag)
            printf("Get access=%d.%d modify=%d.%d create=%d.%d\n",
                    tval[0].tv_secs, tval[0].tv_micro,
                    tval[1].tv_secs, tval[1].tv_micro,
                    tval[2].tv_secs, tval[2].tv_micro);

        if (aflag || set_all) {  /* set mtime */
            tval[0].tv_secs = stime;
            tval[0].tv_micro = 0;
        }
        if (mflag || set_all) {  /* set mtime */
            tval[1].tv_secs = stime;
            tval[1].tv_micro = 0;
        }
        if (cflag || set_all) {  /* set ctime */
            tval[2].tv_secs = stime;
            tval[2].tv_micro = 0;
        }

        if (pflag && (aflag | mflag | cflag || set_all))
            printf("Set access=%d.%d modify=%d.%d create=%d.%d\n",
                    tval[0].tv_secs, tval[0].tv_micro,
                    tval[1].tv_secs, tval[1].tv_micro,
                    tval[2].tv_secs, tval[2].tv_micro);
        SetTimes(name, &tval[0], &tval[1], &tval[2]);
    }

    exit(rc);
}
