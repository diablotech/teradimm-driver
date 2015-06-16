#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/slab.h>

void* foo(void) {
	return kzalloc_node(4096, GFP_KERNEL, 0);
}

