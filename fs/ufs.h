/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _UFS_H
#define _UFS_H

#define ID_BFFS_DISK	(0x42464653L)  /* 'BFFS' filesystem */
#if 0
#define ID_BFFS_DISK	ID_FFS_DISK
#define ID_BFFS_DISK	ID_OFS_DISK
#endif

#define STARTUPMSG	((struct FileSysStartupMsg *) BTOC(DevNode->dn_Startup))
#define ENVIRONMENT	((struct DosEnvec *) BTOC(STARTUPMSG->fssm_Environ))

#define DISK_DEVICE	((char *) BTOC(STARTUPMSG->fssm_Device)) + 1
#define DISK_UNIT	STARTUPMSG->fssm_Unit
#define DISK_FLAGS	STARTUPMSG->fssm_Flags

#define BOOT_BLOCK	0	/* Physical position of disk boot block */
#define SUPER_BLOCK	16	/* Position of superblock in fs         */
#define MAX_PART	8	/* Number disk partitions supported     */


extern	ULONG	phys_sectorsize;	/* physical disk sector size, env */
extern	int	fs_partition;		/* current partition number	  */
extern	struct	fs *superblock;		/* current superblock		  */
extern	struct	IOExtTD *trackIO;	/* pointer to trackdisk IORequest */
extern	ULONG	psectoffset;		/* disk partition start sector    */
extern	ULONG	psectmax;		/* disk partition end sector      */
extern	ULONG	tranmask;		/* device DMA transfer mask	  */

extern	ULONG	pfragshift;		/* pointers in a frag		  */
extern	ULONG	pfragmask;		/* mask current pointer in frag	  */
extern	ULONG	fblkshift;		/* frags in a block		  */
extern	ULONG	fblkmask;		/* mask current frag in block	  */
extern  int	bsd44fs;		/* 0 = 4.3 BSD, 1 = 4.4 BSD	  */
extern  ULONG	fs_lfmask;		/* quick mask, bits flipped ala q */
extern  ULONG	fs_lbmask;		/* quick mask, bits flipped ala q */

#ifndef NULL
#define NULL 0
#endif

#define MAXSYMLINKLEN (12 + 3) * 4 - 1

int  open_dchange(void);
void close_dchange(int normal);
void motor_off(void);
void motor_on(void);
void superblock_flush(void);
void close_ufs(void);
int  open_ufs(void);
int  find_superblock(void);
void superblock_destroy(void);
int  data_read(void *buf, ULONG num, ULONG size);
int  data_write(void *buf, ULONG num, ULONG size);

#endif /* _UFS_H */
