#define __KERNEL__
#include <linux/kernel.h>
#include <linux/blkdev.h>

struct request_queue;
void foo(struct request_queue *q)
{
	blk_queue_max_segments(q, 0);
}
