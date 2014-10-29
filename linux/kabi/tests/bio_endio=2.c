#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>

void foo(struct bio *bio)
{
	bio_endio(bio, 0);
}
