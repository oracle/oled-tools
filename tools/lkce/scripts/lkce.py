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
# program for user-interaction
#
import sys, subprocess, os, re, platform, fcntl

class Lkce:
	def __init__(self):
		#global variables
		self.LKCE_HOME = "/etc/oled/lkce"
		self.LKCE_CONFIG_FILE = self.LKCE_HOME + "/lkce.conf"
		self.LKCE_OUTDIR = "/var/oled/lkce"
		self.LKCE_BINDIR = "/usr/lib/oled-tools"

		#vmlinux_path
		self.KDUMP_KERNELVER = self.run_command('uname -r')

		#default values
		self.ENABLE_KEXEC = "no"
		self.VMLINUX_PATH = "/usr/lib/debug/lib/modules/" + self.KDUMP_KERNELVER + "/vmlinux"
		self.CRASH_CMDS_FILE = self.LKCE_HOME + "/crash_cmds_file"
		self.VMCORE = "yes"
		self.MAX_OUT_FILES = "50"

		#effective values
		self.enable_kexec = self.ENABLE_KEXEC
		self.vmlinux_path = self.VMLINUX_PATH
		self.crash_cmds_file = self.CRASH_CMDS_FILE
		self.vmcore = self.VMCORE
		self.lkce_outdir = self.LKCE_OUTDIR
		self.max_out_files = self.MAX_OUT_FILES

		#set values from config file
		if os.path.exists(self.LKCE_CONFIG_FILE):
			self.read_config(self.LKCE_CONFIG_FILE)

		# lkce as a kdump_pre hook to kexec-tools
		self.LKCE_KDUMP_SH = self.LKCE_HOME + "/lkce_kdump.sh"
		self.LKCE_KDUMP_DIR = self.LKCE_HOME + "/lkce_kdump.d"
		self.FSTAB = "/etc/fstab"
		self.KDUMP_CONF = "/etc/kdump.conf"
		self.TIMEOUT_PATH = self.run_command('which timeout')
	#def __init__

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
		out,err = command.communicate()
		if (sys.version_info[0] == 3):
			out = out.decode("utf-8").strip()

		return out.strip()
	#def run_command(self, cmd):

	def configure_default(self):
		if not os.path.isdir(self.LKCE_HOME):
			cmd = "mkdir -p " + self.LKCE_HOME
			os.system(cmd)

		if self.enable_kexec == "yes":
			print("trying to disable lkce")
			self.disable_lkce()

		#crash_cmds_file
		filename = self.CRASH_CMDS_FILE
		content = """#
# This is the input file for crash utility. You can edit this manually
# Add your own list of crash commands one per line.
#
bt
bt -a
bt -FF
dev
kmem -s
foreach bt
log
mod
mount
net
ps -m
ps -S
runq
quit
"""
		file = open(filename, "w")
		file.write(content)
		file.close()

		#config file
		filename = self.LKCE_CONFIG_FILE
		content = """##
# This is configuration file for lkce
# Use 'oled lkce configure' command to change values
##

#enable lkce in kexec kernel
enable_kexec=""" + self.ENABLE_KEXEC + """

#debuginfo vmlinux path. Need to install debuginfo kernel to get it
vmlinux_path=""" + self.VMLINUX_PATH + """

#path to file containing crash commands to execute
crash_cmds_file=""" + self.CRASH_CMDS_FILE + """

#lkce output directory path
lkce_outdir=""" + self.LKCE_OUTDIR + """

#enable vmcore generation post kdump_report
vmcore=""" + self.VMCORE + """

#maximum number of outputfiles to retain. Older file gets deleted
max_out_files=""" + self.MAX_OUT_FILES

		file = open(filename, "w")
		file.write(content)
		file.close()

		print("configured with default values")
	#def configure_default

	def read_config(self, filename):
		if not os.path.exists(filename):
			return

		file = open(filename, "r")
		for line in file.readlines():
			if re.search("^#", line): #ignore lines starting with '#'
				continue

			# trim space/tab/newline from the line
			line = re.sub(r"\s+", "", line)

			entry = re.split("=", line)
			if "enable_kexec" in entry[0] and entry[1]:
				self.enable_kexec = entry[1]

			elif "vmlinux_path" in entry[0] and entry[1]:
				self.vmlinux_path = entry[1]

			elif "crash_cmds_file" in entry[0] and entry[1]:
				self.crash_cmds_file = entry[1]

			elif "lkce_outdir" in entry[0] and entry[1]:
				self.lkce_outdir = entry[1]

			elif "vmcore" in entry[0] and entry[1]:
				self.vmcore = entry[1]

			elif "max_out_files" in entry[0] and entry[1]:
				self.max_out_files = entry[1]
	#def read_config

	def create_lkce_kdump(self):
		filename = self.LKCE_KDUMP_SH
		cmd = "mkdir -p " + self.LKCE_KDUMP_DIR
		os.system(cmd)

		#for OL7 and above
		mount_cmd = "mount -o bind /sysroot"

		dist = self.get_dist()
		if dist == ".el6": #for OL6
			# get the root device
			cmd = "awk '/^[ \t]*[^#]/ { if ($2 == \"/\") { print $1; }}' " + self.FSTAB
			rootdev = self.run_command(cmd)
			if "LABEL=" in rootdev or "UUID=" in rootdev:
				cmd = "/sbin/findfs " + rootdev
				rootdev = self.run_command(cmd)
			cmd = "blkid -sUUID -o value " + rootdev
			root_uuid = self.run_command(cmd)
			mount_cmd = "mount UUID=" + root_uuid
		#if dist

		# create lkce_kdump.sh script
		content = """#!/bin/sh
# This is a kdump_pre script
# /etc/kdump.conf is used to configure kdump_pre script

# Generate vmcore post lkce_kdump scripts execution
LKCE_VMCORE=""" + self.vmcore + """

# Timeout for lkce_kdump scripts in seconds
LKCE_TIMEOUT="120"

# Temporary directory to mount the actual root partition
LKCE_DIR="/lkce_kdump"

mkdir $LKCE_DIR
""" + mount_cmd + """ $LKCE_DIR
mount -o bind /proc $LKCE_DIR/proc
mount -o bind /dev $LKCE_DIR/dev

LKCE_KDUMP_SCRIPTS=$LKCE_DIR""" + self.LKCE_KDUMP_DIR + """/*

#get back control after $LKCE_TIMEOUT to proceed
export LKCE_KDUMP_SCRIPTS
export LKCE_DIR
""" + self.TIMEOUT_PATH + """ $LKCE_TIMEOUT /bin/sh -c '
echo "LKCE_KDUMP_SCRIPTS=$LKCE_KDUMP_SCRIPTS";
for file in $LKCE_KDUMP_SCRIPTS;
do
	cmd=${file#$LKCE_DIR};
	echo "Executing $cmd";
	chroot $LKCE_DIR $cmd;
done;'

umount $LKCE_DIR/dev
umount $LKCE_DIR/proc
umount $LKCE_DIR

unset LKCE_KDUMP_SCRIPTS
unset LKCE_DIR

if [ "$LKCE_VMCORE" == "no" ]; then
	echo "lkce_kdump.sh: vmcore generation is disabled"
	exit 1
fi

exit 0
"""
		file = open(filename, "w")
		file.write(content)
		file.close()

		cmd = "chmod a+x " + filename
		os.system(cmd)
	# def create_lkce_kdump

	def remove_lkce_kdump(self):
		return self.update_kdump_conf("--remove")
	#def remove_lkce_kdump()

	# enable lkce in /etc/kdump.conf
	def update_kdump_conf(self, arg):
		if not os.path.exists(self.KDUMP_CONF):
			print("error: can not find %s. Please retry after installing kexec-tools") % (self.KDUMP_CONF)
			return 1

		KUDMP_PRE_LINE = "kdump_pre " + self.LKCE_KDUMP_SH
		KUDMP_TIMEOUT_LINE = "extra_bins " + self.TIMEOUT_PATH

		if arg == "--remove":
			cmd = "grep -q '^" + KUDMP_PRE_LINE + "$' " + self.KDUMP_CONF
			ret = os.system(cmd)
			if (ret):  # not present
				print("lkce_kdump entry not set in %s") % (self.KDUMP_CONF)
				return 1
			cmd = "sed --in-place '/""" + \
			KUDMP_PRE_LINE.replace("/", r"\/") + """/d' """ + self.KDUMP_CONF
			cmd = cmd + "; sed --in-place '/""" + \
			KUDMP_TIMEOUT_LINE.replace("/", r"\/") + """/d' """ + self.KDUMP_CONF
			os.system(cmd)
			self.restart_kdump_service()
			return 0

		#arg == "--add"
		cmd = "grep -q '^kdump_pre' " + self.KDUMP_CONF
		ret = os.system(cmd)
		if (ret):  # not present
			cmd = "echo '" + KUDMP_PRE_LINE + "' >> " + self.KDUMP_CONF
			cmd = cmd + "; echo 'extra_bins " + self.TIMEOUT_PATH + "' >> " + self.KDUMP_CONF
			os.system(cmd)
			self.restart_kdump_service()
		else:
			cmd = "grep -q '^" + KUDMP_PRE_LINE + "$' " + self.KDUMP_CONF
			if (os.system(cmd)):  # kdump_pre is enabled, but it is not our lkce_kdump script
				print("lkce_kdump entry not set in %s (manual setting needed)") % (self.KDUMP_CONF)
				print("\npresent entry in kdump.conf")
				cmd = "grep ^kdump_pre " + self.KDUMP_CONF
				os.system(cmd)
				print("\nHint:")
				print("edit the present kdump_pre script and make it run %s")%(self.LKCE_KDUMP_SH)
				return 1
			else:
				print("lkce_kdump is already enabled to run lkce scripts")
				return 0
	# def update_kdump_conf

	def get_dist(self):
		dist = None
		if (platform.linux_distribution()[0] == "Oracle VM server"):
			dist = ".el6"
		else:
			os_release = float(platform.linux_distribution()[1])
			if os_release > 6.0 and os_release < 7.0 :
				dist = ".el6"
			elif os_release > 7.0:
				dist = ".el7"
		return dist
	#def get_dist

	def restart_kdump_service(self):
		dist = self.get_dist()
		if dist == ".el6":
			cmd = "service kdump restart"
		else: #OL7 and above
			cmd = "systemctl restart kdump"

		print("Restarting kdump service..."),
		os.system(cmd)
		print("done!")
	# def restart_kdump_service

	def report(self, subargs):
		if not subargs:
			print("error: report option need additional arguments [oled lkce help]")
			return

		vmcore = ""
		vmlinux = ""
		crash_cmds_file = ""
		outfile = ""
		for subarg in subargs:
			subarg = re.sub(r"\s+", "", subarg)
			entry = re.split("=", subarg)
			if len(entry) < 2:
				print("error: unknown report option %s" % subarg)
				continue

			if "--vmcore" in entry[0]:
				vmcore = entry[1]

			elif "--vmlinux" in entry[0]:
				vmlinux = entry[1]

			elif "--crash_cmds" in entry[0]:
				crash_cmds_file = "/tmp/crash_cmds_file"
				file = open(crash_cmds_file, "w")
				for cmd in entry[1].split(","):
					cmd  = cmd + "\n"
					file.write(cmd)
				#for
				file.write("quit")
				file.close()

			elif "--outfile" in entry[0]:
				outfile = entry[1]

			else:
				print("error: unknown report option %s" % subarg)
				break
		#for

		if vmcore == "":
			print("error: vmcore not specified")
			return

		if vmlinux == "": vmlinux = self.vmlinux_path
		if not os.path.exists(vmlinux):
			print("error: %s not found" % vmlinux)
			return

		if crash_cmds_file == "": crash_cmds_file = self.crash_cmds_file
		if not os.path.exists(crash_cmds_file):
			print("%s not found" % crash_cmds_file)
			return

		cmd = "crash " + vmcore + " " + vmlinux + " -i " + crash_cmds_file
		if outfile != "": cmd = cmd + " > " + outfile
		print("lkce: executing '%s'" % cmd)
		os.system(cmd)

		if crash_cmds_file == "/tmp/crash_cmds_file":
			os.remove(crash_cmds_file)
	#def report

	def configure(self, subargs):
		if not subargs: #default
			subargs = ["--show"]

		filename = self.LKCE_CONFIG_FILE
		for subarg in subargs:
			if subarg == "--default":
				self.configure_default()
			elif subarg == "--show":
				if not os.path.exists(filename):
					print("config file not found")
					return

				print("%15s : %s" % ("vmlinux path", self.vmlinux_path))
				print("%15s : %s" %("crash_cmds_file", self.crash_cmds_file))
				print("%15s : %s " %("vmcore", self.vmcore))
				print("%15s : %s" %("lkce_outdir", self.lkce_outdir))
				print("%15s : %s" %("lkce_in_kexec", self.enable_kexec))
				print("%15s : %s" %("max_out_files", self.max_out_files))
			else:
				subarg = re.sub(r"\s+", "", subarg)
				entry = re.split("=", subarg)
				if len(entry) < 2:
					print("error: unknown configure option %s" % subarg)
					continue

				if "--vmlinux_path" in entry[0]:
					pathv=entry[1].replace("/", "\/")
					cmd = "sed -i 's/vmlinux_path=.*/vmlinux_path= " + pathv +"/' " + filename
					os.system(cmd);
					print("lkce: set vmlinux_path to %s" % entry[1])

				elif "--crash_cmds_file" in entry[0]:
					pathv=entry[1].replace("/", "\/")
					cmd = "sed -i 's/crash_cmds_file=.*/crash_cmds_file= " + pathv +"/' " + filename
					os.system(cmd);
					print("lkce: set crash_cmds_file to %s" % entry[1])

				elif "--lkce_outdir" in entry[0]:
					pathv=entry[1].replace("/", "\/")
					cmd = "sed -i 's/lkce_outdir=.*/lkce_outdir= " + pathv +"/' " + filename
					os.system(cmd);
					print("lkce: set lkce output directory to %s" % entry[1])

				elif "--vmcore" in entry[0]:
					if self.config_vmcore(entry[1]):
						return 1

					cmd = "sed -i 's/vmcore=.*/vmcore= " + entry[1] +"/' " + filename
					os.system(cmd);
					print("lkce: set vmcore to %s" % entry[1])

				elif "--max_out_files" in entry[0]:
					cmd = "sed -i 's/max_out_files=.*/max_out_files= " + entry[1] +"/' " + filename
					os.system(cmd);
					print("lkce: set max_out_files to %s" % entry[1])
				else:
					print("error: unknown configure option %s" % subarg)
		#for
	#def configure

	def config_vmcore(self, value):
		if value not in ['yes', 'no']:
			print("error: invalid option '%s'")%(value)
			return 1

		filename = self.LKCE_KDUMP_SH
		if not os.path.exists(filename):
			print("error: Please enable lkce first, using 'oled lkce enable'");
			return 1

		self.vmcore = value
		self.create_lkce_kdump()
		self.restart_kdump_service()
		return 0
	#def config_vmcore

	def enable_lkce_kexec(self):
		filename = self.LKCE_KDUMP_SH
		if not os.path.exists(filename):
			self.create_lkce_kdump()

		if self.update_kdump_conf("--add") == 1:
			return

		filename = self.LKCE_CONFIG_FILE
		cmd = "sed -i 's/enable_kexec=.*/enable_kexec=yes/' " + filename
		os.system(cmd);
		print("enabled_kexec mode")
	# def enable_lkce_kexec

	def disable_lkce_kexec(self):
		filename = self.LKCE_CONFIG_FILE
		if not os.path.exists(filename):
			print("config file not found")
			return

		if self.update_kdump_conf("--remove") == 1:
			return

		cmd = "sed -i 's/enable_kexec=.*/enable_kexec=no/' " + filename
		os.system(cmd);

		filename = self.LKCE_KDUMP_SH
		if os.path.exists(filename):
			cmd = "rm -f " + self.LKCE_KDUMP_SH
			os.system(cmd)
		print("disabled kexec mode")
	#def disable kexec

	def status(self):
		self.configure(subargs=["--show"])
	#def status

	def clean(self, subarg):
		if "--all" in subarg:
			val = raw_input("lkce removes all the files in %s dir. do you want to proceed(yes/no)? [no]:" %self.LKCE_OUTDIR)
			if "yes" in val:
				cmd = "rm " + self.LKCE_OUTDIR + "/crash*out 2> /dev/null"
				os.system(cmd)
			#if "yes"
		else:
			val = raw_input("lkce deletes all but last three %s/crash*out files. do you want to proceed(yes/no)? [no]:" % self.LKCE_OUTDIR)
			if "yes" in val:
				cmd = "ls -r " + self.LKCE_OUTDIR + "/crash*out 2>/dev/null| tail -n +4 | xargs rm 2> /dev/null"
				os.system(cmd) #start removing from 4th entry
	#def clean

	def listfiles(self):
		dirname = self.LKCE_OUTDIR
		if not os.path.exists(dirname):
			cmd = "mkdir -p " + dirname
			os.system(cmd)

		print("Followings are the crash*out found in %s dir:" % dirname)
		for filename in os.listdir(dirname):
			if re.search("crash.*out", filename):
				print("%s/%s" % (dirname, filename))
		#for
	#def listfiles

	def usage(self):
		usage = """Usage: """ + os.path.basename(sys.argv[0]) + """ <options>
options:
	report <report-options> -- Generate a report from vmcore
	report-options:
		--vmcore=/path/to/vmcore 		- path to vmcore
		[--vmlinux=/path/to/vmlinux] 		- path to vmlinux
		[--crash_cmds=cmd1,cmd2,cmd3,..]	- crash commands to include
		[--outfile=/path/to/outfile] 		- write output to a file

	configure [--default] 	-- configure lkce with default values
	configure [--show] 	-- show lkce configuration -- default
	configure [config-options]
	config-options:
		[--vmlinux_path=/path/to/vmlinux] 	- set vmlinux_path
		[--crash_cmds_file=/path/to/file] 	- set crash_cmds_file
		[--vmcore=yes/no]			- set vmcore generation in kdump kernel
		[--lkce_outdir=/path/to/directory] 	- set lkce output directory
		[--max_out_files=<number>] 		- set max_out_files

	enable_kexec	-- enable lkce in kdump kernel
	disable_kexec   -- disable lkce in kdump kernel
	status 	        -- status of lkce

	clean [--all]	-- clear crash report files
	list		-- list crash report files
"""
		print(usage)
		sys.exit()
	#def usage
#class LKCE

def main():
	lkce = Lkce()

	if len(sys.argv) < 2:
		lkce.usage()

	arg = sys.argv[1]
	if arg == "report":
		lkce.report(sys.argv[2:])

	elif arg == "configure":
		lkce.configure(sys.argv[2:])

	elif arg == "enable_kexec":
		lkce.enable_lkce_kexec()

	elif arg == "disable_kexec":
		lkce.disable_lkce_kexec() 

	elif arg == "status":
		lkce.status()

	elif arg == "clean":
		lkce.clean(sys.argv[2:])

	elif arg == "list":
		lkce.listfiles()

	elif arg == "help" or arg == "-help" or arg == "--help":
		lkce.usage()

	else:
		print("Invalid option: %s")%(arg)
		print("Try 'oled lkce help' for more information")
#def main

if __name__ == '__main__':
	if not os.geteuid() == 0:
		print("Please run lkce as root user.")
		sys.exit()

	lockdir = "/var/run/oled-tools"
	if not os.path.isdir(lockdir):
		os.makedirs(lockdir)
	lockfile = lockdir + "/lkce.lock"
	fh = open(lockfile, "w")
	try: #try lock
		fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
	except: #no lock
		print("another instance of lkce is running.")
		sys.exit()

	main()

	fh.close()
	os.remove(lockfile)
