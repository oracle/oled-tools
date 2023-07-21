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
 * Purpose: This script detects and prints the details of all ARP requests,
 * replies, and ignored packets on all the network interfaces present on the
 * system where this script is being executed.
 * Prerequisites: Refer to the file arp_origin_example.txt
 * Sample output: Refer to the file arp_origin_example.txt
 */

#define DT_VERSION_NUMBER_(M, m, u) \
        ((((M) & 0xFF) << 24) | (((m) & 0xFFF) << 12) | ((u) & 0xFFF))

#if __SUNW_D_VERSION >= DT_VERSION_NUMBER_(2,0,0)
#pragma D option lockmem=unlimited
#endif

#define arp_protocol 0x0806
#define arp_request_opcode 1
#define arp_reply_opcode 2

fbt:vmlinux:__dev_queue_xmit:entry
/ 
ntohs(((struct sk_buff *)arg0)->protocol) == arp_protocol  && 
(this->arphdr = ((struct arphdr *)(((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->network_header))) &&
ntohs(this->arphdr->ar_op) == arp_request_opcode
/
{
        this->dev = ((struct net_device *) ((struct sk_buff *)arg0)->dev);
	this->ip_var = (u64)this->arphdr + sizeof(struct arphr) + this->arphdr->ar_hln + sizeof(this->arphdr->ar_op);
        this->sip = (ipaddr_t *)this->ip_var;
        this->tip = (ipaddr_t *)(this->ip_var + this->arphdr->ar_pln + this->arphdr->ar_hln);

        printf("%Y %s Send arp request: sip: %s, tip: %s\n",
		walltimestamp, this->dev->name, inet_ntoa(this->sip),
		inet_ntoa(this->tip));
}

fbt:vmlinux:arp_rcv:entry
/
(this->arphdr = ((struct arphdr *)(((struct sk_buff *)arg0)->head + ((struct sk_buff *)arg0)->network_header))) &&
ntohs(this->arphdr->ar_op) == arp_reply_opcode
/
{
        this->dev = ((struct net_device *) ((struct sk_buff *)arg0)->dev);
        this->ip_var = (u64)this->arphdr + sizeof(struct arphr) + this->arphdr->ar_hln + sizeof(this->arphdr->ar_op);
        this->sip = (ipaddr_t *)this->ip_var;
        this->tip = (ipaddr_t *)(this->ip_var + this->arphdr->ar_pln + this->arphdr->ar_hln);

        printf("%Y %s Recv arp reply sip: %s tip: %s\n",
		walltimestamp, this->dev->name, inet_ntoa(this->sip),
		inet_ntoa(this->tip));
}

fbt:vmlinux:arp_send:entry,
fbt:vmlinux:arp_create:entry
/ arg0 == arp_request_opcode || arg0 == arp_reply_opcode /
{
	this->tip = (__be32 *)alloca(4);
 	*(this->tip) = arg2;
 	this->sip = (__be32 *)alloca(4);
        *(this->sip) = arg4;

        printf("%Y %s Send arp %s sip: %s, tip: %s\n", 
		walltimestamp, ((struct net_device *)arg3)->name,
		arg0 == 1 ? "request":arg0 == 2 ? "reply":"unknown", 
		inet_ntoa((ipaddr_t *)this->sip),
		inet_ntoa((ipaddr_t *)this->tip));

}

fbt:vmlinux:arp_ignore:entry
{
        self->dev = (struct net_device*)((struct in_device *)arg0)->dev;
        self->sip = arg1;
        self->tip = arg2;
}

fbt:vmlinux:arp_ignore:return
/ self->dev != NULL /
{
        this->tip = (__be32 *)alloca(4);
        *(this->tip) = self->tip;
        this->sip = (__be32 *)alloca(4);
        *(this->sip) = self->sip;

	printf("%Y %s Recv arp request %s sip: %s, tip: %s\n",
		walltimestamp, self->dev->name,
		arg1 == 1 ? "(Ignoring) " : "",
		inet_ntoa((ipaddr_t *)this->sip),
		inet_ntoa((ipaddr_t *)this->tip));

	self->sip = 0;
	self->tip = 0;
	self->dev = 0;
}
