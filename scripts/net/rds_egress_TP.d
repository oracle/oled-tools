#!/usr/sbin/dtrace -qs

/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
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
 */

/*
 * Author(s): Manjunath Patil
 * Purpose: track egress drop reason along with pid and comm.
 * Prerequisites: Refer to the file rds_egress_TP_example.txt
 * Sample output: Refer to the file rds_egress_TP_example.txt
 */

dtrace:::BEGIN
{
	printf("%Y ctrl+c to stop\n", walltimestamp);
}

/*
 *	trace_rds_drop_egress(rm, rs, conn, cpath,
 *						conn ? &conn->c_laddr : NULL,
 *							conn ? &conn->c_faddr : NULL,
 *							reason);
 */
fbt:rds:trace_event_raw_event_rds_drop_egress:entry
/ arg3 != NULL /
{
	this->conn = (struct rds_connection *)arg3;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;
	this->reason = stringof(arg7);

	printf("%Y pid=%d comm=%s [<%s,%s,%d>] reason=%s\n",
			walltimestamp, pid, execname,
			inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos,
			this->reason);
}
