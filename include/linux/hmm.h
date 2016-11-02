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

struct hmm;

/*
 * hmm_pfn_t - HMM use its own pfn type to keep several flags per page
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid
 * HMM_PFN_READ: read permission set
 * HMM_PFN_WRITE: CPU page table have the write permission set
 * HMM_PFN_ERROR: corresponding CPU page table entry point to poisonous memory
 * HMM_PFN_EMPTY: corresponding CPU page table entry is none (pte_none() true)
 * HMM_PFN_DEVICE: this is device memory (ie a ZONE_DEVICE page)
 * HMM_PFN_SPECIAL: corresponding CPU page table entry is special ie result of
 *      vm_insert_pfn() or vm_insert_page() and thus should not be mirror by a
 *      device (the entry will never have HMM_PFN_VALID set and the pfn value
 *      is undefine)
 * HMM_PFN_UNADDRESSABLE: unaddressable device memory (ZONE_DEVICE)
 */
typedef unsigned long hmm_pfn_t;

#define HMM_PFN_VALID (1 << 0)
#define HMM_PFN_READ (1 << 1)
#define HMM_PFN_WRITE (1 << 2)
#define HMM_PFN_ERROR (1 << 3)
#define HMM_PFN_EMPTY (1 << 4)
#define HMM_PFN_DEVICE (1 << 5)
#define HMM_PFN_SPECIAL (1 << 6)
#define HMM_PFN_UNADDRESSABLE (1 << 7)
#define HMM_PFN_SHIFT 8

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


#if IS_ENABLED(CONFIG_HMM_MIRROR)
/*
 * Mirroring: how to use synchronize device page table with CPU page table ?
 *
 * Device driver must always synchronize with CPU page table update, for this
 * they can either directly use mmu_notifier API or they can use the hmm_mirror
 * API. Device driver can decide to register one mirror per device per process
 * or just one mirror per process for a group of device. Pattern is :
 *
 *      int device_bind_address_space(..., struct mm_struct *mm, ...)
 *      {
 *          struct device_address_space *das;
 *          int ret;
 *          // Device driver specific initialization, and allocation of das
 *          // which contain an hmm_mirror struct as one of its field.
 *          ret = hmm_mirror_register(&das->mirror, mm, &device_mirror_ops);
 *          if (ret) {
 *              // Cleanup on error
 *              return ret;
 *          }
 *          // Other device driver specific initialization
 *      }
 *
 * Device driver must not free the struct containing hmm_mirror struct before
 * calling hmm_mirror_unregister() expected usage is to do that when device
 * driver is unbinding from an address space.
 *
 *      void device_unbind_address_space(struct device_address_space *das)
 *      {
 *          // Device driver specific cleanup
 *          hmm_mirror_unregister(&das->mirror);
 *          // Other device driver specific cleanup and now das can be free
 *      }
 *
 * Once an hmm_mirror is register for an address space, device driver will get
 * callback through the update() operation (see hmm_mirror_ops struct).
 */

struct hmm_mirror;

/*
 * enum hmm_update - type of update
 * @HMM_UPDATE_INVALIDATE: invalidate range (no indication as to why)
 */
enum hmm_update {
	HMM_UPDATE_INVALIDATE,
};

/*
 * struct hmm_mirror_ops - HMM mirror device operations callback
 *
 * @update: callback to update range on a device
 */
struct hmm_mirror_ops {
	/* update() - update virtual address range of memory
	 *
	 * @mirror: pointer to struct hmm_mirror
	 * @update: update's type (turn read only, unmap, ...)
	 * @start: virtual start address of the range to update
	 * @end: virtual end address of the range to update
	 *
	 * This callback is call when the CPU page table is updated, the device
	 * driver must update device page table accordingly to update's action.
	 *
	 * Device driver callback must wait until device have fully updated its
	 * view for the range. Note we plan to make this asynchronous in later
	 * patches. So that multiple devices can schedule update to their page
	 * table and once all device have schedule the update then we wait for
	 * them to propagate.
	 */
	void (*update)(struct hmm_mirror *mirror,
		       enum hmm_update action,
		       unsigned long start,
		       unsigned long end);
};

/*
 * struct hmm_mirror - mirror struct for a device driver
 *
 * @hmm: pointer to struct hmm (which is unique per mm_struct)
 * @ops: device driver callback for HMM mirror operations
 * @list: for list of mirrors of a given mm
 *
 * Each address space (mm_struct) being mirrored by a device must register one
 * of hmm_mirror struct with HMM. HMM will track list of all mirrors for each
 * mm_struct (or each process).
 */
struct hmm_mirror {
	struct hmm			*hmm;
	const struct hmm_mirror_ops	*ops;
	struct list_head		list;
};

int hmm_mirror_register(struct hmm_mirror *mirror, struct mm_struct *mm);
int hmm_mirror_register_locked(struct hmm_mirror *mirror,
			       struct mm_struct *mm);
void hmm_mirror_unregister(struct hmm_mirror *mirror);


/*
 * struct hmm_range - track invalidation lock on virtual address range
 *
 * @list: all range lock are on a list
 * @start: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @pfns: array of pfns (big enough for the range)
 * @valid: pfns array did not change since it has been fill by an HMM function
 */
struct hmm_range {
	struct list_head	list;
	unsigned long		start;
	unsigned long		end;
	hmm_pfn_t		*pfns;
	bool			valid;
};

/*
 * To snapshot CPU page table call hmm_vma_get_pfns() then take device driver
 * lock that serialize device page table update and call hmm_vma_range_done()
 * to check if snapshot is still valid. The device driver page table update
 * lock must also be use in the HMM mirror update() callback so that CPU page
 * table invalidation serialize on it.
 *
 * YOU MUST CALL hmm_vma_range_dond() ONCE AND ONLY ONCE EACH TIME YOU CALL
 * hmm_vma_get_pfns() WITHOUT ERROR !
 *
 * IF YOU DO NOT FOLLOW THE ABOVE RULE THE SNAPSHOT CONTENT MIGHT BE INVALID !
 */
int hmm_vma_get_pfns(struct vm_area_struct *vma,
		     struct hmm_range *range,
		     unsigned long start,
		     unsigned long end,
		     hmm_pfn_t *pfns);
bool hmm_vma_range_done(struct vm_area_struct *vma, struct hmm_range *range);
#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */


/* Below are for HMM internal use only ! Not to be used by device driver ! */
void hmm_mm_destroy(struct mm_struct *mm);

#else /* IS_ENABLED(CONFIG_HMM) */

/* Below are for HMM internal use only ! Not to be used by device driver ! */
static inline void hmm_mm_destroy(struct mm_struct *mm) {}

#endif /* IS_ENABLED(CONFIG_HMM) */
#endif /* LINUX_HMM_H */
