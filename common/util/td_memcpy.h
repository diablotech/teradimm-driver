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
#ifndef __td_memcpy_h__
#define __td_memcpy_h__


#if   defined (__KERNEL__)
// Linux, want inline...
#ifndef KABI__memcpy_inline
#define MEMCPY_INLINE
#else
#define MEMCPY_INLINE static inline
#define MEMCPY_INLINE_FILE_IMPL "td_memcpy_gcc.c"

#endif

#else
// Linux User space, want inline
#define  MEMCPY_INLINE static inline
#define MEMCPY_INLINE_FILE_IMPL "td_memcpy_gcc.c"

#endif


// These are our forward declartions - our contract
MEMCPY_INLINE void *td_memcpy_movntq(void *dst, const void *src, unsigned int len);

MEMCPY_INLINE void td_memcpy_movntq_flush(void *dst, const void *src, unsigned int len,
		unsigned flush_src, unsigned flush_dst);

MEMCPY_INLINE void td_memcpy_8x8_movq(void *dst, const void *src,
		unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movq_flush_src64(void *dst, const void *src,
		unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movq_nt_wr_src64(void *dst, const void *src,
		unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movnti(void *dst, const void *src,
		unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum64(void *dst, const void *src,
		unsigned int len, uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_8x8_movq_xsum128(void *dst, const void *src,
		unsigned int len, uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_56B_movq_xsum128(void *dst, const void *src,
		uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_8B_movq_xsum128(void *dst, const void *src,
		uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum128(void *dst, const void *src,
		unsigned int len, uint64_t *xsum);

MEMCPY_INLINE void td_double_memcpy_8x8_movnti_xsum128(void *dst_a, void *dst_b,
		const void *src, unsigned int len, uint64_t *xsum);

MEMCPY_INLINE void td_triple_memcpy_8x8_movnti_xsum128(void *dst_a,
		void *dst_b, void *dst_c, const void *src,
		unsigned int len, uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_56B_movnti_xsum128(void *dst, const void *src,
		uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_8B_movnti_xsum128(void *dst, const void *src,
		uint64_t *xsum);

MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum128_e2e(void *dst,
		const void *src, unsigned int len,
		uint64_t *xsum, /* array of 2 u64 for fletcher checksum */
		uint64_t *e2e);  /* a second parallel checksum for e2e */

MEMCPY_INLINE void td_memcpy_8B_movnti(void *dst, const void *src);

MEMCPY_INLINE void td_memcpy_movntdqa_64(void *dst, const void *src, unsigned int len);
MEMCPY_INLINE void td_memcpy_movntdqa_16(void *dst, const void *src, unsigned int len);

MEMCPY_INLINE void td_zero_8B_movnti(void *dst);
MEMCPY_INLINE void td_zero_8x8_movnti(void *dst, unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movnti_cli_64B(void *dst, const void *src);

MEMCPY_INLINE void td_memcpy_4x16_movntq(void *dst, const void *src, unsigned int len);
MEMCPY_INLINE void td_memcpy_4x16_movntq_cli_64B(void *dst, const void *src);

MEMCPY_INLINE void td_fill_8x8_movnti(void *dst, uint64_t word, unsigned int len);

MEMCPY_INLINE void td_memcpy_8x8_movq_bad_clflush(void *dst, const void *const src_alias[2],
		unsigned int len, uint64_t bad);

#ifdef MEMCPY_INLINE_FILE_IMPL
#include MEMCPY_INLINE_FILE_IMPL
#endif

static inline void td_zero_movnti (void* dst, unsigned int len)
{
	unsigned int accumulated = 0;
	BUG_ON(len & 7);
	/* We do this in 8B chunks until we are 128B aligned */
	while (len & 127) {
		td_zero_8B_movnti(PTR_OFS(dst, accumulated));
		accumulated += 8;
		len -= 8;
	}

	if (len) {
		/* write zeros for the rest of the length */
		td_zero_8x8_movnti(PTR_OFS(dst, accumulated), len);
	}

}

#endif

