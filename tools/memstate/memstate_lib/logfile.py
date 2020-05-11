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

import os
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
                msg = "Exiting! Unable to verify that there is enough disk space available in " \
                        + str(parent_dir) + "; check the man page for more details."
                self.log_debug(msg)
                print(msg)
                sys.exit(1)

            self.log = open(constants.LOGFILE, "a")
            msg = "Capturing memstate data every " + str(interval) + " seconds, in log: " \
                    + constants.LOGFILE
            print(msg + "; press Ctrl-C to stop.")
            self.log_debug(msg + ".")
            Logfile.init_done = True

    def __disk_space_available(self, path):
        out = self.exec_cmd("df -Ph " + path)
        line = ""
        for line in out.splitlines():
            if "Use" in line:
                pos_use = line.split().index("Use%")
                pos_avail = line.split().index("Avail")
                continue
        if not line:
            msg = "Unable to compute disk utilization for " + path + "."
            self.log_debug(msg)
            print(msg)
            return False
        util = line.split()[pos_use][:-1]
        avail = line.split()[pos_avail][:-1]
        avail_unit = line.split()[pos_avail][-1]
        avail_space_mb = 0
        if avail_unit == "T":
            avail_space_mb = round(float(avail) * constants.ONE_MB)
        elif avail_unit == "G":
            avail_space_mb = round(float(avail) * constants.ONE_KB)
        elif avail_unit == "M":
            avail_space_mb = round(float(avail))
        elif avail_unit == "K":
            avail_space_mb = round(float(avail) / constants.ONE_KB)

        self.log_debug("Disk utilization of the partition for " + path + " is " + util + "%.")
        self.log_debug("Available space on disk is " + str(avail_space_mb) + " MB.")

        # If disk space utilization is >= 85% OR if available space is less than 50 MB,
        # then error out. We do not want to fill up the filesystem with memstate logs.
        if int(util) >= constants.MAX_DISKSPACE_UTIL or \
                avail_space_mb < constants.MIN_DISKSPACE_NEEDED:
            return False
        return True

    @staticmethod
    def setup_logrotate():
        f = open(constants.LOGROTATEFILE, "w")
        f.write(constants.LOGFILE + " {\n")
        f.write("\trotate 20\n")
        f.write("\tsize 20M\n")
        f.write("\tcopytruncate\n")
        f.write("\tcompress\n")
        f.write("\tmissingok\n")
        f.write("}\n")
        f.close()

    def write(self, message):
        self.log.write(message)

    @staticmethod
    def flush():
        pass

    @staticmethod
    def rotate_file():
        os.system("logrotate " + constants.LOGROTATEFILE)

    def __del__(self):
        if Logfile.init_done:
            self.log.close()
        if os.path.exists(constants.LOGROTATEFILE):
            os.remove(constants.LOGROTATEFILE)
