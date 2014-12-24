/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("Diablo").  All       *
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
 *    Modified BSD License                                               *
 *                                                                       *
 *    If you have explicit permission from Diablo, then (1) use in       *
 *    source and binary forms, with or without modification; as well as  *
 *    (2) redistribution ONLY in binary form, with or without            *
 *    modification; are permitted provided that the following conditions *
 *    are met:                                                           *
 *                                                                       *
 *    * Redistributions in binary form must reproduce the above          *
 *    copyright notice, this list of conditions and the following        *
 *    disclaimer in the documentation and/or other materials provided    *
 *    with the distribution.                                             *
 *                                                                       *
 *    * Neither the name of the DIABLO nor the names of its contributors *
 *    may be used to endorse or promote products derived from this       *
 *    software without specific prior written permission.                *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR *
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) *
 *    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN          *
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR       *
 *    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,     *
 *    EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                 *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "td_compat.h"

/* This is the Linux td_bio_ref API */

/** real bios are aligned on 512 bytes. We store flags in the bottom 8 bits */
static inline td_bio_flags_t* td_bio_flags_ref(td_bio_ref ref)
{
	td_bio_flags_t *f = (void*)(uint8_t*)&ref->bio_size;
	return f;
}
/* returns the request size in bytes */
static inline unsigned int td_bio_get_byte_size(td_bio_ref ref)
{
	return ref->bio_size & ~0xFFULL;
}

/* returns the request offset as 512B sector count */
static inline uint64_t td_bio_get_sector_offset(td_bio_ref ref)
{
	return (uint64_t)ref->bio_sector;
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

static inline void td_bio_complete_success (td_bio_ref bio)
{
#if KABI__bio_endio == 3
	bio_endio(bio, bio->bi_size, 0);
#else
	bio_endio(bio, 0);
#endif
}

static inline void td_bio_complete_failure (td_bio_ref bio)
{
#if KABI__bio_endio == 3
	bio_endio(bio, bio->bi_size, -EIO);
#else
	bio_endio(bio, -EIO);
#endif
}

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

#endif
