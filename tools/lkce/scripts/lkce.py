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
Program for user-interaction
"""

import errno
import fcntl
import glob
import os
import platform
import re
import shutil
import stat
import subprocess  # nosec
import sys
from typing import Mapping, Optional, List, Tuple, Union, Sequence


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
                del key_values[key]

                # If new_value is not None, update the value; otherwise remove
                # it (i.e. don't write key-new_value back to the file).
                if new_value is not None:
                    fdesc.write(f"{key}{sep}{new_value}\n")
            else:
                # line doesn't match lines to update; write it back as is
                fdesc.write(f"{line}\n")

        # write out any params that do not have existing entries.
        for key in key_values:
            new_value = key_values[key]
            if new_value:
                fdesc.write(f"\n{key}{sep}{new_value}\n")


def read_args_from_file(filename: str) -> Optional[List[str]]:
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


def get_dev_and_mount(path: str) -> Tuple[Union[str, None], Union[str, None]]:
    """ Return the device path and mountpoint for a file or directory"""
    try:
        st = os.stat(path)
        dev_id = st.st_dev

        with open("/proc/mounts", "r") as mounts:
            for line in mounts:
                tok = line.split()
                dev = tok[0]
                mp = tok[1]

                st = os.stat(mp)
                if st.st_dev == dev_id:
                    return (dev, mp)
        return (None, None)
    except FileNotFoundError:
        print(f"error: '{path}' not found.")
        return (None, None)


def get_dev_uuid(dev: str) -> Union[str, None]:
    """ Return the UUID for a device"""
    try:
        st = os.stat(dev)
        dev_id = st.st_rdev
    except OSError as e:
        print(f"error: Unable to stat {dev}: {e}")
        return None

    try:
        files = os.listdir("/dev/disk/by-uuid")
    except OSError as e:
        print(f"error: Unable to list /dev/disk/by-uuid: {e}")
        return None

    try:
        for f in files:
            path = f"/dev/disk/by-uuid/{f}"
            st = os.stat(path, follow_symlinks=True)
            if st.st_rdev == dev_id:
                return f
    except OSError as e:
        print(f"error: Unable to stat {f}: {e}")

    return None


def get_kdump_pre_line(filename: str) -> Union[str, None]:
    """Scan /etc/kdump.conf and return a configuration line
       if it starts with 'kdump_pre ' or else return None
       if no such line is found.
    """
    try:
        with open(filename) as conf_fd:
            conf_lines = conf_fd.read().splitlines()
            conf_fd.close()
        for line in conf_lines:
            if line.startswith("kdump_pre "):
                return line
    except OSError:
        pass

    return None


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
        self.report_cmd = "corelens"
        self.lkce_outdir = "/var/oled/lkce"
        self.kdump_dirty = False
        self.is_configured = False
        self.has_kdump_d = False
        self.kdump_d_dir = "/etc/kdump/pre.d"
        self.kdump_d_file = f"{self.kdump_d_dir}/10-lkce_kdump.sh"

        self.set_defaults()

        # set values from config file
        if os.path.exists(self.lkce_config_file):
            if self.read_config(self.lkce_config_file) == 0:
                self.is_configured = True

        # lkce as a kdump_pre hook to kexec-tools
        self.lkce_kdump_sh = self.lkce_home + "/lkce_kdump.sh"
        self.lkce_kdump_dir = self.lkce_home + "/lkce_kdump.d"
        self.kdump_conf = "/etc/kdump.conf"
        self.kdump_backup = f"{self.lkce_home}/kdump.conf.bak"
    # def __init__

    # default values
    def set_defaults(self) -> None:
        """set default values"""
        self.vmlinux_path = "/usr/lib/debug/lib/modules/" + \
            self.kdump_kernel_ver + "/vmlinux"
        self.crash_cmds_file = self.lkce_home + "/crash_cmds_file"
        self.corelens_args_file = self.lkce_home + "/corelens_args_file"
        self.corelens_default_args = ["-a"]
        self.vmcore = "yes"
        self.report_cmd = "corelens"
        self.max_out_files = "50"
        self.lkce_outdir = "/var/oled/lkce"

        try:
            self.has_kdump_d = os.path.exists(self.kdump_d_dir)
        except OSError:
            self.has_kdump_d = False
    # def set_default

    def need_kdump_conf(self) -> bool:
        """Returns True if we need to edit /etc/kdump.conf to
           enable/disable LKCE, otherwise returns False
        """
        # Disabled for now, as scripts in pre.d will not short-circuit
        # execution, which is needed for inhibiting the extraction of vmcore.
        # return self.has_kdump_d is not True
        return True

    def kdump_pre_d_link(self):
        """Enable LKCE in kdump mode by creating a symlink
           to our script in /etc/kdump/pre.d
           Returns 0 on success, 1 on failure, 2 if already enabled
        """
        try:
            os.symlink(self.lkce_kdump_sh, self.kdump_d_file)
            self.kdump_dirty = True
            print(f"info: Created link {self.kdump_d_file}")
            return 0
        except OSError as e:
            if e.errno == errno.EEXIST:
                print("LKCE is already enabled in kexec mode")
                return 2
            print(f"error: Unable symlink {self.lkce_kdump_sh}",
                  f"to {self.kdump_d_file}: {e}")
        return 1

    def kdump_pre_d_unlink(self):
        """Disable LKCE in kdump mode by removing the symlink
           to our script from /etc/kdump/pre.d

           Returns 0 on success, 1 on failure, 2 if already disabled

        """
        try:
            os.unlink(self.kdump_d_file)
            self.kdump_dirty = True
            print(f"info: Removed link {self.kdump_d_file}")
            return 0
        except OSError as e:
            if e.errno == errno.ENOENT:
                print("LKCE is not currently enabled in kexec mode")
                return 2
            print(f"error: Unable to remove link {self.kdump_d_file}: {e}")
        return 1

    def kexec_enabled(self):
        """Return True if LKCE is enabled in kdump mode, else return False"""
        if self.need_kdump_conf():
            # We determine whether LKCE is enabled by the presence, or
            # lack thereof, of a "kdump_pre" configuration line that contains
            # the path to our script.
            kdump_pre_value = get_kdump_pre_line(self.kdump_conf)
            if kdump_pre_value == f"kdump_pre {self.lkce_kdump_sh}":
                return True
            return False

        try:
            # We determine whether LKCE is enabled by the presence, or
            # thereof, of a symlink to our kdump_pre script in
            # /etc/kdump/pre.d/
            # If that symlink exists and the target is executable, return True
            st = os.stat(self.kdump_d_file)
            if st.st_mode & 0o111:
                return True
        except OSError:
            pass
        return False

    def configure_default(self) -> int:
        """Configure lkce with default values

        Creates self.corelens_args_file, self.crash_cmds_file,
        and self.lkce_config_file
        """
        try:
            os.makedirs(self.lkce_home, exist_ok=True)
        except OSError as e:
            print(f"error: Unable to create {self.lkce_home}: {e}")
            return 1

        # The default is to not enable LKCE in kdump
        self.disable_lkce_kexec()

        self.set_defaults()

        # Create lkce output directory
        try:
            os.makedirs(self.lkce_outdir, exist_ok=True)
        except OSError as e:
            print(f"error: Unable to create {self.lkce_outdir}: {e}")
            return 1

        # config file
        filename = self.lkce_config_file
        content = """##
# This is the configuration file for lkce
# Use the 'oled lkce configure' command to change values
##

#report command to use for lkce
report_cmd=""" + self.report_cmd + """

#debuginfo vmlinux path. Need to install debuginfo kernel to get it
vmlinux_path=""" + self.vmlinux_path + """

#path to file containing crash commands to execute
crash_cmds_file=""" + self.crash_cmds_file + """

#path to file containing corelens command line arguments
corelens_args_file=""" + self.corelens_args_file + """

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
            return 1

        self.is_configured = True
        print("LKCE has been configured with default values.")
        return 0
    # def configure_default

    def read_config(self, filename: str) -> int:
        """Read config file and update the class variables"""
        if not os.path.exists(filename):
            return 1

        try:
            file = open(filename, "r")
        except OSError:
            print("Unable to open file: {filename}")
            return 1

        for line in file.readlines():
            if re.search("^#", line):  # ignore lines starting with '#'
                continue

            # trim space/tab/newline from the line
            line = re.sub(r"\s+", "", line)

            entry = re.split("=", line)

            if "report_cmd" in entry[0] and entry[1]:
                self.report_cmd = entry[1]

            elif "corelens_args_file" in entry[0] and entry[1]:
                self.corelens_args_file = entry[1]

            elif "crash_cmds_file" in entry[0] and entry[1]:
                self.crash_cmds_file = entry[1]

            elif "lkce_outdir" in entry[0] and entry[1]:
                self.lkce_outdir = entry[1]

            elif "vmlinux_path" in entry[0] and entry[1]:
                self.vmlinux_path = entry[1]

            elif "vmcore" in entry[0] and entry[1]:
                self.vmcore = entry[1]

            elif "max_out_files" in entry[0] and entry[1]:
                self.max_out_files = entry[1]
        return 0
    # def read_config

    def create_lkce_kdump(self) -> int:
        """create lkce_kdump.sh script.

        lkce_kdump.sh is attached as kdump_pre hook in /etc/kdump.conf
        """

        (dev, mnt) = get_dev_and_mount("/")
        if not dev or not mnt:
            print(
                f"error: Unable to find the device for /")
            return 1

        uuid = get_dev_uuid(dev)
        if not uuid:
            print(f"error: Unable to find the UUID for {dev}")
            return 1

        dumpdir_env_set = f'''LKCE_SYSROOT_DEV="{dev}"
LKCE_SYSROOT_UUID="{uuid}"\n'''

        try:
            os.makedirs(self.lkce_kdump_dir, exist_ok=True)
        except OSError as e:
            print(f"error: Unable to create {self.lkce_kdump_dir}: {e}")
            return 1

        try:
            os.makedirs(self.lkce_outdir, exist_ok=True)
        except OSError as e:
            print(f"error: Unable to create {self.lkce_outdir}: {e}")
            return 1

        (dev, mnt) = get_dev_and_mount(self.lkce_outdir)
        if not dev or not mnt:
            print(
                f"error: Unable to find the mountpoint for {self.lkce_outdir}")
            return 1

        uuid = get_dev_uuid(dev)
        if not uuid:
            print(f"error: Unable to find the UUID for {dev}")
            return 1

        if mnt != "/":
            dumpdir_env_set += f'''LKCE_DUMP_DEV="{dev}"
LKCE_DUMP_DEV_UUID="{uuid}"
LKCE_DUMP_DEV_MNT="{mnt}"'''

        # create lkce_kdump.sh script
        content = '''#!/bin/sh
# This is a kdump_pre script
# This script is auto-generated. Changes made here will be overwritten.

# Generate vmcore post LKCE kdump scripts execution
LKCE_VMCORE="''' + self.vmcore + '''"
LKCE_KDUMP_SCRIPTS=''' + self.lkce_kdump_dir + '''/*
LKCE_OUTDIR="''' + self.lkce_outdir + '"\n' + dumpdir_env_set + "\n\n"

        filename = self.lkce_kdump_sh
        try:
            file = open(filename, "w")
            file.write(content)
            body = open(f"{self.lkce_home}/kdump_pre_sh_body")
            file.write(body.read())
            body.close()
            file.close()
        except OSError as e:
            print(f"Unable to operate on file: {filename}: {e}")
            return 1

        mode = os.stat(filename).st_mode
        try:
            os.chmod(filename,
                     mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        except OSError as e:
            print(f"error: Unable to make {filename} executable: {e}")
            return 1
        return 0
    # def create_lkce_kdump

    def backup_kdump_conf(self) -> int:
        """Back up the existing /etc/kdump.conf file
           to /etc/oled/lkce/kdump.conf.bak
        """
        try:
            shutil.copyfile(self.kdump_conf, self.kdump_backup)
        except OSError as e:
            print(f"error: Unable to copy {self.kdump_conf} to",
                  f"{self.kdump_backup}: {e}")
            return 1
        return 0
    # def backup_kdump_conf

    def restore_kdump_conf(self) -> int:
        """Restore the LKCE kdump.conf backup from
           /etc/oled/lkce/kdump.conf.bak to /etc/kdump.conf
        """
        try:
            shutil.copyfile(self.kdump_backup, self.kdump_conf)
        except OSError as e:
            print(f"error: Unable to copy {self.kdump_backup} to",
                  f"{self.kdump_conf}: {e}")
            return 1
        return 0
    # def restore_kdump_conf

    def restart_kdump_service_failsafe(self) -> int:
        """Restart the kdump service, and if it fails, try restoring
           the backed-up kdump.conf file, then try restarting with that.
        """
        if restart_kdump_service():
            print("error: Unable to restart the kdump service. Attempting",
                  "to restore /etc/kdump.conf from a backup and restart.")

            if self.restore_kdump_conf():
                print("error: Unable to restore the /etc/kdump.conf file",
                      "from backup.\n"
                      "Manual intervention is needed. You must correct the",
                      "issues with /etc/kdump.conf, then manually restart",
                      "the kdump service successfully or crashes may result",
                      "in system hangs.")
                return 1

            if restart_kdump_service():
                print("error: Unable to restart the kdump service",
                      "even after restoring /etc/kdump.conf\n"
                      "Manual intervention is needed. You must correct the",
                      "issues with /etc/kdump.conf, then manually restart",
                      "the kdump service successfully or crashes may result",
                      "in system hangs.")
                return 1
            return 2
        return 0
    # def restart_kdump_service_failsafe

    def update_kdump_conf(self, arg: str) -> int:
        """Add or remove LKCE configuration from /etc/kdump.conf"""
        if not os.path.exists(self.kdump_conf):
            print(f"error: {self.kdump_conf} does not exist.",
                  "Please retry after installing kexec-tools")
            return 1

        kdump_pre_line = f"kdump_pre {self.lkce_kdump_sh}"

        if self.backup_kdump_conf():
            print(f"warning: Unable to backup {self.kdump_conf}")

        if arg == "--remove":
            try:
                with open(self.kdump_conf, "r+") as conf_fd:
                    conf_lines = conf_fd.read().splitlines()

                    if kdump_pre_line not in conf_lines:
                        print("LKCE is not currently enabled",
                              f"in {self.kdump_conf}")
                        return 2
                    conf_lines.remove(kdump_pre_line)
                    conf_fd.seek(0)
                    conf_fd.write("\n".join(conf_lines))
                    conf_fd.write("\n")
                    conf_fd.truncate()
                    conf_fd.close()
            except OSError as e:
                print(f"error: Unable to update {self.kdump_conf}: {e}")
                return 1

            self.kdump_dirty = True
            return 0

        # arg == "--add"
        kdump_pre_value = get_kdump_pre_line(self.kdump_conf)
        if kdump_pre_value == kdump_pre_line:
            print("info: /etc/kdump.conf has already been modified to run",
                  "the LKCE kdump script.")
            return 2

        if kdump_pre_value:
            # kdump_pre is enabled, but it is not our lkce_kdump script
            print("The LKCE 'kdump_pre' entry is not set in",
                  f"{self.kdump_conf}.\n"
                  f"The current 'kdump_pre' entry in kdump.conf is:\n"
                  f"{kdump_pre_value}\n"
                  "Manual intervention is necessary.\n"
                  "Please see the oled-lkce manual page for",
                  "more information.")
            return 1

        try:
            # add the LKCE kdump_pre line to /etc/kdump.conf
            with open(self.kdump_conf, "a") as conf_fd:
                conf_fd.write(f"{kdump_pre_line}\n")
                conf_fd.close()
        except OSError as e:
            print(f"Error updating /etc/kdump.conf: {e}")
            return 1

        self.kdump_dirty = True
        return 0
    # def update_kdump_conf

    def report(self, subargs: List[str]) -> None:
        """Generate report from vmcore"""
        if not subargs:
            print("error: report option needs additional arguments "
                  "[oled lkce help]")
            return

        d_subargs = {}
        for subarg in subargs:
            subarg.strip()
            entry = subarg.split("=", 1)

            if len(entry) < 2:
                print(f"error: unknown report option: {subarg}")
                continue

            if entry[0] not in ("--vmcore", "--vmlinux", "--report_cmd",
                                "--crash_cmds", "--corelens_args_file",
                                "--outfile"):

                print(f"error: unknown report option: {entry[0]}")
                break

            d_subargs[entry[0].strip()] = entry[1].strip()
        # for

        vmcore = d_subargs.get("--vmcore", None)
        outfile = d_subargs.get("--outfile", None)

        if vmcore is None:
            print("error: vmcore not specified")
            return

        if self.report_cmd == "crash":
            vmlinux = d_subargs.get("--vmlinux", None)
            if vmlinux is None:
                vmlinux = self.vmlinux_path

            if not os.path.isfile(vmlinux):
                print(f"error: vmlinux '{vmlinux}' not found")
                return

            crash_cmds = d_subargs.get("--crash_cmds", None)

            if crash_cmds is None:
                # use configured crash commands file
                if not os.path.isfile(self.crash_cmds_file):
                    print(
                        f"crash_cmds_file '{self.crash_cmds_file}' not found")
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
        elif self.report_cmd == "corelens":
            corelens_args_file = d_subargs.get("--corelens_args_file", None)

            cmd: List[str] = ["corelens", vmcore]
            if corelens_args_file is None:
                # use configured corelens arguments file
                if not os.path.isfile(self.corelens_args_file):
                    print(f"corelens_args_file '{self.corelens_args_file}' "
                          "not found")
                else:
                    corelens_args_file = self.corelens_args_file

            if corelens_args_file is None:
                cmd.extend(self.corelens_default_args)
            else:
                # use specified corelens arguments file
                corelens_args = read_args_from_file(corelens_args_file)
                if corelens_args is None:
                    cmd.extend(self.corelens_default_args)
                else:
                    cmd.extend(corelens_args)

            print("lkce: executing '{}'".format(" ".join(cmd)))

            if outfile:
                with open(outfile, "w") as output_fd:
                    subprocess.run(
                        cmd, stdout=output_fd, check=True,
                        shell=False)  # nosec
            else:
                subprocess.run(
                    cmd, stdout=sys.stdout, check=True,
                    shell=False)  # nosec
        else:
            print(f"lkce: error: Unknown report command: {self.report_cmd}")
    # def report

    def configure(self, subargs: List[str]) -> None:
        """Configure lkce

        Based on subarg, you can configure with default values, show the values
        and also set to given values
        """
        if not subargs:
            print("error: No arguments or parameters given.")
            return

        values_to_update = {}
        filename = self.lkce_config_file
        for subarg in subargs:
            subarg.strip()
            if subarg == "--default":
                if self.configure_default():
                    print("error: LKCE default configuration failed.")
                    return
            elif subarg == "--show":
                if not os.path.exists(filename):
                    print(f"error: LKCE config file {filename} not found.\n"
                          "Please run 'oled lkce configure --default' first.")
                    return

                print("%18s : %s" % ("report_cmd", self.report_cmd))
                print("%18s : %s" %
                      ("corelens_args_file", self.corelens_args_file))
                print("%18s : %s" % ("vmcore", self.vmcore))
                print("%18s : %s" % ("vmlinux path", self.vmlinux_path))
                print("%18s : %s" % ("crash_cmds_file", self.crash_cmds_file))
                print("%18s : %s" % ("lkce_outdir", self.lkce_outdir))
                print("%18s : %s" % ("lkce_in_kexec", self.kexec_enabled()))
                print("%18s : %s" % ("max_out_files", self.max_out_files))
            else:
                entry = subarg.split("=", 1)

                if len(entry) < 2:
                    print(f"error: no value given for option {subarg}")
                    return

                if entry[0] not in ("--corelens_args_file",
                                    "--crash_cmds_file",
                                    "--vmlinux", "--report_cmd",
                                    "--lkce_outdir", "--vmcore",
                                    "--max_out_files"):
                    print(f"error: unknown configure option: {subarg}")
                    return

                values_to_update[entry[0].strip("-")] = entry[1].strip()

        if not self.is_configured:
            print(f"error: LKCE has not been configured.\n"
                  "Please run 'oled lkce configure --default' first.")
            return

        # for
        report_cmd = values_to_update.get("report_cmd", None)
        if report_cmd and self.config_report_cmd(report_cmd):
            return

        vmcore = values_to_update.get("vmcore", None)
        if vmcore and self.config_vmcore(vmcore):
            return

        lkce_outdir = values_to_update.get("lkce_outdir", None)
        if lkce_outdir and self.config_lkce_outdir(lkce_outdir):
            return

        if values_to_update:
            update_key_values_file(filename, values_to_update, sep="=")

        if self.kdump_dirty is True:
            if self.create_lkce_kdump():
                print(f"error: Unable to create kdump_pre script.",
                      "Not restarting kdump service. Please try again.")
                return
            if not self.restart_kdump_service_failsafe():
                self.kdump_dirty = False
                if self.need_kdump_conf():
                    self.backup_kdump_conf()
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
            print("error: Please enable lkce first using",
                  "'oled lkce enable_kexec'")
            return 1

        self.vmcore = value
        self.kdump_dirty = True
        return 0
    # def config_vmcore

    def config_report_cmd(self, value: str) -> int:
        """Configure the report command.

        Called from configure()
        """
        value = value.lower()
        if value != "corelens":
            print(f"error: Invalid report command specified: '{value}'",
                  "\nCurrently, only 'corelens' is supported.")
            return 1
        self.report_cmd = value
        return 0
    # def config_report_cmd

    def config_lkce_outdir(self, value: str) -> int:
        """Configure lkce_outdir value in lkce_config_file.

        Called from configure()
        """
        filename = self.lkce_kdump_sh
        if not os.path.exists(filename):
            print("error: Please enable lkce first using",
                  "'oled lkce enable_kexec'")
            return 1

        # Re-generate the pre-kexec hook script in case the location of the
        # output directory is not on the root filesystem and it needs to be
        # made available in kexec mode.
        self.lkce_outdir = value
        self.kdump_dirty = True
        return 0
    # def config_lkce_outdir

    def enable_lkce_kexec(self) -> int:
        """Enable LKCE vmcore reports in kexec mode"""

        # Generate kdump script in case configuration has changed since
        # the last enable
        if self.create_lkce_kdump():
            print("error: Unable to setup LKCE in kexec mode.")
            return 1

        ret = 0
        if self.need_kdump_conf():
            ret = self.update_kdump_conf("--add")
        else:
            ret = self.kdump_pre_d_link()

        if ret != 0:
            return ret

        if self.kdump_dirty is True:
            if self.restart_kdump_service_failsafe():
                return 1
            self.kdump_dirty = False
            if self.need_kdump_conf():
                self.backup_kdump_conf()

        print("enabled LKCE in kexec mode")
        return 0
    # def enable_lkce_kexec

    def disable_lkce_kexec(self) -> int:
        """Disable LKCE vmcore reports in kexec mode"""
        if not os.path.exists(self.lkce_config_file):
            print(f"config file '{self.lkce_config_file}' not found")
            return 1

        ret = 0
        if self.need_kdump_conf():
            ret = self.update_kdump_conf("--remove")
        else:
            ret = self.kdump_pre_d_unlink()

        if ret != 0:
            return ret

        if self.kdump_dirty is True:
            if self.restart_kdump_service_failsafe():
                return 1
            self.kdump_dirty = False
            if self.need_kdump_conf():
                self.backup_kdump_conf()

        print("disabled LKCE in kexec mode")
        return 0
    # def disable kexec

    def status(self) -> None:
        """Show current configuration values"""
        self.configure(subargs=["--show"])

        if self.report_cmd == "crash":
            pkg = "crash"
        else:
            pkg = "drgn-tools"

        r = subprocess.run(("rpm", "-q", pkg), shell=False,  # nosec
                           check=False)
        if r.returncode != 0:
            print(f"NOTE: The {pkg} package is not installed.")
    # def status

    def clean(self, subarg: List[str]) -> None:
        """Clean up old crash and corelens reports to save space"""
        report_files = []
        for p in ("crash*.out", "corelens*.out"):
            report_files.extend(glob.glob(f"{self.lkce_outdir}/{p}"))

        if "--all" in subarg:
            val = input("lkce will delete all the"
                        f"{self.lkce_outdir}/crash*.out and "
                        f"{self.lkce_outdir}/corelens*.out files.\n"
                        "Do you want to proceed? (yes/no) [no]: ")
            if "yes" not in val:
                return
        else:
            val = input("lkce will delete all but the last "
                        f"{self.max_out_files} {self.lkce_outdir}/crash*.out "
                        f"and {self.lkce_outdir}/corelens*.out files.\n"
                        "Do you want to proceed? (yes/no) [no]: ")
            if "yes" not in val:
                return

            # select all crash/corelens files but the N newest ones
            # where N == self.max_out_files
            report_files.sort(
                key=lambda x: os.path.getctime(x), reverse=True)
            report_files = report_files[int(self.max_out_files):]

        for f in report_files:
            try:
                os.remove(f)
            except OSError as e:
                print(f"error: Unable to remove {f}: {e}")
    # def clean

    def listfiles(self) -> None:
        """List corelens reports already generated"""
        dirname = self.lkce_outdir
        os.makedirs(dirname, exist_ok=True)

        print(f"The following are the reports found in {dirname}:")
        for filename in os.listdir(dirname):
            if re.search("(crash|corelens).*out", filename):
                print(f"{dirname}/{filename}")
    # def listfiles

# class LKCE


def restart_kdump_service() -> int:
    """Restart kdump service"""
    cmd = ("systemctl", "restart", "kdump")  # OL7 and above

    print("Restarting kdump service...")
    try:
        r = subprocess.run(cmd, shell=False, check=True)  # nosec
        if r.returncode == 0:
            print("done!")
        return r.returncode
    except subprocess.CalledProcessError:
        pass
    return 1
# def restart_kdump_service


def usage() -> int:
    """Print usage"""
    usage_ = """Usage: """ + os.path.basename(sys.argv[0]) + """ <options>
options:
    report <report-options> -- Generate a report from vmcore
    report-options:
        --vmcore=/path/to/vmcore 		- path to vmcore
        [--vmlinux=/path/to/vmlinux]            - path to vmlinux
        [--crash_cmds=cmd1,cmd2,cmd3,..]        - crash commands to run
        [--corelens_args_file=/path/to/file]	- path to corelens arguments file
        [--outfile=/path/to/outfile] 		- write output to a file

    configure [--default] 	-- configure lkce with default values
    configure [--show] 	-- show lkce configuration -- default
    configure [config-options]
    config-options:
        [--vmlinux_path=/path/to/vmlinux]       - set vmlinux_path
        [--crash_cmds_file=/path/to/file]       - set crash_cmds_file
        [--corelens_args_file=/path/to/file] 	- set corelens_args_file
        [--vmcore=yes/no]			- set vmcore generation in kdump kernel
        [--lkce_outdir=/path/to/directory] 	- set lkce output directory
        [--max_out_files=<number>] 		- set max_out_files

    enable_kexec    -- enable lkce in kdump kernel
    disable_kexec   -- disable lkce in kdump kernel
    status          -- status of lkce

    clean [--all]   -- clear crash/corelens report files
    list            -- list crash/corelens report files
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
        if not lkce.is_configured:
            print("warning: LKCE has not been configured.\n"
                  "Please run 'oled lkce configure --default'")
        lkce.report(sys.argv[2:])

    elif arg == "configure":
        lkce.configure(sys.argv[2:])

    elif arg == "enable_kexec":
        if lkce.is_configured:
            if lkce.enable_lkce_kexec() == 1:
                print("error: Unable to enable LKCE in kexec mode")
        else:
            print("error: LKCE has not been configured.\n"
                  "Please run 'oled lkce configure --default' first.")

    elif arg == "disable_kexec":
        if lkce.is_configured:
            if lkce.disable_lkce_kexec() == 1:
                print("error: Unable to disable LKCE in kexec mode")
        else:
            print("error: LKCE has not been configured.\n"
                  "Please run 'oled lkce configure --default' first.")

    elif arg == "status":
        lkce.status()

    elif arg == "clean":
        lkce.clean(sys.argv[2:])

    elif arg == "list":
        lkce.listfiles()

    elif arg in ("help", "-help", "--help", "-h"):
        usage()

    else:
        print(f"Invalid option: {arg}")
        print("Try 'oled lkce help' for more information")

    return 0
# def main


if __name__ == '__main__':
    if not os.geteuid() == 0:
        print("Please run LKCE as the root user.")
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
        print("error: another instance of lkce is running.")
        sys.exit()

    try:
        main()
        FH.close()
        os.remove(LOCK_FILE)
    except KeyboardInterrupt:
        print("\nInterrupted by user ctrl+c")
