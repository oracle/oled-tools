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
Program for user-interaction
"""

import fcntl
import glob
import os
import platform
import re
import shutil
import stat
import subprocess  # nosec
import sys
from typing import Sequence, Mapping, Optional, List


def update_key_values_file(
        path: str,
        key_values: Mapping[str, Optional[str]],
        sep: str) -> None:
    """Update key-values in a file.

    For all (key, new_value) pairs in key_values, if value is not None, update
    the value of that key in the file to new_value; otherwise remove the
    key-value pair from the file.  The file is assumed to have a key-value per
    line, with sep as separator.  A line might contain only a key, even without
    a separator, in which case the value is assumed to be empty.
    """
    with open(path) as fdesc:
        data = fdesc.read().splitlines()

    with open(path, "w") as fdesc:
        for line in data:
            key, *_ = line.split("=", maxsplit=1)
            key = key.strip()

            if key in key_values:
                new_value = key_values[key]

                # If new_value is not None, update the value; otherwise remove
                # it (i.e. don't write key-new_value back to the file).
                if new_value is not None:
                    fdesc.write(f"{key}{sep}{new_value}\n")
            else:
                # line doesn't match lines to update; write it back as is
                fdesc.write(f"{line}\n")


class Lkce:
    """Class to include user interaction related functionality"""
    # pylint: disable=too-many-instance-attributes

    def __init__(self) -> None:
        """Constructor for Lkce class"""
        self.lkce_home = "/etc/oled/lkce"
        self.lkce_bindir = "/usr/lib/oled-tools"
        self.lkce_config_file = self.lkce_home + "/lkce.conf"
        self.kdump_kernel_ver = platform.uname().release
        self.vmcore = "yes"

        self.set_defaults()

        # set values from config file
        if os.path.exists(self.lkce_config_file):
            self.read_config(self.lkce_config_file)

        # lkce as a kdump_pre hook to kexec-tools
        self.lkce_kdump_sh = self.lkce_home + "/lkce_kdump.sh"
        self.lkce_kdump_dir = self.lkce_home + "/lkce_kdump.d"
        self.kdump_conf = "/etc/kdump.conf"

        tmp = shutil.which("timeout")
        if tmp is None:
            print("timeout command not found.")
            sys.exit(1)

        self.timeout_path = tmp
    # def __init__

    # default values
    def set_defaults(self) -> None:
        """set default values"""
        self.enable_kexec = "no"
        self.vmlinux_path = "/usr/lib/debug/lib/modules/" + \
            self.kdump_kernel_ver + "/vmlinux"
        self.crash_cmds_file = self.lkce_home + "/crash_cmds_file"
        self.vmcore = "yes"
        self.max_out_files = "50"
        self.lkce_outdir = "/var/oled/lkce"
    # def set_default

    def configure_default(self) -> None:
        """Configure lkce with default values

        Creates self.crash_cmds_file and self.lkce_config_file
        """
        os.makedirs(self.lkce_home, exist_ok=True)

        if self.enable_kexec == "yes":
            print("trying to disable lkce")
            self.disable_lkce_kexec()

        self.set_defaults()

        # crash_cmds_file
        filename = self.crash_cmds_file
        content = """#
# This is the input file for crash utility. You can edit this manually
# Add your own list of crash commands one per line.
#
bt
bt -a
bt -FF
dev
kmem -s
foreach bt
log
mod
mount
net
ps -m
ps -S
runq
quit
"""
        try:
            file = open(filename, "w")
            file.write(content)
            file.close()
        except OSError:
            print(f"Unable to operate on file: {filename}")
            return

        # config file
        filename = self.lkce_config_file
        content = """##
# This is configuration file for lkce
# Use 'oled lkce configure' command to change values
##

#enable lkce in kexec kernel
enable_kexec=""" + self.enable_kexec + """

#debuginfo vmlinux path. Need to install debuginfo kernel to get it
vmlinux_path=""" + self.vmlinux_path + """

#path to file containing crash commands to execute
crash_cmds_file=""" + self.crash_cmds_file + """

#lkce output directory path
lkce_outdir=""" + self.lkce_outdir + """

#enable vmcore generation post kdump_report
vmcore=""" + self.vmcore + """

#maximum number of outputfiles to retain. Older file gets deleted
max_out_files=""" + self.max_out_files

        try:
            file = open(filename, "w")
            file.write(content)
            file.close()
        except OSError:
            print("Unable to operate on file: {filename}")
            return

        print("configured with default values")
    # def configure_default

    def read_config(self, filename: str) -> None:
        """Read config file and update the class variables"""
        if not os.path.exists(filename):
            return

        try:
            file = open(filename, "r")
        except OSError:
            print("Unable to open file: {filename}")
            return

        for line in file.readlines():
            if re.search("^#", line):  # ignore lines starting with '#'
                continue

            # trim space/tab/newline from the line
            line = re.sub(r"\s+", "", line)

            entry = re.split("=", line)
            if "enable_kexec" in entry[0] and entry[1]:
                self.enable_kexec = entry[1]

            elif "vmlinux_path" in entry[0] and entry[1]:
                self.vmlinux_path = entry[1]

            elif "crash_cmds_file" in entry[0] and entry[1]:
                self.crash_cmds_file = entry[1]

            elif "lkce_outdir" in entry[0] and entry[1]:
                self.lkce_outdir = entry[1]

            elif "vmcore" in entry[0] and entry[1]:
                self.vmcore = entry[1]

            elif "max_out_files" in entry[0] and entry[1]:
                self.max_out_files = entry[1]
    # def read_config

    def create_lkce_kdump(self) -> None:
        """create lkce_kdump.sh script.

        lkce_kdump.sh is attached as kdump_pre hook in /etc/kdump.conf
        """
        filename = self.lkce_kdump_sh
        os.makedirs(self.lkce_kdump_dir, exist_ok=True)

        mount_cmd = "mount -o bind /sysroot"  # OL7 and above

        # create lkce_kdump.sh script
        content = """#!/bin/sh
# This is a kdump_pre script
# /etc/kdump.conf is used to configure kdump_pre script

# Generate vmcore post lkce_kdump scripts execution
LKCE_VMCORE=""" + self.vmcore + """

# Timeout for lkce_kdump scripts in seconds
LKCE_TIMEOUT="120"

# Temporary directory to mount the actual root partition
LKCE_DIR="/lkce_kdump"

mkdir $LKCE_DIR
""" + mount_cmd + """ $LKCE_DIR
mount -o bind /proc $LKCE_DIR/proc
mount -o bind /dev $LKCE_DIR/dev

LKCE_KDUMP_SCRIPTS=$LKCE_DIR""" + self.lkce_kdump_dir + """/*

#get back control after $LKCE_TIMEOUT to proceed
export LKCE_KDUMP_SCRIPTS
export LKCE_DIR
""" + self.timeout_path + """ $LKCE_TIMEOUT /bin/sh -c '
echo "LKCE_KDUMP_SCRIPTS=$LKCE_KDUMP_SCRIPTS";
for file in $LKCE_KDUMP_SCRIPTS;
do
    cmd=${file#$LKCE_DIR};
    echo "Executing $cmd";
    chroot $LKCE_DIR $cmd;
done;'

umount $LKCE_DIR/dev
umount $LKCE_DIR/proc
umount $LKCE_DIR

unset LKCE_KDUMP_SCRIPTS
unset LKCE_DIR

if [ "$LKCE_VMCORE" == "no" ]; then
    echo "lkce_kdump.sh: vmcore generation is disabled"
    exit 1
fi

exit 0
"""
        try:
            file = open(filename, "w")
            file.write(content)
            file.close()
        except OSError:
            print("Unable to operate on file: {filename}")
            return

        mode = os.stat(filename).st_mode
        os.chmod(filename, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    # def create_lkce_kdump

    def remove_lkce_kdump(self) -> int:
        """Remove lkce_kdump.sh as kdump_pre hook in /etc/kdump.conf"""
        return self.update_kdump_conf("--remove")
    # def remove_lkce_kdump()

    def update_kdump_conf(self, arg: str) -> int:
        """Add/remove /etc/kdump.conf with kdump_pre hook"""
        if not os.path.exists(self.kdump_conf):
            print(f"error: can not find {self.kdump_conf}. "
                  "Please retry after installing kexec-tools")
            return 1

        kdump_pre_line = f"kdump_pre {self.lkce_kdump_sh}"
        kdump_timeout_line = f"extra_bins {self.timeout_path}"

        with open(self.kdump_conf) as conf_fd:
            conf_lines = conf_fd.read().splitlines()

        kdump_pre_value = None
        for line in conf_lines:
            if line.startswith("kdump_pre "):
                kdump_pre_value = line

        if arg == "--remove":
            if kdump_pre_value != kdump_pre_line:
                print(f"lkce_kdump entry not set in {self.kdump_conf}")
                return 1

            # remove lkce_kdump config
            with open(self.kdump_conf, "w") as conf_fd:
                for line in conf_lines:
                    if line not in (kdump_pre_line, kdump_timeout_line):
                        conf_fd.write(f"{line}\n")

            restart_kdump_service()
            return 0

        # arg == "--add"
        if kdump_pre_value == kdump_pre_line:
            print("lkce_kdump is already enabled to run lkce scripts")
        elif kdump_pre_value:
            # kdump_pre is enabled, but it is not our lkce_kdump script
            print(f"lkce_kdump entry not set in {self.kdump_conf} "
                  "(manual setting needed)\n"
                  f"present entry in kdump.conf:\n{kdump_pre_value}\n"
                  "Hint: edit the present kdump_pre script and make it run"
                  f" {self.lkce_kdump_sh}")
            return 1
        else:
            # add lkce_kdump config
            with open(self.kdump_conf, "a") as conf_fd:
                conf_fd.write(
                    f"{kdump_pre_line}\n{kdump_timeout_line}\n")
            restart_kdump_service()

        return 0
    # def update_kdump_conf

    def report(self, subargs: List[str]) -> None:
        """Generate report from vmcore"""
        if not subargs:
            print("error: report option need additional arguments "
                  "[oled lkce help]")
            return

        d_subargs = {}
        for subarg in subargs:
            subarg.strip()
            entry = subarg.split("=", 1)

            if len(entry) < 2:
                print("error: unknown report option %s" % subarg)
                continue

            if entry[0] not in ("--vmcore", "--vmlinux",
                                "--crash_cmds", "--outfile"):

                print("error: unknown report option %s" % subarg)
                break

            d_subargs[entry[0].strip()] = entry[1].strip()
        # for

        vmcore = d_subargs.get("--vmcore", None)
        vmlinux = d_subargs.get("--vmlinux", None)
        crash_cmds = d_subargs.get("--crash_cmds", None)
        outfile = d_subargs.get("--outfile", None)

        if vmcore is None:
            print("error: vmcore not specified")
            return

        if vmlinux is None:
            vmlinux = self.vmlinux_path
        if not (os.path.exists(vmlinux) and os.path.isfile(vmlinux)):
            print("error: vmlinux '%s' not found" % vmlinux)
            return

        if crash_cmds is None:
            # use configured crash commands file
            if not (os.path.exists(self.crash_cmds_file) and
                    os.path.isfile(self.crash_cmds_file)):
                print(f"crash_cmds_file '{self.crash_cmds_file}' not found")
                return

            cmd: Sequence[str] = ("crash", vmcore, vmlinux, "-i",
                                  self.crash_cmds_file)
            cmd_input = None
        else:
            # use specified crash commands
            cmd = ("crash", vmcore, vmlinux)
            crash_cmds = crash_cmds + "," + "quit"
            cmd_input = "\n".join(crash_cmds.split(",")).encode("utf-8")

        print("lkce: executing '{}'".format(" ".join(cmd)))

        if outfile:
            with open(outfile, "w") as output_fd:
                subprocess.run(
                    cmd, input=cmd_input, stdout=output_fd, check=True,
                    shell=False)  # nosec
        else:
            subprocess.run(
                cmd, input=cmd_input, stdout=sys.stdout, check=True,
                shell=False)  # nosec
    # def report

    def configure(self, subargs: List[str]) -> None:
        """Configure lkce

        Based on subarg, you can configure with default values, show the values
        and also set to given values
        """
        if not subargs:  # default
            subargs = ["--show"]

        values_to_update = {}
        filename = self.lkce_config_file
        for subarg in subargs:
            subarg.strip()
            if subarg == "--default":
                self.configure_default()
            elif subarg == "--show":
                if not os.path.exists(filename):
                    print("config file not found")
                    return

                print("%15s : %s" % ("vmlinux path", self.vmlinux_path))
                print("%15s : %s" % ("crash_cmds_file", self.crash_cmds_file))
                print("%15s : %s" % ("vmcore", self.vmcore))
                print("%15s : %s" % ("lkce_outdir", self.lkce_outdir))
                print("%15s : %s" % ("lkce_in_kexec", self.enable_kexec))
                print("%15s : %s" % ("max_out_files", self.max_out_files))
            else:
                entry = subarg.split("=", 1)

                if len(entry) < 2:
                    print("error: unknown configure option %s" % subarg)
                    return

                if entry[0] not in ("--vmlinux_path", "--crash_cmds_file",
                                    "--lkce_outdir", "--vmcore",
                                    "--max_out_files"):
                    print("error: unknown configure option %s" % subarg)
                    return

                values_to_update[entry[0].strip("-")] = entry[1].strip()
        # for

        vmcore = values_to_update.get("vmcore", None)
        if values_to_update:
            if vmcore and self.config_vmcore(vmcore):
                return

            update_key_values_file(filename, values_to_update, sep="=")
    # def configure

    def config_vmcore(self, value: str) -> int:
        """Configure vmcore value in lkce_config_file.

        Called from configure()
        """
        if value not in ['yes', 'no']:
            print(f"error: invalid option '{value}'")
            return 1

        filename = self.lkce_kdump_sh
        if not os.path.exists(filename):
            print("error: Please enable lkce first, using 'oled lkce enable'")
            return 1

        self.vmcore = value
        self.create_lkce_kdump()
        restart_kdump_service()
        return 0
    # def config_vmcore

    def enable_lkce_kexec(self) -> None:
        """Enable lkce to generate report on crashed kernel in kexec mode"""
        if not os.path.exists(self.lkce_kdump_sh):
            self.create_lkce_kdump()

        if self.update_kdump_conf("--add") == 1:
            return

        update_key_values_file(
            self.lkce_config_file, {"enable_kexec": "yes"}, sep="=")
        print("enabled_kexec mode")
    # def enable_lkce_kexec

    def disable_lkce_kexec(self) -> None:
        """Disable lkce to generate report on crashed kernel in kexec mode"""
        if not os.path.exists(self.lkce_config_file):
            print(f"config file '{self.lkce_config_file}' not found")
            return

        if self.update_kdump_conf("--remove") == 1:
            return

        update_key_values_file(
            self.lkce_config_file, {"enable_kexec": "no"}, sep="=")

        try:
            os.remove(self.lkce_kdump_sh)
        except OSError:
            pass
        print("disabled kexec mode")
    # def disable kexec

    def status(self) -> None:
        """Show current configuration values"""
        self.configure(subargs=["--show"])

        if not os.path.exists(self.vmlinux_path):
            print(f"NOTE: {self.vmlinux_path}: file not found")
            print("kernel debuginfo rpm installed?")

        if subprocess.run(
                ("rpm", "-q", "crash"), shell=False,  # nosec
                check=False).returncode != 0:
            print("NOTE: crash is not installed")
    # def status

    def clean(self, subarg: List[str]) -> None:
        """Clean up old crash reports to save space"""
        if "--all" in subarg:
            val = input(f"lkce removes all the files in {self.lkce_outdir} "
                        "dir. do you want to proceed(yes/no)? [no]:")
            if "yes" in val:
                for file in glob.glob(f"{self.lkce_outdir}/crash*out"):
                    try:
                        os.remove(file)
                    except OSError:
                        pass
            # if "yes"
        else:
            val = input("lkce deletes all but last three "
                        f"{self.lkce_outdir}/crash*out files. "
                        "do you want to proceed(yes/no)? [no]:")
            if "yes" in val:
                crash_files = glob.glob(f"{self.lkce_outdir}/crash*out")

                # remove all crash files but the 3 newest ones
                for file in sorted(crash_files, reverse=True)[3:]:
                    try:
                        os.remove(file)
                    except OSError:
                        pass
    # def clean

    def listfiles(self) -> None:
        """List crash reports already generated"""
        dirname = self.lkce_outdir
        os.makedirs(dirname, exist_ok=True)

        print("Followings are the crash*out found in %s dir:" % dirname)
        for filename in os.listdir(dirname):
            if re.search("crash.*out", filename):
                print("%s/%s" % (dirname, filename))
        # for
    # def listfiles

# class LKCE


def restart_kdump_service() -> None:
    """Restart kdump service"""
    cmd = ("systemctl", "restart", "kdump")  # OL7 and above

    print("Restarting kdump service...")
    subprocess.run(cmd, shell=False, check=True)  # nosec
    print("done!")
# def restart_kdump_service


def usage() -> int:
    """Print usage"""
    usage_ = """Usage: """ + os.path.basename(sys.argv[0]) + """ <options>
options:
    report <report-options> -- Generate a report from vmcore
    report-options:
        --vmcore=/path/to/vmcore 		- path to vmcore
        [--vmlinux=/path/to/vmlinux] 		- path to vmlinux
        [--crash_cmds=cmd1,cmd2,cmd3,..]	- crash commands to include
        [--outfile=/path/to/outfile] 		- write output to a file

    configure [--default] 	-- configure lkce with default values
    configure [--show] 	-- show lkce configuration -- default
    configure [config-options]
    config-options:
        [--vmlinux_path=/path/to/vmlinux] 	- set vmlinux_path
        [--crash_cmds_file=/path/to/file] 	- set crash_cmds_file
        [--vmcore=yes/no]			- set vmcore generation in kdump kernel
        [--lkce_outdir=/path/to/directory] 	- set lkce output directory
        [--max_out_files=<number>] 		- set max_out_files

    enable_kexec    -- enable lkce in kdump kernel
    disable_kexec   -- disable lkce in kdump kernel
    status          -- status of lkce

    clean [--all]   -- clear crash report files
    list            -- list crash report files
"""
    print(usage_)
    sys.exit(0)
# def usage


def main() -> int:
    """Main routine"""
    lkce = Lkce()

    if len(sys.argv) < 2:
        usage()

    arg = sys.argv[1]
    if arg == "report":
        lkce.report(sys.argv[2:])

    elif arg == "configure":
        lkce.configure(sys.argv[2:])

    elif arg == "enable_kexec":
        lkce.enable_lkce_kexec()

    elif arg == "disable_kexec":
        lkce.disable_lkce_kexec()

    elif arg == "status":
        lkce.status()

    elif arg == "clean":
        lkce.clean(sys.argv[2:])

    elif arg == "list":
        lkce.listfiles()

    elif arg in ("help", "-help", "--help"):
        usage()

    else:
        print(f"Invalid option: {arg}")
        print("Try 'oled lkce help' for more information")

    return 0
# def main


if __name__ == '__main__':
    if not os.geteuid() == 0:
        print("Please run lkce as root user.")
        sys.exit(1)

    LOCK_DIR = "/var/run/oled-tools"
    try:
        if not os.path.isdir(LOCK_DIR):
            os.makedirs(LOCK_DIR)
        LOCK_FILE = LOCK_DIR + "/lkce.lock"

        FH = open(LOCK_FILE, "w")
    except OSError:
        print(f"Unable to open file: {LOCK_FILE}")
        sys.exit()

    # try lock
    try:
        fcntl.flock(FH, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:  # no lock
        print("another instance of lkce is running.")
        sys.exit()

    try:
        main()
        FH.close()
        os.remove(LOCK_FILE)
    except KeyboardInterrupt:
        print("\nInterrupted by user ctrl+c")
