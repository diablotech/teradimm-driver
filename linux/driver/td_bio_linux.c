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

#include "td_biogrp.h"
#include "td_util.h"
#include "td_device.h"
#include "td_engine.h"
#include "td_bio.h"

#define TD_SPLIT_TRIM_SIZE      512

#define __next_nbio() ({ struct bio *_bio     = next_nbio ++; _bio ; })
#define __next_nvec() ({ struct bio_vec *_vec = next_nvec ++; _vec ; })
#define __next_buf() ({ char *_buf = buf + TD_SPLIT_TRIM_SIZE; _buf ; })
#define __advance_ovec() ({ ovec_used=0; \
		++oidx; WARN_ON(oidx>obio->bi_vcnt); ++ovec; })



int td_split_req_create_discard(struct td_engine *eng, struct bio *obio,
		td_split_req_create_cb cb, void *opaque)
{
	struct bio_vec *ovec;
	struct bio *next_nbio, *nbio;
	struct bio_vec *next_nvec;
	struct td_biogrp *sreq;
	uint64_t addr, align, hw_sec, size;
	uint64_t end_addr;
	uint64_t default_size, extra_stripes, max_stripe;
	uint64_t stripe, stripe_size;
	uint8_t ssd_count;
	uint max_nbios, max_nvecs, b_size;
	uint start_part, end_part;
	uint ovec_used;
	uint oidx;
	int ret_count;
	char *buf;


	if (td_bio_is_part(obio))
		return -EINVAL;

	td_eng_trace(eng, TR_TRIM, "BIO:trim:bio", (uint64_t)obio);
	td_eng_trace(eng, TR_TRIM, "BIO:trim:sctr", obio->bio_sector);
	td_eng_trace(eng, TR_TRIM, "BIO:trim:size", obio->bio_size);

	max_nbios = td_bio_discard_count(obio, eng);

	if (!max_nbios || max_nbios > TD_SPLIT_REQ_PART_MAX)
		return -EINVAL;

	/* Each bio will only have 1 bio_vec for now.*/
	max_nvecs = max_nbios;

	b_size = (max_nbios * sizeof(struct bio))
		+ (max_nvecs * sizeof(struct bio_vec))
		+ (max_nvecs * TD_SPLIT_TRIM_SIZE);

	sreq = td_stash_biogrp_alloc(eng, b_size);
	if (!sreq)
		return -ENOMEM;

	/* remaining data is chopped up for bio's and vec's */
	next_nbio = (void*)(sreq + 1);
	next_nvec = (void*)(next_nbio + max_nbios);
	buf = (void*)(next_nvec + max_nvecs);

	sreq->sr_orig = obio;

	atomic_set(&sreq->sr_total, 0);

	/* ret_count is a private counter, returned to the caller */
	ret_count = 0;

	sreq->sr_created = td_get_cycles();
	sreq->sr_result = 0;

	/* start on the first vec of the old bio */
	oidx = obio->bio_idx;
	ovec = bio_iovec_idx(obio, oidx);
	ovec_used = 0;


	/* get eng information for calculations. */
	hw_sec = td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);
	ssd_count  = td_eng_conf_hw_var_get(eng, SSD_COUNT);

	/* check alignment, don't trim partial hw_sectors. */
	if (likely(td_bio_aligned_correctly(eng, obio))) {
		addr = td_bio_get_sector_offset(obio);
		size = td_bio_get_byte_size(obio);
	}
	else {
		/* Check front alignment by checking if the starting byte is
		 * on a hw_sec boundary */
		align = (td_bio_get_sector_offset(obio) << SECTOR_SHIFT) %
			hw_sec;
		/* If align isn't 0, then we need to shift the address by the
		 * difference of the hw_sec boundary and the start address */
		if (align)
			align = hw_sec - align;
		addr = td_bio_get_sector_offset(obio)
			+ (align >> SECTOR_SHIFT);

		/* Then align the back by re-calculating the size and removing
		 * the overflow into the next hw_sec. */
		size = td_bio_get_byte_size(obio) - align;
		size -= size % hw_sec;
	}

	/* Used to calculate the next SSD start location. */
	stripe_size = td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS) * hw_sec;
	stripe = (addr << SECTOR_SHIFT) / stripe_size;
	max_stripe = TD_MAX_DISCARD_LBA_COUNT/stripe_size;

	/* The start and end stripe might not be stripe aligned.*/
	start_part = (addr << SECTOR_SHIFT) % stripe_size;
	if (start_part)
		start_part = stripe_size - start_part;
	end_part = (size - start_part) % stripe_size;

	 /* We know the start and end might be different, but every other piece
	 * should be the remaining size /number of BIOs */

	/* Setup for the first loop.. */
	end_addr = extra_stripes = default_size = 0;

	/* Treat really small trims differently*/
	if (size < start_part + end_part) {
		/* Just a wee little trim, only the end_part will be used */
		end_part = size;
	}
	else {  /* Regular trim, spanning multiple stripes.
		   Note these calculations will not be used if there is only a
		   start/end part */

		/* default_size needs to be the number of stripes headed to
		 * ALL devices.  */
		default_size = size - start_part - end_part;
		/* number of stripes..*/
		default_size /= stripe_size;
		/* the extra stripes need to head to the first drive we send to
		 * otherwise, we'll trim the wrong location */
		extra_stripes = default_size % ssd_count;
		/* number of stripes going to every ssd */
		default_size /= ssd_count;
		/* Byte size of each stripe. */
		default_size *= stripe_size;

		if (end_part) {
			end_addr = addr + ((size - end_part) >> SECTOR_SHIFT);
		}
	}

	/* Setup the size for the first loop. */

	if (start_part)
		/* an entire trim for just a part of a stripe at the front. */
		size = start_part;
	else {
		size = default_size;
		if (extra_stripes) {
			extra_stripes--;
			size += stripe_size;
		}
	}

	/* Loop through the number of bios we need to create, altering the
	 * sector start to begin on a stripe for each SSD that is affected by
	 * this discard bio. */
	while(max_nbios > 0)
	{
		struct bio_vec *nvec;
		td_bio_flags_t flags = { .u8 = 0 };

		nbio = __next_nbio();

		nbio->bi_rw     = obio->bi_rw;
		/* This address will be translated into the correct SSD LBA
		 * before the trim command is created. */
		nbio->bio_sector = addr;
		/* The size to trim */
		nbio->bio_size   = size;

		if (max_nbios == 1 && end_part) {
			if (end_addr)
				nbio->bio_sector = end_addr;
			nbio->bio_size = end_part;
		}
		td_eng_trace(eng, TR_BIO, "BIO:Trim start",
				nbio->bio_sector << SECTOR_SHIFT/hw_sec);
		td_eng_trace(eng, TR_BIO, "BIO:Trim size",
				nbio->bio_size);

		/* No bios. */
		nbio->bi_vcnt = 1;
		nbio->bi_io_vec = next_nvec;

		nbio->bio_idx = 0;
		nbio->bi_private = sreq;
		atomic_inc(&sreq->sr_total);


		/* setup the vector. Data will be filled out after the sector
		 * is converted to the per-device LBA. */
		nvec = __next_nvec();

		nvec->bv_page = (struct page*)buf;

		nvec->bv_len = TD_SPLIT_TRIM_SIZE;
		nvec->bv_offset = 0;

		/* mark the new bio as being wrapped */
		flags.is_part = 1;
		nbio->bio_size |= flags.u8;

		/* add the new bio to the list */
		cb(sreq, nbio, opaque);

		/* Setup for next run. */
		buf = __next_buf();
		stripe++;
		addr = (stripe * stripe_size) >> SECTOR_SHIFT;
		size = default_size;
		if(extra_stripes > 0) {
			extra_stripes--;
			size += stripe_size;
		}
		max_nbios--;
		ret_count++;
	}

	return ret_count;

}


int td_bio_split (td_bio_ref obio, unsigned split_size,
		td_split_req_create_cb cb, void *opaque)
{
	uint oidx;
	struct bio_vec *ovec;
	uint64_t addr;
	uint max_nbios, max_nvecs, size;
	uint ovec_used, obio_ofs, obio_left;
	struct bio *next_nbio;
	struct bio_vec *next_nvec;
	struct td_biogrp *sreq;
	int ret_count;

#if 0
	if (td_bio_is_part(obio))
		return -EINVAL;
#endif

	max_nbios = td_bio_page_span(obio, TERADIMM_DATA_BUF_SIZE);

	if (max_nbios < 1 || max_nbios > TD_SPLIT_REQ_PART_MAX)
		return -EINVAL;

	max_nvecs = max_nbios * 2;

	size = (max_nbios * sizeof(struct bio))
		+ (max_nvecs * sizeof(struct bio_vec));

	sreq = td_biogrp_alloc(size);
	if (!sreq)
		return -ENOMEM;

	/* remaining data is chopped up for bio's and vec's */
	next_nbio = (void*)(sreq + 1);
	next_nvec = (void*)(next_nbio + max_nbios);

	if (0) printk("SPLIT (%p) NBIOS %d\n", obio, max_nbios);

	sreq->sr_orig = obio;

	/* sr_total is always one ahead of what is begin constructed; on final
	 * pass through the chopping while() loop below, the sr_total is not
	 * incremented; this way the callback function could consume the
	 * fragments asynchronously and the release will not happen too early */
	atomic_set(&sreq->sr_total, 1);

	/* ret_count is a private counter, returned to the caller */
	ret_count = 0;

	sreq->sr_created = td_get_cycles();
	sreq->sr_result = 0;

	/* start on the first vec of the old bio */
	oidx = obio->bio_idx;
	ovec = bio_iovec_idx(obio, oidx);
	ovec_used = 0;

	/* walk the bio structure updating the split parts */
	addr = (obio->bio_sector << SECTOR_SHIFT);
	obio_ofs = 0;

	/* bi_size has flags encoded in it */
	obio_left = td_bio_get_byte_size(obio);

	while (obio_left) {
		uint lba_ofs, lba_left;
		struct bio *nbio;
		struct bio_vec *nvec;
		uint nbio_left;
		td_bio_flags_t flags = { .u8 = 0 };

		
		/* first create a new bio which describes the access the LBA
		 * starting at addr; the access end either at the LBA boundary
		 * or when the overall access is finished */

		lba_ofs  = addr % split_size;
		lba_left = split_size - lba_ofs;

		nbio = __next_nbio();

		nbio->bi_rw     = obio->bi_rw;
		nbio->bio_sector = (addr >> SECTOR_SHIFT);
		nbio->bio_size   = min(obio_left, lba_left);

		nbio->bi_io_vec = next_nvec;
		nbio->bi_vcnt = 0;
		nbio->bio_idx = 0;

		if (0) printk(" PART (%p) %d/%d (%p) sector %lu size %u\n",
			nbio, ret_count+1, max_nbios, obio->bi_private,
			nbio->bi_sector, nbio->bi_size);

		nbio_left = nbio->bio_size;

		/* next try to assign the new bio transfer to pages in the old
		 * vecs */

		while(nbio_left) {

			uint ovec_left = ovec->bv_len - ovec_used;

			if(!ovec_left) {
				__advance_ovec();
				ovec_used = 0;
				ovec_left = ovec->bv_len;
			}

			nvec = __next_nvec();

			nvec->bv_page   = ovec->bv_page;
			nvec->bv_len    = min(nbio_left, ovec_left);
			nvec->bv_offset = ovec->bv_offset + ovec_used;

			nbio->bi_vcnt ++;

			ovec_used += nvec->bv_len;
			nbio_left -= nvec->bv_len;

		}

		/* update accounting counters */
		obio_ofs += nbio->bio_size;
		obio_left -= nbio->bio_size;
		addr += nbio->bio_size;

		/* mark the new bio as being wrapped */
		flags.is_part = 1;
		nbio->bio_size |= flags.u8;

		/* associate the new bio with the container */
		nbio->bi_private = sreq;

		/* collect the total number */
		if (obio_left)
			atomic_inc(&sreq->sr_total);
		ret_count ++;

		/* add the new bio to the list */
		if (0) printk(" CB[%p](%p, %p, %p)\n", cb, sreq, nbio, opaque);
		cb(sreq, nbio, opaque);

		/* if obio_left == 0, sreq and nbio cannot be used safely, as
		 * the callback could have freed the entire strucutre */
	}

	WARN_ON(ret_count > max_nbios);

	return ret_count;
}

int td_bio_replicate (td_bio_ref obio, int num_bios,
		td_split_req_create_cb cb, void *opaque)
{
	td_bio_flags_t flags = { .u8 = 0 };
	struct td_biogrp *sreq;
	struct bio *next_nbio;
	struct bio_vec *next_nvec, *ovec;
	int max_nvecs, size;
	int ret_count;
	
	flags.is_part = 1;

	max_nvecs = num_bios * 2;

	size = num_bios * sizeof(struct bio)
		+ (max_nvecs * sizeof(struct bio_vec));

	sreq = td_biogrp_alloc(size);

	sreq->sr_orig = obio;
	atomic_set(&sreq->sr_total, num_bios);

	/* ret_count is a private counter, returned to the caller */
	ret_count = 0;
	
	sreq->sr_created = jiffies;
	sreq->sr_result = 0;

	/* remaining data is chopped up for bio's and vec's */
	next_nbio = (void*)(sreq + 1);
	next_nvec = (void*)(next_nbio + num_bios);


	if (0) printk("REPLICATE (%p) NBIOS %d\n", obio, num_bios);

	ret_count = 0;
	while (ret_count < num_bios) {
		uint left;
		struct bio *nbio =  __next_nbio();
		memcpy(nbio, obio, sizeof(struct bio));
		
		left = nbio->bi_vcnt;
		ovec = bio_iovec_idx(obio, obio->bio_idx);
		
		while (left--) {
			struct bio_vec *nvec = __next_nvec();
			memcpy(nvec, ovec, sizeof(struct bio_vec));
			
			ovec++;
		}

		if (0) printk(" PART (%p) %d/%d (%p) sector %lu size %u\n",
			nbio, ret_count+1, num_bios, obio->bi_private,
			nbio->bi_sector, nbio->bi_size);

		/* mark the new bio as being wrapped */
		flags.is_part = 1;
		nbio->bio_size |= flags.u8;

		/* associate the new bio with the container */
		nbio->bi_private = sreq;

		/* collect the total number */
		ret_count ++;

		/* add the new bio to the list */
		if (0) printk(" CB[%p](%p, %p, %p)\n", cb, sreq, nbio, opaque);
		cb(sreq, nbio, opaque);
	}

	WARN_ON(ret_count != num_bios);

	return ret_count;
}


static void __bio_endio(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts)
{
	int rw = bio_data_dir(bio);
	unsigned int size = td_bio_get_byte_size(bio);
	struct gendisk *disk = td_engine_device(eng)->os.disk;

	if (disk) {
#if defined(part_stat_lock)
		int cpu = part_stat_lock();
		struct hd_struct *part = disk_map_sector_rcu(disk, bio->bi_sector);
		if (rw) {
			part_stat_add(cpu, part, sectors[rw], size/512);
		}
		else {
			part_stat_add(cpu, part, sectors[rw], size/512);
		}
		part_stat_add(cpu, part, ticks[rw], td_cycles_to_msec(ts));
		part_stat_inc(cpu, part, ios[rw]);
#else
		if (rw) {
			disk_stat_add(disk, sectors[rw], size/512);
		}
		else {
			disk_stat_add(disk, sectors[rw], size/512);
		}
		disk_stat_inc(disk, ios[rw]);
		disk_stat_add(disk, ticks[rw], td_cycles_to_msec(ts));
#endif
	}

	td_engine_endio(eng, bio, result);

	/* update the request latency */
	if (rw == WRITE)
		td_eng_latency_end(&eng->td_bio_latency, bio,
				"req-wr-latency",
				&eng->td_counters.write.req);
	else
		td_eng_latency_end(&eng->td_bio_latency, bio,
				"req-rd-latency",
				&eng->td_counters.read.req);
}

void td_bio_endio(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts)
{
	if (unlikely (td_bio_is_part(bio))) {
		return td_biogrp_complete_part(eng, bio, result, ts);
	}
	
	td_eng_trace(eng, TR_BIO, "BIO:end:bio   ", (uint64_t)bio);
	
	/* Clear any flags */
	bio->bio_size -= bio->bio_size & 0x00FF;

	__bio_endio(eng, bio, result, ts);

	td_eng_trace(eng, TR_BIO, "BIO:end:result", result);

	/* update the counter */
	eng->td_total_bios++;
}

