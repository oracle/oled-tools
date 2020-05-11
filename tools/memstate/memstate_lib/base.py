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

from __future__ import print_function
import os
import subprocess
import logging
import fcntl
import sys
from datetime import datetime
from memstate_lib import constants

class Base:
    """ Contains general helper functions used by all other modules. """

    logged_messages = set()
    debug_log_configured = False

    def create_file_path(self, filename):
        end = filename.rfind("/")
        parent_dir = filename[0:end]
        if not os.path.exists(parent_dir):
            try:
                os.makedirs(parent_dir, mode=0o700)
            except IOError as e:
                self.log_debug("Could not create directory " + parent_dir + ": " + str(e))

    def exec_cmd(self, cmd):
        """
        Execute the command passed in as the parameter.
        @param cmd: The full command to be executed.
        """
        output = ""
        try:
            args = cmd.split()
            proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            (output, error) = proc.communicate()
            output = output.decode('utf-8')
            if error:
                self.log_debug("Command \'" + cmd + "\' returned error: " + error.decode('utf-8'))
        except Exception as e:
            output = ""
            self.log_debug("Command \'" + cmd + "\' failed with error: " + str(e))
        return output

    def exec_cmd_ignore_err(self, cmd):
        """
        Execute the command passed in as the parameter, ignore any errors.
        @param cmd: The full command to be executed.
        """
        output = ""
        try:
            args = cmd.split(" ")
            with open(os.devnull, 'w') as devnull:
                output = subprocess.Popen(args,
                        stdout=subprocess.PIPE, stderr=devnull).communicate()[0].decode('utf-8')
        except Exception as e:
            output = ""
            self.log_debug("Command \'" + cmd + "\' failed with error: " + str(e))
        return output

    @staticmethod
    def print_error(warn_str):
        print("[Error] " + warn_str)

    @staticmethod
    def print_warn(err_str):
        print("[Warning] " + err_str)

    @staticmethod
    def print_info(msg_str):
        print("[OK] " + msg_str)

    def create_memstate_debug_log(self):
        self.create_file_path(constants.MEMSTATE_DEBUG_LOG)
        logging.basicConfig(filename=constants.MEMSTATE_DEBUG_LOG,
                level=logging.DEBUG,
                format='%(asctime)s %(levelname)-8s %(message)s',
                datefmt='%Y-%m-%d %H:%M:%S')

    @staticmethod
    def log_debug(err_str):
        """
        This function logs errors/warnings/etc. raised by the tool,
        for tool debug and development.
        """
        if not Base.debug_log_configured:
            Base().create_memstate_debug_log()
            Base.debug_log_configured = True
        if err_str not in Base.logged_messages:
            Base.logged_messages.add(err_str)
            logging.debug("[memstate_debug] %s", err_str)

    def print_cmd_err(self, cmd_str):
        self.print_error("Unable to run command '" + cmd_str + "'.")

    @staticmethod
    def convert_mb_to_gb(val_in_mb):
        val_in_gb = float(val_in_mb)/constants.ONE_KB
        return round(val_in_gb, 1)

    @staticmethod
    def convert_kb_to_gb(val_in_kb):
        val_in_gb = float(val_in_kb)/(constants.ONE_KB**2)
        return round(val_in_gb, 1)

    @staticmethod
    def convert_bytes_to_gb(val_in_bytes):
        val_in_gb = float(val_in_bytes)/(constants.ONE_KB**3)
        return round(val_in_gb, 1)

    @staticmethod
    def convert_bytes_to_numpages(val_in_bytes):
        val_in_4k = float(val_in_bytes)/(constants.PAGE_SIZE_KB *constants.ONE_KB)
        return round(val_in_4k, 1)

    @staticmethod
    def print_pretty_gb(str_msg, int_arg):
        print("{0: <30}".format(str_msg) + ":" + "{0: >8}".format(int_arg) + " GB")

    @staticmethod
    def print_pretty_kb(str_msg, int_arg):
        print("{0: <30}".format(str_msg) + ":" + "{0: >12}".format(int_arg) + " KB")

    @staticmethod
    def print_header_l0(str_msg):
        print("==" * 8 + " " + str_msg + " " + "==" * 8)

    @staticmethod
    def print_header_l1(str_msg):
        print("=" * 4 + " " + str_msg + " " + "=" * 4)

    @staticmethod
    def print_header_l2(str_msg):
        print("=" * 2 + " " + str_msg + " " + "=" * 2)

    def get_kernel_ver(self):
        kernel_ver = self.exec_cmd("uname -r")
        if not kernel_ver:
            kernel_ver = "Unknown"
        return kernel_ver.strip()

    @staticmethod
    def get_current_time():
        current_time = datetime.now().strftime("<%m/%d/%Y %H:%M:%S>")
        if not current_time:
            current_time = "Unknown"
        return current_time.strip()

    def get_hostname(self):
        hname = self.exec_cmd("hostname")
        if not hname:
            hname = "Unknown"
        return str(hname.strip())

    @staticmethod
    def open_file(filename, mode):
        try:
            f = open(filename, mode)
        except IOError as e:
            # Ignore if file does not exist
            f = ""
        return f

    @staticmethod
    def order_x_in_kb(x):
        """
        Translate order-x page into its corresponding size, in KB.
        For instance: if x = 3, return 32 (KB).
        @param x: order
        """
        pages = 2**x
        return pages * constants.PAGE_SIZE_KB


class LockFile(Base):
    """ Implements a lock file """

    def __init__(self):
        self.lock_fd = None
        if os.path.exists(constants.LOCK_FILE_DIR):
            parent_dir = os.path.join(constants.LOCK_FILE_DIR, "memstate")
        else:
            parent_dir = os.path.join(constants.LOCK_FILE_DIR_OL6, "memstate")
        if not os.path.exists(parent_dir):
            try:
                os.makedirs(parent_dir, mode=0o700)
            except IOError as e:
                self.log_debug("Could not create directory " + parent_dir + ": " + str(e))
                return
        self.lock_filename = os.path.join(parent_dir, "lock")
        self.locked = False
        self.__create_lock()

    def __create_lock(self):
        """ Creates the directory and lock file """
        self.create_file_path(self.lock_filename)
        self.lock_fd = open(self.lock_filename, "w")

        # Exclusive lock | non-blocking request
        op = fcntl.LOCK_EX | fcntl.LOCK_NB
        try:
            fcntl.flock(self.lock_fd, op)
            self.locked = True
            self.log_debug("Locked file " + self.lock_filename)
        except IOError as e:
            self.log_debug("Failed to lock " + self.lock_filename + ": " + str(e))
            print("Another instance of this script is running; please kill that instance " \
                    "if you want to restart the script.")
            sys.exit(1)

    def __del__(self):
        if self.locked is True:
            self.log_debug("Cleaning up; deleting lock file " + self.lock_filename)
            fcntl.flock(self.lock_fd, fcntl.LOCK_UN)
            self.lock_fd.close()
            os.remove(self.lock_filename)
