#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>
#include <linux/slab.h>

void* foo() {
	return vmalloc_node(4096, 0);
}

