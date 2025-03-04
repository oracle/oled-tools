#!/bin/bash

########
#  Author(s): Arumugam Kolappan, Anand Khoje
#
#  Description:
#   This script will execute the commands to gather FW resource dumps
#   and sysinfo-snapshot for mellanox devices.
# 
#  Usage:
#    This is a helper script for dtrace cm_destroy_id.d
# 
######


WQDUMP_DIR="/root/mlx_logs/"
LOCKFILE="/root/mlx_logs/`basename $0`.lock"

dev=${1}
qpnum=${2}
iter=${3}
cm_id=${4}
date=`date +%Y%m%d_%H%M%S`

if [[ ! -e ${WQDUMP_DIR} ]]; then
	logger "First create /root/mlx_logs/ folder !!!"
	exit
fi

logger "${date}: Start mlxqpdump: QP: ${qpnum} dev: ${dev} [iter_cnt: ${iter} cm_id: ${cm_id}]"

### resourcedump
(
	flock -n 9 && (
		/usr/bin/resourcedump dump -d ${dev} --segment 0x1000 --index1 ${qpnum}  > ${WQDUMP_DIR}/resdump_${dev}_${qpnum}_${date} > /dev/null
	)
) 9> ${LOCKFILE}.resourcedump &

### mstdump
(
	flock -n 11 && (
		/usr/bin/mstdump ${dev} > ${WQDUMP_DIR}/mstdump_${dev}_${date}_1.out
		/usr/bin/mstdump ${dev} > ${WQDUMP_DIR}/mstdump_${dev}_${date}_2.out
		/usr/bin/mstdump ${dev} > ${WQDUMP_DIR}/mstdump_${dev}_${date}_3.out
	)
) 11> ${LOCKFILE}.mstdump &

### sysinfosnapshot
(
	flock -n 13 && (
		${WQDUMP_DIR}/sysinfo-snapshot.py -d ${WQDUMP_DIR} > /dev/null
		ls |wc
	)
) 13> ${LOCKFILE}.sysinfosnapshot &


### wait for children
wait

logger "${date}: Done qpdump on QP: ${qpnum} on dev: ${dev}  [iter_cnt: ${iter} cm_id: ${cm_id}]"

