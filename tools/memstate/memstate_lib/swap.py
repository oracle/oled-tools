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

from collections import OrderedDict
import glob
import re
import time
from memstate_lib import Base
from memstate_lib import Meminfo
from memstate_lib import constants

class Swap(Base):
    """ Extracts per-process swap usage from /proc/<pid>/status """

    def __display_top_vmswap(self, num):
        """
        Sum up all VmSwap per-process from /proc/<pid>/status.
        Sort in descending order, and display the top <num> users of swap.
        @param num: The number of top swap users to print.
        """
        swap = {}
        num_printed = 0
        meminfo = Meminfo()
        swap_used_kb = meminfo.get_swap_used_kb()
        if swap_used_kb == 0:
            print("No swap usage found.")
            return
        start_time = time.time()
        num_files_scanned = 0
        for filestr in glob.glob("/proc/*/status"):
            pid = filestr.split("/")[2]
            name_search_str = '^Name:\s*.+'
            vmswap_search_str = '^VmSwap:\s*[0-9]+'
            try:
                with open(filestr, 'r') as f:
                    data = f.read()
                    f.close()
                    num_files_scanned = num_files_scanned + 1
                name = re.search(name_search_str, data)
                hdr = str(name.group(0).split(":")[1].strip()) + "(pid=" + pid + ")"
                match_list = re.findall(vmswap_search_str, data, flags=re.MULTILINE)
                for match in match_list:
                    v = match.split(":")[1].strip()
                    val = int(swap.get(hdr, 0)) + int(v)
                    swap.update({hdr: val})
                time.sleep(0.01) # Sleep for 10 ms to avoid hogging CPU
            except IOError:
                pass
        end_time = time.time()
        self.log_debug("Time taken to extract swap usage values from " + str(num_files_scanned) \
                + " /proc/<pid>/status files is " + str(round(end_time - start_time)) \
                + " second(s).")
        swap_sorted = OrderedDict(sorted(list(swap.items()), key=lambda x:x[1], reverse=True))
        for s in swap_sorted:
            if num_printed >= num:
                break
            if int(swap_sorted[s]) == 0:
                if num_printed == 0:
                    print("No swap usage found.")
                break
            self.print_pretty_kb(s, swap_sorted[s])
            num_printed += 1

    def memstate_check_swap(self, num=constants.NUM_TOP_SWAP_USERS):
        if num == constants.NO_LIMIT:
            self.print_header_l1("Swap users")
        else:
            self.print_header_l1("Top " + str(num) + " swap space consumers")
        self.__display_top_vmswap(num)
        print("")
