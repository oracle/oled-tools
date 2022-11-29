#!/usr/bin/python
#
# Copyright (c) 2020, Oracle and/or its affiliates.
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

import sys
import os
import platform

# Oracle Linux Enhanced Diagnostic Tools
MAJOR = "0"
MINOR = "6"

BINDIR="/usr/lib/oled-tools"

# cmds
LKCE = BINDIR + "/lkce"
MEMSTATE = BINDIR + "/memstate"
KSTACK = BINDIR + "/kstack"
SYSWATCH = BINDIR + "/syswatch"
# for uek4
FILECACHE_UEK4 = BINDIR + "/filecache_uek4"
DENTRYCACHE_UEK4 = BINDIR + "/dentrycache_uek4"
# for uek5 and higher
FILECACHE = BINDIR + "/filecache"
DENTRYCACHE = BINDIR + "/dentrycache"

def dist():
    os_release = platform.linux_distribution()[1]
    if os_release.startswith("3"):
         if (os_release.replace('.','') <= 346):
             dist = "el6"
         else:
             dist = "el7"
    else:
        os_release = float(platform.linux_distribution()[1])
        if os_release > 6.0 and os_release < 7.0 :
            dist = "el6"
        elif os_release > 7.0 and os_release < 8.0 :
            dist = "el7"
        else :
            dist = "el8"
    return dist


def help(error):
    print("Oracle Linux Enhanced Diagnostic Tools")
    print("Usage:")
    print("  %s <command> <subcommand>" % sys.argv[0])
    print("Valid commands:")
    print("     lkce            -- Linux Kernel Core Extractor")
    print("     memstate        -- Capture and analyze memory usage statistics")
    print("     filecache       -- List the biggest files in page cache")
    print("     dentrycache     -- List a sample of active dentries")
    print("     kstack          -- Gather kernel stack based on the process status or PID")
    print("     syswatch        -- Execute user-provided commands based on the CPU utilization")
    print("     help            -- Show this help message")
    print("     version         -- Print version information")

    if (error):
        sys.exit(1)
    sys.exit(0)

def is_uek4():
    kernel_ver = os.uname()[2]
    if kernel_ver.find("4.1.12") == 0:
        return True
    return False

def run_as_root():
    if (os.geteuid() != 0):
        print("Run as root only")
        sys.exit(1)

def cmd_version():
	version = "%s.%s"%(MAJOR, MINOR)
	print("Oracle Linux Enhanced Diagnostics (oled): version " + version + ".")
	print("Note that this is a developer preview release.")

def cmd_memstate(args):
    cmdline = MEMSTATE
    for arg in args:
        cmdline = cmdline + " %s" % arg
    ret = os.system(cmdline)
    return ret

def cmd_lkce(args):
    cmdline = LKCE
    for arg in args:
        cmdline = cmdline + " %s" % arg
    ret = os.system(cmdline)
    return ret

def cmd_filecache(args):
    if is_uek4():
        cmdline = FILECACHE_UEK4
    else:
        cmdline = FILECACHE

    cmdline = cmdline + " " + " ".join(args)
    ret = os.system(cmdline)
    return ret

def cmd_dentrycache(args):
    if is_uek4():
        cmdline = DENTRYCACHE_UEK4
    else:
        cmdline = DENTRYCACHE

    cmdline = cmdline + " " + " ".join(args)
    ret = os.system(cmdline)
    return ret

def cmd_filecache(args):
    if is_uek4():
        cmdline = FILECACHE_UEK4
    else:
        cmdline = FILECACHE

    cmdline = cmdline + " " + " ".join(args)
    ret = os.system(cmdline)
    return ret

def cmd_kstack(args):
    cmdline = KSTACK
    cmdline = cmdline + " " + " ".join(args)
    ret = os.system(cmdline)
    return ret

def cmd_syswatch(args):
    cmdline = SYSWATCH + " " + " ".join(args)
    return os.system(cmdline)

def cmd_help(args):
    help(False)

def main():
    run_as_root()

    if len(sys.argv) < 2:
        help(True)

    cmd = sys.argv[1]
    args = sys.argv[2:]

    if cmd == "memstate":
        ret = cmd_memstate(args)
        sys.exit(ret)
    elif cmd == "lkce":
        ret = cmd_lkce(args)
        sys.exit(ret)
    elif cmd == "filecache":
        ret = cmd_filecache(args)
        sys.exit(ret)
    elif cmd == "dentrycache":
        ret = cmd_dentrycache(args)
        sys.exit(ret)
    elif cmd == "kstack":
        ret = cmd_kstack(args)
        sys.exit(ret)
    elif cmd == "syswatch":
        ret = cmd_syswatch(args)
        sys.exit(ret)
    elif cmd == "version" or cmd == "--version":
        ret = cmd_version()
        sys.exit(ret)
    elif cmd == "help":
        ret = cmd_help(args)
        sys.exit(ret)
    else:
        print("Invalid command: \"" + cmd + "\"")
        sys.exit(1)

    help(True)

if __name__ == "__main__":
    main()
