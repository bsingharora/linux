#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/pagemap.h>
#include <linux/hmm.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/platform_device.h>

#include <uapi/linux/hmm_dmirror.h>

struct test_device {
	dev_t			dev;
	struct cdev		cdevice;
	struct platform_device	*pdevice;
};

struct test_device dev;

static struct page *test_device_alloc_page(void)
{
	struct page *rpage;

	/*
	 * This is a fake device so we alloc real system memory to fake
	 * our device memory
	 */
	rpage = alloc_page(GFP_HIGHUSER | __GFP_ZERO);
	if (!rpage)
		return NULL;
	get_page(rpage);
	return rpage;
}

static void test_migrate_alloc_and_copy(struct vm_area_struct *vma,
					 const unsigned long *src_pfns,
					 unsigned long *dst_pfns,
					 unsigned long start,
					 unsigned long end,
					 void *private)
{
	unsigned long addr, size;

	for (addr = start; addr < end; addr += size, src_pfns++, dst_pfns++) {
		struct page *spage = migrate_pfn_to_page(*src_pfns);
		struct page *rpage;

		size = migrate_pfn_size(*src_pfns);
		*dst_pfns = (*src_pfns) & MIGRATE_PFN_HUGE;

		if ((*src_pfns) & MIGRATE_PFN_HUGE) {
			// FIXME support huge
			continue;
		}

		rpage = test_device_alloc_page();
		if (!rpage)
			continue;

		if (spage)
			copy_highpage(rpage, spage);
		*dst_pfns = page_to_pfn(rpage) |
			    MIGRATE_PFN_VALID |
			    MIGRATE_PFN_LOCKED;
	}
}

static void test_migrate_finalize_and_map(struct vm_area_struct *vma,
					   const unsigned long *src_pfns,
					   const unsigned long *dst_pfns,
					   unsigned long start,
					   unsigned long end,
					   void *private)
{
	unsigned long addr, size;

	for (addr = start; addr < end; addr+= size, src_pfns++, dst_pfns++) {
		size = migrate_pfn_size(*src_pfns);

		if (!(*src_pfns & MIGRATE_PFN_MIGRATE))
			continue;
	}
}

static const struct migrate_vma_ops test_migrate_ops = {
	.alloc_and_copy		= test_migrate_alloc_and_copy,
	.finalize_and_map	= test_migrate_finalize_and_map,
};

static int test_migrate(unsigned long addr, struct mm_struct *mm, unsigned long npages)
{
	unsigned long end;
	struct vm_area_struct *vma;
	int ret;

	down_read(&mm->mmap_sem);
	end = addr + (npages << PAGE_SHIFT);
	vma = find_vma_intersection(mm, addr, end);
	if (!vma || vma->vm_start > addr || vma->vm_end < end) {
		ret = -EINVAL;
		goto out;
	}

	for (npages = 0; addr < end;) {
		unsigned long src_pfns[64];
		unsigned long dst_pfns[64];
		unsigned long next;

		next = min(end, addr + (64 << PAGE_SHIFT));

		ret = migrate_vma(&test_migrate_ops, vma, 64, addr,
				  next, src_pfns, dst_pfns, NULL);
		if (ret)
			goto out;

		addr = next;
	}

out:
	up_read(&mm->mmap_sem);
	return ret;
}


static long test_fops_unlocked_ioctl(struct file *filp,
				      unsigned int command,
				      unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct hmm_dmirror_migrate migrate;
	int ret;

	switch (command) {
	case HMM_DMIRROR_MIGRATE:
		ret = copy_from_user(&migrate, uarg, sizeof(migrate));
		if (ret)
			return ret;

		ret = test_migrate(migrate.addr, current->mm, migrate.npages);
		if (ret)
			return ret;

		return copy_to_user(uarg, &migrate, sizeof(migrate));
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int test_fops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int test_fops_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations test_fops = {
	.open		= test_fops_open,
	.release	= test_fops_release,
	.unlocked_ioctl = test_fops_unlocked_ioctl,
	.llseek		= default_llseek,
	.owner		= THIS_MODULE,
};

static int test_probe(struct platform_device *pdev)
{
	struct test_device *mdevice = platform_get_drvdata(pdev);
	int ret;

	ret = alloc_chrdev_region(&mdevice->dev, 0, 1, "HMM_MIGRATE");
	if (ret < 0) {
		return ret;
	}

	cdev_init(&mdevice->cdevice, &test_fops);
	ret = cdev_add(&mdevice->cdevice, mdevice->dev, 1);
	if (ret) {
		unregister_chrdev_region(mdevice->dev, 1);
		return ret;
	}

	return 0;
}

static int test_remove(struct platform_device *pdev)
{
	struct test_device *mdevice = platform_get_drvdata(pdev);

	cdev_del(&mdevice->cdevice);
	unregister_chrdev_region(mdevice->dev, 1);
	return 0;
}


static struct platform_device *test_platform_device;

static struct platform_driver test_device_driver = {
	.probe		= test_probe,
	.remove		= test_remove,
	.driver		= {
		.name	= "HMM_DMIRROR",
	},
};

static int __init hmm_test_init(void)
{
	struct test_device *mdevice;
	int ret;

	mdevice = kzalloc(sizeof(*mdevice), GFP_KERNEL);
	if (!mdevice)
		return -ENOMEM;

	test_platform_device = platform_device_alloc("HMM_MIGRATE", -1);
	if (!test_platform_device) {
		kfree(mdevice);
		return -ENOMEM;
	}
	platform_set_drvdata(test_platform_device, mdevice);
	mdevice->pdevice = test_platform_device;

	ret = platform_device_add(test_platform_device);
	if (ret < 0) {
		platform_device_put(test_platform_device);
		return ret;
	}

	ret = platform_driver_register(&test_device_driver);
	if (ret < 0) {
		platform_device_unregister(test_platform_device);
		return ret;
	}

	pr_info("hmm_migrate loaded THIS IS A DANGEROUS MODULE !!!\n");
	return 0;
}

static void __exit hmm_test_exit(void)
{
	struct dmirror_device *mdevice;

	mdevice = platform_get_drvdata(test_platform_device);
	platform_driver_unregister(&test_device_driver);
	platform_device_unregister(test_platform_device);
	kfree(mdevice);
}

module_init(hmm_test_init);
module_exit(hmm_test_exit);
MODULE_LICENSE("GPL");
