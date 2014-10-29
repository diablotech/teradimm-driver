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
struct tr_mirror_params {
	uint32_t        read_stride;
	atomic_t        last_read_dev;
	struct td_osdev_block_params block_params;
	uint64_t                                conf[TR_CONF_MIRROR_MAX];
};

static inline struct tr_mirror_params * tr_mirror(struct td_raid *rdev)
{
	return (struct tr_mirror_params *) rdev->ops_priv;
}



struct tr_mirror_bio_state {
	struct td_raid *rdev;
	td_bio_ref obio;
	
	unsigned bio_count;
};

static void tr_mirror_read (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_mirror_bio_state *trbs = opaque;
	struct td_raid *rdev = trbs->rdev;
	struct tr_member *trm;
	unsigned dev;

	if (0) td_raid_debug(rdev, "MIRROR %p READ %u/%u\n", bio, trbs->bio_count+1, atomic_read(&bg->sr_total));
	
	/* For reads, we could do better to pick a random member */
	dev = atomic_inc_return(&tr_mirror(rdev)->last_read_dev) % tr_conf_var_get(rdev, MEMBERS);

	trm = rdev->tr_members + dev;
	BUG_ON(! trm->trm_device);

	if (0) td_raid_debug(rdev, "  DEV [%u] %s SECTOR %llu (%u bytes)\n",
			dev, td_device_name(trm->trm_device),
			td_bio_get_sector_offset(bio),
			td_bio_get_byte_size(bio)
			);

	td_engine_queue_bio(td_device_engine(trm->trm_device), bio);

	trbs->bio_count++;
}

static void tr_mirror_write (struct td_biogrp *bg, td_bio_ref bio, void *opaque)
{
	struct tr_mirror_bio_state *trbs = opaque;
	struct td_raid *rdev = trbs->rdev;
	struct tr_member *trm;

	if (0) td_raid_debug(rdev, "MIRROR %p WRITE %u/%u\n", bio, trbs->bio_count+1, atomic_read(&bg->sr_total));

	/* Based on bio_count, we distribute the parts */
	trm = rdev->tr_members + trbs->bio_count;
	BUG_ON (!trm->trm_device);

	if (0) td_raid_debug(rdev, "  DEV [%u] %s SECTOR %llu (%u bytes)\n",
			trbs->bio_count, td_device_name(trm->trm_device),
			td_bio_get_sector_offset(bio),
			td_bio_get_byte_size(bio)
			);

	td_engine_queue_bio(td_device_engine(trm->trm_device), bio);

	trbs->bio_count++;
}

/* --- RAID ops ---*/

int tr_mirror_request (struct td_raid *rdev, td_bio_ref bio)
{
	struct tr_mirror_bio_state state;
	int rc;

	state.rdev = rdev;
	state.obio = bio;
	state.bio_count = 0;

	if (td_bio_is_write(bio)) {
		rc = td_bio_replicate(bio, tr_conf_var_get(rdev, MEMBERS), tr_mirror_write, &state);
	} else {
		rc = td_bio_split(bio, tr_mirror(rdev)->read_stride, tr_mirror_read, &state);
	}
	
	if (rc < 0) {
		td_raid_warn(rdev, "Could not split BIO for mirror\n");
		return -EIO;
	}

	return 0;
}

int tr_mirror_request_degraded (struct td_raid *rdev, td_bio_ref bio)
{
	td_raid_warn(rdev, "Passing BIO %p in degraded mode\n", bio);
	return tr_mirror_request(rdev, bio);
}

int tr_mirror_init (struct td_raid *rdev)
{
	struct tr_mirror_params *p = kzalloc(sizeof(struct tr_mirror_params), GFP_KERNEL);
	
	if (!p)
		return -ENOMEM;
	
	/* We need to pick something */
	p->read_stride = 16 * 512;
	atomic_set(&p->last_read_dev, 0);

	rdev->ops_priv = p;
	return 0;
	
}

static int tr_mirror_destroy (struct td_raid *rdev)
{
	if (rdev->ops_priv)
		kfree (rdev->ops_priv);
	return 0;
}

static int tr_mirror_check_member (struct td_raid *rdev, struct td_device *dev, int first)
{
	struct td_osdev_block_params *p = &tr_mirror(rdev)->block_params;
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
	} else {
		/*
		* This new device must match the current raid block_params,
		* or * not be allowed to join the raid
		*/
		if ( p->capacity > td_engine_capacity(eng)
				|| p->bio_max_bytes != td_eng_conf_var_get(eng, BIO_MAX_BYTES)
				|| p->bio_sector_size != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE)
				|| p->hw_sector_size != td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) ) {
			return -EINVAL;
		}
	}

	return 0;
}

int tr_mirror_online (struct td_raid *rdev)
{
	struct td_osdev_block_params *p = &rdev->os.block_params;

	p->capacity = tr_mirror(rdev)->block_params.capacity;
	p->bio_max_bytes = tr_mirror(rdev)->block_params.bio_max_bytes;
	p->hw_sector_size = tr_mirror(rdev)->block_params.hw_sector_size;
	p->bio_sector_size = tr_mirror(rdev)->block_params.bio_sector_size;

	return 0;
}


struct td_raid_ops tr_mirror_ops = {
	._init                   = tr_mirror_init,
	._destroy                = tr_mirror_destroy,
	._check_member           = tr_mirror_check_member,
	._online                 = tr_mirror_online,
	._request                = tr_mirror_request,
	._degraded_request       = tr_mirror_request_degraded,
};
