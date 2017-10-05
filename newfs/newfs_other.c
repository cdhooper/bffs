#include <stdio.h>

Perror(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	static char buf[512];
	sprintf(buf, s, a1, a2, a3, a4);
	perror(buf);
	exit(1);
}

warn(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	static char buf[512];
	sprintf(buf, s, a1, a2, a3, a4);
	perror(buf);
}

err(x, y)
int x, y;
{
	printf("error %d\n", y);
}

Fatal(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	fprintf(stderr, s, a1, a2, a3, a4);
	fprintf(stderr, "\n");
	exit(1);
}
