#ifndef _FILE_BLOCKIO_H
#define _FILE_BLOCKIO_H

int fbread(char *bf, daddr_t bno, int size);
int fbwrite(char *bf, daddr_t bno, int size);

#endif
