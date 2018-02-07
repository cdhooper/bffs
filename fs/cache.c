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

#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <dos/filehandler.h>

#include "config.h"

#include "superblock.h"
#include "ufs.h"
#include "fsmacros.h"
#include "cache.h"
#include "file.h"
#include "handler.h"
#include "stat.h"

#define HASH_SIZE       16      /* lines */
#define CACHE_CG_MAX    16      /* max cgs cacheable */
#define CACHE_SIZE_MIN   8      /* min size of cache */

/* Allocate and deallocate cache memory */
static cache_set_t *cache_alloc(void);
static void cache_free(cache_set_t *node);

/* Lookup blk in cache or return NULL if not in cache */
static cache_set_t *cache_node(ULONG blk);

/* Reassign oldest LRU */
static cache_set_t *cache_getnode(ULONG blk); /* Reassign oldest LRU ent */

/* Add/remove node in stack (LRU) and hash */
static void cache_add(cache_set_t *node);
static void cache_remove(cache_set_t *node);

/* Add node to order list for disk block flush */
static void cache_insert(cache_set_t **head, cache_set_t *node);

/* Move dirty nodes to LRU or flush them to disk if too many */
static void cache_cycle_dirty(void);

/* Move node to most recently used */
static void cache_refresh(cache_set_t *node);

/* Cylinder group cache most recently used */
static void cache_popup_cg(int pos);

cache_set_t *cache_stack;
cache_set_t *cache_stack_tail;
cache_set_t *hashtable[HASH_SIZE];
struct cg   *cache_cg_data[CACHE_CG_MAX];
char         cache_cg_state[CACHE_CG_MAX];
int          cache_alloced    = 0;
int          cache_used       = 0;
int          cache_item_dirty = 0;
extern int   cache_size;    /* initial cache frags if 0 in mountlist */
extern int   cache_cg_size; /* number of cylinder group fsblock buffers */

#undef CACHE_DISABLE_HASH
#ifdef CACHE_DISABLE_HASH
#define CACHE_HASH(blk)   (0)
#else
#define CACHE_HASH(blk) cache_hash(blk)

/*
 * cache_hash()
 *      This routine will calculate a hash value for the expected block
 *      for fast lookup into the disk cache.
 *
 *      It's worth noting that I tried a number of hash algorithms against
 *      live filesystem block addresses and found that the below generated
 *      the best spread across all hash buckets.  DICE doesn't generate the
 *      most optimal code for the below (20 instructions, 4 registers), but
 *      it's not far off from hand-coded (17 instructions, 3 registers).
 */
static ULONG
cache_hash(ULONG blk)
{
    blk = blk ^ (blk >> 16);
    blk = blk ^ (blk >> 8);
    blk = blk ^ (blk >> 4);
    return (blk & (HASH_SIZE - 1));
}
#endif

/*
 * cache_frag()
 *      This routine will return a pointer to a copy of the specified
 *      frag from the disk.  It automatically adjusts the LRU table of
 *      so that it is more likely frequently requested blocks will
 *      remain in the cache.
 */
char *
cache_frag(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        UPSTAT(hit_data_read);
        cache_refresh(node);
    } else {
        UPSTAT(miss_data_read);
        node = cache_getnode(blk);
        if (data_read(node->buf, node->blk, FSIZE)) {
            PRINT2(("cache_frag: data_read failed\n"));
            return (NULL);
        }
        cache_add(node);
    }
    return (node->buf);
}

/*
 * cache_available()
 *      This routine returns the memory address of a frag if it's
 *      already in cache.  It will return NULL if the frag is not
 *      in memory.  Use cache_frag() to fetch frags from disk.
 */
char *
cache_available(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        UPSTAT(hit_data_read);
        cache_refresh(node);
        return (node->buf);
    }
    return (NULL);
}

#ifndef RONLY
/*
 * cache_frag_write()
 *      This routine will return a buffer to write the given block into.
 *      The buffer is pre-set to be a dirty block.  The caching mechanism
 *      will automatically take care of flushing dirty frags once the
 *      buffer is full.  If readflag is set (non-zero), the calling routine
 *      is guaranteed the current contents of the fragment is read into the
 *      cache.  Otherwise, the buffer contents will be the fragment from the
 *      disk only if it is already in the cache.
 */
char *
cache_frag_write(ULONG blk, int readflag)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        UPSTAT(hit_data_write);
        cache_refresh(node);
    } else {
        node = cache_getnode(blk);
        if (readflag) {
            UPSTAT(miss_data_write);
            if (data_read(node->buf, node->blk, FSIZE)) {
                PRINT2(("cache_frag_write: data read failed\n"));
                return (NULL);
            }
        }
        cache_add(node);
    }
    if (!(node->flags & CACHE_DIRTY)) {
        node->flags |= CACHE_DIRTY;
        cache_item_dirty++;
    }
    return (node->buf);
}
#endif


#if 0
/*
 * cache_frag_lock()
 *      This routine will lock a frag in the cache.  It is used to
 *      alleviate the number of calls to cache_frag and cache_frag_write
 *      just to assure a frag remains in the cache.
 */
void
cache_frag_lock(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        PRINT(("frag %d locked\n", blk));
#ifdef FAST
        node->flags |= CACHE_LOCKED;
#else
        if (!(node->flags & CACHE_LOCKED)) {
            UPSTAT(cache_locked_frags);
            node->flags |= CACHE_LOCKED;
        }
#endif
    } else {
        PRINT2(("call to lock for frag %d not in cache\n", blk));
    }
}


/*
 * cache_frag_unlock()
 *      This routine will unlock a locked frag in the cache
 */
void
cache_frag_unlock(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        PRINT(("frag %d unlocked\n", blk));
#ifdef FAST
        node->flags &= ~CACHE_LOCKED;
#else
        if (!(node->flags & CACHE_LOCKED)) {
            UPSTATVALUE(cache_locked_frags, -1);
            node->flags &= ~CACHE_LOCKED;
        }
#endif
    } else {
        PRINT2(("call to lock for frag %d not in cache\n", blk));
    }
}
#endif


/*
 * cache_invalidate()
 *      This routine will throw out a cache entry for the passed block
 *      No flushing will be done for dirty frags.
 */
void
cache_invalidate(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_node(blk)) != NULL) {
        UPSTAT(cache_invalidates);
        cache_remove(node);
        node->blk = 0;
        if (node->flags != CACHE_CLEAN) {
#ifndef FAST
            if (node->flags & CACHE_LOCKED) {
                PRINT2(("invalidating locked %sfrag %u\n",
                        (node->flags & CACHE_DIRTY) ? " dirty" : "",
                        node->blk));
            }
#endif
            if (node->flags & CACHE_DIRTY)
                cache_item_dirty--;
            node->flags = CACHE_CLEAN;
        }
        cache_add(node);
    }
}


/*
 * cache_full_invalidate()
 *      This routine will eliminate all cached frags, similar to
 *      cache destroy, except the structure of the cache remains
 *      intact.  This routine does NOT flush dirty entries first.
 */
void
cache_full_invalidate(void)
{
    cache_set_t *node;

    for (node = cache_stack; node != NULL; node = node->stack_down) {
#ifndef FAST
        if (node->flags & CACHE_DIRTY) {
            PRINT2(("invalidating %slocked dirty frag %u\n",
                    (node->flags & CACHE_LOCKED) ? "" : "un",
                    node->blk));
        }
#endif
        /* No need to adjust cache_item_dirty here */
        node->flags = CACHE_CLEAN;
        node->blk = 0;
    }
    cache_item_dirty = 0;
}

/*
 * cache_getnode()
 *      This routine will return the least recently used cache entry
 *      for new allocation into the cache.  The node is automatically
 *      disassociated (removed) from the cache before returning.
 *      The node is guaranteed to be in a clean state upon return
 *      from this routine.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static cache_set_t *
cache_getnode(ULONG blk)
{
    cache_set_t *node;

    if ((node = cache_stack) == NULL) {
        PRINT2(("ERROR: no cache buffers available\n"));
        return (NULL);
    }
    if (node->flags != CACHE_CLEAN) {
        cache_cycle_dirty();     /* Put those dirty blocks on bottom */
        node = cache_stack;
    }
    if (node->flags & CACHE_DIRTY) {
        PRINT2(("Cache just contains dirty nodes\n"));
        return (NULL);
    }

    cache_remove(node);
    node->blk   = blk;
    node->flags = CACHE_CLEAN;
    return (node);
}


/*
 * cache_cycle_dirty()
 *      This routine is called by cache_getnode in order to move any
 *      dirty frags up to the most recently used position.  Eventually,
 *      those dirty frags should automatically end up on disk rather
 *      than cycling through the cache.  Perhaps there could be a tag
 *      saying how many times a dirty frag has been recycled.
 */
static void
cache_cycle_dirty(void)
{
    cache_set_t *node;

    if (cache_stack == NULL) {
        PRINT2(("Error: cycle dirty found NULL stack.\n"));
        return;
    }
#ifndef RONLY
    if (cache_item_dirty > ((cache_size * 3) >> 2))
        goto do_cache_flush;
#endif

    node = cache_stack;

    while (cache_stack->flags != CACHE_CLEAN) {
        cache_refresh(cache_stack);
        if (cache_stack == node)
            break;
    }

#ifndef RONLY
    if (cache_stack->flags != CACHE_CLEAN) {
        /* Flush the entire cache if we can't find one free frag. */
do_cache_flush:
        PRINT(("flush dirty\n"));
        UPSTATVALUE(cache_force_writes, cache_size);

        cache_flush();

        while (cache_stack->flags != CACHE_CLEAN) {
            cache_refresh(cache_stack);
            if (cache_stack == node)
                break;
        }

        if (cache_stack->flags != CACHE_CLEAN) {
            PRINT2(("BUG: all frags in cache are locked\n"));
            receiving_packets = 0;
        }
    }
#endif
}


/*
 * open_cache()
 *      This routine is called to initialize the data structures
 *      responsible for maintaining the cache.  It initializes both
 *      the regular fragment cache and the cg block cache.
 */
void
open_cache(void)
{
    int index;

    /* init the frag hash table */
    for (index = 0; index < HASH_SIZE; index++)
        hashtable[index] = NULL;

    /* init the cylinder group cache */
    for (index = 0; index < CACHE_CG_MAX; index++)
        cache_cg_data[index] = NULL;

    cache_stack      = NULL;
    cache_stack_tail = NULL;
    cache_alloced    = 0;
    cache_used       = 0;
    cache_item_dirty = 0;
    cache_size       = ENVIRONMENT->de_NumBuffers;
    if (cache_size < CACHE_SIZE_MIN)
        cache_size = CACHE_SIZE_MIN;
    PRINT(("cache=%d  cache_cg=%d\n", cache_size, cache_cg_size));

    cache_adjust();
}


/*
 * cache_adjust()
 *      This routine is called whenever there is a size change for
 *      the cache.  It is responsible for allocating and deallocating
 *      cache buffers as needed.
 */
void
cache_adjust(void)
{
    cache_set_t *node;

    if (cache_size < CACHE_SIZE_MIN)
        cache_size = CACHE_SIZE_MIN;

    /* if the cache is too small */
    while (cache_alloced < cache_size) {
        int tries = 0;
cache_adjust_top:
        if ((node = cache_alloc()) == NULL) {
            if (cache_alloced <= CACHE_SIZE_MIN) {
                PRINT2(("ERROR: unable to allocate more than %u cache"
                        " buffers\n", cache_alloced));
                if (++tries < 2)
                    goto cache_adjust_top;
                if (cache_alloced == 0) {
                    /* Can not continue */
                    receiving_packets = 0;
                }
                superblock->fs_ronly = 1;
                return;
            }
            break;  /* Give up */
        }
        cache_add(node);
    }

    /* if the cache is too large */
    while (cache_alloced > cache_size) {
        node = cache_stack;
        if (node == NULL)
                break;
#ifndef RONLY
        while (cache_stack->flags != CACHE_CLEAN) {
            cache_refresh(cache_stack);
            if (cache_stack == node) {
                cache_flush();
                break;
            }
        }
#endif
        node = cache_stack;
        if (node == NULL)
            break;
        cache_remove(node);
        cache_free(node);
    }
}


/*
 * cache_cg_adjust()
 *      This routine is called whenever there is a size change for
 *      the number of cg cache entries.  It is responsible for deallocating
 *      (only) cache buffers as needed.
 */
void
cache_cg_adjust(void)
{
    int index;

    if (cache_cg_size < 3)
        cache_cg_size = 3;

    if (cache_cg_size >= CACHE_CG_MAX)
        cache_cg_size = CACHE_CG_MAX - 1;

    for (index = cache_cg_size; index < CACHE_CG_MAX; index++) {
        if (cache_cg_data[index] != NULL) {
#ifndef RONLY
            if (cache_cg_state[index]) {
                ULONG cgx = DISK32(cache_cg_data[index]->cg_cgx);
                if (data_write(cache_cg_data[index], cgtod(superblock, cgx),
                               FBSIZE))
                    PRINT2(("** data write fault for cache cg %d\n", cgx));
            }
#endif
            FreeMem(cache_cg_data[index], FBSIZE);
            cache_cg_state[index] = 0;
            cache_cg_data[index]  = NULL;
        }
    }
}


/*
 * close_cache()
 *      This routine is called to deallocate all items in the cache
 *      and return the memory allocated to the cache buffers to the
 *      system free pool.
 */
void
close_cache(void)
{
    int     index;
    cache_set_t *temp;

    UPSTAT(cache_destroys);

    /* free the list of cache blocks */
    while (cache_stack != NULL) {
        if (cache_stack->flags & CACHE_DIRTY)
            PRINT2(("destroying dirty %u\n", cache_stack->blk));
        temp = cache_stack->stack_down;
        cache_free(cache_stack);
        cache_stack = temp;
    }

    /* clean the frag hash table */
    for (index = 0; index < HASH_SIZE; index++)
            hashtable[index] = NULL;

    /* check the cylinder group summary cache */
    for (index = 0; index < CACHE_CG_MAX; index++)
        if (cache_cg_data[index] != NULL) {
            FreeMem(cache_cg_data[index], FBSIZE);
            cache_cg_data[index] = NULL;
        }

    cache_item_dirty = 0;
    cache_stack_tail = NULL;
    cache_used       = 0;
}


#ifndef RONLY
/*
 * cache_flush()
 *      This routine will attempt to flush all DIRTY entries in
 *      the cache out to disk (in sorted order).  It is called
 *      by external routines to synchronize the disk.
 */
int
cache_flush(void)
{
    cache_set_t *head;
    cache_set_t *temp;
    int          flushed = 0;

    if (cache_item_dirty == 0) {
        /* Nothing needs to be flushed to disk */
        return;
    }

    UPSTAT(cache_flushes);

    PRINT(("cache_flush: flushing "));
    temp = cache_stack;
    head = NULL;
    while (temp != NULL) {
        if (temp->flags & CACHE_DIRTY) {
#ifndef FAST
            if (temp->blk == 0) {
                PRINT2(("BUG: bad dirty block\n"));
                receiving_packets = 0;
                temp = temp->stack_down;
                continue;
            }
#endif
            PRINT(("%d ", temp->blk));
            cache_insert(&head, temp); /* make our own dirty list */
            flushed++;
        }
        temp = temp->stack_down;
    }
    PRINT(("(tot=%d, exp=%d)\n", flushed, cache_item_dirty));
#ifndef FAST
    if (cache_item_dirty != flushed) {
        PRINT2(("BUG: cache dirty to flush %d != expected %d\n",
                flushed, cache_item_dirty));
        receiving_packets = 0;
    }
#endif
    /* Now that they are all collected and sorted, write 'em */
    while (head != NULL) {
        head->flags = CACHE_CLEAN;
        if (data_write(head->buf, head->blk, FSIZE))
            PRINT2(("** data write fault for cache flush blk %u\n",
                    head->blk));
        head = head->next;
    }
    cache_item_dirty = 0;

    return (flushed);
}
#endif


/*
 * cache_insert()
 *      This routine maintains an ordered list of cache blocks which
 *      will be used by the flush routine to accomplish an elevator-
 *      type synchronization of dirty cache frags to the disk.  The
 *      list is maintained in order from lowest block number to highest.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_insert(cache_set_t **head, cache_set_t *node)
{
    cache_set_t *current;
    cache_set_t *parent = *head;

    if ((parent == NULL) || (parent->blk >= node->blk)) {
        /* Insert at head */
        node->next = parent;
        *head = node;
        return;
    }

    current = parent->next;
    while (current != NULL) {
        if (current->blk >= node->blk)
            break;
        parent = current;
        current = current->next;
    }

    /* Insert anywhere past head */
    node->next = current;
    parent->next = node;
}


/*
 * cache_frag_move()
 *      This routine uses the cache to quickly move a disk frag from one
 *      location to another.  It first gets the fragment into a cache
 *      buffer.  Then it removes it from the cache, changes its destination
 *      block, marks it dirty, and adds it back to the cache.
 *
 *      This routine is called by external routines (such as the disk
 *      frag allocators) to efficiently move disk blocks that are most
 *      likely already in the cache.
 */
void
cache_frag_move(ULONG to_blk, ULONG from_blk)
{
    cache_set_t *node;

    PRINT(("cache move from %u to %u\n", from_blk, to_blk));

    UPSTAT(cache_moves);

    if ((node = cache_node(from_blk)) != NULL) {
        cache_remove(node);
    } else {
        node = cache_getnode(from_blk);
        if (node == NULL) {
            superblock->fs_ronly = 1;
            return;
        }
        if (data_read(node->buf, node->blk, FSIZE))
            PRINT2(("** cache_frag_move %u data read fault\n", node->blk));
    }

    node->blk = to_blk;
    if (!(node->flags & CACHE_DIRTY)) {
        node->flags = CACHE_DIRTY;
        cache_item_dirty++;
    }
    cache_add(node);
}


/*
 * cache_alloc()
 *      This routine will allocate memory for a new cache node and
 *      set that node's flags accordingly.  The node is returned
 *      to the calling routine.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static cache_set_t *
cache_alloc(void)
{
    cache_set_t *node;

    node = (cache_set_t *)
           AllocMem(sizeof (cache_set_t), MEMF_PUBLIC);

    if (node != NULL) {
        node->flags = CACHE_CLEAN;
        node->buf   = (char *) AllocMem(FSIZE, MEMF_PUBLIC);
        node->blk   = 0;
        if (node->buf == NULL) {
            FreeMem(node, sizeof (cache_set_t));
            return (NULL);
        }
        cache_alloced++;
    }
    return (node);
}


/*
 * cache_free()
 *      This routine will deallocate a cache node which must have
 *      been already removed from the cache.  The memory is returned
 *      to the system free pool.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_free(cache_set_t *node)
{
#ifndef FAST
    if (node == NULL)
        return;
#endif

    cache_alloced--;
    FreeMem(node->buf, FSIZE);
    FreeMem(node, sizeof (cache_set_t));
}


/*
 * cache_add()
 *      This routine will add the specified node to both the cache hash
 *      table and also the cache sequential table (stack).  The node's
 *      fragment address MUST have already been set.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_add(cache_set_t *node)
{
    int index;

#ifndef FAST
    if (node == NULL) {
        PRINT2(("cache_add got NULL node\n"));
        return;
    }
#endif

    /* stack add */
    node->stack_up   = cache_stack_tail;
    node->stack_down = NULL;
    if (cache_stack_tail == NULL)  /* inserting at head */
        cache_stack = node;
    else
        cache_stack_tail->stack_down = node;
    cache_stack_tail = node;


    /* hash add */
    index = CACHE_HASH(node->blk);
    if (hashtable[index] != NULL)
        hashtable[index]->hash_up = node;

    node->hash_up = NULL;
    node->hash_down = hashtable[index];
    hashtable[index] = node;

    if (node->blk)
        cache_used++;
}


/*
 * cache_remove()
 *      This routine will remove the specified node from both the
 *      cache hash table and the cache sequential frag list.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_remove(cache_set_t *node)
{
    int index;

#ifndef FAST
    if (node == NULL) {
        PRINT2(("cache_remove got NULL node\n"));
        return;
    }
#endif

    /* stack remove */
    if (node->stack_up == NULL)     /* head of stack */
        cache_stack = node->stack_down;
    else
        node->stack_up->stack_down = node->stack_down;

    if (node->stack_down == NULL)   /* tail of stack */
        cache_stack_tail = node->stack_up;
    else
        node->stack_down->stack_up = node->stack_up;


    /* hash remove */
    if (node->hash_up == NULL) {
        index = CACHE_HASH(node->blk);
        hashtable[index] = node->hash_down;
    } else {
        node->hash_up->hash_down = node->hash_down;
    }

    if (node->hash_down != NULL)
        node->hash_down->hash_up = node->hash_up;

    if (node->blk)
        cache_used--;
}


/*
 * cache_refresh()
 *      This routine moves the passed node back to the bottom
 *      of the stack.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_refresh(cache_set_t *node)
{
#ifndef FAST
    if (node == NULL) {
        PRINT2(("cache_refresh given NULL node\n"));
        return;
    }
#endif

    if (node == cache_stack_tail)   /* already there */
        return;

    /* stack remove */
    if (node->stack_up == NULL)     /* if (node == cache_stack) */
        cache_stack = node->stack_down;
    else
        node->stack_up->stack_down = node->stack_down;
    node->stack_down->stack_up = node->stack_up;    /* stack_down != NULL */

    /* stack add */
    node->stack_up   = cache_stack_tail;
    node->stack_down = NULL;
    cache_stack_tail->stack_down = node;
    cache_stack_tail = node;
}


/*
 * cache_node()
 *      This routine will return the node address of the specified
 *      disk fragment if it exists in the cache.  If it does not
 *      exist in the cache, NULL will be returned.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static cache_set_t *
cache_node(ULONG blk)
{
    cache_set_t *temp;

    temp = hashtable[CACHE_HASH(blk)];
    while (temp != NULL) {
        if (temp->blk == blk)
            break;
        temp = temp->hash_down;
    }

    return (temp);
}


/*
 * cache_cg()
 *      This routine will return a pointer to a copy of the specified
 *      cylinder group summary information from the disk.  The CG LRU
 *      table is updated to reflect this cg has been the most recent
 *      request.
 */
struct cg *
cache_cg(ULONG cgx)
{
    int index;
    struct cg *entry;

#ifndef FAST
    if (cgx > DISK32(superblock->fs_ncg)) {
        PRINT2(("INCON: cg %u too large\n", cgx));
        superblock->fs_ronly = 1;
        return (NULL);
    }
#endif

    for (index = 0; index < cache_cg_size; index++) {
        entry = cache_cg_data[index];
        if (entry == NULL) {
            UPSTAT(miss_cg_read);
            entry = (struct cg *) AllocMem(FBSIZE, MEMF_PUBLIC);
            if (entry == NULL) {
                PRINT2(("ERROR: unable to alloc cg buffer\n"));
                return (NULL);
            }
            cache_cg_data[index]  = entry;
            cache_cg_state[index] = 0;
            break;
        } else if (DISK32(cache_cg_data[index]->cg_cgx) == cgx) {
            UPSTAT(hit_cg_read);
            goto refresh;
        }
    }

    UPSTAT(miss_cg_read);

    if (index == cache_cg_size)
        index = 0;

    entry = cache_cg_data[index];

#ifndef RONLY
    if (cache_cg_state[index])  /* flush dirty cg blocks */
        cache_cg_flush();
#endif

    PRINT(("caching cg %d - ", cgx));
    if (data_read(entry, cgtod(superblock, cgx), FBSIZE)) {
        PRINT2(("** data read fault for cache cg %u\n", cgx));
        entry->cg_cgx = -1;
        return (NULL);
    }
    cache_cg_state[index] = 0;

refresh:

    cache_popup_cg(index);
    return (entry);
}


/*
 * cache_popup_cg()
 *      This routine is called to move the specified cylinder group
 *      number to the top (most recently used) of the CG LRU.
 *
 *      This is an internal cache routine and should not be called
 *      from outside the cache code.
 */
static void
cache_popup_cg(int pos)
{
    int index;
    int        save_state;
    struct cg *save_buf;

    if (cache_cg_data[pos + 1] == NULL)
        return;

    save_buf   = cache_cg_data[pos];
    save_state = cache_cg_state[pos];

    for (index = pos + 1; index < CACHE_CG_MAX; index++) {
        if (cache_cg_data[index] == NULL) {
            break;
        } else {
            cache_cg_data[index - 1]  = cache_cg_data[index];
            cache_cg_state[index - 1] = cache_cg_state[index];
        }
    }

    cache_cg_data[index - 1]  = save_buf;
    cache_cg_state[index - 1] = save_state;
}

#ifndef RONLY
/*
 * cache_cg_write()
 *      This routine will return a pointer to a copy of the specified
 *      cylinder group summary information from the disk.  The CG LRU
 *      table is updated to reflect this cg has been the most recent
 *      request.  The cg is marked as dirty and will be flushed to
 *      disk automatically the next time the cg flush routine is called.
 */
struct cg *
cache_cg_write(ULONG cgx)
{
    int index;
    int count = 0;
    struct cg *entry;

    for (index = 0; index < cache_cg_size; index++) {
        entry = cache_cg_data[index];
        if (entry && (cgx == DISK32(entry->cg_cgx))) {
            UPSTAT(hit_cg_write);
            cache_cg_state[index] = 1;
            return (entry);
        }
    }

cache_cg_write_top:
    entry = cache_cg(cgx);
    if (entry == NULL) {
        if (count++ < 2) {
            PRINT2(("cache_cg was unable to cache cg %u\n", cgx));
            goto cache_cg_write_top;
        }
        return (NULL);
    }

    for (index = 0; index < cache_cg_size; index++) {
        if (entry == cache_cg_data[index]) {
            UPSTAT(miss_cg_write);
            cache_cg_state[index] = 1;
            return (entry);
        }
    }

    PRINT2(("BUG: Unable to find cg %u cache entry\n", cgx));
    receiving_packets = 0;
    return (NULL);
}


/*
 * cache_cg_flush()
 *      This routine is called to synchronize the disk with all
 *      dirty cylinder group summary information currently in the
 *      CG cache.
 */
int
cache_cg_flush(void)
{
    int index;
    int count = 0;

    for (index = 0; index < CACHE_CG_MAX; index++) {
        if (cache_cg_state[index]) {        /* dirty */
            struct cg *cgp = cache_cg_data[index];
            ULONG      cgx = DISK32(cgp->cg_cgx);
            PRINT(("flush cg %u - ", cgx));
            UPSTAT(cache_cg_flushes);
            if (data_write(cgp, cgtod(superblock, cgx), FBSIZE)) {
                PRINT2(("** data write fault for cache cg %u\n", cgx));
                continue;
            }
            cache_cg_state[index] = 0;
            count++;
        }
    }
    return (count);
}
#endif
