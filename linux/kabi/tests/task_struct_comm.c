#define __KERNEL__
#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/sched.h>

void foobar (struct task_struct *ts)
{
	printk("pid %u[%s] releasing device with locked buffers\n",
		ts->pid, ts->comm);
}
