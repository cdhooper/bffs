/* eln.c version 1.0
 *      This program is copyright (1994) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

#include <stdio.h>
#include <dos/dos.h>

#define HARD 0
#define SOFT 1

main(argc, argv)
int  argc;
char *argv[];
{
	int index = 1;
	int type = HARD;
	char buf[68];
	char *src;
	char *dest;
	void *dest;

	if (argc < 3)
		print_usage(argv[0]);

	if (!strcmp(argv[index], "-s")) {
		type = SOFT;
		index++;
	}

	src = argv[index++];

	if (index >= argc)
		print_usage(argv[0]);

	if (type == HARD)
		dest = (void *) Lock(src, ACCESS_READ);
	else
		dest = (void *) src;

	for (; index < argc; index++)
		if (!MakeLink(argv[index], dest, type)) {
			fprintf(stderr, "Unable to link %s to %s\n",
				argv[index], src);
			printf("%d\n", IoErr());
			Fault(IoErr(), "", buf, 67);
			fprintf(stderr, "   because: %s\n", buf);
		}

	if (type == HARD)
		UnLock(dest);

	exit(0);
}

print_usage(progname)
char *progname;
{
	fprintf(stderr, "usage:  %s [-s] src dest [dest2 dest3 ...]\n", progname);
	exit(1);
}
