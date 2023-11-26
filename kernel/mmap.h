#ifndef _MMAP_H
#define _MMAP_H

#define MMAP_INFO_MAX 64

/*
 * The state of a memory-mapped file's regions in a process' virtual address
 * space.
 */
struct mmap_info {
	struct proc *p;		// Process to which the file is mapped to.
	uint64 vaddr;		// First virtual address in the region.
	size_t len;		// Size of the region.
	int prot;		// R/W protections.
	int flags;		// Region sharing flags (shared or private).
	struct file *file;	// Underlying file that is mapped.
	offset_t off;		// Ignored.

	int num_pages;		// The number of pages that the region currently
				// maps (begins at zero).

	int used;		// Indicates if the region is currently in use.
};

#endif // _MMAP_H
