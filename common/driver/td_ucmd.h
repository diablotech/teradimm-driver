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

#ifndef _TD_UCMD_H_
#define _TD_UCMD_H_

#include "td_kdefn.h"


#include "td_ioctl.h"
#include "td_util.h"

struct td_engine;

struct td_ucmd {
	/* this must be first in the structure */
	struct td_ioctl_device_cmd_pt ioctl;
	/* data used by read/write operations */
	void        *data_virt;
	struct page *data_page;

	struct list_head         queue_link;

	wait_queue_head_t        wq;
	atomic_t                 ref;
	union {
		uint8_t flags;
		struct {
			uint8_t hw_cmd:1;
			uint8_t locked:1;
		};
	};
	struct task_struct *user_task;
};

void td_ucmd_init(struct td_ucmd *ucmd);
struct td_ucmd* td_ucmd_alloc(int virt_size);


static inline void td_ucmd_ready(struct td_ucmd *ucmd)
{
	ucmd->ioctl.result = -EBUSY;
}

int td_ucmd_map(struct td_ucmd *ucmd,
		struct task_struct *task, unsigned long addr);

void td_ucmd_unmap(struct td_ucmd *ucmd);

static inline void td_ucmd_get(struct td_ucmd *ucmd)
{
	atomic_inc(&ucmd->ref);
}

static inline void td_ucmd_put(struct td_ucmd *ucmd)
{
	if (atomic_dec_and_test(&ucmd->ref)) {
		td_ucmd_unmap(ucmd);
		kfree(ucmd);
	}
}

static inline void td_ucmd_starting(struct td_ucmd *ucmd)
{
	ucmd->ioctl.cycles.io.start = td_get_cycles();
}

static inline void td_ucmd_ending(struct td_ucmd *ucmd, int result)
{
	ucmd->ioctl.cycles.io.end = td_get_cycles();
	ucmd->ioctl.result        = result;

	mb();
	
	/* wake up any td_ucmd_wait() callers */
	wake_up_interruptible(&ucmd->wq);
}

static inline int td_ucmd_wait(struct td_ucmd *ucmd)
{
	int rc;

	/* wait for completion */
	rc = wait_event_interruptible_timeout(ucmd->wq,
			ucmd->ioctl.result != -EBUSY, 60*HZ);
	if (rc > 0) {
		/* was woken up */
		rc = ucmd->ioctl.result;
	} else if (!rc) {
		/* timeout occured */
		rc = -ETIMEDOUT;
	}

	return rc;
}

int td_ucmd_run(struct td_ucmd *ucmd, struct td_engine *eng);


#endif
