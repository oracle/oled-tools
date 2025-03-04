#!/usr/bin/env python3
#
# Copyright (c) 2023, Oracle and/or its affiliates.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, see <https://www.gnu.org/licenses/>.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
"""Helper module to analyze slab info."""

from __future__ import print_function
import os
from collections import OrderedDict
from memstate_lib import Base
from memstate_lib import Meminfo
from memstate_lib import constants
from memstate_lib import Hugepages


class Slabinfo(Base):
    """ Analyzes output from /proc/slabinfo """

    def __init__(self):
        self.slab_list_sorted = {}
        self.slab_aliases = {}
        self.slab_total_gb = 0

    def __get_ordered_slab_caches(self):
        """
        Read /sys/kernel/slab/<cache>/ files to compute memory used by each
        slab cache. Sort in descending order by size (KB). Also get a list of
        all aliases for the slab cache (i.e. names of all caches with similar
        attributes which have been merged together).
        """
        # pylint: disable=too-many-locals

        slab_root = '/sys/kernel/slab/'
        slab_list = {}
        aliases = {}
        files_path = os.listdir(slab_root)
        for elem in files_path:
            try:
                if elem.startswith(":"):
                    continue
                cachedir = os.path.join(slab_root, elem)
                if not os.path.isdir(cachedir):
                    continue
                if os.path.islink(cachedir):
                    # Merged slabs symlink to the same slab cache.
                    # In the aliases dictionary, all the symlinked caches point
                    # to the same target. For instance:
                    # ':A-0001152': ['PING', 'UNIX', 'signal_cache']
                    target = os.readlink(cachedir)
                else:
                    # These slabs aren't symlinked to anything.
                    # Add them in the dictionary anyway, they just point to
                    # themselves. For instance:
                    # 'skbuff_head_cache': ['skbuff_head_cache']
                    target = elem
                aliases.setdefault(target, []).append(elem)
            except OSError:
                # Ignore any FileNotFoundErrors and continue onto the next
                # slab cache
                pass

        for val in aliases.values():
            try:
                cache = val[0]
                if len(val) > 1:
                    # This slab cache has at least one other alias - i.e. it
                    # has been merged with another slab cache with similar
                    # attributes.
                    alias_list = val[1:]
                    self.slab_aliases.setdefault(cache, []).extend(alias_list)
                slabs_file = os.path.join(slab_root, cache, "slabs")
                if not os.path.exists(slabs_file):
                    continue
                order_file = os.path.join(slab_root, cache, "order")
                if not os.path.exists(order_file):
                    continue

                with open(slabs_file, "r", encoding="utf8") as slabs_fd:
                    line = slabs_fd.read()
                    slabs = int(line.split()[0].strip())
                with open(order_file, "r", encoding="utf8") as order_fd:
                    order = int(order_fd.read())

                slab_size = int(self.order_x_in_kb(order))
                cache_size = round(slabs * slab_size)
                slab_list[cache] = cache_size
            except OSError:
                # Ignore any FileNotFoundErrors and continue onto the next
                # slab cache
                pass

        self.slab_list_sorted = OrderedDict(
            sorted(slab_list.items(), key=lambda x: x[1], reverse=True))

    def __display_top_slab_caches(self, num, slabs_list=None):
        if slabs_list is None:
            self.print_error("Slab caches list unavailable!")
            return
        num_printed = 0
        for slab, size_kb in slabs_list.items():
            if num != constants.NO_LIMIT and num_printed >= num:
                break
            if size_kb > 0:
                aliases = "(null)"
                if slab in self.slab_aliases:
                    aliases = ', '.join(
                            str(a) for a in self.slab_aliases[slab])
                print(f"{slab: <30}{size_kb: >16}{' ': ^12}{aliases: <60}")
                num_printed += 1

        slab_total_kb = self.__compute_total_slab_size(slabs_list)
        print("")
        if self.slab_total_gb > 1:
            print(
                ">> Total memory used by all slab caches: "
                f"{self.slab_total_gb} GB")
        else:
            print(
                ">> Total memory used by all slab caches: "
                f"{slab_total_kb} KB")

    def __compute_total_slab_size(self, slabs_list=None):
        if slabs_list is None:
            self.print_error("Slab caches list unavailable!")
            return None
        slab_size_kb = sum(slabs_list.values())
        self.slab_total_gb = self.convert_kb_to_gb(slab_size_kb)
        return slab_size_kb

    def __check_slab_usage(self, num):
        """
        Print a warning if total slab usage is >= SLAB_USE_PERCENT of
        (TotalRAM - HugePages).

        Also lists the biggest <NUM_SLAB_CACHES> slab caches.
        """
        self.__get_ordered_slab_caches()
        meminfo = Meminfo()
        hugepages = Hugepages()
        if (self.slab_total_gb >=
                (constants.SLAB_USE_PERCENT *
                 (meminfo.get_total_ram_gb() -
                  hugepages.get_total_hugepages_gb()))):
            self.print_warn("Large slab caches found on this system!")
        self.__display_top_slab_caches(num, self.slab_list_sorted)

    def memstate_check_slab(self, num=constants.NUM_TOP_SLAB_CACHES):
        """Check state of slab."""
        if num == constants.NO_LIMIT:
            hdr = "SLAB CACHES (in KB):"
        else:
            hdr = f"TOP {num} SLAB CACHES (in KB):"
        print(hdr)
        print(
            f"{'SLAB CACHE': <30}{'SIZE (KB)': >16}{' ': ^12}{'ALIASES': <60}")
        self.__check_slab_usage(num)
        print("")
