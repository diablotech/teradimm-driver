#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/slab.h>

void* foo(void) {
	return vmalloc_node(4096, 0);
}

