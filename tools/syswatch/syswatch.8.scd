syswatch(8)

# NAME

syswatch - Execute user-provided commands on CPU utilization.

# SYNOPSIS

syswatch [-h] [-b] [-C NR_CPUS] [-u %usr] [-n %nice] [-s %sys] [-i %iowait]
\[-t TARGET_DIR] [-M MAX_FS_UTIL] [-I SECONDS] -a CMD [-a CMD ...]

# DESCRIPTION

*syswatch* is used to watch CPU utilization and execute user-provided commands
when the configured thresholds are reached.

The tool samples CPU utilization for intervals of _SECONDS_ (5 seconds by
default), and if the configured thresholds are reached in that interval, the
commands specified with *-a* are executed.

At least one of *-u*, *-n*, *-s*, *-i* must be provided, which specify the CPU
thresholds to watch.  If more than one threshold is specified, the commands
will only trigger if all the thresholds are reached in a given interval.  These
values are percentages and must be integer values in the range [1, 100].

If _NR_CPUS_ is not specified or is <= 0, the cumulative utilization of all
CPUs is used.  Otherwise, the commands trigger only if _at least_ _NR_CPUS_
reach all the configured thresholds in the interval.

Option *-a* must be specified at least once.  _CMD_ must be a valid shell
command.  See *USER COMMAND CONSIDERATIONS* for important details.

By default, *syswatch* will create logs in subdirectories in
/var/oled/syswatch. If *-t* is specified, _TARGET_DIR_ is used as base
directory instead.  The base directory will be created if missing (either the
default value or the one configured with *-t*).  See *LOGGING* for a
description of the logs dumped by the tool.

By default, *syswatch* exits once the user-provided commands finish execution
when the CPU utilization thresholds are reached.  If *-b* is specified, instead
it will continue running indefinitely, it will restart watching once the user
commands finish.

*syswatch* monitors the space utilization of the filesystem where logging is
stored (see *LOGGING*).  If the configured space usage limit is reached,
*syswatch* terminates.  The default limit is 85% utilization (i.e. 15% free
space) but this limit can be changed with *-M*.  This limit is checked when
*syswatch* starts running and when the CPU threshold are reached, before
executing the user commands.

*syswatch* must be run as root.

# OPTIONS

*-h*
	Display help.

*-b*
	Run *syswatch* indefinitely until it's manually terminated.

*-C NR_CPUS*
	# of CPUs to apply match criteria.  A value <= 0 means apply system
	wide.  (default: 0)

*-u %user*
	%user utilization threshold.

*-n %nice*
	%nice utilization threshold.

*-s %sys*
	%sys utilization threshold.

*-i %iowait*
	%iowait utilization threshold.

*-t TARGET_DIR*
	_TARGET_DIR_ to store logs (see *LOGGING*). (default:
	/var/oled/syswatch).

*-M MAX_FS_UTIL*
	Max filesystem utilization, in percentage.  (default: 85)

*-I SECONDS*
	CPU utilization sampling interval, in seconds.  (default: 5)

*-a CMD*
	User command to execute when thresholds are reached.

# USER COMMAND CONSIDERATIONS

- The user commands are assumed to be valid shell commands, and are executed
  within a shell (the equivalent of `sh -c '$CMD'`).

- If more than one command is specified (i.e. *-a* appears more than once), all
  of them are executed in parallel (a separate shell is spawned for each one).

- Each command is execute in its own, dedicated directory (see *LOGGING*).

- When syswatch is instructed to run indefinitely (*-b*), CPU utilization
  during the time the commands are running will not be accounted; the next
  sampling interval will start when the commands finish.  This ensures that CPU
  spikes caused by the user commands themselves don't trip the tool.

# LOGGING

Logs are written in _TARGET_DIR_ /var/oled/syswatch by default unless this is
changed with *-t*.

All logs of one *syswatch* invocation will be placed in INSTANCE_DIR
$TARGET_DIR/syswatch_<datetime>\_<pid>.  Where <datetime> is the date and time
*syswatch* started running and <pid> is the PID of that instance.

*syswatch*'s logs will be at $INSTANCE_DIR/syswatch.log.  If stdout is a TTY,
logging will be written to stdout too.

When utilization thresholds are reached, a directory $INSTANCE_DIR/<datetime>
will be created (<datetime> being the datetime at which the thresholds were
reached) and inside this directory, one directory for every command to be
executed (named after the commands themselves; e.g. for command `echo hello`
directory `echo__hello` will be created).  The commands will be executed with
their $CWD pointing to this latter directory, and stdout/stderr redirected to
file `output` inside that directory.

When *syswatch* is ran indefinitely (*-b*), there will be one
$INSTANCE_DIR/<datetime> directory for every time the thresholds are reached.

# DATA COLLECTION

When collecting data for distribution (e.g. to give to support team), it's
recommended to collect the whole *syswatch*'s INSTANCE_DIR, as described in
*LOGGING* (e.g. as a \*.tar.gz file).  If there where several *syswatch* runs,
the whole TARGET_DIR can be collected instead.

# EXAMPLES

- Collect perf data once 99% sys utilization is reached system-wide:

```
	$ syswatch -s 99 -a 'perf record -a -g -F99 -- sleep 15'
```

- Run `cmd1` and `cmd2` in parallel when 90 %sys and 30% user util is reached:

```
	$ syswatch -s 99 -u 30 -a 'cmd1' -a 'cmd2'
```

- Run `cmd1` and `cmd2` serially and `cmd3` in parallel on 70% user util:

```
	$ syswatch -u 70 -a 'cmd1 ; cmd2' -a 'cmd3' ...
```

- Run `cmd` if at least 3 CPUs reach 70% sys and 30% user within intervals of
  2 seconds and the filesystem where the logs are stored has at most 80% space
  used (i.e. 20% free space):

```
	$ syswatch -s 70 -u 30 -C 3 -I 2 -a 'cmd' -M 80
```

- Run syswatch continuously and run `cmd` every time 85% sys util is reached
  and use /tmp/syswatch as base dir for the logs:

```
	$ syswatch -b -s 85 -a 'cmd' -t /tmp/syswatch
```

- Wait until system reached 90% sys util and exit, then run `cmd`:

```
	$ syswatch -s 90 -a true && cmd
```

; vim:set tw=79:
