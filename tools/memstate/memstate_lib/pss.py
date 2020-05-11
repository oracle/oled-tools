#!/usr/bin/python -tt
#
# Copyright (c) 2021, Oracle and/or its affiliates.
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

import time
import os
from collections import OrderedDict
from memstate_lib import Base
from memstate_lib import constants

class Pss(Base):
    """ Analyzes output from /proc/<pid>/smaps """

    def __get_total_pss_gb(self, pss_list=None):
        if pss_list is None:
            self.print_error("Process smaps data unavailable!")
            return None
        pss_kb = 0
        for p in pss_list:
            pss_kb += pss_list[p]
        return self.convert_kb_to_gb(pss_kb)

    def __display_top_proc_pss(self, num):
        """
        Add up all the Pss values per-process, from /proc/<pid>/smaps.
        Sort in descending order, and display the top <num> consumers.
        @param num: The number of top memory consumers to print.
        """
        mem = {}
        mem_sorted = {}
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
                smaps_file = os.path.join(abspath, "smaps")
                if not os.path.exists(smaps_file):
                    continue
                pid = smaps_file.split("/")[2]
                data = ""
                with open(smaps_file, 'r') as f:
                    data = f.read()
                    f.close()
                    num_files_scanned = num_files_scanned + 1
                for line in data.splitlines():
                    if line.startswith("Pss:"):
                        segment_mem = line.split(":")[1].strip().split()[0].strip()
                        if pid in list(mem.keys()):
                            mem[pid] = int(mem[pid]) + int(segment_mem)
                        else:
                            mem[pid] = int(segment_mem)
                time.sleep(0.01) # Sleep for 10 ms, to avoid hogging CPU
        except IOError:
            pass

        end_time = time.time()
        self.log_debug("Time taken to extract Pss values from " + str(num_files_scanned) \
                + " /proc/<pid>/smaps files is " + str(round(end_time - start_time)) \
                + " second(s).")
        mem_sorted = OrderedDict(sorted(list(mem.items()), key=lambda x:x[1], reverse=True))
        for m in mem_sorted:
            if num != constants.NO_LIMIT and num_printed >= num:
                break
            p_comm = "cat /proc/" + str(m) + "/comm"
            p_comm_str = self.exec_cmd_ignore_err(p_comm)
            if not p_comm_str:
                p_comm_str = str(m)
            self.print_pretty_kb(p_comm_str.strip() + "(pid:" + str(m) + ")", mem_sorted[m])
            num_printed += 1
        print("")
        print("Total memory used by all processes: " \
                + str(self.__get_total_pss_gb(mem_sorted)) + " GB")

    def memstate_check_pss(self, num=constants.NUM_TOP_MEM_USERS):
        print("Note: this processing can take a long time - depending on the system config, " \
                "load, etc.\nYou might also notice this script consuming > 95% CPU during this " \
                "run, for a few minutes.\nSo it is not recommended to invoke -p/--pss or " \
                "-a/-all with -v/--verbose too often.\n")
        if num == constants.NO_LIMIT:
            self.print_header_l1("Process memory usage")
        else:
            self.print_header_l1("Top " + str(num) + " memory consumers")
        self.__display_top_proc_pss(num)
        print("")
