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

#ifndef _TD_BIO_H_
#define _TD_BIO_H_

/*
 * The intent of this file is to make it easy to implement wrappers around
 * any OS native block request structures
 */

#include "td_kdefn.h"

#include "td_defs.h"
#include "td_compat.h"
/* Pre-declare this, for the endio function argument */
struct td_engine;

/*
 * These are the forward declarations of BIO stuff that needs
 * to be supported by all the platforms
 */
typedef struct bio td_bio_t;
typedef struct bio * td_bio_ref;

/*
 * This is the BIO API that the engine uses:
 *  - Get/set byte/sector info
 *  - Get/set flags
 *  - end BIO
 */
static inline unsigned int td_bio_get_byte_size(td_bio_ref ref);
static inline void         td_bio_set_byte_size(td_bio_ref ref, unsigned size);

static inline uint64_t td_bio_get_sector_offset(td_bio_ref ref);
static inline void     td_bio_set_sector_offset(td_bio_ref ref, uint64_t s);

static inline int td_bio_is_sync(td_bio_ref ref);
static inline int td_bio_is_discard(td_bio_ref ref);
static inline int td_bio_is_write(td_bio_ref ref);

static inline int td_bio_is_read(td_bio_ref ref)
{
	return ! td_bio_is_write(ref);
}


/*
 * These are few "flags" that we need to keep with BIOs
 */
static inline enum td_commit_type td_bio_flags_get_commitlevel (td_bio_ref ref);
static inline void                td_bio_flags_set_commitlevel (td_bio_ref ref, enum td_commit_type cl);

static inline int td_bio_is_part(td_bio_ref ref);

/*
 * The main "bio endio" function
 */
extern void td_bio_endio(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);

/* This is a support function in td_trim.c */
extern int td_bio_trim_count(td_bio_ref bio, struct td_engine *eng);

/*
 * BIOGRP API
 * 
 * This is the API that the RAID code relies on for bio groups
 * BIO Groups are created by the OS-specific block layer front-ends, and the
 * member BIOs of them are submitted to the device engines.
 *
 * The OS can implement the bio groups as it sees fit.  The raid code only needs
 * a common entry point to "create" the groupings, and a way to get the
 * failure of a part back from the BIOGRP code when a chunk fails.
 *
 * The bio part failure back information needs to get back into the raid code
 * so the raid can handle errors appropriately.
 *
 * The error_part function returns an INT.  A return value of 0 means that
 * the failure was over-ruled.  No further processing can happen, because the
 * error handler did something special with that part.  It as arranged that
 * the part will be finished again at some other time.
 * A return value of non-zero means to complete the part, with the returned
 * value as the result to use for the td_biogrp.
 */

struct td_biogrp;

struct td_biogrp_options
{
	unsigned        split_size;
	unsigned        duplicate_count;
	void            (*submit_part) (struct td_biogrp *grp,
					td_bio_ref bio, void* opaque);
	int             (*error_part) (struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);
};

extern int td_biogrp_create (td_bio_ref obio, struct td_biogrp_options *ops,
		void* opaque);



#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
/*
 * If we are offloading successful endio, this is how the devgroup code
 * calls it
 */
static inline void td_bio_complete_success (td_bio_ref bio);
#endif

#include "td_bio_linux.h"



#ifndef KABI__bio_list
#include "lk_biolist.h"
#endif

#endif

