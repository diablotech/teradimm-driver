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
#include "td_engine.h"
#include "td_bio.h"

#include "td_stash.h"

static void td_split_req_create_list_cb(struct td_biogrp *sreq,
		td_bio_ref new_bio, void *opaque)
{
	struct bio_list *split_bios = opaque;

	bio_list_add(split_bios, new_bio);
}

int td_split_req_create_list(struct td_engine *eng, td_bio_ref orig_bio,
		struct td_biogrp **out_sreq, struct bio_list *split_bios)
{
	return (td_bio_is_discard(orig_bio) ?
		td_split_req_create_discard(eng, orig_bio,
			td_split_req_create_list_cb, split_bios) :
		td_bio_split(orig_bio, TERADIMM_DATA_BUF_SIZE,
			td_split_req_create_list_cb, split_bios) );
}


/* Helper interface for td_engine.c */
int td_split_req_create(struct td_engine *eng, td_bio_ref obio,
		td_split_req_create_cb cb, void *opaque)
{
	return td_bio_split(obio, TERADIMM_DATA_BUF_SIZE, cb, opaque);
}

/*
 * DEFAULT split allocation is via kzalloc/kfree
 */
static void td_biogrp_dealloc_kfree(struct td_biogrp *sreq)
{
	kfree(sreq);
}

struct td_biogrp* td_biogrp_alloc( unsigned int extra)
{
	struct td_biogrp *sreq;
	int size = sizeof(struct td_biogrp) + extra;

	if ((sreq = kzalloc(size, GFP_KERNEL)) ) {
		sreq->_dealloc = td_biogrp_dealloc_kfree;
	}
	return sreq;
	
}
struct td_biogrp* td_biogrp_alloc_kzalloc(struct td_engine* eng,
		unsigned int extra)
{
	struct td_biogrp *sreq;
	sreq = td_biogrp_alloc(extra);
	td_eng_trace(eng, TR_BIO, "split:malloc:alloc", (uint64_t)sreq);
	return sreq;
}

#ifdef CONFIG_TERADIMM_PRIVATE_SPLIT_STASH

static void td_biogrp_dealloc_stash (struct td_biogrp *sreq)
{
	td_stash_dealloc(sreq);
}

struct td_biogrp* td_stash_biogrp_alloc (struct td_engine *eng,
		unsigned int extra)
{
	struct td_stash_info *info = eng->td_split_stash;
	int size = sizeof(struct td_biogrp) + extra;
	

	struct td_biogrp *sreq  =td_stash_alloc(info, size);

	if (unlikely(sreq == NULL))
		return td_biogrp_alloc_kzalloc(eng, extra);

	sreq->_dealloc = td_biogrp_dealloc_stash;
	td_eng_trace(eng, TR_BIO, "split:stash:alloc", (uint64_t)sreq);
	return sreq;
}

#endif

void td_biogrp_complete_part(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts)
{
	struct td_biogrp *sr = td_bio_group(bio);
	int done, total;

	if (unlikely(!sr) )
		return;

	/* if nothing failed yet, update the biogrp status */
	if (result && ! sr->sr_result)
		sr->sr_result = result;

	/*
	 * Mark another piece as as done
	 * 
	 * Be very careful here.  Afer we've done our atomic_inc_return, we
	 * can't touch the biogrp anymore, unless we we are the one that
	 * finishes it.  If we didn't finish it, another CPU could be
	 * finishing it while this CPU could be in an interrupt and delaying
	 * us until the other CPU has free()'d it.
	 *
	 */
	total = atomic_read(&sr->sr_total);
	done = atomic_inc_return(&sr->sr_finished);

	/* return if there are outstanding parts, our inc wasn't the last one */
	if (done < total)
		return;

	WARN_ON(done > total);

	/*
	 * We are the one that finished it, it's all ours now, nobody else
	 * will touch it
	 */

	td_bio_endio(eng, sr->sr_orig, sr->sr_result,
			td_get_cycles() - sr->sr_created);
	td_biogrp_free(sr);
}


