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
 * Purpose: This script is used to trace and display the send and recv bytes
 * per connection
 * Prerequisites: Refer to the file rds_bcopy_metric_example.txt
 * Sample output: Refer to the file rds_bcopy_metric_example.txt
 */

#define DT_VERSION_NUMBER_(M, m, u) \
        ((((M) & 0xFF) << 24) | (((m) & 0xFFF) << 12) | ((u) & 0xFFF))

#if __SUNW_D_VERSION >= DT_VERSION_NUMBER_(2,0,0)
#pragma D option lockmem=unlimited
#endif

dtrace:::BEGIN
{
	printf("%Y %16s %10s \n", walltimestamp, "[<connection> op]", "MB/s");
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
	this->fip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@payloads[inet_ntoa(this->sip), inet_ntoa(this->fip), this->tos, "send"] = sum(self->payload_len);
	self->rs = 0;
	self->payload_len = 0;
}

fbt::rds_ib_inc_copy_to_user:entry
{
	self->inc = (struct rds_incoming *)arg0;
}

fbt::rds_ib_inc_copy_to_user:return
/ self->inc && arg1 > 0 /
{
	this->copied = (int)arg1;
	this->conn = (struct rds_connection *)self->inc->i_conn;
	this->sip = &this->conn->c_laddr.in6_u.u6_addr32[3];
	this->fip = &this->conn->c_faddr.in6_u.u6_addr32[3];
	this->tos = this->conn->c_tos;

	@payloads[inet_ntoa(this->sip), inet_ntoa(this->fip), this->tos, "recv"] = sum(this->copied);
	self->inc = 0;
}

tick-10s
{
	/* convert bytes/10 secs to MB/s */
	normalize(@payloads, 10*1024*1024);
	printa("[<%s,%s,%d> %s] %-@d\n", @payloads);
	clear(@payloads);
}
