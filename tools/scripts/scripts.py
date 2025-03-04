#!/usr/bin/env python3
#
# Copyright (c) 2024, Oracle and/or its affiliates.
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
#
# Author: Jose Lombera <jose.lombera@oracle.com>

"""
Tool to execute and manipulate oled-tools scripts.
"""

import argparse
import datetime
import glob
import logging
import logging.handlers
import os
import re
import signal
import subprocess  # nosec
import sys
import platform

from typing import List, Mapping, Optional, Sequence

SCRIPTS_DIR = "/usr/libexec/oled-tools/scripts.d"
STARTUP_CONFIG_FILE = "/usr/libexec/oled-tools/scripts.d/startup-scripts.conf"
USER_STARTUP_CONFIG_FILE = "/etc/oled/startup-scripts.conf"
STARTUP_SCRIPTS_OUT_DIR = "/var/oled/startup-scripts"


def get_available_scripts() -> Mapping[str, str]:
    """Get available oled-tools scripts.

    Returns a dictionary {script_name: script_path}.
    """
    return {
        os.path.basename(path): path
        for path in glob.iglob(os.path.join(SCRIPTS_DIR, "*"))
        if os.path.isfile(path) and path != STARTUP_CONFIG_FILE
    }


def get_startup_script_names(config_path: str) -> Mapping[str, bool]:
    """Return startup scripts and whether they are enabled by default."""
    if not os.path.isfile(config_path):
        return {}  # no startup config file, assume no startup scripts

    startup_scripts = {}

    with open(config_path) as fdesc:
        for line in fdesc:
            line = line.strip()

            if not line:
                continue

            # lines expected in format
            # '<script_name>': for not enabled startup script
            # '+ <script_name>': for enabled startup script
            if line.startswith("+ "):
                startup_scripts[line[2:].strip()] = True  # startup enabled
            else:
                startup_scripts[line] = False

    return startup_scripts


def get_user_startup_cofig(config_path: str) -> Mapping[str, bool]:
    """Return user startup script enabled/disabled config.

    Read script names from config_path, one per line.  Lines with format
    '*<script_name>' mark <script_name> as enabled; '!<script_name>' mark
    <script_name> as disabled.  Lines not conforming to these two formats are
    ignored.
    """
    if not os.path.isfile(config_path):
        return {}

    startup_scripts = {}

    with open(config_path) as fdesc:
        for line_num, line in enumerate(fdesc, 1):
            line = line.strip()

            if not line:
                continue

            # lines expected in format:
            # '+ <script_name>': for startup enabled scripts
            # '- <script_name>': for startup disabled scripts
            if line.startswith("+ "):
                startup_scripts[line[2:].strip()] = True
            elif line.startswith("- "):
                startup_scripts[line[2:].strip()] = False
            else:
                logging.warning(
                    "%s:%d: '%s': invalid config; lines must start with "
                    "'+ ' or '- '", config_path, line_num, line)

    return startup_scripts


def version_tuple(version):
    """
    Converts a kernel version string into a tuple of integers.
    """
    return tuple(int(x) for x in re.split(r'[.-]', version) if x.isnumeric())


def is_kernel_version_compatible(current_kernel_version, min_version, max_version):
    """
    Checks if the current kernel version is compatible with the specified
    minimum and maximum kernel versions.
    """
    if not min_version and not max_version:
        return False
    current_version = '.'.join(current_kernel_version.split('.')[:-2])
    current_version_tuple = version_tuple(current_version)
    min_version_tuple = version_tuple(min_version) if min_version else None
    max_version_tuple = version_tuple(max_version) if max_version else None

    if min_version_tuple and current_version_tuple < min_version_tuple:
        return False
    if max_version_tuple and current_version_tuple > max_version_tuple:
        return False

    return True


def get_compat_kernel_versions(file_path: str) -> tuple:
    """
    Retrieves the minimum and maximum compatible kernel versions
    from the specified file.
    """
    min_kernel_version = None
    max_kernel_version = None
    curr_ver = platform.uname().release.split('-')[0]

    if not os.path.isfile(file_path):
        return min_kernel_version, max_kernel_version

    try:
        with open(file_path, 'r') as file:
            for line in file:
                line = line.strip()
                min_match = re.match(r'\s*\*\s*min_kernel\s+(.*)', line)
                max_match = re.match(r'\s*\*\s*max_kernel\s+(.*)', line)
                if min_match:
                    min_versions = re.sub(r'\s+', '', min_match.group(1).strip())
                    for version in min_versions.split(','):
                        if version.startswith(curr_ver):
                            min_kernel_version = version
                elif max_match:
                    max_versions = re.sub(r'\s+', '', max_match.group(1).strip())
                    for version in max_versions.split(','):
                        if version.startswith(curr_ver):
                            max_kernel_version = version

    except (FileNotFoundError, IOError, OSError) as exception:
        print(f"Error reading file {file_path}: {exception}")

    return min_kernel_version, max_kernel_version


def list_scripts() -> None:
    """List available scripts."""
    current_kernel_version = platform.uname().release
    scripts = get_available_scripts()
    startup_config = get_startup_script_names(STARTUP_CONFIG_FILE)
    user_config = get_user_startup_cofig(USER_STARTUP_CONFIG_FILE)

    if scripts:
        print(
            "(Startup Enabled: '*' = enabled by default; '+' = enabled by user;"
            " '-' disabled by user)\n")
        print(f"{' ':<30}\tStartup \tStartup")
        print(f"{'Script Name':<30}\tEligible\tEnabled")
        print(f"{'=' * 30}\t{'=' * 8}\t{'=' * 7}")

    for name in sorted(scripts):
        startup_str = ""
        enabled_str = ""

        if name in startup_config:
            startup_str = "*"  # startup script

            if startup_config[name]:
                # enabled by default; override if disabled by user
                enabled_str = "*" if user_config.get(name, True) else "-"
            else:
                # not enabled by default; override if enabled by user
                enabled_str = "" if not user_config.get(name, False) else "+"

        if name.endswith(".d"):
            min_kernel_version, max_kernel_version = get_compat_kernel_versions(
                scripts[name]
            )
            if not is_kernel_version_compatible(
                current_kernel_version, min_kernel_version, max_kernel_version
            ):
                continue

        print(f"{name:<30}\t{startup_str:^8}\t{enabled_str:^7}")


def run_script(script_name: str, args: List[str]) -> None:
    """Run the script with the given name."""

    script_path = get_available_scripts().get(script_name)

    if not script_path:
        logging.error("Script '%s' not found", script_name)
        sys.exit(1)

    if script_name.endswith(".d"):
        current_kernel_version = platform.uname().release
        min_kernel_version, max_kernel_version = get_compat_kernel_versions(script_path)
        if not is_kernel_version_compatible(
            current_kernel_version, min_kernel_version, max_kernel_version
        ):
            logging.error("Script '%s' not compatible for running kernel", script_name)
            sys.exit(1)

    logging.info("Running script '%s %s'...", script_path, " ".join(args))

    try:
        os.execv(script_path, [script_path] + args)  # nosec
    except Exception as exp:  # pylint: disable=broad-except
        logging.error("Execution failed: %s", exp)
        sys.exit(1)


def update_user_config(
        config_path: str, script_name: str, enable: Optional[bool]) -> None:
    """Update script in user configuration file.

    Perform following operations based on following values of `enable`:
      None: reset operation; remove every occurrence of `script_name` from the
            file.
      True: enable script; ensure only one line with content "*<script_name>"
            exists in the file.
      False: disable script; ensure only one line with content "!<script_name>"
             exists in the file.
    """
    lines = []

    if os.path.exists(config_path):
        with open(USER_STARTUP_CONFIG_FILE) as fdesc:
            lines = fdesc.readlines()
    else:
        if enable is None:
            return  # reset operation, nothing to do if the file doesn't exist

    with open(USER_STARTUP_CONFIG_FILE, "w") as fdesc:
        # Remove lines matching (insensitive to additional white space) either:
        #  - '+ <script_name>'
        #  - '- <script_name>'
        #  - empty lines  (this is just for cleanup of the config file)
        # Then add the script name at the end with the proper configuration.
        matching_regex = re.compile(f"^(([\\+-] +)?{script_name}$|)$")

        for line in lines:
            if not matching_regex.match(line.strip()):
                fdesc.write(line)

        if enable is not None:
            if enable:
                fdesc.write(f"+ {script_name}\n")
            else:
                fdesc.write(f"- {script_name}\n")


def reset_startup(script_name: Optional[str]) -> None:
    """Reset startup state of a script to the default value.

    If script_name is None, clear all user startup configuration.
    """
    if not os.path.isfile(USER_STARTUP_CONFIG_FILE):
        return  # nothing to do if there is no user config

    if script_name is None:
        # Reset all user startup config.  Just remove the user startup config.
        os.remove(USER_STARTUP_CONFIG_FILE)
    else:
        update_user_config(USER_STARTUP_CONFIG_FILE, script_name, enable=None)


def enable_startup(script_name: str) -> None:
    """Enable a script to run at startup."""
    scripts = get_available_scripts()
    startup_scripts = get_startup_script_names(STARTUP_CONFIG_FILE)

    if script_name not in scripts or script_name not in startup_scripts:
        logging.error("'%s' is not a valid startup script", script_name)
        sys.exit(1)

    # Just remove script from user config if it's enabled by default; otherwise
    # enable it in the user config.
    if startup_scripts[script_name]:
        update_user_config(USER_STARTUP_CONFIG_FILE, script_name, enable=None)
    else:
        update_user_config(USER_STARTUP_CONFIG_FILE, script_name, enable=True)


def disable_startup(script_name: str) -> None:
    """Disable a script to run at startup."""
    scripts = get_available_scripts()
    startup_scripts = get_startup_script_names(STARTUP_CONFIG_FILE)

    if script_name not in scripts or script_name not in startup_scripts:
        logging.error("'%s' is not a valid startup script", script_name)
        sys.exit(1)

    # Just remove script from user config if it's not enabled by default;
    # otherwise disable it in the user config.
    if not startup_scripts[script_name]:
        update_user_config(USER_STARTUP_CONFIG_FILE, script_name, enable=None)
    else:
        update_user_config(USER_STARTUP_CONFIG_FILE, script_name, enable=False)


def run_startup_scripts(scripts: Sequence[str], outdir: str) -> None:
    """Run startup scripts.

    See run_startup_enabled() for details.
    """
    pids = {}  # dictionary {pid: script_path} of all scripts executed

    for path in scripts:
        script_dir = os.path.join(outdir, os.path.basename(path))
        logging.info("Running script '%s'.  Outdir: '%s'", path, script_dir)

        try:
            os.makedirs(script_dir)

            with open(os.path.join(script_dir, "output.log"), "x") as out_fd:
                proc = subprocess.Popen(  # nosec
                    path, close_fds=True, stdout=out_fd, stderr=out_fd,
                    stdin=subprocess.DEVNULL, cwd=script_dir, shell=False)
                pids[proc.pid] = path
        except Exception as exp:  # pylint: disable=broad-except
            logging.error("Failed to execute '%s': %s", path, str(exp))

    # Wait for scripts to finish.  Print exit status of scripts as they
    # terminate, which can differ than the order in which they were spawned.
    while pids:
        try:
            pid, status = os.waitpid(-1, 0)
        except ChildProcessError:
            # Some times child processes are reaped in the "background",
            # causing os.waitpid() to raise ChildProcessError when there are no
            # more child processes to reap.  I was not able to determine how
            # they are being reaped, but I suspect subprocess module might be
            # doing it.  This is not deterministically reproducible, which
            # suggests a timing race.
            # If this happens, we won't be able to retrieve the exit status of
            # the processes we had pending, so just log an error informing the
            # situation and exit.
            logging.error(
                "Following child processes terminated but were reaped in the "
                "background; cannot determine their exit status:\n\t%s",
                "\n\t".join(f"PID: {p} - {path}" for p, path in pids.items()))
            sys.exit(1)

        script = pids.pop(pid, None)

        if script is not None:
            if os.WEXITSTATUS(status) == 0:
                logging.info("Script '%s' terminated successfully", script)
            else:
                logging.error(
                    "Script '%s' terminated with status %d.  See script's "
                    "output for more details", script, os.WEXITSTATUS(status))
        else:
            logging.error(
                "Unknown child process with PID %d terminated with status %d",
                pid, os.WEXITSTATUS(status))


def run_startup_enabled(base_outdir: str) -> None:
    """Run all the enabled startup scripts.

    Run startup enable scripts in parallel, each in it's own subdir
    `base_outdir/<timestamp>/<script_basename>/`.
    """
    startup_scripts = get_startup_script_names(STARTUP_CONFIG_FILE)
    user_config = get_user_startup_cofig(USER_STARTUP_CONFIG_FILE)

    # enabled startup scripts overridden with user config
    enabled_scripts = tuple(
        path
        for name, path in get_available_scripts().items()
        if (name in startup_scripts
            and user_config.get(name, startup_scripts[name]))
    )

    if not enabled_scripts:
        logging.info("No startup scripts enabled")
        return

    outdir = os.path.join(
        base_outdir, datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S"))

    logging.info("Running enabled startup scripts.  Output dir: %s", outdir)
    os.makedirs(outdir)
    run_startup_scripts(enabled_scripts, outdir)


def setup_logging(verbose: bool = False) -> None:
    """Setup application logging."""
    # Log to syslog if running as a systemd sevice
    if os.getenv("INVOCATION_ID") is not None:
        logging.basicConfig(
            format="%(levelname)s - %(message)s",
            handlers=(logging.handlers.SysLogHandler(address="/dev/log"),),
            level=logging.DEBUG if verbose else logging.INFO)
    else:
        logging.basicConfig(
            format="%(asctime)s.%(msecs)d %(levelname)s - %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
            level=logging.DEBUG if verbose else logging.INFO)


def parse_args(args: Optional[List[str]] = None) -> argparse.Namespace:
    """Parse CLI arguments."""

    parser = argparse.ArgumentParser(
        description="Performs actions on scripts provided by oled-tools.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    subparsers = parser.add_subparsers(
        dest="cmd",
        help="Subcommands")

    subparsers.add_parser(
        "list", help="List oled-tools scripts.")

    run_parser = subparsers.add_parser("run", help="Run oled-tools script.")
    run_parser.add_argument(
        "run_script", metavar="SCRIPT", help="Script to run")
    run_parser.add_argument(
        "run_args", metavar="ARGS", nargs=argparse.REMAINDER,
        help="Script arguments.")

    reset_parser = subparsers.add_parser(
        "reset-startup",
        help="Reset startup config of a script to system default")
    reset_group = reset_parser.add_mutually_exclusive_group(required=True)
    reset_group.add_argument(
        "--all", dest="reset_script", action="store_const", const=None)
    reset_group.add_argument(
        "reset_script", metavar="SCRIPT", nargs="?",
        help="Script to reset to default startup state")

    enable_parser = subparsers.add_parser(
        "enable-startup", help="Enable a script to run at startup")
    enable_parser.add_argument(
        "enable_script", metavar="SCRIPT", help="Script to enable")

    disable_parser = subparsers.add_parser(
        "disable-startup", help="Disable a script from running at startup")
    disable_parser.add_argument(
        "disable_script", metavar="SCRIPT", help="Script to disable")

    run_startup_parser = subparsers.add_parser(
        "run-startup-enabled", help="Run enabled startup scripts")
    run_startup_parser.add_argument(
        "--outdir", dest="run_startup_outdir", metavar="OUTDIR",
        default=STARTUP_SCRIPTS_OUT_DIR, help="Output dir")

    return parser


def main(args: Optional[List[str]] = None) -> None:
    """Main function."""
    parser = parse_args(args)
    options = parser.parse_args(args)

    setup_logging()

    if options.cmd != "list" and os.getuid() != 0:
        logging.error("Command '%s' must be run as root", options.cmd)
        sys.exit(1)

    if options.cmd == "list":
        list_scripts()
    elif options.cmd == "run":
        run_script(options.run_script, options.run_args)
    elif options.cmd == "reset-startup":
        reset_startup(options.reset_script)
    elif options.cmd == "enable-startup":
        enable_startup(options.enable_script)
    elif options.cmd == "disable-startup":
        disable_startup(options.disable_script)
    elif options.cmd == "run-startup-enabled":
        run_startup_enabled(options.run_startup_outdir)
    else:
        parser.print_help(sys.stderr)


def exit_signal_handler(*_args, **_kwargs) -> None:
    """Signal handler that exits the program."""
    logging.error("Interrupted")
    sys.exit(1)


if __name__ == "__main__":
    # gracefully handle common termination signals
    signal.signal(signal.SIGINT, exit_signal_handler)
    signal.signal(signal.SIGTERM, exit_signal_handler)

    main(sys.argv[1:])
