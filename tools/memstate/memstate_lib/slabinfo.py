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
from collections import OrderedDict
from memstate_lib import Base
from memstate_lib import Meminfo
from memstate_lib import constants
from memstate_lib import Hugepages


class Slabinfo(Base):
    """ Analyzes output from /proc/slabinfo """

    def __init__(self, infile=None):
        self.input_file = False
        self.hugepages = Hugepages()
        self.data = ""
        if infile:
            self.input_file = True
            self.__read_slabinfo_file(infile)

    def __read_slabinfo_file(self, infile):
        data = self.open_file(str(infile), 'r')
        if data:
            self.data = data.read()
            data.close()
        else:
            self.print_error("Unable to read input file '" + str(infile) + "'")
            self.data = ""

    def __read_slabinfo(self):
        self.data = self.read_text_file("/proc/slabinfo")

    @staticmethod
    def __slabinfo_get_name(line):
        return line.split()[0].strip()

    @staticmethod
    def __slabinfo_get_num_slabs(line):
        return line.split()[14].strip()

    @staticmethod
    def __slabinfo_get_pages_per_slab(line):
        return line.split()[5].strip()

    def __get_ordered_slab_caches(self):
        """
        Run "cat /proc/slabinfo" and compute memory used by each slab cache.
        Sort in descending order and return list.
        @return: List of all slab caches, sorted in descending order based on
                 size. Note that the size of the slab caches in the list is in
                 KB.
        """
        if self.data == "":
            self.print_error("Slabinfo data unavailable; nothing to analyze.")
            return None
        slab_list = {}
        slab_caches_sorted = {}
        for line in self.data.splitlines():
            if len(line.split()) != 16:
                continue
            c_name = self.__slabinfo_get_name(line)
            c_num_slabs = self.__slabinfo_get_num_slabs(line)
            c_pages_per_slab = self.__slabinfo_get_pages_per_slab(line)
            slab_list[c_name] = (
                int(c_num_slabs) * int(c_pages_per_slab) *
                constants.PAGE_SIZE_KB)

        slab_caches_sorted = OrderedDict(
            sorted(slab_list.items(), key=lambda x: x[1], reverse=True))
        return slab_caches_sorted

    def __display_top_slab_caches(self, num, slabs_list=None):
        if slabs_list is None:
            self.print_error("Slab caches list unavailable!")
            return
        num_printed = 0
        if num != constants.NO_LIMIT:
            for slab, size_kb in slabs_list.items():
                if num_printed >= num or self.convert_kb_to_gb(size_kb) <= 0:
                    break
                self.print_pretty_gb(slab, self.convert_kb_to_gb(size_kb))
                num_printed += 1
        else:  # NO_LIMIT
            for slab, size_kb in slabs_list.items():
                if size_kb != 0:
                    self.print_pretty_kb(slab, size_kb)

    def __get_total_slab_size_gb(self, slabs_list=None):
        if slabs_list is None:
            self.print_error("Slab caches list unavailable!")
            return None
        slab_size_kb = sum(slabs_list.values())

        return self.convert_kb_to_gb(slab_size_kb)

    def __check_slab_usage(self, num):
        """
        Print a warning if total slab usage is >= SLAB_USE_PERCENT of
        (TotalRAM - HugePages).

        Also lists the biggest <NUM_SLAB_CACHES> slab caches.
        """
        if not self.input_file:
            self.__read_slabinfo()
        if self.data == "":
            self.print_error("Slabinfo data unavailable!")
            return
        slab_list = self.__get_ordered_slab_caches()
        if self.input_file:
            slab_total_gb = self.__get_total_slab_size_gb(slab_list)
            print(f"Total size of all slab caches is {slab_total_gb} GB.")
        else:
            meminfo = Meminfo()
            slab_total_gb = meminfo.get_total_slab_gb()
            if (slab_total_gb >=
                    (constants.SLAB_USE_PERCENT *
                     (meminfo.get_total_ram_gb() -
                      self.hugepages.get_total_hugepages_gb()))):
                self.print_warn("Large slab caches found on this system!")
        self.__display_top_slab_caches(num, slab_list)

    def memstate_check_slab(self, num=constants.NUM_TOP_SLAB_CACHES):
        """Check state of slab."""
        if num == constants.NO_LIMIT:
            self.print_header_l1("Slab caches")
        else:
            self.print_header_l1("Top " + str(num) + " slab caches")
        self.__check_slab_usage(num)
        print("")
