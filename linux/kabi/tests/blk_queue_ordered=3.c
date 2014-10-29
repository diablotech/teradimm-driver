#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>

struct request_queue;
int foo(struct request_queue *q)
{
	return blk_queue_ordered(q, 0, 0);
}
