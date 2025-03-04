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
"""oled-oomwatch"""

import os
import sys
import syslog
import argparse
import datetime
import json
import logging
from typing import Sequence, Mapping, Union
import subprocess
import psutil

VERSION = "1.0.0"
CONFIG_FILE = "/etc/oled/oomwatch.json"
PMIE_CONF_FILE = "/var/lib/pcp/config/pmie/config.default"
DEBUG = False

# Default values
default_config_dict = {
    "memfree_threshold": 0,
    "swapfree_threshold": 0,
    "delta": "30 sec",
    "holdoff": 0,
    "monitored_process": [""]
}


def print_conf(conf_dict: Mapping):
    """Print the current active oled.oom_watch configuration"""
    print(f"memfree_threshold : {conf_dict['memfree_threshold']}")
    print(f"swapfree_threshold: {conf_dict['swapfree_threshold']}")
    print(f"delta             : {conf_dict['delta']}")
    print(f"holdoff           : {conf_dict['holdoff']}")
    print(f"monitored_process : {conf_dict['monitored_process']}")


def set_oomwatch_value(key: str, val: str) -> bool:
    """Update a PMIE @key with the passed @val"""
    if DEBUG:
        logging.info("Setting oomwatch param %s=%s", key, val)
    try:
        subprocess.run(
            ["/usr/bin/pmieconf", "-f",
             PMIE_CONF_FILE, "modify", "oled.oom_watch", key, str(val)],
            check=True
        )
        return True
    except subprocess.CalledProcessError as e:
        if DEBUG:
            logging.error("Unexpected error setting PMIE param %s=%s: %s",
                          key, val, str(e))
        else:
            logging.error("Unexpected error setting PMIE param %s=%s",
                          key, val)
    return False


def reload_pmie() -> bool:
    """Reload PMIE"""
    try:
        subprocess.run(["/usr/libexec/pcp/lib/pmie", "reload"], check=True)
        return True
    except subprocess.CalledProcessError as e:
        if DEBUG:
            logging.error("Unexpected error reloading PMIE: %s", str(e))
        else:
            logging.error("Unexpected error reloading PMIE")
    return False


def oomwatch_disable() -> bool:
    """Disable the oomwatch PMIE rule"""
    try:
        subprocess.run(["/usr/bin/pmieconf", "-f", PMIE_CONF_FILE,
                        "disable", "oled.oom_watch"], check=True)
        logging.info("Disabled PMIE rule oled.oom_watch")
        return True
    except subprocess.CalledProcessError as e:
        if DEBUG:
            logging.error(
                "Unexpected error disabling PMIE rule oled.oom_watch: %s",
                str(e))
        else:
            logging.error(
                "Unexpected error disabling PMIE rule oled.oom_watch")
    return False


def oomwatch_enable() -> bool:
    """Return whether the oomwatch PMIE rule is enabled or not"""
    try:
        subprocess.run(["/usr/bin/pmieconf", "-f", PMIE_CONF_FILE,
                        "enable", "oled.oom_watch"],
                       check=True)
        logging.info("Enabled PMIE rule oled.oom_watch")
        return True
    except subprocess.CalledProcessError as e:
        if DEBUG:
            logging.error(
                "Unexpected error enabling PMIE rule oled.oom_watch: %s",
                str(e))
        else:
            logging.error("Unexpected error enabling PMIE rule oled.oom_watch")
    return False


def oomwatch_status() -> bool:
    """Enable the oomwatch PMIE rule"""
    try:
        result = subprocess.run(["/usr/bin/pmieconf", "-f", PMIE_CONF_FILE,
                                 "list", "oled.oom_watch", "enabled"],
                                check=True, stdout=subprocess.PIPE,
                                universal_newlines=True)
        for l in result.stdout.splitlines():
            cur_line = l.strip()
            if cur_line.startswith("enabled ="):
                val = cur_line[10:].strip()
                if val == "yes":
                    return True
                return False
    except subprocess.CalledProcessError as e:
        if DEBUG:
            logging.error("Unexpected error getting status for PMIE rule "
                          "oled.oom_watch: %s", str(e))
        else:
            logging.error("Unexpected error getting status for PMIE rule "
                          "oled.oom_watch")
    return False


def oomwatch_reload(conf_dict: Mapping) -> bool:
    """Push all @conf_dict PMIE key/value pairs to PMIE"""
    need_reload = False
    for k, v in conf_dict.items():
        if k != "monitored_process":
            need_reload = set_oomwatch_value(k, v) or need_reload
    return need_reload


def find_processes_to_kill(proc_names: Sequence[str]):
    """Find monitored processes consuming the most memory."""
    processes = []
    for proc in psutil.process_iter(['name', 'memory_info']):
        if proc.pid == os.getpid():
            continue
        try:
            if proc.info['name'] in proc_names:
                processes.append(proc)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return processes


def kill_high_memuser(proc_names: Sequence[str]):
    """Kill the process with the highest RSS usage."""
    start_time = datetime.datetime.now()

    logging.info(
        "Thresholds exceeded. Searching for processes to terminate...")

    processes_to_kill = find_processes_to_kill(proc_names)

    if processes_to_kill:
        p = max(processes_to_kill, key=lambda p: p.memory_info().rss)
        target_name = p.name()
        target_pid = p.pid

        logging.info("Killing process %s (PID: %d) using %.2f MB",
                     target_name, target_pid,
                     p.memory_info().rss / (1024 ** 2))

        try:
            subprocess.run(["/usr/bin/kill", "-9", str(p.pid)], check=True)
            logging.info("Process %s (PID: %d) killed using 'kill -9'.",
                         target_name, target_pid)
            execution_time = \
                (datetime.datetime.now() - start_time).total_seconds()
            logging.info("Execution Time: %.2f seconds", execution_time)
        except subprocess.CalledProcessError as e:
            logging.error("Unexpected error killing PID %d: %s",
                          target_pid, str(e))
    else:
        logging.info("No monitored processes are running.")


def setup_logging() -> None:
    """Setup application logging."""
    logging.basicConfig(
        format="%(asctime)s.%(msecs).3d %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
        level=logging.INFO)

    # set identity for syslog messages produced by this process
    syslog.openlog("oled.oom_watch")


def setup_args() -> argparse.ArgumentParser:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=f"oled oomwatch v{VERSION}",
                                     add_help=True)
    parser.add_argument("-v", "--version", action="store_true",
                        help="Print version")
    parser.add_argument("-e", "--enable", action="store_true",
                        help="Enable oomwatch")
    parser.add_argument("-d", "--disable", action="store_true",
                        help="Disable oomwatch")
    parser.add_argument("-r", "--reload", action="store_true",
                        help="Reload the oomwatch settings stored in %s" %
                        CONFIG_FILE)
    parser.add_argument("-s", "--status", action="store_true",
                        help="Display oomwatch status")
    parser.add_argument("-k", "--kill", action="store_true",
                        help="Kill processes matching specifications")

    subparsers = parser.add_subparsers(dest="command")
    config_parser = subparsers.add_parser("configure",
                               help="Configure oomwatch parameters")
    config_parser.add_argument("--show", action="store_true",
                               help="Show the current configuration")
    config_parser.add_argument("--delta", type=str,
                               help="Set delta parameter (time str)")
    config_parser.add_argument("--holdoff", type=str,
                               help="Set holdoff parameter (time str)")
    config_parser.add_argument("--memfree_threshold", type=float,
                               help="Set memfree threshold parameter (float)")
    config_parser.add_argument("--swapfree_threshold", type=float,
                               help="Set swapfree threshold parameter (float)")
    config_parser.add_argument(
        "--monitored_process", type=str,
        help="Set programs allowed to be killed (comma-delimited str)")
    return parser


def load_oomwatch_conf(conf_path: str) -> Union[Mapping, None]:
    """Load the stored oled.oom_watch config json"""
    try:
        with open(conf_path, "r") as conf:
            config = json.load(conf)
            return config
    except OSError as e:
        print(f"Unable to load config from {conf_path}: {e}")
    except json.JSONDecodeError as e:
        print(f"Unable to parse json from {conf_path}: {e}")
    return None


def write_oomwatch_conf(conf_path: str, conf_dict) -> bool:
    """Write the oled.oom_watch config file"""
    try:
        with open(conf_path, "w+") as conf:
            conf.write(json.dumps(conf_dict))
            if DEBUG:
                logging.info("Updated oomwatch conf at %s", conf_path)
            return True
    except OSError as e:
        print(f"Unable to load config from {conf_path}: {e}")
    except json.JSONDecodeError as e:
        print(f"Unable to parse json from {conf_path}: {e}")
    return True


def main(args: Sequence[str]) -> None:
    """Main function"""
    setup_logging()
    parser = setup_args()

    if len(sys.argv) < 2:
        parser.print_help(sys.stderr)
        sys.exit(1)

    options = parser.parse_args(args)
    need_reload = False

    if os.geteuid() != 0:
        logging.error("This script must be run as root.")
        sys.exit(1)

    conf = load_oomwatch_conf(CONFIG_FILE)
    if not conf:
        conf = default_config_dict

    if options.version:
        print(f"oomwatch version {VERSION}")
    elif options.enable:
        need_reload = oomwatch_enable()
    elif options.disable:
        need_reload = oomwatch_disable()
    elif options.status:
        if oomwatch_status():
            print("oomwatch is enabled")
        else:
            print("oomwatch is not enabled")
            sys.exit(1)
    elif options.reload:
        need_reload = oomwatch_reload(conf)
    elif options.kill:
        kill_high_memuser(conf["monitored_process"])
    elif options.command == "configure":
        change = False
        if options.show:
            print_conf(conf)
            return
        if options.memfree_threshold is not None:
            val = str(options.memfree_threshold)
            conf["memfree_threshold"] = val
            change = True
        if options.swapfree_threshold is not None:
            val = str(options.swapfree_threshold)
            conf["swapfree_threshold"] = val
            change = True
        if options.delta is not None:
            try:
                dval = int(options.delta.split()[0])
                if dval < 1:
                    print("The minimum acceptable value of delta is 1 second")
                    sys.exit(1)
            except (ValueError, IndexError):
                pass
            val = str(options.delta)
            conf["delta"] = val
            change = True
        if options.holdoff is not None:
            try:
                dval = int(options.holdoff.split()[0])
                if dval < 0:
                    print(
                        "The minimum acceptable value of holdoff is 0 seconds")
                    sys.exit(1)
            except (ValueError, IndexError):
                pass
            val = str(options.holdoff)
            conf["holdoff"] = val
            change = True
        if options.monitored_process is not None:
            monitored_programs = \
                [s.strip() for s in options.monitored_process.split(',')]
            conf["monitored_process"] = list(set(monitored_programs))
            change = True

        if change:
            write_oomwatch_conf(CONFIG_FILE, conf)
            need_reload = oomwatch_reload(conf)

    if need_reload:
        reload_pmie()


if __name__ == '__main__':
    main(sys.argv[1:])
