#include <linux/kernel.h>
#include <asm/io.h>

void __iomem *test(resource_size_t offset, unsigned long size) {
	return ioremap_wc(offset, size);
}
