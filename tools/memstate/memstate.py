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
"""Main memstate driver."""

import time
import sys
import os
import signal
import argparse
from memstate_lib import Base
from memstate_lib import LockFile
from memstate_lib import Meminfo
from memstate_lib import Slabinfo
from memstate_lib import Buddyinfo
from memstate_lib import Numa
from memstate_lib import Hugepages
from memstate_lib import Pss
from memstate_lib import Swap
from memstate_lib import Logfile
from memstate_lib import Rss
from memstate_lib import constants


class Memstate(Base):
    """Class with top-level memstate logic."""
    # pylint: disable=too-many-instance-attributes
    def __init__(self):
        self.meminfo = Meminfo()
        self.numa = Numa()
        self.hugepages = Hugepages()
        self.slabinfo = Slabinfo()
        self.buddyinfo = Buddyinfo()
        self.pss = Pss()
        self.rss = Rss()
        self.swap = Swap()
        self.print_header = True

    def print_time(self):
        """Print time ('zzz <time>')."""
        print("zzz " + self.get_current_time())

    def memstate_header(self):
        """Print memstate header to stdout if it has not been printed yet."""
        if self.print_header:
            self.print_header_l0("Gathering memory usage data")
            print("Kernel version: " + self.get_kernel_ver())
            print("Hostname: " + self.get_hostname())
            self.print_time()
            print("")
            self.print_header = False

    def memstate_opt_none(self):
        """
        Display the memory usage summary, and run a quick health check.
        """
        self.meminfo.display_usage_summary()
        self.numa.memstate_check_numa()
        self.slabinfo.memstate_check_slab()
        self.rss.memstate_check_rss()
        self.check_health()

    def memstate_opt_all(self, verbose):
        """
        Display memory usage summary, along with a list of user processes
        consuming most memory and swap, and run a quick health check.
        """
        self.meminfo.display_usage_summary()
        if verbose:
            self.numa.memstate_check_numa()
            self.slabinfo.memstate_check_slab(constants.NO_LIMIT)
            self.rss.memstate_check_rss(constants.NO_LIMIT)
            self.swap.memstate_check_swap(constants.NO_LIMIT)
        else:
            self.numa.memstate_check_numa()
            self.slabinfo.memstate_check_slab()
            self.rss.memstate_check_rss()
            self.swap.memstate_check_swap()
        self.check_health()

    def memstate_opt_pss(self, pid, verbose=False):
        """Run PSS checks."""
        if verbose:
            self.pss.memstate_check_pss(pid, constants.NO_LIMIT)
        else:
            self.pss.memstate_check_pss(pid)

    def memstate_opt_swap(self):
        """Run Swap checks."""
        self.swap.memstate_check_swap(constants.NO_LIMIT)

    def memstate_opt_slab(self, verbose=False):
        """Run Slabinfo checks."""
        if verbose:
            self.slabinfo.memstate_check_slab(constants.NO_LIMIT)
        else:
            self.slabinfo.memstate_check_slab()

    def memstate_opt_numa(self, verbose=False):
        """Run NUMA checks."""
        self.numa.memstate_check_numa(constants.NO_LIMIT, verbose)

    def check_health(self):
        """
        Check the various memory usage stats against the acceptable thresholds.
        """
        self.print_header_l1("Health checks")
        self.check_sysctl_config()
        self.meminfo.check_pagetables_size()
        self.check_rds_cache_size()
        self.check_for_pmem()
        self.meminfo.check_unaccounted_memory()
        self.meminfo.check_committed_as()
        self.buddyinfo.check_fragmentation_status(self.numa.num_numa_nodes)
        self.buddyinfo.check_vmstat()

    def check_sysctl_config(self):
        """Check sysctl configuration."""
        self.check_mfk_setting()
        self.check_wsf()

    def check_mfk_setting(self):
        """
        The value of the sysctl vm.min_free_kbytes should be:
            - max(0.5% of RAM, 1 GB per NUMA node)
        If it's set to a lower value (or if it's too high), print a warning.
        """
        try:
            current_mfk_kb = int(
                self.read_text_file("/proc/sys/vm/min_free_kbytes").strip())
        except ValueError:
            self.print_warn(
                "Unable to read/verify vm.min_free_kbytes setting.\n")
            return

        current_mfk_percent = round(
            int(current_mfk_kb) / self.meminfo.get_total_ram_kb() * 100, 3)
        mfk_val1_kb = 0.5 * constants.PERCENT * self.meminfo.get_total_ram_kb()
        per_numa_node_config = constants.ONE_GB
        if self.numa.num_numa_nodes > 1:
            mfk_val2_kb = (
                (per_numa_node_config * self.numa.num_numa_nodes) /
                constants.ONE_KB)
        else:
            mfk_val2_kb = 0
        recommended_mfk_kb = int(max(mfk_val1_kb, mfk_val2_kb))

        mfk_warning_str = (
            "Recommended value for vm.min_free_kbytes is "
            f"{recommended_mfk_kb} KB "
            "(max[0.5% of RAM, 1 GB per NUMA node]);\n"
            f"current value is {current_mfk_kb} KB ({current_mfk_percent}%).")
        mfk_update_str = (
            "Please update to the recommended value using either 'sysctl -w' "
            "or in /etc/sysctl.conf.")
        if recommended_mfk_kb > current_mfk_kb:
            self.print_warn(mfk_warning_str)
            print(
                "There is a higher possiblity of compaction stalls due to "
                "fragmentation if free memory dips too low.")
            print(mfk_update_str)
        elif current_mfk_kb >= 2*recommended_mfk_kb:
            self.print_warn(mfk_warning_str)
            print(
                "There is a higher possibility of the OOM-killer being invoked"
                " if memory usage goes up.")
            print(mfk_update_str)
        else:
            self.print_info(
                f"The value of vm.min_free_kbytes is: {current_mfk_kb} KB.")

    def check_wsf(self):
        """Check that watermark_scale_factor is not too large.

        Print a warning if it is.
        """
        wsf = int(self.read_text_file("/proc/sys/vm/watermark_scale_factor"))

        if wsf > constants.WSF_THRESHOLD:
            print("")
            self.print_warn(
                f"vm.watermark_scale_factor has been increased to {wsf}.")

    def check_rds_cache_size(self):
        """Check that RDS cache size is not too large.

        Print a warning if it is.
        """
        rds_size_gb = self.meminfo.get_rds_cache_gb()
        max_rds_size = (
            constants.RDS_USE_PERCENT * (
                (self.meminfo.get_total_ram_gb()
                 - self.hugepages.get_total_hugepages_gb())))
        if (rds_size_gb > 0 and rds_size_gb >= max_rds_size):
            print("")
            self.print_warn(f"RDS receive cache is large: {rds_size_gb} GB.")

    def check_for_pmem(self):
        """
        Calculate size of persistent memory device/namespace metadata.
        Depending on how the namespace was created (see -M below), the metadata
        (which consists of one struct page (64 bytes) per 4KB of pmem space)
        can be stored in RAM. This can be a significant amount of RAM, and
        could show up as significant memory usage by kernel. However, we do not
        know for sure (at this point) if pmem metadata is indeed stored in RAM
        -- that depends on what options were used by the admin while creating
        the namespace. The goal of this function is to just print a warning so
        that the admin can verify or rule out that pmem metadata is stored in
        RAM.

        nvme-create-namespace option:
        -M, --map: For 'fsdax' or 'devdax' namespaces, define whether metadata
        is stored in volatile memory (mem) or persistent storage (dev)
        """
        pmem_size = 0
        daxctl_output = self.exec_cmd("daxctl list")
        for line in daxctl_output.splitlines():
            if line.strip().find("size") != -1:
                pmem_size += int(line.split(":")[1][:-1])
        if pmem_size == 0:
            return
        pmem_numpages = self.convert_bytes_to_numpages(pmem_size)
        pmem_metadata_gb = round(
            self.convert_bytes_to_gb(
                pmem_numpages * constants.STRUCT_PAGE_SIZE),
            1)
        if (self.meminfo.get_unaccounted_memory_gb() != 0
                and (self.meminfo.get_unaccounted_memory_gb() >=
                     pmem_metadata_gb)):
            print("")
            self.print_warn(
                "There could be a potential pmem metadata usage of "
                f"{pmem_metadata_gb}GB; please check how the pmem namespace"
                "was created.")


def setup_signal_handlers():
    """
    Catch ctrl-c and other signals that can cause this script to terminate,
    and exit after any cleanup.
    """
    signal.signal(signal.SIGHUP, exit_handler)
    signal.signal(signal.SIGTERM, exit_handler)
    signal.signal(signal.SIGINT, exit_handler)


def exit_handler(_signum, _frame):
    """Signal handler that exits the process."""
    exit_with_msg("Received interrupt, exiting!")


def exit_with_msg(msg=""):
    """Print a message to stdout and exit."""
    sys.stdout = sys.__stdout__
    sys.stderr = sys.__stderr__
    print(msg)
    sys.exit(0)


def check_if_root():
    """Check we are running as root user; exit otherwise."""
    if os.geteuid() != 0:
        exit_with_msg("This tool should be run as root.")


def validate_args(args):
    """Validate user-provided arguments.

    Checks if there are any invalid combinations of arguments:
      - "all" cannot be mixed with other options (except "verbose" and
        "frequency")
      - "verbose" can be combined with almost any other option except "swap"
      - "frequency" cannot be used if an input file is provided (for "slab" or
        "numa")
      - "frequency" cannot be used with "numa" for live capture too (it's too
        expensive)
    """
    if args.verbose and args.swap:
        print(
            "Option -w/--swap does not support -v/--verbose; "
            "see usage for more details.")
        return 1
    if args.frequency:
        if args.numa is not None:
            print(
                "Option -n/--numa does not support -f/--frequency; "
                "see usage for more details.")
            return 1
        if (args.slab is not None and args.slab != 'nofile'):
            print(
                "Option -s/--slab INPUT_FILE does not support -f/--frequency; "
                "see usage for more details.")
            return 1
    if args.all:
        # pylint: disable=too-many-boolean-expressions
        if (args.pss or args.swap
                or (args.numa is not None and args.numa != 'nofile')
                or (args.slab is not None and args.slab != 'nofile')):
            print(
                "Option -a/--all can only be combined with -v/--verbose and "
                "-f/--frequency; see usage for more details.")
            return 1
    return 0


def main():
    """Main function for memstate driver."""
    # pylint: disable=too-many-statements,too-many-branches
    parser = argparse.ArgumentParser(
        prog='oled memstate',
        description=(
            'memstate: Capture and analyze memory usage data on this system.'))

    parser.add_argument(
        "-p", "--pss", metavar="PID", nargs="?",
        const=constants.DEFAULT_SHOW_PSS_SUMMARY,
        help="display per-process memory usage")
    parser.add_argument(
        "-w", "--swap", action="store_true",
        help="display per-process swap usage")
    parser.add_argument(
        "-s", "--slab", metavar="FILE", help="analyze/display slab usage",
        nargs="?", const="nofile")
    parser.add_argument(
        "-n", "--numa", metavar="FILE", nargs="?", const="nofile",
        help="analyze/display NUMA stats")
    parser.add_argument(
        "-a", "--all", help="display all data", action="store_true")
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="verbose data capture; combine with other options")
    parser.add_argument(
        "-f", "--frequency", metavar="INTERVAL", nargs="?",
        const=constants.DEFAULT_INTERVAL,
        help="interval at which data should be collected (default: 30s)")

    # Before processing arguments:
    # 0. Verify that the user is running as root.
    # 1. Set up signal handlers to catch termination signals and exit cleanly.
    # 2. Check if another instance of this script is running; if so, terminate.
    #    This is to prevent log corruption as well as hogging too many system
    #    resources.
    # 3. Validate arguments.
    # 4. Set up output logfile:
    # 4a. If this is to be run periodically, redirect stdout to logfile.
    # 4b. If this is a one-shot run, print to console; user will redirect
    #     output if desired.

    check_if_root()
    setup_signal_handlers()
    _lock = LockFile()  # noqa: F841

    interval = 0
    args_passed = False
    opt_verbose = False
    args = parser.parse_args()
    if validate_args(args):
        parser.print_help()
        return

    # One-time processing of input file (not live capture).
    if args.numa is not None and args.numa != 'nofile':
        print("Reading numa_maps from file: " + args.numa)
        numa = Numa(args.numa)
        numa.process_numa_maps(constants.NO_LIMIT)
        return

    if args.slab is not None and args.slab != 'nofile':
        print("Reading slabinfo from file: " + args.slab)
        slabinfo = Slabinfo(args.slab)
        slabinfo.memstate_check_slab(constants.NO_LIMIT)
        return

    # Live memstate data capture (either one-shot or periodic).
    if args.frequency is not None:
        interval = int(args.frequency)
    if args.verbose:
        opt_verbose = True

    # If this is to be run periodically, redirect stdout to logfile.
    if interval >= 5:
        logfile = Logfile(interval)
        sys.stdout = logfile
    elif interval < 0:
        print(
            "Invalid interval value; please specify a positive number "
            "(unit: seconds).")
        return
    elif 0 < interval < 5:
        print(
            "Invalid interval value; the lowest valid interval is 5 seconds.")
        return

    # Start live memstate data capture on this system; run in a loop until
    # terminated by user.

    memstate = Memstate()
    memstate.memstate_header()

    while True:
        if args.all:
            args_passed = True
            memstate.memstate_opt_all(opt_verbose)
        if args.pss:
            args_passed = True
            memstate.memstate_opt_pss(args.pss, opt_verbose)
        if args.swap:
            args_passed = True
            memstate.memstate_opt_swap()
        if args.slab is not None:
            args_passed = True
            memstate.memstate_opt_slab(opt_verbose)
        if args.numa is not None:
            args_passed = True
            memstate.memstate_opt_numa(opt_verbose)
        if not args_passed:
            if opt_verbose:
                exit_with_msg(
                    "Option -v/--verbose should be used in combination with "
                    "other options; see -h/--help or the man page for more "
                    "details.")
                return
            memstate.memstate_opt_none()

        if interval == 0:
            break
        time.sleep(interval)
        logfile.rotate_file()
        memstate.print_time()


if __name__ == '__main__':
    main()
