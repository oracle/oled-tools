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
 * Purpose: Display rds connection and its related qpn, cqn and irq lines
 * Prerequisites: Refer to the file rds_conn2irq_example.txt
 * Sample output: Refer to the file rds_conn2irq_example.txt
 */

/*
 * min_kernel 4.14.35-2042,5.4.17,5.15.0-200.103.1
 */

dtrace:::BEGIN
{
        printf("%Y print rds conn to irq mapping. Run rds-info -I parallelly to populate. ctrl+c to stop and print summary\n", walltimestamp);
}

fbt:rds_rdma:rds_ib_conn_info_visitor:entry
/ seen[(struct rds_connection *)arg0] != 1 /
{
	this->conn = (struct rds_connection *)arg0;
	this->ic = (struct rds_ib_connection *)this->conn->c_path[0].cp_transport_data;

	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	this->scq = this->ic->i_scq;
	this->m_ibcq = (struct mlx5_ib_cq *)this->scq;
	this->send_mcq = (struct mlx5_core_cq *)&this->m_ibcq->mcq;

	this->rcq = this->ic->i_rcq;
	this->m_ibcq = (struct mlx5_ib_cq *)this->rcq;
	this->recv_mcq = (struct mlx5_core_cq *)&this->m_ibcq->mcq;

	printf("[<%s,%s,%d>] conn=%p ic=%p qpn=%u dqpn=%u scqn=%u scqirq=%u rcqn=%u rcqirq=%u\n",
		inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, this->conn, this->ic,
		this->ic->i_qp_num, this->ic->i_dst_qp_num,
		this->send_mcq->cqn, this->send_mcq->irqn,
		this->recv_mcq->cqn, this->recv_mcq->irqn);

	@irq_dist[this->send_mcq->irqn] = count();
	@irq_dist[this->recv_mcq->irqn] = count();
	seen[this->conn] = 1;
}

dtrace:::END
{
	printf("Summary:\n");
	printf("%12s %8s\n", "irqn", "count");
	printa("%12d %@8d\n", @irq_dist);
}
