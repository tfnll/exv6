#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "mmap.h"

#define MAP_FAILED ((uint64) -1)

#define PROT_READ	0x1	// Allow reading to a mapped file.
#define PROT_WRITE	0x10	// Allow writing to a mapped file.

#define MAP_SHARED	0x1	// Writes to file eventually written to disk.
#define MAP_PRIVATE	0x10	// Writes to file are not written to disk.

static int mmap_args_collect(size_t *, int *, int *, int *, struct file **,
				offset_t *);
static int munmap_args_collect(uint64 *, size_t *);
static int mmap_info_reserve(struct proc *, uint64, size_t, int, int,
				struct file *, offset_t);
static void mmap_info_free(struct mmap_info *);

/*
 * Memory-map a file the process' address space.
 *
 * Note that this syscall does not immediately map the file to the process'
 * address space. Rather, it stores the region's data and lazily maps the file's
 * pages on pagefaults.
 */
uint64
sys_mmap(void)
{
	int ret, prot, flags, fd;
	uint64 ret_addr, start;
	size_t len;
	struct file *file;
	offset_t offset;
	struct proc *p;

	ret_addr = MAP_FAILED;

	p = myproc();
	if (!p)
		goto out;

	ret = mmap_args_collect(&len, &prot, &flags, &fd, &file, &offset);
	if (ret < 0)
		goto out;

	/*
	 * Cannot allow reading of region if the file itself is not readable.
	 */
	if (!file->readable && (prot & PROT_READ))
		goto out;

	/*
	 * Cannot allow writing of region if the file itself is not writable.
	 * However, if the mapping is private, mmap can allow writing to the
	 * file, since these writes will not be propagated to the underlying
	 * file.
	 */
	if (!file->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE))
		goto out;

	/*
	 * Start the region on a page boundary.
	 */
	start = PGROUNDUP(p->sz);

	/*
	 * Reserve an mmap_region struct to allow the lazy mapping of file data
	 * on pagefaults.
	 */
	ret = mmap_info_reserve(p, start, len, prot, flags, file, offset);
	if (ret < 0)
		goto out;

	/*
	 * Indicate another user of the file.
	 */
	filedup(file);

	ret_addr = start;
	p->sz = start + len;
out:
	return ret_addr;
}

/*
 * Collect the mmap syscall arguments from the trap frame.
 */
static
int
mmap_args_collect(size_t *len, int *prot, int *flags, int *fd,
			struct file **file, offset_t *offset)
{
	int ret;

	ret = argaddr(1, len);
	if (ret < 0)
		return -1;

	ret = argint(2, prot);
	if (ret < 0)
		return -1;

	ret = argint(3, flags);
	if (ret < 0)
		return -1;

	ret = argfd(4, fd, file);
	if (ret < 0)
		return -1;

	ret = argaddr(5, offset);
	if (ret < 0)
		return -1;

	return 0;
}

/*
 * Unmap a memory-mapped region from a process' address space.
 */
uint64
sys_munmap(void)
{
	int ret;
	size_t len;
	uint64 vaddr_u64;
	struct mmap_info *info;
	struct proc *p;
	struct inode *ip;
	uint write_amount;

	ret = munmap_args_collect(&vaddr_u64, &len);
	if (ret < 0)
		return -1;

	/*
	 * Mapped regions are aligned on a page boundary. Align the faulting
	 * virtual address to a page boundary.
	 */
	vaddr_u64 = PGROUNDDOWN(vaddr_u64);

	p = myproc();
	if (!p)
		return -1;

	/*
	 * Fetch the mmap_region struct for the page's region. If one isn't
	 * found, it can be assumed that the page was not memory-mapped.
	 */
	info = mmap_info_get(p, vaddr_u64);
	if (!info)
		return -1;

	/*
	 * If the number of the region's pages mapped is zero, then this page
	 * was likely not mapped.
	 */
	if (info->num_pages == 0)
		return -1;

	/*
	 * Traverse each mapped page in the process' address space.
	 */
	for (int i = 0; i < len; i += PGSIZE, vaddr_u64 += PGSIZE) {
		/*
		 * If the mapping is shared, write the file's mappings back to
		 * disk. Any updates will thus be saved to the underlying file
		 * on disk.
		 */
		if (info->flags & MAP_SHARED) {
			ip = info->file->ip;
			write_amount = min(PGSIZE, info->len - vaddr_u64);

			begin_op();
			ilock(ip);
			ret = writei(ip, 1, vaddr_u64, i, write_amount);
			if (ret < 0) {
				iunlock(ip);
				end_op();

				return -1;
			}
			iunlock(ip);
			end_op();
		}

		/*
		 * Unmap the page from the process' address space.
		 */
		uvmunmap(p->pagetable, vaddr_u64, PGSIZE, 1);
		if ((--info->num_pages) == 0)
			mmap_info_free(info);
	}

	return 0;
}

/*
 * Collect munmap syscall arguments from the trap frame.
 */
static
int
munmap_args_collect(uint64 *addr, size_t *len)
{
	int ret;

	ret = argaddr(0, addr);
	if (ret < 0)
		return -1;

	ret = argaddr(1, len);
	if (ret < 0)
		return -1;

	return 0;
}

/*
 * Reserve an mmap_info struct from the process' memory.
 */
static
int
mmap_info_reserve(struct proc *p, uint64 vaddr, size_t len, int prot, int flags,
			struct file *file, offset_t off)
{
	struct mmap_info *info;

	for (int i = 0; i < MMAP_INFO_MAX; i++) {
		info = &p->mmap_regions[i];
		if (info->used == 0) {
			info->used = 1;

			info->p = p;
			info->vaddr = vaddr;
			info->len = len;
			info->prot = prot;
			info->flags = flags;
			info->file = file;
			info->off = off;

			info->num_pages = len / PGSIZE;
			if (len % PGSIZE != 0)
				info->num_pages++;

			return 0;
		}
	}

	return -1;
}

/*
 * Release an mmap_info struct to the process' memory.
 */
static
void
mmap_info_free(struct mmap_info *info)
{
	info->used = 0;
}

/*
 * Fetch a mmap_info struct in which a file is mapped to the virtual address
 * given as input.
 */
struct mmap_info *
mmap_info_get(struct proc *p, uint64 vaddr)
{
	int addr_in_range;
	struct mmap_info *info;
	uint64 mm_begin, mm_end;

	for (int i = 0; i < MMAP_INFO_MAX; i++) {
		info = &p->mmap_regions[i];
		mm_begin = info->vaddr;
		mm_end = mm_begin + info->len;

		addr_in_range = (vaddr >= mm_begin) && (vaddr < mm_end);

		if (info->used && addr_in_range)
			return info;
	}

	return 0;
}

/*
 * Lazily map a file's contents to a page. Ensure that the file offset matches
 * that of the page's position from the start of the mapped region.
 */
int
mmap_pagefault_handle(struct mmap_info *info, uint64 vaddr, void *phys)
{
	int ret, perms;
	uint offset, len;

	/*
	 * Allow for reading/writing the page depending on the protections
	 * specified at the call to mmap.
	 */
	perms = PTE_U;
	if (info->prot & PROT_READ)
		perms |= PTE_R;
	if (info->prot & PROT_WRITE)
		perms |= PTE_W;

	/*
	 * The contents to be mapped should be at the same offset within the
	 * file as the page is from the beginning of the mapped region.
	 */
	offset = min(PGSIZE, vaddr - info->vaddr);

	/*
	 * If the amount of bytes to write is less than the page's size, only
	 * write that number of bytes.
	 */
	len = min(PGSIZE, (info->vaddr + info->len) - vaddr);

	/*
	 * Read the file's contents to the physical page frame.
	 */
	ilock(info->file->ip);
	ret = readi(info->file->ip, 0, (uint64) phys, offset, len);
	if (ret < 0) {
		iunlock(info->file->ip);
		return -1;
	}
	iunlock(info->file->ip);

	/*
	 * Map the page frame to the process' virtual address space.
	 */
	ret = mappages(info->p->pagetable, vaddr, PGSIZE, (uint64) phys, perms);
	if (ret != 0) {
		kalloc_refcnt_dec(phys);
		return -1;
	}

	return 0;
}
