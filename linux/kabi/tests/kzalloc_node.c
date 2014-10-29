#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>
#include <linux/slab.h>

void* foo() {
	return kzalloc_node(4096, GFP_KERNEL, 0);
}

