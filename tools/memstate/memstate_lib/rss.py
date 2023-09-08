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
"""
Helper module to extract memory usage (RSS) from /proc/<pid>/status.
"""

from collections import OrderedDict
import glob
import re
import time
from memstate_lib import Base
from memstate_lib import constants


class Rss(Base):
    """ Extracts per-process stats from /proc/<pid>/status """

    def __display_top_vmrss(self, num):
        """
        Extract per-process VmRSS from /proc/<pid>/status.
        Sort in descending order, and display the top <num> memory hoggers.
        @param num: The number of top memory users to print.
        """
        # pylint: disable=too-many-locals
        rss = {}
        num_printed = 0
        start_time = time.time()
        num_files_scanned = 0
        name_search_str = r'^Name:\s*.+'
        vmrss_search_str = r'^VmRSS:\s*[0-9]+'
        for filestr in glob.glob("/proc/*/status"):
            pid = filestr.split("/")[2]
            if pid.isdigit() is False:
                continue
            try:
                with open(filestr, "r", encoding="utf8") as status_fd:
                    data = status_fd.read()
                num_files_scanned = num_files_scanned + 1
                name = re.search(name_search_str, data)
                comm = name.group(0).split(":")[1].strip()
                hdr = f"{comm}({pid})"
                match_list = re.findall(
                    vmrss_search_str, data, flags=re.MULTILINE)
                for match in match_list:
                    rss_kb = int(match.split(":")[1].strip())
                    rss[hdr] = rss_kb + rss.get(hdr, 0)
            except IOError:
                pass
        end_time = time.time()
        self.log_debug(
            f"Time taken to extract memory stats from {num_files_scanned}"
            f" /proc/<pid>/status files is {round(end_time - start_time)} "
            "second(s).")
        rss_sorted = OrderedDict(
            sorted(rss.items(), key=lambda x: x[1], reverse=True))
        for comm, rss_kb in rss_sorted.items():
            if num != constants.NO_LIMIT and num_printed >= num:
                break
            self.print_pretty_kb(comm, rss_kb)
            num_printed += 1

    def memstate_check_rss(self, num=constants.NUM_TOP_MEM_USERS):
        """Check per-process memory usage (RSS)."""
        if num == constants.NO_LIMIT:
            hdr = "PROCESS MEMORY USAGE (in KB):"
        else:
            hdr = f"TOP {num} MEMORY CONSUMERS (in KB):"
        print(hdr)
        print(f"{'PROCESS(PID)': <30}{'RSS': >12}")
        self.__display_top_vmrss(num)
        print("")
