/*
 * Copyright (C) 2017 IBM Corporation
 * Author: Reza Arbab <arbab@linux.vnet.ibm.com>
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

#ifndef CDM_H
#define CDM_H

#include <linux/hmm.h>
#include <linux/miscdevice.h>

struct cdm_device {
	struct hmm_device *hmm_device;
	struct hmm_devmem *hmm_devmem;
	struct resource res;
	struct miscdevice miscdev;
};

static inline struct device *cdmdev_dev(struct cdm_device *cdmdev)
{
	return &cdmdev->hmm_device->device;
}

static inline int cdmdev_to_node(struct cdm_device *cdmdev)
{
	return dev_to_node(cdmdev_dev(cdmdev));
}

#endif /* CDM_H */
