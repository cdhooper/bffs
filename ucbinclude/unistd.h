#ifndef _UCB_UNISTD_H_
#define _UCB_UNISTD_H_

#ifndef SEEK_SET
#define     SEEK_SET        0       /* set file offset to offset */
#endif

#ifndef SEEK_CUR
#define     SEEK_CUR        1       /* set file offset to current plus offset */
#endif

#ifndef SEEK_END
#define     SEEK_END        2       /* set file offset to EOF plus offset */
#endif

int getopt(int nargc, char * const *nargv, const char *ostr);

#endif /* _UCB_UNISTD_H_ */
