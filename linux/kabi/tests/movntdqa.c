#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/i387.h>


void td_memcpy_movntdqa(void *dst, const void *src,
		unsigned int len)
{
	kernel_fpu_begin();
	__asm__ __volatile__ (
		"1:\n"

		"movntdqa    0*16(%%rsi),  %%xmm0       \n"
		"movntdqa    1*16(%%rsi),  %%xmm1       \n"
		"movntdqa    2*16(%%rsi),  %%xmm2       \n"
		"movntdqa    3*16(%%rsi),  %%xmm3       \n"
		"leaq        4*16(%%rsi),  %%rsi        \n"

		"movdqa      %%xmm0,       0*16(%%rdi)  \n"
		"movdqa      %%xmm1,       1*16(%%rdi)  \n"
		"movdqa      %%xmm2,       2*16(%%rdi)  \n"
		"movdqa      %%xmm3,       3*16(%%rdi)  \n"
		"leaq        4*16(%%rdi),  %%rdi        \n"

		"subl        $64,          %%ecx        \n" /* 4 * 16 == 64 */
		"jnz         1b                         \n"

		"sfence                                 \n"

		: /* no output */
		: "S" (src), "D" (dst), "c" (len)
		/* : "memory" */
		);
	kernel_fpu_end();
}

