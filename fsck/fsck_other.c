Perror(s, a1, a2, a3, a4)
char *s;
unsigned long a1, a2, a3, a4;
{
	static char buf[1024];
	sprintf(buf, s, a1, a2, a3, a4);
	perror(buf);
}

err(x, y)
int x, y;
{
	printf("error %d\n", y);
}

errx(x, str, arg1, arg2, arg3)
{
	printf("error: ");
	printf(str, arg1, arg2, arg3);
	printf("\n");
}
