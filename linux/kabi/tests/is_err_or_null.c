#include <linux/kernel.h>
#include <linux/err.h>

void *ptr;
void foo(void)
{
	if (IS_ERR_OR_NULL(ptr)) {
		;
	}
}
