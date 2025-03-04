oled-scripts(8)

# NAME

oled-scripts - Execute additional oled-tools scripts.

# SYNOPSIS

oled-scripts [-h] {list | run SCRIPT [ARGS]
\| run-startup-enabled [--outdir OUTDIR ] | enable-startup SCRIPT
\| disable-startup SCRIPT | reset-startup {--all | SCRIPT}}

# DESCRIPTION

*oled-scripts* is used to run additional scripts provided by oled-tools.  The
list of available scripts can be listed with command *list*.

# COMMANDS

*list*
	List available scripts.

*run* _SCRIPT_ [_ARGS_]
	Run the given script with the given arguments.  _SCRIPT_ must be an
	script name as returned by command *list*.

*run-startup-enabled* [--outdir _OUTDIR_]
	Run all the enabled startup scripts.  _OUTDIR_ is used as base
	directory for the output of the scripts; /var/oled/startup-scripts/ is
	used by default.  See *STARTUP SCRIPTS* for more details.

*enable-startup* _SCRIPT_
	Enable a startup script.

*disable-startup* _SCRIPT_
	Disable a startup script.

*reset-startup* {_SCRIPT_ | _--all_ }
	Reset a startup script to its default enabled/disabled state.  I.e.
	reverts any invocation of commands *enable-startup*/*disable-startup*.
	If _--all_ is provided, resets the enabled/disabled state of all
	startup scripts.

# STARTUP SCRIPTS

Startup scripts are scripts that can be run at startup, mostly for diagnostics
on information collection.  A startup can be in state _enabled_ or _disabled_,
only _enabled_ script are executed by command *run-startup-enabled*.  List of
startup enabled scripts is fixed, but a script's enabled/disabled state can be
changed by users using commands *enable-startup*/*disable-startup*.  Thus the
user can control which startup scripts are run by *run-startup-enabled*.
Command *reset-startup* can be used to reset a script to its default enabled
state.

## LIST OF STARTUP SCRIPTS

The *list* command list all available scripts, showing which of those are
startup scripts, and which of those are enabled.  It also shows which scripts'
enabled state have been changed by user, and differs from the default setup.

## EXECUTION OF STARTUP SCRIPTS

*run-startup-scripts* runs all the enabled startup scripts in parallel and
waits for all the scripts to terminate.  The order in which they are started is
no guaranteed, thus the scripts must no assume any order/dependency w.r.t to
other startup scripts.

## STARTUP SCRIPTS OUTPUT

*run-startup-scripts* creates a directory at _OUTDIR_/_<timestamp>_/, and
within this directory, one subdirectory is created for each script, named
after the script itself.  The scripts are executed with their CWD pointing to
their subdirectory and the stdout/stderr are redirected to file `output.log`
within that subdirectory.  For example, for a script named script1, the subdir
and output file would be _OUTDIR_/_<timestamp>_/script1/output.log.

The output dir is /var/oled/startup-scripts/ by default, but it can be changed
by passing `--outdir OUTDIR` to *run-startup-enabled* command.

# AUTHOR

Jose Lombera <jose.lombera@oracle.com>
