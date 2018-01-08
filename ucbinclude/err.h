#ifndef _UCB_ERR_H_
#define _UCB_ERR_H_

char *strerror(int err);
void err(int exit_code, const char *fmt, ...);
void errx(int exit_code, const char *fmt, ...);
int warnx(const char *fmt, ...);
int warn(const char *fmt, ...);

#endif /* _UCB_ERR_H_ */
