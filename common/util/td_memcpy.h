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

