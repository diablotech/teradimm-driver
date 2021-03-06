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
#define TERADIMM_ADDRESS_BITS         32  /*  2^32 is 4G */
#define TERADIMM_ADDRESS_MAX          ( 1ULL << TERADIMM_ADDRESS_BITS )
#define TERADIMM_ADDRESS_MASK         ( TERADIMM_ADDRESS_MAX - 1 )

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
		uint alias, uint bufs_per_alias, uint buf_index,
		uint64_t avoid_mask)
{
	off_t block_index;
	off_t ofs;
	off_t shift_mask = 0;
	off_t cur_bit = 0;

	/* convert 2 dimensional coordinates into a linear one */
	block_index = (alias * bufs_per_alias) + buf_index;

	/* calculate the logical offset */
	ofs = (off_t)type << TERADIMM_ACCESS_TYPE_SHIFT
	    | block_index << TERADIMM_ACCESS_BLOCK_SHIFT;

	for (cur_bit = 0 ; cur_bit < TERADIMM_ADDRESS_BITS ; cur_bit++) {
		if (avoid_mask & (1ULL << cur_bit)) {
			ofs = ( (ofs & shift_mask)
				    | (((ofs & (~shift_mask)) << 1)) );
		}
		shift_mask |= (1ULL << cur_bit);
	}

	if (ofs & avoid_mask || ofs >= TERADIMM_ADDRESS_MAX) {
		pr_err("BAD %u:%u:%u:%u OFFSET %0lx (%0llx)\n",
				type, alias, bufs_per_alias, buf_index, ofs,
				ofs & avoid_mask);
	}

	/* return the least significant 33 bits */
	return ofs & TERADIMM_ADDRESS_MASK;
}
#define TERADIMM_OFFSET(type,alias,index,avoid_mask) \
	teradimm_offset(TERADIMM_##type, alias, \
			TERADIMM_##type##_MAX, index, avoid_mask)

/* offset between current read buffer and next alias */
#define TERADIMM_OFFSET_TO_NEXT_ALIAS(type) \
	( TERADIMM_##type##_MAX << TERADIMM_ACCESS_BLOCK_SHIFT )

#endif
