#ifndef _GETOPT_H
int    getopt(int nargc, char * const * nargv, const char *ostr);
extern char *optarg;
extern int opterr;
extern int optind;
extern int optopt;
extern int optreset;
#endif
