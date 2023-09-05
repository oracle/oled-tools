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

# Author: Srikanth C S <srikanth.c.s@oracle.com>

"""
This script scans the backup images for corruption. This is helpful
to monitor VM images and scan them for corruption without needing
a downtime on the Guest machine.

Instructions on running the script:
usage: scanfs [-h] [-c] [-s] directory_path

optional arguments:
  -h, --help      show this help message and exit
  -c, --clean     Cleanup if any loopback devices are left behind
  -s, --setup     Setup loopback devices and exit

Output:
All the script operations are saved to the file
directory_path/Scanfs-<date>/summary
If any corruption is detected, the volume names are added to file
directory_path/Scanfs-<date>/corruption


Overview of what the script does:
1. Makes sure that we are on the host
2. Prompt cleanup message if loop devices are left behind
3. Create a reflink copy of the .img files
4. Setup the loop devices and partition mappings
5. Rename the conflicting VG to scan<Guest name>
6. Determine the type of filesystem and run xfs_repair or e2fsck
(Does not repair the fs, just check for corruption)
7. Record the outputs of repair to a summary file
8. If any corrupt device is found report it and add its name to corruption file
9. Cleanup - Remove VGs, partitions and loop devices and update VG cache

#####################Error codes#####################
        0 - Successful
        1 - Some general errors
        2 - Not running script on host
        3 - Does not support reflinks
        4 - Mount failure
        5 - Corruptions found
        6 - vgimportclone fails(renaming VG failed)
        7 - Another instance of the script is running
######################################################
"""

import time
import sys
import os
import signal
import argparse
import subprocess  # nosec
import fcntl
import datetime
import glob
import platform
import logging
from pathlib import Path
from typing import List, Tuple, Dict, Mapping, Optional
from types import FrameType

VERSION = "1.0"


class FailCleanup(Exception):
    """
    Failed to do operation or received a non-zero return code.
    """


def print_both(message: str) -> None:
    """
    Write data to file and to tty
    """
    logging.info(message)
    print(message)


def exit_with_msg(msg: str = "", error: int = 1) -> None:
    """"
    Error out when something undesired happens
    """
    print(msg)
    logging.error(msg)
    sys.exit(error)


def check_if_root() -> None:
    """
    Check if the script is run in superuser mode
    """
    if os.getuid() != 0:
        exit_with_msg("This tool should be run as root.", 1)


def validate_args(args: argparse.Namespace) -> bool:
    """
    Checks if there are any invalid combinations of arguments:
    - "clean" cannot be combined with "setup"
    """
    return os.path.isdir(args.directory_path)


def reflink_copy(ref_dir: str, images: List[str]) -> List[str]:
    """
    Create reflink copies of the files
    """
    reflink_copies = []
    for filename in images:
        reflink_img = os.path.join(ref_dir, f"ref{os.path.basename(filename)}")
        print_both(f"setting up - {os.path.basename(filename)}")
        logging.info("Original Image - %s", filename)
        start_time = time.time()
        process = subprocess.run(
            ["/bin/cp", "--reflink", filename, reflink_img],
            check=False, shell=False)  # nosec
        if process.returncode:
            exit_with_msg("Reflink copy failed", 1)

        logging.info("Reflink copy - %s copy time = %s s\n",
                     reflink_img, round((time.time() - start_time), 3))
        reflink_copies.append(reflink_img)
    return reflink_copies


def setup(path: str, ref_dir: str,
          new_vg_name: str) -> Tuple[List[str], List[str], Dict]:
    """
    Take reflink copies of .img files
    Setup loop devices
    Get all PVs and rename it
    Get the list of VGs to run the tests on
    Activate these VGs
    """
    # Check if .img files are present
    print_both("\nSetting up")
    images = glob.glob(os.path.join(path, "*.img"))
    if not images:
        exit_with_msg("Directory does not contain any .img files."
                      " Please check the path.")

    logging.info("Creating %s to store reflink copies\n", ref_dir)
    os.makedirs(ref_dir, exist_ok=True)

    reflink_copies = reflink_copy(ref_dir, images)
    logging.info("\nSetting up the loop devices\n")
    old_vg = subprocess.run(
        ["/sbin/vgs", "--readonly", "--noheadings", "-o", "vg_name"],
        stdout=subprocess.PIPE, check=False,
        shell=False).stdout.decode().split()  # nosec

    for i in reflink_copies:
        logging.info(
            subprocess.run(["/sbin/kpartx", "-av", i], check=False,  # nosec
                           stdout=subprocess.PIPE).stdout.decode())

    process = subprocess.run(["/sbin/pvscan"],  # nosec
                             stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, check=False)
    logging.info(process.stdout.decode())
    logging.info(subprocess.getoutput("/sbin/pvscan --cache"))

    uniq_vgs = rename_vgs(new_vg_name, old_vg)

    logging.info(subprocess.getoutput("/sbin/vgscan"))
    for vg_ in uniq_vgs:
        logging.info("\nActivating VG - %s", vg_)
        logging.info(
            subprocess.run(["/sbin/vgchange", "-ay", vg_],  # nosec
                           stdout=subprocess.PIPE,
                           check=False).stdout.decode())

    # Get the XFS and EXT4 block devices that are not part of any volume groups
    dev_xfs = subprocess.getoutput("blkid -o device -t TYPE=xfs | grep loop")
    block_dev = {}
    block_dev['xfs'] = dev_xfs.split()

    dev_ext4 = subprocess.getoutput("blkid -o device -t TYPE=ext4 | grep loop")
    block_dev['ext4'] = dev_ext4.split()

    logging.info("\nlsblk output")
    logging.info(subprocess.getoutput("/bin/lsblk -f"))
    print_both("Setup Complete")
    return reflink_copies, uniq_vgs, block_dev


def rename_vgs(new_vg_name: str,
               old_vg: List[str]) -> List[str]:
    """
    Rename all the conflicting Volume groups
    """
    # pylint: disable=too-many-locals
    conflicting_vg = []
    uniq_vgs = []
    pvs_op = subprocess.run(  # nosec
        ["/sbin/pvs", "--noheadings", "-o", "pv_name,vg_name", "-S",
         "pv_name=~loop"], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False).stdout.decode().splitlines()
    pvs = [i.split() for i in pvs_op]
    new_vg = set(vg for pv, vg in pvs)

    for vg_ in new_vg:
        vg_name = f"{new_vg_name}-{vg_}"
        if vg_ in old_vg:
            # Conflicting VG names, rename the vg using vgimportclone
            conflicting_vg.append(vg_)
            rename_pvs = []
            for i in pvs:
                if i[1] == vg_:
                    rename_pvs.append(i[0])
            logging.info("\nPV List = %s", rename_pvs)
            if rename_pvs:
                logging.info("Renaming volume group from %s to %s",
                             vg_, vg_name)
                logging.info("vgimportclone -n %s %s", vg_name,
                             ' '.join(map(str, rename_pvs)))
                logging.info(subprocess.getoutput("/sbin/pvscan --cache"))
                output = subprocess.run(["/sbin/vgimportclone", "-n",  # nosec
                                         vg_name] + rename_pvs,
                                        check=False)
                if output.returncode:
                    print_both("Failed to rename the VG \n"
                               "Cleaning up...\n")
                    uniq_vgs += [vg for vg in new_vg
                                 if vg not in conflicting_vg]
                    raise FailCleanup("VG rename failed")
        else:
            # For Non-coinciding VG names, rename them
            # to script specific names for tracking
            logging.info("\nRenaming volume group from "
                         "%s to %s", vg_, vg_name)
            logging.info("vgrename %s %s", vg_, vg_name)
            logging.info(
                subprocess.run(["/sbin/vgrename", vg_, vg_name],  # nosec
                               stdout=subprocess.PIPE,
                               check=False).stdout.decode())
        uniq_vgs.append(vg_name)
    for vg_ in conflicting_vg:
        if vg_ in uniq_vgs:
            print_both(f"{vg_}, present in VG List, cleaning up\n")
            raise FailCleanup("Conflicting VGs found")
    return uniq_vgs


def cleanup(reflink_copies: List[str], del_ref=True) -> None:
    """
    Deactivate new VGs added from loop devices
    Remove partition mappings and loop devices
    Delete the reflink copies
    Update VG cache
    """
    print_both("\nCleaning up")

    vg_uuid = []
    for image in reflink_copies:
        loopdev = subprocess.getoutput("losetup -O NAME "
                                       f"-j {image}").splitlines()
        if not loopdev:
            logging.warning("No loop device found for %s", image)
            continue
        dev_name = os.path.basename(loopdev[-1])
        uuids = subprocess.getoutput("vgs --noheadings -o vg_uuid "
                                     f"--select pv_name=~{dev_name}p")
        vg_uuid += uuids.split()
    for id_ in vg_uuid:
        logging.info(subprocess.getoutput("vgchange -an --select "
                                          f"vg_uuid={id_}"))
    logging.info(subprocess.getoutput("/sbin/vgscan --cache"))

    logging.info("\nRemoving partition mappings and loop devices")
    loop_dev = []
    for image in reflink_copies:
        loop_dev.append(subprocess.getoutput(
            f"losetup -j \"{image}\" -O NAME").split()[-1])
    for i in loop_dev:
        subprocess.run(["/sbin/kpartx", "-d", i],  # nosec
                       check=False)
        subprocess.run(["/sbin/losetup", "-d", i],  # nosec
                       check=False)
    if del_ref:
        for i in reflink_copies:
            os.remove(i)

    print_both("Cleanup complete.\n")
    logging.info("lsblk-output")
    logging.info(subprocess.getoutput("/bin/lsblk"))
    logging.info("Updating VG cache\n")
    logging.info(subprocess.getoutput("/sbin/vgscan --cache"))


def check_and_clean(path: str) -> None:
    """"
    If script is called with --clean flag, check for
    loop devices and then call cleanup()
    """
    if not int(subprocess.getoutput("losetup | wc -l")):
        exit_with_msg("\nNo loop devices found, nothing to cleanup\n", 0)

    if int(subprocess.getoutput("losetup | grep '(deleted)' | wc -l")):
        print("\nUnderlying file of loop device is deleted.")

    reflink_copies = subprocess.getoutput(
        f"losetup -O BACK-FILE | grep -E '(^|\\s){path}/ref_dir'").splitlines()

    if reflink_copies:
        cleanup(reflink_copies, del_ref=False)
        print_both("\nThe base images of the loop devices "
                   "are left behind:")
        for i in reflink_copies:
            print_both(i)
        print()
        exit_with_msg("Delete these manually if they are not required\n", 0)
    else:
        exit_with_msg(f"\nNo loop devices found from {path}.\n"
                      "Loop devices are from a different path. "
                      "Please clean them up manually\n", 0)


def check_loop() -> bool:
    """
    Check for pre-existing loop devices
    """
    if int(subprocess.getoutput("losetup | wc -l")):
        print_both("Detected pre-existing loop devices")
        print_both("Please clean them up"
                   " before running the script again\n")
        return False
    return True


def check_mount(path: str) -> None:
    """
    Check if the mount point supports reflink
    """
    mount_point = find_mount_point(path)
    output = subprocess.getoutput(f"xfs_info {mount_point}")
    if "reflink=1" not in output:
        exit_with_msg("Error: filesystem for '"
                      f"{path} ' doesn't support Reflink", 3)
    if not glob.glob(os.path.join(path, "*.img")):
        exit_with_msg("No .img files in given path")


def debug_print(path: str) -> None:
    """
    Print debug data to the log file
    """
    logging.debug("\nDebug data:\n")
    logging.debug("Date - %s",
                  (datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')))
    logging.debug("Scan directory path - %s", path)
    logging.debug("\nImage under given path - \n")
    logging.debug(subprocess.getoutput(f"ls -lhs {path}/*.img"))
    for filename in glob.glob(os.path.join(path, "*.img")):
        logging.debug(subprocess.getoutput(f"/bin/stat {filename}"))
    logging.debug("\nKernel - \n %s", platform.uname())
    logging.debug("Uptrack version - ")
    logging.debug(subprocess.getoutput("/bin/uptrack-uname -a"))
    logging.debug("Last Reboot time - \n")
    logging.debug(subprocess.getoutput("last -n 3 reboot"))


def xfs_check(tmp_mnt: str, uniq_vgs: List[str],
              block_dev: Mapping[str, List[str]]) -> List[List[str]]:
    """
    Scan XFS volumes for corruption
    """
    print_both("\nChecking XFS Filesystems")
    volumes = []
    corrupt = []
    for vg_ in uniq_vgs:
        for vol in subprocess.getoutput(f"blkid /dev/{vg_}/* -o "
                                        "device -t TYPE=xfs").split():
            volumes.append(vol)
    volumes = volumes + block_dev['xfs']
    for vol in volumes:
        # If XFS host FS fails to get the extent allocation, it can
        # lead to mount failure. This can happen when the extent hint
        # is set to a high value and the filesystem is extremely
        # fragmented. Multiple runs of mount should eventually succeed
        # as the allocation gets fulfilled.
        #
        # This is fixed in the kernel by commit "xfs: don't use delalloc
        # extents for COW on files with extsize hints".
        # Retaining this to make sure that extent allocation isn't
        # the reason for mount failure, in case the kernel does not
        # contain the above fix. Same applies for ext4_check().
        for _ in range(5):
            mnt = subprocess.run(["/bin/mount", vol, tmp_mnt],  # nosec
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, check=False)
            if mnt.returncode == 0:
                logging.info("mount %s successful", vol)
                break
            logging.info("mount %s failed, retry mount", vol)
            logging.info("\noutput %s \n", mnt.stdout)
        else:
            print_both(f"{vol} - mount failed - marking it as corrupt")
            corrupt.append(["xfs - mount-fail", vol])
            continue

        umnt = subprocess.run(["/bin/umount", tmp_mnt],  # nosec
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, check=False)
        if umnt.returncode != 0:
            print_both("umount failed - doing a cleanup")
            logging.info("\noutput %s\n", umnt.stdout)
            raise FailCleanup("Umount failed")
        logging.info("umount %s successful", vol)

        print_both(f"Scanning {vol}")
        logging.info("\n\t\txfs_repair -n %s", vol)
        output = subprocess.run(["/sbin/xfs_repair", "-n", vol],  # nosec
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        logging.info(output.stderr.decode())
        logging.info(output.stdout.decode())
        if output.returncode != 0:
            corrupt.append(["xfs", vol])
    return corrupt


def ext4_check(tmp_mnt: str, uniq_vgs: List[str],
               block_dev: Mapping[str, List[str]]) -> List[List[str]]:
    """
    Scan EXT4 volumes for corruption
    """
    print_both("\nChecking EXT4 Filesystems")
    volumes = []
    corrupt = []
    for vg_ in uniq_vgs:
        for vol in subprocess.getoutput(f"blkid /dev/{vg_}/* -o "
                                        "device -t TYPE=ext4").split():
            volumes.append(vol)
    volumes = volumes + block_dev['ext4']

    for vol in volumes:
        # See comment in xfs_check() regarding multiple mount retries.
        for _ in range(5):
            mnt = subprocess.run(["/bin/mount", vol, tmp_mnt],  # nosec
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, check=False)
            if mnt.returncode == 0:
                logging.info("mount %s successful", vol)
                break
            logging.info("mount %s failed, retry mount", vol)
            logging.info("\noutput %s \n", mnt.stdout)
        else:
            print_both(f"{vol} - mount failed - marking it as corrupt")
            corrupt.append(["ext4 - mount-fail", vol])
            continue

        umnt = subprocess.run(["/bin/umount", tmp_mnt],  # nosec
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, check=False)
        if umnt.returncode != 0:
            print_both("umount failed - doing a cleanup")
            logging.info("\noutput %s\n", umnt.stdout)
            raise FailCleanup("Umount failed")
        logging.info("umount %s successful", vol)

        print_both(f"Scanning {vol}")
        logging.info("\n\t\te2fsck -fn %s", vol)

        output = subprocess.run(["/sbin/e2fsck", "-fn", vol],  # nosec
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        logging.info(output.stderr.decode())
        logging.info(output.stdout.decode())
        if output.returncode != 0:
            corrupt.append(["ext4", vol])
    return corrupt


def display_corruption(corrupt_file: str,
                       corrupt: List[List[str]]) -> None:
    """
    Display the corrupt volumes if found
    corrupt - List that saves corrupt volumes
    corrupt_file - Name of the file that saves the names
    of these corrupt volumes
    """
    if not corrupt:
        print_both("\nNo corrupt volume found\n")
    else:
        print_both("Corruption found in these volumes:")
        print_both("\n************************************\n")
        with open(corrupt_file, "w", encoding="utf8") as cfp:
            for fs_type, dev in corrupt:
                print_both(f"{fs_type} - {dev}")
                cfp.write(f"{fs_type} - {dev}")
        print_both("\n************************************\n")
        exit_with_msg("", 5)


def find_mount_point(path: str) -> str:
    """
    Find the mount point for the given path
    """
    path = os.path.realpath(path)
    while not os.path.ismount(path):
        path = os.path.dirname(path)
    return path


def remove_directory(dir_name: str) -> None:
    """
    Function to remove a dir and exit if it fails
    """
    try:
        if os.path.isdir(dir_name) and len(os.listdir(dir_name)) == 0:
            os.rmdir(dir_name)
    except OSError:
        exit_with_msg("rmdir failed ", 1)


def signal_handler(_signo: int, _frame: Optional[FrameType]) -> None:
    """
    Handle the signals such as ctrl-c
    """
    print("Received interrup, cleaning up...")
    sys.exit(_signo)


def setup_signal_handlers() -> None:
    """
    Catch ctrl-c and other signals that can terminate the script and
    process them to do a cleanup and exit.
    """
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGHUP, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)


def parse_args() -> argparse.Namespace:
    """
    Parse the CLI arguments
    """
    parser = argparse.ArgumentParser(
        prog='scanfs',
        description='scanfs: Scan KVM images for corruption.'
                    'Supports XFS and EXT4')

    parser.add_argument("directory_path", help="Path to the image files",
                        type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-c", "--clean", help="Cleanup if any loopback"
                       "devices are left behind", action="store_true")
    group.add_argument("-s", "--setup", help="Setup loopback devices"
                       "and exit", action="store_true")

    args = parser.parse_args()
    if not validate_args(args):
        parser.error(f"Invalid Directory Path '{args.directory_path}'")
    return args


def setup_logging(log_file: str) -> None:
    """Setup application logging."""
    formatter = logging.Formatter(
        fmt="%(asctime)s %(levelname)s - %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S%z")

    file_hdlr = logging.FileHandler(log_file, mode="w")
    file_hdlr.setFormatter(formatter)

    logger = logging.getLogger()

    logger.addHandler(file_hdlr)
    logger.setLevel(logging.DEBUG)


def main() -> None:
    """
    main function
    """
    # pylint: disable=too-many-statements,too-many-locals
    args = parse_args()
    check_if_root()

    path = args.directory_path
    path = os.path.abspath(path)
    check_mount(path)

    timestamp = datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    # For the conflicting VG, rename it to
    # Scan<last 6 characters of the VM name>
    vm_name = os.path.splitext(os.path.basename(path))[0][-6:]
    new_vg_name = f"Scan{vm_name}"
    op_dir = f"{path}/Scanfs-{timestamp}"
    ref_dir = f"{path}/ref_dir-{timestamp}"
    tmp_mnt = f"{path}/mnt-{timestamp}"

    corrupt = []         # type: List[List[str]]
    uniq_vgs = []        # type: List[str]
    reflink_copies = []  # type: List[str]
    block_dev = {}       # type: Dict[str, List]
    fail_flag = False

    os.makedirs(op_dir, exist_ok=True)

    summary = os.path.join(op_dir, "summary")

    setup_logging(summary)

    print_both(f"\nScanfs {VERSION} \n")
    corrupt_file = os.path.join(op_dir, "corruption")

    try:
        if args.clean:
            debug_print(path)
            print(f"Check {summary} for more details\n")
            check_and_clean(path)

        bool_loop = check_loop()
        if not bool_loop:
            exit_with_msg()
        debug_print(path)
        reflink_copies, uniq_vgs, block_dev = setup(path,
                                                    ref_dir,
                                                    new_vg_name)

        if args.setup:
            exit_with_msg("Stopping after setup", 0)

        os.makedirs(tmp_mnt, exist_ok=True)

        corrupt += xfs_check(tmp_mnt, uniq_vgs, block_dev)
        corrupt += ext4_check(tmp_mnt, uniq_vgs, block_dev)

    except FailCleanup as error:
        print(error, ", doing a cleanup")
        fail_flag = True
    except BaseException as error:  # pylint: disable=broad-except
        fail_flag = True
        logging.info("ERRNO: %s", error)
    finally:
        if args.setup or args.clean:
            sys.exit(0)
        if not bool_loop:
            sys.exit(1)
        if os.path.ismount(tmp_mnt):
            subprocess.run(["/bin/umount", tmp_mnt],  # nosec
                           check=False)
        logging.info(subprocess.getoutput("/sbin/vgscan --cache"))
        remove_directory(tmp_mnt)
        cleanup(reflink_copies)
        remove_directory(ref_dir)
        print(f"Check {summary} for more details\n")
        if fail_flag:
            sys.exit(1)
        display_corruption(corrupt_file, corrupt)


if __name__ == '__main__':
    with open(__file__, "rb") as lock_fd:
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except IOError as err:
            print("ERROR: ", err)
            print(__file__, " is already running, exiting ",
                  file=sys.stderr)
            sys.exit(7)
        main()
