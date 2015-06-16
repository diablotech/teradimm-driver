/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("DIABLO").  All       *
 *    rights reserved.                                                   *
 *                                                                       *
 *    This software is being licensed under a dual license, at Diablo's  *
 *    sole discretion.                                                   *
 *                                                                       *
 *    GPL License                                                        *
 *                                                                       *
 *    If you do not have explicit permission from Diablo, then you may   *
 *    only redistribute it and/or modify it under the terms of the GNU   *
 *    General Public License as published by the Free Software           *
 *    Foundation; either version 2 of the License, or (at your option)   *
 *    any later version located at <http://www.gnu.org/licenses/>.  See  *
 *    the GNU General Public License for more details.                   *
 *                                                                       *
 *    BSD License                                                        *
 *                                                                       *
 *    If you have explicit permission from Diablo, then redistribution   *
 *    and use in source and binary forms, with or without modification,  *
 *    are permitted provided that the following conditions are met:      *
 *                                                                       *
 *        * Redistributions of source code must retain the above         *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer.                                                    *
 *                                                                       *
 *        * Redistributions in binary form must reproduce the above      *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer in the documentation and/or other materials         *
 *        provided with the distribution.                                *
 *                                                                       *
 *        * Neither the name of the DIABLO nor the names of its          *
 *        contributors may be used to endorse or promote products        *
 *        derived from this software without specific prior written      *
 *        permission.                                                    *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL         *
 *    DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE   *
 *    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS      *
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,       *
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING           *
 *    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS *
 *    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.       *
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

#define TD_WORKER_MAX_OCCUPANCY       1              /* how many devices per worker */
#define TD_WORKER_EXTRA_TOKENS        0              /* how many additional workers can wake up */

#define TD_WORKER_MAX_LOOPS           1000000UL      /* max loops */

#define TD_WORKER_WITHOUT_DEVS_NSEC   TD_WORKER_MSEC(2)     /* 2ms   before giving up on scouting */
#define TD_WORKER_IDLE_SHARE_NSEC     TD_WORKER_USEC(1)     /* 1us   before sharing devices if they are idle */
#define TD_WORKER_BUSY_SHARE_NSEC     TD_WORKER_MSEC(10)    /* 10ms  before sharing devices if they are busy */

#define TD_WORKER_FORCE_RELEASE_NSEC  TD_WORKER_MSEC(500)   /* 500ms  before giving up on devices */

#define TD_WORKER_DEV_WAIT_JIFFIES    1 /* device will go WAITING state in 1 ms after being used last */
#define TD_WORKER_DEV_IDLE_JIFFIES    2 /* device will go IDLE state in 2 ms after being used last */

#define TD_WORKER_SLEEP_JIFFIES_MIN   0         /* sleep at least this long */
#define TD_WORKER_SLEEP_JIFFIES_MAX   0         /* sleep at most this long (zero means using exclusive events) */
#define TD_WORKER_SYNC_JIFFIES        HZ        /* sync times out after this long */

#define TD_WORKER_EXTRA_TOKEN_JIFFIES 5*HZ      /* when an extra token is injected, it's kept around for 5 seconds */

#define TD_WORKER_WAKE_SHARE          1         /* when sharing devices, wake up this number of workers */
#define TD_WORKER_WAKE_SLEEP          0         /* when going to sleep, wake up this number of workers */

#define TD_WORKER_MAX_PER_NODE        64        /* this limits the number of threads per node */

#ifdef TERADIMM_CONFIG_AVOID_EVENTS
#undef TD_WORKER_WITHOUT_DEVS_NSEC
#undef TD_WORKER_SLEEP_JIFFIES_MIN
/* on VMware, we don't use events, but instead rely on a lot of threads that
 * sleep for a short amount of time between doing work */
#define TD_WORKER_WITHOUT_DEVS_NSEC   TD_WORKER_USEC(100)   /* 100us before giving up on scouting */
#define TD_WORKER_SLEEP_JIFFIES_MIN   1         /* put each worker to sleep for this long */
#endif

#define TD_WORK_ITEM_MAX_PER_NODE     32        /* this limits the number of devices per node */

/* ------------------------------------------------------------------------ */
/* worker debug */

static inline void __td_work_node_counter_inc(struct td_work_node *wn,
		enum td_devgroup_node_counter which);
#define td_work_node_counter_inc(_wn,_which) \
	__td_work_node_counter_inc(_wn,TD_DEVGROUP_NODE_COUNT_##_which)

static inline void __td_worker_counter_inc(struct td_worker *w,
		enum td_devgroup_worker_counter which);
#define td_worker_counter_inc(_w,_which) \
	__td_worker_counter_inc(_w,TD_DEVGROUP_WORKER_COUNT_##_which)
#define td_worker_counter_get(_w,_which) \
	__td_worker_counter_get(_w,TD_DEVGROUP_WORKER_COUNT_##_which)

#ifdef CONIFG_TERADIMM_WORKER_FTRACE
#define td_work_ftrace(fmt,a...) trace_printk(fmt,##a)
#define TD_WORK_TOKEN_ACCOUNTING
#else
#define td_work_ftrace(fmt,a...) do { /* nothing */ } while(0)
#undef TD_WORK_TOKEN_ACCOUNTING
#endif

/* ------------------------------------------------------------------------ */
/* work_item goes into the td_engine that will be worked on */

struct td_work_item {
	struct td_device    *wi_device;

	struct list_head    wi_scout_link;      /**< link in active worker's list of devices */
	struct list_head    wi_active_link;     /**< link in active worker's list of devices */

	unsigned long       wi_last_incoming;   /**< jiffies of the last time a new IO arrived */
	unsigned long       wi_last_scouting;   /**< jiffies of the last time the device was scouted */
	unsigned long       wi_last_activity;   /**< jiffies of the last time the device had something to do */
	unsigned long       wi_last_state_check;/**< jiffies of the last time state change was attempted */
	unsigned long       wi_loops;           /**< incremented on each worker loop */

	wait_queue_head_t   wi_sync;            /**< events that the synchronize function waits on */
	atomic_t            wi_sync_req;        /**< used to request the thread to wake up the synchronizing thread */

	unsigned            wi_can_run:1;       /**< ready to go */
	unsigned            wi_has_work_token:1;/**< set is this work item holding a work token */

	atomic_t            wi_scouted_count;   /**< zero if no one is scouting it, non-zero otherwise */

	struct td_worker    *wi_scout_worker;   /**< worker thread currently scouting this device */
	struct td_worker    *wi_active_worker;  /**< worker thread currently working on this device */

	atomic_t            wi_state;           /**< see enum td_work_item_state */
};

enum td_work_item_state {
	WI_IDLE,                                /**< nothing to do */
	WI_ACTIVE,                              /**< working on IO */
	WI_WAITING,                             /**< waiting for IO */
	WI_STATE_MAX
};
extern const char * td_work_item_state_name[WI_STATE_MAX];

static inline void td_work_item_init(struct td_work_item *wi,
		struct td_device *dev)
{
	memset(wi, 0, sizeof(struct td_work_item));

	wi->wi_device = dev;
	init_waitqueue_head(&wi->wi_sync);
	atomic_set(&wi->wi_sync_req, 0);
	atomic_set(&wi->wi_scouted_count, 0);
	atomic_set(&wi->wi_state, WI_IDLE);
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

static inline enum td_work_item_state td_work_item_state(struct td_work_item *wi)
{
	return atomic_read(&wi->wi_state);
}

static inline bool td_work_item_is_state(struct td_work_item *wi,
		enum td_work_item_state test)
{
	return td_work_item_state(wi) == test;
}

/* forward declarations */
static inline void __td_work_node_poke(struct td_work_node *wn);
static inline void td_work_node_create_token(struct td_work_node *wn);
static inline void td_work_node_destroy_token(struct td_work_node *wn);
static inline void td_work_item_destroys_token(struct td_work_node *wn,
		struct td_work_item *wi);

/* change state to ACTIVE, if change was from IDLE then create a new token,
 * return true if this call had to change from non-ACTIVE to ACTIVE */
static inline bool td_work_item_going_active(struct td_work_node *wn,
		struct td_work_item *wi)
{
	enum td_work_item_state was;

	/* record when we last saw requests */
	if (wi->wi_last_incoming < jiffies) {
		td_work_ftrace("TD going_active jiffies update (%ld)\n",
			jiffies - wi->wi_last_incoming);
		wi->wi_last_incoming = jiffies;
		mb();
	}

	/* if we are not active, try to become active */
	was = WI_ACTIVE;
	if (!td_work_item_is_state(wi, WI_ACTIVE))
		was = atomic_xchg(&wi->wi_state, WI_ACTIVE);

	td_work_ftrace("TD going_active %s -> ACTIVE\n",
		td_work_item_state_name[was]);

	switch (was) {
	default:
	case WI_ACTIVE:
		/* no change */
		return false;

	case WI_IDLE:
		/* this was an idle device, that got some new work */
		td_work_node_counter_inc(wn, STATE_ACTIVE);
		td_work_node_create_token(wn);
		return true;

	case WI_WAITING:
		/* this device was winding down, but got more work. */
		td_work_node_counter_inc(wn, STATE_ACTIVE);
		return true;
	}
}

/* called by engine when we get more work to do
 * returns true if it had to signal the thread (was IDLE or WAITING) */
static inline bool td_work_item_poke(struct td_work_node *wn,
		struct td_work_item *wi)
{
	bool went_active;

	if (td_work_item_is_state(wi, WI_ACTIVE)
			&& jiffies == wi->wi_last_incoming)
		return 0;
	
	went_active = td_work_item_going_active(wn, wi);
	if (went_active)
		/* we need an event */
		__td_work_node_poke(wn);

	return went_active;
}

/* called by worker when it runs out of things to do on a device,
 * returns true if the device went into a WAITING state */
static inline bool td_work_item_going_waiting(struct td_work_node *wn,
		struct td_work_item *wi)
{
	enum td_work_item_state was;

	td_work_ftrace("TD going_waiting (%d,%s,%ld)\n",
		wi->wi_has_work_token,
		td_work_item_state_name[td_work_item_state(wi)],
		jiffies - wi->wi_last_incoming);

	/* to go into a WAITING state, we must be ACTIVE */
	if (!td_work_item_is_state(wi, WI_ACTIVE))
		return false;

	/* don't bother if we received IO recently */
	if (wi->wi_last_incoming >= jiffies)
		return false;

	/* enter WAITING state */
	was = atomic_xchg(&wi->wi_state, WI_WAITING);

	td_work_ftrace("TD going_waiting %s -> WAITING\n",
		td_work_item_state_name[was]);

	if (was != WI_WAITING)
		td_work_node_counter_inc(wn, STATE_WAITING);

	return true;
}

/* called by worker when WAITING device expires it's DEV_IDLE_JIFFIES */
static inline void td_work_item_going_idle(struct td_work_node *wn,
		struct td_work_item *wi)
{
#if 0
	long idle_in, diff;
#endif
	enum td_work_item_state was;

	if (td_work_item_is_state(wi, WI_WAITING)
			|| (jiffies - wi->wi_last_activity) < 5)
	td_work_ftrace("TD going_idle (%d,%s,%ld)\n",
		wi->wi_has_work_token,
		td_work_item_state_name[td_work_item_state(wi)],
		jiffies - wi->wi_last_activity);

	/* we will only attempt to go IDLE while we are holding
	 * a work token so that it can be destroyed */
	if (!wi->wi_has_work_token)
		return;

	/* don't bother checking if it's not in WAITING state */
	if (!td_work_item_is_state(wi, WI_WAITING))
		return;

	/* don't bother if we received IO recently */
	if (wi->wi_last_incoming >= jiffies)
		return;

	/* going from WAITING to IDLE */
	was = atomic_cmpxchg(&wi->wi_state, WI_WAITING, WI_IDLE);

	td_work_ftrace("TD going_idle %s -> IDLE\n",
		td_work_item_state_name[was]);

	switch (was) {
	case WI_WAITING:
		/* successfully switched from WAITING to IDLE now */
		td_work_item_destroys_token(wn, wi);
		td_work_node_counter_inc(wn, STATE_IDLE);
		break;

	default:
	case WI_IDLE:
	case WI_ACTIVE:
		/* no action required */
		break;
	}
}

static inline bool td_work_item_needs_poke(struct td_work_item *wi)
{
	if (!wi)
		return false;

	if (jiffies <= wi->wi_last_activity)
		return false;

	if (td_work_item_is_state(wi, WI_ACTIVE))
		return false;

	return true;
}

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

#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	uint64_t            w_last_wait_jiffies;
#endif

	unsigned            w_unused_work_tokens;
	unsigned            w_activated_devices;

	unsigned            w_scouting_allowed:1;
	unsigned            w_devices_shared:1;
	unsigned            w_devices_idled:1;
	unsigned            w_going_down:1;

#ifdef CONIFG_TERADIMM_WORKER_FTRACE
	unsigned    already_said_make_devices_available_for_scouting:1;
#endif

	cycles_t    w_cycles_wake;                  /*!< cycles when thread work up */
	cycles_t    w_cycles_without_devices;       /*!< thread could not scout anything */
	cycles_t    w_cycles_idle_share_devices;    /*!< thread needs to share devices, only if it's idle */
	cycles_t    w_cycles_busy_share_devices;    /*!< thread needs to share devices, even if it's busy */
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

static inline void __td_worker_counter_inc(struct td_worker *w,
		enum td_devgroup_worker_counter which)
{
	w->counters[which] ++;
}

static inline uint64_t __td_worker_counter_get(struct td_worker *w,
		enum td_devgroup_worker_counter which)
{
	return w->counters[which];
}

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

	unsigned            wn_node;
	struct td_devgroup *wn_devgroup;

	/* workers and work items (number used from fixed-sized arrays) */

	unsigned            wn_work_item_count;
	unsigned            wn_worker_count;

	/** event that the worker threads wait on */
	wait_queue_head_t   wn_event;

	/** this is the number of workers tokens that are available for
	 * threads that wake up.  when a thread wakes up, it can only continue
	 * if there is a token available.  each token represents a device that
	 * needs work.  tokens are created when a device becomes ACTIVE, and
	 * destroyed when a device goes IDLE.  while a device is being worked
	 * on, the total count is reduced temporarily. */
	TD_DECLARE_IN_PRIVATE_CACHE_LINE(1,
		atomic_t    wn_work_token_pool;
		);

#ifdef TD_WORK_TOKEN_ACCOUNTING
	/* DEBUG, only used if CONIFG_TERADIMM_WORKER_FTRACE is on */
	atomic_t            wn_work_tokens_total;
	atomic_t            wn_work_tokens_worker;
	atomic_t            wn_work_tokens_device;
#endif

	atomic_t            wn_extra_work_token;
	unsigned long       wn_extra_work_token_expires;

	atomic_t            wn_work_item_available_count; /**< number of devs with no td_worker_scout */
	atomic_t            wn_work_item_active_count;    /**< number of devs with td_worker_active set */

	/** work items represent devices that need to be worked on */
	struct td_work_item wn_work_items[TD_WORK_ITEM_MAX_PER_NODE];

	/** workers represent threads that are part of the device group */
	struct td_worker    *wn_workers;

	uint64_t    counters[TD_DEVGROUP_NODE_COUNT_MAX];
};

extern int td_work_node_init(struct td_work_node *wn, struct td_devgroup *dg,
		unsigned node);
extern void td_work_node_exit(struct td_work_node *wn);

extern int td_work_node_start(struct td_work_node *wn);
extern void td_work_node_stop(struct td_work_node *wn);

extern int td_work_node_attach_device(struct td_work_node *wn, struct td_device *dev);
extern void td_work_node_detach_device(struct td_work_node *wn, struct td_device *dev);

extern int td_work_node_synchronize_item(struct td_work_node *wn, struct td_work_item *wi);

static inline void __td_work_node_counter_inc(struct td_work_node *wn,
		enum td_devgroup_node_counter which)
{
		wn->counters[which] ++;
}

/* send event to workers */
static inline void __td_work_node_poke(struct td_work_node *wn)
{
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	td_work_ftrace("TD work_node %u poke\n", wn->wn_node);
	wake_up_interruptible(&wn->wn_event);
#endif
}

/* create a work token for a short amount of time which will cause
 * worker threads to spin up and do maintenance tasks; called by
 * td_devgroup_poke() */
static inline void td_work_node_poke(struct td_work_node *wn)
{
	int was;

	wn->wn_extra_work_token_expires = jiffies + TD_WORKER_EXTRA_TOKEN_JIFFIES;
	mb();

	was = atomic_xchg(&wn->wn_extra_work_token, 1);
	if (was == 0) {
		/* new token was created */
		td_work_ftrace("TD work_node %u creates extra token\n", wn->wn_node);
		td_work_node_create_token(wn);
	} else
		td_work_ftrace("TD work_node %u extended extra token for %ld ms\n",
				wn->wn_node, wn->wn_extra_work_token_expires - jiffies);

	__td_work_node_poke(wn);
}

static inline void td_work_node_extra_token_cleanup(struct td_work_node *wn)
{
	int was;

	if (jiffies < wn->wn_extra_work_token_expires)
		return;

	was = atomic_xchg(&wn->wn_extra_work_token, 0);
	if (was == 1) {
		/* this token is done */
		td_work_ftrace("TD work_node %u destroys extra token\n", wn->wn_node);
		td_work_node_destroy_token(wn);
	}
}

static inline void td_work_node_extra_token_yeild(struct td_work_node *wn)
{
	if (atomic_read(&wn->wn_extra_work_token))
		schedule();
}

/**
 * the work token life cycle
 *
 *  - device moving from IDLE->ACTIVE increments wn_work_token_pool
 *     ... @see td_work_node_create_token()
 *
 *  - worker waking up grabs MAX_OCCUPANCY tokens
 *     ... @see td_worker_acquire_work_tokens()
 *
 *  - worker gives token to activated device
 *     ... @see td_worker_gives_token_to_work_item()
 *
 *  - when device is shared...
 *    - if device is idle, token is destroyed
 *       ... @see td_work_item_destroys_token()
 *
 *    - otherwise, token is returned to wn_work_token_pool
 *       ... @see td_work_item_retires_token()
 *
 *  - worker with left over tokens, puts them into wn_work_token_pool
 *     ... @see td_worker_release_work_tokens()
 *
 * how tokens are accounted
 *
 *  - available tokens in wn->wn_work_token_pool
 *  - worker holding tokens before scouting w->w_unused_work_tokens
 *  - scouted/active device holds token in wi->wi_has_work_token
 *
 */

/* create token; called when worker moves from IDLE->ACTIVE and for handling
 * extra tokens */
static inline void td_work_node_create_token(struct td_work_node *wn)
{
	atomic_inc(&wn->wn_work_token_pool);
#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_inc(&wn->wn_work_tokens_total);
#endif
	td_work_ftrace("TD work_node %u create token (a%d:%d+%d/%d:%d)\n", wn->wn_node,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));
}

/* destroy token; called when extra token expires */
static inline void td_work_node_destroy_token(struct td_work_node *wn)
{
	atomic_dec(&wn->wn_work_token_pool);
#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_dec(&wn->wn_work_tokens_total);
#endif
	td_work_ftrace("TD work_node %u destroyed token (a%d:%d+%d/%d:%d)\n", wn->wn_node,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));
}

/* worker activates a device; giving the token to the work_item */
static inline void td_worker_gives_token_to_work_item(struct td_worker *w,
		struct td_work_item *wi)
{
	WARN_ON(w->w_unused_work_tokens < 1);
	WARN_ON(wi->wi_has_work_token);

	w->w_unused_work_tokens --;
	wi->wi_has_work_token = 1;

#ifdef TD_WORK_TOKEN_ACCOUNTING
	{
	struct td_work_node *wn = w->w_work_node;
	atomic_dec(&wn->wn_work_tokens_worker);
	atomic_inc(&wn->wn_work_tokens_device);
	}
#endif

#ifdef CONIFG_TERADIMM_WORKER_FTRACE
	{
	struct td_work_node *wn = w->w_work_node;
	td_work_ftrace("TD worker %u gives token to work item (w%d:a%d:%d+%d/%d:%d)\n", w->w_cpu,
			w->w_unused_work_tokens,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));
	}
#endif
}

/* IDLE work item destroys the token */
static inline void td_work_item_destroys_token(struct td_work_node *wn,
		struct td_work_item *wi)
{
	if (!wi->wi_has_work_token)
		return;

	wi->wi_has_work_token = 0;

#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_dec(&wn->wn_work_tokens_device);
	atomic_dec(&wn->wn_work_tokens_total);
#endif

	td_work_ftrace("TD work_node %u destroys token (a%d:%d+%d/%d:%d)\n", wn->wn_node,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));
}

/* work item releases it's token before being shared */
static inline bool td_work_item_retires_token(struct td_work_node *wn,
		struct td_work_item *wi)
{
	if (!wi->wi_has_work_token)
		return false;

	wi->wi_has_work_token = 0;
	atomic_inc(&wn->wn_work_token_pool);

#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_dec(&wn->wn_work_tokens_device);
#endif

	td_work_ftrace("TD work_node %u retires token (a%d:%d+%d/%d:%d)\n", wn->wn_node,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));

	return true;
}


#endif
