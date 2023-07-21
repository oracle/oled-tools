#!/usr/sbin/dtrace -Cqs

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
 * Author(s): Rama Nichanamatlu, Praveen Kumar Kannoju.
 * Purpose: This script monitors the completions on all the connections and
 * calculates the time difference between the consecutive completions. If
 * this time difference exceeds the threshold time (in microseconds), given
 * as input argument, message will be printed providing the details of the
 * connection parameters. Minimum input value is 500 microseconds.
 * Prerequisites: Refer to the file rds_check_tx_stall_example.txt
 * Sample output: Refer to the file rds_check_tx_stall_example.txt
*/

#define DT_VERSION_NUMBER_(M, m, u) \
        ((((M) & 0xFF) << 24) | (((m) & 0xFFF) << 12) | ((u) & 0xFFF))

#if __SUNW_D_VERSION >= DT_VERSION_NUMBER_(2,0,0)
#pragma D option lockmem=unlimited
#endif

uint64_t rds_connection[struct rds_connection *];

dtrace:::BEGIN
/ $1 < 500 /
{
        printf("\n Error: Argument should be >= 500 ( microseconds) [%Y]\n\n", walltimestamp);
        printf("-----------------------------------------------------------------------------\n");
        exit(0);
}

dtrace:::BEGIN
{
        time_to_wait = $1;
        printf("%Y Print each time there is a gap that exceeds the threshold, ctrl+c to stop.\n", walltimestamp);
}

fbt:rds_rdma:rds_ib_send_cqe_handler:entry
{
        this->ic = (struct rds_ib_connection *)arg0;
        this->conn = (struct rds_connection *)this->ic->conn;

	self->latency = rds_connection[this->conn] > 0 ? (timestamp - rds_connection[this->conn])/1000 : 0;
        rds_connection[this->conn] = timestamp;
}

fbt:rds_rdma:rds_ib_send_cqe_handler:entry
/ self->latency > time_to_wait /
{
        this->ic = (struct rds_ib_connection *)arg0;
        this->conn = (struct rds_connection *)this->ic->conn;
        this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
        this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
        this->tos = this->conn->c_tos;

        printf("[%s,%s,%d] last send completion: %d usecs ago\n",
		inet_ntoa(this->sip), inet_ntoa(this->dip),
		this->tos, self->latency);
}
