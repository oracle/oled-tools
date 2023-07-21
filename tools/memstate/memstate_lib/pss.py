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
    """ Analyzes output from /proc/<pid>/smaps_rollup """

    def __get_total_pss_gb(self, pss_list=None):
        if pss_list is None:
            self.print_error("Process smaps data unavailable!")
            return None
        pss_kb = sum(pss_list.values())
        return self.convert_kb_to_gb(pss_kb)

    def __display_top_proc_pss(self, num):
        """
        Add up all the Pss values per-process, from /proc/<pid>/smaps_rollup.
        Sort in descending order, and display the top <num> consumers.
        @param num: The number of top memory consumers to print.
        """
        # pylint: disable=too-many-locals
        pss = {}
        pss_sorted = {}
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
            p_comm_str = self.read_text_file(
                f"/proc/{pid}/comm", on_error=str(pid))
            self.print_pretty_kb(
                f"{p_comm_str.strip()}(pid:{pid})", pss_sorted[pid])
            num_printed += 1
        print("")
        print(
            "Total memory used by all processes: "
            f"{self.__get_total_pss_gb(pss_sorted)} GB")

    def memstate_check_pss(self, num=constants.NUM_TOP_MEM_USERS):
        """Check per-process memory usage."""
        print(
            "Note: this processing can take a while - depending on the "
            "system config, load, etc.\nYou might also notice this script "
            "consuming > 95% CPU during this run, for a few minutes.\nSo it "
            "is not recommended to invoke -p/--pss too often.\n")
        if num == constants.NO_LIMIT:
            self.print_header_l1("Process memory usage")
        else:
            self.print_header_l1("Top " + str(num) + " memory consumers")
        self.__display_top_proc_pss(num)
        print("")
