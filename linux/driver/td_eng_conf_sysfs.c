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

#include "td_device.h"
#include "td_engine.h"
#include "td_eng_conf.h"
#include "td_eng_conf_sysfs.h"
#include <linux/ctype.h>



static int __check_dev_kdev (struct td_osdev *dev, void* data)
{
	return dev->type == TD_OSDEV_DEVICE && dev->disk &&
		disk_to_dev(dev->disk) == data;
}

/* locate the td_device based on it's kernel device */
static inline struct td_device *td_device_from_device(struct device *kdev)
{
	struct td_osdev *dev = td_osdev_find(__check_dev_kdev, kdev);
	if (dev)
		return td_device_from_os(dev);
	return NULL;
}


#define __td_dev_set_conf(dev,_conf_,what,val) ({                            \
		typeof(dev->td_engine._conf_) *conf;                         \
		conf = &(dev)->td_engine._conf_;                             \
		conf->what = (val);                                          \
	})

#define __td_verify_dev_always(dev) 0

#define __td_verify_dev_inactive(dev) ({                                     \
		int rc = 0;                                                  \
		if (unlikely(td_device_check_state(dev, ONLINE)))            \
			rc = -EBUSY;                                         \
		rc;                                                          \
	})

/* ---- */

#define SHOW_FMT_u32 "%u"
#define SHOW_FMT_u64 "%lu"

#define RO_ATTRS S_IRUGO
#define RW_ATTRS S_IRUGO | S_IWUSR

#define DECLARE_SHOW_FUNCTION(_type_,_conf_,_name_)                          \
	static ssize_t _name_##_show(struct device *kdev,                    \
			struct device_attribute *attr, char *buf)            \
	{                                                                    \
		struct td_device *dev;                                       \
		uint16_t val;                                                \
		int rc;                                                      \
                                                                             \
		dev = td_device_from_device(kdev);                           \
		if (!dev)                                                    \
			return -ENODEV;                                      \
                                                                             \
                val = td_eng_conf_var_get(td_device_engine(dev), _name_);    \
                                                                             \
                                                                             \
		rc = sprintf(buf, SHOW_FMT_##_type_"\n", val);               \
                                                                             \
		td_device_put(dev);                                          \
		return rc;                                                   \
	}

#define DECLARE_STORE_FUNCTION(_type_,_conf_,_name_,_mode_,_min_,_max_)      \
	static ssize_t _name_##_store(struct device *kdev,                   \
			struct device_attribute *attr,                       \
			const char *buf, size_t len)                         \
	{                                                                    \
		int rc;                                                      \
		struct td_device *dev;                                       \
		ulong val;                                                   \
                                                                             \
		dev = td_device_from_device(kdev);                           \
		if (!dev)                                                    \
			return -ENODEV;                                      \
                                                                             \
		rc = __td_verify_dev_##_mode_(dev);                          \
		if (rc)                                                      \
			return rc;                                           \
                                                                             \
		rc = td_parse_ulong(buf, 0, &val);                           \
		if (rc)                                                      \
			goto _name_##bail;                                   \
		rc = -EINVAL;                                                \
		if (val < (ulong)(_min_) || val > (ulong)(_max_))            \
			goto _name_##bail;                                   \
                                                                             \
                                                                             \
                td_eng_conf_var_set(td_device_engine(dev), _name_, val);     \
                                                                             \
		rc = len;                                                    \
	_name_##bail:                                                        \
		td_device_put(dev);                                          \
		return rc;                                                   \
	}

#define DECLARE_TD_ATTRIBUTE(_type_,_name_,_mode_,_min_,_max_)                \
	DECLARE_SHOW_FUNCTION(_type_,conf,_name_)                             \
	DECLARE_STORE_FUNCTION(_type_,conf,_name_,_mode_,_min_,_max_)         \
	static DEVICE_ATTR(_name_, RW_ATTRS, _name_##_show, _name_##_store);

#define DECLARE_HW_ATTRIBUTE(_type_,_name_,_mode_,_min_,_max_)                \
	DECLARE_SHOW_FUNCTION(_type_,conf.hardware,_name_)                    \
	DECLARE_STORE_FUNCTION(_type_,conf.hardware,_name_,_mode_,_min_,_max_)\
	static DEVICE_ATTR(_name_, RW_ATTRS, _name_##_show, _name_##_store);


#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wtype-limits"

DECLARE_TD_ATTRIBUTE(  u32,  TOKENS,                    inactive,  0,  TD_TOKENS_PER_DEV-1)
DECLARE_TD_ATTRIBUTE(  u32,  HOST_READ_BUFS,            inactive,  0,  TD_HOST_RD_BUFS_PER_DEV-1)
DECLARE_TD_ATTRIBUTE(  u32,  HOST_WRITE_BUFS,           inactive,  0,  TD_HOST_WR_BUFS_PER_DEV-1)
DECLARE_TD_ATTRIBUTE(  u32,  CORE_BUFS,                 inactive,  0,  TD_CORE_BUFS_PER_DEV-1)
DECLARE_TD_ATTRIBUTE(  u32,  WRITE_WAIT_USEC,           always,    0,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  BIO_R_START_TIMEOUT_USEC,    always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_R_QUEUED_TIMEOUT_USEC,   always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_R_RECEIVED_TIMEOUT_USEC, always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_R_WAIT_SSD_TIMEOUT_USEC, always,    1,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  BIO_W_START_TIMEOUT_USEC,    always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_W_QUEUED_TIMEOUT_USEC,   always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_W_RECEIVED_TIMEOUT_USEC, always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  BIO_W_WAIT_SSD_TIMEOUT_USEC, always,    1,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  START_TIMEOUT_USEC,        always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  QUEUED_TIMEOUT_USEC,       always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  RECEIVED_TIMEOUT_USEC,     always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  WAIT_SSD_TIMEOUT_USEC,     always,    1,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  RETRIES,                   always,    1,  UINT_MAX);
#ifdef CONFIG_NEW_RETRIES
DECLARE_TD_ATTRIBUTE(  u32,  TIMEOUT_RETRY,            always,    0,  1);
DECLARE_TD_ATTRIBUTE(  u32,  XSUM_ERR_RETRY,            always,    0,  1);
#endif

DECLARE_TD_ATTRIBUTE(  u32,  RESETS,                    always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  CLFLUSH,                   always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  WBINVD,                    always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  HOST_READ_ALIASES,         inactive,  0,  TD_MAX_ALLOWED_ALIASES);
DECLARE_TD_ATTRIBUTE(  u32,  HOST_STATUS_ALIASES,       inactive,  0,  TD_MAX_ALLOWED_ALIASES);
DECLARE_TD_ATTRIBUTE(  u32,  QUICK_WRBUF,               always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  QUICK_TOK_REUSE,           always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  HALT_ON_TIMEOUT,           always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  VERBOSE_TOK_FAILURES,      always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  RETRY_TIMEDOUT_DEALLOCATES,always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  ORPHAN_RDBUF_REQ_CNT,      always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  ORPHAN_RDBUF_TIMEOUT_CNT,  always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  ORPHAN_RDBUF_REQ_MSEC,     always,    1,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  EARLY_COMMIT,              always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  EARLY_DEALLOCATE,          always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  EARLY_START,               always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  MAX_START_BURST,           always,    1,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  EARLY_SPLIT_REQ,           always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  COLLISION_CHECK,           always,    0,  2);

DECLARE_TD_ATTRIBUTE(  u32,  INDEPENDENT_DEALLOCATE,    always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  DELAY_POST_WRBUF_USEC,     always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  DELAY_POST_CMD_USEC,       always,    0,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  HOLD_TIMEDOUT_TOKENS,      always,    0,  1);

#ifdef CONFIG_MEGADIMM_USES_CORE_BUFFS_ONLY
DECLARE_TD_ATTRIBUTE(  u32,  CORE_ONLY,                 always,    0,  UINT_MAX);
#endif


DECLARE_TD_ATTRIBUTE(  u32,  MAGIC_FLAGS,               always,    0,  UINT_MAX);

DECLARE_TD_ATTRIBUTE(  u32,  DELAY_WRITE_TO_READ_USEC,  always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  TARGET_IOPS,               always,    0,  UINT_MAX);
DECLARE_TD_ATTRIBUTE(  u32,  IOPS_SAMPLE_MSEC,          always,    0,  UINT_MAX);

//DECLARE_HW_ATTRIBUTE(  u32,  HW_SECTOR_SIZE,            inactive,  512, 4096);
//DECLARE_HW_ATTRIBUTE(  u32,  BIO_SECTOR_SIZE,           inactive,  512, 4096);

static struct attribute *td_disk_attrs[] = {
	&dev_attr_TOKENS.attr,
	&dev_attr_HOST_READ_BUFS.attr,
	&dev_attr_HOST_WRITE_BUFS.attr,
	&dev_attr_CORE_BUFS.attr,
	&dev_attr_WRITE_WAIT_USEC.attr,
	&dev_attr_BIO_R_START_TIMEOUT_USEC.attr,
	&dev_attr_BIO_R_QUEUED_TIMEOUT_USEC.attr,
	&dev_attr_BIO_R_RECEIVED_TIMEOUT_USEC.attr,
	&dev_attr_BIO_R_WAIT_SSD_TIMEOUT_USEC.attr,
	&dev_attr_BIO_W_START_TIMEOUT_USEC.attr,
	&dev_attr_BIO_W_QUEUED_TIMEOUT_USEC.attr,
	&dev_attr_BIO_W_RECEIVED_TIMEOUT_USEC.attr,
	&dev_attr_BIO_W_WAIT_SSD_TIMEOUT_USEC.attr,
	&dev_attr_START_TIMEOUT_USEC.attr,
	&dev_attr_QUEUED_TIMEOUT_USEC.attr,
	&dev_attr_RECEIVED_TIMEOUT_USEC.attr,
	&dev_attr_WAIT_SSD_TIMEOUT_USEC.attr,
	&dev_attr_RETRIES.attr,
#ifdef CONFIG_NEW_RETRIES
	&dev_attr_TIMEOUT_RETRY.attr,
	&dev_attr_XSUM_ERR_RETRY.attr,
#endif
	&dev_attr_RESETS.attr,
	&dev_attr_QUICK_WRBUF.attr,
	&dev_attr_QUICK_TOK_REUSE.attr,
	&dev_attr_HALT_ON_TIMEOUT.attr,
	&dev_attr_VERBOSE_TOK_FAILURES.attr,
	&dev_attr_RETRY_TIMEDOUT_DEALLOCATES.attr,
	&dev_attr_ORPHAN_RDBUF_REQ_CNT.attr,
	&dev_attr_ORPHAN_RDBUF_TIMEOUT_CNT.attr,
	&dev_attr_ORPHAN_RDBUF_REQ_MSEC.attr,
	&dev_attr_EARLY_COMMIT.attr,
	&dev_attr_EARLY_DEALLOCATE.attr,
	&dev_attr_EARLY_START.attr,
	&dev_attr_MAX_START_BURST.attr,
	//&dev_attr_HW_SECTOR_SIZE.attr,
	//&dev_attr_BIO_SECTOR_SIZE.attr,
	&dev_attr_EARLY_SPLIT_REQ.attr,
	&dev_attr_COLLISION_CHECK.attr,
#ifdef CONFIG_MEGADIMM_USES_CORE_BUFFS_ONLY
	&dev_attr_CORE_ONLY.attr,
#endif
	&dev_attr_MAGIC_FLAGS.attr,
	&dev_attr_INDEPENDENT_DEALLOCATE.attr,
	&dev_attr_CLFLUSH.attr,
	&dev_attr_WBINVD.attr,
	&dev_attr_HOST_READ_ALIASES.attr,
	&dev_attr_HOST_STATUS_ALIASES.attr,
	&dev_attr_DELAY_POST_WRBUF_USEC.attr,
	&dev_attr_DELAY_POST_CMD_USEC.attr,
	&dev_attr_HOLD_TIMEDOUT_TOKENS.attr,
	&dev_attr_DELAY_WRITE_TO_READ_USEC.attr,
	&dev_attr_TARGET_IOPS.attr,
	&dev_attr_IOPS_SAMPLE_MSEC.attr,
	NULL
};

static struct attribute_group td_disk_attr_group = {
	.name = "conf",
	.attrs = td_disk_attrs,
};

#if 0

#define DECLARE_SIM_ATTRIBUTE(_type_,_name_,_mode_,_min_,_max_)               \
	DECLARE_SHOW_FUNCTION(_type_,msconf,_name_)                           \
	DECLARE_STORE_FUNCTION(_type_,msconf,_name_,_mode_,_min_,_max_)       \
	static DEVICE_ATTR(_name_, RW_ATTRS, _name_##_show, _name_##_store);

DECLARE_SIM_ATTRIBUTE(  u32,  channels,                  inactive,  0,  TD_CHANS_PER_DEV-1)
DECLARE_SIM_ATTRIBUTE(  u32,  luns,                      inactive,  0,  TD_LUNS_PER_CHAN-1)
DECLARE_SIM_ATTRIBUTE(  u32,  lunbuses,                  inactive,  0,  TD_LUNBUSES_PER_CHAN-1)
DECLARE_SIM_ATTRIBUTE(  u32,  ssd_read_bufs,             inactive,  0,  TD_SSD_RD_BUFS_PER_DEV-1)
DECLARE_SIM_ATTRIBUTE(  u32,  ssd_write_bufs,            inactive,  0,  TD_SSD_WR_BUFS_PER_DEV-1)
DECLARE_SIM_ATTRIBUTE(  u32,  horizon_usec,              always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  flash_read_nsec,           always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  flash_write_nsec,          always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  ssd_lun_xfer_nsec,         always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  core_ssd_xfer_nsec,        always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  host_core_xfer_nsec,       always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  proc_cmd_nsec,             always,    0,  UINT_MAX);
DECLARE_SIM_ATTRIBUTE(  u32,  proc_stat_nsec,            always,    0,  UINT_MAX);


static struct attribute *td_disk_sim_attrs[] = {
	&dev_attr_channels.attr,
	&dev_attr_luns.attr,
	&dev_attr_lunbuses.attr,
	&dev_attr_host_read_bufs.attr,
	&dev_attr_ssd_read_bufs.attr,
	&dev_attr_ssd_write_bufs.attr,
	&dev_attr_flash_read_nsec.attr,
	&dev_attr_flash_write_nsec.attr,
	&dev_attr_ssd_lun_xfer_nsec.attr,
	&dev_attr_core_ssd_xfer_nsec.attr,
	&dev_attr_host_core_xfer_nsec.attr,
	&dev_attr_proc_cmd_nsec.attr,
	&dev_attr_proc_stat_nsec.attr,
	&dev_attr_horizon_usec.attr,
	NULL
};

static struct attribute_group td_disk_sim_attr_group = {
	.name = "sim_conf",
	.attrs = td_disk_sim_attrs,
};
#endif


int td_eng_conf_sysfs_register(struct td_device *dev)
{
	int rc;
	struct gendisk *disk = dev->os.disk;
	rc = sysfs_create_group(&disk_to_kobj(disk), &td_disk_attr_group);
#if 0
	if (rc<0)
		return rc;
	if (!strncmp(dev->td_name, "sim", 3))
		rc = sysfs_create_group(&disk_to_dev(disk)->kobj,
			&td_disk_sim_attr_group);
#endif
	return rc;
}

void td_eng_conf_sysfs_unregister(struct td_device *dev)
{
	struct gendisk *disk = dev->os.disk;
	sysfs_remove_group(&disk_to_kobj(disk), &td_disk_attr_group);
#if 0
	if (!strncmp(dev->td_name, "sim", 3))
		sysfs_remove_group(&disk_to_dev(disk)->kobj,
			&td_disk_sim_attr_group);
#endif
}


