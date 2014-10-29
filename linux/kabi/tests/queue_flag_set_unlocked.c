#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>

struct request_queue;
void foo(struct request_queue *q)
{
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, q);

}
