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
 * Author: Manjunath Patil
 * Purpose: Trace and display the rdma read/write in bytes and the
 * 	corresponding latency (in usec) per connection as in the below format
 * Prerequisites: Refer to rds_rdma_lat_example.txt 
 * Sample output: Refer to rds_rdma_lat_example.txt
 */

#define DT_VERSION_NUMBER_(M, m, u) \
        ((((M) & 0xFF) << 24) | (((m) & 0xFFF) << 12) | ((u) & 0xFFF))

#if __SUNW_D_VERSION >= DT_VERSION_NUMBER_(2,0,0)
#pragma D option lockmem=unlimited
#endif

#define container_of(__ptr, __type, __member) ((__type *)((unsigned long long)__ptr - (unsigned long long)offsetof(__type, __member)))

dtrace:::BEGIN
{
	printf("%Y %16s %-16s %s\n", walltimestamp, "[<connection> op]",
		"Bytes", "Latency(in usecs)"); }

fbt:rds:rds_cmsg_rdma_args:entry
{
	this->rm = (struct rds_message *)arg1;
	this->op = (struct rm_rdma_op *)&this->rm->rdma;
	track_rdma[this->op] = timestamp;
}

fbt::rds_ib_send_unmap_rdma*:entry
/ track_rdma[(struct rm_rdma_op *) arg1] /
{
	this->op = (struct rm_rdma_op *)arg1;
	this->rm = (struct rds_message *)container_of(this->op, struct rds_message, rdma);

	this->cpath = (struct rds_conn_path *)this->rm->m_conn_path;
	this->conn = (struct rds_connection *)this->cpath->cp_conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->fip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	printf("[<%s,%s,%d> %c] bytes=%d lat(usec)=%u \n",
		inet_ntoa(this->sip), inet_ntoa(this->fip), this->tos,
		this->op->op_write ? 'w' : 'r', this->op->op_bytes,
		(timestamp-track_rdma[this->op])/1000);
	track_rdma[this->op] = 0;
}
