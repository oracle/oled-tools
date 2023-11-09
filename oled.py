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
"""oled-tools driver"""

import argparse
import sys
import os
import platform
from typing import Sequence

# Oracle Linux Enhanced Diagnostic Tools
MAJOR = "0"
MINOR = "7"

BINDIR = "/usr/libexec/oled-tools"

# Valid oled subcomands
OLED_CMDS = (
    "dentrycache", "filecache", "kstack", "lkce", "memstate", "syswatch",
    "scanfs", "vmcore_sz")

# oled subcommands with a UEK4 variant
OLED_UEK4_CMDS = ("filecache", "dentrycache")


def parse_args(args: Sequence[str]) -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        usage="%(prog)s {-h | -v | COMMAND [ARGS]}",
        description="""\
Valid commands:
     lkce        -- Linux Kernel Core Extractor
     memstate    -- Capture and analyze memory usage statistics
     filecache   -- List the biggest files in page cache (only on x86-64)
     dentrycache -- List a sample of active dentries (only on x86-64)
     kstack      -- Gather kernel stack based on the process status or PID
     syswatch    -- Execute user-provided commands based on the CPU utilization
     scanfs      -- Scan KVM images for corruption, supports XFS and EXT4
     vmcore_sz   -- Estimating vmcore size before kernel dump
""",
        epilog="NOTE: Must run as root.")

    parser.add_argument(
        "-v", "--version", action="version",
        version=(
            f"Oracle Linux Enhanced Diagnostics (oled) v{MAJOR}.{MINOR} "
            "(developer preview release)"))
    parser.add_argument(
        "command", metavar="COMMAND", choices=OLED_CMDS,
        help=argparse.SUPPRESS)
    parser.add_argument(
        "args", metavar="ARGS", nargs=argparse.REMAINDER,
        help=argparse.SUPPRESS)

    options = parser.parse_args(args)
    cmd = options.command

    if not is_x86_64() and cmd in ("filecache", "dentrycache"):
        parser.error(
            f"The command 'oled {cmd}' is not supported on the current "
            "architecture.")

    return options


def is_uek4() -> bool:
    """Return true if kernel is UEK4."""
    return os.uname().release.startswith("4.1.12")


def is_x86_64() -> bool:
    """Return true if machine architecture is x86_64."""
    return platform.machine() == "x86_64"


def main(args: Sequence[str]) -> None:
    """Main function."""

    options = parse_args(args)

    # only allow root to execute subcommands
    if os.getuid() != 0:
        print("Run as root only", file=sys.stderr)
        sys.exit(1)

    cmd = options.command

    if is_uek4() and cmd in OLED_UEK4_CMDS:
        cmd = f"{cmd}_uek4"

    exec_path = os.path.join(BINDIR, cmd)
    prog_name = f"oled-{cmd}"
    exec_args = [prog_name] + options.args

    try:
        os.execv(exec_path, exec_args)  # nosec
    except Exception:
        print(f"Error executing: {exec_path} {exec_args}", file=sys.stderr)
        raise


if __name__ == "__main__":
    main(sys.argv[1:])
