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
#include "td_biogrp.h"


/* Per raid type params */
struct tr_stripe_params {
	uint32_t                                stride;
	uint64_t                                dev_lbas;
	struct td_osdev_block_params		block_params;
	uint64_t                                conf[TR_CONF_STRIPE_MAX];
};


static inline struct tr_stripe_params * tr_stripe(struct td_raid *rdev)
{
	return (struct tr_stripe_params *) rdev->ops_priv;
}


struct tr_stripe_bio_state {
	struct td_raid *rdev;
	td_bio_ref obio;
	
	unsigned bio_count;
};



/* --- RAID ops ---*/
static void tr_stripe_bio (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_stripe_bio_state *trbs = opaque;
	struct td_engine *eng;
	uint64_t stride;
	uint64_t devs;

	/* Ugly hack here */
	stride = tr_stripe(trbs->rdev)->stride;
	devs = tr_conf_var_get(trbs->rdev, MEMBERS);

	if (0) printk("STRIPE %p %u/%u\n", bio, trbs->bio_count+1, atomic_read(&bg->sr_total));

	if (1) {
		uint64_t sector = td_bio_get_sector_offset(bio);
		uint64_t piece = sector / stride;
		uint64_t offset = sector % stride;

		uint64_t dev_sector = stride * (piece / devs) + offset;
		uint64_t dev = piece % devs;

		/* And now we "fix" the sector here... */
		bio->bi_sector = dev_sector;

		eng = td_device_engine(trbs->rdev->tr_members[dev].trm_device);

		if (0) printk(" SECTOR %llu TO DEV [%llu] %s SECTOR %llu [%llu:%llu] (%u bytes)\n",
				sector, dev, td_eng_name(eng),
				dev_sector, piece, offset,
				td_bio_get_byte_size(bio));


		td_engine_queue_bio(eng, bio);
	}
	trbs->bio_count++;
}


static int tr_stripe_request (struct td_raid *rdev, td_bio_ref bio)
{
	struct tr_stripe_bio_state state;
	int rc;

	state.rdev = rdev;
	state.obio = bio;
	state.bio_count = 0;

	rc = td_bio_split(bio, TERADIMM_DATA_BUF_SIZE, tr_stripe_bio, &state);
	
	if (rc < 0) {
		td_raid_warn(rdev, "Could not split BIO for stripe\n");
		return -EIO;
	}

	return 0;
}


static int tr_stripe_check_member (struct td_raid *rdev, struct td_device *dev, int first)
{
	struct td_osdev_block_params *p = &tr_stripe(rdev)->block_params;
	struct td_engine *eng = td_device_engine(dev);

	if (first) {
		/* If this is the 1st device, it dictates RAID block_params */
		p->capacity = td_engine_capacity(eng);

		p->bio_max_bytes =
			td_eng_conf_var_get(eng, BIO_MAX_BYTES);
		p->bio_sector_size =
			td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);
		p->hw_sector_size =
			td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);
		
		/* But we don't do discard on raids */
		p->discard = 0;

		/* And we'll save how many usable LBAs we have */
		tr_stripe(rdev)->dev_lbas = (p->capacity >> SECTOR_SHIFT) & ~(tr_stripe(rdev)->stride);
	} else {
		/*
		* This new device must match the current raid block_params,
		* or * not be allowed to join the raid
		*/
		if ( ( td_engine_capacity(eng) >> SECTOR_SHIFT) < tr_stripe(rdev)->dev_lbas
				|| p->bio_max_bytes != td_eng_conf_var_get(eng, BIO_MAX_BYTES)
				|| p->bio_sector_size != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE)
				|| p->hw_sector_size != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) ) {
			return -EINVAL;
		}
	}

	return 0;
}

int tr_stripe_online (struct td_raid *rdev)
{
	struct td_osdev_block_params *p = &rdev->os.block_params;

	uint64_t capacity;
	
	td_raid_info(rdev, "Bringing stripe online:\n");
	p->bio_max_bytes = tr_stripe(rdev)->block_params.bio_max_bytes;
	p->hw_sector_size = tr_stripe(rdev)->block_params.hw_sector_size;
	p->bio_sector_size = tr_stripe(rdev)->block_params.bio_sector_size;

	capacity = SECTOR_SIZE * tr_stripe(rdev)->dev_lbas
			* tr_conf_var_get(rdev, MEMBERS);
	p->capacity = capacity;


	td_raid_info(rdev, " - stride %u [%x]\n",
			tr_stripe(rdev)->stride, tr_stripe(rdev)->stride);
	td_raid_info(rdev,  " - %llu [%llu] LBAs over %llu devs\n",
			tr_stripe(rdev)->dev_lbas, tr_stripe(rdev)->dev_lbas,
			tr_conf_var_get(rdev, MEMBERS));

	return 0;
}

static int tr_stripe_init (struct td_raid *rdev)
{
	struct tr_stripe_params *p = kzalloc(sizeof(struct tr_stripe_params), GFP_KERNEL);
	
	if (!p)
		return -ENOMEM;
	
	p->stride = 8;

	p->conf[TR_CONF_STRIPE_STRIDE] = p->stride * SECTOR_SIZE;
	
	rdev->ops_priv = p;
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
	case TR_CONF_STRIPE_STRIDE:
		if (val & (TD_PAGE_SIZE-1) ) {
			td_raid_err(rdev, "Invalid STRIDE size: %llu not aligned\n", val);
			return -EPERM;
		}
		if (val < TD_PAGE_SIZE) {
			td_raid_err(rdev, "Invalid STRIDE size: %llu too small\n", val);
			return -EPERM;
		}

		tr_stripe(rdev)->conf[var] = val;
		/* Mirror our LBA version */
		tr_stripe(rdev)->stride = val >> SECTOR_SHIFT;
		td_raid_info(rdev, "STRIPE set to %u LBAs from %llu bytes\n",
				tr_stripe(rdev)->stride, val);
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
	._online                 = tr_stripe_online,
	._request                = tr_stripe_request,
	
	._get_conf               = tr_stripe_get_conf,
	._set_conf               = tr_stripe_set_conf,
};
