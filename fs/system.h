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

#ifndef _SYSTEM_H
#define _SYSTEM_H

typedef struct {
        ULONG   fa_type;
        ULONG   fa_mode;
        ULONG   fa_nlink;
        ULONG   fa_uid;
        ULONG   fa_gid;
        ULONG   fa_size;
        ULONG   fa_blocksize;
        ULONG   fa_rdev;
        ULONG   fa_blocks;
        ULONG   fa_fsid;
        ULONG   fa_fileid;
        ULONG   fa_atime;
        ULONG   fa_atime_us;
        ULONG   fa_mtime;
        ULONG   fa_mtime_us;
        ULONG   fa_ctime;
        ULONG   fa_ctime_us;
} fileattr_t;

extern char *volumename;
extern int   og_perm_invert;  /* invert permissions on other/group */

void NewVolNode(void);
void RemoveVolNode(void);
void close_files(void);
void truncate_file(struct BFFSfh *fileh);
int  ResolveColon();
int  print_stats();
int  print_hash();
void FillInfoBlock();
void FillAttrBlock(fileattr_t *attr, struct icommon *finode);
int  open_filesystem(void);
void close_filesystem(void);

void UnNameHandler(void);
void NameHandler(const char *name);
void InitStats(void);
void FillInfoBlock(FileInfoBlock_3_t *fib, struct BFFSLock *lock,
                   struct icommon *finode, struct direct *dir_ent);
ULONG parent_parse(struct BFFSLock *lock, char *path, char **name);
ULONG path_parse(struct BFFSLock *lock, char *path);
struct BFFSLock *CreateLock(ULONG key, int mode, ULONG pinum, ULONG poffset);

#endif /* _SYSTEM_H */
