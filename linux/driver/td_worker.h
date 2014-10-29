/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2014 Diablo Technologies Inc. (Diablo).              *
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

#ifndef _TD_WORKER_H_
#define _TD_WORKER_H_

#include "td_kdefn.h"

#include <linux/notifier.h>

#include "td_defs.h"
#include "td_cpu.h"
#include "td_bio.h"

#define TD_WORKER_USEC(x)             ((x)*1000ULL)
#define TD_WORKER_MSEC(x)             TD_WORKER_USEC((x)*1000ULL)

struct td_device;
struct td_devgroup;

struct td_work_item;
struct td_worker;
struct td_work_node;

#define TD_WORKER_MAX_OCCUPANCY       2              /* how many devices per worker */
#define TD_WORKER_EXTRA_TOKENS        0              /* how many additional workers can wake up */

#define TD_WORKER_MAX_LOOPS           1000000UL      /* max loops */

/*
 * If we're avoiding events, this should be small, because
 * another worker will quickly wake up and get it.
 */
#ifdef TERADIMM_CONFIG_AVOID_EVENTS
#define TD_WORKER_WITHOUT_DEVS_NSEC   TD_WORKER_USEC(100)   /* 100us before giving up on scouting */
#else
#define TD_WORKER_WITHOUT_DEVS_NSEC   TD_WORKER_MSEC(2)     /* 2ms   before giving up on scouting */
#endif

#define TD_WORKER_SHARE_NSEC          TD_WORKER_MSEC(10)    /* 10ms  before sharing devices unconditionally */
#define TD_WORKER_RELEASE_NSEC        TD_WORKER_MSEC(10)    /* 10ms  before releasing a scouted device */

#define TD_WORKER_FORCE_RELEASE_NSEC  TD_WORKER_MSEC(500)   /* 500ms  before giving up on devices */

#define TD_WORKER_DEV_IDLE_JIFFIES    2 /* a device that's only been idle for this long is kept active */
#define TD_WORKER_DEV_WAIT_JIFFIES    2 /* a device that's only been idle for this long will get scouted */

#define TD_WORKER_SLEEP_JIFFIES_MIN   10        /* sleep at least this long */
#define TD_WORKER_SLEEP_JIFFIES_MAX   (60*HZ)   /* sleep at most this long */
#define TD_WORKER_SYNC_JIFFIES        HZ        /* sync times out after this long */

#define TD_WORKER_WAKE_SHARE          1
#define TD_WORKER_WAKE_SLEEP          0

#define TD_WORKER_MAX_PER_NODE        8         /* this limits the number of threads per node */

#define TD_WORK_ITEM_MAX_PER_NODE     8         /* this limits the number of devices per node */

/* ------------------------------------------------------------------------ */
/* work_item goes into the td_engine that will be worked on */

struct td_work_item {
	struct td_device    *wi_device;

	struct list_head    wi_scout_link;      /**< link in active worker's list of devices */
	struct list_head    wi_active_link;     /**< link in active worker's list of devices */

	unsigned long       wi_last_scouting;   /**< jiffies of the last time the device was scouted */
	unsigned long       wi_last_activity;   /**< jiffies of the last time the device had something to do */
	unsigned long       wi_loops;           /**< incremented on each worker loop */

	wait_queue_head_t   wi_sync;            /**< events that the synchronize function waits on */
	atomic_t            wi_sync_req;        /**< used to request the thread to wake up the synchronizing thread */

	unsigned            wi_can_run:1;       /**< ready to go */

	atomic_t            wi_scouted_count;   /**< zero if no one is scouting it, non-zero otherwise */

	struct td_worker    *wi_scout_worker;   /**< worker thread currently scouting this device */
	struct td_worker    *wi_active_worker;  /**< worker thread currently working on this device */
};

static inline void td_work_item_init(struct td_work_item *wi,
		struct td_device *dev)
{
	memset(wi, 0, sizeof(struct td_work_item));

	wi->wi_device = dev;
	init_waitqueue_head(&wi->wi_sync);
	atomic_set(&wi->wi_sync_req, 0);
	atomic_set(&wi->wi_scouted_count, 0);
}

static inline void td_work_item_enable(struct td_work_item *wi)
{
	wi->wi_can_run = 1;
	mb();
}

static inline void td_work_item_disable(struct td_work_item *wi)
{
	wi->wi_can_run = 0;
	mb();
}

static inline bool td_work_item_can_run(struct td_work_item *wi)
{
	return !! ( wi->wi_can_run );
}

static inline bool td_work_item_needs_poke(struct td_work_item *wi, uint64_t td_worker_dev_idle_jiffies)
{
	long diff;

	if (!wi)
		return false;

	diff = jiffies - wi->wi_last_scouting;
	if (diff > td_worker_dev_idle_jiffies)
		return true;

	return false;
}

#if 0
static inline void td_work_item_sometimes_poke(struct td_work_item *wi)
{
	struct td_device *dev;

	if (!td_work_item_needs_poke(wi))
		return;

	dev = wi->wi_device;
	td_devgroup_poke(dev->td_devgroup);
}
#endif

/* ------------------------------------------------------------------------ */

struct td_worker {
	struct {
		unsigned            count;
		struct list_head    list;
	} w_scout_devs, w_active_devs;

	unsigned            w_cpu;
	struct task_struct  *w_task;

	struct td_work_node *w_work_node;

	unsigned long       w_sleep_start;          /*!< jiffies of sleep start */
	unsigned long       w_loops;
	unsigned long       w_total_activity;
	unsigned            w_work_item_scan_offset;

	unsigned            w_devices_shared:1;
	unsigned            w_devices_scouted:1;
	unsigned            w_has_work_token:1;
	unsigned            w_going_down:1;

	cycles_t    w_cycles_wake;                  /*!< cycles when thread work up */
	cycles_t    w_cycles_without_devices;       /*!< thread could not scout anything */
	cycles_t    w_cycles_share_devices;         /*!< thread needs to share devices */
	cycles_t    w_cycles_release_devices;       /*!< thread can be released if it's scouted */
	cycles_t    w_cycles_force_release_devices; /*!< thread has been running for too long */

	uint64_t    counters[TD_DEVGROUP_WORKER_COUNT_MAX];
};

static inline void td_worker_init(struct td_worker *w,
		int cpu, struct td_work_node *wn)
{
	memset(w, 0, sizeof(*w));
	INIT_LIST_HEAD(&w->w_scout_devs.list);
	INIT_LIST_HEAD(&w->w_active_devs.list);
	w->w_cpu = cpu;
	w->w_work_node = wn;
}

extern int td_worker_start(struct td_worker *w);
extern int td_worker_stop(struct td_worker *w);

#define td_worker_for_each_work_item(w,type,wi) \
	list_for_each_entry(wi,&w->w_##type##_devs.list,wi_##type##_link)

#define td_worker_for_each_work_item_safe(w,type,wi,tmp_wi) \
	list_for_each_entry_safe(wi,tmp_wi,&w->w_##type##_devs.list,wi_##type##_link)


/* ------------------------------------------------------------------------ */
/* work_node goes into the td_devgroup that contains all the td_worker threads
 * there is one of these per NUMA node */

/**
 * A work node ties together work items (devices) and workers (threads).
 * Work items are scouted-by and kept active-by workers.
 *
 * Only one worker can own a given state of a work item, but it is possible
 * for two workers to respectively own scouted-by and active-by states.
 *
 * The td_work_item data structure holds the hold counter and pointer for each
 * state.  The hold counter is zero when no worker owns that state, and non
 * zero if the state is owned by another thread.  The pointer to the worker
 * holding that state can only be set after the worker acquired the hold
 * counter.
 *
 * When a worker increments the hold counter and the operation results in a
 * value greater than 1, the worker lost the acquire to another worker, and
 * must decrement the hold counter.  If the decrement results in zero, it
 * could try again.
 */
struct td_work_node {

	struct td_devgroup *wn_devgroup;

	/* workers and work items (number used from fixed-sized arrays) */

	unsigned            wn_work_item_count;
	unsigned            wn_worker_count;

	/** worker tokens are allocated by threads that are waking up.
	 * only a certain number of threads are allowed to run at a given
	 * time, based on configuration. */
	TD_DECLARE_IN_PRIVATE_CACHE_LINE(1,
		atomic_t    wn_worker_tokens;
		);

	/** this is the number of worker tokens that have been forfeited 
	 * by threads before sleeping due to inactivity.  they are 
	 * put back in use when the device group is poked. */
	atomic_t    wn_worker_tokens_idle;

	/** this is the total number of tokens that are in use, the number is
	 * recalculated on the fly as devices are added */
	unsigned            wn_total_worker_tokens;
	atomic_t            wn_token_recompute_needed;

	atomic_t            wn_work_item_available_count; /**< number of devs with no td_worker_scout */
	atomic_t            wn_work_item_active_count;    /**< number of devs with td_worker_active set */

	/** work items represent devices that need to be worked on */
	struct td_work_item wn_work_items[TD_WORK_ITEM_MAX_PER_NODE];

	/** workers represent threads that are part of the device group */
	struct td_worker    *wn_workers;

};

extern int td_work_node_init(struct td_work_node *wn, struct td_devgroup *dg);
extern void td_work_node_exit(struct td_work_node *wn);

extern int td_work_node_start(struct td_work_node *wn);
extern void td_work_node_stop(struct td_work_node *wn);

extern int td_work_node_attach_device(struct td_work_node *wn, struct td_device *dev);
extern void td_work_node_detach_device(struct td_work_node *wn, struct td_device *dev);

extern int td_work_node_synchronize_item(struct td_work_node *wn, struct td_work_item *wi);

#endif
