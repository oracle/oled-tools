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

# Author: Partha Satapathy <partha.satapathy@oracle.com>
# Co-author: Srikanth C S <srikanth.c.s@oracle.com>

"""
The vmcore_sz takes the dump_level as an argument and estimates the
vmcore size if a kernel dump is obtained at that moment. It displays
the total number of pages; pages need to be excluded depending on
the dump level and the expected VMcore size in bytes. If the dump
level is not specified, the default configured in "/etc/kdump.conf"
will be used.
"""

import sys
import os
import argparse
import subprocess  # nosec
from typing import List
from typing import NamedTuple


class MemoryUsage(NamedTuple):
    """
    This class saves page count details
    """
    zero_pg: int
    npvt_pg: int
    pctc_pg: int
    user_pg: int
    free_pg: int
    page_sz: int
    total_pg: int


def get_mem_usage() -> MemoryUsage:
    """
    Get memory data from makedumpfile --mem-usage command
    """
    mem_op = subprocess.run(["makedumpfile",  # nosec
                             "--mem-usage", "/proc/kcore"],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if mem_op.returncode != 0:
        print(mem_op.stdout.decode())
        exit_with_msg("Error executing makedumpfile.")

    mem_usg = mem_op.stdout.decode().strip().splitlines()
    header = mem_usg.pop(0).split()
    typ, pages = header.index("TYPE"), header.index("PAGES")
    page_sz = total_pg = 0
    zero_pg = npvt_pg = pctc_pg = user_pg = free_pg = 0

    for i in mem_usg:
        if "page size:" in i:
            page_sz = int(i.split()[-1])
            continue
        if "Total pages on system:" in i:
            total_pg = int(i.split()[-1])
            continue
        arr = i.split()
        if not arr:
            continue
        if arr[typ] == "ZERO":
            zero_pg = int(arr[pages])
        elif arr[typ] == "NON_PRI_CACHE":
            npvt_pg = int(arr[pages])
        elif arr[typ] == "PRI_CACHE":
            pctc_pg = int(arr[pages])
        elif arr[typ] == "USER":
            user_pg = int(arr[pages])
        elif arr[typ] == "FREE":
            free_pg = int(arr[pages])

    return MemoryUsage(zero_pg=zero_pg, npvt_pg=npvt_pg,
                       pctc_pg=pctc_pg, user_pg=user_pg,
                       free_pg=free_pg, page_sz=page_sz,
                       total_pg=total_pg)


def exit_with_msg(msg: str = "", error: int = 1) -> None:
    """"
    Error out when something undesired happens
    """
    print(msg)
    sys.exit(error)


def check_if_root() -> None:
    """
    Check if the script is run in superuser mode
    """
    if os.getuid() != 0:
        exit_with_msg("This tool should be run as root.", 1)


def parse_args() -> argparse.Namespace:
    """
    Parse the CLI arguments
    """
    parser = argparse.ArgumentParser(
        prog='vmcore_sz',
        description='vmcore_sz: Estimating vmcore size before kernel dump')

    parser.add_argument("-d", "--dump_level", help="Dump level, "
                        "an integer in range 0..31",
                        type=int)

    args = parser.parse_args()
    if args.dump_level is not None:
        if not 0 <= args.dump_level <= 31:
            parser.error(f"Invalid dump level - {args.dump_level}")

    return args


def get_default_dump_level() -> List[int]:
    """
    Get the default dump level from /etc/kdump.conf
    """
    find_def = []
    default = []
    output = subprocess.getoutput("grep core_collector /etc/kdump.conf "
                                  "| grep makedumpfile"
                                  "| grep -v '#'").strip().split()

    # The file /etc/kdump.conf can contain multiple dump levels. Such as
    # core_collector makedumpfile -l --message-level 7 -d 9,17,31
    # In cases like above, dump vmcore size estimate for all the dump levels.
    while "-d" in output:
        find_def.append(output[output.index("-d") + 1])
        output.pop(output.index("-d"))

    for ele in find_def:
        default += [int(i) for i in ele.split(',')]

    return default


def get_vmcore_size(mem: MemoryUsage, dump_level: int) -> int:
    """
    Calculate size of the vmcore based on makedumpfile --mem-usage
    """
    ex_page = dump_level
    dump_pg = mem.total_pg

    if ex_page & 0x1:
        print(f"Exclude zero pages : {mem.zero_pg}")
        dump_pg -= mem.zero_pg

    if ex_page & 0x2:
        print(f"Exclude non private cache : {mem.npvt_pg}")
        dump_pg -= mem.npvt_pg

    if ex_page & 0x4:
        print(f"Exclude private cache : {mem.pctc_pg}")
        dump_pg -= mem.pctc_pg

    if ex_page & 0x8:
        print(f"Exclude user pages : {mem.user_pg}")
        dump_pg -= mem.user_pg

    if ex_page & 0x10:
        print(f"Exclude free pages : {mem.free_pg}")
        dump_pg -= mem.free_pg

    print(f"\nTotal Pages : {mem.total_pg}")
    print(f"Pages to be dumped : {dump_pg}")
    dump_size = dump_pg * mem.page_sz

    return dump_size


def main() -> None:
    """
    main function
    """
    args = parse_args()
    check_if_root()
    dump_levels = []
    if args.dump_level is None:
        dump_levels = get_default_dump_level()
        print("\nDump level is not specified. "
              f"Using default/configured - {dump_levels}")
    else:
        dump_levels.append(args.dump_level)

    mem = get_mem_usage()

    for level in dump_levels:
        print("\n----------------------------------------------")
        if not 0 <= level <= 31:
            print(f"Invalid dump level {level}")
            continue
        print(f"Dump level = {level}")
        dump_size = get_vmcore_size(mem, level)

        print("\n----------------------------------------------")
        print(f"Expected vmcore size in bytes : {dump_size}")
        print("----------------------------------------------\n")


if __name__ == '__main__':
    main()
