#!/usr/sbin/dtrace -Cqs

/*
 * Copyright (c) 2023, Oracle and/or its affiliates.
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
 * Author: Arumugam Kolappan
 * Purpose: Print 'vhca id' of the mellanox devices (CX5) from a VM
 * The mellanox macro in UEK7 is different from UEK[5,6]. When running
 * the script in UEK7, add the '-D uek7' argument to the command.
 * Sample output: Refer to the file mlx_vhcaid_example.txt
 */

/*
 * min_kernel 4.14.35-2047.505.1,5.4.17-2136.315.5.8,5.15.0-200.103.1
 */

#define container_of(__ptr, __type, __member) ((__type *)((unsigned long long)__ptr - (unsigned long long)offsetof(__type, __member)))
#define __mlx5_nullp(typ) ((struct mlx5_ifc_##typ##_bits *)0)
#define __mlx5_bit_sz(typ, fld) sizeof(__mlx5_nullp(typ)->fld)
#define __mlx5_mask(typ, fld) ((u32)((1ull << __mlx5_bit_sz(typ, fld)) - 1))
#define __mlx5_bit_off(typ, fld) (offsetof(struct mlx5_ifc_##typ##_bits, fld))
#define __mlx5_dw_bit_off(typ, fld) (32 - __mlx5_bit_sz(typ, fld) - (__mlx5_bit_off(typ, fld) & 0x1f))
#define __mlx5_dw_off(typ, fld) (__mlx5_bit_off(typ, fld) / 32)
#define MLX5_GET(typ, p, fld) ((ntohl(*((__be32 *)(p) + __mlx5_dw_off(typ, fld))) >> __mlx5_dw_bit_off(typ, fld)) & __mlx5_mask(typ, fld))

#ifdef uek7
/* Macro for UEK7 kernel */
#define MLX5_CAP_GEN(mdev, cap) MLX5_GET(cmd_hca_cap, mdev->caps.hca, cap)
#else
/* Macro for UEK5 (4.14.35) and UEK6(5.4.17) kernels */
#define MLX5_CAP_GEN(mdev, cap) MLX5_GET(cmd_hca_cap, mdev->caps.hca_cur, cap)
#endif

dtrace:::BEGIN
{
	head = &(`rds_ib_devices);
	dev = head->next;
	total = 0;
}

tick-1sec
/ dev == head /
{
	printf("\n===============================\n");
	printf("Number of devices: %d\n", total);
	printf("===============================\n");
	exit(0);
}

tick-1sec
{
	total = total + 1;
	this->rds_ib_dev = container_of(dev, struct rds_ib_device, list);
	this->ibdev = (struct ib_device *)this->rds_ib_dev->dev;
	this->mibdev = (struct mlx5_ib_dev *)this->ibdev;
	this->mdev = (struct mlx5_core_dev *)this->mibdev->mdev;
	this->pcidev = (struct pci_dev *)this->mdev->pdev;
	this->dev = (struct device *)&this->pcidev->dev;
	this->kobj = (struct kobject *)&this->dev->kobj;

	this->addr = this->rds_ib_dev->ipaddr_list.next;
	this->ipaddr = container_of(this->addr, struct rds_ib_ipaddr, list);

	printf("%d) ip_addr=%s vhca_id=%d rds_dev=%p ib_dev=%p name=%s mdev=%p bdf=%s\n", total,
		inet_ntoa(&this->ipaddr->ipaddr.in6_u.u6_addr32[3]), MLX5_CAP_GEN(this->mdev, vhca_id),
		this->rds_ib_dev, this->ibdev, this->ibdev->name, this->mdev, stringof(this->kobj->name));
	dev = dev->next;
}
