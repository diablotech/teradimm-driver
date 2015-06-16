#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/slab.h>

void foo(void) {
	void *ptr = 0;
	clflush(ptr);
}

