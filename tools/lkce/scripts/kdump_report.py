#!/usr/bin/env python3
#
# Copyright (c) 2023-2024, Oracle and/or its affiliates.
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
Program that runs inside kdump kernel
Program to run the crash or corelens utility inside the kdump kernel
"""
import glob
import time
import os
import platform
import re
import shutil
import subprocess  # nosec
import sys
from typing import Optional, List

MIN_SYSTEM_MEMORY_KB = 768 << 10


def read_corelens_args(filename: str) -> Optional[List[str]]:
    """Read command line arguments file and return the args as a list"""
    try:
        with open(filename, 'r') as f:
            args = [
                w.strip()
                for l in f
                for w in l.split()
                if not l.startswith('#')
            ]
            return args
    except OSError as e:
        print(f"kdump_report: Unable to operate on file: {filename}: {e}")
        return None


def get_system_memory() -> int:
    """Return the amount of total memory in KB"""
    try:
        with open("/proc/meminfo", "r") as f:
            for l in f:
                if l.startswith("MemTotal:"):
                    w = l.split()
                    if len(w) < 3:
                        return -1
                    return int(w[1])
    except OSError as e:
        print(f"Unable to read memory info from /proc/meminfo: {e}")
    return -1


class KdumpReport:
    """Class to include all kdump report related functionality"""
    # pylint: disable=too-many-instance-attributes
    def __init__(self) -> None:
        """Constructor for KdumpReport class"""
        self.vmlinux = ""
        self.vmcore = "/proc/vmcore"
        self.kdump_kernel_ver = platform.uname().release
        self.report_cmd = "corelens"

        self.kdump_report_home = "/etc/oled/lkce"
        self.kdump_report_config_file = self.kdump_report_home + "/lkce.conf"
        self.kdump_report_crash_cmds_file = self.kdump_report_home + \
            "/crash_cmds_file"
        self.kdump_report_corelens_args_file = self.kdump_report_home + \
            "/corelens_args_file"
        self.kdump_report_out = "/var/oled/lkce"
        self.timedout_action = "reboot -f"
        self.system_memory_kb = get_system_memory()

        # default values
        self.vmlinux_path = "/usr/lib/debug/lib/modules/" + \
            self.kdump_kernel_ver + "/vmlinux"
        self.crash_cmds_file = self.kdump_report_crash_cmds_file

        self.corelens_args_file = self.kdump_report_corelens_args_file
        self.max_out_files = "50"
        self.corelens_args = ["-a"]

        self.read_config(self.kdump_report_config_file)

        self.kdump_report_out_file = self.kdump_report_out + \
            f"/{self.report_cmd}_" + time.strftime("%Y%m%d-%H%M%S") + ".out"
    # def __init__

    def read_config(self, filename: str) -> int:
        """Read config file and update the class variables"""
        if not os.path.exists(filename):
            sys.exit(1)

        try:
            file = open(filename, "r")
        except OSError:
            print(f"kdump_report: Unable to operate on file: {filename}")
            return 1

        for line in file.readlines():
            if re.search("^#", line):  # ignore lines starting with '#'
                continue

            # trim space/tab/newline from the line
            line = re.sub(r"\s+", "", line)

            entry = re.split("=", line)

            if "report_cmd" in entry[0] and entry[1]:
                self.report_cmd = entry[1]
                if self.report_cmd not in ("crash", "corelens"):
                    print("kdump_report: Invalid report command: %s" %
                          {self.report_cmd})
                    return 1

            elif "vmlinux_path" in entry[0] and entry[1]:
                self.vmlinux_path = entry[1]

            elif "crash_cmds_file" in entry[0] and entry[1]:
                self.crash_cmds_file = entry[1]

            elif "corelens_args_file" in entry[0] and entry[1]:
                self.corelens_args_file = entry[1]

            elif "max_out_files" in entry[0] and entry[1]:
                self.max_out_files = entry[1]

            elif "lkce_outdir" in entry[0] and entry[1]:
                self.kdump_report_out = entry[1]

        return 0
    # def read_config

    def get_vmlinux(self, verbose: bool = True) -> int:
        """Check for vmlinux in config path and then in default location.

        Report error if not found
        """
        vmlinux_1 = self.vmlinux_path
        vmlinux_2 = "/usr/lib/debug/lib/modules/" + \
                    self.kdump_kernel_ver + "/vmlinux"

        if os.path.isfile(vmlinux_1):
            self.vmlinux = vmlinux_1
        elif os.path.isfile(vmlinux_2):
            self.vmlinux = vmlinux_2
        else:
            if verbose:
                print("kdump_report: vmlinux not found in following",
                      "locations:")
                print(f"kdump_report: {vmlinux_1}")
                print(f"kdump_report: {vmlinux_2}")
            return 1
        if verbose:
            print(f"kdump_report: vmlinux found at {self.vmlinux}")
        return 0
    # def get_vmlinux

    def run_crash(self) -> int:
        """Run the crash utility against vmcore"""
        crash_path = shutil.which("crash")
        if not crash_path:
            print("kdump_report: 'crash' executable not found")
            return 1

        if self.get_vmlinux():
            sys.exit(1)
        if not os.path.isfile(self.crash_cmds_file):
            print(f"kdump_report: {self.crash_cmds_file} not found")
            return 1

        if self.system_memory_kb > 0:
            if self.system_memory_kb < MIN_SYSTEM_MEMORY_KB:
                print("kdump_report: Not running crash",
                      "because there is insufficient available memory.\n"
                      "Refer to the oled-lkce manual page for",
                      "more information.")
                return 0

        os.makedirs(self.kdump_report_out, exist_ok=True)

        args = (crash_path, self.vmlinux, self.vmcore, "-i",
                self.crash_cmds_file)
        print(f"kdump_report: Executing '{' '.join(args)}'; output file "
              f"'{self.kdump_report_out_file}'")

        ret = 0
        with open(self.kdump_report_out_file, "w") as output_fd:
            r = subprocess.run(args, close_fds=True, stdout=output_fd,
                               stderr=output_fd, stdin=subprocess.DEVNULL,
                               shell=False, check=True)  # nosec
            ret = r.returncode
        return ret
    # def run_crash

    def run_corelens(self) -> int:
        """Run the corelens utility against vmcore"""
        corelens_path = shutil.which("corelens")
        if not corelens_path:
            print("kdump_report: 'corelens' executable not found")
            return 1

        using_dwarf = self.get_vmlinux(verbose=False) == 0
        if using_dwarf and self.system_memory_kb > 0:
            if self.system_memory_kb < MIN_SYSTEM_MEMORY_KB:
                print("kdump_report: Not running corelens",
                      "because there is insufficient available memory.\n"
                      "Refer to the oled-lkce manual page for",
                      "more information.")
                return 0

        os.makedirs(self.kdump_report_out, exist_ok=True)

        args = [corelens_path, self.vmcore]
        corelens_args = read_corelens_args(self.corelens_args_file)
        if corelens_args:
            args.extend(corelens_args)
        else:
            args.extend(self.corelens_args)
        print(f"kdump_report: Executing '{' '.join(args)}'; output file "
              f"'{self.kdump_report_out_file}'")

        ret = 0
        with open(self.kdump_report_out_file, "w") as output_fd:
            r = subprocess.run(args, close_fds=True, stdout=output_fd,
                               stderr=output_fd, stdin=subprocess.DEVNULL,
                               shell=False, check=True)  # nosec
            ret = r.returncode
        return ret
    # def run_corelens

    def run_report(self) -> int:
        """Run either crash or corelens, depending on configuration"""
        if self.report_cmd == "crash":
            return self.run_crash()
        return self.run_corelens()
    # def run_report_

    def clean_up(self) -> None:
        """Clean up old corelens reports to save space"""
        max_files = int(self.max_out_files)
        report_files = []
        for p in ("crash*.out", "corelens*.out"):
            report_files.extend(glob.glob(f"{self.kdump_report_out}/{p}"))

        if len(report_files) > max_files:
            print(f"kdump_report: found more than {max_files}[max_out_files] "
                  "out files. Deleting older ones")

            report_files.sort(key=lambda x: os.path.getctime(x), reverse=True)
            for f in report_files[max_files:]:
                try:
                    os.remove(f)
                except OSError as e:
                    print(f"kdump_report: Error removing file {f}: {e}")
    # def clean_up
# class KDUMP_REPORT


def main() -> int:
    """Main routine"""
    kdump_report = KdumpReport()

    print("kdump_report: kdump_report is enabled to run")
    kdump_report.run_report()
    kdump_report.clean_up()
    sys.exit(0)
# def main


if __name__ == '__main__':
    main()
