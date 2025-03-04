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
 * Author: Praveen Kumar Kannoju
 * Purpose: This script provides latency details of the icmp packet, during
 * its journey in the kernel. Output shows the time delta between two
 * consequtive kernel functions through which the packet travels.
 * Prerequisites: Refer to the file ping_lat_example.txt
 * Sample output: Refer to the file ping_lat_example.txt
*/

/*
 * min_kernel 4.14.35-2042,5.4.17,5.15.0-200.103.1
 */

#define ICMP_PROTOCOL   1
#define ICMP_ECHO       8
#define ICMP_ECHO_REPLY 0

fbt:vmlinux:ip_send_skb:entry,
fbt:ib_ipoib:ipoib_cm_send:entry
/
(this->nh = ((struct sk_buff *)arg1)->head + ((struct sk_buff *)arg1)->network_header) &&
(this->iphdr = (struct iphdr *)this->nh) &&
(this->iphdr->protocol == ICMP_PROTOCOL) &&
(this->th = ((struct sk_buff *)arg1)->head + ((struct sk_buff *)arg1)->transport_header) &&
(this->icmphdr = (struct icmphdr *)this->th) &&
(this->icmphdr->type == ICMP_ECHO)
/
{
	this->sip = inet_ntoa((__be32 *)(&this->iphdr->saddr));
	this->dip = inet_ntoa((__be32 *)(&this->iphdr->daddr));
	this->icmp_seq = ntohs(this->icmphdr->un.echo.sequence);
	this->icmp_id = ntohs(this->icmphdr->un.echo.id);

	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, probefunc] = timestamp;
}

/*
 * Same as above, but for functions with sk_buff in arg0 rather than arg1.
 */
fbt:vmlinux:dev_hard_start_xmit:entry,
fbt:ib_ipoib:ipoib_start_xmit:entry
/
(this->nh = ((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->network_header) &&
(this->iphdr = (struct iphdr *)this->nh) &&
(this->iphdr->protocol == ICMP_PROTOCOL) &&
(this->th = ((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->transport_header) &&
(this->icmphdr = (struct icmphdr *)this->th) &&
(this->icmphdr->type == ICMP_ECHO)
/
{
	this->sip = inet_ntoa((__be32 *)(&this->iphdr->saddr));
	this->dip = inet_ntoa((__be32 *)(&this->iphdr->daddr));
	this->icmp_seq = ntohs(this->icmphdr->un.echo.sequence);
	this->icmp_id = ntohs(this->icmphdr->un.echo.id);

	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, probefunc] = timestamp;
}

fbt:vmlinux:icmp_rcv:entry
/
(this->nh = ((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->network_header) &&
(this->iphdr = (struct iphdr *)this->nh) &&
(this->iphdr->protocol == ICMP_PROTOCOL) &&
(this->th = ((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->transport_header) &&
(this->icmphdr = (struct icmphdr *)this->th) &&
(this->icmphdr->type == ICMP_ECHO_REPLY)
/
{
	this->sip = inet_ntoa((__be32 *)(&this->iphdr->daddr));
	this->dip = inet_ntoa((__be32 *)(&this->iphdr->saddr));
	this->icmp_seq = ntohs(this->icmphdr->un.echo.sequence);
	this->icmp_id = ntohs(this->icmphdr->un.echo.id);

	this->ip_send_skb_time = timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ip_send_skb"];
	this->dev_hard_start_xmit_time = timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "dev_hard_start_xmit"];
	this->ipoib_start_xmit_time = timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ipoib_start_xmit"];
	this->ipoib_cm_send_time = timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ipoib_cm_send"];
	this->icmp_rcv_time = timestamp;

	printf("icmp id: %d, icmp sequence: %d, src ip: %s, dst ip: %s\n", this->icmp_id, this->icmp_seq, this->sip, this->dip);
	printf("\nroutine			        delta(ns)\n\n");
	printf("ip_send_skb\n");
	printf("dev_hard_start_xmit 	%15d\n", this->dev_hard_start_xmit_time - this->ip_send_skb_time);
	printf("ipoib_start_xmit 	%15d\n", this->ipoib_start_xmit_time > 0 ? this->ipoib_start_xmit_time - this->dev_hard_start_xmit_time : 0);
	printf("ipoib_cm_send 		%15d\n", this->ipoib_cm_send_time > 0 ? this->ipoib_cm_send_time - this->ipoib_start_xmit_time : 0);
	printf("icmp_rcv 		%15d\n\n", this->icmp_rcv_time - this->ipoib_cm_send_time);
	printf("request-reply latency: 	%15d\n", this->icmp_rcv_time - this->ip_send_skb_time);
	printf("------------------------------------------------------------------\n");

	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ip_send_skb"] = 0;
	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "dev_hard_start_xmit"] = 0;
	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ipoib_start_xmit"] = 0;
	timestamp_info[this->sip, this->dip, this->icmp_seq, this->icmp_id, "ipoib_cm_send"] = 0;

}

