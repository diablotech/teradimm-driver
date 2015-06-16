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

#define DEBUG_STRIPE_PRINTK 0


/* Per raid type params */
struct tr_stripe_params {
	uint64_t                                conf[TR_CONF_STRIPE_MAX];
};


static inline struct tr_stripe_params * tr_stripe(struct td_raid *rdev)
{
	return (struct tr_stripe_params *) rdev->ops_priv;
}

#define tr_stripe_var_get(rdev, which)                             \
	(tr_stripe(rdev)->conf[TR_CONF_STRIPE_##which])
#define tr_stripe_var_set(rdev, which, val)                            \
	do { tr_stripe(rdev)->conf[TR_CONF_STRIPE_##which] = val;                   \
	td_raid_debug(rdev, "CONF STRIPE_%s set to %llu\n", __stringify(which), tr_stripe(rdev)->conf[TR_CONF_STRIPE_##which]); \
	} while (0)



static int tr_stripe_part_error (struct td_engine *eng, td_bio_ref bio, 
		int result, cycles_t ts)
{
	struct td_raid *rdev = td_engine_device(eng)->td_raid;
	struct tr_member *trm;
	int idx;

	BUG_ON(!rdev);

	if (DEBUG_STRIPE_PRINTK) printk("BIO PART ERROR on %s (%p)\n", td_device_name(td_engine_device(eng)), td_engine_device(eng));
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

	BUG_ON(!trm);

	return result;
};



struct tr_stripe_bio_state {
	struct td_raid *rdev;
	td_bio_ref obio;
	
	unsigned bio_count;
};



/* --- RAID ops ---*/
static void tr_stripe_bio (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_member *trm;
	struct tr_stripe_bio_state *trbs = opaque;
	uint64_t stride = tr_stripe_var_get(trbs->rdev, DEV_STRIDE);
	uint64_t devs = tr_conf_var_get(trbs->rdev, MEMBERS);
	uint64_t sector = td_bio_get_sector_offset(bio);

	uint64_t piece, offset, dev_sector, dev;

	if (DEBUG_STRIPE_PRINTK) printk("STRIPE %p %u/%u\n", bio, trbs->bio_count+1, bg->sr_parts);

	piece = sector / stride;
	offset = sector % stride;
	dev_sector = stride * (piece / devs) + offset;
	dev = piece % devs;

	trm = trbs->rdev->tr_members + dev;

	if (trm->trm_state == TR_MEMBER_ACTIVE) {
		struct td_engine *eng = td_device_engine(trm->trm_device);

		/* And now we "fix" the sector here... */
		td_bio_set_sector_offset(bio, dev_sector);

		if (DEBUG_STRIPE_PRINTK) printk(" - SECTOR %llu TO DEV [%llu] %s SECTOR %llu [%llu:%llu] (%u bytes)\n",
				sector, dev, td_eng_name(eng),
				dev_sector, piece, offset,
				td_bio_get_byte_size(bio));

		td_engine_queue_bio(eng, bio);
	} else {
		/* Not active, trm may even be missing */
		td_bio_endio(NULL, bio, -EIO, 0);
	}
	trbs->bio_count++;
}


static int tr_stripe_request (struct td_raid *rdev, td_bio_ref bio)
{
	struct td_biogrp_options opts;
	struct tr_stripe_bio_state state;
	int rc;

	state.rdev = rdev;
	state.obio = bio;
	state.bio_count = 0;

	opts.split_size = TD_PAGE_SIZE;
	opts.duplicate_count = 1;
	opts.submit_part = tr_stripe_bio;
	opts.error_part = tr_stripe_part_error;

	rc = td_biogrp_create(bio, &opts, &state);
	
	if (rc < 0) {
		td_raid_warn(rdev, "Could not split BIO for stripe\n");
		return -EIO;
	}

	return 0;
}


static int tr_stripe_check_member (struct td_raid *rdev, struct td_device *dev)
{
	struct td_engine *eng = td_device_engine(dev);

	if (TR_MEMBERSET_EMPTY(rdev) && ! tr_conf_var_get(rdev, CAPACITY)) {
		/* If this is the 1st device, it dictates RAID block_params */
		td_raid_debug(rdev, "Initializing stripe conf from %s\n",
				td_device_name(dev));

		tr_stripe_var_set(rdev, DEV_LBAS,
				td_engine_lbas(eng) &
				~(tr_stripe_var_get(rdev, DEV_STRIDE)-1) );

		tr_conf_var_set(rdev, BIO_SECTOR_SIZE,
				td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE));
		tr_conf_var_set(rdev, HW_SECTOR_SIZE,
				td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));
		
		tr_conf_var_set(rdev, CAPACITY,
				tr_stripe_var_get(rdev, DEV_LBAS)
				* tr_conf_var_get(rdev, MEMBERS)
				* (1<<SECTOR_SHIFT) );

		if (! tr_conf_var_get(rdev, BIO_MAX_BYTES) )
				tr_conf_var_set(rdev, BIO_MAX_BYTES,
					td_eng_conf_var_get(eng, BIO_MAX_BYTES));
	} else {
		/*
		* This new device must match the current raid block_params,
		* or * not be allowed to join the raid
		*/
		if ( ( td_engine_lbas(eng) < tr_stripe_var_get(rdev, DEV_LBAS))
				|| tr_conf_var_get(rdev, BIO_SECTOR_SIZE) != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE)
				|| tr_conf_var_get(rdev, HW_SECTOR_SIZE) != td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) ) {
			return -EINVAL;
		}
	}

	return 0;
}

static int tr_stripe_handle_member (struct td_raid *rdev, int idx)
{
	struct tr_member *trm = rdev->tr_members + idx;

	/* In stripe, sync is a NO-OP, we go active */
	if (trm->trm_state == TR_MEMBER_SYNC) {
		trm->trm_state = TR_MEMBER_ACTIVE;
		TR_ACTIVESET_SET(rdev, idx);
	}

	if (TR_ACTIVESET_FULL(rdev) ) {
		td_raid_info(rdev, "STRIPE MEMBER CHANGE: OPTIMAL\n");
		tr_enter_run_state(rdev, OPTIMAL);
	} else {
		td_raid_info(rdev, "STRIPE MEMBER CHANGE: FAILED\n");
		tr_enter_run_state(rdev, FAILED);
	}

	return 0;
}
static int tr_stripe_fail_member (struct td_raid *rdev, int idx)
{
	//struct tr_member *trm = rdev->tr_members + idx;

	tr_enter_run_state(rdev, FAILED);

	return 0;
}

int tr_stripe_online (struct td_raid *rdev)
{
	if (! TR_MEMBERSET_FULL(rdev) ) {
		td_raid_err(rdev, "Stripe cannot go online, members missing\n");
		return -EINVAL;
	}

	td_raid_info(rdev, "Bringing stripe online:\n");

	td_raid_info(rdev, " - stride %llu [%llx]\n",
			tr_stripe_var_get(rdev, DEV_STRIDE),
			tr_stripe_var_get(rdev, DEV_STRIDE));
	td_raid_info(rdev,  " - %llu [%llx] LBAs over %llu devs\n",
			tr_stripe_var_get(rdev, DEV_LBAS),
			tr_stripe_var_get(rdev, DEV_LBAS),
			tr_conf_var_get(rdev, MEMBERS));

	return 0;
}

static int tr_stripe_init (struct td_raid *rdev)
{
	struct tr_stripe_params *p = kzalloc(sizeof(struct tr_stripe_params), GFP_KERNEL);
	
	if (!p)
		return -ENOMEM;
	
	rdev->ops_priv = p;

	tr_stripe_var_set(rdev, DEV_STRIDE, 8);
	tr_stripe_var_set(rdev, STRIDE, 8<<SECTOR_SHIFT);
	
	return 0;
}

static int tr_stripe_destroy (struct td_raid *rdev)
{
	if (rdev->ops_priv)
		kfree (rdev->ops_priv);
	return 0;
}

static int tr_stripe_get_conf (struct td_raid *rdev, uint32_t var, uint64_t *val)
{
	switch (var) {
	case TR_CONF_STRIPE_STRIDE:
	case TR_CONF_STRIPE_DEV_LBAS:
	case TR_CONF_STRIPE_DEV_STRIDE:
		*val = tr_stripe(rdev)->conf[var];
		return 0;

	case TR_CONF_STRIPE_MAX:
		/* Nothing */;
	}
	return -EINVAL;
}

static int tr_stripe_set_conf (struct td_raid *rdev, uint32_t var, uint64_t val)
{
	switch (var) {
	case TR_CONF_STRIPE_DEV_STRIDE:
		/*
		 * This is the STRIDE, as LBA value.  We will adjust
		 * to bytes, and consider this as a STRIDE set
		 */
		var = TR_CONF_STRIPE_STRIDE;
		val <<= SECTOR_SHIFT;
		/* Fall through */
	case TR_CONF_STRIPE_STRIDE:
		if (val & (TD_PAGE_SIZE-1) ) {
			td_raid_err(rdev, "Invalid STRIDE size: %llu not aligned\n", val);
			return -EPERM;
		}
		if (val < TD_PAGE_SIZE) {
			td_raid_err(rdev, "Invalid STRIDE size: %llu too small\n", val);
			return -EPERM;
		}
		if (!val || (val & (val - 1))) {
			td_raid_err(rdev, "Invalid STRIDE size: %llu not power of 2\n", val);
			return -EPERM;
		}

		/* Set our DEV_STRIDE, in LBAs */
		tr_stripe_var_set(rdev, DEV_STRIDE, val >> SECTOR_SHIFT);

		td_raid_info(rdev, "STRIPE set to %llu LBAs from %llu bytes\n",
				tr_stripe_var_get(rdev, DEV_STRIDE), val);

		/* Fall through to the set for STRIDE */

	case TR_CONF_STRIPE_DEV_LBAS:
		tr_stripe(rdev)->conf[var] = val;
		td_raid_debug(rdev, "CONF [%u] set to %llu\n", var, val);
		return 0;

	case TR_CONF_STRIPE_MAX:
		/* Nothing */;
	}
	return -EINVAL;
}


struct td_raid_ops tr_stripe_ops = {
	._init                   = tr_stripe_init,
	._destroy                = tr_stripe_destroy,
	._check_member           = tr_stripe_check_member,
	._handle_member          = tr_stripe_handle_member,
	._fail_member            = tr_stripe_fail_member,
	._online                 = tr_stripe_online,
	._request                = tr_stripe_request,
	
	._get_conf               = tr_stripe_get_conf,
	._set_conf               = tr_stripe_set_conf,
};
