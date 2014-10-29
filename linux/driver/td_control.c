/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. (Diablo).              *
 *    All rights reserved.                                               *
 *                                                                       *
 *    This program is free software; you can redistribute it and/or      *
 *    modify it under the terms of the GNU General Public License        *
 *    as published by the Free Software Foundation; either version 2     *
 *    of the License, or (at your option) any later version located at   *
 *    <http://www.gnu.org/licenses/                                      *
 *                                                                       *
 *    This program is distributed WITHOUT ANY WARRANTY; without even     *
 *    the implied warranty of MERCHANTABILITY or FITNESS FOR A           *
 *    PARTICULAR PURPOSE.  See the GNU General Public License for        *
 *    more details.                                                      *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "td_kdefn.h"

#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "td_control.h"
#include "td_devgroup.h"
#include "td_device.h"
#include "td_engine.h"
#include "td_raid.h"
#include "td_ioctl.h"
#include "td_monitor.h"
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

char version[VERSION_MAX_STR_SIZE-1] = __stringify(TERADIMM_VERSION);
static struct mutex td_control_mutex;

static int td_control_open(struct inode *, struct file *);
static int td_control_release(struct inode *, struct file *);
static long td_control_unlocked_ioctl(struct file *, unsigned int, unsigned long);
static long td_control_compat_ioctl(struct file *, unsigned int, unsigned long);

static const struct file_operations td_control_fops = {
	.open           = td_control_open,
	.release        = td_control_release,
	.unlocked_ioctl = td_control_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = td_control_compat_ioctl,
#endif
};

#ifdef CONFIG_PM

static int td_control_platform_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct td_osdev *dev = td_osdev_from_platform(pdev);
	
	if (dev->type == TD_OSDEV_DEVICE) {
		/* Sending hibernate because we do not see anything by FREEZE come
		* through here and HIBERNATE is the safest. */
		return td_engine_suspend(td_device_engine(td_device_from_os(dev)), PMSG_HIBERNATE);
	}
	return 0;
}


static int td_control_platform_resume(struct platform_device *pdev)
{
	struct td_osdev *dev = td_osdev_from_platform(pdev);
	if (dev->type == TD_OSDEV_DEVICE) {
		return td_engine_resume(td_device_engine(td_device_from_os(dev)));
	}
	return 0;
}

static void td_control_platform_shutdown(struct platform_device *pdev)
{
	struct td_osdev *dev = td_osdev_from_platform(pdev);
	if (dev->type == TD_OSDEV_DEVICE)
		td_engine_suspend(td_device_engine(td_device_from_os(dev)), PMSG_HIBERNATE);
}

static int td_control_platform_remove(struct platform_device *dev)
{
	return 0;
}
static int td_control_platform_probe(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver td_pdrv = {
	.probe = td_control_platform_probe,
	.remove = td_control_platform_remove,
	.shutdown = td_control_platform_shutdown,
	.suspend = td_control_platform_suspend,
	.resume = td_control_platform_resume,
	.driver = {
		.name  = "MCS",
		.owner = THIS_MODULE,
	},
};
#define td_platform_driver_register(pdev) platform_driver_register(pdev)
#define td_platform_driver_unregister(pdev) platform_driver_unregister(pdev)

#else /* no CONFIG_PM */
static inline int td_pm_noerror ()
{
	return 0;
}
#define td_platform_driver_register(pdev)       td_pm_noerror()
#define td_platform_driver_unregister(pdev)     td_pm_noerror()

#endif /* end CONFIG_PM */

static struct miscdevice td_control_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "td-control",
	.fops = &td_control_fops,
};

int __init td_control_init(void)
{
	int rc;

	mutex_init(&td_control_mutex);

	rc = misc_register(&td_control_misc_dev);

	if (rc) {
		pr_err("Registering td_control device failed!\n");
		goto misc_register_failed;
	}

	rc = td_platform_driver_register(&td_pdrv);

	if (rc) {
		pr_err("Registering td_control device failed!\n");
		goto platform_register_failed;
	}

	return rc;

	/* For completeness */
	td_platform_driver_unregister(&td_pdrv);
platform_register_failed:
	misc_deregister(&td_control_misc_dev);
misc_register_failed:
	return rc;
}

void __exit td_control_exit(void)
{
	/* lock here synchronizes us with the last in flight ioctl */
	mutex_lock(&td_control_mutex);

	td_platform_driver_unregister(&td_pdrv);

	misc_deregister(&td_control_misc_dev);


	mutex_unlock(&td_control_mutex);
}

static int td_control_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int td_control_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* -------- ioctl for the td-control device -------- */

static int td_ioctl_devgroup_get_status(struct td_ioctl_devgroup_status *);
static int td_ioctl_devgroup_get_counters(struct td_ioctl_devgroup_counters *);
static int td_ioctl_devgroup_get_conf(struct td_ioctl_devgroup_conf *,
			bool fill_mode);
static int td_ioctl_devgroup_set_conf(struct td_ioctl_devgroup_conf *);

static long td_control_ioctl(struct file *filp, unsigned int cmd,
		unsigned long raw_arg)
{
	union {
		struct td_ioctl_devgroup_list dg_list;
		struct td_ioctl_devgroup_create dg_create;
		struct td_ioctl_devgroup_name dg_name;
		struct td_ioctl_devgroup_status dg_stat;
		struct td_ioctl_devgroup_counters dg_counter;
		struct td_ioctl_devgroup_conf dg_conf;
		struct td_ioctl_device_list dev_list;
		struct td_ioctl_device_create dev_create;
		struct td_ioctl_device_name dev_name;
		struct td_ioctl_device_ver ver;
		struct td_ioctl_monitor_rate mon_rate;
#ifdef TD_IOCTL_RAID_CREATE_V0
		struct td_ioctl_raid_device_create_v0 raid_create_v0;
#endif
		struct td_ioctl_raid_create raid_create;
	} __user *u_arg, *k_arg, __static_arg, *__big_arg = NULL;
	unsigned copy_in_size, copy_out_size, big_size = 0;
	int rc;

	/* prepare */

	u_arg = (__user void*)raw_arg;
	k_arg = &__static_arg;

	copy_in_size = 0;
	copy_out_size = 0;
	switch (cmd) {
	case TD_IOCTL_DEVGROUP_LIST:
		/* copy in the base structure */
		rc = -EFAULT;
		copy_in_size = sizeof(k_arg->dg_list);
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		if (!k_arg->dg_list.buf_size)
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = k_arg->dg_list.buf_size;
		copy_out_size = big_size;
		break;

	case TD_IOCTL_DEVGROUP_CREATE:
		copy_in_size = sizeof(k_arg->dg_create);
		break;

	case TD_IOCTL_DEVGROUP_DELETE:
	case TD_IOCTL_DEVGROUP_START:
	case TD_IOCTL_DEVGROUP_STOP:
		copy_in_size = sizeof(k_arg->dg_name);
		break;

	case TD_IOCTL_DEVGROUP_GET_STATUS:
		copy_in_size = sizeof(k_arg->dg_stat);
		copy_out_size = sizeof(k_arg->dg_stat);
		break;

	case TD_IOCTL_DEVGROUP_GET_ALL_COUNTERS:

		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_devgroup_counters)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_DEVGROUP_COUNTER_SIZE(k_arg->dg_counter.count);

		copy_in_size = big_size;
		copy_out_size = copy_in_size;
		break;

	case TD_IOCTL_DEVGROUP_GET_ALL_CONF:
	case TD_IOCTL_DEVGROUP_GET_CONF:
	case TD_IOCTL_DEVGROUP_SET_CONF:

		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_devgroup_conf)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_DEVGROUP_CONF_SIZE(k_arg->dg_conf.count);

		copy_in_size = big_size;
		if (cmd != TD_IOCTL_DEVGROUP_SET_CONF)
			copy_out_size = copy_in_size;
		break;

	case TD_IOCTL_DEVICE_LIST:
		/* copy in the base structure */
		rc = -EFAULT;
		copy_in_size = sizeof(k_arg->dev_list);
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		if (!k_arg->dev_list.buf_size)
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = k_arg->dev_list.buf_size;
		copy_out_size = big_size;
		break;

	case TD_IOCTL_DEVICE_CREATE:
		copy_in_size = sizeof(k_arg->dev_create);
		break;

	case TD_IOCTL_DEVICE_DELETE:
		copy_in_size = sizeof(k_arg->dev_name);
		break;

	case TD_IOCTL_DEVICE_DRIVER_VER:
		copy_out_size = sizeof(struct td_ioctl_device_ver);
		break;

	case TD_IOCTL_MONITOR_RATE:
		copy_in_size = sizeof(struct td_ioctl_monitor_rate);
		break;

#ifdef TD_IOCTL_RAID_CREATE_V0
	case TD_IOCTL_RAID_CREATE_V0:
		copy_in_size = sizeof(k_arg->raid_create_v0);
		copy_out_size = sizeof(k_arg->raid_create_v0);
		break;
#endif
	case TD_IOCTL_RAID_CREATE:
		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_raid_create)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_RAID_CREATE_SIZE(k_arg->raid_create.raid_conf.count);

		copy_in_size = big_size;
		break;

	case TD_IOCTL_RAID_DELETE:
		copy_in_size = sizeof(k_arg->dev_name);
		break;

	case TD_IOCTL_RAID_LIST:
		/* copy in the base structure */
		rc = -EFAULT;
		copy_in_size = sizeof(k_arg->dev_list);
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		if (!k_arg->dev_list.buf_size)
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = k_arg->dev_list.buf_size;
		copy_out_size = big_size;
		break;
		
	default:
		/* nothing to copy in */
		break;
	}

	/* allocate a big buffer if needed */
	if (big_size) {
		rc = -ENOMEM;
		__big_arg = kzalloc(big_size, GFP_KERNEL);
		if (!__big_arg) {
			pr_err("Control ioctl failed to allocate %u bytes.",
					big_size);
			goto bail_ioctl;
		}
		k_arg = __big_arg;
	}

	/* copy in the data struct */

	if (copy_in_size) {
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg, copy_in_size)) {
			pr_err("Control ioctl failed to copy in %u bytes.",
					copy_in_size);
			goto bail_ioctl;
		}
	}

	/* check if output can be written to */

	if (copy_out_size) {
		rc = -EFAULT;
		if (!access_ok(VERIFY_WRITE, u_arg, copy_out_size)) {
			pr_err("Control ioctl cannot write %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

	/* perform the operation under lock to prevent races with other users */

	mutex_lock(&td_control_mutex);

	switch (cmd) {
	case TD_IOCTL_DEVGROUP_LIST:
		/** ioctl used to query available device groups */
		rc = td_devgroup_dump_names(
				k_arg->dg_list.buffer,
				k_arg->dg_list.buf_size,
				&k_arg->dg_list.group_count);
		if (rc >= 0) {
			k_arg->dg_list.buf_size = rc;
			copy_out_size = sizeof(k_arg->dg_list) + rc;
			rc = 0;
		}
		break;

	case TD_IOCTL_DEVGROUP_CREATE:
		/* ioctl used to create a device group */
		rc = td_devgroup_create(k_arg->dg_create.group_name,
				k_arg->dg_create.thread_socket,
				k_arg->dg_create.thread_nice);
		break;

	case TD_IOCTL_DEVGROUP_DELETE:
		/* ioctl used to delete a device group */
		rc = td_devgroup_delete(k_arg->dg_name.group_name);
		break;

	case TD_IOCTL_DEVGROUP_START:
		/* ioctl used to start the thread of a device group */
		rc = td_devgroup_start(k_arg->dg_name.group_name);
		break;

	case TD_IOCTL_DEVGROUP_STOP:
		/* ioctl used to stop the thread of a device group */
		rc = td_devgroup_stop(k_arg->dg_name.group_name);
		break;

	case TD_IOCTL_DEVGROUP_GET_STATUS:
		/* ioctl used to query the status of a device group */
		rc = td_ioctl_devgroup_get_status(&k_arg->dg_stat);
		break;

	case TD_IOCTL_DEVGROUP_GET_ALL_COUNTERS:
		/* ioctl used to query the status of a device group */
		rc = td_ioctl_devgroup_get_counters(&k_arg->dg_counter);
		break;

	case TD_IOCTL_DEVGROUP_GET_ALL_CONF:
	case TD_IOCTL_DEVGROUP_GET_CONF:
		/* ioctl used to query the configuration of a device group */
		rc = td_ioctl_devgroup_get_conf(&k_arg->dg_conf,
				cmd == TD_IOCTL_DEVGROUP_GET_ALL_CONF);
		break;

	case TD_IOCTL_DEVGROUP_SET_CONF:
		/* ioctl used to change the configuration of a device group */
		rc = td_ioctl_devgroup_set_conf(&k_arg->dg_conf);
		break;

	case TD_IOCTL_DEVICE_LIST:
		/** ioctl used to query available devices */
		rc = td_osdev_dump_names(TD_OSDEV_DEVICE,
				k_arg->dev_list.buffer,
				k_arg->dev_list.buf_size,
				&k_arg->dev_list.dev_count);
		if (rc >= 0) {
			k_arg->dev_list.buf_size = rc;
			copy_out_size = sizeof(k_arg->dev_list) + rc;
			rc = 0;
		}
		break;

	case TD_IOCTL_DEVICE_CREATE:
		/** ioctl used to create a new device */
		rc = td_device_create(k_arg->dev_create.dev_name,
				k_arg->dev_create.phys_slot_name,
				k_arg->dev_create.phys_mem_base,
				k_arg->dev_create.phys_mem_size,
				k_arg->dev_create.irq_num,
				k_arg->dev_create.memspeed,
				k_arg->dev_create.cpu_socket);
		break;

	case TD_IOCTL_DEVICE_DELETE:
		/** ioctl used to delete an existing device */
		rc = td_device_delete(k_arg->dev_name.dev_name);
		break;

	case TD_IOCTL_DEVICE_DRIVER_VER:
		rc = td_ioctl_control_driver_ver(&k_arg->ver);
		break;
		
	case TD_IOCTL_MONITOR_RATE:
		rc = td_ioctl_control_mon_rate(&k_arg->mon_rate);
		break;

#ifdef TD_IOCTL_RAID_CREATE_V0
	case TD_IOCTL_RAID_CREATE_V0:
		rc = td_raid_create_v0(k_arg->raid_create_v0.raid_name,
				k_arg->raid_create_v0.raid_uuid,
				k_arg->raid_create_v0.raid_level,
				k_arg->raid_create_v0.raid_members_count);
		break;
#endif
	case TD_IOCTL_RAID_CREATE:
		rc = td_raid_create(k_arg->raid_create.raid_name,
				k_arg->raid_create.raid_uuid,
				k_arg->raid_create.raid_conf.count,
				k_arg->raid_create.raid_conf.entries);
		break;

	case TD_IOCTL_RAID_DELETE:
		rc = td_raid_delete(k_arg->dev_name.dev_name);
		break;

	case TD_IOCTL_RAID_LIST:
		/** ioctl used to query available devices */
		rc = td_osdev_dump_names(TD_OSDEV_RAID,
				k_arg->dev_list.buffer,
				k_arg->dev_list.buf_size,
				&k_arg->dev_list.dev_count);
		if (rc >= 0) {
			k_arg->dev_list.buf_size = rc;
			copy_out_size = sizeof(k_arg->dev_list) + rc;
			rc = 0;
		}
		break;
		
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	mutex_unlock(&td_control_mutex);

	switch (rc) {
	case 0:
		break;

	case -ENOBUFS:
		/* in these cases we'd like to copy_to_user so that
		 * the user app can check the count and allocate a
		 * larger buffer.
		*/
		if (cmd == TD_IOCTL_DEVGROUP_GET_ALL_COUNTERS)
			break;
		if (cmd == TD_IOCTL_DEVGROUP_GET_ALL_CONF)
			break;

	default:
		goto bail_ioctl;
	}

	/* copy data back */

	if (copy_out_size) {
		if (copy_to_user(u_arg, k_arg, copy_out_size)) {
			pr_err("Control ioctl failed to copy out %u bytes.",
					copy_out_size);
			rc = -EFAULT;
			goto bail_ioctl;
		}
	}

bail_ioctl:
	if (__big_arg)
		kfree(__big_arg);
	return rc;
}
static long td_control_unlocked_ioctl(struct file *filp, unsigned int cmd,
		unsigned long raw_arg)
{
	int rc;

	rc = td_control_ioctl(filp, cmd, raw_arg);
	if(-ENOIOCTLCMD == rc)
		rc = -EINVAL;
	return rc;
}

#ifdef CONFIG_COMPAT
static long td_control_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long raw_arg)
{
	int rc;

	rc = td_control_ioctl(filp, cmd,
			(unsigned long)compat_ptr(raw_arg));
	/* COMPAT_IOCTL is supposed to return ENOIOCTLCMD */
	return rc;
}
#endif

/** ioctl used to query the status of a device group */
static int td_ioctl_devgroup_get_status(struct td_ioctl_devgroup_status *stat)
{
	int rc;
	struct td_devgroup *dg;

	rc = -ENOENT;
	dg = td_devgroup_get_by_name(stat->group_name);
	if (!dg)
		goto error_not_found;

	stat->thread_socket  = dg->dg_socket;
	stat->thread_nice    = dg->dg_nice;
	stat->thread_running = td_devgroup_is_running(dg);

	td_devgroup_put(dg);

	rc = 0;

error_not_found:
	return rc;
}


/** ioctl used to query the counters of a device group */
static int td_ioctl_devgroup_get_counters(struct td_ioctl_devgroup_counters *cntrs)
{
	int i, j, rc = 0;
	struct td_devgroup *dg;
	struct td_work_node *wn;
	struct td_worker *w;

	rc = -ENOENT;
	dg = td_devgroup_get_by_name(cntrs->group_name);
	if (!dg)
		goto error;

	wn = &(dg->dg_work_node);

	/* determine the total number of static & dynamic counters */
	i = TD_DEVGROUP_ENDIO_COUNT_MAX + \
		(wn->wn_worker_count * TD_DEVGROUP_WORKER_COUNT_MAX);

	if (cntrs->count < i) {
		/* the user buffer isn't big enough, return the count required */
		cntrs->count = i;
		rc = -ENOBUFS;
		goto error;
	}

	cntrs->count = 0; /* increase the count in loop body */

	/* fill the static counters */
	for (i = 0 ; i < TD_DEVGROUP_ENDIO_COUNT_MAX ; i++, cntrs->count++) {
		cntrs->entries[cntrs->count].type = TD_DEVGROUP_COUNTER_ENDIO;
		cntrs->entries[cntrs->count].var = cntrs->count;
		rc = td_devgroup_get_counter(dg, NULL, cntrs->entries[cntrs->count].type,
			cntrs->entries[cntrs->count].var,
			&cntrs->entries[cntrs->count].val);

		if (rc == -ENOENT)
			continue;
		if (rc)
			goto error;
	}

	/* fill the dynamic counters */
	for (i = 0 ; i < TD_DEVGROUP_WORKER_COUNT_MAX ; i++) {
		for (j = 0 ; j < wn->wn_worker_count ; j++, cntrs->count++) {
			w = (wn->wn_workers + j);
			cntrs->entries[cntrs->count].type = TD_DEVGROUP_COUNTER_WORKER;
			cntrs->entries[cntrs->count].worker = w->w_cpu;
			cntrs->entries[cntrs->count].var = i;
			rc = td_devgroup_get_counter(dg, w, cntrs->entries[cntrs->count].type,
				cntrs->entries[cntrs->count].var,
				&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}
	}
	if (rc == -ENOENT)
		rc = 0;

	td_devgroup_put(dg);

error:
	return rc;
}

/** ioctl used to query the configuration of a device group */
static int td_ioctl_devgroup_get_conf(struct td_ioctl_devgroup_conf *conf,
			bool fill_mode)
{
	int i, rc = -EINVAL;
	struct td_devgroup *dg;

	dg = td_devgroup_get_by_name(conf->group_name);
	if (!dg)
		goto error;

	if (fill_mode) {
		/* determine the total number of configuration options */
		i = TD_DEVGROUP_CONF_GENERAL_MAX + TD_DEVGROUP_CONF_WORKER_MAX;

		if (conf->count < i) {
			/* the user buffer isn't big enough, return the count required */
			conf->count = i;
			rc = -ENOBUFS;
			goto error;
		}

		conf->count = 0; /* increase the count in loop body */

		/* fill the general counters */
		for (i = 0 ; i < TD_DEVGROUP_CONF_GENERAL_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_DEVGROUP_CONF_GENERAL;
			conf->entries[conf->count].var = i;
			rc = td_devgroup_get_conf(dg, conf->entries[conf->count].type,
				conf->entries[conf->count].var,
				&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the worker counters */
		for (i = 0 ; i < TD_DEVGROUP_CONF_WORKER_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_DEVGROUP_CONF_WORKER;
			conf->entries[conf->count].var = i;
			rc = td_devgroup_get_conf(dg, conf->entries[conf->count].type,
				conf->entries[conf->count].var,
				&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		if (rc == -ENOENT)
			rc = 0;
	}
	else {
		rc = td_devgroup_get_conf(dg, conf->entries[0].type,
			conf->entries[0].var,
			&conf->entries[0].val);
	}

	td_devgroup_put(dg);

error:
	return rc;
}

/** ioctl used to change the configuration of a device group */
static int td_ioctl_devgroup_set_conf(struct td_ioctl_devgroup_conf *conf)
{
	int i, rc = -EINVAL;
	struct td_devgroup *dg;

	dg = td_devgroup_get_by_name(conf->group_name);
	if (!dg)
		goto error_not_found;

	rc = -EIO;

	if(!td_devgroup_is_running(dg))
		goto error_devgroup_stop;

	for (i=0; i<conf->count; i++) {
		rc = td_devgroup_set_conf(dg, conf->entries[i].type,
				conf->entries[i].var,
				conf->entries[i].val);
		if (rc)
			break;
	}

error_devgroup_stop:
	td_devgroup_put(dg);

error_not_found:
	return rc;
}

int td_ioctl_control_mon_rate( struct td_ioctl_monitor_rate *mr)
{
	td_monitor_set_rate(mr->rate);
	return 0;
}

int td_ioctl_control_driver_ver (struct td_ioctl_device_ver *ver)
{
	memcpy(ver->version, version, sizeof(version));
	return 0;
}
