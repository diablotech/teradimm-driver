#include <linux/kernel.h>
#include <linux/dmi.h>

static void dmi_walk_cb(const struct dmi_header *dm)
{
}

void foo(void)
{
	dmi_walk(&dmi_walk_cb);
}
