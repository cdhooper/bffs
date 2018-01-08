#include <stdio.h>
#include <clib/exec_protos.h>
#include <dos/dos.h>
#include "dump_fstab.h"

extern int diskfd;
extern int DEV_BSIZE;

void error_exit(int num);

/*
 * The following is Amiga specific support code
 */

#ifdef AMIGA

#ifdef _DCC
char *_stack_base = NULL;
#endif

int
break_abort(void)
{
        fprintf(stderr, "^C\n");
        dio_close();
        exit(1);
}

int
fbwrite(char *buf, int bno, int size)
{
        fprintf(stderr, "Error, attempt to write aborted!\n");
        error_exit(1);
	return(1);
}

void
error_exit(int num)
{
        dio_close();
        exit(num);
}

int
seteuid(int euid)
{
    return (0);
}

int
setuid(int euid)
{
    return (0);
}

int
getuid(void)
{
#ifdef _DCC
    /* Work around DICE library setjmp() bug */
    extern char *_stk_base;
    _stack_base = _stk_base;
#endif
    return (0);
}

int
geteuid(void)
{
    return (0);
}

int
getpid(void)
{
    return (0);
}

void
sync(void)
{
}

int
pause(void)
{
#if 0
    int signals = Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D |
                       SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
#endif
}

void
cleanup(void)
{
}

/*
 * The following (unimplemented) functions are for reading fstab entries
 * They might be useful to implement in the future if someone wants to
 * use the same fstab as they have on their NetBSD box.
 */
struct fstab *
getfsent(void)
{
    return (NULL);
}

int
setfsent(void)
{
    return (1);
}

void
endfsent(void)
{
}

int
gethostname(char *name, size_t len)
{
    strncpy(name, "Amiga", len);
}

#ifdef _DCC
int
ftruncate(int fd, unsigned long length)
{
    /* Might need to enhance this code to perform Amiga ACTION_TRUNCATE */
    return (0);
}

int
fork(void)
{
    printf("fork() called\n");
    return (1);
}
#endif
#endif
