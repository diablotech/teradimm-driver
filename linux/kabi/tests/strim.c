#include <linux/kernel.h>
#include <linux/string.h>

const char* foo(void) {
	return strim("Some text ");
}

