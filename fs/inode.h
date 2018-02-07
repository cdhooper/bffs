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

#ifndef _INODE_H
#define _INODE_H

icommon_t *inode_read(int inum);    /* inode struct from cache (for read) */
icommon_t *inode_modify(int inum);  /* inode struct from cache (for write) */

int  inum_new(int parent, int type);
void inum_free(ULONG inum);
void inum_sane(ULONG inum, int type);
int  is_writable(ULONG inum);
int  is_readable(ULONG inum);
ULONG ridisk_frag(ULONG file_frag_num, ULONG inum);
ULONG widisk_frag(ULONG file_frag_num, ULONG inum);
ULONG cidisk_frag(ULONG file_frag_num, ULONG inum, ULONG newblk);

#ifdef BOTHENDIAN
void adjust_ic_size(icommon_t *inode, int adjust);
void adjust_ic_blks(icommon_t *inode, int adjust);
#endif

#endif /* _INODE_H */
