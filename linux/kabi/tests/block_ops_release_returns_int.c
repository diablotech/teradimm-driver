#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/blkdev.h>

struct gendisk;

static int block_ops_release(struct gendisk *gd, fmode_t fm)
{
	(void)gd;
	(void)fm;
	return 0;
}

struct block_device_operations foo = {
	.release = block_ops_release,
};
