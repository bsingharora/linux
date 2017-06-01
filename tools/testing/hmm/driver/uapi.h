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

#ifndef CDM_UAPI_H
#define CDM_UAPI_H

#include <asm/types.h>

struct cdm_migrate {
	__u64 start;
	__u64 end;
};

#define CDM_IOC_MAGIC 0xBB

#define CDM_IOC_UP _IO(CDM_IOC_MAGIC, 1)
#define CDM_IOC_DOWN _IO(CDM_IOC_MAGIC, 2)
#define CDM_IOC_MIGRATE _IOW(CDM_IOC_MAGIC, 3, struct cdm_migrate)
#define CDM_IOC_MIGRATE_BACK _IOW(CDM_IOC_MAGIC, 4, struct cdm_migrate)

#endif /* CDM_UAPI_H */
