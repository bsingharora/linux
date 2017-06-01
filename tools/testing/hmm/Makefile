# Copyright (C) 2017 IBM Corporation
# Author: Reza Arbab <arbab@linux.vnet.ibm.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>

KDIR ?= /lib/modules/$(shell uname -r)/build

default: modules cdmctl migrate_pages

migrate_pages: LDLIBS = -lnuma

modules:
	$(MAKE) -C $(KDIR) M=$(PWD)/driver modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/driver clean
	$(RM) *.o cdmctl migrate_pages
