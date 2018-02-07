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

#ifndef _STAT_H
#define _STAT_H

/*
 * Update statistics only when FAST mode is off
 */
#ifdef FAST
#define UPSTAT(x)         /* (x) */
#define UPSTATVALUE(x, y) /* (x, y) */
#else
#define UPSTAT(x)         stat->x++
#define UPSTATVALUE(x, y) stat->x += y
#endif


/*
 * The information structure used by bffstool to monitor a filessystem.
 */
struct stat {
    ULONG   magic;              /* 'BFF\0' for BFFS FileSystem R0 */
    ULONG   size;               /* structure size (in longs)      */
    ULONG   phys_sectorsize;    /* physical device sector size    */
    ULONG   superblock;         /* updated only on packet request */
    ULONG   cache_head;         /* updated only on packet request */
    ULONG   cache_hash;         /* updated only on packet request */
    ULONG  *cache_size;         /* updated only on packet request */
    ULONG  *cache_cg_size;      /* updated only on packet request */
    ULONG  *cache_item_dirty;   /* updated only on packet request */
    ULONG  *cache_alloced;      /* updated only on packet request */
    ULONG  *disk_poffset;       /* updated only on packet request */
    ULONG  *disk_pmax;          /* updated only on packet request */
    ULONG  *unix_paths;         /* updated only on packet request */
    ULONG  *resolve_symlinks;   /* updated only on packet request */
    ULONG  *case_dependent;     /* updated only on packet request */
    ULONG  *link_comments;      /* updated only on packet request */
    ULONG  *inode_comments;     /* updated only on packet request */
    ULONG  *cache_used;         /* updated only on packet request */
    ULONG  *timer_secs;         /* updated only on packet request */
    ULONG  *timer_loops;        /* updated only on packet request */
    LONG   *GMT;                /* updated only on packet request */
    ULONG  *minfree;            /* updated only on packet request */
    ULONG  *og_perm_invert;     /* updated only on packet request */
    ULONG   hit_data_read;      /* cache stats */
    ULONG   hit_data_write;
    ULONG   miss_data_read;
    ULONG   miss_data_write;
    ULONG   hit_cg_read;        /* cg cache stats */
    ULONG   hit_cg_write;
    ULONG   miss_cg_read;
    ULONG   miss_cg_write;
    ULONG   cache_invalidates;
    ULONG   cache_locked_frags;
    ULONG   cache_moves;
    ULONG   cache_flushes;
    ULONG   cache_destroys;
    ULONG   cache_force_writes;
    ULONG   cache_cg_flushes;
    ULONG   cache_cg_force_writes;
    ULONG   direct_reads;       /* reads DMA'ed directly to/from memory */
    ULONG   direct_writes;
    ULONG   direct_read_bytes;
    ULONG   direct_write_bytes;
    ULONG   locates;            /* packets */
    ULONG   examines;
    ULONG   examinenexts;
    ULONG   flushes;
    ULONG   findinput;
    ULONG   findoutput;
    ULONG   reads;
    ULONG   writes;
    ULONG   renames;
    char    handler_version[32];
    char    disk_type[32];
    ULONG   gmt_shift;
};

#endif /* _STAT_H */
