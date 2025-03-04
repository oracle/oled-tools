#!/usr/sbin/dtrace -Cqs

/*
 * Copyright (c) 2024, Oracle and/or its affiliates.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, see <https://www.gnu.org/licenses/>.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 * Author(s): Rajan Shanmugavelu, Shminderjit Singh
 * Purpose: to measure SCSI IO queue time in milliseconds (ms).
 *
 * The DTrace 'fbt' and 'profile' modules need to be loaded
 * ('modprobe -a fbt profile') for UEK5.
 * Sample output: Refer to the file scsi_queue_example.txt
 */

/*
 * min_kernel 4.14.35-2047.539.2,5.4.17-2136.315.5.8,5.15.0-200.103.1
 */

#pragma D option dynvarsize=100m
#pragma D option strsize=25

dtrace:::BEGIN
{
	printf("Tracing... Hit Ctrl-C to end.\n");
}

fbt::__scsi_queue_insert:entry
{
	start_time[arg0] = timestamp;
}

::scsi_dispatch_cmd_start
/this->start = start_time[arg0]/
{
	this->elapsed = (timestamp - this->start) / 1000;
	this->cmd = (struct scsi_cmnd *) arg0;
	this->scsi_device = (struct scsi_device *)this->cmd->device;
	this->device = (struct device) this->scsi_device->sdev_gendev;
#ifdef uek7
	this->rq = (struct request *)((unsigned long long)this->cmd - sizeof(struct request));
	this->sd = stringof(this->rq->rq_disk->disk_name);
#else
        this->sd = stringof(this->cmd->request->rq_disk->disk_name);
#endif
	this->dev_name = stringof(this->device.kobj.name);

	@avg[this->sd, this->dev_name] = avg(this->elapsed/1000);
	@plot[this->sd, this->dev_name] = lquantize(this->elapsed / 1000, 0, 1000, 100);

	start_time[arg0] = 0;
}

dtrace:::END
{
	printf("%Y", walltimestamp);
	printf("Wait queue time by disk (ms):\n");
	printa("\n %-12s %-20s\n%@d", @plot);
	printf("\n\n %-12s %-20s %12s\n", "DEVICE", "NAME", "AVG_WAIT(us)");
	printa("  %-12s %-20s %@12d\n", @avg);
}

