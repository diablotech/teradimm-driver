#include <linux/kernel.h>
#include <linux/bio.h>

unsigned int foo(struct bio *bi) {
	return bi->bi_size;
}
