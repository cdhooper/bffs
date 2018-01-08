#include <stdio.h>
#include <stdarg.h>
#include <err.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))
#endif

const char * errmsg[] = {
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

char *
strerror(int err)
{
    static char buf[16];

    if ((err >= 0) && (err < ARRAY_SIZE(errmsg)))
	return(errmsg[err]);

    sprintf(buf, "%d", err);
    return(buf);
}

void
Perror(char *s, const char *fmt, ...)
{
    va_list va;
    static char buf[1024];

    va_start(va, fmt);
    vsprintf(buf, s, va);
    fprintf(stderr, fmt, va);
    va_end(va);
    perror(buf);
}


void
errx(int exit_code, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    cleanup();
    exit(exit_code);
}

void
err(int exit_code, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    cleanup();
    exit(exit_code);
}

int
warnx(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    return(0);
}

int
warn(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    return(0);
}
