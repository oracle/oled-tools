#!/usr/sbin/dtrace -qs

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
 * Author(s): Rama Nichanamatlu, Arumugam Kolappan
 * Purpose: Track 'the time interrupt is disabled' due to spin_lock_irq*()
 * kernel functions. Takes an argument[holdtime in ms] and prints the process
 * (and its call stack) which disables interrupt more than the given time.
 * Argument: Time in ms (Minimum value is 2)
 *
 * Start Bug: 32080059
 * Prerequisites: Refer to the file spinlock_time_example.txt
 * Sample output: Refer to the file spinlock_time_example.txt
 */

#pragma D option cleanrate=50hz
#pragma D option dynvarsize=16000000
#pragma D option bufsize=16m

dtrace:::BEGIN
/ $1 < 2 /
{
	printf("Error: Argument should be >= 2 ms [%Y]\n", walltimestamp);
	exit(0);
}

dtrace:::BEGIN
{
	printf("Processes holding spinlock_irq longer than %d ms [%Y]\n", $1, walltimestamp);
	printf("----------------------------------------------------------------------\n");
	hold_time = $1 * 1000 * 1000;
}

/*
 * Use cpu number along with lock_pointer in the hash key (time[arg0, cpu]) to
 * handle the scenario: Second cpu acquires a lock before release_probe is fired
 * after first cpu unlocked it.
 */
lockstat:vmlinux:*spin_lock_irq*:spin-acquire
{
	time[arg0, cpu] = timestamp;
}

lockstat:vmlinux:*:spin-release
/ time[arg0, cpu] && (this->nsec = timestamp - time[arg0, cpu]) >= hold_time /
{
	printf("\n[%Y] time=%d ms lock=%p pid=%d cpu=%d cmd=%s\n",
 		walltimestamp, this->nsec/1000000,
		arg0, pid, cpu, curthread->comm);
	stack();
	time[arg0, cpu] = 0;

	/* Resetting the clause variable 'this->nsec' seems helping to avoid
	 * 'dynamic variable drops with non-empty dirty list'
	 * with Oracle Linux DTrace 1.0
	 */
	this->nsec = 0;
}

lockstat:vmlinux:*:spin-release
/ time[arg0, cpu] /
{
	time[arg0, cpu] = 0;
	this->nsec = 0;
}
