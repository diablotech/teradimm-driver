/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. (Diablo).              *
 *    All rights reserved.                                               *
 *                                                                       *
 *    This program is free software; you can redistribute it and/or      *
 *    modify it under the terms of the GNU General Public License        *
 *    as published by the Free Software Foundation; either version 2     *
 *    of the License, or (at your option) any later version located at   *
 *    <http://www.gnu.org/licenses/                                      *
 *                                                                       *
 *    This program is distributed WITHOUT ANY WARRANTY; without even     *
 *    the implied warranty of MERCHANTABILITY or FITNESS FOR A           *
 *    PARTICULAR PURPOSE.  See the GNU General Public License for        *
 *    more details.                                                      *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_device.h"
#include "td_monitor.h"
#include "td_mon.h"
#include "td_control.h"

static uint td_monitor_enable = 1;
static uint td_monitor_rate = TD_MONITOR_PERIOD;

/* TODO: deprecated, remove when we reach 1.5 */
module_param_named(monitor_enable, td_monitor_enable, uint, 0444);
MODULE_PARM_DESC(monitor_enable,
		"deprecated, use 'monitor' instead.");

module_param_named(monitor, td_monitor_enable, uint, 0444);
MODULE_PARM_DESC(monitor,
		"ECC monitor task (0 disable, 1 enable/default)");


static int td_monitor_task(void *thread_data)
{
	int rate;

	while (!kthread_should_stop()) {

		rate = td_monitor_rate;

		if (likely(td_monitor_rate)) {
			td_monitor_ecc_poll_all_devs();
		}
		else {
			rate = TD_MONITOR_PERIOD;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(rate * HZ);
	}

	return 0;
}

static struct task_struct *monitor_task;

int __init td_monitor_init(void)
{
	if (td_monitor_enable) {
		monitor_task = kthread_run(td_monitor_task, NULL, "td/monitor");
		pr_warn("Started thread td_monitor\n");
	}
	return 0;
}

int td_monitor_exit(void)
{
	int rc = 0;
	if (monitor_task) {
		rc = kthread_stop(monitor_task);
		pr_warn("Stopped thread td_monitor\n");
	}
	return rc;
}

void td_monitor_set_rate(int rate)
{
	td_monitor_rate = rate;
}
