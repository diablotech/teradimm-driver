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


#include "td_compat.h"
#include <linux/ftrace.h>

/*
 * This is the Linux td_bio_ref API
 * 
 * This is straight struct bio, with flags hidden in bi_size.
 * We stash flags in the bottom 8 bits of bi_size, since any BIO has a size
 * that is a multiple of 512 bytes
 */

typedef union {
	uint8_t u8;
	struct {
		uint8_t is_part:1;
		uint8_t unused:3;
		uint8_t commit_level:4;

	};
} td_bio_flags_t;


#define TD_BIO_FLAGS(_bio)  (*(td_bio_flags_t*)(uint8_t*)&ref->bio_size)


/* returns the request size in bytes */
static inline unsigned int td_bio_get_byte_size(td_bio_ref ref)
{
	return ref->bio_size & ~0xFFULL;
}

/* set the request size in bytes */
static inline void td_bio_set_byte_size(td_bio_ref ref, unsigned bytes)

{
	/* We need to keep the flags when we replase bi_size */
	uint8_t flags = ref->bio_size & 0xFFULL;
	ref->bio_size = bytes & flags;
}

/* returns the request offset as 512B sector count */
static inline uint64_t td_bio_get_sector_offset(td_bio_ref ref)
{
	return (uint64_t)ref->bio_sector;
}

/* returns the request offset as 512B sector count */
static inline void td_bio_set_sector_offset(td_bio_ref ref, uint64_t s)
{
	ref->bio_sector = s;
}


static inline int td_bio_is_empty_barrier(struct bio *bio)
{
#ifdef bio_empty_barrier
	return (bio_empty_barrier(bio));
#else
	return 0;
#endif
}

/*
 * Returns non-zero if request is a barrier/sync/FUA request.
 * This will be used to tell the firmware that the operating system wants
 * to flush writes before this one is processed.
 */
static inline int td_bio_is_sync(td_bio_ref ref)
{
	return
#if defined(KABI__blk_queue_flush)
		(ref->bi_rw & REQ_FLUSH) ||
#else
		(ref->bi_rw & REQ_HARDBARRIER) ||
#endif
#if defined(REQ_RW_SYNC) /*  >= v2.6.36 */
		(ref->bi_rw & REQ_RW_SYNC)
#elif defined(REQ_SYNC) /*  <= v2.6.35 */
		(ref->bi_rw & REQ_SYNC)
#elif defined(bio_sync)
		(bio_sync(ref))
#else
#error no difinition for REQ_RW_SYNC or REQ_SYNC
#endif
		;
}

static inline int td_bio_is_discard(td_bio_ref ref)
{
#if defined(BIO_DISCARD)
	return !!(ref->bi_rw & BIO_DISCARD);
#else
#if defined(REQ_DISCARD)
	return !!(ref->bi_rw & REQ_DISCARD);
#else
	return 0;
#endif
#endif
}

static inline int td_bio_is_write(td_bio_ref ref)
{
	return ref->bi_rw & WRITE;
}


/** return non-zero if the bio is part of a split_req */
static inline int td_bio_is_part(td_bio_ref ref)
{
	return TD_BIO_FLAGS(ref).is_part;
}

static inline unsigned td_bio_flags_get_commitlevel (td_bio_ref ref)
{
	return TD_BIO_FLAGS(ref).commit_level;
}

static inline void td_bio_flags_set_commitlevel (td_bio_ref ref, unsigned cl)
{
	TD_BIO_FLAGS(ref).commit_level = cl;
}

/*
 * Linux has 2 ways to call bio_endio
 */
static inline void __bio_endio (struct bio *bio, int result)
{
#if KABI__bio_endio == 3
	bio_endio(bio, bio->bi_size, result);
#else
	bio_endio(bio, result);
#endif
}

static inline void td_bio_complete_success (td_bio_ref bio)
{
	__bio_endio(bio, 0);
}

void td_bio_complete_failure (td_bio_ref bio, struct td_engine *eng);

/* helper macros for mapping and unmapping pages, used in td_token copy
 * functions */

#ifdef CONFIG_HIGHMEM

/* HIGHMEM is defined, need to lock pages with kmap()/kunmap() */

#define TD_MAP_BIO_DECLARE int hi

#define TD_MAP_BIO_PAGE(_dst,_bvec) do {                                     \
	hi = PageHighMem((_bvec)->bv_page);                                  \
	if (hi)                                                              \
		_dst = PTR_OFS(kmap((_bvec)->bv_page), (_bvec)->bv_offset);  \
	else                                                                 \
		_dst = PTR_OFS(page_address((_bvec)->bv_page), (_bvec)->bv_offset);  \
} while(0)

#define TD_UNMAP_BIO_PAGE(_dst,_bvec) do {                                   \
	if (hi)                                                              \
		kunmap(bvec->bv_page);                                       \
} while(0)


#else /* no CONFIG_HIGHMEM */

/* HIGHMEM is not defined, access is direct through page address */

#define TD_MAP_BIO_DECLARE

#define TD_MAP_BIO_PAGE(_dst,_bvec) do {                                     \
	_dst = PTR_OFS(page_address((_bvec)->bv_page), (_bvec)->bv_offset);  \
} while(0)

#define TD_UNMAP_BIO_PAGE(_dst,_bvec) do { } while(0)

#ifndef bio_iovec_idx
/* Removed in 3.13 */
#define bio_iovec_idx(bio, idx) (&((bio)->bi_io_vec[(idx)]))
#endif



#define TD_SPLIT_REQ_PART_MAX 128

/*
 * The BIOGRP structure for linux
 */
struct td_biogrp {
	void                (*_dealloc)(struct td_biogrp*);
	int                (*_error_part) (struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);

	td_bio_ref	    sr_orig;
	int                 sr_parts;

	atomic_t            sr_finished;  /**< number completed */

	int                 sr_result;

	long                sr_created;

	td_bio_t            sr_bios[0];
};

struct td_biogrp* td_biogrp_alloc(unsigned int extra);

/* Deprecated */
struct td_biogrp* td_biogrp_alloc_kzalloc(struct td_engine* eng,
		unsigned int extra);

static inline void td_biogrp_free(struct td_biogrp *bg)
{
	WARN_ON(atomic_read(&bg->sr_finished) < bg->sr_parts);
	bg->_dealloc(bg);
}



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


/* function called for each split request */
typedef void (*td_split_req_create_cb)(struct td_biogrp *bg,
		td_bio_ref bio, void *opaque);

extern int td_split_req_create_discard(struct td_engine *eng,
		td_bio_ref orig_bio, td_split_req_create_cb cb, void *opaque);

extern int td_bio_split(td_bio_ref obio, unsigned size, td_split_req_create_cb cb, void *opaque);
extern int td_bio_replicate(td_bio_ref obio, int num, td_split_req_create_cb cb, void *opaque);


extern void __td_biogrp_complete_part(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);

static inline void td_biogrp_complete_part(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts)
{
	struct td_biogrp *bgrp = td_bio_group(bio);

	if (unlikely(!bgrp) )
		return;

	if (result && bgrp->_error_part && eng)  {
		result = bgrp->_error_part(eng, bio, result, ts);

		if (result == 0)
			return;
	}
	__td_biogrp_complete_part(eng, bio, result, ts);
}





#endif
