# oled-tools

Oracle Linux Enchanced Diagnostic (OLED) tools is a collection of tools,
scripts, configs, etc. that collect and analyze data about the health of the
system in order to root cause and resolve any system issues.

## Components

The oled-tools repo includes the following debug tools/scripts that aid in
gathering additional debug data from the system. Please review the
corresponding man pages for more details.

- lkce: Extracts data from a vmcore or from within the kdump kernel after a
  crash
- memstate: Captures and analyzes various memory usage statistics on the
  running system
- filecache: List the paths of the biggest files present in the page cache
- dentrycache: Lists a sample of file paths which have active dentries in the
  dentry hash table
- kstack: Collects the kernel stack trace for selected processes, based on
  status or PID
- syswatch: Execute user-provided commands when CPU utilization reaches a
  threshold

## Installation

### Build dependencies

- `make`
- `python3`
- `zlib-devel`
- `bzip2-devel`
- `elfutils-devel`

### Runtime dependencies

- `python3`
- `zlib`
- `bzip2-libs`
- `elfutils-libs`

### Build and install

```bash
$ git clone https://github.com/oracle/oled-tools.git
$ cd oled-tools
$ make
$ make install
```

`lkce` requires additional setup the firs time, it's recommended to run
following command after oled-tools is installed:

```bash
$ [ -f /etc/oled/lkce/lkce.conf ] || sudo oled lkce configure --default
```

## Usage

`oled-tools` must be run as root.  After it has been installed, it can be run
with

```
sudo oled <subcommand> [args]
```

Run `sudo oled --help` to see a list of subcommands supported and other options
and `sudo oled <subcommand> {-h | --help}` to see help of specific commands.
You can also consult man pages `oled(8)` and `oled-<subcommand>(8)`.

## Contributing

This project welcomes contributions from the community. Before submitting a
pull request, please [review our contribution guide](./CONTRIBUTING.md)

## Security

Please consult the [security guide](./SECURITY.md) for our responsible security
vulnerability disclosure process

## License

oled-tools is licensed under [GPLv2](LICENSE.txt).
