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
"""Helper module to check buddyinfo."""

import re

from memstate_lib import Base
from memstate_lib import constants


class Buddyinfo(Base):
    """ Analyzes output from /proc/buddyinfo """

    def __read_buddyinfo(self):
        return self.read_text_file("/proc/buddyinfo", on_error="")

    def __display_pagetypeinfo(self):
        data = self.read_text_file("/proc/pagetypeinfo", on_error="")
        if data == "":
            self.print_error("Unable to read file /proc/pagetypeinfo")
            return

        data = data.split("\n\n")[1].splitlines()
        pgti_header = data[0]
        pgti_body = data[1:]

        print("\nPagetypeinfo:")
        print(f"  {pgti_header}")

        regex = re.compile("Normal.*(Unmovable|Movable|Reclaimable)")
        for line in pgti_body:
            if regex.search(line):
                print(f"  {line}")

    def __display_zoneinfo(self):
        interesting = ['Node', 'free', 'min', 'low', 'high']
        data = self.read_text_file("/proc/zoneinfo", on_error="")
        if data == "":
            self.print_error("Unable to read file /proc/zoneinfo")
            return
        print("\nZoneinfo:")
        for line in data.splitlines():
            if any(word in line.split() for word in interesting):
                print(f"  {line}")

    def check_fragmentation_status(self, num_numa_nodes=1):
        """
        Parses the output of 'cat /proc/buddyinfo' for each 'Normal' zone to
        check for fragmentation status. If the amount of memory available in
        higher-order chunks is <X% of free memory, it flags the system as
        fragmented. Higher-order chunks are order-4 and above; lower-order
        chunks are order-3 and below.
        """
        fragmented = False
        fragmented_nodes = []
        data = self.__read_buddyinfo()
        if data == "":
            self.print_cmd_err("cat /proc/buddyinfo")
            return
        print("\nBuddyinfo:")
        print("  (Low orders are 0-3, high orders are 4-10).")
        for line in data.splitlines():
            # We don't care about DMA or DMA32 zones.
            if "Normal" in line:
                buddyinfo = line.split()
                # buddyinfo is a list containing elements like:
                # ['Node', '0,', 'zone', 'Normal', '124', '186', '127', '127',
                #       '38', '10', '1', '3', '3', '1', '22451']
                current_node = buddyinfo[1][:-1]
                del buddyinfo[0:4]

                # Low orders are non-costly orders: 0-3.
                low = 0
                for i in range(4):
                    low += round(int(buddyinfo[i]) * self.order_x_in_kb(i))

                # High/costly orders are 4-10.
                high = 0
                for i in range(4, 11):
                    high += round(int(buddyinfo[i]) * self.order_x_in_kb(i))

                total = round(low + high, 1)
                low_percent = round((low / total) * 100, 2)
                high_percent = round((high / total) * 100, 2)

                if low_percent > constants.FRAG_LEVEL_LOW_ORDERS:
                    fragmented = True
                    if num_numa_nodes > 1:
                        fragmented_nodes.append(current_node)

                print(f"  {line}")
                print(f"  Total: {total} KB;\t\tLow: {low} KB ({low_percent}%)"
                      f";\t\tHigh: {high} KB ({high_percent}%)")
        if fragmented is True:
            print("")
            if num_numa_nodes > 1:
                node_str = ", ".join(fragmented_nodes)
                self.print_warn(
                    f"Memory on NUMA node(s) ({node_str}) is fragmented; "
                    "system may run into compaction stalls.")
            else:
                self.print_warn(
                    "Memory is fragmented - system may run into compaction "
                    "stalls.")
            self.__display_per_zone_data()

    def __display_per_zone_data(self):
        self.__display_pagetypeinfo()
        self.__display_zoneinfo()

    def check_vmstat(self):
        """
        Read /proc/vmstat and get counters related to compaction stalls, etc.
        """
        vmstat = self.read_text_file("/proc/vmstat", on_error="")
        if vmstat == "":
            self.print_error("Unable to read file /proc/vmstat")
            return
        interesting = (
            'compact', 'allocstall_normal', 'kswapd_low_wmark_hit_quickly',
            'kswapd_high_wmark_hit_quickly', 'drop_', 'oom_',
            'zone_reclaim_failed'
        )
        print("\nVmstat:")
        for line in vmstat.splitlines():
            if any(line.startswith(word) for word in interesting):
                print(f"  {line}")
        print("")
        return
