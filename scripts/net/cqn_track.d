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
 * Purpose: This script tracks a Completion Queue number and prints when there
 * is a new completion by tracking completion handler, tasklet, arming and
 * poll_cq calls.
 * Prerequisites: Refer to the file cqn_track_example.txt
 * Sample output: Refer to the file cqn_track_example.txt
 */

/*
 * min_kernel 4.14.35-2047.505.1,5.4.17-2136.315.5.8,5.15.0-200.103.1
 */

#pragma D option cleanrate=50hz
#pragma D option dynvarsize=16000000
#pragma D option bufsize=16m

#define container_of(__ptr, __type, __member) ((__type *)((unsigned long long)__ptr - (unsigned long long)offsetof(__type, __member)))

uint64_t cq_add_to_tasklet_timestamp[struct mlx5_core_cq *];
uint64_t comp_timestamp[struct ib_cq *];
uint64_t arm_timestamp[struct ib_cq *];
uint64_t poll_timestamp[struct ib_cq *];

fbt:mlx5_core:mlx5_add_cq_to_tasklet:entry
/
(this->m_core_cq = (struct mlx5_core_cq *)arg0) &&
(this->cqn = this->m_core_cq->cqn) &&
(
(this->cqn == $1)
)
/
{
	this->irqn = this->m_core_cq->irqn;
	this->cq_add_to_tasklet_lat = cq_add_to_tasklet_timestamp[this->m_core_cq] > 0 ? ((timestamp - cq_add_to_tasklet_timestamp[this->m_core_cq])/1000) : 0;
	cq_add_to_tasklet_timestamp[this->m_core_cq] = timestamp;

	printf("%Y:%lu:%s: m_core_cq=%p cqn=%d irqn=%d last_call(usecs ago):%lu\n",
		walltimestamp, timestamp, probefunc, this->m_core_cq, this->cqn,
		this->irqn, this->cq_add_to_tasklet_lat);
}

fbt:ib_uverbs:ib_uverbs_comp_handler:entry
/
(this->ib_cq = (struct ib_cq *)arg0) &&
(this->m_cq = (struct mlx5_ib_cq *)this->ib_cq) &&
(this->m_core_cq = (struct mlx5_core_cq *)&this->m_cq->mcq) &&
(this->cqn = this->m_core_cq->cqn) &&
(
(this->cqn == $1)
)
/
{
	this->irqn = this->m_core_cq->irqn;

        this->uobj = (struct ib_ucq_object *)this->ib_cq->uobject;
	#ifdef uek5
	this->uobject = (struct ib_uobject *)&this->uobj->uobject;
	#else
        this->uobject = (struct ib_uobject *)&this->uobj->uevent.uobject;
	#endif
        this->fd = this->uobject->id;

	this->comp_lat = comp_timestamp[this->ib_cq] > 0 ? ((timestamp - comp_timestamp[this->ib_cq])/1000) : 0;
	comp_timestamp[this->ib_cq] = timestamp;

        printf("%Y:%lu:%s: ibdev=%p m_core_cq=%p ib_cq=%p cqn=%d irqn=%d channel_fd=%d last_call(usecs ago)=%lu\n",
                walltimestamp, timestamp, probefunc, this->ib_cq->device, this->m_core_cq, this->ib_cq,
		this->cqn, this->irqn, this->fd, this->comp_lat);
}

fbt:mlx5_ib:mlx5_ib_poll_cq:entry
/
(this->ib_cq = (struct ib_cq *)arg0) &&
(this->m_cq = (struct mlx5_ib_cq *)this->ib_cq) &&
(this->m_core_cq = (struct mlx5_core_cq *)&this->m_cq->mcq) &&
(this->cqn = this->m_core_cq->cqn) &&
(
(this->cqn == $1)
)
/
{
	this->irqn = this->m_core_cq->irqn;
	this->poll_lat = poll_timestamp[this->ib_cq] > 0 ? ((timestamp - poll_timestamp[this->ib_cq])/1000) : 0;
	poll_timestamp[this->ib_cq] = timestamp;

        printf("%Y:%lu:%s: ib_dev=%p m_core_cq=%p ib_cq=%p cqn=%d irqn=%d last_call(usecs ago)=%lu\n",
                walltimestamp, timestamp, probefunc, this->ib_cq->device, this->m_core_cq, this->ib_cq,
		this->cqn, this->irqn, this->poll_lat);
}

fbt:mlx5_ib:mlx5_ib_arm_cq:entry
/
(this->ib_cq = (struct ib_cq *)arg0) &&
(this->m_cq = (struct mlx5_ib_cq *)this->ib_cq) &&
(this->m_core_cq = (struct mlx5_core_cq *)&this->m_cq->mcq) &&
(this->cqn = this->m_core_cq->cqn) &&
(
(this->cqn == $1)
)
/
{
	this->irqn = this->m_core_cq->irqn;
	this->arm_lat = arm_timestamp[this->ib_cq] > 0 ? ((timestamp - arm_timestamp[this->ib_cq])/1000) : 0;
	arm_timestamp[this->ib_cq] = timestamp;

        printf("%Y:%lu:%s: ibdev=%p m_core_cq=%p ib_cq=%p cqn=%d irqn=%d last_call(usecs ago)=%lu\n",
                walltimestamp, timestamp, probefunc, this->ib_cq->device, this->m_core_cq, this->ib_cq,
		this->cqn, this->irqn, this->arm_lat);
}
