#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>

int foo() {
	u64 now = 0;
	u64 then = 0;
	return time_after64(now, then);
}
