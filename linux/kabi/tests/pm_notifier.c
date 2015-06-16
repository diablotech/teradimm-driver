#include <linux/kernel.h>
#include <linux/suspend.h>

struct notifier_block *nb;
void reg (void)
{
	register_pm_notifier(nb);

}
