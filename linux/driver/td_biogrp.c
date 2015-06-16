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

#include "td_defs.h"
#include "td_bio.h"
#include "td_util.h"
#include "td_device.h"
#include "td_engine.h"
#include "td_osdev.h"

#define TD_SPLIT_TRIM_SIZE      512


int td_bio_split (td_bio_ref obio, unsigned split_size,
		td_split_req_create_cb cb, void *opaque)
{
	struct td_biogrp_options ops = {
		.split_size = split_size,
		.duplicate_count = 1,
		.submit_part = cb,
		.error_part = NULL,
	};

	return td_biogrp_create(obio, &ops, opaque);
}

int td_bio_replicate (td_bio_ref obio, int num_bios,
		td_split_req_create_cb cb, void *opaque)
{
	struct td_biogrp_options ops =
	{
		.split_size = TD_PAGE_SIZE,
		.duplicate_count = num_bios,
		.submit_part = cb,
		.error_part = NULL,
	};

	return td_biogrp_create(obio, &ops, opaque);

}

static void td_split_req_create_list_cb(struct td_biogrp *sreq,
		td_bio_ref new_bio, void *opaque)
{
	struct bio_list *split_bios = opaque;

	bio_list_add(split_bios, new_bio);
}

int __td_split_req_create_list(struct td_engine *eng, td_bio_ref orig_bio,
		struct td_biogrp **out_sreq, struct bio_list *split_bios)
{
	return (td_bio_is_discard(orig_bio) ?
		td_split_req_create_discard(eng, orig_bio,
			td_split_req_create_list_cb, split_bios) :
		td_bio_split(orig_bio, TERADIMM_DATA_BUF_SIZE,
			td_split_req_create_list_cb, split_bios) );
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

void __td_biogrp_complete_part(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts)
{
	struct td_biogrp *bgrp = td_bio_group(bio);
	int done, total;

	/* if nothing failed yet, update the biogrp status */
	if (result && ! bgrp->sr_result)
		bgrp->sr_result = result;

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
	total = bgrp->sr_parts;
	done = atomic_inc_return(&bgrp->sr_finished);

	/* return if there are outstanding parts, our inc wasn't the last one */
	if (done < total)
		return;

	WARN_ON(done != total);

	/*
	 * We are the one that finished it, it's all ours now, nobody else
	 * will touch it
	 */

	td_bio_endio(eng, bgrp->sr_orig, bgrp->sr_result,
			td_get_cycles() - bgrp->sr_created);
	td_biogrp_free(bgrp);
}


