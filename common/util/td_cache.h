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
#ifndef __td_cache_h__
#define __td_cache_h__

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/byteorder.h>
#else
#include <stdint.h>
#include <endian.h>
#define cpu_to_be64 htobe64
#define kernel_fpu_begin() do{}while(0)
#define kernel_fpu_end() do{}while(0)
#endif

#include "td_eng_conf.h"
#include "td_compat.h"

/* NTF flush mode writes this word over each cacheline to invalidate */
#define TERADIMM_NTF_FILL_WORD 0xffffffffffffffffULL

/**
 * cause the cachelines of a buffer to be flushed
 * @param eng             - engine, used to get configuration
 * @param pre_or_post     - PRE or POST
 * @param buffer_type     - STATUS or RDBUF or CMD
 * @param ptr             - buffer pointer
 *
 * NOTE: this is a macro so some of the arguments are not variables or
 * constants, but parts of defines that specify different levels of cache line
 * flushing.
 *
 * EXAMPLE: this code causes a cache line flush before a status buffer is
 * read:
 *
 *          td_cache_flush(eng, PRE, STATUS, status, size);
 *
 * where the 'status' and 'size' are the pointer to the device buffer, and
 * it's length.
 */
#define td_cache_flush(eng,pre_or_post,buffer_type,ptr,len)                   \
do {                                                                          \
	switch (td_eng_conf_var_get(eng, CLFLUSH) & TD_FLUSH_##buffer_type##_MASK) {        \
	case TD_FLUSH_##buffer_type##_CLF_##pre_or_post:                      \
		clflush_cache_range(ptr, len);                                \
		break;                                                        \
	case TD_FLUSH_##buffer_type##_NTF_##pre_or_post:                      \
		td_fill_8x8_movnti(ptr, TERADIMM_NTF_FILL_WORD, len);         \
		break;                                                        \
	}                                                                     \
} while(0)

/** true if {PRE,POST} is enabled for buffer type */
#define td_cache_flush_test(eng,pre_or_post,buffer_type)                      \
	(td_eng_conf_var_get(eng, CLFLUSH) &                                  \
		(TD_FLUSH_##buffer_type##_CLF_##pre_or_post                   \
		|TD_FLUSH_##buffer_type##_NTF_##pre_or_post)                  \
	)

#define td_cache_flush_exact_test(eng,pre_or_post,clf_or_ntf,buffer_type)     \
	((td_eng_conf_var_get(eng, CLFLUSH) & TD_FLUSH_##buffer_type##_MASK)  \
	 == TD_FLUSH_##buffer_type##_##clf_or_ntf##_##pre_or_post             \
	)

#endif
