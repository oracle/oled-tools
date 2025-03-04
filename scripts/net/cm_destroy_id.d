#!/usr/sbin/dtrace -qs

/*
 * Author(s): Arumugam Kolappan, Anand Khoje
 *
 * Description:
 *  This script will execute the external command "mlxqpdump.sh" when
 *  cm_destroy_id_wait_timeout() probe fires. It is better to give full
 *  path to the external command. Stops after 5 probes.
 *
 * Usage:
 *    ./cm_destroy_id.d
 *
 * output:
 *    When the probe is fired, it prints qp_num, cm_id, pci_id and runs 'mlxqpdump.sh'
 */

/*
 * min_kernel 4.14.35-2047.536.3,5.4.17-2136.330.7.4,5.15.0-206.149.3
 */

#pragma D option destructive

dtrace:::BEGIN
{
	iter_count = 0;
}

fbt::cm_destroy_id_wait_timeout:entry
{
	this->cm_id = (struct ib_cm_id *)arg0;
	this->ibdev = (struct ib_device *)this->cm_id->device;
	this->mibdev = (struct mlx5_ib_dev *)this->ibdev;
	this->mdev = (struct mlx5_core_dev *)this->mibdev->mdev;
	this->pcidev = (struct pci_dev *)this->mdev->pdev;
	this->dev = (struct device *)&this->pcidev->dev;
	this->kobj = (struct kobject *)&this->dev->kobj;

	/* get the QP-num */
	this->cm_id_priv = (struct cm_id_private *)this->cm_id;
	this->mad_agent = (struct ib_mad_agent *)this->cm_id_priv->av.port->mad_agent;
	this->ib_qp = (struct ib_qp *)this->mad_agent->qp;

	iter_count = iter_count + 1;

	printf("Trigger Resourcedump: iter=%d qp_num=%d cm_id=%p bdf=%s\n",
		iter_count, this->ib_qp->qp_num, this->cm_id, stringof(this->kobj->name));

	/* Execute the command to run in the back ground */

	system("/bin/bash /root/mlx_logs/mlxqpdump.sh %s %d  %d %p",
		stringof(this->kobj->name), this->ib_qp->qp_num, iter_count, this->cm_id);

}

fbt::cm_destroy_id_wait_timeout:entry
/ ( iter_count >= 5 ) /
{
	printf("Exiting the script after %d iterations\n", iter_count);
	exit(0);
}

