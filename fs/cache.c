#include <exec/memory.h>
#include <dos/filehandler.h>

#include "config.h"

#include "superblock.h"
#include "ufs.h"
#include "fsmacros.h"
#include "cache.h"
#include "file.h"
#include "handler.h"
#include "stat.h"

#define HASH_SIZE	32	/* lines */
#define CACHE_CG_MAX	16	/* max cgs cacheable */

struct	cache_set *hashtable[HASH_SIZE];
struct	cg *cache_cg_data[CACHE_CG_MAX];
int	cache_cg_state[CACHE_CG_MAX];
struct	cache_set *cache_stack;
struct	cache_set *cache_stack_tail;
int	cache_alloced	    = 0;
int	cache_used	    = 0;
int	cache_item_dirty    = 0;
extern int cache_size;		/* initial cache frags if 0 in mountlist */
extern int cache_cg_size;	/* number of cylinder group fsblock buffers */

#define CACHE_HASH(blk) 	(blk & (HASH_SIZE - 1))
/*			was	((blk >> 4) & (HASH_SIZE - 1)) */



/* cache_frag()
 *	This routine will return a pointer to a copy of the specified
 *	frag from the disk.  It automatically adjusts the LRU table of
 *	so that it is more likely frequently requested blocks will
 *	remain in the cache.
 */
char *cache_frag(blk)
int blk;
{
	struct cache_set *node;

	if ((node = cache_node(blk)) != NULL) {
		UPSTAT(hit_data_read);
		cache_refresh(node);
	} else {
		UPSTAT(miss_data_read);
		node = cache_getnode(blk);
		if (data_read(node->buf, node->blk, FSIZE))
			PRINT2(("** data read fault for cache_frag\n"));
		cache_add(node);
	}
	return(node->buf);
}


char *cache_available(blk)
int blk;
{
	struct cache_set *node;

	if ((node = cache_node(blk)) != NULL) {
		UPSTAT(hit_data_read);
		cache_refresh(node);
		return(node->buf);
	}
	return(NULL);
}

#ifndef RONLY
/* cache_frag_write()
	This routine will return a buffer to write the given block into.
	The buffer is pre-set to be a dirty block.  The caching mechanism
	will automatically take care of flushing dirty frags once the
	buffer is full.  If readflag is set (non-zero), the calling routine
	is guaranteed the current contents of the fragment is read into the
	cache.  Otherwise, the buffer contents will be the fragment from the
	disk only if it is already in the cache.
*/
char *cache_frag_write(blk, readflag)
int blk;
int readflag;
{
	struct cache_set *node;

	if ((node = cache_node(blk)) != NULL) {
		UPSTAT(hit_data_write);
		cache_refresh(node);
	} else {
		node = cache_getnode(blk);
		if (readflag) {
			UPSTAT(miss_data_write);
			if (data_read(node->buf, node->blk, FSIZE))
				PRINT2(("** data read fault for cache_frag_write\n"));
		}
		cache_add(node);
	}
	if (!(node->flags & CACHE_DIRTY)) {
		node->flags |= CACHE_DIRTY;
		cache_item_dirty++;
	}
	return(node->buf);
}
#endif


/* cache_frag_lock()
 *	This routine will lock a frag in the cache.  It is used to
 *	alleviate the number of calls to cache_frag and cache_frag_write
 *	just to assure a frag remains in the cache.
 */
cache_frag_lock(blk)
int blk;
{
	struct cache_set *node;
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
	} else
		PRINT(("call to lock for frag %d not in cache!\n", blk));
}


/* cache_frag_unlock()
 *	This routine will unlock a locked frag in the cache
 */
cache_frag_unlock(blk)
int blk;
{
	struct cache_set *node;
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
	} else
		PRINT(("call to lock for frag %d not in cache!\n", blk));
}


/* cache_invalidate()
 *	This routine will throw out a cache entry for the passed block
 *	No flushing will be done for dirty frags.
 */
cache_invalidate(blk)
int blk;
{
	struct cache_set *node;

	if ((node = cache_node(blk)) != NULL) {
		UPSTAT(cache_invalidates);
		cache_remove(node);
		node->blk = 0;
		if (node->flags != CACHE_CLEAN) {
			if (node->flags & CACHE_DIRTY) {
			    if (node->flags & CACHE_LOCKED)
				PRINT2(("invalidating locked dirty frag! %d\n",
					blk));
			} else
			    PRINT2(("invalidating locked clean frag! %d\n", blk));
			node->flags = CACHE_CLEAN;
		}
		cache_add(node);
	}
}


/* cache_full_invalidate()
 *	This routine will eliminate all cached frags, similar to
 *	cache destroy, except the structure of the cache remains
 *	intact.  This routine does NOT flush dirty entries first.
 */
cache_full_invalidate()
{
	struct	cache_set *node;

	node = cache_stack;

	while (node != NULL) {
#ifdef FAST
		node->flags = CACHE_CLEAN;
#else
		if (node->flags != CACHE_CLEAN) {
			if (node->flags & CACHE_DIRTY) {
			    if (node->flags & CACHE_LOCKED)
				PRINT2(("invalidating locked dirty frag!\n"));
			} else
			    PRINT2(("invalidating locked clean frag!\n"));
			node->flags = CACHE_CLEAN;
		}
#endif
		node->blk = 0;
		node = node->stack_down;
	}
}


#ifndef RONLY
/* cache_frag_flush()
 *	This routine will flush a single block if it exists in the cache
 *	and is dirty.  It will not invalidate it from the cache.
 */
cache_frag_flush(blk)
int blk;
{
	struct cache_set *node;

	if ((node = cache_node(blk)) == NULL)
		return(0);

	if (node->flags & CACHE_DIRTY) {
		UPSTAT(cache_force_writes);
		if (node->flags & CACHE_LOCKED)
			PRINT2(("INCON: cache flush on locked frag %d\n", blk));
		node->flags = CACHE_CLEAN;
		PRINT(("cff: %d was dirty\n", blk));
		if (data_write(node->buf, node->blk, FSIZE))
			PRINT2(("** data write fault for cache frag flush\n"));
	}
	return(1);
}
#endif


/* cache_getnode()
 *	This routine will return the least recently used cache entry
 *	for new allocation into the cache.  The node is automatically
 *	disassociated (removed) from the cache before returning.
 *	The node is guaranteed to be in a clean state upon return
 *	from this routine.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
struct cache_set *cache_getnode(blk)
int blk;
{
	struct cache_set *node;

	cache_getnode_top:
	if (cache_stack->flags != CACHE_CLEAN)
		cache_cycle_dirty();     /* Put those dirty blocks on bottom */

	if ((node = cache_stack) == NULL) {
		PRINT(("ERROR: no cache buffers available!\n"));
/*		close_filesystem(); */
		goto cache_getnode_top;
	}
	cache_remove(node);
	node->blk   = blk;
	node->flags = CACHE_CLEAN;
	return(node);
}


/* cache_cycle_dirty()
 *	This routine is called by cache_getnode in order to move any
 *	dirty frags up to the most recently used position.  Eventually,
 *	those dirty frags should automatically end up on disk rather
 *	than cycling through the cache.  Perhaps there could be a tag
 *	saying how many times a dirty frag has been recycled.
 */
cache_cycle_dirty()
{
	struct cache_set *node;

	if (cache_stack == NULL) {
		PRINT(("Error: cycle dirty found NULL stack.\n"));
		return;
	}

	node = cache_stack;

	while (cache_stack->flags != CACHE_CLEAN) {
		cache_refresh(cache_stack);
		if (cache_stack == node)
			break;
	}

#ifndef RONLY
	if ((cache_stack->flags != CACHE_CLEAN) ||
	    (cache_item_dirty > ((cache_size * 3) >> 2))) {
		PRINT(("cycle dirty "));
		UPSTATVALUE(cache_force_writes, cache_size);
		cache_flush();   /* flush the entire cache if we can't
				    find one free frag. */

		while (cache_stack->flags != CACHE_CLEAN) {
			cache_refresh(cache_stack);
			if (cache_stack == node)
				break;
		}

		if (cache_stack->flags != CACHE_CLEAN)
			PRINT2(("** ERROR - all frags in cache locked!\n"));
	}
#endif
}


/* open_cache()
 *	This routine is called to initialize the data structures
 *	responsible for maintaining the cache.  It initializes both
 *	the regular fragment cache and the cg block cache.
 */
open_cache()
{
	int index;

	/* init the frag hash table */
	for (index = 0; index < HASH_SIZE; index++)
		hashtable[index] = NULL;

	/* init the cylinder group cache */
	for (index = 0; index < CACHE_CG_MAX; index++)
		cache_cg_data[index] = NULL;

	cache_stack	 = NULL;
	cache_stack_tail = NULL;
	cache_alloced	 = 0;
	cache_used	 = 0;
	cache_item_dirty = 0;
	cache_size	 = ENVIRONMENT->de_NumBuffers / NSPF(superblock);
	if (cache_size < 8)
		cache_size = 8;
	PRINT(("cache=%d  cache_cg=%d\n", cache_size, cache_cg_size));

	cache_adjust();
}


/* cache_adjust()
 *	This routine is called whenever there is a size change for
 *	the cache.  It is responsible for allocating and deallocating
 *	cache buffers as needed.
 */
cache_adjust()
{
	struct cache_set *node;

	/* if the cache is too small */
	while (cache_alloced < cache_size) {
		cache_adjust_top:
		if ((node = cache_alloc(CACHE_CLEAN)) == NULL)
		    if (cache_size > 2) {
			PRINT(("ERROR: unable to allocate cache buffers\n"));
/*			close_filesystem(); */
			goto cache_adjust_top;
		    } else
			break;
		node->blk = 0;
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


/* cache_cg_adjust()
 *	This routine is called whenever there is a size change for
 *	the number of cg cache entries.  It is responsible for deallocating
 *	(only) cache buffers as needed.
 */
cache_cg_adjust()
{
	int index;

	if (cache_cg_size < 3)
	    cache_cg_size = 3;

	if (cache_cg_size >= CACHE_CG_MAX)
	    cache_cg_size = CACHE_CG_MAX - 1;

	for (index = cache_cg_size; index < CACHE_CG_MAX; index++)
	    if (cache_cg_data[index] != NULL) {
#ifndef RONLY
		if (cache_cg_state[index])
		    if (data_write(cache_cg_data[index], cgtod(superblock,
				   cache_cg_data[index]->cg_cgx), FBSIZE))
			PRINT2(("** data write fault for cache cg %d\n",
				cache_cg_data[index]->cg_cgx));
#endif
		FreeMem(cache_cg_data[index], FBSIZE);
		cache_cg_state[index] = 0;
		cache_cg_data[index]  = NULL;
	    }
}


/* close_cache()
 *	This routine is called to deallocate all items in the cache
 *	and return the memory allocated to the cache buffers to the
 *	system free pool.
 */
close_cache()
{
	int	index;
	struct	cache_set *temp;

	UPSTAT(cache_destroys);

	/* free the list of cache blocks */
	while (cache_stack != NULL) {
		if (cache_stack->flags & CACHE_DIRTY)
			PRINT(("mess! destroying dirty %d\n", cache_stack->blk));
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

	cache_stack_tail = NULL;
	cache_used	 = 0;
}


#ifndef RONLY
/* cache_flush()
 *	This routine will attempt to flush all DIRTY entries in
 *	the cache out to disk (in sorted order).  It is called
 *	by external routines to synchronize the disk.
 */
cache_flush()
{
	struct cache_set *head;
	struct cache_set *temp;
	int flushed = 0;

	if (!cache_item_dirty) {
		PRINT(("cache_flush: no items dirty\n"));
		return(0);
	}

	UPSTAT(cache_flushes);

	PRINT(("cache_flush: flushing "));
	temp = cache_stack;
	head = NULL;
	while (temp != NULL) {
		if (temp->flags == CACHE_DIRTY) {
			if (temp->blk == 0) {
				PRINT2(("INCON: bad dirty block!\n"));
				temp = temp->stack_down;
				continue;
			}
			PRINT(("%d ", temp->blk));
			cache_insert(&head, temp);  /* make our own dirty list */
			flushed++;
		}
		temp = temp->stack_down;
	}
	PRINT(("(%d total, %d expected)\n", flushed, cache_item_dirty));
	cache_item_dirty = 0;

	/* Now that they are all collected and sorted, write 'em */
	while (head != NULL) {
		head->flags = CACHE_CLEAN;
		if (data_write(head->buf, head->blk, FSIZE))
			PRINT2(("** data write fault for cache flush\n"));
		head = head->next;
	}
}
#endif


/* cache_insert()
 *	This routine maintains an ordered list of cache blocks which
 *	will be used by the flush routine to accomplish an elevator-
 *	type synchromization of dirty cache frags to the disk.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_insert(head, node)
struct cache_set **head;
struct cache_set *node;
{
	struct cache_set *current;
	struct cache_set *parent;

	parent	= NULL;
	current	= *head;

	while (current != NULL)
		if (current->blk >= node->blk) {
			if (parent == NULL) {
				node->next = *head;
				*head = node;
			} else {
				node->next = current;
				parent->next = node;
			}
			break;
		} else {
			parent	= current;
			current = current->next;
		}

	if (current == NULL) {
		node->next = NULL;
		if (parent == NULL)
			*head = node;
		else
			parent->next = node;
	}
}


/* cache_frag_move()
 *	This routine uses the cache to quickly move a disk frag from one
 *	location to another.  It first gets the fragment into a cache
 *	buffer.  Then, it removes it from the cache, changes its destination
 *	address, marks it dirty, and adds it back to the cache.
 *	This routine is called by external routines (such as the disk
 *	frag allocators) to efficiently move disk blocks that are most
 *	likely in the cache already.
 */
cache_frag_move(to_blk, from_blk)
int to_blk;
int from_blk;
{
	struct cache_set *node;

	PRINT(("cache move from %d to %d\n", from_blk, to_blk));

	cache_invalidate(to_blk);

	UPSTAT(cache_moves);

	if ((node = cache_node(from_blk)) != NULL)
		cache_remove(node);
	else {
		node = cache_getnode(from_blk);
		if (data_read(node->buf, node->blk, FSIZE))
			PRINT2(("** data read fault for cache_frag_move\n"));
	}

	node->blk = to_blk;
	if (node->flags != CACHE_DIRTY) {
		node->flags = CACHE_DIRTY;
		cache_item_dirty++;
	}
	cache_add(node);
}


/* cache_alloc()
 *	This routine will allocate memory for a new cache node and
 *	set that node's flags accordingly.  The node is returned
 *	to the calling routine.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
struct cache_set *cache_alloc(flags)
int flags;
{
	struct cache_set *node;

	node = (struct cache_set *) AllocMem(sizeof(struct cache_set), MEMF_PUBLIC);
	if (node != NULL) {
		node->flags	 = flags;
		node->buf	 = (char *) AllocMem(FSIZE, MEMF_PUBLIC);
		if (node->buf == NULL) {
			FreeMem(node, sizeof(struct cache_set));
			return(NULL);
		}
	}
	cache_alloced++;
	return(node);
}


/* cache_free()
 *	This routine will deallocate a cache node which must have
 *	been already removed from the cache.  The memory is returned
 *	to the system free pool.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_free(node)
struct cache_set *node;
{
#ifndef FAST
	if (node == NULL)
		return;
#endif

	cache_alloced--;
	FreeMem(node->buf, FSIZE);
	FreeMem(node, sizeof(struct cache_set));
}


/* cache_add()
 *	This routine will add the specified node to both the cache hash
 *	table and also the cache sequential table.  The node's fragment
 *	address MUST have already been set.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_add(node)
struct cache_set *node;
{
	int index;

#ifndef FAST
	if (node == NULL) {
		PRINT(("cache_add got NULL node.\n"));
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


/* cache_remove()
 *	This routine will remove the specified node from both the
 *	cache hash table and the cache sequential frag list.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_remove(node)
struct cache_set *node;
{
	int index;

#ifndef FAST
	if (node == NULL) {
		PRINT(("cache_remove got NULL node.\n"));
		return;
	}
#endif

	/* stack remove */
	if (node->stack_up == NULL)	/* head of stack */
		cache_stack = node->stack_down;
	else
		node->stack_up->stack_down = node->stack_down;

	if (node->stack_down == NULL)	/* tail of stack */
		cache_stack_tail = node->stack_up;
	else
		node->stack_down->stack_up = node->stack_up;


	/* hash remove */
	if (node->hash_up == NULL) {
		index = CACHE_HASH(node->blk);
		hashtable[index] = node->hash_down;
	} else
		node->hash_up->hash_down = node->hash_down;

	if (node->hash_down != NULL)
		node->hash_down->hash_up = node->hash_up;

	if (node->blk)
		cache_used--;
}


/* cache_refresh()
 *	This routine moves the passed node back to the bottom
 *	of the stack.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_refresh(node)
struct cache_set *node;
{
#ifndef FAST
	if (node == NULL) {
		PRINT(("cache_refresh given NULL node.\n"));
		return;
	}
#endif

	if (node->stack_down == NULL)	/* already there */
		return;

	/* stack remove */
	if (node->stack_up == NULL)	/* if (node == cache_stack) */
		cache_stack = node->stack_down;
	else
		node->stack_up->stack_down = node->stack_down;
	node->stack_down->stack_up = node->stack_up;	/* stack_down != NULL */

	/* stack add */
	node->stack_up   = cache_stack_tail;
	node->stack_down = NULL;
	cache_stack_tail->stack_down = node;
	cache_stack_tail = node;
}


/* cache_node()
 *	This routine will return the node address of the specified
 *	disk fragment if it exists in the cache.  If it does not
 *	exist in the cache, NULL will be returned.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
struct cache_set *cache_node(blk)
int blk;
{
	int index;
	struct cache_set *temp;

	temp = hashtable[CACHE_HASH(blk)];
	while (temp != NULL) {
		if (temp->blk == blk)
			break;
		temp = temp->hash_down;
	}

	return(temp);
}


/* cache_cg()
 *	This routine will return a pointer to a copy of the specified
 *	cylinder group summary information from the disk.  The CG LRU
 *	table is updated to reflect this cg has been the most recent
 *	request.
 */
struct cg *cache_cg(cgx)
ULONG cgx;
{
	int index;
	struct cg *entry;

#ifndef FAST
	if (cgx > superblock->fs_ncg) {
		PRINT(("cg %d too large!\n"));
		return(NULL);
	}
#endif

	for (index = 0; index < cache_cg_size; index++)
	    if (cache_cg_data[index] == NULL) {
		UPSTAT(miss_cg_read);
		cache_cg_state[index] = 0;
		cache_cg_data[index] = (struct cg *) AllocMem(FBSIZE, MEMF_PUBLIC);
		if (cache_cg_data[index] == NULL) {
		    PRINT(("unable to alloc, returning NULL\n"));
		    return(NULL);
		}
		break;
	    } else if (cache_cg_data[index]->cg_cgx == cgx) {
		UPSTAT(hit_cg_read);
		entry = cache_cg_data[index];
		goto refresh;
	    }

	UPSTAT(miss_cg_read);

	if (index == cache_cg_size)
	    index = 0;

	entry = cache_cg_data[index];

#ifndef RONLY
	if (cache_cg_state[index]) 	/* flush dirty cg blocks */
	    cache_cg_flush();
#endif

	PRINT(("caching cg %d - ", cgx));
	if (data_read(entry, cgtod(superblock, cgx), FBSIZE))
		PRINT2(("** data read fault for cache cg %d\n", cgx));

	cache_cg_state[index] = 0;

	refresh:

	cache_popup_cg(index);
	return(entry);
}


/* cache_popup_cg()
 *	This routine is called to move the specified cylinder group
 *	number to the top (most recently used) of the CG LRU.
 *
 *	This is an internal cache routine and should not be called
 *	from outside the cache code.
 */
cache_popup_cg(pos)
int pos;
{
	int index;
	int	   save_state;
	struct cg *save_buf;

	if (cache_cg_data[pos + 1] == NULL)
		return;

	save_buf   = cache_cg_data[pos];
	save_state = cache_cg_state[pos];

	for (index = pos + 1; index < CACHE_CG_MAX; index++)
	    if (cache_cg_data[index] == NULL)
		break;
	    else {
		cache_cg_data[index - 1]  = cache_cg_data[index];
		cache_cg_state[index - 1] = cache_cg_state[index];
	    }

	cache_cg_data[index - 1]       = save_buf;
	cache_cg_state[index - 1] = save_state;
}

/* cache_cg_write()
 *	This routine will return a pointer to a copy of the specified
 *	cylinder group summary information from the disk.  The CG LRU
 *	table is updated to reflect this cg has been the most recent
 *	request.  The cg is marked as dirty and will be flushed to
 *	disk automatically the next time the cg flush routine is called.
 */
struct cg *cache_cg_write(cgx)
ULONG cgx;
{
	int index;
	struct cg *entry;

	for (index = 0; index < cache_cg_size; index++) {
		entry = cache_cg_data[index];
		if (entry && (cgx == entry->cg_cgx)) {
			UPSTAT(hit_cg_write);
			cache_cg_state[index] = 1;
			return(entry);
		}
	}

	cache_cg_write_top:
	entry = cache_cg(cgx);
	if (entry == NULL) {
		PRINT(("cache_cg was unable to cache cg!\n"));
/*		close_filesystem(); */
/*		close_ufs(0); */
		goto cache_cg_write_top;
	}

	for (index = 0; index < cache_cg_size; index++)
		if (entry == cache_cg_data[index]) {
			UPSTAT(miss_cg_write);
			cache_cg_state[index] = 1;
			return(entry);
		}

	PRINT2(("INCON: Unable to find cg cache entry!  Should not happen\n"));
	return(NULL);
}


#ifndef RONLY
/* cache_cg_flush()
 *	This routine is called to synchronize the disk with all
 *	dirty cylinder group summary information currently in the
 *	CG cache.
 */
cache_cg_flush()
{
	int index;

	for (index = 0; index < CACHE_CG_MAX; index++)
	    if (cache_cg_state[index]) {	/* dirty */
		PRINT(("flushing cg %d - ", cache_cg_data[index]->cg_cgx));
		UPSTAT(cache_cg_flushes);
		if (data_write(cache_cg_data[index], cgtod(superblock,
				cache_cg_data[index]->cg_cgx), FBSIZE))
			PRINT2(("** data write fault for cache cg %d\n",
				cache_cg_data[index]->cg_cgx));
		cache_cg_state[index] = 0;
	    }
}
#endif
