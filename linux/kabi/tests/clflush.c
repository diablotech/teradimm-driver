#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>
#include <linux/slab.h>

void foo() {
	void *ptr = 0;
	clflush(ptr);
}

