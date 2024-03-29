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
"""Common code for memstate modules."""

from __future__ import print_function
import os
import subprocess  # nosec
import logging
import fcntl
import platform
import socket
import sys
from datetime import datetime
from typing import Set, Optional

from memstate_lib import constants


class Base:
    """ Contains general helper functions used by all other modules. """
    # pylint: disable=too-many-public-methods

    logged_messages: Set[str] = set()
    debug_log_configured = False

    def create_file_path(self, filename):
        """Create the base dir of a file path if missing."""
        end = filename.rfind("/")
        parent_dir = filename[0:end]
        if not os.path.exists(parent_dir):
            try:
                os.makedirs(parent_dir, mode=0o700)
            except OSError as exn:
                self.log_debug(
                    f"Could not create directory {parent_dir}: {exn}")

    def read_text_file(self, path: str, on_error: Optional[str] = None) -> str:
        """Read content of a file as text.

        If an error occurred, if on_error is not None, return that value;
        otherwise raise an exception.
        """
        try:
            with open(path, "r", encoding="utf8") as fdesc:
                return fdesc.read()
        except Exception as exn:
            self.log_debug(f"Unable to read file '{path}': {exn}")

            if on_error is not None:
                return on_error

            raise

    def exec_cmd(self, cmd):
        """
        Execute the command passed in as the parameter.
        @param cmd: The full command to be executed.
        """
        output = ""
        try:
            args = cmd.split()
            result = subprocess.run(
                        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        shell=False, check=True)  # nosec
            output = result.stdout.decode('utf-8')
        except Exception as exn:  # pylint: disable=broad-except
            output = ""
            self.log_debug(f"Command '{cmd}' failed with error: {exn}")
        return output

    @staticmethod
    def get_page_size():
        """Get the default page size on the current system."""
        try:
            return os.sysconf("SC_PAGE_SIZE")
        except ValueError:
            print("Unable to read the PAGE_SIZE of this system; exiting.\n")
            sys.exit(1)

    @staticmethod
    def print_error(warn_str):
        """Print an error message."""
        print(f"{'[ERR] ': <8}{warn_str}")

    @staticmethod
    def print_warn(err_str):
        """Print a warning message."""
        print(f"{'[WARN] ': <8}{err_str}")

    @staticmethod
    def print_info(msg_str):
        """Print an info message."""
        print(f"{'[OK] ': <8}{msg_str}")

    def create_memstate_debug_log(self):
        """Create memstate debug log file."""
        self.create_file_path(constants.MEMSTATE_DEBUG_LOG)
        logging.basicConfig(
            filename=constants.MEMSTATE_DEBUG_LOG, level=logging.DEBUG,
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
        """Print an error informing failure when running a command."""
        self.print_error("Unable to run command '" + cmd_str + "'.")

    @staticmethod
    def convert_mb_to_gb(val_in_mb):
        """Return MBs to GBs."""
        val_in_gb = float(val_in_mb)/constants.ONE_KB
        return round(val_in_gb, 1)

    @staticmethod
    def convert_kb_to_gb(val_in_kb):
        """Return KBs to GBs."""
        val_in_gb = float(val_in_kb)/(constants.ONE_KB**2)
        return round(val_in_gb, 1)

    @staticmethod
    def convert_bytes_to_gb(val_in_bytes):
        """Return number of bytes to GBs."""
        val_in_gb = float(val_in_bytes)/(constants.ONE_KB**3)
        return round(val_in_gb, 1)

    @staticmethod
    def convert_bytes_to_numpages(val_in_bytes):
        """Return number of bytes to number pages."""
        val_in_4k = \
            float(val_in_bytes) / (Base.get_page_size() * constants.ONE_KB)
        return round(val_in_4k, 1)

    @staticmethod
    def print_pretty_gb(str_msg, int_arg):
        """Print GB value in a pretty format."""
        print(f"{str_msg: <30}{int_arg: >12}")

    @staticmethod
    def print_pretty_kb(str_msg, int_arg):
        """Print KB value in a pretty format."""
        print(f"{str_msg: <30}{int_arg: >12}")

    @staticmethod
    def get_kernel_ver():
        """Get kernel version."""
        return platform.uname().release

    @staticmethod
    def get_current_time():
        """Get current time formatted as 'm/d/Y H:M:S'.

        If current time could not be determined, return "Unknown".
        """
        current_time = datetime.now().strftime("%m/%d/%Y %H:%M:%S")
        if not current_time:
            current_time = "Unknown"
        return current_time.strip()

    @staticmethod
    def get_hostname():
        """Get hostname.

        Return "Unknown" if hostname could not be retrieved.
        """
        hname = socket.gethostname()
        if not hname:
            hname = "Unknown"
        return hname

    @staticmethod
    def open_file(filename, mode):
        """Open file with the given mode.

        If an OSError occurs, return an empty string instead."""
        try:
            return open(filename, mode, encoding="utf8")
        except OSError:
            # Ignore if file does not exist
            return ""

    @staticmethod
    def order_x_in_kb(order):
        """
        Translate order-x page into its corresponding size, in KB.
        For instance: if order = 3, return 32 (KB).
        """
        pages = 2**order
        return pages * Base.get_page_size() / constants.ONE_KB


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
            except OSError as exn:
                self.log_debug(
                    f"Could not create directory {parent_dir}: {exn}")
                return
        self.lock_filename = os.path.join(parent_dir, "lock")
        self.locked = False
        self.__create_lock()

    def __create_lock(self):
        """ Creates the directory and lock file """
        self.create_file_path(self.lock_filename)

        try:
            # pylint: disable-next=consider-using-with
            self.lock_fd = open(self.lock_filename, "w", encoding="utf8")
            # Exclusive lock | non-blocking request
            fcntl.flock(self.lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.locked = True
            self.log_debug("Locked file " + self.lock_filename)
        except IOError as exn:
            self.log_debug(f"Failed to lock {self.lock_filename}: {exn}")
            print(
                "Another instance of this script is running; please kill that "
                "instance if you want to restart the script.")
            sys.exit(1)

    def __del__(self):
        if self.locked is True:
            try:
                self.log_debug(
                    f"Cleaning up; deleting lock file {self.lock_filename}")
                fcntl.flock(self.lock_fd, fcntl.LOCK_UN)
                self.lock_fd.close()
                os.remove(self.lock_filename)
            except OSError as exp:
                self.log_debug(
                    "Caught exception while cleaning up/deleting lock file: "
                    f"{exp}")
