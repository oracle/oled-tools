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
 * Purpose: The script shows the rdma transfer rate[MB/s] for each rds
 * 	connection. It also shows histograms of inverse bandwidth in nsecs/KB
 * 	[Avg number of nsecs to transfer 1KB of data].
 * Prerequisites: Refer to the file rds_rdma_xfer_rate_example.txt
 * Sample output: Refer to the file rds_rdma_xfer_rate_example.txt
 */

#define container_of(__ptr, __type, __member) ((__type *)((unsigned long long)__ptr - (unsigned long long)offsetof(__type, __member)))

dtrace:::BEGIN
{
	printf("%Y\n%-40s %-10s\n", walltimestamp, "[<connection> op]", "MB/s");
}

fbt:rds:rds_cmsg_rdma_args:entry
{
	this->rm = (struct rds_message *)arg1;
	this->op = (struct rm_rdma_op *)&this->rm->rdma;
	track_rdma[this->op] = timestamp;
}

/* rds_ib_send_unmap_rdma.isra.9 */
fbt::rds_ib_send_unmap_rdma*:entry
/ (this->op = (struct rm_rdma_op *)arg1) && track_rdma[this->op] /
{
	this->rdma_op = this->op->op_write ? 'w' : 'r';
	this->rm = (struct rds_message *) container_of(this->op, struct rds_message, rdma);
	this->cpath = (struct rds_conn_path *)this->rm->m_conn_path;
	this->conn = (struct rds_connection *)this->cpath->cp_conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@rdma_xfers[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, this->rdma_op] = sum(this->op->op_bytes);

	/* Histogram of inverse bandwidth */
	this->quanta = (timestamp - track_rdma[this->op])/(this->op->op_bytes/1024);
	@rdma_hist[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, this->rdma_op] = quantize(this->quanta);
	track_rdma[this->op] = 0;
}

tick-10s
{
	printf("%Y\n", walltimestamp);

	/* convert bytes/10 secs to MB/s */
	normalize(@rdma_xfers, 1024*1024*10);
	printa("[<%s,%s,%d> %c] %-@d\n", @rdma_xfers);
	clear(@rdma_xfers);
}

/* Histograming inverse bandwidth. Printed when dtrace is ended */
dtrace:::END
{
	printf("%Y - Histogram of inverse bandwidth reported in nsecs/KB\n", walltimestamp);
	printa("[<%s,%s,%d> %c] \n%@d\n", @rdma_hist);
}
