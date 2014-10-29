#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>

void foo( struct request_queue *q)
{
	q->limits.discard_granularity = 1;
}
