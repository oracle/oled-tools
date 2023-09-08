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
"""Helper modulo to check and report NUMA information."""

from __future__ import print_function
from collections import OrderedDict
import glob
import time
from memstate_lib import Base
from memstate_lib import constants
from memstate_lib import Hugepages


class Numa(Base):
    """ Analyzes output from /proc/<pid>/numa_maps """

    def __init__(self, infile=None):
        self.input_file = False
        self.numa_maps = ""
        self.hugepages = Hugepages()
        self.num_numa_nodes = 1
        if infile is None:
            self.num_numa_nodes = self.get_num_numa_nodes()
        else:
            self.input_file = True
            self.__read_numa_maps_file(infile)
            self.num_numa_nodes = self.__get_num_numa_nodes_numamaps()

    def __read_numa_maps_file(self, infile):
        fdesc = self.open_file(infile, 'r')
        if fdesc:
            self.numa_maps = fdesc.read()
            fdesc.close()
        else:
            self.print_error("Unable to read input file '" + str(infile) + "'")
            self.numa_maps = ""

    def __read_numa_maps(self):
        """
        Capture the contents of /proc/*/numa_maps into a data stream (string).
        """
        header_str = "zzz /proc/<pid>/numa_maps:"
        newline = "\n"
        separator = "=" * 8
        self.numa_maps = header_str
        self.numa_maps += newline
        start_time = time.time()
        num_files_scanned = 0
        for filestr in glob.glob("/proc/*/numa_maps"):
            pid = filestr.split("/")[2]
            comm = self.read_text_file(
                f"/proc/{pid}/comm", on_error="UNKOWN").strip()
            fdesc = self.open_file(filestr, 'r')
            if not fdesc:
                continue
            data = fdesc.read()
            pstr = str(comm) + "(" + pid + "):"
            if data:
                self.numa_maps += pstr
                self.numa_maps += newline
                self.numa_maps += data
                self.numa_maps += newline
            num_files_scanned = num_files_scanned + 1
            fdesc.close()
            time.sleep(0.01)  # Sleep for 10 ms, to avoid hogging CPU
        end_time = time.time()
        self.log_debug(
            f"Time taken to read {num_files_scanned} /proc/<pid>/numa_maps "
            f"files is: {round(end_time - start_time)} second(s).")
        self.numa_maps += separator
        self.numa_maps += newline

    def __print_numa_maps_headers(self):
        print(f"{'PROCESS(PID)': <30}{'NODE 0': >15}", end=' ')
        for i in range(1, self.num_numa_nodes):
            print(f"{'NODE ' + str(i): >14}", end=' ')
        print("")

    def __print_numastat_headers(self):
        print(f"{'NODE 0': >45}", end=' ')
        for i in range(1, self.num_numa_nodes):
            print(f"{'NODE '  + str(i): >14}", end=' ')
        print("")

    @staticmethod
    def __print_pretty_numastat_kb(str_msg, numa_arr_kb):
        print(f"{str_msg: <30}", end=' ')
        for _, val in enumerate(numa_arr_kb):
            print(f"{str(val): >14}", end=' ')
        print("")

    def __check_numa_maps_bindings(self):
        """
        Parse the output of /proc/*/numa_maps, and print any interesting
        mappings.

        Right now, we check for:
        - explicit bindings to any NUMA node, if they exist
        """
        interesting = [' bind:']
        matched = ""
        printstr = ""
        found_something = False
        start_time = time.time()
        if self.numa_maps == "":
            self.print_error("Unable to read numa_maps.")
            return
        for line in self.numa_maps.splitlines():
            if (not line[:1].isdigit() and len(line.split()) == 1
                    and line[-1] == ":"):
                # This needs to find both these patterns of strings (with or
                # without pid):
                # "oracle_105390_w:" as well as "systemd-journal(3187):"
                header_str = line + "\n"
                if found_something:
                    matched = matched + "\n" + printstr
                printstr = header_str
                found_something = False
            if any(word in line for word in interesting):
                printstr = printstr + line + "\n"
                found_something = True
        end_time = time.time()
        self.log_debug(
            "Time taken to check numa_maps for explicit binding, etc., is "
            f"{round(end_time - start_time)} second(s).")
        if matched:
            print("\nParsing numa_maps; these lines might be interesting:")
            print(matched)

    def __is_low_memfree(self, memfree_kb, memtotal_kb):
        if not memfree_kb or not memtotal_kb:
            self.print_error(
                "Unable to read memfree/memtotal values per NUMA node.")
            return False
        lowest_ratio = min(
            round(float(free) / float(total), 2)
            for total, free in zip(memtotal_kb, memfree_kb)
        )
        if lowest_ratio <= constants.NUMASTAT_MIN_MEMFREE_PERCENT:
            # MemFree is low on at least one NUMA node.
            return True
        return False

    def __check_for_numa_imbalance(self, numa_row_str, numa_val_mb):
        """
        Checks for imbalance in memory usage across all NUMA nodes. If, for
        instance, MemFree on one node is >= NUMASTAT_DIFF_PERCENT that of
        another node, then we flag it as imbalance.
        [Note: these values are slightly arbitrary/based on anecdotal evidence
        from customer bugs; should be refined further.]
        """
        lowest = min(float(val) for val in numa_val_mb)
        highest = max(float(val) for val in numa_val_mb)
        if (float(highest) >
                float(constants.NUMASTAT_DIFF_PERCENT * float(lowest))):
            # Uh oh! Seems like we have some imbalance here - should keep an
            # eye on it.
            self.print_warn(f"{numa_row_str} is imbalanced across NUMA nodes.")

    def __display_numa_maps(self, nlist, num):
        """
        Display aggregated per-NUMA memory usage per process, in descending
        order.
        """
        if not nlist:
            return
        nlist_sorted = OrderedDict(
            sorted(nlist.items(), key=lambda x: x[1], reverse=True))
        comm_ignore = []
        num_printed = 0
        self.__print_numa_maps_headers()
        for elem in nlist_sorted:
            if num != constants.NO_LIMIT and num_printed >= num:
                break
            comm = elem.split()[0].strip()
            if comm in comm_ignore:
                continue  # comm/pid has already been printed
            per_pid_val = {
                key: val
                for key, val in nlist_sorted.items()
                if comm == key.split()[0].strip()
            }
            if not per_pid_val:
                continue
            per_pid_sorted = OrderedDict(
                sorted(per_pid_val.items(), key=lambda x: x[0]))
            hdr_printed = False
            for pid_elem in per_pid_sorted:
                if hdr_printed is False:
                    comm = pid_elem.split()[0]
                    print(f"{comm: <30}", end=' ')
                    hdr_printed = True
                    comm_ignore.append(comm)
                value_kb = int(nlist_sorted[pid_elem]) * constants.PAGE_SIZE_KB
                print(f"{value_kb: >14}", end=" ")
            print("")
            num_printed += 1

    def process_numa_maps(self, num):
        """
        Parse the output of /proc/*/numa_maps, and print:
        - the total memory allocated by each process, on each NUMA node
        - explicit bindings to any NUMA node, if they exist
        - a warning if there is a heavy skew on one NUMA node, for any process
          (TODO)
        Note: this operation can take a *very* long time depending on the
              system config, load, etc. You might also notice this script
              consuming > 95% CPU during this processing for a few minutes. So
              it is not recommended to invoke this function too often.
        """
        # pylint: disable=too-many-branches
        if not self.input_file:
            self.__read_numa_maps()
        if self.numa_maps == "":
            self.print_error("Unable to read numa_maps data.")
            return

        if num != constants.NO_LIMIT:
            hdr = "Per-node memory usage, per process " + \
                    f"(in KB, top {num} processes):"
        else:
            hdr = "Per-node memory usage, per process (in KB):"
        print(hdr)
        i = 0
        nlist = {}
        key = None
        start_time = time.time()
        while i < self.num_numa_nodes:
            pages = 0
            for line in self.numa_maps.splitlines():
                ni_search_str = "N" + str(i) + "="
                if line[:1].isalpha() and line.endswith(":"):
                    comm = line[:-1].strip()
                    key = comm + " " + ni_search_str[:-1].strip()
                    key_stored = False
                node_i = [
                    elem for elem in line.split() if ni_search_str in elem
                ]
                if key_stored is False:
                    key_stored = True
                    pages = 0  # Reset!
                for elem in node_i:
                    pages += int(str(elem).split("=")[1])
                if key:
                    nlist[key] = pages
            i += 1  # Check next numa node
        end_time = time.time()
        self.log_debug(
            "Time taken to process numa_maps files is: "
            f"{round(end_time - start_time)} second(s).")
        if not nlist:
            self.print_error(
                "Could not process numa_maps data. If this is not a live "
                "analysis, check if the data in the input file is in the "
                "correct format; check the man page for more details.")
        else:
            self.__display_numa_maps(nlist, num)
            self.__check_numa_maps_bindings()
            print("")

    def get_num_numa_nodes(self):
        """
        Read '/sys/devices/system/node/online' to check number of NUMA nodes on
        this system.
        For instance, on a 2-node system, that file will contain:
        # cat /sys/devices/system/node/online
        0-1
        """
        num_nodes = 1
        out = self.read_text_file(
            "/sys/devices/system/node/online", on_error="").strip()
        if not out:
            self.print_error(
                "Unable to read /sys/devices/system/node/online - assuming "
                "there's one NUMA node.")
            return num_nodes
        online_nodes = str(out).split("-")
        if len(online_nodes) > 1:
            num_nodes = int(online_nodes[1]) + 1
        return num_nodes

    def __get_num_numa_nodes_numamaps(self):
        num_nodes = 1
        i = 1  # There's at least one NUMA node (N0)
        while i < constants.MAX_NUMA_NODES:
            found_another = False
            ni_search_str = "N" + str(i) + "="
            for line in self.numa_maps.splitlines():
                ni_val = [
                    elem for elem in line.split() if ni_search_str in elem
                ]
                if ni_val:
                    # Found ith NUMA node!
                    i += 1
                    num_nodes += 1
                    found_another = True
                    break
            if found_another is False:
                # No sense in continuing to look for more NUMA nodes
                break
        return num_nodes

    @staticmethod
    def __get_numa_meminfo_val(line):
        val = line.split(":")[1].strip()
        return val.split()[0]

    def __print_hugepage_stats_by_node(self):
        hp_nr = self.hugepages.get_nr_hugepages_matrix_kb()
        for hp_size, nr_kb in hp_nr.items():
            # If all elements in a list are 0s, ignore that list.
            if not all(e == 0 for e in nr_kb):
                self.__print_pretty_numastat_kb(
                    f"Total Hugepages ({hp_size} KB)", nr_kb)

        hp_free = self.hugepages.get_free_hugepages_matrix_kb()
        for hp_size, nr_kb in hp_free.items():
            if not all(e == 0 for e in nr_kb):
                self.__print_pretty_numastat_kb(
                    f"Free Hugepages ({hp_size} KB)", nr_kb)

    def __display_numa_meminfo(self):
        """
        Display info similar to numastat -m output, by reading
        /sys/devices/system/node/node*/meminfo
        """
        i = 0
        numa_memtotal_kb = []
        numa_memfree_kb = []
        numa_filepages_kb = []
        numa_anonpages_kb = []
        numa_slab_kb = []
        numa_shmem_kb = []
        if self.num_numa_nodes > 1:
            print(
                "NUMA is enabled on this system; number of NUMA nodes is "
                f"{self.num_numa_nodes}.")
        else:
            print("NUMA is not enabled on this system.")
            return
        print("Per-node memory usage summary (in KB):")
        self.__print_numastat_headers()
        while i < self.num_numa_nodes:
            nodei_meminfo = self.read_text_file(
                f"/sys/devices/system/node/node{i}/meminfo")
            for line in nodei_meminfo.splitlines():
                if line.find("MemTotal:") != -1:
                    numa_memtotal_kb.append(self.__get_numa_meminfo_val(line))
                if line.find("MemFree:") != -1:
                    numa_memfree_kb.append(self.__get_numa_meminfo_val(line))
                if line.find("FilePages:") != -1:
                    numa_filepages_kb.append(self.__get_numa_meminfo_val(line))
                if line.find("AnonPages:") != -1:
                    numa_anonpages_kb.append(self.__get_numa_meminfo_val(line))
                if line.find("Slab:") != -1:
                    numa_slab_kb.append(self.__get_numa_meminfo_val(line))
                if line.find("Shmem:") != -1:
                    numa_shmem_kb.append(self.__get_numa_meminfo_val(line))
            i += 1

        self.__print_pretty_numastat_kb("MemTotal", numa_memtotal_kb)
        self.__print_pretty_numastat_kb("MemFree", numa_memfree_kb)
        self.__print_pretty_numastat_kb("FilePages", numa_filepages_kb)
        self.__print_pretty_numastat_kb("AnonPages", numa_anonpages_kb)
        self.__print_pretty_numastat_kb("Slab", numa_slab_kb)
        self.__print_pretty_numastat_kb("Shmem", numa_shmem_kb)
        self.__print_hugepage_stats_by_node()

        if not self.__is_low_memfree(numa_memfree_kb, numa_memtotal_kb):
            # No need to check for imbalance if there's plenty of free memory
            # on all NUMA nodes.
            return

        self.__check_for_numa_imbalance("MemFree", numa_memfree_kb)
        self.__check_for_numa_imbalance("FilePages", numa_filepages_kb)
        self.__check_for_numa_imbalance("AnonPages", numa_anonpages_kb)
        self.__check_for_numa_imbalance("Slab", numa_slab_kb)
        self.__check_for_numa_imbalance("Shmem", numa_shmem_kb)

    def memstate_check_numa(
            self, num=constants.NUM_TOP_NUMA_MAPS, verbose=False):
        """Display NUMA information and check some state."""
        print("NUMA STATISTICS:")
        self.__display_numa_meminfo()
        print("")
        if verbose is False:
            return
        if self.num_numa_nodes > 1:
            print("Reading /proc/<pid>/numa_maps (this could take " +
                  "a while) ...\n")
            self.process_numa_maps(num)
