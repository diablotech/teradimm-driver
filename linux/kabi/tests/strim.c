
#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/bio.h>
#include <linux/string.h>

const char* foo() {
	return strim("Some text ");
}

