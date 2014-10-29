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

#ifndef _TD_BIO_H_
#define _TD_BIO_H_

/*
 * The intent of this file is to make it easy to implement wrappers around
 * any OS native block request structures
 */

#include "td_defs.h"
#include "td_compat.h"

typedef union {
	uint8_t u8;
	struct {
		uint8_t is_part:1;
		uint8_t unused:3;
		uint8_t commit_level:4;

	};
} td_bio_flags_t;

/* Predeclare this, for the endio function argument */
struct td_engine;

/*
 * These are the forward declartions of BIO stuff that needs
 * to be supported by all the platforms
 */
typedef struct bio td_bio_t;
typedef struct bio * td_bio_ref;

static inline unsigned int td_bio_get_byte_size(td_bio_ref ref);
static inline uint64_t td_bio_get_sector_offset(td_bio_ref ref);

static inline int td_bio_is_sync(td_bio_ref ref);
static inline int td_bio_is_write(td_bio_ref ref);
static inline int td_bio_is_discard(td_bio_ref ref);

static inline td_bio_flags_t* td_bio_flags_ref(td_bio_ref ref);
static inline int td_bio_is_part(td_bio_ref ref);

static inline void td_bio_complete_success (td_bio_ref bio);
static inline void td_bio_complete_failure (td_bio_ref bio);

extern void td_bio_endio(struct td_engine *eng, td_bio_ref bio, int result, cycles_t ts);

#include "td_bio_linux.h"


/** return non-zero if the bio is part of a split_req */
static inline int td_bio_is_part(td_bio_ref ref)
{
	td_bio_flags_t *f = td_bio_flags_ref(ref);
	return f->is_part;
}

static inline int td_bio_is_read(td_bio_ref ref)
{
	return ! td_bio_is_write(ref);
}

#ifndef KABI__bio_list
#include "lk_biolist.h"
#endif

#endif

