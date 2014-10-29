#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>
#include <linux/slab.h>

#ifdef LINUX_VERSION_CODE = KERNEL_VERSION(2,6,18)
#define clflush_cache_range(_a,_b) UNEXPORTED(_a,_b)
#endif

void foo() {
	void *ptr = 0;
	unsigned int size = 0;


	clflush_cache_range(ptr, size);
}

