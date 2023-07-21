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
Program that runs inside kdump kernel
Program to run crash utility inside the kdump kernel
"""
import glob
import time
import os
import platform
import re
import shutil
import subprocess  # nosec
import sys


class KdumpReport:
    """Class to include all kdump report related functionality"""
    # pylint: disable=too-many-instance-attributes
    def __init__(self) -> None:
        """Constructor for KdumpReport class"""
        self.vmlinux = ""
        self.vmcore = "/proc/vmcore"
        self.kdump_kernel_ver = platform.uname().release

        self.kdump_report_home = "/etc/oled/lkce"
        self.kdump_report_config_file = self.kdump_report_home + "/lkce.conf"
        self.kdump_report_crash_cmds_file = self.kdump_report_home + \
            "/crash_cmds_file"
        self.kdump_report_out = "/var/oled/lkce"
        self.kdump_report_out_file = self.kdump_report_out + \
            "/crash_" + time.strftime("%Y%m%d-%H%M%S") + ".out"
        self.timedout_action = "reboot -f"

        # default values
        self.kdump_report = "yes"
        self.vmlinux_path = "/usr/lib/debug/lib/modules/" + \
            self.kdump_kernel_ver + "/vmlinux"
        self.crash_cmds_file = self.kdump_report_crash_cmds_file
        self.max_out_files = "50"

        self.read_config(self.kdump_report_config_file)
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
            if "vmlinux_path" in entry[0] and entry[1]:
                self.vmlinux_path = entry[1]

            elif "crash_cmds_file" in entry[0] and entry[1]:
                self.crash_cmds_file = entry[1]

            elif "enable_kexec" in entry[0] and entry[1]:
                self.kdump_report = entry[1]

            elif "max_out_files" in entry[0] and entry[1]:
                self.max_out_files = entry[1]

        return 0
    # def read_config

    def get_vmlinux(self) -> int:
        """Check for vmlinux in config path and then in default location.

        Report error if not found
        """
        vmlinux_1 = self.vmlinux_path
        vmlinux_2 = "/usr/lib/debug/lib/modules/" + \
                    self.kdump_kernel_ver + "/vmlinux"

        if os.path.exists(vmlinux_1) and os.path.isfile(vmlinux_1):
            self.vmlinux = vmlinux_1
        elif os.path.exists(vmlinux_2) and os.path.isfile(vmlinux_2):
            self.vmlinux = vmlinux_2
        else:
            print("kdump_report: vmlinux not found in following locations.")
            print("kdump_report: %s" % vmlinux_1)
            print("kdump_report: %s" % vmlinux_2)
            sys.exit(1)

        print("kdump_report: vmlinux found at %s" % self.vmlinux)
        return 0
    # def get_vmlinux

    def run_crash(self) -> int:
        """Run crash utility against vmcore"""
        crash_path = shutil.which("crash")

        if not crash_path:
            print("kdump_report: 'crash' executable not found")
            return 1

        self.get_vmlinux()
        if not (os.path.exists(self.crash_cmds_file) and
                os.path.isfile(self.crash_cmds_file)):
            print("kdump_report: %s not found" % self.crash_cmds_file)
            return 1

        os.makedirs(self.kdump_report_out, exist_ok=True)

        args = (crash_path, self.vmlinux, self.vmcore, "-i",
                self.crash_cmds_file)
        print(f"kdump_report: Executing '{' '.join(args)}'; output file "
              f"'{self.kdump_report_out_file}'")

        with open(self.kdump_report_out_file, "w") as output_fd:
            subprocess.run(args, close_fds=True, stdout=output_fd,
                           stderr=output_fd, stdin=subprocess.DEVNULL,
                           shell=False, check=True)  # nosec
        return 0
    # def run_crash

    def clean_up(self) -> None:
        """Clean up old crash reports to save space"""
        max_files = int(self.max_out_files)
        crash_files = glob.glob(f"{self.kdump_report_out}/crash*out")

        if len(crash_files) > max_files:
            print(f"kdump_report: found more than {max_files}[max_out_files] "
                  "out files. Deleting older ones")

            for file in sorted(crash_files, reverse=True)[max_files:]:
                try:
                    os.remove(file)
                except OSError:
                    pass  # ignore permissions and missing file errors
    # def clean_up
# class KDUMP_REPORT


def main() -> int:
    """Main routine"""
    kdump_report = KdumpReport()

    if kdump_report.kdump_report != "yes":
        print("kdump_report: kdump_report is disabled to run")
        sys.exit(1)
    else:
        print("kdump_report: kdump_report is enabled to run")
        kdump_report.run_crash()
        kdump_report.clean_up()
        sys.exit(0)
# def main


if __name__ == '__main__':
    main()
