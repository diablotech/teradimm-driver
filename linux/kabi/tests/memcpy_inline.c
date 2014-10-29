#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/types.h>


static inline void td_memcpy_8x8_movq_xsum128(void *dst, const void *src,
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
		);
}
void foo (void *dst, void *src, unsigned int len, uint64_t *xsum)
{
	uint64_t values[1000];
	int i;
	for (i = 0; i < 1000; i++)
		values[i] = i;

	len = len + values[0];
	td_memcpy_8x8_movq_xsum128(dst, src, len, xsum);
}
