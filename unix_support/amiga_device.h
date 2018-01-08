#ifndef _AMIGA_DEVICE_H
int dio_open(char *name);
int dio_close(void);
int dio_checkstack(int minimum);
int dio_inhibit(int inhibit);
int bread(char *buf, daddr_t blk, long size);
int bwrite(char *buf, daddr_t blk, long size);
#endif
