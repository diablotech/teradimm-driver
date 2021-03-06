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
#ifndef __td_checksum_h__
#define __td_checksum_h__

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/byteorder.h>
#else
#include <stdint.h>
#include <endian.h>
#define cpu_to_be64 htobe64
#endif

/**
 * \brief advance a checksum with fixed word
 *
 * @param word    - 64b word to advance checksum by
 * @param count   - number of times to apply it
 */
static inline void td_checksum128_advance(uint64_t *xsum, uint64_t word, unsigned count)
{
	for(;count;--count) {
		xsum[0] += word;
		xsum[1] += xsum[0];
	}
}

/**
 * \brief advance a checksum with zeros
 *
 * @param bytes     - length of zeros the data was exended by
 * 
 * We often have to extend buffers with zeros.  We can write the
 * zeros without computing the checksum and quickly advance the checksum
 * knowing how fletch rolls the 2 pices together.
 *
 */
static inline void td_checksum128_advance_zeroed(uint64_t *xsum, unsigned bytes)
{
	xsum[1] = xsum[1] + (bytes/8 * xsum[0]);
}

/** 
 * \brief compute a host endianness fletcher checksum 
 * 
 * @param data    - pointer to data buffer
 * @param len     - length in bytes, assume to be multiple of 8
 * @param xsum[2] - pointer to uint64_t[2] where the fletcher checksum is stored
 *
 * NOTE: the initial state of xsum[2] is the seed of the checksum.  On first
 * call that should be set to {0,0}.
 *
 * NOTE: overflow is ignored in checksum128
 */
#ifdef CONFIG_TERADIMM_CHECKSUM128_C
static inline void td_checksum128(const void *data, unsigned len,
		uint64_t *xsum)
{
	uint64_t val;
	const uint64_t *p = data;
	const uint64_t *e = (uint64_t*)((char*)data + len);

	while(p < e) {
		val = (*p);
		xsum[0] += val;
		xsum[1] += xsum[0];
		p ++;
	}
}
#else

static inline void td_checksum128(const void *src, unsigned len,
		uint64_t *xsum)
{
	register uint64_t t1=0, t2=0, t3=0, t4=0;

	len = (len + 31) & ~31;

	__asm__ __volatile__ (
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"                                     \n"
		"leaq    4*8(%[src]),   %[src]        \n"
		"                                     \n"
		"addq    %[t1],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t2],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t3],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t4],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"subl    $32,           %[len]        \n" // 4 * 8 == 32
		"jnz     1b                           \n"
		"                                     \n"
		"\n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"+r"(t1), [t2]"+r"(t2), [t3]"+r"(t3), [t4]"+r"(t4)
		, [src]"+S"(src), [len]"+c" (len)
		:
		);
}
#endif

/** 
 * \brief compute a big-endian fletcher checksum 
 * 
 * @param data    - pointer to data buffer
 * @param len     - length in bytes, assume to be multiple of 8
 * @param xsum[2] - pointer to uint64_t[2] where the fletcher checksum is stored
 *
 * NOTE: the initial state of xsum[2] is the seed of the checksum.  On first
 * call that should be set to {0,0}.
 *
 * NOTE: overflow is ignored in checksum128
 */
#ifdef CONFIG_TERADIMM_CHECKSUM128_C
static inline void td_checksum128_be(const void *data, unsigned len,
		uint64_t *xsum)
{
	uint64_t val;
	const uint64_t *p = data;
	const uint64_t *e = (uint64_t*)((char*)data + len);

	while(p < e) {
		val = cpu_to_be64(*p);
		xsum[0] += val;
		xsum[1] += xsum[0];
		p ++;
	}
}
#else
static inline void td_checksum128_be(const void *src, unsigned len,
		uint64_t *xsum)
{
	register uint64_t t1=0, t2=0, t3=0, t4=0;

	len = (len + 31) & ~31;

	__asm__ __volatile__ (
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"                                     \n"
		"bswapq  %[t1]                        \n"
		"bswapq  %[t2]                        \n"
		"bswapq  %[t3]                        \n"
		"bswapq  %[t4]                        \n"
		"                                     \n"
		"leaq    4*8(%[src]),   %[src]        \n"
		"                                     \n"
		"addq    %[t1],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t2],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t3],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t4],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"subl    $32,           %[len]        \n" // 4 * 8 == 32
		"jnz     1b                           \n"
		"                                     \n"
		"\n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"+r"(t1), [t2]"+r"(t2), [t3]"+r"(t3), [t4]"+r"(t4)
		, [src]"+S"(src), [len]"+c" (len)
		:
		);
}
#endif

static inline void td_checksum64(const void *src, unsigned len,
		uint64_t *xsum)
{
	register uint64_t t1=0, t2=0, t3=0, t4=0;

	len = (len + 31) & ~31;

	__asm__ __volatile__ (
		"                                     \n"
		"xorq    %%rax,         %%rax         \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"                                     \n"
		"leaq    4*8(%[src]),   %[src]        \n"
		"                                     \n"
		"addq    %[t1],         %[xsum]       \n"
		"adcq    %[t2],         %[xsum]       \n"
		"adcq    %[t3],         %[xsum]       \n"
		"adcq    %[t4],         %[xsum]       \n"
		"setc    %%al                         \n"
		"addq    %%rax,         %[xsum]       \n"
		"                                     \n"
		"subl    $32,           %[len]        \n" // 4 * 8 == 32
		"jnz     1b                           \n"
		"                                     \n"
		"\n"
		: [xsum]"+r"(*xsum),
		  [t1]"+r"(t1), [t2]"+r"(t2), [t3]"+r"(t3), [t4]"+r"(t4)
		, [src]"+S"(src), [len]"+c" (len)
		:
		: "%rax"
		);
}

static inline void td_checksum64_be(const void *src, unsigned len,
		uint64_t *xsum)
{
	register uint64_t t1=0, t2=0, t3=0, t4=0;

	len = (len + 31) & ~31;

	__asm__ __volatile__ (
		"                                     \n"
		"xorq    %%rax,         %%rax         \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"                                     \n"
		"bswapq  %[t1]                        \n"
		"bswapq  %[t2]                        \n"
		"bswapq  %[t3]                        \n"
		"bswapq  %[t4]                        \n"
		"                                     \n"
		"leaq    4*8(%[src]),   %[src]        \n"
		"                                     \n"
		"addq    %[t1],         %[xsum]       \n"
		"adcq    %[t2],         %[xsum]       \n"
		"adcq    %[t3],         %[xsum]       \n"
		"adcq    %[t4],         %[xsum]       \n"
		"setc    %%al                         \n"
		"addq    %%rax,         %[xsum]       \n"
		"                                     \n"
		"subl    $32,           %[len]        \n" // 4 * 8 == 32
		"jnz     1b                           \n"
		"                                     \n"
		"bswapq  %[xsum]                      \n"
		"                                     \n"
		"\n"
		: [xsum]"+r"(*xsum),
		  [t1]"+r"(t1), [t2]"+r"(t2), [t3]"+r"(t3), [t4]"+r"(t4)
		, [src]"+S"(src), [len]"+c" (len)
		:
		: "%rax"
		);
}


#endif
