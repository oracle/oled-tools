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
"""Helper module to retrieve hugepages info."""

from __future__ import print_function
import glob
import os
from memstate_lib import Base


class Hugepages(Base):
    """
    Extracts the number of hugepages of all sizes (reserved and free) on
    all NUMA nodes.
    """
    def __init__(self):
        self.hp_nr_dict = {}
        self.hp_free_dict = {}

    def __read_nr_hugepages(self):
        for path in glob.glob("/sys/devices/system/node/node*"):
            # Extract node0 from /sys/devices/system/node/node0, then extract
            # the number following "node"
            current_node = path.split("/")[-1][4:]
            hp_dir = os.path.join(path, "hugepages/hugepages*")
            for hp_path in glob.glob(hp_dir):
                # We're attempting to extract hugepage size from hp_path (for
                # 2MB/1GB pages):
                # /sys/devices/system/node/node*/hugepages/hugepages-2048kB
                # /sys/devices/system/node/node*/hugepages/hugepages-1048576kB
                hp_size = hp_path.split("/")[-1][10:]
                hp_size_kb = hp_size[:-2]

                # Read nr_hugepages and free_hugepages for each NUMA node
                nr_path = os.path.join(hp_path, "nr_hugepages")
                free_path = os.path.join(hp_path, "free_hugepages")
                nr_val = int(self.read_text_file(nr_path))
                free_val = int(self.read_text_file(free_path))
                key = hp_size_kb + "_" + current_node
                self.hp_nr_dict[key] = nr_val
                self.hp_free_dict[key] = free_val

    def get_total_hugepages_gb(self):
        """Get total amount of memory in hugepages in GBs."""
        self.__read_nr_hugepages()
        hp_nr_total_kb = 0
        for key in self.hp_nr_dict:
            (hp_size, _) = key.split("_")
            val = self.hp_nr_dict[key]
            hp_nr_total_kb = \
                hp_nr_total_kb + round(float(val * int(hp_size)), 1)
        return round(self.convert_kb_to_gb(hp_nr_total_kb), 1)

    @staticmethod
    def __create_matrix(hp_dict):
        hp_size_arr = []

        # Determine number of hugepage sizes
        for key in hp_dict:
            (size, _) = key.split("_")
            hp_size_kb = int(size)
            if hp_size_kb not in hp_size_arr:
                hp_size_arr.append(hp_size_kb)

        # Create a dictionary of lists, where each list in the dictionary is
        # accessed using the hugepage_size as key, and the list has
        # nr_hugepages (of that size) for each NUMA node.
        # For instance, on a 2-node system with both 2 MB and 1 GB hugepages,
        # hp_matrix will be something similar to:
        #   {'2048': [22212, 22272], '1048576': [99, 100]}

        hp_matrix = {}
        for key in hp_dict:
            (size, _) = key.split("_")
            val = round(hp_dict[key] * int(size))
            hp_matrix.setdefault(size, []).append(int(val))

        return hp_matrix

    def get_nr_hugepages_matrix_kb(self):
        """Get total KBs of hugepages per hugepage size per NUMA node.

        Return a dictionary with following format:
           {
             <HP_size1>: [<KB_node0>, <KB_node1>, ..., <KB_nodeN],
             <HP_size2>: [<KB_node0>, <KB_node1>, ..., <KB_nodeN],
             ...
           }

        Where <HP_sizeX> are the different hugepage sizes (e.g. '2048' for 2MB
        hupepages and '1048576' for 1GB pages) and <KB_nodeN> is the amount of
        memory, in KBs, of hugepages of the given size in NUMA node N.
        """
        self.__read_nr_hugepages()
        hp_dict = self.hp_nr_dict
        return self.__create_matrix(hp_dict)

    def get_free_hugepages_matrix_kb(self):
        """Get total free KBs of hugepages per hupepage size per NUMA node.

        See get_nr_hugepages_matrix_kb() for details of the information
        returned.
        """
        self.__read_nr_hugepages()
        hp_dict = self.hp_free_dict
        return self.__create_matrix(hp_dict)
