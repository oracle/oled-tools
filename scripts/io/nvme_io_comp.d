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
 * Purpose: to measure nvme IO latency in microseconds (us).
 * The script requires 1 argument:
 *    - the reporting period (in secs).
 * and a compilation flag "-D uek7" for uek7 run only.
 * The DTrace 'fbt' and 'profile' modules need to be loaded
 * ('modprobe -a fbt profile') for UEK5.
 * Sample output: Refer to the file nvme_io_comp_example.txt
 */

/*
 * min_kernel 4.14.35-2047.539.2,5.4.17-2136.315.5.8,5.15.0-100.52.2
 */

#pragma D option dynvarsize=100m
#pragma D option strsize=25

string ncmd[uchar_t];
string stat[ushort_t];

dtrace:::BEGIN
{
        printf("%d is reporting interval.\n", $1);

	ncmd[0x00] = "FLUSH";
	ncmd[0x01] = "WRITE";
	ncmd[0x02] = "READ";
	ncmd[0x04] = "WRITE_UNCOR";
	ncmd[0x05] = "COMPARE";
	ncmd[0x08] = "WRITE_ZEROS";
	ncmd[0x09] = "DSM";
	ncmd[0x0c] = "VERIFY";
	ncmd[0x0d] = "RESV_REG";
	ncmd[0x0e] = "RESV_RPT";
	ncmd[0x11] = "RESV_ACQ";
	ncmd[0x15] = "RESV_REL";

	stat[0x00] = "Success";
	stat[0x01] = "Invalid Opcode";
	stat[0x02] = "Invalid Field";
	stat[0x03] = "DID Conflict";
	stat[0x04] = "Data Xfer Error";
	stat[0x06] = "Internal Error";
	stat[0x07] = "Abort Request";
	stat[0x08] = "Abort Queue";
	stat[0x83] = "Resv Conflict";
	stat[0x280] = "Write Fault";
	stat[0x281] = "Read Error";
	stat[0x287] = "Unwritten Block";

	printf("Tracing... Hit Ctrl-C to end.\n");
}

::nvme_setup_cmd:entry
{
	this->req = (struct request *) arg1;
#ifdef uek7
        (this->req->rq_disk != NULL) ? cmnd[this->req] =
                (struct nvme_command *)((struct request *) arg1 + sizeof(struct request)) : 0;
#else
        (this->req->rq_disk != NULL) ? cmnd[this->req] = (struct nvme_command *)arg2 : 0;
#endif
        (this->req->rq_disk != NULL) ? nvme_req_starttime[this->req] = timestamp : 0;
}

::nvme_complete_rq:entry
/ this->start = (int64_t) (nvme_req_starttime[(struct request *) arg0]) / 
{
	this->comp_req = (struct request *) arg0;
	this->nvme_cmd = (struct nvme_command *) cmnd[this->comp_req];
	this->comp_diskname = stringof(this->comp_req->rq_disk->disk_name);
	this->opcode = (uint8_t) this->nvme_cmd->rw.opcode & 0xff;
	this->nvme_req = (struct nvme_request *) (this->comp_req + 1);
	this->nvme_req_stat = (uint16_t) (this->nvme_req->status & 0x7ff);
	this->command = ncmd[this->opcode] != NULL ?  ncmd[this->opcode]: "UNKNOWN";
	@opcount[this->comp_diskname, this->command,
		stat[this->nvme_req_stat]] = count();
	@lat_time[this->comp_diskname] = quantize((timestamp - this->start) / 1000);
	cmnd[this->comp_req] = NULL;
	nvme_req_starttime[this->comp_req] = 0;
}

tick-$1s, END 
{
	printf("\n\tSample Time : %-25Y\n", walltimestamp);
	printf("\t========================================================\n");
	printa("\t\t%s %@5u\t(us)\n", @lat_time);
	printf("\t========================================================\n");
	printf("\tDevice\t  Command\t  Status\t\tCount\n");
	printf("\t=======================================================\n");
	printa("\t%s     %-12s %-20s 	 %@d\n", @opcount);
	printf("\t=======================================================\n");
	clear(@opcount);
	clear(@lat_time);
}
