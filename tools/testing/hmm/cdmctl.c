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

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "driver/uapi.h"

int main(int argc, char *argv[])
{
	int fd, cmd, rc;

	if (argc != 3)
		return -EINVAL;

	if (!strcmp(argv[2], "up"))
		cmd = CDM_IOC_UP;
	else if (!strcmp(argv[2], "down"))
		cmd = CDM_IOC_DOWN;
	else
		return -EINVAL;

	fd = open(argv[1], O_NONBLOCK);
	if (fd < 0) {
		perror(argv[1]);
		return fd;
	}

	rc = ioctl(fd, cmd);
	close(fd);
	if (rc) {
		perror("ioctl");
		return -errno;
	}

	return 0;
}
