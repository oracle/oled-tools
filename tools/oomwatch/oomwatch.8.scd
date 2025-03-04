oled-oomwatch(8)

# NAME

oled-oomwatch - Monitor memory thresholds and take action when they are exceeded.

# SYNOPSIS

oled oomwatch  {-h --help | -v --version | configure | -e --enable
\| -d --disable | -r --reload | -s --status | -k --kill}

# DESCRIPTION

*oled-oomwatch* Is a userspace oom-killer.  The tool will monitor swap and memory usage and will kill the top memory using process out of a list of processes once the configured thresholds are exceeded.  The tool uses Performance Co-Pilot's (PCP) Performance Metric Inference Engine (PMIE) to monitor the memory and swap thresholds.

# COMMANDS

*-h --help*
	Display help

*-v --version*
	Display the version information.

*-d --disable*
	Disable *oled-oomwatch* from running.

*-e --enable*
	Enable *oled-oomwatch*.  Once you enable the tool, it will begin monitoring the memory and swap usage and will kill the selected programs if the thresholds are exceeded.

*-r --reload*
	Reload the settings stored in the *oled-oomwatch* configuration file.

*-s --status*
	Display whether *oled-oomwatch* is enabled or not.

*-k --kill*
	Kill the selected programs if the thresholds are exceeded.  You do not, normally, need to run this command manually.  *oled-oomwatch* will take care of this for you.

*configure*
	The configure command has several sub-options discussed below.
		- --help - Display configuration specific help.
		- --show - Display the current configuration.
		- --delta - This sets the frequency, in seconds, at which the thresholds are checked.
		- --holdoff - Specifies the "cool off" period in seconds.  The default of 0 should be a good setting for most use cases.
		- --memfree_threshold - The threshold for memory free.  This is expressed as a percentage of free memory remaining.  Setting this to 100 or greater means that it is, effectively, always true.  Setting it to 0 is, effectively, always false.
		- --swapfree_threshold - This will set the threshold for free swap.  Like memfree_threshold it is also a percentage.
		- --monitored_process - Use this to specify the list of process names that *oled-oomwatch* is allowed to kill.  Provide this as a quoted, comma-separated list. Only programs whose names are in this list will be killed when memory or swap thresholds are met or exceeded.



# EXAMPLE

The following commands can be used to configure *oled-oomwatch* to kill the top memory user out of the programs listed when there is less than 10% memory free *and* less than 25% swap free.  The tool will check every 5 seconds.

```
oled oomwatch configure --delta=5 --memfree_threshold=10 --swapfree_threshold=25 --monitored_process="application1,application2"

oled oomwatch -e
```

# FILES

_/var/oled/pcp/pmie/HOSTNAME/pmie.log_
	Diagnostic information will be written to this file from PMIE

# SEE ALSO

PCP(1), PMIE(1), OLED(8)
