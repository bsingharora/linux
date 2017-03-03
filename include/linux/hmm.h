/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: Jérôme Glisse <jglisse@redhat.com>
 */
/*
 * HMM provides 3 separate functionality :
 *   - Mirroring: synchronize CPU page table and device page table
 *   - Device memory: allocating struct page for device memory
 *   - Migration: migrating regular memory to device memory
 *
 * Each can be used independently from the others.
 *
 *
 * Mirroring:
 *
 * HMM provide helpers to mirror process address space on a device. For this it
 * provides several helpers to order device page table update in respect to CPU
 * page table update. Requirement is that for any given virtual address the CPU
 * and device page table can not point to different physical page. It uses the
 * mmu_notifier API behind the scene.
 *
 * Device memory:
 *
 * HMM provides helpers to help leverage device memory either addressable like
 * regular memory by the CPU or un-addressable at all. In both case the device
 * memory is associated to dedicated structs page (which are allocated like for
 * hotplug memory). Device memory management is under the responsibility of the
 * device driver. HMM only allocate and initialize the struct pages associated
 * with the device memory by hotpluging a ZONE_DEVICE memory range.
 *
 * Allocating struct page for device memory allow to use device memory allmost
 * like any regular memory. Unlike regular memory it can not be added to the
 * lru, nor can any memory allocation can use device memory directly. Device
 * memory will only end up to be use in a process if device driver migrate some
 * of the process memory from regular memory to device memory.
 *
 *
 * Migration:
 *
 * Existing memory migration mechanism (mm/migrate.c) does not allow to use
 * something else than the CPU to copy from source to destination memory. More
 * over existing code is not tailor to drive migration from process virtual
 * address rather than from list of pages. Finaly the migration flow does not
 * allow for graceful failure at different step of the migration process.
 *
 * HMM solves all of the above through simple API :
 *
 *      hmm_vma_migrate(ops, vma, src_pfns, dst_pfns, start, end, private);
 *
 * With ops struct providing 2 callback alloc_and_copy() which allocated the
 * destination memory and initialize it using source memory. Migration can fail
 * after this step and thus last callback finalize_and_map() allow the device
 * driver to know which page were successfully migrated and which were not.
 *
 * This can easily be use outside of HMM intended use case.
 *
 *
 * This header file contain all the API related to this 3 functionality and
 * each functions and struct are more thoroughly documented in below comments.
 */
#ifndef LINUX_HMM_H
#define LINUX_HMM_H

#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_HMM)


/*
 * hmm_pfn_t - HMM use its own pfn type to keep several flags per page
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid
 * HMM_PFN_WRITE: CPU page table have the write permission set
 */
typedef unsigned long hmm_pfn_t;

#define HMM_PFN_VALID (1 << 0)
#define HMM_PFN_WRITE (1 << 1)
#define HMM_PFN_SHIFT 2

/*
 * hmm_pfn_to_page() - return struct page pointed to by a valid hmm_pfn_t
 * @pfn: hmm_pfn_t to convert to struct page
 * Returns: struct page pointer if pfn is a valid hmm_pfn_t, NULL otherwise
 *
 * If the hmm_pfn_t is valid (ie valid flag set) then return the struct page
 * matching the pfn value store in the hmm_pfn_t. Otherwise return NULL.
 */
static inline struct page *hmm_pfn_to_page(hmm_pfn_t pfn)
{
	if (!(pfn & HMM_PFN_VALID))
		return NULL;
	return pfn_to_page(pfn >> HMM_PFN_SHIFT);
}

/*
 * hmm_pfn_to_pfn() - return pfn value store in a hmm_pfn_t
 * @pfn: hmm_pfn_t to extract pfn from
 * Returns: pfn value if hmm_pfn_t is valid, -1UL otherwise
 */
static inline unsigned long hmm_pfn_to_pfn(hmm_pfn_t pfn)
{
	if (!(pfn & HMM_PFN_VALID))
		return -1UL;
	return (pfn >> HMM_PFN_SHIFT);
}

/*
 * hmm_pfn_from_page() - create a valid hmm_pfn_t value from struct page
 * @page: struct page pointer for which to create the hmm_pfn_t
 * Returns: valid hmm_pfn_t for the page
 */
static inline hmm_pfn_t hmm_pfn_from_page(struct page *page)
{
	return (page_to_pfn(page) << HMM_PFN_SHIFT) | HMM_PFN_VALID;
}

/*
 * hmm_pfn_from_pfn() - create a valid hmm_pfn_t value from pfn
 * @pfn: pfn value for which to create the hmm_pfn_t
 * Returns: valid hmm_pfn_t for the pfn
 */
static inline hmm_pfn_t hmm_pfn_from_pfn(unsigned long pfn)
{
	return (pfn << HMM_PFN_SHIFT) | HMM_PFN_VALID;
}


/* Below are for HMM internal use only ! Not to be used by device driver ! */
void hmm_mm_destroy(struct mm_struct *mm);

#else /* IS_ENABLED(CONFIG_HMM) */

/* Below are for HMM internal use only ! Not to be used by device driver ! */
static inline void hmm_mm_destroy(struct mm_struct *mm) {}

#endif /* IS_ENABLED(CONFIG_HMM) */
#endif /* LINUX_HMM_H */
