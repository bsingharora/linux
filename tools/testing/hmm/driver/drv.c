/*
 * Copyright (C) 2017 IBM Corporation
 * Author: Reza Arbab <arbab@linux.vnet.ibm.com>
 *
 * HMM enhancements for CDM by Balbir Singh <bsingharora@gmail.com>
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

#include <linux/fs.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "cdm.h"
#include "uapi.h"

int cdm_migrate(struct cdm_device *cdmdev, struct cdm_migrate *mig);

#define CDM_DEVICE_MAX 6

static struct cdm_device *cdm_device[CDM_DEVICE_MAX];

int cdm_driver_idx;

static long cdm_fops_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct miscdevice *miscdev = file->private_data;
	struct cdm_device *cdmdev;
	struct cdm_migrate mig;
	int rc;

	if (!miscdev)
		return -EINVAL;

	cdmdev = container_of(miscdev, struct cdm_device, miscdev);

	switch (cmd) {
	case CDM_IOC_MIGRATE_BACK:
		cdmdev = NULL;
		/* fallthrough */
	case CDM_IOC_MIGRATE:
		rc = copy_from_user(&mig, (void __user *)arg, sizeof(mig));
		if (rc)
			return rc;

		return cdm_migrate(cdmdev, &mig);
	}

	return -ENOTTY;
}

static const struct file_operations cdm_device_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cdm_fops_ioctl
};

static int cdm_resource_init(struct resource *res, struct device_node *dn)
{
	const __be32 *addrp;
	u64 size;

	addrp = of_get_address(dn, 0, &size, NULL);
	res->start = of_translate_address(dn, addrp);

	if (!addrp || res->start == OF_BAD_ADDR)
		return -EINVAL;

	res->flags = IORESOURCE_MEM;
	res->end = res->start + size - 1;
	res->name = dn->full_name;

	return 0;
}

static void cdmdev_page_free(struct hmm_devmem *devmem, struct page *page)
{
}

static int cdmdev_page_fault(struct hmm_devmem *devmem,
				struct vm_area_struct *vma,
				unsigned long addr,
				struct page *page,
				unsigned int flags,
				pmd_t *pmdp)
{
	return 0;
}

struct hmm_devmem_ops cdmdev_ops = {
	.free = cdmdev_page_free,
	.fault = cdmdev_page_fault,
};

static int cdm_miscdev_init(struct miscdevice *miscdev, int devno)
{
	int rc;

	miscdev->name = kasprintf(GFP_KERNEL, "cdm%d", devno);
	if (!miscdev->name)
		return -ENOMEM;

	miscdev->minor = MISC_DYNAMIC_MINOR;
	miscdev->fops = &cdm_device_fops;
	miscdev->mode = 0444;

	rc = misc_register(miscdev);
	if (rc)
		kfree(miscdev->name);

	return rc;
}

static void cdm_miscdev_remove(struct miscdevice *miscdev)
{
	misc_deregister(miscdev);
	kfree(miscdev->name);
}


static int cdm_device_probe(struct device_node *dn)
{
	int ncdm = cdm_driver_idx++;
	struct cdm_device *cdmdev;
	struct device *dev;
	int rc;

	if (ncdm >= CDM_DEVICE_MAX)
		return 0;

	cdmdev = kzalloc(sizeof(*cdmdev), GFP_KERNEL);
	if (!cdmdev)
		return -ENOMEM;

	rc = cdm_resource_init(&cdmdev->res, dn);
	if (rc)
		goto err;

	cdmdev->hmm_device = hmm_device_new(cdmdev);

	cdmdev->hmm_devmem = hmm_devmem_add_coherent(&cdmdev_ops,
					&cdmdev->hmm_device->device,
					&cdmdev->res);
	if (!cdmdev->hmm_devmem)
		goto err;

	rc = cdm_miscdev_init(&cdmdev->miscdev, cdm_driver_idx);
	if (rc)
		goto err2;

	dev = cdmdev_dev(cdmdev);
	dev->of_node = dn;

	cdm_device[ncdm] = cdmdev;
	return 0;
err2:
	hmm_devmem_remove(cdmdev->hmm_devmem);
err:
	hmm_device_put(cdmdev->hmm_device);
	kfree(cdmdev);
	return rc;
}

static void cdm_device_remove(struct cdm_device *cdmdev)
{
	hmm_device_put(cdmdev->hmm_device);
	cdm_miscdev_remove(&cdmdev->miscdev);
	kfree(cdmdev);
}

static void cdm_device_remove_all(void)
{
	int ncdm = cdm_driver_idx;

	while (ncdm)
		cdm_device_remove(cdm_device[--ncdm]);
}

static int __init cdm_init(void)
{
	struct device_node *dn;

	for_each_compatible_node(dn, NULL, "ibm,coherent-device-memory") {
		int rc = cdm_device_probe(dn);

		if (rc) {
			cdm_device_remove_all();
			of_node_put(dn);
			return rc;
		}
	}

	return 0;
}

static void __exit cdm_exit(void)
{
	cdm_device_remove_all();
}

module_init(cdm_init);
module_exit(cdm_exit);

/* As an exercise, do not use EXPORT_SYMBOL_GPL() symbols. */
MODULE_LICENSE("non-GPL");
