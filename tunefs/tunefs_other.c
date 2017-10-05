#include <stdio.h>

Perror(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	static char buf[1024];
	sprintf(buf, s, a1, a2, a3, a4);
	perror(buf);
}

err(x, str, arg1, arg2, arg3)
int x;
char *str;
int arg1, arg2, arg3;
{
	printf("error %d: ", x);
	printf(str, arg1, arg2, arg3);
	printf("\n");
}

errx(x, str, arg1, arg2, arg3)
int x;
char *str;
int arg1, arg2, arg3;
{
	printf("error %d: ", x);
	printf(str, arg1, arg2, arg3);
	printf("\n");
	exit(x);
}

warnx(s, a1, a2, a3, a4, a5)
char *s;
unsigned long a1, a2, a3, a4, a5;
{
	fprintf(stderr, s, a1, a2, a3, a4, a5);
	fprintf(stderr, "\n");
}


#ifdef AMIGA
void break_abort()
{
        fprintf(stderr, "^C\n");
        dio_inhibit(0);
        dio_close();
        exit(1);
}
#endif

void error_exit(num)
int num;
{
#ifdef AMIGA
        dio_inhibit(0);
        dio_close();
#endif
        exit(num);
}
