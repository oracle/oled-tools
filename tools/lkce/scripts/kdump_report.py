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

#
# program that runs inside kdump kernel
# program to run crash utility inside the kdump kernel
#
import time
import os
import re
import subprocess
import sys
import signal

class KdumpReport:
	def __init__(self):
		#global variables
		self.VMLINUX = ""
		self.VMCORE = "/proc/vmcore"
		self.KDUMP_KERNELVER = self.run_command('uname -r')

		self.KDUMP_REPORT_HOME = "/etc/oled/lkce"
		self.KDUMP_REPORT_CONFIG_FILE = self.KDUMP_REPORT_HOME + "/lkce.conf"
		self.KDUMP_REPORT_CRASH_CMDS_FILE = self.KDUMP_REPORT_HOME + "/crash_cmds_file"
		self.KDUMP_REPORT_OUT = "/var/oled/lkce"
		self.KDUMP_REPORT_OUT_FILE = self.KDUMP_REPORT_OUT + "/crash_" + time.strftime("%Y%m%d-%H%M%S") + ".out"
		self.TIMEDOUT_ACTION = "reboot -f"

		# default values
		self.kdump_report = "yes"
		self.vmlinux_path = "/usr/lib/debug/lib/modules/" + self.KDUMP_KERNELVER + "/vmlinux"
		self.crash_cmds_file = self.KDUMP_REPORT_CRASH_CMDS_FILE
		self.max_out_files = "50"

		self.read_config(self.KDUMP_REPORT_CONFIG_FILE)
	# def __init__

	def run_command(self, cmd):
		"""
		Execute command and return the result
		Parameters:
		cmd : Command to run

		Returns string: result of the specific command executed.
		"""
		command = subprocess.Popen(cmd,
					 stdin=None,
					 stdout=subprocess.PIPE,
					 stderr=subprocess.PIPE,
					 shell=True)
		out, err = command.communicate()
		if (sys.version_info[0] == 3):
			out = out.decode("utf-8").strip()

		return out.strip()
	#def run_command(self, cmd):

	def read_config(self, filename):
		if not os.path.exists(filename):
			self.exit()

		file = open(filename, "r")
		for line in file.readlines():
			if re.search("^#", line):# ignore lines starting with '#'
				continue

			# trim space/tab/newline from the line
			line = re.sub(r"\s+", "", line)

			entry = re.split("=", line)
			if "vmlinux_path" in entry[0] and entry[1]:
				self.vmlinux_path = entry[1]

			elif "crash_cmds_file" in entry[0] and entry[1]:
				self.crash_cmds_file = entry[1]

			elif "enable_kexec" in entry[0] and entry[1]:
				self.kdump_report = entry[1]

			elif "max_out_files" in entry[0] and entry[1]:
				self.max_out_files = entry[1]
	#def read_config

	def get_vmlinux(self):
		VMLINUX_1 = self.vmlinux_path
		VMLINUX_2 = "/usr/lib/debug/lib/modules/" + self.KDUMP_KERNELVER + "/vmlinux"

		if os.path.exists(VMLINUX_1):
			self.VMLINUX = VMLINUX_1
		elif os.path.exists(VMLINUX_2):
			self.VMLINUX = VMLINUX_2
		else:
			print("kdump_report: vmlinux not found in the following locations.")
			print("kdump_report: %s" % VMLINUX_1)
			print("kdump_report: %s" % VMLINUX_2)
			self.exit()

		print("kdump_report: vmlinux found at %s" % self.VMLINUX)
	# def get_vmlinux

	def run_crash(self):
		self.get_vmlinux()
		if not os.path.exists(self.crash_cmds_file):
			print("kdump_report: %s not found" % self.crash_cmds_file)
			return 1

		if not os.path.exists(self.KDUMP_REPORT_OUT):
			cmd = "mkdir -p " + self.KDUMP_REPORT_OUT
			os.system(cmd)

		cmd = "crash " + self.VMLINUX + " " + self.VMCORE + " -i " + self.crash_cmds_file + " > " + self.KDUMP_REPORT_OUT_FILE
		print("kdump_report: Executing '%s'" % cmd)
		os.system(cmd)
	# def run_crash

	def clean_up(self):
		cmd = "ls -r " + self.KDUMP_REPORT_OUT + "/crash*out 2>/dev/null | tail -n +" + \
		str(int(self.max_out_files) + 1)
		delete_files = self.run_command(cmd)
		delete_files = re.sub(r"\s+", " ", delete_files)

		if delete_files:
			print("kdump_report: found more than %s[max_out_files] out files. Deleting older ones" % self.max_out_files)
			cmd = "rm -f " + delete_files
			os.system(cmd)
	# def clean_up

	def exit(self):
		sys.exit(0)
	# def exit

# class KDUMP_REPORT

def main():
	kdump_report = KdumpReport()

	if kdump_report.kdump_report != "yes":
		print("kdump_report: kdump_report is disabled to run")
		kdump_report.exit()
	else:
		print("kdump_report: kdump_report is enabled to run")
		kdump_report.run_crash()
		kdump_report.clean_up()
		kdump_report.exit()
# def main

if __name__ == '__main__':
	main()
