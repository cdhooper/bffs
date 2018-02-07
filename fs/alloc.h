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

#ifndef _ALLOC_H
#define _ALLOC_H

#define bit_set(ptr, index)   (ptr[(index) >> 3] |= 1<<((index) & 7))
#define bit_clr(ptr, index)   (ptr[(index) >> 3] &= ~(1<<((index) & 7)))
#define bit_val(ptr, index)  ((ptr[(index) >> 3] & 1<<((index) & 7)) ? 1 : 0)
#define bit_chk(ptr, index)  ((ptr[(index) >> 3] & 1<<((index) & 7)))

int   frags_left_are_free(uint8_t *map, ULONG frag);
int   block_avail(uint8_t *map, ULONG fpos);
int   block_fragblock_find(struct cg *mycg, ULONG preferred, int min_frags);
ULONG block_allocate(ULONG nearblock);
void  block_deallocate(ULONG block);
int   frags_allocate(ULONG fragstart, ULONG frags);
void  frags_deallocate(ULONG fragstart, ULONG frags);

#endif /* _ALLOC_H */
