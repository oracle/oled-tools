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
from typing import Sequence

# Oracle Linux Enhanced Diagnostic Tools
MAJOR = "1"
MINOR = "0"
MICRO = "2"

BINDIR = "/usr/libexec/oled-tools"

# Valid oled subcomands
OLED_CMDS = (
    "kstack", "lkce", "oomwatch", "memstate",
    "syswatch", "scanfs", "vmcore_sz", "scripts")


def parse_args(args: Sequence[str]) -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        usage="%(prog)s {-h | -v | COMMAND [ARGS]}",
        description="""\
Valid commands:
     kstack      -- Gather kernel stack based on the process status or PID
     lkce        -- Linux Kernel Core Extractor
     memstate    -- Capture and analyze memory usage statistics
     oomwatch    -- Kill processes exceeding memory thresholds
     scanfs      -- Scan KVM images for corruption, supports XFS and EXT4
     scripts     -- Run additional oled-tools scripts
     syswatch    -- Execute user-provided commands based on the CPU utilization
     vmcore_sz   -- Estimating vmcore size before kernel dump
""",
        epilog="NOTE: Must run as root.")

    parser.add_argument(
        "-v", "--version", action="version",
        version=("Oracle Linux Enhanced Diagnostics (oled) "
                 f"v{MAJOR}.{MINOR}.{MICRO}"))
    parser.add_argument(
        "command", metavar="COMMAND", choices=OLED_CMDS,
        help=argparse.SUPPRESS)
    parser.add_argument(
        "args", metavar="ARGS", nargs=argparse.REMAINDER,
        help=argparse.SUPPRESS)

    return parser.parse_args(args)


def main(args: Sequence[str]) -> None:
    """Main function."""

    options = parse_args(args)

    # only allow root to execute subcommands
    if os.getuid() != 0:
        print("Run as root only", file=sys.stderr)
        sys.exit(1)

    cmd = options.command

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
