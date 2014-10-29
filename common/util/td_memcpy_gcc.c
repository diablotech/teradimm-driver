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

/*
 * This file contines the code of the TD memcopy routines.
 * 
 * This file is specifically inteneded to be "include-able" 
 * by the td_memcpy.h to get them directly inlined,
 * or to be standalone, and get non-inlined versions
 */

#if   defined(__KERNEL__)

#ifdef SUPPRESS_KERNEL_WARNINGS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#endif

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/i387.h>
#include "td_compat.h"
#ifdef COMPILE_WITH_ALL_WARNINGS
#pragma GCC diagnostic pop
#endif

#else

#include <stdint.h>
#include <endian.h>
#define cpu_to_be64 htobe64
#define kernel_fpu_begin() do{}while(0)
#define kernel_fpu_end() do{}while(0)

#endif

#ifndef MEMCPY_INLINE
#define MEMCPY_INLINE
#endif



MEMCPY_INLINE void *td_memcpy_movntq(void *dst, const void *src,
		unsigned int len)
{
	kernel_fpu_begin();

	__asm__ __volatile__ (
		"1:\n"
		"subl    $32,           %%ecx         \n" /* 4 * 8 == 32 */

		"movq    0*8(%%rsi),    %%mm0         \n"
		"movq    1*8(%%rsi),    %%mm1         \n"
		"movq    2*8(%%rsi),    %%mm2         \n"
		"movq    3*8(%%rsi),    %%mm3         \n"
		"leaq    4*8(%%rsi),    %%rsi         \n"

		"movntq  %%mm0,         0*8(%%rdi)    \n"
		"movntq  %%mm1,         1*8(%%rdi)    \n"
		"movntq  %%mm2,         2*8(%%rdi)    \n"
		"movntq  %%mm3,         3*8(%%rdi)    \n"
		"addq    $32,           %%rdi         \n"

		"jnz     1b                           \n"

		"sfence                               \n"

		: [src]"=&S" (src), [dst]"=&D" (dst), [len]"=&c" (len)
		: "[src]" (src), "[dst]" (dst), "[len]" (len)
		: "cc"
		);

	kernel_fpu_end();

	return dst;
}

MEMCPY_INLINE void td_memcpy_movntq_flush(void *dst, const void *src,
		unsigned int len, unsigned flush_src, unsigned flush_dst)
{
	unsigned ofs;

	kernel_fpu_begin();

	len = (len + 31) & ~31;

	flush_src = (flush_src + 31) & ~31;
	flush_dst = (flush_dst + 31) & ~31;

	for (ofs=0; ofs<len; ofs += 32) {

		if (flush_src && !(ofs % flush_src))
			clflush_cache_range((void*)src, flush_src);

		__asm__ __volatile__ (
				"movq    0*8(%%rsi),   %%mm0          \n"
				"movq    1*8(%%rsi),   %%mm1          \n"
				"movq    2*8(%%rsi),   %%mm2          \n"
				"movq    3*8(%%rsi),   %%mm3          \n"

				"movntq  %%mm0,         0*8(%%rdi)    \n"
				"movntq  %%mm1,         1*8(%%rdi)    \n"
				"movntq  %%mm2,         2*8(%%rdi)    \n"
				"movntq  %%mm3,         3*8(%%rdi)    \n"

				:  /* no output */
				: "S" (src), "D" (dst)
				/* : "memory" */
				);

		if (flush_dst && !(ofs % flush_dst))
			clflush_cache_range(dst, flush_dst);

		src = PTR_OFS(src, 32);
		dst = PTR_OFS(dst, 32);
	}

	kernel_fpu_end();
}

MEMCPY_INLINE void td_memcpy_8x8_movq(void *dst, const void *src,
		unsigned int len)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"mov     %[t1],         0*8(%[dst])   \n"
		"mov     %[t2],         1*8(%[dst])   \n"
		"mov     %[t3],         2*8(%[dst])   \n"
		"mov     %[t4],         3*8(%[dst])   \n"
		"mov     %[t5],         4*8(%[dst])   \n"
		"mov     %[t6],         5*8(%[dst])   \n"
		"mov     %[t7],         6*8(%[dst])   \n"
		"mov     %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		: "cc"
		);

}


MEMCPY_INLINE void td_memcpy_8x8_movq_flush_src64(void *dst, const void *src,
		unsigned int len)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"clflush (%[src])                     \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"mov     %[t1],         0*8(%[dst])   \n"
		"mov     %[t2],         1*8(%[dst])   \n"
		"mov     %[t3],         2*8(%[dst])   \n"
		"mov     %[t4],         3*8(%[dst])   \n"
		"mov     %[t5],         4*8(%[dst])   \n"
		"mov     %[t6],         5*8(%[dst])   \n"
		"mov     %[t7],         6*8(%[dst])   \n"
		"mov     %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		: "cc"
		);

}

MEMCPY_INLINE void td_memcpy_8x8_movq_nt_wr_src64(void *dst, const void *src,
		unsigned int len)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;
	register uint64_t z = 0;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"xorq    %[z],          %[z]          \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"mov     %[t1],         0*8(%[dst])   \n"
		"mov     %[t2],         1*8(%[dst])   \n"
		"mov     %[t3],         2*8(%[dst])   \n"
		"mov     %[t4],         3*8(%[dst])   \n"
		"mov     %[t5],         4*8(%[dst])   \n"
		"mov     %[t6],         5*8(%[dst])   \n"
		"mov     %[t7],         6*8(%[dst])   \n"
		"mov     %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"movnti  %[z],          0*8(%[src])   \n"
		"movnti  %[z],          1*8(%[src])   \n"
		"movnti  %[z],          2*8(%[src])   \n"
		"movnti  %[z],          3*8(%[src])   \n"
		"movnti  %[z],          4*8(%[src])   \n"
		"movnti  %[z],          5*8(%[src])   \n"
		"movnti  %[z],          6*8(%[src])   \n"
		"movnti  %[z],          7*8(%[src])   \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jz      2f                           \n"
		"jmp     1b                           \n"
		"                                     \n"
		"2:                                   \n"
		"                                     \n"
		: [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len), [z]"=&r"(z)
		: "[src]"(src), "[dst]"(dst), "[len]"(len), "[z]"(z)
		: "cc"
		);

}

MEMCPY_INLINE void td_memcpy_8B_movnti(void *dst, const void *src)
{
	register uint64_t t1;

	__asm__ __volatile__ (
		"                                     \n"
		"mov     (%[src]),       %[t1]        \n"
		"movnti  %[t1],         (%[dst])      \n"
		"                                     \n"
		: [t1]"=&r"(t1)
		: [src]"S"(src), [dst]"D"(dst)
		);
}



MEMCPY_INLINE void td_memcpy_8x8_movnti(void *dst, const void *src,
		unsigned int len)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=r&"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		: 
		: "cc"
		);
}

MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum64(void *dst, const void *src,
		unsigned int len, uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"                                     \n"
		"xorq    %%rax,         %%rax         \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"addq    %[t1],         %[xsum0]      \n"
		"adcq    %[t2],         %[xsum0]      \n"
		"adcq    %[t3],         %[xsum0]      \n"
		"adcq    %[t4],         %[xsum0]      \n"
		"adcq    %[t5],         %[xsum0]      \n"
		"adcq    %[t6],         %[xsum0]      \n"
		"adcq    %[t7],         %[xsum0]      \n"
		"adcq    %[t8],         %[xsum0]      \n"
		"setc    %%al                         \n"
		"addq    %%rax,         %[xsum0]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "rax", "cc"
		);
}


/* copy multiple 64B chunks from src to dst using 8x8 blocks and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_8x8_movq_xsum128(void *dst, const void *src,
		unsigned int len, uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"leaq    8*8(%[src]),    %[src]       \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t8],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movq    %[t1],         0*8(%[dst])   \n"
		"movq    %[t2],         1*8(%[dst])   \n"
		"movq    %[t3],         2*8(%[dst])   \n"
		"movq    %[t4],         3*8(%[dst])   \n"
		"movq    %[t5],         4*8(%[dst])   \n"
		"movq    %[t6],         5*8(%[dst])   \n"
		"movq    %[t7],         6*8(%[dst])   \n"
		"movq    %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),    %[dst]       \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		: "cc"
		);
}


/* copy a single 56B chunk from src to dst using a single pass and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_56B_movq_xsum128(void *dst, const void *src,
		uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7;

	__asm__ __volatile__ (
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movq    %[t1],         0*8(%[dst])   \n"
		"movq    %[t2],         1*8(%[dst])   \n"
		"movq    %[t3],         2*8(%[dst])   \n"
		"movq    %[t4],         3*8(%[dst])   \n"
		"movq    %[t5],         4*8(%[dst])   \n"
		"movq    %[t6],         5*8(%[dst])   \n"
		"movq    %[t7],         6*8(%[dst])   \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7),
		  [src]"=&S"(src), [dst]"=&D"(dst)
		: "[src]"(src), "[dst]"(dst)
		);
}


/* copy a single 8B chunk from src to dst and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_8B_movq_xsum128(void *dst, const void *src,
		uint64_t *xsum)
{
	register uint64_t t1;

	__asm__ __volatile__ (
		"mov     0*8(%[src]),   %[t1]         \n"
		"                                     \n"
		"addq    %[t1],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movq    %[t1],         0*8(%[dst])   \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1),
		  [src]"=&S"(src), [dst]"=&D"(dst)
		: "[src]"(src), "[dst]"(dst)
		);
}


/* copy multiple 64B chunks from src to dst using 8x8 blocks and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum128(void *dst, const void *src,
		unsigned int len, uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t8],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "cc"
		);
}

/* read from 'src', a 64B multiple of bytes described by 'len', and write it
 * into dst_a and dst_b using non-temporal writes, while computing the
 * fletcher xsum which is stored into 'xsum'. */
MEMCPY_INLINE void td_double_memcpy_8x8_movnti_xsum128(void *dst_a, void *dst_b,
		const void *src, unsigned int len, uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"pushfq                               \n"
		"cli                                  \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t8],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst_a]) \n"
		"movnti  %[t2],         1*8(%[dst_a]) \n"
		"movnti  %[t3],         2*8(%[dst_a]) \n"
		"movnti  %[t4],         3*8(%[dst_a]) \n"
		"movnti  %[t5],         4*8(%[dst_a]) \n"
		"movnti  %[t6],         5*8(%[dst_a]) \n"
		"movnti  %[t7],         6*8(%[dst_a]) \n"
		"movnti  %[t8],         7*8(%[dst_a]) \n"
		"                                     \n"
		"addq    $64,           %[dst_a]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst_b]) \n"
		"movnti  %[t2],         1*8(%[dst_b]) \n"
		"movnti  %[t3],         2*8(%[dst_b]) \n"
		"movnti  %[t4],         3*8(%[dst_b]) \n"
		"movnti  %[t5],         4*8(%[dst_b]) \n"
		"movnti  %[t6],         5*8(%[dst_b]) \n"
		"movnti  %[t7],         6*8(%[dst_b]) \n"
		"movnti  %[t8],         7*8(%[dst_b]) \n"
		"                                     \n"
		"addq    $64,           %[dst_b]      \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		"popfq                                \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"+S"(src), [len]"+c"(len),
		  [dst_a]"+D"(dst_a), [dst_b]"+r"(dst_b)
		:
		: "cc"
		);
}


/* read from 'src', a 64B multiple of bytes described by 'len', and write it
 * into dst_a, dst_b and dst_c using non-temporal writes, while computing the
 * fletcher xsum which is stored into 'xsum'. */
MEMCPY_INLINE void td_triple_memcpy_8x8_movnti_xsum128(void *dst_a,
		void *dst_b, void *dst_c, const void *src,
		unsigned int len, uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7;

	len = (len + 63) & ~63;

	/* NOTE: there aren't enough registers, so RCX is overloaded for two uses:
	 * (1) ECX (lower 32bits of RCX) are set to the number of bytes to copy
	 * (2) RCX is used as the 8th word holding the data
	 * the length value is pushed on the stack while not in use.
	 */

	__asm__ __volatile__ (
		"pushfq                               \n"
		"cli                                  \n"
		"                                     \n"
		"1:                                   \n"
		"                                     \n"
		"pushq   %%rcx                        \n"  /* stash the length */
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %%rcx         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %%rcx,         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst_a]) \n"
		"movnti  %[t2],         1*8(%[dst_a]) \n"
		"movnti  %[t3],         2*8(%[dst_a]) \n"
		"movnti  %[t4],         3*8(%[dst_a]) \n"
		"movnti  %[t5],         4*8(%[dst_a]) \n"
		"movnti  %[t6],         5*8(%[dst_a]) \n"
		"movnti  %[t7],         6*8(%[dst_a]) \n"
		"movnti  %%rcx,         7*8(%[dst_a]) \n"
		"                                     \n"
		"addq    $64,           %[dst_a]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst_b]) \n"
		"movnti  %[t2],         1*8(%[dst_b]) \n"
		"movnti  %[t3],         2*8(%[dst_b]) \n"
		"movnti  %[t4],         3*8(%[dst_b]) \n"
		"movnti  %[t5],         4*8(%[dst_b]) \n"
		"movnti  %[t6],         5*8(%[dst_b]) \n"
		"movnti  %[t7],         6*8(%[dst_b]) \n"
		"movnti  %%rcx,         7*8(%[dst_b]) \n"
		"                                     \n"
		"addq    $64,           %[dst_b]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst_c]) \n"
		"movnti  %[t2],         1*8(%[dst_c]) \n"
		"movnti  %[t3],         2*8(%[dst_c]) \n"
		"movnti  %[t4],         3*8(%[dst_c]) \n"
		"movnti  %[t5],         4*8(%[dst_c]) \n"
		"movnti  %[t6],         5*8(%[dst_c]) \n"
		"movnti  %[t7],         6*8(%[dst_c]) \n"
		"movnti  %%rcx,         7*8(%[dst_c]) \n"
		"                                     \n"
		"addq    $64,           %[dst_c]      \n"
		"                                     \n"
		"popq    %%rcx                        \n"  /* restore length */
		"                                     \n"
		"subl    $(8*8),        %%ecx         \n"
		"jnz     1b                           \n"
		"                                     \n"
		"popfq                                \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7),
		  [src]"+S"(src), [len]"+c"(len),
		  [dst_a]"+D"(dst_a),
		  [dst_b]"+r"(dst_b),
		  [dst_c]"+r"(dst_c)
		:
		: "cc"
		);
}


/* copy a single 56B chunk from src to dst using a single pass and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_56B_movnti_xsum128(void *dst, const void *src,
		uint64_t *xsum)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7;

	__asm__ __volatile__ (
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
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
		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"addq    %[t7],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7),
		  [src]"+S"(src), [dst]"+D"(dst)
		:
		: "cc"
		);
}


/* copy a single 8B chunk from src to dst and updating
 * fletcher checksum (uint64_t[2]) */
MEMCPY_INLINE void td_memcpy_8B_movnti_xsum128(void *dst, const void *src,
		uint64_t *xsum)
{
	register uint64_t t1;

	__asm__ __volatile__ (
		"mov     (%[src]),      %[t1]         \n"
		"                                     \n"
		"addq    %[t1],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"                                     \n"
		"movnti  %[t1],         (%[dst])      \n"
		"                                     \n"
		: [t1]"=&r"(t1),
		  [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1])
		:
		  [src]"S"(src), [dst]"D"(dst)
		);
}


MEMCPY_INLINE void td_memcpy_8x8_movnti_xsum128_e2e(void *dst,
		const void *src, unsigned int len,
		uint64_t *xsum, /* array of 2 u64 for fletcher checksum */
		uint64_t *e2e)  /* a second parallel checksum for e2e */
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"                                     \n"
		"mov     0*8(%[src]),   %[t1]         \n"
		"mov     1*8(%[src]),   %[t2]         \n"
		"mov     2*8(%[src]),   %[t3]         \n"
		"mov     3*8(%[src]),   %[t4]         \n"
		"mov     4*8(%[src]),   %[t5]         \n"
		"mov     5*8(%[src]),   %[t6]         \n"
		"mov     6*8(%[src]),   %[t7]         \n"
		"mov     7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"

		"addq    %[t1],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t1],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t2],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t2],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t3],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t3],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t3],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t4],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t4],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t5],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t5],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t6],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t6],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t7],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"addq    %[t8],         %[xsum0]      \n"
		"addq    %[xsum0],      %[xsum1]      \n"
		"addq    %[t8],         %[e2e0]       \n"
		"addq    %[e2e0],       %[e2e1]       \n"

		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [xsum0]"+r"(xsum[0]), [xsum1]"+r"(xsum[1]),
		  [e2e0]"+r"(e2e[0]), [e2e1]"+r"(e2e[1]),
		  [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "cc"
		);
}


MEMCPY_INLINE void td_memcpy_movntdqa_64(void *dst, const void *src,
		unsigned int len)
{
#if defined __KERNEL__ && ! defined KABI__movntdqa
#warning movntdqa not supported by compiler
	BUG_ON(1);
#else
#if 0
	WARN_ON((uintptr_t)dst&63ULL);
	WARN_ON((uintptr_t)src&63ULL);
	WARN_ON(len&63ULL);
#endif
	/* fall back to slow copy on misalignment */
	if ( (uintptr_t)dst&15ULL || (uintptr_t)src&15ULL
			|| len&63ULL ) {
		memcpy(dst, src, len);
		return;
	}

	kernel_fpu_begin();
	__asm__ __volatile__ (
		"1:\n"

		"movntdqa    0*16(%%rsi),  %%xmm0       \n"
		"movntdqa    1*16(%%rsi),  %%xmm1       \n"
		"movntdqa    2*16(%%rsi),  %%xmm2       \n"
		"movntdqa    3*16(%%rsi),  %%xmm3       \n"
		"addq        $(4*16),      %%rsi        \n"

		"movdqa      %%xmm0,       0*16(%%rdi)  \n"
		"movdqa      %%xmm1,       1*16(%%rdi)  \n"
		"movdqa      %%xmm2,       2*16(%%rdi)  \n"
		"movdqa      %%xmm3,       3*16(%%rdi)  \n"
		"leaq        4*16(%%rdi),  %%rdi        \n"

		"subl        $64,          %%ecx        \n" /* 4 * 16 == 64 */
		"jnz         1b                         \n"

		"sfence                                 \n"

		: [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		: "cc"
		);
	kernel_fpu_end();
#endif
}

MEMCPY_INLINE void td_memcpy_movntdqa_16(void *dst, const void *src,
		unsigned int len)
{
#if defined __KERNEL__ && ! defined KABI__movntdqa
#warning movntdqa not supported by compiler
	BUG_ON(1);
#else
#if 0
	WARN_ON((uintptr_t)dst&15ULL);
	WARN_ON((uintptr_t)src&15ULL);
	WARN_ON(len&15ULL);
#endif
	/* fall back to slow copy on misalignment */
	if ( (uintptr_t)dst&15ULL || (uintptr_t)src&15ULL
			|| len&15ULL ) {
		memcpy(dst, src, len);
		return;
	}

	kernel_fpu_begin();
	__asm__ __volatile__ (
		"1:\n"

		"movntdqa    0*16(%%rsi),  %%xmm0       \n"
		"addq        $16,          %%rsi        \n"

		"movdqa      %%xmm0,       0*16(%%rdi)  \n"
		"addq        $16,          %%rdi        \n"

		"subl        $16,          %%ecx        \n"
		"jnz         1b                         \n"

		"sfence                                 \n"

		: [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		: "cc"
		);
	kernel_fpu_end();
#endif
}

MEMCPY_INLINE void td_zero_8B_movnti(void *dst)
{
	register uint64_t z = 0;

	__asm__ __volatile__ (
		"xorq    %[z],          %[z]         \n"
		"movnti  %[z],          (%[dst])     \n"
		"                                     \n"
		: [z]"=&r"(z)
		: [dst]"r"(dst)
		);
}

MEMCPY_INLINE void td_fill_8B_movnti(void *dst, uint64_t word, unsigned int count)
{
	__asm__ __volatile__ (
		"1:                             \n"
		"                               \n"
		"movnti    %[word],   (%[dst])  \n"
		"addq      $8,        %[dst]    \n"
		"                               \n"
		"decl      %[count]             \n"
		"jnz       1b                   \n"
		"                               \n"
		: [count]"+&c"(count), [dst]"+&D"(dst)
		: [word]"r"(word)
		: "cc"
		);
}

MEMCPY_INLINE void td_zero_8x8_movnti(void *dst, unsigned int len)
{
	register uint64_t z = 0;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"xorq    %[z],          %[z]         \n"
		"1:                                   \n"
		"movnti  %[z],          0*8(%[dst])   \n"
		"movnti  %[z],          1*8(%[dst])   \n"
		"movnti  %[z],          2*8(%[dst])   \n"
		"movnti  %[z],          3*8(%[dst])   \n"
		"movnti  %[z],          4*8(%[dst])   \n"
		"movnti  %[z],          5*8(%[dst])   \n"
		"movnti  %[z],          6*8(%[dst])   \n"
		"movnti  %[z],          7*8(%[dst])   \n"
		"                                     \n"
		"addq    $(8*8),        %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jz      2f                           \n"
		"jmp     1b                           \n"
		"                                     \n"
		"2:                                   \n"
		"                                     \n"
		: [dst]"+D"(dst), [len]"+c"(len), [z]"=r"(z)
		:
		: "cc"
		);

}

MEMCPY_INLINE void td_memcpy_8x8_movnti_cli_64B(void *dst,
		const void *src)
{
	// allow the compiler to decide on which registers are used
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	// Interrupts are disabled to prevent preemption
	__asm__ __volatile__ (
		"  pushfq                               \n"
		"  cli                                  \n"
		"                                       \n"
		"  mov     0*8(%[src]),   %[t1]         \n"
		"  mov     1*8(%[src]),   %[t2]         \n"
		"  mov     2*8(%[src]),   %[t3]         \n"
		"  mov     3*8(%[src]),   %[t4]         \n"
		"  mov     4*8(%[src]),   %[t5]         \n"
		"  mov     5*8(%[src]),   %[t6]         \n"
		"  mov     6*8(%[src]),   %[t7]         \n"
		"  mov     7*8(%[src]),   %[t8]         \n"
		"                                       \n"
		"  mfence                               \n"
		"                                       \n"
		"  movnti  %[t1],         0*8(%[dst])   \n"
		"  movnti  %[t2],         1*8(%[dst])   \n"
		"  movnti  %[t3],         2*8(%[dst])   \n"
		"  movnti  %[t4],         3*8(%[dst])   \n"
		"  movnti  %[t5],         4*8(%[dst])   \n"
		"  movnti  %[t6],         5*8(%[dst])   \n"
		"  movnti  %[t7],         6*8(%[dst])   \n"
		"  movnti  %[t8],         7*8(%[dst])   \n"
		"                                       \n"
		"  mfence                               \n"
		"                                       \n"
		"  popfq                                \n"
		"                                       \n"
		: [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"=&S"(src), [dst]"=&D"(dst)
		: "[src]"(src), "[dst]"(dst)
		);
}

MEMCPY_INLINE void td_memcpy_4x16_movntq(void *dst, const void *src,
		unsigned int len)
{
	BUG_ON((uintptr_t)dst&15ULL);
	BUG_ON((uintptr_t)src&15ULL);
	BUG_ON(len&63U);

	kernel_fpu_begin();

	// Interrupts are disabled to prevent preemption
	__asm__ __volatile__ (
		"1:                                     \n"
		"  movdqa  0*16(%[src]),  %%xmm0        \n"
		"  movdqa  1*16(%[src]),  %%xmm1        \n"
		"  movdqa  2*16(%[src]),  %%xmm2        \n"
		"  movdqa  3*16(%[src]),  %%xmm3        \n"
		"                                       \n"
		"  leaq    4*16(%[src]),  %[src]        \n"
		"                                       \n"
		"  sfence                               \n"
		"                                       \n"
		"  movntdq %%xmm0,        0*16(%[dst])  \n"
		"  movntdq %%xmm1,        1*16(%[dst])  \n"
		"  movntdq %%xmm2,        2*16(%[dst])  \n"
		"  movntdq %%xmm3,        3*16(%[dst])  \n"
		"                                       \n"
		"  addq    $64,           %[dst]        \n"
		"                                       \n"
		"  sfence                               \n"
		"                                       \n"
		"  subl    $(4*16),       %[len]        \n"
		"  jnz     1b                           \n"
		"                                       \n"
		: [src]"=&S"(src), [dst]"=&D"(dst), [len]"=&c"(len)
		: "[src]"(src), "[dst]"(dst), "[len]"(len)
		);

	kernel_fpu_end();
}

MEMCPY_INLINE void td_memcpy_4x16_movntq_cli_64B(void *dst,
		const void *src)
{
	BUG_ON((uintptr_t)dst&15ULL);
	BUG_ON((uintptr_t)src&15ULL);

	kernel_fpu_begin();

	// Interrupts are disabled to prevent preemption
	__asm__ __volatile__ (
		"  pushfq                               \n"
		"  cli                                  \n"
		"                                       \n"
		"  movdqa  0*16(%[src]),  %%xmm0        \n"
		"  movdqa  1*16(%[src]),  %%xmm1        \n"
		"  movdqa  2*16(%[src]),  %%xmm2        \n"
		"  movdqa  3*16(%[src]),  %%xmm3        \n"
		"                                       \n"
		"  sfence                               \n"
		"                                       \n"
		"  movntdq %%xmm0,        0*16(%[dst])  \n"
		"  movntdq %%xmm1,        1*16(%[dst])  \n"
		"  movntdq %%xmm2,        2*16(%[dst])  \n"
		"  movntdq %%xmm3,        3*16(%[dst])  \n"
		"                                       \n"
		"  sfence                               \n"
		"                                       \n"
		"  popfq                                \n"
		"                                       \n"
		: [src]"=&S"(src), [dst]"=&D"(dst)
		: "[src]"(src), "[dst]"(dst)
		);

	kernel_fpu_end();
}

/* fill memory with given word */
MEMCPY_INLINE void td_fill_8x8_movnti(void *dst, uint64_t word, unsigned int len)
{
	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"1:                                   \n"
		"movnti  %[w],          0*8(%[dst])   \n"
		"movnti  %[w],          1*8(%[dst])   \n"
		"movnti  %[w],          2*8(%[dst])   \n"
		"movnti  %[w],          3*8(%[dst])   \n"
		"movnti  %[w],          4*8(%[dst])   \n"
		"movnti  %[w],          5*8(%[dst])   \n"
		"movnti  %[w],          6*8(%[dst])   \n"
		"movnti  %[w],          7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jz      2f                           \n"
		"jmp     1b                           \n"
		"                                     \n"
		"2:                                   \n"
		"                                     \n"
		: [dst]"+&D"(dst), [len]"+&c"(len)
		: [w]"r"(word)
		: "cc"
		);

}

/* copy from src to dst, when a bad word is detected, that line is flushed */
MEMCPY_INLINE void td_memcpy_8x8_movq_bad_clflush(void *dst, const void const *const src_alias[2],
		unsigned int len, uint64_t bad)
{
	register const void *a0 = src_alias[0];
	register const void *a1 = src_alias[1];
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	/* read using "fast path"
	 * - read in 64B, test last word of reach cacheline for 'bad' pattern
	 * - if found go to slow path
	 * - else write to destination
	 * - repeat
	 */

	__asm__ __volatile__ (
		"mfence                               \n"
		"\n"
		"\n" /* fast loop, look for 'bad' pattern */
		"\n"
		"1:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"   /* read out the cache line */
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"movq    4*8(%[src]),   %[t5]         \n"
		"movq    5*8(%[src]),   %[t6]         \n"
		"movq    6*8(%[src]),   %[t7]         \n"
		"movq    7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"cmpq    %[t1],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t2],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t3],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t4],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t5],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t6],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t7],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"cmpq    %[t8],         %[bad]        \n"   /* test against the bad pattern */
		"je      2f                           \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"   /* it's good */
		"leaq    8*8(%[alias]), %[alias]      \n"   /* it's good */
		"                                     \n"
		"movq    %[t1],         0*8(%[dst])   \n"   /* copy to destination */
		"movq    %[t2],         1*8(%[dst])   \n"
		"movq    %[t3],         2*8(%[dst])   \n"
		"movq    %[t4],         3*8(%[dst])   \n"
		"movq    %[t5],         4*8(%[dst])   \n"
		"movq    %[t6],         5*8(%[dst])   \n"
		"movq    %[t7],         6*8(%[dst])   \n"
		"movq    %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"   /* count down length */
		"jnz     1b                           \n"
		"jmp     9f                           \n"   /* reached the end */
		"\n"
		"\n" /* bad pattern found, use the alias */
		"\n"
		"2:                                   \n"
		"movq    %[alias],      %[src]        \n"
		"                                     \n"
		"movq    %%rcx,         %[t2]         \n"   /* remember length */
		"                                     \n"
		"mfence                               \n"   /* alias must be invalidated */
		"3:                                   \n"
		"clflush (%[src])                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"subl    $(8*8),        %[len]        \n"   /* count down length */
		"jnz     3b                           \n"
		"mfence                               \n"
		"                                     \n"
		"movq    %[t2],         %%rcx         \n"   /* restore length */
		"subq    %%rcx,         %[src]        \n"
		"                                     \n"
		"\n"
		"\n" /* now reading from invalidated alias */
		"\n"
		"4:                                   \n"
		"                                     \n"
		"movq    0*8(%[src]),   %[t1]         \n"   /* read out the cache line */
		"movq    1*8(%[src]),   %[t2]         \n"
		"movq    2*8(%[src]),   %[t3]         \n"
		"movq    3*8(%[src]),   %[t4]         \n"
		"movq    4*8(%[src]),   %[t5]         \n"
		"movq    5*8(%[src]),   %[t6]         \n"
		"movq    6*8(%[src]),   %[t7]         \n"
		"movq    7*8(%[src]),   %[t8]         \n"
		"                                     \n"
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"movq    %[t1],         0*8(%[dst])   \n"   /* copy to destination */
		"movq    %[t2],         1*8(%[dst])   \n"
		"movq    %[t3],         2*8(%[dst])   \n"
		"movq    %[t4],         3*8(%[dst])   \n"
		"movq    %[t5],         4*8(%[dst])   \n"
		"movq    %[t6],         5*8(%[dst])   \n"
		"movq    %[t7],         6*8(%[dst])   \n"
		"movq    %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"leaq    8*8(%[dst]),   %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"   /* count down length */
		"jnz     4b                           \n"
		"                                     \n"
		"9:                                   \n"   /* exit */
		"                                     \n"
		: [t1]"=r"(t1), [t2]"=r"(t2), [t3]"=r"(t3), [t4]"=r"(t4),
		  [t5]"=r"(t5), [t6]"=r"(t6), [t7]"=r"(t7), [t8]"=r"(t8),
		  [src]"+S"(a0), [alias]"+r"(a1),
		  [dst]"+D"(dst), [len]"+c"(len), [bad]"+r"(bad)
		:
		: "cc"
		);

}


