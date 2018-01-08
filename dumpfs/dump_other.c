#include <stdio.h>
extern int fsi;
extern int DEV_BSIZE;

void error_exit();

char *strerror(err)
int err;
{
        static char unknown[20];
        strcpy(unknown, " entry not found");
        return(unknown);
}


/*
 * The following is Amiga specific support code
 */

#ifdef AMIGA
void break_abort()
{
        fprintf(stderr, "^C\n");
        dio_close();
        exit(1);
}

/*
 * read a block from the file system
 */
fbread(char *buf, int bno, int size)
{
        int n;

/*
	printf("bread %d, buf=%x bno=%d size=%d ss=%d\n",
		fsi, buf, bno, size, DEV_BSIZE);
*/

        if (lseek(fsi, bno * DEV_BSIZE, 0) < 0) {
                printf("seek error: %ld\n", bno);
                perror("bread");
                error_exit(33);
        }
        n = read(fsi, buf, size);
        if (n != size) {
                printf("read error: %ld\n", bno);
                perror("bread");
                error_exit(34);
        }

        return(0);
}

fbwrite(char *buf, int bno, int size)
{
        fprintf(stderr, "Error, attempt to write aborted!\n");
        error_exit(1);
	return(1);
}

void error_exit(num)
int num;
{
        dio_close();
        exit(num);
}
#endif
