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

#ifndef _TD_SPLIT_REQ_H_
#define _TD_SPLIT_REQ_H_

#include "td_kdefn.h"
#include "td_limits.h"
#include "td_eng_conf.h"
#include "td_engine.h"
#include "td_defs.h"
#include "td_bio.h"



struct td_token;
struct td_engine;

#define TD_SPLIT_REQ_PART_MAX 128

struct td_biogrp {
	void                (*_dealloc)(struct td_biogrp*);
	
	td_bio_ref	    sr_orig;

	// TODO: atomic
	atomic_t            sr_total;     /**< total number of splits */
	atomic_t            sr_finished;  /**< number completed */

	int                 sr_result;

	long                sr_created;

	td_bio_t            sr_bios[0];
};



struct td_biogrp* td_biogrp_alloc(unsigned int extra);

/* Deprecated */
struct td_biogrp* td_biogrp_alloc_kzalloc(struct td_engine* eng,
		unsigned int extra);



#ifdef CONFIG_TERADIMM_PRIVATE_SPLIT_STASH
struct td_biogrp* td_stash_biogrp_alloc(struct td_engine* eng,
		unsigned int extra);
#endif

/** returns the biogrp container for a bio, or NULL */
static inline struct td_biogrp *td_bio_group(td_bio_ref bio)
{
	if (! td_bio_is_part(bio))
		return NULL;

	return (struct td_biogrp *)bio->bi_private;
}

/* returns the number of discard bios that need to be created from this bio */
static inline int td_bio_discard_count(td_bio_ref bio, struct td_engine *eng)
{
	uint64_t start, end; /*start and end lba*/
	uint64_t start_off, end_off; /*start and end lba offset*/
	uint64_t lbas, ssd_stripes;
	uint64_t stripe_size = td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS);
	uint64_t stripes = 0;
	uint64_t hw_sec = td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);
	uint64_t count = 0;
	uint64_t max_stripe;
	uint8_t ssd_count = (uint8_t)td_eng_conf_hw_var_get(eng, SSD_COUNT);

	/* Fast path. */
	if(likely(td_bio_is_discard(bio) == 0))
		goto not_discard;

	/* Set to byte size */
	start = td_bio_get_sector_offset(bio) << SECTOR_SHIFT;
	end = start + td_bio_get_byte_size(bio) - 1;

	/* Convert to LBA */
	start /= hw_sec;
	end /= hw_sec;

	/* Calc the LBAs */
	lbas = 1 + end - start;
	/* LBA start fragment */
	start_off = start % stripe_size;
	if (start_off)
		start_off = stripe_size - start_off;

	end_off = (end + 1) % stripe_size;

	/* For small files, the start_off is the full discard. */
	if (start_off >= lbas) {
		count++;
		goto just_one;
	}

	if (unlikely(start_off)) {
		/* The front is not aligned. */
		start -= start_off;
		lbas -= start_off;
		/* an entire trim for a part of a stripe? */
		count++;
	}

	if (unlikely(end_off)) {
		/* The back is not aligned. */
		end -= end_off;
		lbas -= end_off;
		/* an entire trim for a part of a stripe? */
		count++;
	}

	if (unlikely(!lbas))
		goto just_fragments;

	/* Get the rest of the stripe count. */
	stripes = lbas / stripe_size;

	if (stripes/ssd_count == 0) {
		count += stripes;
		goto under1_per_ssd;
	}
	/* Overcautious.. make sure we won't overflow the command.. */
	ssd_stripes = stripes/ssd_count +
		(stripes % ssd_count ? 1: 0);

	max_stripe = TD_MAX_DISCARD_LBA_COUNT/stripe_size;

	if (unlikely(ssd_stripes > max_stripe)) {
		/* If we would overflow the command, then plan for more
		 * bio's..although this is not supported in td_split_bio right
		 * now. */
		count += (ssd_stripes/max_stripe) * ssd_count;
		/* Left over stripes */
		stripes %= ssd_count;
		count += stripes;
		goto massive_trim;
	}

	count += ssd_count;

massive_trim:
under1_per_ssd:
just_fragments:
just_one:
not_discard:
	return (int)count;

}
/* End part of a split req */
extern void td_biogrp_complete_part(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);

/* releases resources held by a split req */
static inline void td_biogrp_free(struct td_biogrp *bg)
{
	WARN_ON(atomic_read(&bg->sr_finished) < atomic_read(&bg->sr_total));
	bg->_dealloc(bg);
}



/* old interface */
extern int td_split_req_create_list(struct td_engine *eng, td_bio_ref obio,
		struct td_biogrp **out_bg, struct bio_list *split_bios);

/* function called for each split request */
typedef void (*td_split_req_create_cb)(struct td_biogrp *bg,
		td_bio_ref bio, void *opaque);

/* takes a large bio and splits it into multiple LBA-aligned bios */
extern int td_split_req_create(struct td_engine *eng, td_bio_ref orig_bio,
		td_split_req_create_cb cb, void *opaque);

extern int td_split_req_create_discard(struct td_engine *eng,
		td_bio_ref orig_bio, td_split_req_create_cb cb, void *opaque);

extern int td_bio_split(td_bio_ref obio, unsigned size, td_split_req_create_cb cb, void *opaque);
extern int td_bio_replicate(td_bio_ref obio, int num, td_split_req_create_cb cb, void *opaque);

#endif
