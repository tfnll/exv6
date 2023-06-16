/*
 * slab_alloc.c: Kernel slab allocator
 *
 * xv6 has only a page allocator and cannot dynamically allocate objects smaller
 * than a page. To work around this limitation, xv6 declares objects smaller
 * than a page statically. For example, xv6 declares an array of file
 * structures, an array of proc structures, and so on. As a result, the number
 * of files the system can have open is limited by the size of the statically
 * declared file array, which has NFILE entries (see kernel/file.c and
 * kernel/param.h).
 *
 * The solution is to adopt the slab allocator.
 *
 * The slab allocator builds on the page allocator and manages objects of a
 * specific size (e.g., file structures). It maintains a number of "slabs". For
 * simplicity, we assume that a slab spans exactly one page. Each slab contains
 * some metadata at the beginning and a number of objects after the metadata.
 *
 * For allocation, the slab allocator finds a slab that is not full and
 * allocates a free object from the slab. If all slabs are full, the slab
 * allocator may ask the page allocator for more free slabs (via kalloc). For
 * deallocation, the slab allocator gives an object back to the corresponding
 * slab. If all objects within a slab are free, the slab allocator may give the
 * slab back to the page allocator (via kfree).
 *
 * This implementation attempts to mimic the slab allocator discussed in Jeff
 * Bonwick's "The Slab Allocator: An Object-Caching Kernel Memory Allocator".
 * This paper can be found at:
 * https://www.usenix.org/legacy/publications/library/proceedings/bos94/bonwick.html
 */

#include "types.h"
#include "riscv.h"
#include "defs.h"

#define KMEM_CACHE_MAX 200

/*
 * Kernel caches can be statically initialized. For each cache, a corresponding
 * bitflag is needed to indicate whether or not the cache is currently in use
 * by some subsystem in the kernel.
 */
struct kmem_cache	KMEM_CACHES[KMEM_CACHE_MAX];
int			KMEM_CACHE_FLAGS[KMEM_CACHE_MAX];

static void kmem_cache_slab_zero(struct kmem_cache *);
static void *kmem_cache_slab_find(struct kmem_cache *);
static struct kmem_cache *KMEM_CACHES_RESERVE(void);
static int KMEM_CACHES_FREE(struct kmem_cache *);

/*
 * Allocate a new "clean" (i.e. no previous data contained) cache from
 * KMEM_CACHES and point to it with cp.
 */
int
kmem_cache_create(struct kmem_cache **cp, int align)
{
	struct kmem_cache *cache;

	/*
	 * Current caches only allocate 1 page for their slabs, so any object
	 * sizes larger than that are not currently allowed.
	 */
	if (align <= 0 || align > SLAB_LIM)
		return 0;

	/*
	 * Get a cache from KMEM_CACHES.
	 */
	cache = KMEM_CACHES_RESERVE();
	if (!cache)
		return 0;

	/*
	 * Ensure that all data is prepared for the kernel object that the cache
	 * will hold.
	 */
	cache->align = align;
	cache->slab = 0;
	cache->size = 0;
	cache->len = SLAB_LIM / align;
	cache->prev = 0;
	cache->next = 0;

	*cp = cache;

	return 1;
}

/*
 * Allocate an object from the current cache.
 */
void *
kmem_cache_alloc(struct kmem_cache *cp, int flags)
{
	int ret;
	void *obj;

	/*
	 * If the cache's size is 0, then the slab is NULL (i.e. has not been
	 * allocated yet). Allocate the slab with kalloc().
	 */
	if (cp->size == 0) {
		cp->slab = kalloc();
		if (!cp->slab)
			return 0;

		kmem_cache_slab_zero(cp);
	} else if (cp->size == cp->len) {
		/*
		 * The slab is full. Allocate the object from the next cache in
		 * the linked list.
		 */
		if (!cp->next) {
			/*
			 * The next cache in the list does not currently exist.
			 * Attempt to allocate (reserve) it and return NULL (0)
			 * if allocation fails.
			 */
			ret = kmem_cache_create(&cp->next, cp->align);
			if (!ret)
				return 0;

			/*
			 * Allocation was successful, ensure that the next cache
			 * understands which cache comes BEFORE it in the
			 * linked list (i.e. the current cache).
			 */
			cp->next->prev = cp;
		}

		/*
		 * Allocate the object from the next cache and return it.
		 */
		return kmem_cache_alloc(cp->next, cp->align);
	}

	/*
	 * Find a free space for the object in the cache's slab. If a free
	 * space doesn't exist (which shouldn't happen here, as we've already
	 * verified that 0 <= cp->size < cp->len, thus there is space in the
	 * slab), then return NULL (0).
	 */
	obj = kmem_cache_slab_find(cp);
	if (obj == 0)
		return 0;

	/*
	 * Update the cache's size and return the free space's location.
	 */
	cp->size++;

	return obj;
}

/*
 * Free an object from the current cache.
 */
void
kmem_cache_free(struct kmem_cache **cp, void *obj)
{
	int *p;
	struct kmem_cache *cache;
	void *ptr;

	cache = *cp;

	/*
	 * Iterate through this cache's slab and check if the slab currently
	 * holds the object to be freed.
	 */
	for (ptr = cache->slab; ptr < (cache->slab + SLAB_LIM); ptr += cache->align) {
		/*
		 * If the current position in the slab does not match the
		 * object's position, continue on.
		 */
		if (ptr != obj)
			continue;

		/*
		 * Set the first four bytes of the object in the cache to -1 to
		 * show it is a free space. Decrement the cache's size to show a
		 * slot was freed.
		 */
		p = (int *) obj;
		*p = -1;

		cache->size--;

		/*
		 * If it is shown that the cache no longer holds any more
		 * objects, free the cache's slab and deallocate the cache on
		 * KMEM_CACHES.
		 */
		if (cache->size < 0) {
			kalloc_refcnt_dec(cache->slab);
			KMEM_CACHES_FREE(cache);

			/*
			 * If the cache is in the middle of 2 nodes of the
			 * linked list, set cache->prev's next to cache->next
			 * and cache->next's prev to cache->prev. In effect,
			 * this will do the following:
			 *
			 * BEFORE:
			 *	prev <--> curr <--> next
			 *
			 * AFTER:
			 *	prev <--> next
			 *
			 * Thus, the current cache is now completely
			 * deallocated.
			 */
			if (cache->next && cache->prev) {
				cache->prev->next = cache->next;
				cache->next->prev = cache->prev;
			} else if (cache->next && !cache->prev)
				/*
				 * If this cache node is the current head of a
				 * linked list, update its pointer to point to
				 * cache->next (in effect, this will make
				 * cache->next the new head of the linked list).
				 */
				*cp = cache->next;
		}

		break;
	}

	/*
	 * The object wasn't found in the current cache, so move on to the next
	 * cache (if it exists).
	 */
	if (cache->next)
		kmem_cache_free(&cache->next, obj);
}

/*
 * Reserve a cache from KMEM_CACHES.
 */
static struct kmem_cache *
KMEM_CACHES_RESERVE(void)
{
	/*
	 * Traverse the KMEM_CACHES and KMEM_CACHE_FLAGS arrays, if a
	 * corresponding cache's bit flag is 0 (indicating the cache that the
	 * flag is representing is free), reserve the cache.
	 */
	for (int i = 0; i < KMEM_CACHE_MAX; i++) {
		/*
		 * If the cache is currently allocated, continue.
		 */
		if (KMEM_CACHE_FLAGS[i] == 1)
			continue;

		/*
		 * An unallocated cache has been reached. Set its corresponding
		 * bit flag to 1 (indicating it's currently taken) and return
		 * a pointer to the cache.
		 */
		KMEM_CACHE_FLAGS[i] = 1;

		return &KMEM_CACHES[i];
	}

	/*
	 * No free cache was found, return NULL (0).
	 */
	return 0;
}

/*
 * Return a cache to KMEM_CACHES for future use/allocation for others (or the
 * system returning the cache).
 */
static int
KMEM_CACHES_FREE(struct kmem_cache *cp)
{
	struct kmem_cache *cache_ptr;

	/*
	 * Traverse KMEM_CACHES and find which cache in the array corresponds
	 * to the one being returned.
	 */
	for (int i = 0; i < KMEM_CACHE_MAX; i++) {
		cache_ptr = &KMEM_CACHES[i];
		if (cp != cache_ptr)
			continue;

		/*
		 * The cache has been found in the array, set its corresponding
		 * bit flag to 0 (indicating the cache is now free) and return
		 * a success code.
		 */
		KMEM_CACHE_FLAGS[i] = 0;

		return 1;
	}

	/*
	 * The cache was not found. This should never happen for a cache
	 * allocated from KMEM_CACHES.
	 */
	return 0;
}

/*
 * When a new slab is allocated, each space in the slab must indicate that the
 * space in that position is free (indicated by setting the (int *) of an
 * object's space in the cache to -1).
 */
static void
kmem_cache_slab_zero(struct kmem_cache *cp)
{
	int *p;
	void *ptr;

	/*
	 * Go through each space for an object (aligned on a boundary specified
	 * by cp->align) in the slab and set the (int *) of the space to -1.
	 */
	for (ptr = cp->slab; ptr < (cp->slab + SLAB_LIM); ptr += cp->align) {
		p = (int *) ptr;
		*p = -1;
	}
}

/*
 * Find a free space within a slab in which a kernel object can be allocated
 * from.
 */
static void *
kmem_cache_slab_find(struct kmem_cache *cp)
{
	int *p;
	void *ptr;

	/*
	 * Go through each space for an object (aligned on a boundary specified
	 * by cp->align) in the slab.
	 */
	for (ptr = cp->slab; ptr < (cp->slab + SLAB_LIM); ptr += cp->align) {
		p = (int *) ptr;

		/*
		 * If the (int *) of the space is -1, then the space is
		 * currently free.
		 */
		if (*p == -1) {
			/*
			 * Set the (int *) of the space to 0 (indicating that
			 * the space is currently in-use) and return the space's
			 * location.
			 */
			*p = 0;
			return ptr;
		}
	}

	/*
	 * No free space was found. Return NULL (0).
	 */
	return 0;
}
