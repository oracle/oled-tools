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
"""Helper module to analyze per-process memory usage."""

import time
import os
from collections import OrderedDict
from memstate_lib import Base
from memstate_lib import constants


class Pss(Base):
    """ Analyzes output from /proc/<pid>/smaps_rollup and /proc/<pid>/smaps"""

    def __get_total_pss_gb(self, pss_list=None):
        if pss_list is None:
            self.print_error("Process smaps data unavailable!")
            return None
        pss_kb = sum(pss_list.values())
        return self.convert_kb_to_gb(pss_kb)

    def __display_top_proc_pss(self, num):
        """
        Add up all the pss values per-process, from /proc/<pid>/smaps_rollup.
        Sort in descending order, and display the top <num> consumers.
        Also display rss, private and smap_pss values.
        @param num: The number of top memory consumers to print.
        """
        # pylint: disable=too-many-locals,too-many-branches,too-many-statements
        pss = {}
        pss_sorted = {}
        rss = {}
        priv = {}
        swap_pss = {}
        num_printed = 0
        start_time = time.time()
        num_files_scanned = 0
        proc_root = '/proc/'

        try:
            files_path = os.listdir(proc_root)
            for elem in files_path:
                abspath = os.path.join(proc_root, elem)
                if not os.path.isdir(abspath):
                    continue
                smaps_file = os.path.join(abspath, "smaps_rollup")
                if not os.path.exists(smaps_file):
                    continue
                pid = smaps_file.split("/")[2]
                if pid.isdigit() is False:
                    continue
                data = ""
                try:
                    with open(smaps_file, "r", encoding="utf8") as smaps_fd:
                        data = smaps_fd.read()
                    num_files_scanned = num_files_scanned + 1
                    for line in data.splitlines():
                        if line.startswith("Pss:"):
                            pss_val = line.split(":")[1].strip().split()[0]
                            pss[pid] = int(pss_val)
                        elif line.startswith("Rss:"):
                            rss[pid] = \
                                    int(line.split(":")[1].strip().split()[0])
                        elif line.startswith("Private_Clean:"):
                            priv[pid] = priv.get(pid, 0) + \
                                    int(line.split(":")[1].strip().split()[0])
                        elif line.startswith("Private_Dirty:"):
                            priv[pid] = priv.get(pid, 0) + \
                                    int(line.split(":")[1].strip().split()[0])
                        elif line.startswith("SwapPss:"):
                            swap_pss[pid] = \
                                    int(line.split(":")[1].strip().split()[0])
                except OSError:
                    pass
        except OSError:
            pass

        end_time = time.time()
        if not pss:
            print(
                "Unable to get process memory usage summary for all "
                "processes on this system;\nplease run memstate with "
                "`-p <pid>` to get memory usage details for a specific "
                "process.")
            return

        self.log_debug(
            f"Time taken to extract Pss values from {num_files_scanned} "
            f"/proc/<pid>/smaps_rollup files is {round(end_time - start_time)}"
            " second(s).")
        pss_sorted = OrderedDict(
            sorted(pss.items(), key=lambda x: x[1], reverse=True))
        for pid in pss_sorted:
            if num != constants.NO_LIMIT and num_printed >= num:
                break
            comm_str = self.read_text_file(
                f"/proc/{pid}/comm", on_error=str(pid))
            comm_str = f"{comm_str.strip()}({pid})"
            print(
                f"{comm_str: <30}{pss_sorted[pid]: >16}"
                f"{rss[pid]: >16}{priv[pid]: >16}"
                f"{swap_pss[pid]: >16}")
            num_printed += 1
        print("")
        print(
            "Total memory used by all processes: "
            f"{self.__get_total_pss_gb(pss_sorted)} GB")

    def __display_single_process_mem(self, pid):
        # pylint: disable=too-many-locals,too-many-branches,too-many-statements
        proc_mem = {}
        proc_root = '/proc/'

        vma_arr = {}
        pss_arr = {}
        shared_arr = {}
        priv_arr = {}
        hugetlb_arr = {}
        addr = 0
        print_vmas = 0

        try:
            abspath = os.path.join(proc_root, str(pid))
            if not os.path.isdir(abspath):
                print("Could not read /proc/" + pid + "/smaps file.\n")
                return
            smaps_file = os.path.join(abspath, "smaps")
            if not os.path.exists(smaps_file):
                print("Could not read /proc/" + pid + "/smaps file.\n")
                return
            with open(smaps_file, "r", encoding="utf8") as smaps_fd:
                for line in smaps_fd:
                    # Process the VMAs
                    val = line.split()[0].strip().split("-")[0].strip()
                    try:
                        if int(val, 16):
                            addr = val
                            vma_arr[addr] = line
                    except ValueError:
                        pass
                    if line.startswith("Pss:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Pss"] = proc_mem.get("Pss", 0) + val
                        pss_arr[addr] = val
                    elif line.startswith("Shared_Clean:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Shared"] = proc_mem.get("Shared", 0) + val
                        shared_arr[addr] = shared_arr.get(addr, 0) + val
                    elif line.startswith("Shared_Dirty:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Shared"] = proc_mem.get("Shared", 0) + val
                        shared_arr[addr] = shared_arr.get(addr, 0) + val
                    elif line.startswith("Private_Clean:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Private"] = proc_mem.get("Private", 0) + val
                        priv_arr[addr] = priv_arr.get(addr, 0) + val
                    elif line.startswith("Private_Dirty:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Private"] = proc_mem.get("Private", 0) + val
                        priv_arr[addr] = priv_arr.get(addr, 0) + val
                    elif line.startswith("Shared_Hugetlb:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Hugetlb"] = proc_mem.get("Hugetlb", 0) + val
                        hugetlb_arr[addr] = hugetlb_arr.get(addr, 0) + val
                    elif line.startswith("Private_Hugetlb:"):
                        val = int(line.split(":")[1].strip().split()[0])
                        proc_mem["Hugetlb"] = proc_mem.get("Hugetlb", 0) + val
                        hugetlb_arr[addr] = hugetlb_arr.get(addr, 0) + val
        except IOError:
            pass

        comm_str = self.read_text_file(f"/proc/{pid}/comm", on_error=str(pid))
        print(f"Memory usage summary for process {comm_str.strip()}"
              f" (pid: {pid}):")
        for key, value in proc_mem.items():
            print(f"{key: <16}{value: >16} KB")

        print_vmas = 0
        print("\nDisplaying process VMAs >= 256 KB (numbers are in KB):")
        for key, value in vma_arr.items():
            # pylint: disable=too-many-boolean-expressions
            if pss_arr[key] >= constants.VMA_KB or \
                    priv_arr[key] >= constants.VMA_KB or \
                    shared_arr[key] >= constants.VMA_KB or \
                    hugetlb_arr[key] >= constants.VMA_KB:
                # Print header first
                if not print_vmas:
                    print(
                        f"{'ADDR': <28}{'PSS': >16}{'SHARED': >16}"
                        f"{'PRIV': >16}{'HUGETLB': >16}{' ': ^12}"
                        f"{'MAPPING': <32}")
                    print_vmas = 1
                addr_range = value.split()[0]
                if len(value.split()) < 6:
                    map_str = "[anon]"
                elif len(value.split()) == 6:
                    map_str = value.split()[5]
                else:  # 7 fields in vma line
                    map_str = " ".join(value.split()[-2:])
                print(
                    f"{addr_range: <28}{pss_arr[key]: >16}"
                    f"{shared_arr[key]: >16}{priv_arr[key]: >16}"
                    f"{hugetlb_arr[key]: >16}{' ': ^12}{map_str: <32}")

        if print_vmas == 0:
            print("None")

    def memstate_check_pss(self, pid, num=constants.NUM_TOP_MEM_USERS):
        """Check per-process memory usage (PSS)."""
        if pid != constants.DEFAULT_SHOW_PSS_SUMMARY:
            self.__display_single_process_mem(pid)
            return
        if num == constants.NO_LIMIT:
            hdr = "PROCESS MEMORY USAGE (in KB, ordered by PSS):"
        else:
            hdr = f"TOP {num} MEMORY CONSUMERS (in KB, ordered by PSS):"
        print(hdr)
        print(
            f"{'PROCESS(PID)': <30}{'PSS': >16}{'RSS': >16}"
            f"{'PRIVATE': >16}{'SWAP': >16}")
        self.__display_top_proc_pss(num)
        print("")
