#include <stdio.h>

char *errmsg[] = {
	"Unknown",
	"Not owner",
	"No such file or directory",
	"No such process",
	"Interrupted system call",
	"I/O error",
	"No such device or address",
	"Arg list too long",
	"Exec format error",
	"Bad file number",
	"No children",
	"No more processes",
	"Not enough core",
	"Permission denied",
	"Bad address",
	"Block device required",
	"Mount device busy",
	"File exists",
	"Cross-device link",
	"No such device",
	"Not a director",
	"Is a directory",
	"Invalid argument",
	"File table overflow",
	"Too many open files",
	"Not a typewriter",
	"Text file busy",
	"File too large",
	"No space left on device",
	"Illegal seek",
	"Read-only file system"
};

Perror(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	static char buf[1024];
	sprintf(buf, s, a1, a2, a3, a4);
	perror(buf);
        return(0);
}


int errx(x,str,a,b,c,d,e)
unsigned long x;
char *str;
unsigned long a,b,c,d,e;
{
        fprintf(stderr, str, a, b, c, d, e);
	fprintf(stderr, "\n");
	cleanup();
	exit(x);
}

int err(x,str,a,b,c,d,e)
unsigned long x;
char *str;
unsigned long a,b,c,d,e;
{
        fprintf(stderr, str, a, b, c, d, e);
	fprintf(stderr, "\n");
        return(0);
}

char *strerror(err)
int err;
{
        static char buf[16];

	if ((err >= 0) && (err < 31))
		return(errmsg[err]);

	sprintf(buf, "%d", err);
        return(buf);
}

int warnx(x,y)
char *x,y;
{
        fprintf(stderr, x,y);
	fprintf(stderr, "\n");
        return(0);
}

undelete(x)
char *x;
{
	return(-1);
}

exit_cleanup(x)
int x;
{
	cleanup();
	exit(x);
}
