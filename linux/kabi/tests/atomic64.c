#define __KERNEL__
#include <linux/kconfig.h>
#include <asm/atomic.h>
#include <linux/types.h>

uint64_t foo() {
	atomic64_t bar;
	atomic64_set(&bar, 2ULL);
	atomic64_xchg(&bar, 1ULL);
	return atomic64_read(&bar);
}

