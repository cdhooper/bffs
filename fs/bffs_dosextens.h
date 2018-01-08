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

#ifndef _BFFS_DOSEXTENS_H
#define _BFFS_DOSEXTENS_H

/* new 3.0 packets */
#define ACTION_SET_OWNER	1036

/* special BFFS packets */
#define ACTION_CREATE_OBJECT	2346
#define ACTION_GET_PERMS	2995
#define ACTION_SET_PERMS	2996
#define ACTION_SET_TIMES	2997
#define ACTION_SET_DATES	2998
#define ACTION_FS_STATS		2999

/* Ralph Babel packets */
#define ACTION_GET_DISK_FSSM	4201
#define ACTION_FREE_DISK_FSSM	4202

/* Given to me by Michael B. Smith, AS225 packets */
#define ACTION_EX_OBJECT	50
#define ACTION_EX_NEXT		51


/* Types for fib_DirEntryType.	NOTE that both USERDIR and ROOT are	 */
/* directories, and that directory/file checks should use <0 and >=0.	 */
/* This is not necessarily exhaustive!	Some handlers may use other	 */
/* values as needed, though <0 and >=0 should remain as supported as	 */
/* possible.								 */
#define ST_ROOT		1
#define ST_USERDIR	2
#define ST_SOFTLINK	3	/* looks like dir, but may point to a file! */
#define ST_LINKDIR	4	/* hard link to dir */
#define ST_FILE		-3	/* must be negative for FIB! */
#define ST_LINKFILE	-4	/* hard link to file */
#define ST_BDEVICE	-10	/* block special device */
#define ST_CDEVICE	-11	/* char special device */
#define ST_SOCKET	-12	/* UNIX socket */
#define ST_FIFO		-13	/* named pipe (queue) */
#define ST_LIFO		-14	/* named pipe (stack) */
#define ST_WHITEOUT	-15	/* whiteout entry */


#ifndef FIBB_HIDDEN
#define FIBB_HIDDEN    7        /* program is not visible in a listing */
#define FIBF_HIDDEN    (1<<FIBB_HIDDEN)
#endif

#ifndef FIBB_OTR_READ
#define FIBB_OTR_READ      15   /* Other: file is readable */
#define FIBB_OTR_WRITE     14   /* Other: file is writable */
#define FIBB_OTR_EXECUTE   13   /* Other: file is executable */
#define FIBB_OTR_DELETE    12   /* Other: prevent file from being deleted */
#define FIBB_GRP_READ      11   /* Group: file is readable */
#define FIBB_GRP_WRITE     10   /* Group: file is writable */
#define FIBB_GRP_EXECUTE   9    /* Group: file is executable */
#define FIBB_GRP_DELETE    8    /* Group: prevent file from being deleted */
#define FIBF_OTR_READ      (1<<FIBB_OTR_READ)
#define FIBF_OTR_WRITE     (1<<FIBB_OTR_WRITE)
#define FIBF_OTR_EXECUTE   (1<<FIBB_OTR_EXECUTE)
#define FIBF_OTR_DELETE    (1<<FIBB_OTR_DELETE)
#define FIBF_GRP_READ      (1<<FIBB_GRP_READ)
#define FIBF_GRP_WRITE     (1<<FIBB_GRP_WRITE)
#define FIBF_GRP_EXECUTE   (1<<FIBB_GRP_EXECUTE)
#define FIBF_GRP_DELETE    (1<<FIBB_GRP_DELETE)
#endif

/* 3.x version of FileInfoBlock */
typedef struct {
   LONG	  fib_DiskKey;
   LONG	  fib_DirEntryType;  /* Type of Directory. If < 0, then a plain file.
			      * If > 0 a directory */
   char	  fib_FileName[108]; /* Null terminated. Max 30 chars used for now */
   LONG	  fib_Protection;    /* bit mask of protection, rwxd are 3-0.	   */
   LONG	  fib_EntryType;
   LONG	  fib_Size;	     /* Number of bytes in file */
   LONG	  fib_NumBlocks;     /* Number of blocks in file */
   struct DateStamp fib_Date;/* Date file last changed */
   char	  fib_Comment[80];  /* Null terminated comment associated with file */

   /* Note: the following fields are not supported by all filesystems.  */
   /* They should be initialized to 0 sending an ACTION_EXAMINE packet. */
   /* When Examine() is called, these are set to 0 for you.             */
   /* AllocDosObject() also initializes them to 0.                      */
   UWORD  fib_OwnerUID;		/* owner's UID */
   UWORD  fib_OwnerGID;		/* owner's GID */

   LONG   is_remote;		/* on nfs mounted volume */
   LONG   mode;			/* unix-mode when is_remote != ZERO */
   char   fib_Reserved[8];
   LONG	  fib_DirOffset;
} FileInfoBlock_3_t;

#endif	/* _BFFS_DOSEXTENS_H */
