/*
 * Copyright (C) 2017 IBM Corporation
 * Author: Reza Arbab <arbab@linux.vnet.ibm.com>
 * Author: Balbir Singh <bsingharora@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/migrate.h>
#include <linux/bitops.h>
#include "cdm.h"
#include "uapi.h"


#ifdef MIGRATE_PFN_MIGRATE

static struct page *alloc_page_cdm(struct cdm_device *cdmdev,
				   struct page *old)
{
	unsigned long bitmap_no, bitmap_size;
	struct page *page;

	BUG_ON(!cdmdev->bitmap);

	bitmap_size = (cdmdev->hmm_devmem->pfn_last -
			cdmdev->hmm_devmem->pfn_first + 1) / sizeof(unsigned long);
	mutex_lock(&cdmdev->lock);
	bitmap_no = bitmap_find_next_zero_area_off(cdmdev->bitmap,
						bitmap_size, 0, 1, 0, 0);
	if (bitmap_no >= bitmap_size) {
		mutex_unlock(&cdmdev->lock);
		pr_warn("Out of memory\n");
		return NULL;
	}

	bitmap_set(cdmdev->bitmap, bitmap_no, 1);
	mutex_unlock(&cdmdev->lock);

	page = pfn_to_page((unsigned long)cdmdev->hmm_devmem->pfn_first
							+ bitmap_no);
	get_page(page);
	return page;
}

static void alloc_and_copy(struct vm_area_struct *vma,
			   const unsigned long *src, unsigned long *dst,
			   unsigned long start, unsigned long end,
			   void *private)
{
	struct cdm_device *cdmdev = (struct cdm_device *)private;
	unsigned long i;

	for (i = 0; i < (end - start) >> PAGE_SHIFT; i++) {
		struct page *spage, *dpage;

		if (!(src[i] & MIGRATE_PFN_MIGRATE))
			continue;

		spage = migrate_pfn_to_page(src[i]);
		dpage = alloc_page_cdm(cdmdev, spage);

		if (!dpage) {
			pr_err("%s: failed to alloc dst[%ld]\n", __func__, i);
			dst[i] = 0;
			continue;
		}

		/* use dma here */
		copy_highpage(dpage, spage);

		lock_page(dpage);
		dst[i] = migrate_pfn(page_to_pfn(dpage)) | MIGRATE_PFN_LOCKED;
	}
}

static void finalize_and_map(struct vm_area_struct *vma,
			     const unsigned long *src,
			     const unsigned long *dst,
			     unsigned long start, unsigned long end,
			     void *private)
{
	unsigned long i;

	for (i = 0; i < (end - start) >> PAGE_SHIFT; i++) {
		if (src[i] & MIGRATE_PFN_MIGRATE)
			continue;

		pr_err("%s: src[%ld] not migrated\n", __func__, i);
	}
}

static const struct migrate_vma_ops cdm_migrate_ops = {
	.alloc_and_copy = alloc_and_copy,
	.finalize_and_map = finalize_and_map
};

int cdm_migrate(struct cdm_device *cdmdev, struct cdm_migrate *mig)
{
	struct vm_area_struct *vma;
	unsigned long addr, next;

	down_read(&current->mm->mmap_sem);
	vma = find_vma_intersection(current->mm, mig->start, mig->end);

	for (addr = mig->start; addr < mig->end; addr = next) {
		unsigned long src[64], dst[64];
		int rc;

		next = min_t(unsigned long, mig->end,
			     addr + (64 << PAGE_SHIFT));

		rc = migrate_vma(&cdm_migrate_ops, vma, addr, next, src, dst,
				 cdmdev);
		if (rc) {
			up_read(&current->mm->mmap_sem);
			return rc;
		}
	}
	up_read(&current->mm->mmap_sem);

	return 0;
}

#else

int cdm_migrate(struct cdm_device *cdmdev, struct cdm_migrate *mig)
{
	return -ENOSYS;
}

#endif /* MIGRATE_PFN_MIGRATE */
