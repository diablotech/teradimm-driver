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
#include "td_device.h"
#include "td_engine.h"
#include "td_devgroup.h"
#include "td_ucmd.h"
#include "td_eng_hal.h"
#include "td_ioctl.h"
#include "td_params.h"
#include "td_monitor.h"

static void td_monitor_get_ecc_counts(struct td_engine *eng,
		const struct td_param_page4_map *param)
{
	eng->ecc_bins.ddr3.curr_count     = param->mEccErrInfo.HOSTECCErrCnt.u32;

	eng->ecc_bins.internal.curr_count = param->mEccErrInfo.HISCEccErrCnt.u32
					  + param->mEccErrInfo.CPMDMAErrCnt.u32;
}

static void td_monitor_start_read(struct td_ucmd *ucmd, struct td_engine *eng)
{
	// like td_ucmd_run but without the wait
	td_ucmd_ready(ucmd);
	td_ucmd_get(ucmd);
	td_enqueue_ucmd(eng, ucmd);
	td_engine_poke(eng);
}

static int td_monitor_read_ecc_counts(struct td_engine *eng)
{
	int rc;
	struct td_ucmd *ucmd;

	eng->ecc_bins.ucmd = NULL;

	rc = -EBUSY;
	if (td_state_upgrade_in_progress(eng))
		goto upgrading;

	rc = -EFAULT;
	if (!td_state_can_issue_monitor_requests(eng))
		goto wrong_state;

	rc = -ENOMEM;
	ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
	if (!ucmd) {
		td_eng_err(eng, "Couldn't allocate UCMD for ecc_counts\n");
		goto nomem;
	}

	ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;
	rc = td_eng_cmdgen(eng, get_params, ucmd->ioctl.cmd, 4);
	if (unlikely(rc)) {
		td_eng_err(eng, "Could not gencmd get_params page 4, rc = %d\n", rc);
		goto gen_cmd_failed;
	}

	td_monitor_start_read(ucmd, eng);
	eng->ecc_bins.ucmd = ucmd;
	return 0;

gen_cmd_failed:
	/* We have an unwanted command.. take it back! */
	td_ucmd_put(ucmd);
nomem:
wrong_state:
upgrading:

	return rc;
}

static int td_monitor_finish_read(struct td_engine *eng)
{
	int rc;
	struct td_ucmd *ucmd = eng->ecc_bins.ucmd;

	if (! ucmd)
		return -EIO;

	rc = td_ucmd_wait(ucmd);
	if (rc) {
		td_eng_err(eng, "Waiting for get_params page 4 failed, rc = %d\n", rc);
	} else {
		td_monitor_get_ecc_counts(eng, ucmd->data_virt);
	}

	td_ucmd_put(ucmd);
	eng->ecc_bins.ucmd = NULL;

	return rc;
}

//============================================================================

// read ecc_alarm from params page 1
static int td_monitor_read_ecc_alarm(struct td_engine *eng)
{
	int rc;
	struct td_ucmd *ucmd;
	const struct td_param_page1_map *param;

	if (!td_state_can_accept_requests(eng)) {
		if (td_run_state_check(eng, TERMINATING)) {
			schedule_timeout_interruptible(1 + (HZ/100));
			return -EFAULT;
		}
		td_eng_warn(eng, "request in state %d (ecc_alarm)\n", td_run_state(eng));
		return -EIO;
	}

	ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
	if (!ucmd)
		return -ENOMEM;

	ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;
	rc = td_eng_cmdgen(eng, get_params, ucmd->ioctl.cmd, 1);
	if (rc) {
		td_eng_err(eng, "Could not gencmd get_params page 1, rc = %d\n", rc);
		goto error;
	}
	rc = td_ucmd_run(ucmd, eng);
	if (rc) {
		td_eng_err(eng, "Could not run get_params page 1, rc = %d\n", rc);
		goto error;
	}

	param = ucmd->data_virt;

	eng->ecc_bins.ecc_alarm = param->ecc_alarm.value;

error:
	if (ucmd)
		td_ucmd_put(ucmd);
	return rc;
}

// write ecc_alarm to params page 1
static int td_monitor_write_ecc_alarm(struct td_engine *eng)
{
	int rc;
	struct td_ucmd *ucmd;
	struct td_param_page1_map *param;

	if (!td_state_can_accept_requests(eng)) {
		td_eng_warn(eng, "request in state %d\n", td_run_state(eng));
		return -EIO;
	}

	ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
	if (!ucmd)
		return -ENOMEM;

	memset(ucmd->data_virt, 0, TERADIMM_DATA_BUF_SIZE);

	param = ucmd->data_virt;
	param->ecc_alarm.value = eng->ecc_bins.ecc_alarm;
	param->ecc_alarm.set = 1;

	ucmd->ioctl.data_len_to_device = TERADIMM_DATA_BUF_SIZE;
	ucmd->ioctl.data_len_from_device = 0;
	ucmd->ioctl.data = param;
	rc = td_eng_cmdgen(eng, set_params, ucmd->ioctl.cmd, 1);
	if (rc) {
		td_eng_err(eng, "Could not gencmd set_params, rc = %d\n", rc);
		goto error;
	}
	rc = td_ucmd_run(ucmd, eng);
	if (rc) {
		td_eng_err(eng, "Could not run set_params, rc = %d\n", rc);
		goto error;
	}

error:
	if (ucmd)
		td_ucmd_put(ucmd);
	return rc;
}

//============================================================================

// restore PFA for internal ECC alarm
static void td_monitor_restore_ecc_alarm_state(struct td_engine *eng)
{
	// skip when ecc_alarm has already been read from params page 1
	if (eng->ecc_bins.ecc_alarm_read)
		return;

	// read ecc_alarm from params page 1
	td_monitor_read_ecc_alarm(eng);

	if (eng->ecc_bins.ecc_alarm & ECC_ALARM_INTERNAL_INVALID) {
		// set ecc_alarm for the first time
		eng->ecc_bins.ecc_alarm = 0;
		td_monitor_write_ecc_alarm(eng);
	}

	if (eng->ecc_bins.ecc_alarm & ECC_ALARM_INTERNAL_10_SECOND) {
		struct td_bin *bp = (struct td_bin*)&eng->ecc_bins.internal.secs;
		bp->difference = bp->threshold + 1;
	}

	if (eng->ecc_bins.ecc_alarm & ECC_ALARM_INTERNAL_24_HOUR) {
		struct td_bin *bp = (struct td_bin*)&eng->ecc_bins.internal.hours;
		bp->difference = bp->threshold + 1;
	}

	eng->ecc_bins.ecc_alarm_read = 1;
}

// save PFA for internal ECC alarm
static void td_monitor_save_ecc_alarm_state(struct td_engine *eng)
{
	struct td_bin *bp;
	uint32_t new_ecc_alarm = eng->ecc_bins.ecc_alarm;

	bp = (struct td_bin*)&eng->ecc_bins.internal.secs;
	if (bp->difference > bp->threshold) {
		new_ecc_alarm |= ECC_ALARM_INTERNAL_10_SECOND;
	}

	bp = (struct td_bin*)&eng->ecc_bins.internal.hours;
	if (bp->difference > bp->threshold) {
		new_ecc_alarm |= ECC_ALARM_INTERNAL_24_HOUR;
	}

	if (eng->ecc_bins.ecc_alarm != new_ecc_alarm) {
		eng->ecc_bins.ecc_alarm = new_ecc_alarm;

		td_eng_warn(eng, "ECC ALARM %u\n", new_ecc_alarm);
		td_monitor_write_ecc_alarm(eng);
	}
}

//============================================================================

static void td_monitor_bin_init(struct td_bin *bp, uint64_t threshold)
{
	bp->index = 0;
	bp->num_valid = 0;
	bp->triggered = 0;
	bp->threshold = threshold;
	bp->difference = 0;
}

void td_monitor_bins_init(struct td_bins *bins)
{
	bins->curr_count = 0;
	bins->second_count = 0;
	td_monitor_bin_init((struct td_bin*)&bins->secs, 16);
	td_monitor_bin_init((struct td_bin*)&bins->hours, 256);
}

static void td_monitor_bin_reset(struct td_bin *bp)
{
	bp->index = 0;
	bp->num_valid = 0;
	bp->triggered = 0;
}

int td_monitor_get_bin_counts(const struct td_bin *bp, uint64_t *counts, int limit)
{
	int nc;
	int nv = bp->num_valid;
	int i  = bp->index;

	if (i >= nv) i = 0;  // wrap around

	for (nc = 0; nc < nv && nc < limit; nc++) {
		*counts++ = bp->counts[i++];
		if (i >= nv) i = 0;  // wrap around
	}
	return nc;
}


//============================================================================

static int td_monitor_update_bin_counts(uint64_t curr_count,
		struct td_bin *bp, int num_bins)
{
	int triggered;
	int nv = bp->num_valid;
	int i  = bp->index;
	uint64_t last_count = bp->counts[(i >= nv) ? 0 : i];
	int64_t difference;

	bp->counts[i++] = curr_count;
	if (i >= num_bins) i = 0;  // wrap around
	bp->index = i;

	if (nv < num_bins) {
		if (bp->num_valid++ == 0) {
			// never trigger the very first time
			last_count = curr_count;
		}
	}

	// Beware curr_count resets to zero during online upgrade
	difference = curr_count - last_count;

	if (difference > (int64_t)bp->difference)
		bp->difference = difference;  // keep max difference

	// does difference exceed threshold?
	triggered = difference > bp->threshold;

	if (triggered != bp->triggered) {
		bp->triggered = triggered;

		if (bp->action) bp->action(bp);
	}
	return triggered;
}

static int td_monitor_update_bin_seconds(struct td_bins *bins)
{
	struct td_bin_secs *bp = &bins->secs;

	return td_monitor_update_bin_counts(bins->curr_count,
			(struct td_bin*)bp, ARRAY_SIZE(bp->counts));
}

static int td_monitor_update_bin_hours(struct td_bins *bins)
{
	struct td_bin_hours *bp = &bins->hours;

	return td_monitor_update_bin_counts(bins->curr_count,
			(struct td_bin*)bp, ARRAY_SIZE(bp->counts));
}

static void td_monitor_update_bins(struct td_engine *eng,
		struct td_bins *bins)
{
	// reset seconds bins when updates have been stalled too long
	if (time_after(jiffies, bins->last_time + 3 * TD_MONITOR_PERIOD * HZ)) {
		struct td_bin *bp = (struct td_bin*)&bins->secs;
		td_monitor_bin_reset(bp);
	}
	bins->last_time = jiffies;

	td_monitor_update_bin_seconds(bins);

	if (--bins->second_count <= 0) {
		bins->second_count = 3600;
		td_monitor_update_bin_hours(bins);
	}
}

static void td_monitor_wait_ecc_counts(struct td_engine *eng)
{
	int rc = td_monitor_finish_read(eng);
	if (rc) {
		if (rc != -EIO)
			td_eng_warn(eng, "ECC read rc = %d\n", rc);
	} else {
		td_monitor_restore_ecc_alarm_state(eng);

		td_monitor_update_bins(eng, &eng->ecc_bins.ddr3);
		td_monitor_update_bins(eng, &eng->ecc_bins.internal);

		td_monitor_save_ecc_alarm_state(eng);
	}
}

static int td_monitor_read_ecc_counts_iter(struct td_device *dev, void *data)
{
	struct td_engine *eng = td_device_engine(dev);
	struct list_head *wait_list = data;

	int rc = td_monitor_read_ecc_counts(eng);

	if (rc == 0) {
		td_device_hold(td_engine_device(eng));
		list_add_tail(&eng->ecc_bins.wait_link, wait_list);
	}
	return 0;
}

void td_monitor_ecc_poll_all_devs(void)
{
	struct td_engine *eng, *tmp;
	struct list_head wait_list;  // list of engines with ucmd to wait on

	INIT_LIST_HEAD(&wait_list);

	// loop over td_device_list and
	// accumulate an list of engines with ucmds to wait on
	td_device_list_iter(td_monitor_read_ecc_counts_iter, &wait_list);

	// loop over list of engines with ucmds to wait on
	list_for_each_entry_safe(eng, tmp, &wait_list, ecc_bins.wait_link) {

		td_monitor_wait_ecc_counts(eng);

		list_del(&eng->ecc_bins.wait_link);
		td_device_put(td_engine_device(eng));
	}
}
