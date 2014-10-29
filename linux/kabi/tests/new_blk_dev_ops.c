#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/blkdev.h>

static int foo_block_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

struct block_device_operations foo_block_fops = {
	.open = foo_block_open,
};
