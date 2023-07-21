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
"""Logging module."""

import math
import os
import shutil
import subprocess  # nosec
import sys
from memstate_lib import Base
from memstate_lib import constants


class Logfile(Base):
    """
    Captures output in desired log file and sets up logrotate config
    in order to cap disk space consumed by the log file.
    """

    init_done = False

    def __init__(self, interval):
        if not Logfile.init_done:
            end = constants.LOGFILE.rfind("/")
            parent_dir = constants.LOGFILE[0:end]
            self.setup_logrotate()
            if not os.path.exists(parent_dir):
                os.makedirs(parent_dir)

            if not self.__disk_space_available(parent_dir):
                msg = (
                    "Exiting! Unable to verify that there is enough disk space"
                    f"available in {parent_dir}; check the man page for more "
                    "details.")
                self.log_debug(msg)
                print(msg)
                sys.exit(1)

            self.log = open(constants.LOGFILE, "a")
            msg = (
                f"Capturing memstate data every {interval} seconds, in log: "
                f"{constants.LOGFILE}")
            print(msg + "; press Ctrl-C to stop.")
            self.log_debug(msg + ".")
            Logfile.init_done = True

    @staticmethod
    def __disk_space_available(path):
        disk = shutil.disk_usage(path)
        free_mb = math.ceil(disk.free / 2**20)
        util_percent = math.ceil(disk.used / disk.total * 100)

        return ((util_percent < constants.MAX_DISKSPACE_UTIL)
                and (free_mb >= constants.MIN_DISKSPACE_NEEDED))

    @staticmethod
    def setup_logrotate():
        """Setup logrotate(8) configuration."""
        with open(constants.LOGROTATEFILE, "w") as conf_fd:
            conf_fd.write(
                f"{constants.LOGFILE} {{\n"
                "\trotate 20\n"
                "\tsize 20M\n"
                "\tcopytruncate\n"
                "\tcompress\n"
                "\tmissingok\n"
                "}\n")

    def write(self, message):
        """Write message to log file."""
        self.log.write(message)

    @staticmethod
    def flush():
        """Flush log file."""

    @staticmethod
    def rotate_file():
        """Rotate log file."""
        subprocess.run(
            ("logrotate", constants.LOGROTATEFILE),
            check=False, shell=False)  # nosec

    def __del__(self):
        if Logfile.init_done:
            self.log.close()
        if os.path.exists(constants.LOGROTATEFILE):
            os.remove(constants.LOGROTATEFILE)
