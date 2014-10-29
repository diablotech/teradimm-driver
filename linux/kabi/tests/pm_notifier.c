#define __KERNEL__
#include <linux/kconfig.h>
#include <linux/suspend.h>

struct notifier_block *nb;
void reg() {
	register_pm_notifier(nb);

}
