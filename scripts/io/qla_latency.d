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
 *
 * Author(s): Rajan Shanmugavelu, Shminderjit Singh
 * Purpose: to measure QLogic adapter IO latency in milliseconds (ms).
 * The script requires 1 argument:
 *    - the reporting period (in secs) for latency
 * The DTrace 'fbt' and 'profile' modules need to be loaded
 * ('modprobe -a fbt profile') for UEK5.
 * Sample output: Refer to the file qla_latency_example.txt
 */

/*
 * min_kernel 4.14.35-2047.539.2,5.4.17-2136.315.5.8,5.15.0-100.52.2
 */

#pragma D option dynvarsize=100m
#pragma D option strsize=25

string opcode[ushort_t];

BEGIN
{
	printf("Running %s with %d seconds interval\n", $0, $1);
	opcode[0x00] = "TUR";
	opcode[0x08] = "Read(6)";
	opcode[0x0a] = "Write(6)";
	opcode[0x12] = "Inquiry";
	opcode[0x25] = "Read Cap(10)";
	opcode[0x28] = "Read(10)";
	opcode[0x2a] = "Write(10)";
	opcode[0x5e] = "Pers Resv In";
	opcode[0xa0] = "Report Luns";
	opcode[0xa3] = "Maint In";

	printf("Hit Ctrl-C to quit...\n");
}

fbt::qla2xxx_queuecommand:entry
{
	this->qla_cmd = (struct scsi_cmnd *)arg1;
	qla_starttime[this->qla_cmd] = timestamp;
}

fbt::qla2x00_sp_compl:entry
/ qla_starttime[(this->qla_comp_cmd = (struct scsi_cmnd *) ((srb_t *)arg0)->u.scmd.cmd)] /
{
	this->qla_opcode = this->qla_comp_cmd->cmnd[0];
	this->status = stringof((this->qla_comp_cmd->result == 0) ? "Success" : "Failed");
	this->qla_start = (int64_t) qla_starttime[this->qla_comp_cmd];
	this->qla_elapsed = (timestamp - this->qla_start) / 1000000;
	@cmd_count[opcode[this->qla_opcode], this->qla_opcode, this->status] = count();	
	@qla_lat[opcode[this->qla_opcode], this->status] = quantize(this->qla_elapsed);
	qla_starttime[this->qla_comp_cmd] = 0;
}

END,
tick-$1s
{
	printf("===============================================================================\n");
	printf("\n      Sample Time : %-25Y Latency at Qlogic Adapter layer\n", walltimestamp);
	printf("===============================================================================\n");
	printf("            Command      OpCode    Status       Count\n");
	printf("        ========================================================\n");
	printa("            %-10s    0x%-2x     %-10s  %@d\n", @cmd_count);
	printf("        ========================================================\n");
	printa("        %-36s %-5s %@d  \t(ms)\n        ", @qla_lat);
	printf("========================================================\n");
	clear(@qla_lat);
	clear(@cmd_count);
}
