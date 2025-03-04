additional-scripts(8)

# NAME

additional-scripts - Additional diagnostic scripts that are provided with Oracle Linux Enhanced Diagnostics (OLED).

# DESCRIPTION

The additional diagnostic scripts provided in the oled-tools package extend the functionality provided in OLED.  Run these scripts by using the *oled scripts* command.  For detailed information about each script see the documentation in the /usr/libexec/oled-tools/scripts.d/docs/ directory.

# CORE

- spinlock_time.d - Tracks the time interrupt is disabled because of spin_lock_irq\*() kernel functions.  Takes an argument [holdtime in ms] and prints the process (and its call stack) which disables interrupt more than the specified time.
	- ARGUMENTS: Time in ms (Minimum value is 2)

# NET

- arp_origin.d - Detects and prints the details of all ARP requests, replies, and ignored packets on all the network interfaces present on the system where the script is run.


- cm_destroy_id.d - Probes for _cm_destroy_id_wait_timeout()_ and prints the cm_id, qp_num and pci_id of the associated QP.  The script exits after 5 probes.  This script also calls mlxqpdump.sh.


- cq.d -  Tracks new RDS connections and prints the Completion Queue objects and the associated completion queue numbers.


- cqn_track.d - Tracks an RDS Completion Queue number and prints when there is a new completion by tracking completion handler, tasklet, arming, and poll_cq calls.


- mlx_vhcaid.d - Prints 'vhca id' of the Mellanox devices (CX5) from a VM.


- mlxqpdump.sh - Used by cm_destroy_id.d to collect firmware resource dumps and sysinfo-snapshots for Mellanox devices.


- ping_lat.d - Provides latency details of and ICMP packet as it's processed in the kernel. The output shows the time delta between two consecutive kernel functions through which the packet travels.


- rds_bcopy_metric.d - Traces and displays the send/recv bytes per connection.


- rds_check_tx_stall.d - Monitors the completions on all connections and calculates the time difference between consecutive completions. If the time difference exceeds the threshold time (in microseconds), given as input argument, a message is printed to prove the details of the connection parameters.
	- ARGUMENTS: Threshold time in microseconds.  (Minimum: 500)


- rds_conn2irq.d - Displays an RDS connection and its related qpn, cqn and ird lines.


- rds_egress_TP.d - Tracks the RDS egress drop reason along with pid and comm.


- rds_rdma_lat.d - Traces and displays the RDMA read/write in bytes and the corresponding latency (in usec) per connection.


- rds_rdma_xfer_rate.d - Shows the RDMA transfer rate [MB/s] for each RDS connection.  It also shows histograms of inverse bandwidth in nsecs/KB [Avg number of nsecs to transfer 1KB of data].


- rds_tx_funccount.d - Traces and displays the rate of calls for send_xmit, ib_xmit and send_cqe_handler per 10 sec.

# IO

- nvme_io_comp.d - Measures nvme IO latency in microseconds (us).
	- ARGUMENTS: The sampling interval (in secs) for latency, the reporting period (in secs) for command completion.

- scsi_latency.d - Measures SCSI Mid Layer IO latency in milliseconds (ms) and measures QLogic adapter layer IO latency in milliseconds(ms).

- scsi_queue.d - Measures SCSI IO queue time in milliseconds (ms).

# SEE ALSO

oled(8),
oled-scripts(8)
