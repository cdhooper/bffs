#ifndef _AMIGA_UNIX_H
#define _AMIGA_UNIX_H

#include <sys/time.h>

int PMakeLink(const char *dname, const char *sname, ulong type);
int GetTimes(const char *name, void *atime, void *mtime, void *ctime);
int SetPerms(const char *name, ulong mode);
int SetTimes(const char *name, unix_timeval_t *atime, unix_timeval_t *mtime,
             unix_timeval_t *ctime);

#endif  /* _AMIGA_UNIX_H */
