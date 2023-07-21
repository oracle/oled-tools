# oled-tools

Oracle Linux Enchanced Diagnostic (OLED) tools is a collection of tools,
script, configs, etc. that collect and analyze data about the health of the
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

## Using oled-tools

### Dependencies

#### Build dependencies

- `make`
- `python3`
- `zlib-devel`
- `bzip2-devel`
- `elfutils-devel`

#### Runtime dependencies

- `python3`
- `zlib`
- `bzip2-libs`
- `elfutils-libs`

### Local build and install

```bash
$ git clone https://github.com/oracle/oled-tools.git
$ cd oled-tools
$ make install
```

`lkce` requires additional setup the firs time, it's recommended to run
following command after oled-tools is installed:

```bash
$ [ -f /etc/oled/lkce/lkce.conf ] || sudo oled lkce configure --default
```

### Usage examples

```bash
$ oled -h
usage: oled.py {-h | -v | COMMAND [ARGS]}

Valid commands:
     lkce        -- Linux Kernel Core Extractor
     memstate    -- Capture and analyze memory usage statistics
     filecache   -- List the biggest files in page cache
     dentrycache -- List a sample of active dentries
     kstack      -- Gather kernel stack based on the process status or PID
     syswatch    -- Execute user-provided commands based on the CPU utilization

optional arguments:
  -h, --help     show this help message and exit
  -v, --version  show program's version number and exit

NOTE: Must run as roo

$ oled memstate -h
usage: oled memstate [-h] [-p] [-w] [-s [FILE]] [-n [FILE]] [-a] [-v]
                     [-f [INTERVAL]]

memstate: Capture and analyze memory usage data on this system.

optional arguments:
  -h, --help            show this help message and exit
  -p, --pss             display per-process memory usage
  -w, --swap            display per-process swap usage
  -s [FILE], --slab [FILE]
                        analyze/display slab usage
  -n [FILE], --numa [FILE]
                        analyze/display NUMA stats
  -a, --all             display all data
  -v, --verbose         verbose data capture; combine with other options
  -f [INTERVAL], --frequency [INTERVAL]
                        interval at which data should be collected (default:
                        30s)

$ oled dentrycache -h
dentrycache: List a sample of file paths which have active dentries, on this system.
Usage: oled dentrycache [-l] [-n] [-k] [-h] [-v]
Options:
   -l, --limit <number>       list at most <number> dentries, 10000 by default
   -n, --negative             list negative dentries only, disabled by default
   -k, --kexec                list dentries for crashed production kernel
   -h, --help                 show this message
   -v, --version              show version

Note: Works on Oracle UEK4/UEK5/UEK6 kernels only. Check the man page for more information.
```

## Contributing

Your contributions are welcome! You will need to sign the Oracle Contributor
Agreement before starting a pull request; see [CONTRIBUTING](CONTRIBUTING.md)
for more information.

## License

oled-tools is licensed under [GPLv2](LICENSE.txt).
