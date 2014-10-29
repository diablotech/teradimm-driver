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

#include "td_compat.h"

#include "td_devgroup.h"
#include "td_device.h"
#include "td_engine.h"
#ifdef CONFIG_TERADIMM_SYSFS
#include "td_eng_conf_sysfs.h"
#endif
#include "td_eng_conf.h"
#include "td_eng_hal.h"
#include "td_ioctl.h"
#include "td_ucmd.h"

/**
 * set a configuration variable
 *
 * NOTE: call this function while holding td_device_lock(), and the device
 *       cannot be active
 */
int td_device_set_conf(struct td_device *dev, enum td_device_conf_type type,
		uint32_t conf_num, uint64_t val)
{
	int rc = -EINVAL;
	struct td_engine *eng;
	const struct td_conf_var_desc *descr;
	uint64_t *reg;
	WARN_TD_DEVICE_UNLOCKED(dev);
	if (type >= TD_DEVICE_CONF_TYPE_MAX)
		goto bail;

	eng = td_device_engine(dev);

	if (type == TD_DEVICE_CONF_HAL) {
		/* Try setting the hal with this value */
		rc = td_eng_hal_set_conf(eng, conf_num, val);
		goto bail;
	}

	switch (type) {
	default:
		goto bail;

	case TD_DEVICE_CONF_ENGINE:
		if (conf_num >= TD_CONF_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_var_desc[conf_num];
		reg = &eng->conf.regs[conf_num];
		break;

	case TD_DEVICE_CONF_HARDWARE:
		if (conf_num >= TD_CONF_HW_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_hw_var_desc[conf_num];
		reg = &eng->conf.hardware[conf_num];
		break;

	case TD_DEVICE_CONF_MCEFREE:
		if (conf_num >= TD_CONF_MCEFREE_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_mcefree_var_desc[conf_num];
		reg = &eng->conf.mcefree[conf_num];
		break;

	}

	rc = -ENOENT;
	if(descr->check == NULL)
		goto bail;

	rc = -EBUSY;
	if (descr->check(eng, conf_num, val))
		goto bail;

	rc = -EINVAL;
	if (val < descr->min || val > descr->max)
		goto bail;

	*reg = val;
	rc = 0;

bail:
	return rc;
}

/**
 * get a configuration variable
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_get_conf(struct td_device *dev, enum td_device_conf_type type,
		uint32_t conf_num, uint64_t *val)
{
	int rc = -ENOENT;
	struct td_engine *eng;
	const struct td_conf_var_desc *descr;
	uint64_t *reg = NULL;
	WARN_TD_DEVICE_UNLOCKED(dev);

	if (type >= TD_DEVICE_CONF_TYPE_MAX)
		goto bail;

	eng = td_device_engine(dev);

	if (type == TD_DEVICE_CONF_HAL) {
		/* Try setting the hal with this value */
		rc = td_eng_hal_get_conf(eng, conf_num, val);
		goto bail;
	}

	switch (type) {
	default:
		goto bail;


	case TD_DEVICE_CONF_ENGINE:
		if (conf_num >= TD_CONF_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_var_desc[conf_num];
		reg = &eng->conf.regs[conf_num];
		break;

	case TD_DEVICE_CONF_HARDWARE:
		if (conf_num >= TD_CONF_HW_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_hw_var_desc[conf_num];
		reg = &eng->conf.hardware[conf_num];
		break;

	case TD_DEVICE_CONF_MCEFREE:
		if (conf_num >= TD_CONF_MCEFREE_REGS_MAX)
			goto bail;
		descr = &td_eng_conf_mcefree_var_desc[conf_num];
		reg = &eng->conf.mcefree[conf_num];
		break;
	}

	*val = *reg;
	rc = 0;

bail:
	return rc;
}

int td_ioctl_device_get_conf(struct td_device *dev,
		struct td_ioctl_conf *conf, bool fill_mode)
{
	int i, rc = -EINVAL;

	WARN_TD_DEVICE_UNLOCKED(dev);

	if (fill_mode) {
		/* determine the total number of configuration options */
		i = TD_CONF_REGS_MAX;
		i += TD_CONF_HW_REGS_MAX;
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
		i += TD_CONF_MCEFREE_REGS_MAX;
#endif

		if (conf->count < i) {
			/* the user buffer isn't big enough, return the count required */
			conf->count = i;
			rc = -ENOBUFS;
			goto error;
		}

		conf->count = 0; /* increase the count in loop body */

		/* fill the engine conf */
		for (i = 0 ; i < TD_CONF_REGS_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_DEVICE_CONF_ENGINE;
			conf->entries[conf->count].var = i;
			rc = td_device_get_conf(dev, conf->entries[conf->count].type,
					conf->entries[conf->count].var,
					&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the hardware conf */
		for (i = 0 ; i < TD_CONF_HW_REGS_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_DEVICE_CONF_HARDWARE;
			conf->entries[conf->count].var = i;
			rc = td_device_get_conf(dev, conf->entries[conf->count].type,
					conf->entries[conf->count].var,
					&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
		/* fill the mcefree conf */
		for (i = 0 ; i < TD_CONF_MCEFREE_REGS_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_DEVICE_CONF_MCEFREE;
			conf->entries[conf->count].var = i;
			rc = td_device_get_conf(dev, conf->entries[conf->count].type,
					conf->entries[conf->count].var,
					&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		if (rc == -ENOENT)
			rc = 0;
#endif
	}
	else {
		for (i = 0; i < conf->count; i++) {
			rc = td_device_get_conf(dev, conf->entries[i].type,
					conf->entries[i].var,
					&conf->entries[i].val);
			if (rc)
				break;
		}
	}

error:
	return rc;
}

int td_ioctl_device_set_conf(struct td_device *dev,
		struct td_ioctl_conf *conf)
{
	int i, rc = 0;
	struct td_engine *eng;
	struct td_devgroup *dg;

	rc = -EINVAL;
	if (!conf->count)
		goto error;

	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;
	if (!dg)
		goto error;

	rc = -EIO;
	if(!td_run_state_check(eng, RUNNING))
		goto error;

	if(!td_devgroup_is_running(dg))
		goto error;

	for (i=0; i<conf->count; i++) {
		rc = td_device_set_conf(dev, conf->entries[i].type,
				conf->entries[i].var,
				conf->entries[i].val);
		if (rc)
			break;
	}

error:
	return rc;
}

int td_ioctl_device_get_info(struct td_device *dev,
		struct td_ioctl_device_info *info)
{
	strncpy(info->phys_slot_name, dev->td_slot, TD_DEVICE_SLOT_NAME_MAX);

	info->phys_mem_base    = dev->td_mapper.phys_base;
	info->phys_mem_size    = dev->td_mapper.phys_size;
	info->phys_mem_speed   = dev->td_memspeed;
	info->irq_num          = dev->td_irq;
	info->memspeed         = dev->td_memspeed;
	info->cpu_socket       = dev->td_cpu_socket;

	/* TODO: We should put the actual UUID in here somehow */
	memcpy(info->uuid, dev->os.uuid, sizeof(info->uuid));
	return 0;
}

int td_ioctl_device_get_state(struct td_device *dev,
		struct td_ioctl_device_state *state)
{
	struct td_devgroup *dg;

	dg = dev->td_devgroup;
	if (dg)
		strncpy(state->group_name, dg->dg_name, TD_DEVGROUP_NAME_MAX);
	else
		memset(state->group_name, 0, sizeof(state->group_name));

	state->device_state  = dev->td_state;
	state->run_state     = td_run_state(td_device_engine(dev));

	state->storage_capacity = dev->os.block_params.capacity;

	state->control_users = atomic_read(&dev->os.control_users);
	state->block_users   = atomic_read(&dev->os.block_users);

	return 0;
}
/**
 * handle a user command
 * @param dev  - device to send command to
 * @param user_cmd - The user mapped command ioctl data
 * @return 0 on success, or appropriate error
 *
 * NOTE: the ucmd used by this function is not the
 * user buffer.  Contents from the user buffer must be
 * updated from the ucmd once it's completed.
 */
int td_ioctl_device_cmd_pt(struct td_device *dev,
		struct td_ioctl_device_cmd_pt *dev_cmd,
		void *locker_context)
{
	int rc;
	struct td_engine *eng;
	struct td_ucmd *ucmd;
	uint64_t start, end;

	WARN_TD_DEVICE_UNLOCKED(dev);

	start = td_get_cycles();
	ucmd = (struct td_ucmd*)kmalloc(sizeof(*ucmd), GFP_KERNEL);
	if (!ucmd)
		return -ENOMEM;

	memcpy(&ucmd->ioctl, dev_cmd, sizeof(ucmd->ioctl));

	eng = td_device_engine(dev);

	if (!td_state_can_accept_requests(eng)) {
		rc = -EIO;
		goto setup_error;
	}

	/* if expecting to send data, pointer and length are required */
	if (ucmd->ioctl.data_len_to_device
			&& !(ucmd->ioctl.data_len_to_device <= PAGE_SIZE
				&& ucmd->ioctl.data)) {
		td_eng_err(eng, "UCMD: to len %u, data: %p\n",
				ucmd->ioctl.data_len_to_device,
				ucmd->ioctl.data);
		rc = -EINVAL;
		goto setup_error;
	}
	if (ucmd->ioctl.data_len_from_device
			&& !(ucmd->ioctl.data_len_from_device <= PAGE_SIZE
				&& ucmd->ioctl.data)) {
		td_eng_err(eng, "UCMD from len  %u, data: %p\n",
				ucmd->ioctl.data_len_from_device,
				ucmd->ioctl.data);
		rc = -EINVAL;
		goto setup_error;
	}

	td_ucmd_init(ucmd);

	if (unlikely(eng->locker_context == locker_context))
		ucmd->locked = 1;

	rc = 0;
	if(ucmd->ioctl.data) {
		rc = td_ucmd_map(ucmd, current,
				 (unsigned long)ucmd->ioctl.data);
	}
	if (rc)
		goto bail_running;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	end = td_get_cycles();
	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;
	
	memcpy(dev_cmd, &ucmd->ioctl, sizeof(ucmd->ioctl));

bail_running:
	td_ucmd_put(ucmd);
	return rc;

setup_error:
	kfree(ucmd);
	return rc;
}

int td_device_is_locked(struct td_device *dev, void *locker_context) {
	struct td_engine *eng = td_device_engine(dev);
	if (eng->locker_context == locker_context) {
		return 1;
	} else {
		return 0;
	}
}

int td_device_get_counter(struct td_device *dev,
		enum td_device_counter_type type,
		uint32_t cntr_num, uint64_t *val)
{
	int rc = -ENOENT;
	struct td_engine *eng;
	uint64_t *reg = NULL;
	uint64_t offset = 0;
	WARN_TD_DEVICE_UNLOCKED(dev);

	if (type >= TD_DEVICE_COUNTER_TYPE_MAX)
		goto bail;


	eng = td_device_engine(dev);

	switch (type) {
	default:
		goto bail;

	case TD_DEVICE_COUNTER_READ:
		if (cntr_num >= TD_DEV_GEN_COUNT_MAX)
			goto bail;
		offset = TD_DEV_GEN_COUNTER_READ_DATA(cntr_num);
		break;

	case TD_DEVICE_COUNTER_WRITE:
		if (cntr_num >= TD_DEV_GEN_COUNT_MAX)
			goto bail;
		offset = TD_DEV_GEN_COUNTER_WRITE_DATA(cntr_num);
		break;

	case TD_DEVICE_COUNTER_CONTROL:
		if (cntr_num >= TD_DEV_GEN_COUNT_MAX)
			goto bail;
		offset = TD_DEV_GEN_COUNTER_CONTROL_DATA(cntr_num);
		break;


	case TD_DEVICE_COUNTER_TOKEN:
		if (cntr_num >= TD_DEV_TOKEN_COUNT_MAX)
			goto bail;
		offset = TD_DEV_TOKEN_COUNTER_DATA(cntr_num);
		break;

	case TD_DEVICE_COUNTER_MISC:
		if (cntr_num >= TD_DEV_MISC_COUNT_MAX)
			goto bail;
		offset = TD_DEV_MISC_COUNTER_DATA(cntr_num);
		break;
	}

	reg = &eng->td_counters.data[offset];
	*val = *reg;
	rc = 0;
bail:
	return rc;

}

int td_ioctl_device_get_counters(struct td_device *dev,
		struct td_ioctl_device_counters *cntrs, bool fill_mode)
{
	int i, rc = -EINVAL;

	WARN_TD_DEVICE_UNLOCKED(dev);

	if (fill_mode) {
		/* determine the total number of counters */
		i = TD_DEV_GEN_COUNT_MAX           /* read */
			+ TD_DEV_GEN_COUNT_MAX     /* write */
			+ TD_DEV_GEN_COUNT_MAX     /* control */
			+ TD_DEV_TOKEN_COUNT_MAX   /* token */
			+ TD_DEV_MISC_COUNT_MAX;   /* misc */

		if (cntrs->count < i) {
			/* the user buffer isn't big enough, return the count required */
			cntrs->count = i;
			rc = -ENOBUFS;
			goto error;
		}

		cntrs->count = 0; /* increase the count in loop body */

		/* fill the read counters */
		for (i = 0 ; i < TD_DEV_GEN_COUNT_MAX ; i++, cntrs->count++) {
			cntrs->entries[cntrs->count].type = TD_DEVICE_COUNTER_READ;
			cntrs->entries[cntrs->count].var = i;
			rc = td_device_get_counter(dev, cntrs->entries[cntrs->count].type,
					cntrs->entries[cntrs->count].var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the write counters */
		for (i = 0 ; i < TD_DEV_GEN_COUNT_MAX ; i++, cntrs->count++) {
			cntrs->entries[cntrs->count].type = TD_DEVICE_COUNTER_WRITE;
			cntrs->entries[cntrs->count].var = i;
			rc = td_device_get_counter(dev, cntrs->entries[cntrs->count].type,
					cntrs->entries[cntrs->count].var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the control counters */
		for (i = 0 ; i < TD_DEV_GEN_COUNT_MAX ; i++, cntrs->count++) {
			cntrs->entries[cntrs->count].type = TD_DEVICE_COUNTER_CONTROL;
			cntrs->entries[cntrs->count].var = i;
			rc = td_device_get_counter(dev, cntrs->entries[cntrs->count].type,
					cntrs->entries[cntrs->count].var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the token counters */
		for (i = 0 ; i < TD_DEV_TOKEN_COUNT_MAX ; i++, cntrs->count++) {
			cntrs->entries[cntrs->count].type = TD_DEVICE_COUNTER_TOKEN;
			cntrs->entries[cntrs->count].var = i;
			rc = td_device_get_counter(dev, cntrs->entries[cntrs->count].type,
					cntrs->entries[cntrs->count].var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the misc counters */
		for (i = 0 ; i < TD_DEV_MISC_COUNT_MAX ; i++, cntrs->count++) {
			cntrs->entries[cntrs->count].type = TD_DEVICE_COUNTER_MISC;
			cntrs->entries[cntrs->count].var = i;
			rc = td_device_get_counter(dev, cntrs->entries[cntrs->count].type,
					cntrs->entries[cntrs->count].var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		if (rc == -ENOENT)
			rc = 0;
	} else {
		for (i = 0; i < cntrs->count; i++) {
			rc = td_device_get_counter(dev, cntrs->entries[i].type,
					cntrs->entries[i].var,
					&cntrs->entries[i].val);
			if (rc)
				break;
		}
	}

error:
	return rc;
}

int td_ioctl_device_get_stats(struct td_device *dev,
		struct td_ioctl_device_stats *stats)
{
	struct td_engine *eng = td_device_engine(dev);

	memcpy(stats, &eng->td_stats,
			sizeof(*stats));

	stats->read.req_queued_cnt    = td_engine_queued_bio_reads(eng);
	stats->write.req_queued_cnt   = td_engine_queued_bio_writes(eng);

	stats->control.req_queued_cnt = eng->td_queued_ucmd_count;


	return 0;
}

static int td_ioctl_device_do_raw_buffer(struct td_device *dev, int write,
		struct td_ioctl_device_raw_buffer *raw)
{
	int rc;
	void *buf;

	/* validate the type and size */

	switch(raw->type) {
	case TD_BUF_READ:
	case TD_BUF_WRITE:
	case TD_BUF_READ_META:
	case TD_BUF_WRITE_META:
	case TD_BUF_GLOBAL_STATUS:
	case TD_BUF_EX_STATUS:
	case TD_BUF_GLOBAL_EX_STATUS:
	case TD_BUF_CMD_STATUS:
	case TD_BUF_COMMAND:
		break;
	default:
		rc = -EINVAL;
		goto error_type;
	}

	rc = -EINVAL;
	if (!raw->buf_len || raw->buf_len > TERADIMM_DATA_BUF_SIZE)
		goto error_size;

	/* allocate a bounce buffer */

	rc = -ENOMEM;
	buf = kzalloc(raw->buf_len, GFP_KERNEL);
	if (!buf)
		goto error_alloc;

	if (write) {
		rc = -EFAULT;
		if (copy_from_user(buf, raw->buf, raw->buf_len))
			goto error_copy;
		
		rc = td_eng_hal_set_raw_buffer(td_device_engine(dev),
				raw->type, raw->index,
				buf, raw->buf_len);
		raw->buf_len = rc;
	} else {
		/* get the data */
		rc = td_eng_hal_get_raw_buffer(td_device_engine(dev),
				raw->type, raw->index,
				buf, raw->buf_len);
		if (rc < 0)
			goto error_get;

		raw->buf_len = rc;

		/* copy it to user space */

		rc = -EFAULT;
		if (copy_to_user(raw->buf, buf, raw->buf_len))
			goto error_copy;
		rc = 0;
	}


error_copy:
error_get:
	kfree(buf);
error_alloc:
error_size:
error_type:
	return rc;
}

int td_ioctl_device_get_raw_buffer(struct td_device *dev,
		struct td_ioctl_device_raw_buffer *raw)
{
	return td_ioctl_device_do_raw_buffer(dev, 0, raw);
}

int td_ioctl_device_set_raw_buffer(struct td_device *dev,
		struct td_ioctl_device_raw_buffer *raw)
{
	return td_ioctl_device_do_raw_buffer(dev, 1, raw);
}

int td_ioctl_device_get_rdbufs(struct td_device *dev,
		struct td_ioctl_device_rdbufs *rdbufs)
{
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
	struct td_engine *eng = td_device_engine(dev);
	const uint64_t now = jiffies;
	int i;

	memcpy(rdbufs, &eng->rdbufs, sizeof(*rdbufs));

#define __adjust_ptr(p) { \
		void *_p = (p); \
		void *_b = (&eng->rdbufs); \
		p = (struct list_head*)((char*)_p - (char*)_b ); \
	}

	__adjust_ptr(rdbufs->_td_rd_buf_lru.next);
	__adjust_ptr(rdbufs->_td_rd_buf_lru.prev);

	for (i=0; i<TD_HOST_RD_BUFS_PER_DEV; i++) {
		struct td_rd_buf *rb;
		rb = &rdbufs->_td_rd_bufs[i];

		__adjust_ptr(rb->drb_lru_link.next);
		__adjust_ptr(rb->drb_lru_link.prev);

		rb->drb_last_used = now - rb->drb_last_used;
	}

	return 0;

#else
	return -EINVAL;
#endif
}

int td_ioctl_device_get_reg(struct td_device *dev,
		struct td_ioctl_device_get_reg *gr)
{
	int rc;
	struct td_engine *eng = td_device_engine(dev);
	uint64_t start, end;
	struct td_ucmd *ucmd;
	struct page *p;
	struct td_ioctl_device_get_reg* ret;

	start = td_get_cycles();

	rc = -EIO;
	if (!td_state_can_accept_requests(eng))
		goto setup_error;

	rc = -ENOMEM;
	ucmd = (struct td_ucmd *)kmalloc(sizeof(struct td_ucmd), GFP_KERNEL);
	if (!ucmd)
		goto ucmd_fail;

	td_ucmd_init(ucmd);

	ucmd->ioctl.data_len_to_device = 0;
	ucmd->ioctl.data_len_from_device = 4096;
	ucmd->hw_cmd = 1;

	/* Set up the command here. */

	p = alloc_page(GFP_KERNEL);
	ucmd->ioctl.data = p;
	rc = td_eng_cmdgen_chk(eng, get_reg, &ucmd->ioctl.cmd[0], gr->reg);
	if (rc)
		goto ucmdgen_fail;
	rc = td_ucmd_map(ucmd, NULL, (unsigned long) ucmd->ioctl.data);
	if (rc)
		goto bail_setup;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	ret = (struct td_ioctl_device_get_reg*)ucmd->data_virt;

	gr->value = *(uint32_t*)ucmd->data_virt;

	end = td_get_cycles();

	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;

bail_running:
bail_setup:
ucmdgen_fail:
	td_ucmd_put(ucmd);
	return rc;

ucmd_fail:
setup_error:
	return rc;
}

int td_ioctl_device_put_reg(struct td_device *dev,
		struct td_ioctl_device_put_reg *pr)
{
	struct td_engine *eng = td_device_engine(dev);
	int rc;
	uint64_t start, end;
	struct td_ucmd *ucmd;

	start = td_get_cycles();

	rc = -EIO;
	if (!td_state_can_accept_requests(eng))
		goto setup_error;

	rc = -ENOMEM;
	ucmd = (struct td_ucmd *)kmalloc(sizeof(struct td_ucmd), GFP_KERNEL);
	if (!ucmd)
		goto ucmd_fail;

	td_ucmd_init(ucmd);
	ucmd->ioctl.data_len_to_device = 0;
	ucmd->ioctl.data_len_from_device = 0;
	ucmd->ioctl.data = NULL;
	ucmd->hw_cmd = 1;


	rc = td_eng_cmdgen_chk(eng, put_reg, &ucmd->ioctl.cmd[0],
			pr->reg, pr->data, pr->mask);
	if (rc)
		goto ucmdgen_fail;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	end = td_get_cycles();

	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;

bail_running:
ucmdgen_fail:
	td_ucmd_put(ucmd);
	return rc;

ucmd_fail:
setup_error:
	return rc;
}

int td_ioctl_device_get_params(struct td_device *dev,
		struct td_ioctl_device_params *pr)
{
	struct td_engine *eng = td_device_engine(dev);
	int rc;
	uint64_t start, end;
	struct td_ucmd *ucmd;

	start = td_get_cycles();

	rc = -EIO;
	if (!td_state_can_accept_requests(eng))
		goto setup_error;

	rc = -ENOMEM;
	ucmd = (struct td_ucmd *)kmalloc(sizeof(struct td_ucmd), GFP_KERNEL);
	if (!ucmd)
		goto ucmd_fail;

	td_ucmd_init(ucmd);
	ucmd->ioctl.data_len_to_device = 0;
	ucmd->ioctl.data_len_from_device = 4096;


	ucmd->ioctl.data = pr->buf;
	rc = td_eng_cmdgen_chk(eng, get_params, &ucmd->ioctl.cmd[0], pr->page);
	if (rc)
		goto ucmdgen_fail;

	rc = td_ucmd_map(ucmd, current, (unsigned long) ucmd->ioctl.data);
	if (rc)
		goto bail_setup;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	end = td_get_cycles();

	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;

bail_running:
bail_setup:
ucmdgen_fail:
	td_ucmd_put(ucmd);
	return rc;

ucmd_fail:
setup_error:
	return rc;
}

int td_ioctl_device_set_params(struct td_device *dev,
		struct td_ioctl_device_params *pr)
{
	struct td_engine *eng = td_device_engine(dev);
	int rc;
	uint64_t start, end;
	struct td_ucmd *ucmd;

	start = td_get_cycles();

	rc = -EIO;
	if (!td_state_can_accept_requests(eng))
		goto setup_error;

	rc = -ENOMEM;
	ucmd = (struct td_ucmd *)kmalloc(sizeof(struct td_ucmd), GFP_KERNEL);
	if (!ucmd)
		goto ucmd_fail;

	td_ucmd_init(ucmd);
	ucmd->ioctl.data_len_to_device = 4096;
	ucmd->ioctl.data_len_from_device = 0;
	ucmd->ioctl.data = pr->buf;

	rc = td_eng_cmdgen_chk(eng, set_params, &ucmd->ioctl.cmd[0], pr->page);
	if (rc)
		goto ucmdgen_fail;
	rc = td_ucmd_map(ucmd, current, (unsigned long) ucmd->ioctl.data);
	if (rc)
		goto bail_setup;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	end = td_get_cycles();
	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;

bail_running:
bail_setup:
ucmdgen_fail:
	td_ucmd_put(ucmd);
	return rc;

ucmd_fail:
setup_error:
	return rc;
}

int td_ioctl_device_ata_pt (struct td_device *dev,
		struct td_ioctl_device_ssd_pt *ssd_cmd)
{
	int rc;
	/* Must be kmalloced, because kfreed int ucmd_put */
	struct td_ucmd *ucmd;
	struct td_engine *eng = td_device_engine(dev);
	uint64_t start, end;

	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOMEM;
	ucmd = kmalloc(sizeof(*ucmd), GFP_KERNEL);
	if (!ucmd)
		goto ucmd_fail;
	memset(ucmd, 0, sizeof(*ucmd));

	start = td_get_cycles();

	if (!td_state_can_accept_requests(eng)) {
		rc = -EIO;
		goto setup_error;
	}

	/* if expecting to send data, pointer and length are required */
	if (ssd_cmd->data_len_to_device) {
		if ( !(ssd_cmd->data_len_to_device <= PAGE_SIZE && ssd_cmd->data) ) {
			rc = -EINVAL;
			goto setup_error;
		}
		ucmd->ioctl.data = ssd_cmd->data;
		ucmd->ioctl.data_len_to_device = ssd_cmd->data_len_to_device;
	} else if (ssd_cmd->data_len_from_device) {
		if (! (ssd_cmd->data_len_from_device <= PAGE_SIZE  && ssd_cmd->data) ) {
			rc = -EINVAL;
			goto setup_error;
		}
		ucmd->ioctl.data_len_from_device = ssd_cmd->data_len_from_device;
		ucmd->ioctl.data = ssd_cmd->data;
	}


	// For now we only send to monet 0,    td_cmdgen_ata
	rc = td_eng_cmdgen_chk(eng, ata, &ucmd->ioctl.cmd[0], ssd_cmd->ata_cmd,
			0, 512);
	if (rc)
		goto ucmdgen_fail;

	td_ucmd_init(ucmd);

	rc = td_ucmd_map(ucmd, current, (unsigned long)ucmd->ioctl.data);
	if (rc)
		goto bail_setup;

	/*  run gets a reference for the execution thread */
	rc = td_ucmd_run(ucmd, eng);
	if (rc < 0)
		goto bail_running;

	end = td_get_cycles();
	ucmd->ioctl.cycles.ioctl.start = start;
	ucmd->ioctl.cycles.ioctl.end = end;

bail_running:
bail_setup:
ucmdgen_fail:
	td_ucmd_put(ucmd);
	return rc;

setup_error:
	kfree(ucmd);
ucmd_fail:
	return rc;
}

int td_ioctl_device_start_bio(struct td_device *dev)
{
	int rc, running, to;
	struct td_engine *eng;
	struct td_devgroup *dg;
	WARN_TD_DEVICE_UNLOCKED(dev);
	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;
	if (!dg)
		goto error_no_grp;

	rc = -EIO;
	if (!eng->bio_context)
		goto error_no_context;


	/* if the devgroup is running, we need to synchronize with */
	running = td_devgroup_is_running(dg);

	rc = td_devgroup_remove_device(dg, dev);
	if (rc)
		goto error_remove_grp;

	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	td_eng_info(eng, "Restarting BIOs\n");
	td_engine_start_bio(eng);

	/* Re-attach */
	rc = td_devgroup_add_device(dg, dev);
	if (rc)
		goto error_add_to_grp;

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* Wait for state: RUNNING */
	to = td_run_state_wait_ms(eng, RUNNING, 1000);
	if (to < 0) {
		rc = -ETIMEDOUT;
		td_eng_err(eng, "Error: Could not start BIOs; stuck in state %d\n",
				td_run_state(eng));
		goto error_init_fail;
	}


error_add_to_grp:
error_remove_grp:
error_no_context:
error_no_grp:
	return rc;

error_init_fail:
	td_devgroup_remove_device(dg, dev);
	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	/* Try to undo device removal.. */
	if(!td_devgroup_add_device(dg, dev)) {
		dev->td_devgroup = dg;
		td_devgroup_poke(dg);

	}
	return rc;
}


int td_ioctl_device_stop_bio(struct td_device *dev, void *bio_context)
{
	int rc, running, to;
	struct td_engine *eng;
	struct td_devgroup *dg;
	WARN_TD_DEVICE_UNLOCKED(dev);
	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;
	if (!dg)
		goto error_no_grp;

	rc = -EIO;
	if (!td_run_state_check(eng, RUNNING))
		goto error_engine_stop;


	/* if the devgroup is running, we need to synchronize with */
	running = td_devgroup_is_running(dg);

	rc = td_devgroup_remove_device(dg, dev);
	if (rc)
		goto error_remove_grp;

	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	td_eng_info(eng, "Stopping BIOs for maitenance operations\n");
	td_engine_stop_bio(eng, bio_context);

	/* Re-attach */
	rc = td_devgroup_add_device(dg, dev);
	if (rc)
		goto error_add_to_grp;

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* Wait for state: UCMD_ONLY */
	to = td_run_state_wait_ms(eng, UCMD_ONLY, 10000);
	if (to < 0) {
		rc = -ETIMEDOUT;
		td_eng_err(eng, "Error: Could not stop BIOs; stuck in state %d\n",
				td_run_state(eng));
		goto error_init_fail;
	}

	return rc;

error_add_to_grp:
	td_engine_start_bio(eng);

error_remove_grp:
error_engine_stop:
error_no_grp:
	return rc;

error_init_fail:
	td_devgroup_remove_device(dg, dev);
	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	td_engine_start_bio(eng);

	/* Try to undo device removal.. */
	if(!td_devgroup_add_device(dg, dev)) {
		dev->td_devgroup = dg;
		td_devgroup_poke(dg);

	}
	return rc;
}

int td_ioctl_device_lock(struct td_device *dev,
		struct td_ioctl_device_lock *lock,
		void *locker_context)
{
	int rc, running;
	struct td_engine *eng;
	struct td_devgroup *dg;
	WARN_TD_DEVICE_UNLOCKED(dev);
	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;
	if (!dg)
		goto error_no_grp;

	rc = -EIO;
	if (!td_run_state_check(eng, RUNNING) &&
			!td_run_state_check(eng, UCMD_ONLY))
		goto error_engine_stop;

	/* if the devgroup is running, we need to synchronize with */
	running = td_devgroup_is_running(dg);

	rc = td_devgroup_remove_device(dg, dev);
	if (rc)
		goto error_remove_grp;

	/* sync */
	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	/* Now we can grab what we want.. */
	rc = td_engine_lock(eng, lock, locker_context);
	if (rc)
		goto error_locking;

	/* Re-attach */
	rc = td_devgroup_add_device(dg, dev);
	if (rc)
		goto error_add_to_grp;

	/* wake up the thread */
	td_devgroup_poke(dg);


error_add_to_grp:

error_remove_grp:
error_engine_stop:
error_no_grp:
	return rc;

error_locking:
	/* Try to undo device removal.. */
	if(!td_devgroup_add_device(dg, dev)) {
		dev->td_devgroup = dg;
		td_devgroup_poke(dg);

	}
	return rc;


}

int td_ioctl_device_unlock(struct td_device *dev,
		void *locker_context)
{
	int rc, running;
	struct td_engine *eng;
	struct td_devgroup *dg;
	WARN_TD_DEVICE_UNLOCKED(dev);
	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;
	if (!dg)
		goto error_no_grp;

	if (!td_run_state_check(eng, RUNNING) &&
			!td_run_state_check(eng, UCMD_ONLY)) {
		rc = -EIO;
		goto error_engine_stop;
	}

	/* if the devgroup is running, we need to synchronize with */
	running = td_devgroup_is_running(dg);

	rc = td_devgroup_remove_device(dg, dev);
	if (rc)
		goto error_remove_grp;

	//dev->td_devgroup = NULL;

	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	/* Now we can grab what we want.. */
	rc = td_engine_unlock(eng, locker_context);

	if (rc)
		printk("ERROR: Unlock failed!");
	/* Re-attach only if there is a group. */
	if (dg)
		rc = td_devgroup_add_device(dg, dev);

	if (rc)
		goto error_add_to_grp;

	/* success */
	//dev->td_devgroup = dg;

	/* wake up the thread */
	td_devgroup_poke(dg);

error_add_to_grp:
error_remove_grp:
error_engine_stop:
error_no_grp:
	return rc;
}


int td_ioctl_device_go_online (struct td_device *dev)
{
	return td_device_go_online(dev);
}

int td_ioctl_device_go_offline (struct td_device *dev)
{
	return td_device_go_offline(dev);
}
int td_ioctl_device_attach (struct td_device *dev,
		const char *devgroup)
{
	return td_device_attach(dev, devgroup);
}

int td_ioctl_device_reset (struct td_device *dev)
{
	return td_device_reset(dev);
}
/*
 * This is a debug only ioctl, really, a device should
 * never not be in a running group.  Low level tools
 * can use this, but middleware won't expose it
 */
int td_ioctl_device_detach (struct td_device *dev)
{
	return td_device_detach(dev);
}

int td_ioctl_device_global_ext_status (struct td_device *dev,
		struct td_ioctl_device_global_ext_status *gstatus)
{
	int rc;
	struct td_devgroup *dg;
	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOENT;
	dg = dev->td_devgroup;
	if (!dg)
		goto error;

	gstatus->u64 = -1;
	mb();

	rc = td_eng_hal_read_ext_status(td_device_engine(dev),
			TERADIMM_EXT_STATUS_GLOBAL_IDX, &gstatus->u64, 0);

error:
	return rc;
}


int td_ioctl_device_set_errors (struct td_device *dev,
		struct td_ioctl_device_error_injection *conf)
{
	td_device_engine(dev)->td_errors_mask = 0;
	mb();
	td_device_engine(dev)->td_errors_count = 0;
	mb();
printk("SET ERRORS_COUNT to %llu\n", conf->errors_count);
	td_device_engine(dev)->td_errors_count = conf->errors_count;
	mb();
printk("SET ERRORS_MASK to %016llx\n", conf->errors_mask);
	td_device_engine(dev)->td_errors_mask = conf->errors_mask;
	mb();
	return 0;
}

int td_ioctl_device_get_errors (struct td_device *dev,
		struct td_ioctl_device_error_injection *conf)
{
	conf->errors_mask = td_device_engine(dev)->td_errors_mask;
	conf->errors_count = td_device_engine(dev)->td_errors_count;
	return 0;
}

int td_ioctl_device_get_ecc_counters(struct td_device *dev,
		struct td_ioctl_device_ecc_counters *cntrs)
{
	struct td_engine *eng = td_device_engine(dev);
	const struct td_bin *bp;

	switch (cntrs->type) {
		case TD_IOCTL_DEV_ECC_CNT_DDR3_SECS:
			bp = (const struct td_bin*)&eng->ecc_bins.ddr3.secs;
			break;
		case TD_IOCTL_DEV_ECC_CNT_DDR3_HOURS:
			bp = (const struct td_bin*)&eng->ecc_bins.ddr3.hours;
			break;
		case TD_IOCTL_DEV_ECC_CNT_INTERNAL_SECS:
			bp = (const struct td_bin*)&eng->ecc_bins.internal.secs;
			break;
		case TD_IOCTL_DEV_ECC_CNT_INTERNAL_HOURS:
			bp = (const struct td_bin*)&eng->ecc_bins.internal.hours;
			break;
		default:
			return -EINVAL;
	}

	cntrs->num_counters = td_monitor_get_bin_counts(bp, cntrs->counters,
						    cntrs->num_counters);
	cntrs->threshold    = bp->threshold;
	cntrs->status       = bp->difference > bp->threshold ? bp->difference : 0;

	return 0;
}


int td_ioctl_device_flush(struct td_device *dev)
{
	struct td_engine *eng = td_device_engine(dev);
	int rc = 0;
	volatile uint64_t completed;
	uint64_t done, timeout;

	completed = eng->td_stats.write.req_completed_cnt;

	done = eng->td_stats.write.req_active_cnt
		+ eng->td_stats.write.req_queued_cnt
		+ eng->td_stats.write.req_completed_cnt;

	timeout = 1000000;
	while(timeout > 0 && done > completed ) {

		completed = eng->td_stats.write.req_completed_cnt;
		udelay(1);
		timeout--;
	}

	return rc;
}

int td_raid_get_conf(struct td_raid *dev, enum td_raid_conf_type type,
					 uint32_t conf_num, uint64_t *val)  {
	int rc = -ENOENT;
	uint64_t reg = 0ULL;
	WARN_TD_DEVICE_UNLOCKED(dev);

	if (type >= TD_RAID_CONF_MAX)
		goto bail;

	switch (type) {
	default:
		goto bail;
	case TD_RAID_CONF_GENERAL:
		if (conf_num >= TR_CONF_GENERAL_MAX)
			goto bail;
		reg = dev->conf.general[conf_num];
		break;
	case TD_RAID_CONF_OPS:
		if (dev->ops->_get_conf) {
			rc = dev->ops->_get_conf(dev, conf_num, &reg);
			if (rc) 
				goto bail;
		}
		break;
	}

	*val = reg;
	rc = 0;

bail:
	return rc;
}


int td_ioctl_raid_get_conf(struct td_raid *dev,
		struct td_ioctl_conf *conf, bool fill_mode)
{
	int i, rc = -EINVAL;
	int ops_max = 0;
	enum td_raid_level raid_level;

	WARN_TD_DEVICE_UNLOCKED(dev);

	if (fill_mode) {
		/* determine the total number of configuration options */
		i = TR_CONF_GENERAL_MAX;
		raid_level = (enum td_raid_level)tr_conf_var_get(dev, LEVEL);
		switch (raid_level) {
		case TD_RAID_STRIPE:
			ops_max = TR_CONF_STRIPE_MAX;
			i += TR_CONF_STRIPE_MAX;
			break;
		case TD_RAID_MIRROR:
			ops_max = TR_CONF_MIRROR_MAX;
			i += TR_CONF_MIRROR_MAX;
			break;
		case TD_RAID_10:
		case TD_RAID_UNKNOWN:
			/* break */;
		}

		if (conf->count < i) {
			/* the user buffer isn't big enough, return the count required */
			conf->count = i;
			rc = -ENOBUFS;
			goto error;
		}

		conf->count = 0; /* increase the count in loop body */

		/* fill the general conf */
		for (i = 0 ; i < TR_CONF_GENERAL_MAX ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_RAID_CONF_GENERAL;
			conf->entries[conf->count].var = i;
			rc = td_raid_get_conf(dev, (enum td_raid_conf_type)conf->entries[conf->count].type,
					conf->entries[conf->count].var,
					&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}

		/* fill the ops conf */
		for (i = 0 ; i < ops_max ; i++, conf->count++) {
			conf->entries[conf->count].type = TD_RAID_CONF_OPS;
			conf->entries[conf->count].var = i;
			rc = td_raid_get_conf(dev, (enum td_raid_conf_type)conf->entries[conf->count].type,
					conf->entries[conf->count].var,
					&conf->entries[conf->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;
		}
	}
	else {
		for (i = 0; i < conf->count; i++) {
			rc = td_raid_get_conf(dev, conf->entries[i].type,
					conf->entries[i].var,
					&conf->entries[i].val);
			if (rc)
				break;
		}
	}

error:
	return rc;
}
