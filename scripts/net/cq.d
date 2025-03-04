#!/usr/sbin/dtrace -Cqs

/*
 * Copyright (c) 2024, Oracle and/or its affiliates.
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
 * Author(s): Manjunath Patil.
 * Purpose: This script tracks new connections and prints the Completion Queue
 * objects and the associated completion queue numbers.
 * Prerequisites: Refer to the file cq_example.txt
 * Sample output: Refer to the file cq_example.txt
 */

/*
 * min_kernel 5.4.17,5.15.0-200.103.1
 */

fbt:mlx5_ib:mlx5_ib_create_cq:entry
{
	self->ib_cq = (struct ib_cq *)arg0;
}

fbt:mlx5_ib:mlx5_ib_create_cq:return
/ self->ib_cq /
{
	this->ib_cq = self->ib_cq;
	this->m_cq = (struct mlx5_ib_cq *)this->ib_cq;
	this->m_core_cq = (struct mlx5_core_cq *)&this->m_cq->mcq;
	this->cqn = this->m_core_cq->cqn;
	this->irqn = this->m_core_cq->irqn;

	printf("%s:%d:%s ib_dev=%p ib_cq=%p m_core_cq=%p cqn=%d irqn=%d\n", probefunc, pid, execname,
		this->ib_cq->device, this->ib_cq, this->m_core_cq, this->cqn, this->irqn);
}

fbt:mlx5_ib:mlx5_ib_destroy_cq:entry
/
(this->ib_cq = (struct ib_cq *)arg0) &&
(this->m_cq = (struct mlx5_ib_cq *)this->ib_cq) &&
(this->m_core_cq = (struct mlx5_core_cq *)&this->m_cq->mcq) &&
(this->cqn = this->m_core_cq->cqn)
/
{
    this->irqn = this->m_core_cq->irqn;
    printf("%s:%d:%s ib_dev=%p ib_cq=%p m_core_cq=%p cqn=%d irqn=%d\n", probefunc, pid, execname,
            this->ib_cq->device, this->ib_cq, this->m_core_cq, this->cqn, this->irqn);
}
