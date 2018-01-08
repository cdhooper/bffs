#ifndef _UCB_SYS_TIME_H_
#define _UCB_SYS_TIME_H_

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */
typedef struct {
	long	tv_sec;		/* seconds */
	long	tv_usec;	/* and microseconds */
} unix_timeval_t;

#ifdef __linux
#define __BEGIN_NAMESPACE_STD
#define __END_NAMESPACE_STD
#define __USING_NAMESPACE_STD(x)
#define __BEGIN_NAMESPACE_C99
#define __END_NAMESPACE_C99
#define __USING_NAMESPACE_C99(x)
#define __MODE_T_TYPE unsigned short
#define __THROW
#define __THROWNL
#define __attribute_pure__
#define __extension__
#define __wur
#define __attribute_malloc__
#define __attribute_alloc_size__(x)
#define __attribute_warn_unused_result__
#define __nonnull(x)
#include <stdint.h>

typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned long u_long;
#else

/*
 * Structure defined by POSIX.4 to be like a timeval.
 */
struct timespec {
	long	ts_sec;		/* seconds */
	long	ts_nsec;	/* and nanoseconds */
};
#endif

#include <time.h>
#include <sys/cdefs.h>

#endif /* !_SYS_TIME_H_ */
