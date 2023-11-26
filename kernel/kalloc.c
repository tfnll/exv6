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
static struct kmem_percpu *kmem_get(void);
static struct run *kmem_steal(void);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

/*
 * There are eight CPUs running in xv6. Each one must reserve it's own freelist
 * and lock. Each kmem lock is named kmem_$(CPU_NUMBER).
 */
char *KMEM_CPU_LOCKNAMES[NCPU] = {
	"kmem_0",
	"kmem_1",
	"kmem_2",
	"kmem_3",
	"kmem_4",
	"kmem_5",
	"kmem_6",
	"kmem_7"
};

struct run {
  struct run *next;
};

/*
 * Each CPU can maintain its own freelist and lock, and also must keep track of
 * the number of free pages it has in its freelist.
 */
struct kmem_percpu {
	struct spinlock lock;
	struct run *freelist;
	uint64 nfree;
};

struct {
  struct kmem_percpu cpus[NCPU];
  uint refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

// Paging is not yet turned on. Initialize the physcial page allocator.
void
kinit()
{
	for (int i = 0; i < NCPU; i++)
		initlock(&kmem.cpus[i].lock, KMEM_CPU_LOCKNAMES[i]);

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
  struct kmem_percpu *cpu;

  cpu = kmem_get();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&cpu->lock);
  r->next = cpu->freelist;
  cpu->freelist = r;
  cpu->nfree++;
  release(&cpu->lock);
}

/*
 * Allocate one 4096-byte page of physical memory from the current CPU's
 * freelist. Returns a pointer that the kernel can use. Returns 0 if the memory
 * cannot be allocated.
 */
void *
kalloc(void)
{
  struct run *r;
  struct kmem_percpu *cpu;

  cpu = kmem_get();

  acquire(&cpu->lock);
  r = cpu->freelist;
  if(r){
    cpu->freelist = r->next;
    cpu->nfree--;
  } else
	/*
	 * There was no physical page found in the current CPU's freelist. Steal
	 * a page from another CPU's freelist.
	 */
	r = kmem_steal();

  release(&cpu->lock);

  if(r) {
    memset((char*)r, 0, PGSIZE); // Zero-out the page frame.
    kalloc_refcnt_add(r);
  }
  return (void*)r;
}

uint64
sys_nfree(void)
{
  struct kmem_percpu *cpu;

  cpu = kmem_get();

  return cpu->nfree;
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
	struct kmem_percpu *cpu;

	cpu = kmem_get();

	idx = kalloc_refcnt_idx(pa);
	if (idx < 0)
		return;

	acquire(&cpu->lock);
	kmem.refcnt[idx]++;
	release(&cpu->lock);
}

void
kalloc_refcnt_dec(void *pa)
{
	uint idx;
	struct kmem_percpu *cpu;

	cpu = kmem_get();

	idx = kalloc_refcnt_idx(pa);
	if (idx < 0)
		return;

	if (kmem.refcnt[idx] == 0)
		panic("kalloc_refcnt_dec");

	acquire(&cpu->lock);
	kmem.refcnt[idx]--;
	if (kmem.refcnt[idx] == 0) {
		release(&cpu->lock);
		kfree(pa);
	} else
		release(&cpu->lock);
}

/*
 * Get the current CPU's kmem freelist.
 */
static
struct kmem_percpu *
kmem_get(void)
{
	int id;

	/*
	 * Device interrupts must be disabled before cpuid() can be called.
	 */
	push_off();

	id = cpuid();

	/*
	 * Turn device interrupts back on.
	 */
	pop_off();

	return &kmem.cpus[id];
}

/*
 * Steal a page from a CPU's kmem freelist.
 */
static
struct run *
kmem_steal(void)
{
	struct run *r;
	struct kmem_percpu *cpu;

	r = 0;

	for (int i = 0; i < NCPU; i++) {
		cpu = &kmem.cpus[i];
		if (cpu->freelist) {
			acquire(&cpu->lock);
			if (!cpu->freelist) {
				release(&cpu->lock);
				continue;
			}

			r = cpu->freelist;
			cpu->freelist = r->next;
			cpu->nfree--;

			release(&cpu->lock);
			break;
		}
	}

	return r;
}
