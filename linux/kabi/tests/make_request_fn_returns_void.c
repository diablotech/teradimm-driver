#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/blkdev.h>

void foo_request_fn(struct request_queue *rq, struct bio *bio)
{
}

void foo(struct request_queue *rq)
{
	blk_queue_make_request(rq, foo_request_fn);
}
