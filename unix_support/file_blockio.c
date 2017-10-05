#include <stdio.h>
#include <sys/types.h>

extern int ifd;
extern int ofd;
extern int sectorsize;

/*
 * read a block from the file system
 */
#ifdef AMIGA
fbread(bf, bno, size)
#else
bread(bf, bno, size)
#endif
	char *bf;
	daddr_t bno;
	int size;
{
	int n;

/*
printf("bread %d, buf=%x bno=%d size=%d ss=%d\n", ifd, bf, bno, size, sectorsize);
*/
	if (lseek(ifd, (off_t)bno * sectorsize, 0) < 0) {
		printf("seek error: %ld\n", bno);
		perror("bread");
		exit(33);
	}
	n = read(ifd, bf, size);
	if (n != size) {
		printf("read error: %ld\n", bno);
		perror("bread");
		exit(34);
	}

	return(0);
}


/*
 * write a block to the file system
 */
#ifdef AMIGA
fbwrite(bf, bno, size)
#else
bwrite(bf, bno, size)
#endif
	char *bf;
	daddr_t bno;
	int size;
{
	int n;

	if (ofd < 0)
		return(0);

/*
printf("bwrite %d, buf=%x bno=%d size=%d ss=%d\n", ofd, bf, bno, size, sectorsize);
*/

	if (lseek(ofd, (off_t)bno * sectorsize, 0) < 0) {
		printf("seek error: %ld\n", bno);
		perror("bwrite");
		exit(35);
	}
	n = write(ofd, bf, size);
	if (n != size) {
		printf("write error: %ld\n", bno);
		perror("bwrite");
		exit(36);
	}

	return(0);
}
