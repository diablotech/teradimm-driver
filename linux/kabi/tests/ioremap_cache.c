#define __KERNEL__
#include <linux/kconfig.h>
#include <asm/io.h>

void __iomem *test(resource_size_t offset, unsigned long size) {
	return ioremap_cache(offset, size);
}
