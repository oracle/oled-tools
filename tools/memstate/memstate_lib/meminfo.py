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
"""Helper module to report memory information."""

from __future__ import print_function
from memstate_lib import Base
from memstate_lib import constants
from memstate_lib import Hugepages


class Meminfo(Base):
    """ Analyzes output from /proc/meminfo; checks RDS allocation """
    # pylint: disable=too-many-instance-attributes

    def __init__(self):
        self.mem_total_gb = 0
        self.slab_total_gb = 0
        self.pagetables_gb = 0
        self.swap_used_kb = 0
        self.unaccounted_gb = 0
        self.rds_cache_size_gb = 0
        self.committed_as_gb = 0
        self.hugepages = Hugepages()

    def __read_meminfo(self):
        data = self.read_text_file("/proc/meminfo", on_error="")
        if data == "":
            self.print_error("Unable to read /proc/meminfo")
            return data
        for line in data.splitlines():
            if line.startswith("MemTotal:"):
                self.mem_total_gb = self.__meminfo_get_value_gb(line)
            if line.startswith("Slab:"):
                self.slab_total_gb = self.__meminfo_get_value_gb(line)
            if line.startswith("PageTables:"):
                self.pagetables_gb = self.__meminfo_get_value_gb(line)
        return data

    def __calc_rds_cache_size(self):
        """
        Get memory consumed for all RDS rx frags from the output of rds-info.
        """
        rds_output = self.exec_cmd("rds-info -c")
        size_gb = 0
        for line in rds_output.splitlines():
            if line.strip().startswith("ib_rx_total_frags"):
                num_frags = int(line.split()[1])
                size_gb = self.convert_kb_to_gb(
                    num_frags * constants.FRAG_SIZE_KB)
        return round(size_gb, 1)

    def __meminfo_get_value_gb(self, line):
        kb_value = line.split(":")[1].strip().split(" ")[0]
        return self.convert_kb_to_gb(int(kb_value))

    def get_total_ram_gb(self):
        """Return total system memory in GBs."""
        self.__read_meminfo()
        return self.mem_total_gb

    def get_total_ram_kb(self):
        """Return total system memory in KBs."""
        self.__read_meminfo()
        return round(
            self.mem_total_gb * constants.ONE_KB * constants.ONE_KB, 1)

    def get_rds_cache_gb(self):
        """Return RDS cache size in GBs."""
        self.__calc_rds_cache_size()
        return self.rds_cache_size_gb

    def get_total_slab_gb(self):
        """Return total slab memory in GBs."""
        self.__read_meminfo()
        return self.slab_total_gb

    def get_pagetables_gb(self):
        """Return size of page tables in GBs."""
        self.__read_meminfo()
        return self.pagetables_gb

    def get_swap_used_kb(self):
        """Return amount of swap used in KBs."""
        self.__read_meminfo()
        return self.swap_used_kb

    def get_unaccounted_memory_gb(self):
        """Return amount of unaccounted memory in GBs."""
        return self.unaccounted_gb

    def print_pretty_gb_l1(self, str_msg, int_arg):
        """Pretty-print a GBs value with 1-level indentation."""
        printstr = (" " * 2) + str_msg
        self.print_pretty_gb(printstr, int_arg)

    def print_pretty_gb_l2(self, str_msg, int_arg):
        """Pretty-print a GBs value with 2-level indentation."""
        printstr = (" " * 4) + str_msg
        self.print_pretty_gb(printstr, int_arg)

    def __print_hugepages_summary(self):
        hp_nr = self.hugepages.get_nr_hugepages_matrix_kb()
        for key, val in hp_nr.items():
            hp_nr_total = sum(val)
            if hp_nr_total:
                self.print_pretty_gb_l1(
                    f"Total Hugepages ({key} KB)",
                    round(self.convert_kb_to_gb(hp_nr_total), 1))
        hp_free = self.hugepages.get_free_hugepages_matrix_kb()
        for key, val in hp_free.items():
            hp_free_total = sum(val)
            if hp_free_total:
                self.print_pretty_gb_l1(
                    f"Free Hugepages ({key} KB)",
                    round(self.convert_kb_to_gb(hp_free_total), 1))

    def display_usage_summary(self):
        """Report several memory metrics."""
        # pylint: disable=too-many-statements,too-many-branches,too-many-locals

        mem_free = 0
        cached = 0
        buffers = 0
        vmalloc = 0
        kernelstack = 0
        anonpages = 0
        shmem = 0
        swap_total = 0
        swap_free = 0
        user_allocs = 0

        data = self.__read_meminfo()
        if data == "":
            self.print_cmd_err("cat /proc/meminfo")
            return

        print("MEMORY USAGE SUMMARY (in GB):")
        for line in data.splitlines():
            if line.startswith("MemFree:"):
                mem_free = self.__meminfo_get_value_gb(line)
            if line.startswith("Cached:"):
                cached = self.__meminfo_get_value_gb(line)
            if line.startswith("Buffers:"):
                buffers = self.__meminfo_get_value_gb(line)
            if line.startswith("VmallocUsed:"):
                vmalloc = self.__meminfo_get_value_gb(line)
            if line.startswith("KernelStack:"):
                kernelstack = self.__meminfo_get_value_gb(line)
            if line.startswith("AnonPages:"):
                anonpages = self.__meminfo_get_value_gb(line)
            if line.startswith("Mapped:"):
                mapped = self.__meminfo_get_value_gb(line)
            if line.startswith("Shmem:"):
                shmem = self.__meminfo_get_value_gb(line)
            if line.startswith("SwapTotal:"):
                swap_total = self.__meminfo_get_value_gb(line)
            if line.startswith("SwapFree:"):
                swap_free = self.__meminfo_get_value_gb(line)
            if line.startswith("Committed_AS:"):
                self.committed_as_gb = self.__meminfo_get_value_gb(line)

        self.rds_cache_size_gb = self.__calc_rds_cache_size()
        self.swap_used_kb = round(swap_total - swap_free, 1)

        # Userspace allocations include process stack and heap, page cache, I/O
        # buffers. The page cache value includes mapped files as well as shared
        # memory.
        user_allocs = round(anonpages + cached + buffers, 1)

        # Kernel allocations include slabs, vmalloc, pagetables, stack, RDS
        # cache, and more. The rest of kernel allocations are "unknown" - these
        # typically tend to be kernel memory that are consumed via
        # __get_free_pages() or related APIs, which are not tracked anywhere.
        # If the "unaccounted" value is large, that's a red flag.
        kernel_allocs = round(
            self.slab_total_gb + vmalloc + self.pagetables_gb + kernelstack
            + self.rds_cache_size_gb, 1)
        self.unaccounted_gb = round(
            self.mem_total_gb -
            (mem_free + kernel_allocs + user_allocs
             + self.hugepages.get_total_hugepages_gb()), 1)
        kernel_allocs = round(kernel_allocs + self.unaccounted_gb, 1)

        self.print_pretty_gb("Total memory", self.mem_total_gb)
        self.print_pretty_gb("Free memory", mem_free)
        self.print_pretty_gb(
            "Used memory", round(self.mem_total_gb - mem_free, 1))
        self.print_pretty_gb_l1("Userspace", user_allocs)
        self.print_pretty_gb_l2("Processes", round(anonpages + mapped, 1))
        self.print_pretty_gb_l2("Page cache", cached)
        self.print_pretty_gb_l2("Shared mem", shmem)
        self.print_pretty_gb_l1("Kernel", kernel_allocs)
        self.print_pretty_gb_l2("Slabs", self.slab_total_gb)
        if self.rds_cache_size_gb != 0:
            self.print_pretty_gb_l2("RDS", self.rds_cache_size_gb)
        self.print_pretty_gb_l2("Unknown", self.unaccounted_gb)
        self.__print_hugepages_summary()
        self.print_pretty_gb(
            "Swap used", self.convert_kb_to_gb(self.swap_used_kb))
        print("")

    def check_pagetables_size(self):
        """Emit a warning if pages tables are too large."""
        self.__read_meminfo()
        if (0 < self.pagetables_gb >=
                (constants.PAGETABLES_USE_PERCENT *
                 (self.mem_total_gb -
                  self.hugepages.get_total_hugepages_gb()))):
            print("")
            self.print_warn(
                "Page tables are larger than expected "
                f"({self.pagetables_gb} GB); if this is an Exadata system, "
                "check if the DB parameter USE_LARGE_PAGES is set to ONLY.")
        else:
            print("")
            self.print_info(
                f"Page tables size is: {self.pagetables_gb} GB.")

    def check_unaccounted_memory(self):
        """Emit a warning if unaccounted memory is larger than a threshold."""
        if (0 < self.unaccounted_gb >=
                (constants.UNACCOUNTED_THRESHOLD *
                 (self.mem_total_gb -
                  self.hugepages.get_total_hugepages_gb()))):
            print("")
            self.print_warn(
                "Unaccounted kernel memory use is larger than expected: "
                f"{self.unaccounted_gb} GB.")
        elif self.unaccounted_gb > 0:
            print("")
            self.print_info(
                f"Unaccounted kernel memory is: {self.unaccounted_gb} GB.")

    def check_committed_as(self):
        """
        Emit a warning if committed virtual memory is more than physically
        available.
        """
        self.__read_meminfo()
        if (self.committed_as_gb >=
                (self.mem_total_gb - self.hugepages.get_total_hugepages_gb())):
            print("")
            self.print_warn(
                "Max virtual memory allocated is more than available physical"
                f" memory (Committed_AS = {self.committed_as_gb} GB).")
