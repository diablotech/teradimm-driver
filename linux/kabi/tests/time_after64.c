#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bio.h>

void foo(struct bio *bio)
{
	bio_endio(bio, 0);
}

int bar (void)
{
	u64 now = 0;
	u64 then = 0;
	return time_after64(now, then);
}
