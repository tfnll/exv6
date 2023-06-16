// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
static uint kalloc_refcnt_idx(void *);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint64 nfree;
  uint refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

// Paging is not yet turned on. Initialize the physcial page allocator.
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.nfree++;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.nfree--;
  }
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    kalloc_refcnt_add(r);
  }
  return (void*)r;
}

uint64
sys_nfree(void)
{
  return kmem.nfree;
}

static uint
kalloc_refcnt_idx(void *pa)
{
	uint idx;
	uint64 begin, last;

	if ((char *) pa < end)
		return -1;

	begin = PGROUNDDOWN((uint64) pa);
	last = PGROUNDUP((uint64) end);

	idx = (begin - last) / PGSIZE;

	return idx;
}

void
kalloc_refcnt_add(void *pa)
{
	uint idx;

	idx = kalloc_refcnt_idx(pa);
	if (idx < 0)
		return;

	acquire(&kmem.lock);
	kmem.refcnt[idx]++;
	release(&kmem.lock);
}

void
kalloc_refcnt_dec(void *pa)
{
	uint idx;

	idx = kalloc_refcnt_idx(pa);
	if (idx < 0)
		return;

	if (kmem.refcnt[idx] == 0)
		panic("kalloc_refcnt_dec");

	acquire(&kmem.lock);
	kmem.refcnt[idx]--;
	if (kmem.refcnt[idx] == 0) {
		release(&kmem.lock);
		kfree(pa);
	} else
		release(&kmem.lock);
}
