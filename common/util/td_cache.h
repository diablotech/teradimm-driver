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
#define TERADIMM_NTF_FILL_WORD 0x0ULL

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

#define td_rdbuf_flush(eng, pre_or_post,ptr, len, old_cache)                  \
do {                                                                          \
	switch (td_eng_conf_var_get(eng, CLFLUSH) & TD_FLUSH_RDBUF_MASK) {        \
	case TD_FLUSH_RDBUF_CLF_##pre_or_post:                                \
		clflush_cache_range(ptr, len);                                \
		break;                                                        \
	case TD_FLUSH_RDBUF_NTF_##pre_or_post:                                \
		if (old_cache) {                                              \
			if (0) printk("RDBUF FLUSH: td_memcpy_8x8_movnti(%p, %p, %u)\n", ptr, old_cache, len); \
			td_memcpy_8x8_movnti(ptr, old_cache, len);            \
		} else {                                                      \
			if (0) printk("RDBUF FLUSH: td_fill_8x8_movnti(%p, %016llu, %u)\n", ptr, TERADIMM_NTF_FILL_WORD, len); \
			td_fill_8x8_movnti(ptr, TERADIMM_NTF_FILL_WORD, len); \
		}                                                             \
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
