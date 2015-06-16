/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("DIABLO").  All       *
 *    rights reserved.                                                   *
 *                                                                       *
 *    This software is being licensed under a dual license, at Diablo's  *
 *    sole discretion.                                                   *
 *                                                                       *
 *    GPL License                                                        *
 *                                                                       *
 *    If you do not have explicit permission from Diablo, then you may   *
 *    only redistribute it and/or modify it under the terms of the GNU   *
 *    General Public License as published by the Free Software           *
 *    Foundation; either version 2 of the License, or (at your option)   *
 *    any later version located at <http://www.gnu.org/licenses/>.  See  *
 *    the GNU General Public License for more details.                   *
 *                                                                       *
 *    BSD License                                                        *
 *                                                                       *
 *    If you have explicit permission from Diablo, then redistribution   *
 *    and use in source and binary forms, with or without modification,  *
 *    are permitted provided that the following conditions are met:      *
 *                                                                       *
 *        * Redistributions of source code must retain the above         *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer.                                                    *
 *                                                                       *
 *        * Redistributions in binary form must reproduce the above      *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer in the documentation and/or other materials         *
 *        provided with the distribution.                                *
 *                                                                       *
 *        * Neither the name of the DIABLO nor the names of its          *
 *        contributors may be used to endorse or promote products        *
 *        derived from this software without specific prior written      *
 *        permission.                                                    *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL         *
 *    DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE   *
 *    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS      *
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,       *
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING           *
 *    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS *
 *    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.       *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_device.h"
#include "td_devgroup.h"
#include "td_ioctl.h"
#include "td_engine.h"
#include "td_eng_conf_sysfs.h"
#include "td_eng_conf.h"
#include "td_raid.h"
#include "td_compat.h"
#include "td_ucmd.h"
#include "td_eng_hal.h"
#include "td_discovery.h"
#include "td_dev_ata.h"
#include "td_memspace.h"
#include "td_bio.h"

#include "tr_mirror.h"

static int DEBUG_MIRROR_PRINTK = 0;
module_param_named(DEBUG_MIRROR_PRINTK, DEBUG_MIRROR_PRINTK, uint, 0644);

struct tr_mirror_bio_state {
	struct td_raid *rdev;
	td_bio_ref obio;
	
	unsigned bio_count;
	
	/* Read / Write state */
	union {
		struct {
			unsigned dup;
			unsigned next_dev;
		} write;
		struct {
			unsigned dup;
			unsigned next_dev;
		} read;
	};
};

static void tr_mirror_optimal_read (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_mirror_bio_state *trbs = opaque;
	struct td_raid *rdev = trbs->rdev;
	struct tr_member *trm;
	unsigned dev;
	int rc;

	if (DEBUG_MIRROR_PRINTK) td_raid_warn(rdev, "MIRROR %p READ %u/%u\n", bio, trbs->bio_count+1, bg->sr_parts);
	
	/* For reads, we could do better to pick a random member */
	dev = atomic_inc_return(&tr_mirror(rdev)->last_read_dev) % tr_conf_var_get(rdev, MEMBERS);

	trm = rdev->tr_members + dev;
	BUG_ON(! trm->trm_device);

	if (DEBUG_MIRROR_PRINTK) td_raid_warn(rdev, "  DEV [%u] %s SECTOR %llu (%u bytes)\n",
			dev, td_device_name(trm->trm_device),
			td_bio_get_sector_offset(bio),
			td_bio_get_byte_size(bio)
			);

	rc = td_engine_queue_bio(td_device_engine(trm->trm_device), bio);
	if (unlikely(rc))
		td_bio_endio(td_device_engine(trm->trm_device), bio, rc, 0);

	trbs->bio_count++;
}

static void tr_mirror_optimal_write (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_mirror_bio_state *trbs = opaque;
	struct td_raid *rdev = trbs->rdev;
	struct tr_member *trm;
	int rc;

	if (DEBUG_MIRROR_PRINTK) td_raid_warn(rdev, "MIRROR %p WRITE %u/%u\n", bio, trbs->bio_count+1, bg->sr_parts);

	/* Based on bio_count, we distribute the parts */
	trm = rdev->tr_members + trbs->write.next_dev;
	BUG_ON (!trm->trm_device);

	if (DEBUG_MIRROR_PRINTK) td_raid_warn(rdev, "  DEV [%u] %s SECTOR %llu (%u bytes)\n",
			trbs->bio_count, td_device_name(trm->trm_device),
			td_bio_get_sector_offset(bio),
			td_bio_get_byte_size(bio)
			);

	rc = td_engine_queue_bio(td_device_engine(trm->trm_device), bio);
	if (unlikely(rc))
		td_bio_endio(td_device_engine(trm->trm_device), bio, rc, 0);

	trbs->bio_count++;
	if (++trbs->write.next_dev >= trbs->write.dup)
		trbs->write.next_dev = 0;
}

static void tr_mirror_degraded_io (struct td_biogrp *grp, td_bio_ref bio, void*data)
{
	struct td_engine *eng = data;
	int rc;

	if (DEBUG_MIRROR_PRINTK) td_raid_warn(td_engine_device(eng)->td_raid,
			"DEGRADED %s to %s\n",
			td_bio_is_write(bio) ? "WRITE" : "READ",
			td_eng_name(eng));

	rc = td_engine_queue_bio(eng, bio);
	if (unlikely(rc))
		td_bio_endio(eng, bio, rc, 0);
}


static int tr_mirror_fail_member (struct td_raid *rdev, int idx)
{
	if (tr_check_run_state(rdev, OPTIMAL) ) {
		td_raid_warn(rdev, "mirror now in degraded state\n");
		tr_enter_run_state(rdev, DEGRADED);
	} else if (TR_ACTIVESET_EMPTY(rdev)) {
		if (! tr_check_run_state(rdev, FAILED) )
			td_raid_err(rdev, "mirror now in failed state\n");
		tr_enter_run_state(rdev, FAILED);
	}
	return 0;
}

static int tr_mirror_part_error (struct td_engine *eng, td_bio_ref bio, 
		int result, cycles_t ts)
{
	struct td_raid *rdev = td_engine_device(eng)->td_raid;
	struct tr_member *trm;
	int idx;
	
	BUG_ON(!rdev);

	if (DEBUG_MIRROR_PRINTK) printk("BIO PART ERROR on %s (%p)\n", td_device_name(td_engine_device(eng)), td_engine_device(eng));
	/*
	 * We had an error - 1st thing is to make that engine no longer active
	 */
	for (idx = 0; idx < tr_conf_var_get(rdev, MEMBERS); idx++) {
		trm = rdev->tr_members + idx;
		if (td_engine_device(eng) == trm->trm_device) {
			/* Found our index */
			td_raid_fail_member(rdev, idx);
			break;
		}
		trm = NULL;
	}
	
	if (tr_check_run_state(rdev, FAILED)) {
		td_raid_err(rdev, "RAID FAILED\n");
		return result;
	}
	/*
	 * From this point, we're stopping the normal biogrp piece from being
	 * an error
	 */

	if (td_bio_is_write(bio) ) {
		td_eng_warn(eng, "RAID WRITE ERROR; ignoring raid error\n");
		/* And call td_bio_endio with this piece successfully now. */
		td_bio_endio(eng, bio, 0, ts);
	} else {
		/*
		 * We have a failed read - we need to see if we can re-submit
		 * it on the other member
		 */
		int partner_idx = (idx ? 0 : 1);
		trm = rdev->tr_members + partner_idx;

		td_raid_info(rdev, "Retry READ on %d: \"%s\"\n", partner_idx,
				td_device_name(trm->trm_device));

		/* And the new engine will endio this piece later*/
		td_engine_queue_bio(td_device_engine(trm->trm_device), bio);

	}

	/* Returning 0 here, stopping biogrp endio from recording this error */
	return 0;
};

/*
 * Optimal IO
 *  - If it's a write, it needs to be duplicated and then go to both
 *    members of the raid
 *  - If it's a ready, it only needs to go to one member.  Balancing
 *    how it's distributed is in the optimal_read function
 */
int tr_mirror_request_optimal (struct td_raid *rdev, td_bio_ref bio)
{
	struct td_biogrp_options opts;
	struct tr_mirror_bio_state state;
	int rc;

	state.rdev = rdev;
	state.obio = bio;
	state.bio_count = 0;

	opts.split_size = TD_PAGE_SIZE;
	opts.error_part = tr_mirror_part_error;

	if (td_bio_is_write(bio)) {
		opts.submit_part = tr_mirror_optimal_write;
		state.write.dup = tr_conf_var_get(rdev, MEMBERS);
		state.write.next_dev = 0;
		opts.duplicate_count = state.write.dup;
	} else {
		opts.submit_part = tr_mirror_optimal_read;
		opts.duplicate_count = 1;
	}

	rc = td_biogrp_create(bio, &opts, &state);
	if (rc < 0) {
		td_raid_warn(rdev, "Could not split BIO for mirror\n");
		return -EIO;
	}

	return 0;

}

/*
 * Handle a mirror request when we are not optimal.
 * - We need to pick a device to use, and send all IO there
 */
int tr_mirror_request_degraded (struct td_raid *rdev, td_bio_ref bio)
{
	struct tr_member *trm;
	struct td_biogrp_options opts;
	int rc;
	
	/* Are we already FAILED? */
	if (tr_check_run_state(rdev, FAILED))
		return -EIO;

	/*
	 * We are in degraded mode, this needs to just go to one device
	 * If we can't find a device, we go to failed...
	 */
	opts.split_size = TD_PAGE_SIZE;
	opts.duplicate_count = 1;
	opts.submit_part = tr_mirror_degraded_io;
	opts.error_part = tr_mirror_part_error;

	for (rc = 0; rc < tr_conf_var_get(rdev, MEMBERS); rc++) {
		trm = rdev->tr_members + rc;
		if (trm->trm_state == TR_MEMBER_ACTIVE) {
			if (DEBUG_MIRROR_PRINTK)
				td_raid_warn(rdev, "Degraded IO to %s\n", 
					td_device_name(trm->trm_device));
			rc = td_biogrp_create(bio, &opts,
					td_device_engine(trm->trm_device));

			if (rc < 0)
				td_raid_warn(rdev, "Could not split BIO for degraded mirror\n");

			return rc;
		}
	}
	
	if (tr_check_run_state(rdev, DEGRADED) ) {
		/* This way we only print it once */
		td_raid_err(rdev, "Could not find ACTIVE member for degraded mirror\n");

	}
	/* If we're here, we're done... */
	tr_enter_run_state(rdev, FAILED);
	return -EIO;
}

/*
 * Syncing IO
 *  - While syncing is going on, all READ IO goes to ACTIVE, as in DEGRADED
 *  - Writes still go to both, as in OPTIMAL.
 *  - We take the lock only writes, so we can co-ordinate with
 *    the resync thread to make sure our writes aren't mixed with his
 */
static int tr_mirror_request_syncing (struct td_raid *rdev, td_bio_ref bio)
{
	int rc;
	

	if (td_bio_is_write(bio)) {
		/* 
		* Writes go to both, just as if OPTIMAL, but with through the
		* resync_context stuff.
		* The resync stuff must be under the resync lock
		*/
		spin_lock_bh(&rdev->resync_context.trs_bio_lock);
		if (rdev->resync_context._trs_queue_bio) {
			rc = rdev->resync_context._trs_queue_bio(rdev, bio);
			spin_unlock_bh(&rdev->resync_context.trs_bio_lock);
		} else {
			spin_unlock_bh(&rdev->resync_context.trs_bio_lock);
			rc = tr_mirror_request_optimal(rdev, bio);
		}
	} else {
		/*
		 * Reads always just go to the ACTIVE sync source, as
		 * DEGRADED.
		 */
		 rc = tr_mirror_request_degraded(rdev, bio);
	}

	/* Return the result */
	return rc;
}



/* --- RAID ops ---*/
int tr_mirror_request (struct td_raid *rdev, td_bio_ref bio)
{
	if (likely(tr_check_run_state(rdev, OPTIMAL)) )
		return tr_mirror_request_optimal(rdev, bio);

	else if (rdev->resync_context.resync_task) 
		return tr_mirror_request_syncing(rdev, bio);
	else
		return tr_mirror_request_degraded(rdev, bio);

	
}

int tr_mirror_init (struct td_raid *rdev)
{
	struct tr_mirror *rm;
	
	if (tr_conf_var_get(rdev, MEMBERS) != 2) {
		td_raid_err(rdev, "ERROR: Cannot create mirror with %llu members\n",
				tr_conf_var_get(rdev, MEMBERS) );
		return -EINVAL;
	}

	rm = kzalloc(sizeof(struct tr_mirror), GFP_KERNEL);
	if (!rm)
		return -ENOMEM;
	
	/* We need to pick something */
	atomic_set(&rm->last_read_dev, 0);

	rdev->ops_priv = rm;
	rdev->ops_counter_max = TR_MIRROR_COUNT_MAX;

	spin_lock_init(&rdev->resync_context.trs_bio_lock);
	bio_list_init(&rdev->resync_context.trs_bios);

	return 0;
	
}

static int tr_mirror_destroy (struct td_raid *rdev)
{
	if (rdev->ops_priv)
		kfree (rdev->ops_priv);
	return 0;
}

static int tr_mirror_check_member (struct td_raid *rdev, struct td_device *dev)
{
	struct td_engine *eng = td_device_engine(dev);

	if (TR_MEMBERSET_EMPTY(rdev) && ! tr_conf_var_get(rdev, CAPACITY)) {
		/* If this is the 1st device, it dictates RAID block_params */
		td_raid_warn(rdev, "RAID CONF not specified, defaulting to values from %s\n", td_device_name(dev));

		tr_conf_var_set(rdev, CAPACITY, td_engine_capacity(eng));

		tr_conf_var_set(rdev, BIO_SECTOR_SIZE,
			td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE));
		tr_conf_var_set(rdev, HW_SECTOR_SIZE,
			td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));

		if (! tr_conf_var_get(rdev, BIO_MAX_BYTES) )
				tr_conf_var_set(rdev, BIO_MAX_BYTES,
					td_eng_conf_var_get(eng, BIO_MAX_BYTES));
		
	} else {
		/*
		* This new device must match the current raid block_params,
		* or * not be allowed to join the raid
		*/
		if (tr_conf_var_get(rdev, CAPACITY) > td_engine_capacity(eng)
				|| tr_conf_var_get(rdev, BIO_SECTOR_SIZE) != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE)
				|| tr_conf_var_get(rdev, HW_SECTOR_SIZE) != td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) ) {
			return -EINVAL;
		}
	}

	return 0;
}

static int tr_mirror_handle_member (struct td_raid *rdev, int idx)
{
	struct tr_member *trm = rdev->tr_members + idx;
	td_raid_info(rdev, "Handle device %d change: %s\n", idx,
			trm->trm_device ? td_device_name(trm->trm_device) : "<empty>");

	if (trm->trm_state == TR_MEMBER_SYNC) {
		if (! TR_ACTIVESET_EMPTY(rdev)) {
			if (tr_mirror_resync(rdev) == 0)
				return 0;

			td_raid_err(rdev, "RESYNC ATTEMPT failed\n");
		} else {
			td_raid_err(rdev, "No ACTIVE for resync\n");
		}
		tr_raid_member_enter_state(trm, FAILED);
	}

	if (TR_ACTIVESET_FULL(rdev)) {
		td_raid_info(rdev, "activeset full, OPTIMAL\n");
		tr_enter_run_state(rdev, OPTIMAL);
	} else if (! TR_ACTIVESET_EMPTY(rdev)) {
		if (!tr_check_run_state(rdev, DEGRADED) ) {
			td_raid_info(rdev, "activeset not full, DEGRADED\n");
			tr_enter_run_state(rdev, DEGRADED);
			tr_mirror_resync(rdev);
		}
	} else {
		if (!tr_check_run_state(rdev, FAILED) ) {
			td_raid_info(rdev, "activeset empty, FAILED\n");
			tr_enter_run_state(rdev, FAILED);
		}
	}
	return 0;
}


int tr_mirror_online (struct td_raid *rdev)
{
	/* We need to have at least 1 *active* member */
	if (TR_ACTIVESET_EMPTY(rdev) ) {
		td_raid_err(rdev, "Mirror cannot go online, all members missing\n");
		return -EINVAL;
	}

	if (! TR_MEMBERSET_FULL(rdev) ) {
		td_raid_warn(rdev, "Going online with missing devices; forcing DEGRADED\n");
	}

	return 0;
}

int tr_mirror_resync(struct td_raid *rdev)
{
	if (rdev->resync_context.resync_task)
		return 0;

	rdev->resync_context.resync_task = kthread_create(tr_mirror_resync_thread,
			rdev, TD_THREAD_NAME_PREFIX"%s/resync", td_raid_name(rdev));

	if (unlikely(IS_ERR_OR_NULL(rdev->resync_context.resync_task))) {
		int rc = PTR_ERR(rdev->resync_context.resync_task);
		rdev->resync_context.resync_task = NULL;
		td_raid_err(rdev, "ERROR: Resync thread rc = %d\n", rc);
		return rc ?: -EFAULT;
	}

	wake_up_process(rdev->resync_context.resync_task);

	return 0;
}

int tr_mirror_get_counter (struct td_raid *rdev, uint32_t var, uint64_t *val)
{
	if (var < TR_MIRROR_COUNT_MAX) {
		*val = tr_mirror(rdev)->counter[var];
		return 0;
	}

	return -EINVAL;
}

struct td_raid_ops tr_mirror_ops = {
	._init                   = tr_mirror_init,
	._destroy                = tr_mirror_destroy,
	._check_member           = tr_mirror_check_member,
	._handle_member          = tr_mirror_handle_member,
	._fail_member            = tr_mirror_fail_member,
	._online                 = tr_mirror_online,
	._request                = tr_mirror_request,
	._resync                 = tr_mirror_resync,
	._get_counter            = tr_mirror_get_counter,
};
