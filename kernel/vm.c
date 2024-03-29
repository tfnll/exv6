#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "mmap.h"

#define NUM_PTE 512

static void vmprint_helper(pagetable_t, int);
static void vmprint_pte(pte_t, int, int);

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void print(pagetable_t);

/*
 * create a direct-map page table for the kernel and
 * turn on paging. called early, in supervisor mode.
 * the page allocator is already initialized.
 */
void
kvminit()
{
  // Allocate a page of physical memory to hold the root page-table page.
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // map uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // map virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // map PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..39 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..12 -- 12 bits of byte offset within the page.
static pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove mappings from a page table. The mappings in
// the given range must exist. Optionally free the
// physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 size, int do_free)
{
  uint64 a, last;
  pte_t *pte;
  uint64 pa;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 0)) == 0)
      goto next;

    if((*pte & PTE_V) == 0)
	goto next;

    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      pa = PTE2PA(*pte);
      kalloc_refcnt_dec((void*)pa);
    }
    *pte = 0;
next:
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
}

// create an empty user page table.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    panic("uvmcreate: out of memory");
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kalloc_refcnt_dec(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  uint64 newup = PGROUNDUP(newsz);
  if(newup < PGROUNDUP(oldsz))
    uvmunmap(pagetable, newup, oldsz - newup, 1);

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kalloc_refcnt_dec((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, 0, sz, 1);
  freewalk(pagetable);
}

/*
 * Given a parent process's page table, copy its memory into a child's page
 * table. Copies both the page table and the physical memory. returns 0 on
 * success, -1 on failure. Frees any allocated pages on failure.
 */
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
	pte_t *pte;
	uint64 pa, i;
	uint flags;

	for (i = 0; i < sz; i += PGSIZE){
		if ((pte = walk(old, i, 0)) == 0)
			continue;

		if ((*pte & PTE_V) == 0)
			continue;

		pa = PTE2PA(*pte);
		flags = PTE_FLAGS(*pte);

		/*
		 * For copy-on-write pages:
		 *
		 * 1) Writing should be disallowed, as there will be a page
		 *    fault when a process tries to write to the page (which the
		 *    kernel can then use the make a copy of the page).
		 *
		 * 2) The PTE_C bit should be set, as the kernel will be able to
		 *    distinguish a copy-on-write page from a page that just
		 *    shouldn't be written to (such as a code page).
		 */
		flags &= ~PTE_W;
		flags |= PTE_C;

		/*
		 * Map the "old" page table's underlying physical page to the
		 * "new" page table (in essence, these two page tables will now.
		 * share their underlying physical memory).
		 */
		if (mappages(new, i, PGSIZE, (uint64) pa, flags) != 0)
			goto err;

		/*
		 * Remap this page back to the "old" page table, but with new
		 * PTE permissions indicating that the page is now
		 * copy-on-write.
		 */
		uvmunmap(old, i, PGSIZE, 0);

		if (mappages(old, i, PGSIZE, (uint64) pa, flags) != 0)
			goto err;

		kalloc_refcnt_add((void *) pa);
	}

	return 0;

err:
	uvmunmap(new, 0, i, 1);

	return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

/*
 * Copy from kernel to user. Copy len bytes from src to virtual address dstva in
 * a given page table. Return 0 on success, -1 on error.
 */
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
	int ret, flags, valid, writable, cow;
	uint64 n, va0, pa0;
	pte_t *pte;
	void *phys;

	while(len > 0) {
		va0 = PGROUNDDOWN(dstva);
		if (va0 > MAXVA)
			return -1;

		pte = walk(pagetable, va0, 0);
		if (pte == 0) {
			/*
			 * The user VM page that the kernel is writing to is not
			 * yet mapped (i.e. it's a page that should be
			 * lazy-allocated).
			 */
			phys = kalloc();
			if (phys == 0)
				return -1;

			flags = PTE_W | PTE_R | PTE_X | PTE_U;

			ret = mappages(pagetable, va0, PGSIZE, (uint64) phys,
				flags);
			if (ret < 0) {
				kalloc_refcnt_dec(phys);
				return -1;
			}

			pa0 = (uint64) phys;
		} else {
			pa0 = walkaddr(pagetable, va0);

			/*
			 * The PTE exists for this user VM page. Check whether
			 * it's a copy-on-write page. If so, allocate a new page
			 * frame and copy the contents of the original shared
			 * page frame to it.
			 */
			flags = PTE_FLAGS(*pte);
			valid = flags & PTE_V;
			writable = flags & PTE_W;
			cow = flags & PTE_C;
			if (valid && !writable && cow) {
				phys = kalloc();
				if (phys == 0)
					goto end;

				/*
				 * The new page will no longer be copy-on-write.
				 * Allow for writing and disable the
				 * copy-on-write identifier.
				 */
				flags |= PTE_W;
				flags &= ~PTE_C;

				/*
				 * Copy the contents of the shared page frame to
				 * the new private page frame and swap the two
				 * in the page table.
				 */
				memmove(phys, (void *) pa0, PGSIZE);

				uvmunmap(pagetable, va0, PGSIZE, 1);

				ret = mappages(pagetable, va0, PGSIZE,
					(uint64) phys, flags);
				if (ret < 0) {
					kalloc_refcnt_dec((void *) phys);
					return -1;
				}

				pa0 = (uint64) phys;
			}
		}

end:
		n = PGSIZE - (dstva - va0);
		if (n > len)
			n = len;

		if (pa0 != 0)
			memmove((void *)(pa0 + (dstva - va0)), src, n);

		len -= n;
		src += n;
		dstva = va0 + PGSIZE;
	}

	return 0;
}

/*
 * Copy from user to kernel. Copy len bytes to dst from virtual address srcva in
 * a given page table. Return 0 on success, -1 on error.
 */
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
	int ret, flags;
	uint64 n, va0, pa0;
	pte_t *pte;
	void *phys;

	while (len > 0) {
		va0 = PGROUNDDOWN(srcva);
		if (va0 > MAXVA)
			return -1;

		pte = walk(pagetable, va0, 0);
		if (pte == 0) {
			/*
			 * The user VM page that the kernel is writing to is not
			 * yet mapped (i.e. it's a page that should be
			 * lazy-allocated).
			 */
			phys = kalloc();
			if (phys == 0)
				return -1;

			flags = PTE_W | PTE_R | PTE_X | PTE_U;

			ret = mappages(pagetable, va0, PGSIZE, (uint64) phys,
				flags);
			if (ret < 0) {
				kalloc_refcnt_dec(phys);
				return -1;
			}

			pa0 = (uint64) phys;
		} else
			pa0 = walkaddr(pagetable, va0);

		n = PGSIZE - (srcva - va0);
		if (n > len)
			n = len;

		if (pa0 != 0)
			memmove(dst, (void *)(pa0 + (srcva - va0)), n);

		len -= n;
		dst += n;
		srcva = va0 + PGSIZE;
	}

	return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

// Print the contents of a given page table.
void
vmprint(pagetable_t table)
{
	printf("page table (address: %p)\n", table);
	vmprint_helper(table, 0);
}

static void
vmprint_helper(pagetable_t pagetable, int level)
{
	pte_t pte;
	uint64 child;
	int is_valid, is_table;

	for (int i = 0; i < NUM_PTE; i++) {
		pte = pagetable[i];

		is_valid = pte & PTE_V;

		if (is_valid) {
			vmprint_pte(pte, i, level + 1);

			is_table = (pte & (PTE_R | PTE_W | PTE_X)) == 0;

			if (is_table) {
				child = PTE2PA(pte);
				vmprint_helper((pagetable_t) child, level + 1);
			}
		}
	}
}

/*
 * Print the contents of a PTE in the vmprint() format.
 */
static void
vmprint_pte(pte_t entry, int count, int level)
{
	for (int i = 0; i < level; i++)
		printf(" ..");

	printf("%d: pte %p pa %p\n", count, entry, PTE2PA(entry));
}

/*
 * Handle a process's page fault by allocating memory for the faulting page and
 * mapping it to the process's virtual address space (at the faulting virtual
 * page's boundary).
 */
int
uvm_handle_page_fault(struct proc *p, uint64 fault_va)
{
	int ret, perms, guard, valid, cow, writable;
	void *phys_pg;
	uint64 vm_pg;
	pte_t *pte;
	struct mmap_info *info;

	/*
	 * If the faulting address is larger than the process address space's
	 * size, it's an invalid access.
	 */
	if (fault_va >= p->sz)
		return -1;

	/*
	 * Since the faulting virtual address may not be aligned on a page
	 * boundary, use PGROUNDDOWN(va) to find the starting address of the
	 * faulting virtual page.
	 */
	vm_pg = PGROUNDDOWN(fault_va);

	/*
	 * Get the PTE of the virtual memory page and check it's not the
	 * process's stack guard page (i.e. PTE_V && !PTE_U) or a copy-on-write
	 * page.
	 */
	pte = walk(p->pagetable, vm_pg, 0);
	if (pte != 0) {
		/*
		 * Check if the page is the stack guard page.
		 */
		guard = *pte & PTE_V;
		guard &= !(*pte & PTE_U);

		if (guard) {
			/*
			 * The PTE is valid yet not user-accessible, which are
			 * the permissions for the stack guard page.
			 */
			return -1;
		}

		/*
		 * Check if the page is a copy-on-write page. If it is, allocate
		 * a new page frame and map it in the place of the copy-on-write
		 * page.
		 */
		valid = *pte & PTE_V;
		writable = *pte & PTE_W;
		cow = *pte & PTE_C;
		if (valid && !writable && cow) {
			phys_pg = kalloc();
			if (phys_pg == 0)
				return -1;

			memmove(phys_pg, (void *) PTE2PA(*pte), PGSIZE);

			/*
			 * The page is no longer copy-on-write. Enable writing
			 * and disable the COW identifier.
			 */
			perms = PTE_FLAGS(*pte);
			perms |= PTE_W;
			perms &= ~PTE_C;

			/*
			 * Swap the shared page frame with the new, "owned" page
			 * frame.
			 */
			uvmunmap(p->pagetable, vm_pg, PGSIZE, 1);

			ret = mappages(p->pagetable, vm_pg, PGSIZE,
				(uint64) phys_pg, perms);
			if (ret < 0) {
				kalloc_refcnt_dec(phys_pg);
				return -1;
			}

			return 0;
		}
	}

	/*
	 * There is no PTE mapping for this virtual memory address (i.e. it is
	 * to be lazy-allocated and mapped). Allocate a page of physical memory
	 * and zero it.
	 */
	phys_pg = kalloc();
	if (phys_pg == 0)
		return -1;
	memset(phys_pg, 0, PGSIZE);

	info = mmap_info_get(p, vm_pg);
	if (info)
		return mmap_pagefault_handle(info, vm_pg, phys_pg);

	/*
	 * Set the permissions for the newly-allocated virtual page.
	 */
	perms = PTE_W | PTE_X | PTE_R | PTE_U;

	/*
	 * Map the allocated physical page to the faulting virtual page.
	 */
	ret = mappages(p->pagetable, vm_pg, PGSIZE, (uint64) phys_pg, perms);
	if (ret != 0) {
		kalloc_refcnt_dec(phys_pg);
		return -1;
	}

	return 0;
}
