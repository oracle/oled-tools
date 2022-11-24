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
 * Author: Manjunath Patil
 * Purpose: This script is used to trace and display rate of calls for
 * 	sendmsg, send_xmit, ib_xmit and send_cqe_handler per 10 sec.
 * Prerequisites: Refer to the file rds_tx_funccount_example.txt
 * Sample output: Refer to the file rds_tx_funccount_example.txt
 */

dtrace:::BEGIN
{
	printf("%Y: rate of calls for sendmsg, send_xmit, ib_xmit and send_cqe_handler. ctrl+c to stop\n", walltimestamp);
}

fbt::rds_sendmsg:entry
{
	this->socket = (struct socket *)arg0;
	this->sock = this->socket->sk;
	self->rs = (struct rds_sock *)this->sock;
	self->payload_len = (size_t)arg2;
}

fbt::rds_sendmsg:return
/ self->rs && self->payload_len == (size_t)arg1 /
{
	this->conn = (struct rds_connection *)self->rs->rs_conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@counts[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, "01send_msg"] = count();
	self->rs = 0;
	self->payload_len = 0;
}

/* in case rds_sendmsg() returns with error, clear memory */
fbt::rds_sendmsg:return
{
	self->rs = 0;
	self->payload_len = 0;
}

fbt:rds:rds_send_xmit:entry
{
	this->cpath = (struct rds_conn_path *)arg0;
	this->conn = (struct rds_connection *)this->cpath->cp_conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@counts[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, "02send_xmits"] = count();
}

fbt:rds_rdma:rds_ib_xmit:entry
{
	this->conn = (struct rds_connection *)arg0;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@counts[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, "03ib_xmits"] = count();
}

fbt:rds_rdma:rds_ib_send_cqe_handler:entry
{
	this->ic = (struct rds_ib_connection *)arg0;
	this->conn = (struct rds_connection *)this->ic->conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->dip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@counts[inet_ntoa(this->sip), inet_ntoa(this->dip), this->tos, "04send_cqe_handler"] = count();
}

END,tick-10s
{
	printf("--- %Y ---\n", walltimestamp);
	printa("[<%s,%s,%d> %s] %-@d\n", @counts);
	clear(@counts);
}
