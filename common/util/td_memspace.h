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

#ifndef _TD_MEMSPACE_H
#define _TD_MEMSPACE_H

#include "td_limits.h"
/*
 * Megadimm offset looks like this:
 *
 *    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   +-----------------------------+-----+----------------------------+
 *   |X     X                      |type |offset in buffer            |
 *   +-----------------------------+-----+----------------------------+
 */

/*
 * TeraDIMM offsets looks like this:
 *
 *    3 3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 *    2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   +-------------------------------+-----+----------------------------+
 *   |                               |type |offset in buffer            |
 *   +-------------------------------+-----+----------------------------+
 *
 * [13:0]  - 14 bits, 16k buffer offset, at most 4k are usable, depends on type
 * [16:14] - 3 bits, buffer type (read/write/command/status/etc)
 * [32:17] - 16 bits, buffer index + aliasing
 *
 * The split between buffer indices and aliases depends on how many buffers
 * are usable in the given type.
 *
 * name              type   count    size  aliases
 * read buffers         0      32     4kB     4096
 * write buffers        1       8     4kB    16384
 * read meta buffs      2      32    64 B     4096
 * write meta buffs     3       8    64 B    16384
 * status buffers       4       1   256 B   131072
 * command buffers      5     256    64 B      512
 * extended status      6       1  2144 B   131072
 *
 */

/* base offset of the first buffer of each type */
#define TERADIMM_ACCESS_TYPE_SHIFT    14  /*  2^14 is 16k */

/* buffer/alias index starts at bit 17 */
#define TERADIMM_ACCESS_BLOCK_SHIFT   17  /*  2^17 is 128M */

/* an address has 33 bits */
#define TERADIMM_ADDRESS_BITS         33  /*  2^33 is 8G */
#define TERADIMM_ADDRESS_MASK         ( (1ULL << TERADIMM_ADDRESS_BITS) - 1 )

/* enumeration of buffer types */
enum teradimm_buf_type {
	TERADIMM_READ_DATA       = 0, /*  32 x 4kB each */
	TERADIMM_WRITE_DATA      = 1, /*   8 x 4kB each */
	TERADIMM_READ_META_DATA  = 2, /*  32 x 64B each */
	TERADIMM_WRITE_META_DATA = 3, /*   8 x 64B each */
	TERADIMM_STATUS          = 4, /*   1 x 256B */
	TERADIMM_COMMAND         = 5, /* 256 x 64B each */
	TERADIMM_EXT_STATUS      = 6, /*   1 x 4kB */
};

/* maximum number of buffers per location */
#define TERADIMM_READ_DATA_MAX         32
#define TERADIMM_WRITE_DATA_MAX         8
#define TERADIMM_READ_META_DATA_MAX    32
#define TERADIMM_WRITE_META_DATA_MAX    8
#define TERADIMM_STATUS_MAX             1
#define TERADIMM_COMMAND_MAX          256
#define TERADIMM_EXT_STATUS_MAX         1

/* how much data each buffer holds */
#define TERADIMM_DATA_BUF_SIZE          4096
#define TERADIMM_META_BUF_SIZE           128
#define TERADIMM_STATUS_SIZE             256
#define TERADIMM_COMMAND_SIZE             64
#define TERADIMM_EXT_STATUS_SIZE        2144

/* offsets in the extended status buffer */
#define TERADIMM_EXT_STATUS_CMD_IDX(x)       (x)
#define TERADIMM_EXT_STATUS_WEP_READ_IDX(x)  (256+x)
#define TERADIMM_EXT_STATUS_WEP_COMPLETE_IDX (256+10)
#define TERADIMM_EXT_STATUS_GLOBAL_IDX       (256+11)

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
/* "fw status" mode uses WEP-7 to convey completion of status messages, the
 * WEP-7 contains status data, and a semaphore */
#define TERADIMM_WEP7_SEMAPHORE_OFS    0x100
/* dito for read buffer 0 status method */
#define TERADIMM_RDBUF0_SEMAPHORE_OFS  0x100
/* "fw status" mode uses upper 64 bytes of metadata as a core buffer marker */
#define TERADIMM_META_BUF_MARKER_OFS   64
#define TERADIMM_META_BUF_MARKER_SIZE  64
#endif

static inline off_t teradimm_offset(enum teradimm_buf_type type,
		uint alias, uint bufs_per_alias, uint buf_index)
{
	off_t block_index;
	off_t ofs;

	/* convert 2 dimensional coordinates into a linear one */
	block_index = (alias * bufs_per_alias) + buf_index;

	/* calculate the logical offset */
	ofs = (off_t)type << TERADIMM_ACCESS_TYPE_SHIFT
	    | block_index << TERADIMM_ACCESS_BLOCK_SHIFT;

	/* return the least significant 33 bits */
	return ofs & TERADIMM_ADDRESS_MASK;
}
#define TERADIMM_OFFSET(type,alias,index) \
	teradimm_offset(TERADIMM_##type, alias, TERADIMM_##type##_MAX, index)

/* offset between current read buffer and next alias */
#define TERADIMM_OFFSET_TO_NEXT_ALIAS(type) \
	( TERADIMM_##type##_MAX << TERADIMM_ACCESS_BLOCK_SHIFT )

#endif
