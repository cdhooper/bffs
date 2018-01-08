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

#ifndef _FILE_H
#define _FILE_H

extern	int	unix_paths;	  /* follow Unix pathnames=1, AmigaDOS=0 */
extern	int	write_pending;	  /* fs modified, but cache not flushed */
extern	char	*linkname;	  /* resolved name of symlnk */
extern	int	read_link;	  /* 1=currently reading a link (packet.c) */

#define I_DIR	0
#define I_FILE	1

#ifndef NULL
#define NULL	0
#endif

int file_name_find(ULONG pinum, char *name);
ULONG file_read(ULONG inum, ULONG sbyte, ULONG nbytes, char *buf);
ULONG file_write(ULONG inum, ULONG sbyte, ULONG nbytes, char *buf);
int file_find(int inum, char *name);
int file_deallocate(ULONG inum);
int file_blocks_add(struct icommon *inode, ULONG inum, ULONG startfrag,
		    ULONG blocks);
int file_block_extend(ULONG inum);
void file_blocks_deallocate(ULONG inum);
int file_block_retract(int inum);
ULONG frag_expand(ULONG inum);
void symlink_delete(ULONG inum, ULONG size);
int symlink_create(ULONG inum, struct icommon *inode, char *contents);

#endif /* _FILE_H */
