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

#ifndef _CACHE_H
#define _CACHE_H

extern	int cache_size;			/* max frags allowed in cache */
extern	int cache_cg_size;		/* max cgs in cg cache */
char   *cache_frag(int blk);		/* get buffer for specified frag */

/* get buffer for frag, mark dirty */
char   *cache_frag_write(int blk, int readflag);

char   *cache_available(int blk);	/* get buffer if in cache, else NULL */
struct	cg *cache_cg(ULONG cgx);	/* get buffer for specified cg */
struct	cg *cache_cg_write(ULONG cgx);	/* get buffer for cg, mark dirty */
void	cache_frag_flush(int blk);	/* sync disk with specified frag */
void	cache_adjust(void);		/* handle cache size change */
void	cache_cg_adjust(void);		/* handle cache cg size change */
void	cache_flush(void);		/* write out all dirty frags in cache */
void	cache_cg_flush(void);		/* write out the cg summary cache */
void    cache_full_invalidate(void);    /* eliminate all cached frags */
void    cache_invalidate(int blk);      /* throw out a cache entry (no flush) */
void    open_cache(void);
void    close_cache(void);

/* Reassign cached frag to a different block address */
void	cache_frag_move(int to_blk, int from_blk);

/* This is the basic node for the cache routines */
struct cache_set {
	int blk;			/* disk frag address of node */
	int flags;			/* frag node flags (see below) */
	char *buf;			/* memory buffer address */
	struct cache_set *stack_up;	/* older cache nodes */
	struct cache_set *stack_down;	/* newer cache nodes */
	struct cache_set *hash_up;	/* hash chain parent */
	struct cache_set *hash_down;	/* hash chain child  */
	struct cache_set *next;		/* sorted order higher address */
};

/* cache frag node flags */
#define CACHE_CLEAN   0			/* frag clean	   */
#define CACHE_DIRTY   1			/* frag dirty	   */
#define CACHE_LOCKED  2			/* locked in cache */

#ifndef NULL
#define NULL 0
#endif

#endif /* _CACHE_H */
