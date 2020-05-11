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

#### Global constants
## Do not change these values!
PAGE_SIZE_KB = 4
FRAG_SIZE_KB = 16
STRUCT_PAGE_SIZE = 64
ONE_KB = 1024.00
ONE_MB = ONE_KB * ONE_KB
ONE_GB = ONE_KB * ONE_MB
PERCENT = 0.01
NO_LIMIT = -1
# The assumption here is that the system can be at most an 8-socket system.
MAX_NUMA_NODES = 8
# For parsing output from /proc/buddyinfo. Lower orders are 0-3, higher order buckets are 4-10.
BUDDYINFO_START_INDEX = 0
LOW_ORDER_MAX_INDEX = 3
HIGH_ORDER_MAX_INDEX = 10
#### End: global constants

#### Begin: config values
## These values control how many lines of output are printed for different
## categories. Change them if you want more/less output.
NUM_TOP_SLAB_CACHES = 10        # N biggest slab caches
NUM_TOP_MEM_USERS = 10          # N top memory consumers (user processes)
NUM_TOP_SWAP_USERS = 10         # N top users of swap space
NUM_TOP_NUMA_MAPS = 20          # N biggest consumers of memory on each NUMA node

## Thresholds for warnings
# For instance, print a warning if X slab cache usage exceeds
# SLAB_USE_PERCENT * (TotalRAM - TotalHugepages). These values are a bit arbitrary,
# and represent what we think are "safe" thresholds for most workloads.
# Change these to alter thresholds when warnings are printed.
SLAB_USE_PERCENT = 30 * PERCENT
RDS_USE_PERCENT = 10 * PERCENT
PAGETABLES_USE_PERCENT = 2 * PERCENT
UNACCOUNTED_THRESHOLD = 10 * PERCENT
WSF_THRESHOLD = 100

# Thresholds for NUMA imbalance checks
NUMASTAT_MIN_MEMFREE_PERCENT = 10 * PERCENT     # Min MemFree/MemTotal ratio across all NUMA nodes
NUMASTAT_DIFF_PERCENT = 200 * PERCENT           # max(MemFree[Nx])/min(MemFree[Ny]) ratio

# Buddyinfo fragmentation level
FRAG_LEVEL_LOW_ORDERS = 95      # Percent of free memory that's available in low (0-3) orders
ORDER_KB = [4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]  # Order-x in KB

# Output log file, lock file and default execution frequency
LOGROTATEFILE = "/etc/logrotate.d/memstate"
LOGFILE = "/var/oled/memstate/memstate.log"
MEMSTATE_DEBUG_LOG = "/var/oled/memstate/memstate_debug.log"
LOCK_FILE_DIR = "/run/lock/"
LOCK_FILE_DIR_OL6 = "/var/run/"
DEFAULT_INTERVAL = 30           # Unit: seconds
MAX_DISKSPACE_UTIL = 85         # This is the max allowed "Use%" field in 'df -h' output
                                # for the logfile path; the tool will exit if there's not
                                # enough space on disk.
MIN_DISKSPACE_NEEDED = 50       # Unit: MB
#### End: config values
