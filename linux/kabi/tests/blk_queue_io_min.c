#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>

struct request_queue;
void foo(struct request_queue *q)
{
	blk_queue_io_min(q, 1);

}
