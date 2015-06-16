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

#include "td_kdefn.h"

#include "td_worker.h"
#include "td_devgroup.h"
#include "td_device.h"
#include "td_eng_hal.h"
#include "td_compat.h"

#define wi_trace(_wi_,_t_,_l_,_x_) \
	td_eng_trace(td_device_engine((_wi_)->wi_device), _t_, _l_, _x_)

const char * td_work_item_state_name[WI_STATE_MAX] = { "IDLE", "ACTIVE", "WAITING" };

#ifdef TERADIMM_CONFIG_AVOID_EVENTS
uint td_workers_per_node = TD_WORKER_MAX_PER_NODE;
#else
/* We want dynamic workers/node */
uint td_workers_per_node = 0;
#endif

module_param_named(max_workers_per_node, td_workers_per_node, uint, 0644);
MODULE_PARM_DESC(max_workers_per_node, "How many workers run in each node");

static uint td_idle_msec = 2;
module_param_named(idle_msec, td_idle_msec, uint, 0644);
MODULE_PARM_DESC(idle_msec, "How long the thread remains idle in milliseconds");

static uint td_yield_msec = 100;
module_param_named(yield_msec, td_yield_msec, uint, 0644);
MODULE_PARM_DESC(yield_msec, "How often to yield the thread in milliseconds");

static void td_work_item_sync_event(struct td_work_item *wi);
static int td_worker_thread(void *thread_data);

int td_worker_start(struct td_worker *w)
{
	struct td_work_node *wn;
	struct td_devgroup *dg;
	int rc;

	if (w->w_task)
		return 0;

	wn = w->w_work_node;
	BUG_ON(!wn);

	dg = wn->wn_devgroup;
	BUG_ON(!dg);

	w->w_unused_work_tokens = 0;
	w->w_loops = 0;
	w->w_devices_shared = 0;
	w->w_devices_idled = 0;
	w->w_going_down = 0;

	w->w_task = kthread_create(td_worker_thread, w,
			TD_THREAD_NAME_PREFIX "%s/%u",
			dg->dg_name, w->w_cpu);

	if (unlikely(IS_ERR_OR_NULL(w->w_task)))
		goto error_create;

	kthread_bind(w->w_task, w->w_cpu);
	wake_up_process(w->w_task);

	return 0;

error_create:
	pr_err("Failed "TD_THREAD_NAME_PREFIX "%s/%u "
			"thread creation: err=%ld\n", dg->dg_name,
			w->w_cpu, PTR_ERR(w->w_task));
	rc = PTR_ERR(w->w_task) ?: -EFAULT;
	w->w_task = NULL;
	return rc;
}

int td_worker_stop(struct td_worker *w)
{
	struct td_work_node *wn;
	struct td_devgroup *dg;
	int rc = 0;

	if (!w->w_task)
		goto error_stop;

	wn = w->w_work_node;
	BUG_ON(!wn);

	dg = wn->wn_devgroup;
	BUG_ON(!dg);

	w->w_going_down = 1;
	mb();

#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	wake_up_interruptible_all(&wn->wn_event);
#endif

	rc = kthread_stop(w->w_task);
	w->w_task = NULL;
	pr_warn("Stopped thread "TD_THREAD_NAME_PREFIX "%s/%u\n",
			dg->dg_name, w->w_cpu);

error_stop:
	return rc;
}


static inline int __td_worker_scout_device(struct td_worker *w,
		struct td_work_item *wi)
{
	struct td_work_node *wn = w->w_work_node;

	if ( atomic_inc_return(&wi->wi_scouted_count) != 1 ) {
		/* someone else has scouted this device */
		atomic_dec(&wi->wi_scouted_count);
		return -EBUSY;
	}

	WARN_ON(wi->wi_scout_worker);

	wi->wi_last_scouting = jiffies;

	td_device_hold(wi->wi_device);
	atomic_dec(&wn->wn_work_item_available_count);

	wi->wi_scout_worker = w;

	mb();

	list_add_tail(&wi->wi_scout_link, &w->w_scout_devs.list);
	w->w_scout_devs.count ++;

	/* transfer work token to device */
	td_worker_gives_token_to_work_item(w, wi);

	td_work_ftrace("TD worker %u scouts %s (%u)\n",
			w->w_cpu,
			td_device_name(wi->wi_device),
			w->w_scout_devs.count);

	return 0;
}

static inline void __td_worker_activate_scouted_device(struct td_worker *w,
		struct td_work_item *wi)
{
	struct td_work_node *wn = w->w_work_node;

	WARN_ON(wi->wi_scout_worker != w);
	WARN_ON(wi->wi_active_worker);

	td_device_hold(wi->wi_device);
	atomic_inc(&wn->wn_work_item_active_count);

	wi->wi_active_worker = w;

	mb();

	list_add_tail(&wi->wi_active_link, &w->w_active_devs.list);
	w->w_active_devs.count ++;
	w->w_activated_devices ++;

	wi_trace(wi, TR_SCOUT, "WORKER-ACTIVATE", w->w_loops);

	td_work_ftrace("TD worker %u activates %s (%u)\n",
			w->w_cpu,
			td_device_name(wi->wi_device),
			w->w_active_devs.count);
}

static inline void __td_worker_unscout_device(struct td_worker *w,
		struct td_work_item *wi)
{
	struct td_work_node *wn = w->w_work_node;

	WARN_ON(wi->wi_scout_worker != w);

	/* before token is shared, release its token */
	td_work_item_retires_token(wn, wi);

	if(wi->wi_active_worker == w)
		wi_trace(wi, TR_SCOUT, "WORKER-UNSCOUT", w->w_loops);

	list_del(&wi->wi_scout_link);

	wmb();

	wi->wi_scout_worker = NULL;
	w->w_scout_devs.count --;
	atomic_dec(&wi->wi_scouted_count);

	td_work_ftrace("TD worker %u unscouts %s (%u)\n",
			w->w_cpu,
			td_device_name(wi->wi_device),
			w->w_scout_devs.count);

	atomic_inc(&wn->wn_work_item_available_count);
	td_work_item_sync_event(wi);
	td_device_put(wi->wi_device);
}

static inline void __td_worker_deactivate_device(struct td_worker *w,
		struct td_work_item *wi)
{
	struct td_work_node *wn = w->w_work_node;

	WARN_ON(wi->wi_active_worker != w);

	wi_trace(wi, TR_SCOUT, "WORKER-DEACTIVATE", w->w_loops);

	list_del(&wi->wi_active_link);

	wmb();

	wi->wi_active_worker = NULL;
	w->w_active_devs.count --;

	td_work_ftrace("TD worker %u deactivates %s (%u)\n",
			w->w_cpu,
			td_device_name(wi->wi_device),
			w->w_active_devs.count);

	atomic_dec(&wn->wn_work_item_active_count);
	td_work_item_sync_event(wi);
	td_device_put(wi->wi_device);
}

/* worker acquires token; called by a worker that woke up */
static inline bool td_worker_acquire_work_tokens(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;
	int max, after, overdrawn;

	/* we want to grab this many elements */
	max = td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY);
	WARN_ON(max > TD_WORK_ITEM_MAX_PER_NODE);
	WARN_ON(w->w_unused_work_tokens);

	/* grab what we can */
	after = atomic_sub_return(max, &wn->wn_work_token_pool);

	if (after >= 0) {
		/* allocation was successful */
		w->w_unused_work_tokens = max;
#ifdef TD_WORK_TOKEN_ACCOUNTING
		atomic_add(max, &wn->wn_work_tokens_worker);
#endif
		td_work_ftrace("TD worker %u allocates %u tokens\n", w->w_cpu,
				max);
		return true;
	}

	/* 'after' is negative, which means it the token pool had fewer than
	 * 'max' tokens.  'overdrawn' holds the value that we were short */
	overdrawn = 0 - after;

	if (overdrawn >= max) {
		/* we were unable to allocate any tokens */
		atomic_add(max, &wn->wn_work_token_pool);
		td_work_ftrace("TD worker %u fails to allocate tokens\n", w->w_cpu);
		return false;
	}

	td_work_ftrace("TD worker %u allocates %u tokens (overdrawn %u)\n",
			w->w_cpu, max, overdrawn);

	/* now we are in a funny place where we grabbed some tokens but
	 * not all that we wanted, but we were the first to get here
	 * so we allow it.  next worker to wake up will get overdrawn
	 * value >= max, and will fail to proceed.  this covers the case
	 * of having a number of tokens out there that's not divisible
	 * by max_occupancy */
	w->w_unused_work_tokens = max;
#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_add(max, &wn->wn_work_tokens_worker);
#endif
	return true;
}

static inline void td_worker_release_work_tokens(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	unsigned count;

	count = w->w_unused_work_tokens;
	if (!count)
		return;

#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_sub(count, &wn->wn_work_tokens_worker);
#endif

	w->w_unused_work_tokens = 0;
	atomic_add(count, &wn->wn_work_token_pool);

	td_work_ftrace("TD worker %u retires %u unused token (w%d:a%d:%d+%d/%d:%d)\n", w->w_cpu, count,
			w->w_unused_work_tokens,
			atomic_read(&wn->wn_work_token_pool),
			atomic_read(&wn->wn_work_tokens_worker),
			atomic_read(&wn->wn_work_tokens_device),
			atomic_read(&wn->wn_work_tokens_total),
			atomic_read(&wn->wn_extra_work_token));
}

#if 0
static inline void td_worker_release_work_token(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;

	if ( !w->w_has_work_token )
		return;

	atomic_inc(&wn->wn_worker_tokens);
	w->w_has_work_token = 0;
}


static inline void td_worker_consume_work_token(struct td_worker *w)
{
#ifdef TERADIMM_CONFIG_AVOID_EVENTS
	/* If we are avoiding events, we can't consume tokens */
	td_worker_release_work_token(w);
#else
	struct td_work_node *wn = w->w_work_node;

	if ( !w->w_has_work_token )
		return;

	atomic_inc(&wn->wn_worker_tokens_idle);
	w->w_has_work_token = 0;
#endif

	td_work_ftrace("TD worker %u consumed (%u)\n", w->w_cpu,
			atomic_read(&wn->wn_worker_tokens_idle));
}
#endif

static inline bool td_work_item_has_work_to_do(struct td_work_item *wi)
{
	struct td_device *dev = NULL;
	struct td_engine *eng = NULL;
	struct td_devgroup *dg = NULL;
	struct td_work_node *wn = NULL;

	if (!td_work_item_can_run(wi))
		return false;

	dev = wi->wi_device;
	eng = &dev->td_engine;
	dg = dev->td_devgroup;
	wn = &dg->dg_work_node;

	if (!td_work_item_is_state(wi, WI_IDLE))
		return true;

	/* if it's IDLE, check if it actually wants the CPU */
	if (td_engine_needs_cpu_time(eng))
		if (td_work_item_going_active(wn, wi))
			/* it changed its mind, and went ACTIVE */
			return true;

	/* it's IDLE and doesn't want the CPU */
	return false;
}


static inline void td_worker_starting_active_loop(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;

	cycles_t now = td_get_cycles();

	w->w_cycles_wake = now;
	w->w_cycles_without_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, WITHOUT_DEVS_NSEC));
	w->w_cycles_idle_share_devices   = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, IDLE_SHARE_NSEC));
	w->w_cycles_busy_share_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, BUSY_SHARE_NSEC));
	w->w_cycles_force_release_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, FORCE_RELEASE_NSEC));

	WARN_ON(w->w_cycles_without_devices < now);
	WARN_ON(w->w_cycles_idle_share_devices < now);
	WARN_ON(w->w_cycles_busy_share_devices < now);
	WARN_ON(w->w_cycles_force_release_devices < now);

	w->w_loops = 0;
	w->w_scouting_allowed = 1;
	w->w_devices_shared = 0;
	w->w_devices_idled = 0;
	w->w_total_activity = 0;
	w->w_activated_devices = 0;

#ifdef CONIFG_TERADIMM_WORKER_FTRACE
	w->already_said_make_devices_available_for_scouting = 0;
#endif
}

static inline void td_worker_acquire_a_device(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;
	unsigned i, ndx;

	if (!dg->dg_devs_count) {
		return;
	}

	if (atomic_read(&wn->wn_work_item_available_count) <= 0
			&& !w->w_scout_devs.count) {
		return;
	}

	/* each time this worker scans the list, start at a different offset */
	w->w_work_item_scan_offset ++;

	for (i=0; i<TD_WORK_ITEM_MAX_PER_NODE; i++) {
		struct td_work_item *wi;

		ndx = (i + w->w_work_item_scan_offset)
			% TD_WORK_ITEM_MAX_PER_NODE;

		wi = wn->wn_work_items + ndx;

		if (!wi->wi_device)
			/* this work_item is not used */
			continue;

		if (wi->wi_scout_worker) {
			/* this work-item is being scouted */
			if (wi->wi_scout_worker == w)
				/* scouted by us */
				goto already_scouted;
			/* skip, scouted by someone else */
			continue;
		}

		if ( !w->w_scouting_allowed || !w->w_unused_work_tokens )
			/* scouting is not allowed, or no tokens left */
			continue;

		if ( w->w_scout_devs.count >= td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY))
			/* we can't take on more work */
			continue;

		if (wi->wi_active_worker == w)
			/* not scouted, but already worked on by us */
			continue;

		if (! td_work_item_has_work_to_do(wi))
			continue;

		td_work_ftrace("TD %u found work on %s\n", w->w_cpu,
				td_device_name(wi->wi_device));

		/* this device has work to do, and is not currently scouted */
		if (__td_worker_scout_device(w, wi)) {
			/* tried to grab it, but failed */
			continue;
		}

already_scouted:
		if (wi->wi_active_worker)
			/* scouted by us, but already active */
			continue;

		mb();

		/* claim device scouted by us, but not active */
		__td_worker_activate_scouted_device(w, wi);

		/* only grab one at a time */
		break;
	}
}

static inline int td_worker_acquire_more_devices(struct td_worker *w)
{
	struct td_work_node *wn;
	struct td_devgroup *dg;
	int max;

	wn = w->w_work_node;
	dg = wn->wn_devgroup;

	/* if we have already activated our devices, there is nothing we could do */
	max = (int)td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY);
	if (w->w_activated_devices >= max)
		return 0;

	/* if there are no tokens left to give out, and we have no scouted
	 * devices that need to be upgraded to active, then there is nothing
	 * to do */
	if (!w->w_unused_work_tokens
			&& !w->w_scout_devs.count)
		return 0;

	td_worker_acquire_a_device(w);

	return 0;
}

static inline int td_worker_make_devices_available_for_scouting(struct td_worker *w)
{
	struct td_work_item *wi;

#ifdef CONIFG_TERADIMM_WORKER_FTRACE
	if (!w->already_said_make_devices_available_for_scouting) {
		td_work_ftrace("TD worker %u make_devices_available_for_scouting (s=%u, a=%u)\n",
			w->w_cpu,
			w->w_scout_devs.count,
			w->w_active_devs.count);
		w->already_said_make_devices_available_for_scouting = 1;
	}
#endif

	/* only once per outer loop pass */
	if (w->w_devices_shared)
		return -EAGAIN;

	/* walk the active list, anything we are scouting can be
	 * released for other workers to scout */
	td_worker_for_each_work_item(w, active, wi) {
		if (wi->wi_scout_worker == w) {
			__td_worker_unscout_device(w, wi);
		}
	}

	/* if all devices were shared, we don't have to come back
	 * here and we can release the work token */
	if (!w->w_scout_devs.count)
		w->w_devices_shared = 1;

	return 0;
}

static inline void td_worker_release_scouted_active_devices(struct td_worker *w)
{
	struct td_work_item *wi, *tmp_wi;

	//td_work_ftrace("TD worker %u release_scouted_active_devices\n", w->w_cpu);

	/* walk the active list, stop working on anything scouted by other workers */
	td_worker_for_each_work_item_safe(w, active, wi, tmp_wi) {
		struct td_worker * wi_scout = wi->wi_scout_worker;
		if (wi_scout && wi_scout != w) {
			wi_trace(wi, TR_SCOUT, "WORKER-SCOUTED", wi_scout->w_cpu);
			__td_worker_deactivate_device(w, wi);
		}
	}
}

static inline void td_worker_release_an_active_device(struct td_worker *w,
		struct td_work_item *wi)
{
	td_work_ftrace("TD worker %u release_an_active_device %s\n",
			w->w_cpu, td_device_name(wi->wi_device));

	if (wi->wi_scout_worker == w) {
		__td_worker_unscout_device(w, wi);
	}
	__td_worker_deactivate_device(w, wi);
}

static inline void td_worker_release_all_devices(struct td_worker *w)
{
	struct td_work_item *wi, *tmp_wi;

	td_work_ftrace("TD worker %u release_all_devices\n",
			w->w_cpu);

	td_worker_for_each_work_item_safe(w, active, wi, tmp_wi) {
	      wi_trace(wi, TR_SCOUT, "WORKER-RELEASE", w->w_loops);
	}
	/* first make any scouted devices available for scouting by others */
	td_worker_for_each_work_item_safe(w, scout, wi, tmp_wi) {
		__td_worker_unscout_device(w, wi);
	}

	/* next make any active devices available for being worked on by others */
	td_worker_for_each_work_item_safe(w, active, wi, tmp_wi) {
		__td_worker_deactivate_device(w, wi);
	}
}

static int td_worker_wake_condition(struct td_worker *w)
{
	td_work_ftrace("TD worker %u checks\n", w->w_cpu);

	td_worker_counter_inc(w, WAKE_CHECK);

	if ( w->w_going_down )
		return 1;

	if ( td_worker_acquire_work_tokens(w) )
		return 1;

	td_worker_counter_inc(w, NO_WAKE_TOKEN);
	return 0;
}

static void td_worker_wait_for_work_token(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;
#ifdef TERADIMM_CONFIG_AVOID_EVENTS
	uint64_t min_sleep = td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN);
#else
	uint64_t max_sleep = td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MAX);
	uint64_t no_wake_cnt, diff;
	uint64_t start_jiffies;
	int rc;
#endif

#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	/* if we have not yielded this process in a while, do it now */
	diff = jiffies - w->w_last_wait_jiffies;
	if (diff > HZ) {
		schedule();
		w->w_last_wait_jiffies = jiffies;
	}
#endif

	while (!w->w_unused_work_tokens && !w->w_going_down) {

		td_worker_counter_inc(w, LOOP2);

#ifdef TERADIMM_CONFIG_AVOID_EVENTS

		if (td_worker_wake_condition(w))
			break;

		td_work_ftrace("TD %s worker %u %s (w%d:a%d:%d+%d/%d:%d)\n", dg->dg_name, w->w_cpu,
				min_sleep ? "sleeps" : "yields",
				w->w_unused_work_tokens,
				atomic_read(&wn->wn_work_token_pool),
				atomic_read(&wn->wn_work_tokens_worker),
				atomic_read(&wn->wn_work_tokens_device),
				atomic_read(&wn->wn_work_tokens_total),
				atomic_read(&wn->wn_extra_work_token));

		if (min_sleep)
			schedule_timeout_uninterruptible(min_sleep);
		else
			schedule();

#else

		no_wake_cnt = td_worker_counter_get(w, NO_WAKE_TOKEN);
		start_jiffies = jiffies;

		td_work_ftrace("TD %s worker %u waits (w%d:a%d:%d+%d/%d:%d)\n", dg->dg_name, w->w_cpu,
				w->w_unused_work_tokens,
				atomic_read(&wn->wn_work_token_pool),
				atomic_read(&wn->wn_work_tokens_worker),
				atomic_read(&wn->wn_work_tokens_device),
				atomic_read(&wn->wn_work_tokens_total),
				atomic_read(&wn->wn_extra_work_token));

		if (max_sleep) {
			rc = wait_event_interruptible_timeout(
					wn->wn_event,
					td_worker_wake_condition(w),
					max_sleep);
		} else {
			rc = wait_event_interruptible_exclusive(
					wn->wn_event,
					td_worker_wake_condition(w));
		}

		/* if we had to wait, then remember when we waited last */
		diff = td_worker_counter_get(w, NO_WAKE_TOKEN) - no_wake_cnt;
		if (diff > 1 || start_jiffies != jiffies)
			w->w_last_wait_jiffies = jiffies;
#endif
	}
}

/* returns zero to continue, non-zero to exit loop */
static int td_worker_do_work(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;
	struct td_work_item *wi = NULL;
	unsigned total_activity = 0;
	unsigned total_future_work = 0;
	cycles_t now;
	int rc;

	now = td_get_cycles();

	td_worker_counter_inc(w, LOOP3);

	/* get more devices if needed */

	rc = td_worker_acquire_more_devices(w);
	if (rc < 0)
		return rc;

	/* we have work to do */

	td_busy_start(dg);

	td_worker_for_each_work_item(w, active, wi) {
		struct td_device *dev = wi->wi_device;
		struct td_engine *eng = &dev->td_engine;
		int dev_activity = 0, dev_future_work = 0;
		long diff;

		/* do work on this device */

		wi->wi_loops ++;

		dev_activity = 0;
		if (td_work_item_can_run(wi)) {
			dev_activity = td_device_do_work(dev);

			/* we don't expect negative return */
			WARN_ON(dev_activity < 0);

			dev_future_work = td_engine_queued_work(eng)
				+ td_all_active_tokens(eng)
				+ td_early_completed_reads(eng);

			/* fast path ... */
			if (likely ((dev_activity > 0) || dev_future_work)) {
				/* we have done some work */
				wi->wi_last_activity = jiffies;

				total_activity += dev_activity;
				total_future_work += dev_future_work;

				/* work on the next device */
				continue;
			}
		}

		/* only try to change states once per jiffie */
		if (wi->wi_last_state_check == jiffies)
			continue;
		wi->wi_last_state_check = jiffies;

		/* exception handling if we didn't do any work */

		WARN_ON(dev_activity);
		WARN_ON(dev_future_work);

		/* this device has no work, and if that
		 * happens for too long get rid of it */
		diff = jiffies - wi->wi_last_activity;

		if (td_work_item_is_state(wi, WI_ACTIVE)
				&& diff > td_dg_conf_worker_var_get(dg, DEV_WAIT_JIFFIES)) {
			/* this is an ACTIVE device that is going WAITING state */
			wi_trace(wi, TR_SCOUT, "WORKER-WAITING", w->w_loops);
			wi_trace(wi, TR_SCOUT, "    wi_has_work_token", wi->wi_has_work_token);
			wi_trace(wi, TR_SCOUT, "    state", td_work_item_state(wi));
			td_work_item_going_waiting(wn, wi);

		} else if (td_work_item_is_state(wi, WI_WAITING)
				&& diff > td_dg_conf_worker_var_get(dg, DEV_IDLE_JIFFIES)) {
			/* this is a WAITING device that is going to IDLE state */

			wi_trace(wi, TR_SCOUT, "WORKER-IDLE", w->w_loops);
			wi_trace(wi, TR_SCOUT, "    wi_has_work_token", wi->wi_has_work_token);
			wi_trace(wi, TR_SCOUT, "    state", td_work_item_state(wi));
			td_work_item_going_idle(wn, wi);

			/* there is no need to keep this device around */
			td_worker_release_an_active_device(w, wi);
			w->w_devices_idled = 1;

			/* break out of the device loop */
			break;
		}

		/* work on this device is done, signal to
		 * other threads, if needed */
		td_work_item_sync_event(wi);

		mb();
		/* now, work on the next device */
	}

	w->w_total_activity += total_activity;

	td_busy_end(dg);

	/* time management */

	if ( now > w->w_cycles_force_release_devices ) {
		/* been running too long? */
		td_worker_counter_inc(w, EXIT_FINAL);
		return -ETIMEDOUT;
	}

	if (!w->w_devices_shared && w->w_scout_devs.count) {

		bool do_share = false;

		/* the device did some work, and became idle */
		if (w->w_total_activity
				&& ! total_activity
				&& ! total_future_work) {

			if (now > w->w_cycles_idle_share_devices) {
				/* the worker worked on this device for too long,
				 * so migrate it to another device */
				do_share = true;
				td_worker_counter_inc(w, SHARE_IDLE);
			}

		}

		/* even if the device is busy we will eventually get rid of it */
		if (!do_share && now > w->w_cycles_busy_share_devices) {
			do_share = true;
			td_worker_counter_inc(w, SHARE_BUSY);
		}

		if (do_share) {
			/* now that we are sharing, don't
			 * allow acquisition of new devices */
			w->w_scouting_allowed = 0;

			td_work_ftrace("TD worker %u do_share (s=%u, a=%u)\n",
					w->w_cpu,
					w->w_scout_devs.count,
					w->w_active_devs.count);

			if (!td_worker_make_devices_available_for_scouting(w)) {
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
				/* we successfully shared and have future work, so
				 * wake up some other workers */
				if ( td_dg_conf_worker_var_get(dg, WAKE_SHARE))
					wake_up_interruptible_nr(&wn->wn_event, td_dg_conf_worker_var_get(dg, WAKE_SHARE));
#endif
			}
		}
	}

	if ( w->w_devices_shared ) {
		/* devices have been up for scouting, if they
		 * were grabbed, then it's time to release them */
		td_worker_release_scouted_active_devices(w);

		if (!w->w_scout_devs.count && !w->w_active_devs.count) {
			/* if we scouted devices before, and
			 * have released them all, and have no
			 * active devices left, it's time to go */
			td_worker_counter_inc(w, EXIT_SHARED);
			return -ENODEV;
		}
	}

	if (!w->w_scout_devs.count && !w->w_active_devs.count) {

		/* we have no devices, so maybe we are running
		 * for an extra token... be nice */
		td_work_node_extra_token_yeild(wn);

		if (w->w_devices_idled) {
			/* We have no devices...
			 * If we've already idled them, then we are also idle. */
			td_worker_counter_inc(w, EXIT_IDLE);
			return -ENODEV;

		} else if (now > w->w_cycles_without_devices) {
			/* Could not scout devices after configured
			 * timeout period, and since we have no active
			 * devices it is time to go idle */
			td_worker_counter_inc(w, EXIT_NO_DEVICES);
			return -ENODEV;
		}
	}

	/* we will keep going */
	return 0;
}


/* the worker thread */
static int td_worker_thread(void *thread_data)
{
	int rc;
	struct td_worker *w = thread_data;
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;

	BUG_ON(!dg);

	set_user_nice(current, dg->dg_nice);

	td_busy_reset(dg);

	td_worker_starting_active_loop(w);

	while(!kthread_should_stop()) {

		td_worker_counter_inc(w, LOOP1);

		w->w_sleep_start = jiffies;

		td_worker_wait_for_work_token(w);

		if (w->w_going_down)
			break;

		/* woke up, will be doing more work */

		td_worker_starting_active_loop(w);

		td_work_ftrace("TD %s worker %u works (w%d:a%d:%d+%d/%d:%d)\n", dg->dg_name, w->w_cpu,
				w->w_unused_work_tokens,
				atomic_read(&wn->wn_work_token_pool),
				atomic_read(&wn->wn_work_tokens_worker),
				atomic_read(&wn->wn_work_tokens_device),
				atomic_read(&wn->wn_work_tokens_total),
				atomic_read(&wn->wn_extra_work_token));

		for(w->w_loops=0; w->w_loops < td_dg_conf_worker_var_get(dg, MAX_LOOPS); w->w_loops++) {
			rc = td_worker_do_work(w);
			if (rc)
				break;
		}

		td_work_ftrace("TD %u end L%lu A%lu (w%d:a%d:%d+%d/%d:%d)\n", w->w_cpu, w->w_loops,
				w->w_total_activity,
				w->w_unused_work_tokens,
				atomic_read(&wn->wn_work_token_pool),
				atomic_read(&wn->wn_work_tokens_worker),
				atomic_read(&wn->wn_work_tokens_device),
				atomic_read(&wn->wn_work_tokens_total),
				atomic_read(&wn->wn_extra_work_token));

		/* loop finished, cleanup */

		td_worker_release_all_devices(w);
		td_worker_release_work_tokens(w);

		td_work_node_extra_token_cleanup(wn);

		/* wake up a replacement */

#ifndef TERADIMM_CONFIG_AVOID_EVENTS
		if (w->w_total_activity && td_dg_conf_worker_var_get(dg, WAKE_SLEEP))
			wake_up_interruptible_nr(&wn->wn_event, td_dg_conf_worker_var_get(dg, WAKE_SLEEP));
#endif

		/* go to sleep */
		if (td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN))
			schedule_timeout_uninterruptible(td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN));
	}

	/* make sure everything is released */
	td_worker_release_work_tokens(w);
	td_worker_release_all_devices(w);
	mb();

	return 0;
}

/* ------------------------------------------------------------------------ */

static int td_work_item_sync_condition(struct td_work_node *wn,
		struct td_work_item *wi, unsigned long start)
{
	struct td_devgroup *dg;

	/* 
	 * The sync condition is different if we are runnable or not:
	 * 1) If runnable, we're waiting for something to start
	 *    - once we see wi_loops incrementing, it is running
	 * 2) If not runnable, we are waiting for it to be idle
	 *    - We look for scout and active workers to be NULL
	 * Otherwise, we set the sync_req, and poke the groups
	 * event to make sure they are running and service this
	 * work item.
	 */
	if (td_work_item_can_run(wi) )
	{
		if (wi->wi_loops != start)
			return 1;
	} else {
		if (!wi->wi_scout_worker && !wi->wi_active_worker)
			return 1;
	}

	/* set the request */
	atomic_set(&wi->wi_sync_req, 1);

	/* make sure thread is not sleeping */
	dg = wn->wn_devgroup;
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	wake_up_interruptible_all(&wn->wn_event);
#endif

	return 0;
}

/** wait for a work item to be synchronized, waiting for one of:
 * - work item sync_condition is met:
 * - SYNC_JIFFIES jiffies has elapsed
 */
int td_work_node_synchronize_item(struct td_work_node *wn, struct td_work_item *wi)
{
	struct td_devgroup *dg;
	int rc;
	unsigned long start;

	if (!wi)
		return -EFAULT;

	start = wi->wi_loops;

	dg = wn->wn_devgroup;

	/* wait for it to synchronize with us */
#ifdef TERADIMM_CONFIG_AVOID_EVENTS
	rc = td_dg_conf_worker_var_get(dg, SYNC_JIFFIES);
	while (!td_work_item_sync_condition(wn, wi, start) && rc > 0) {
		int to_sleep = min(10, rc);
		schedule_timeout(to_sleep);
		rc -= to_sleep;
	}
#else
	rc = wait_event_interruptible_timeout(
			wi->wi_sync,
			td_work_item_sync_condition(wn, wi, start),
			td_dg_conf_worker_var_get(dg, SYNC_JIFFIES));
#endif
	switch (rc) {
	case 0: /* 1s timeout */
		return -ETIMEDOUT;

	case -ERESTARTSYS: /* interrupted by a signal */
		return rc;

	default: /* condition occurred */
		return 0;
	}
}


static void td_work_item_sync_event(struct td_work_item *wi)
{
	/* wake up any threads waiting for completion of this run */
	if (atomic_read(&wi->wi_sync_req)) {
		atomic_set(&wi->wi_sync_req, 0);
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
		wake_up_interruptible(&wi->wi_sync);
#endif
	}
}

/* ------------------------------------------------------------------------ */

#ifdef CONFIG_TERADIMM_CORE_FIRST
/* return the numerically smaller cpu that is a core sibling to this one,
 * or -1 if there is no such cpu */
static inline int find_prev_core_sibling(unsigned cpu)
{
	int sibling;
	int want_package = topology_physical_package_id(cpu);
	int want_core_id = topology_core_id(cpu);

	for (sibling=cpu-1; sibling>=0; sibling--) {
		/* cpu must be part of the same node */
		if (want_package != topology_physical_package_id(sibling))
			continue;
		/* cpu must be part of the same core */
		if (want_core_id != topology_core_id(sibling))
			continue;

		return sibling;
	}

	return -1;
}
#endif

int td_work_node_init(struct td_work_node *wn, struct td_devgroup *dg,
		unsigned node)
{
	unsigned size, cpus_per_socket;
	unsigned worker;
	int cpu;
#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	unsigned socket_cpu_start, socket_cpu_end;
#endif
#ifdef CONFIG_TERADIMM_CORE_FIRST
	int sibling;
#endif

	if (wn->wn_workers)
		return 0;

	wn->wn_node = node;

	init_waitqueue_head(&wn->wn_event);

	atomic_set(&wn->wn_work_token_pool, 0);

#ifdef TD_WORK_TOKEN_ACCOUNTING
	atomic_set(&wn->wn_work_tokens_total, 0);
	atomic_set(&wn->wn_work_tokens_worker, 0);
	atomic_set(&wn->wn_work_tokens_device, 0);
#endif

	atomic_set(&wn->wn_work_item_available_count, 0);
	atomic_set(&wn->wn_work_item_active_count, 0);

	memset(wn->wn_work_items, 0, sizeof(wn->wn_work_items));

#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	if (dg->dg_socket >= td_socketmap_size) {
	       pr_err("No socketmap for socket %u\n", dg->dg_socket);
	       return -EINVAL;
	}

	if (dg->dg_socket > 0)
	      socket_cpu_start = td_socketmap[dg->dg_socket-1] + 1;
	else
	      socket_cpu_start = 0;
	socket_cpu_end = td_socketmap[dg->dg_socket];
	cpus_per_socket = socket_cpu_end - socket_cpu_start + 1;;
#else
	cpus_per_socket = 0;
	for (cpu=0; cpu<MAX_CPU_NUMBER; cpu++) {
		if (dg->dg_socket == topology_physical_package_id(cpu))
			cpus_per_socket ++;
	}
#endif

	if (td_workers_per_node == 0)
		td_workers_per_node = TD_WORKER_MAX_PER_NODE;

	if (cpus_per_socket >= td_workers_per_node)
	      cpus_per_socket = td_workers_per_node;

	size = cpus_per_socket * sizeof(struct td_worker);
	wn->wn_workers = kzalloc_node(size, GFP_KERNEL, dg->dg_socket);
	if (!wn->wn_workers)
		return -ENOMEM;
	
#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	/* we are given a static cpu map */
	worker = 0;
	for (cpu=0; cpu<MAX_CPU_NUMBER; cpu++) {
	        if (cpu < socket_cpu_start || cpu > socket_cpu_end)
			continue;
		if(worker >= cpus_per_socket)
			break;

		td_worker_init(wn->wn_workers + worker,
				cpu, wn);
		worker++;
	}

#else
	worker = 0;
	for (cpu=MAX_CPU_NUMBER-1; cpu>=0 && worker < cpus_per_socket; cpu--) {
		if (dg->dg_socket != topology_physical_package_id(cpu))
			continue;

		td_worker_init(wn->wn_workers + worker,
				cpu, wn);
		worker++;

#ifdef CONFIG_TERADIMM_CORE_FIRST
		if(worker >= cpus_per_socket)
			break;

		sibling = find_prev_core_sibling(cpu);
		if (sibling == -1)
			continue;

		td_worker_init(wn->wn_workers + worker,
				sibling, wn);
		worker++;
#endif

	}
#endif
	WARN_ON(worker == 0);

	wn->wn_devgroup = dg;

	wn->wn_work_item_count = 0;
	wn->wn_worker_count = worker;

	return 0;
}

void td_work_node_exit(struct td_work_node *wn)
{
	if (wn->wn_workers) {
		kfree(wn->wn_workers);
		wn->wn_workers = NULL;
		wn->wn_worker_count = 0;
	}
}

int td_work_node_start(struct td_work_node *wn)
{
	unsigned worker;
	int rc;

	WARN_ON(!wn->wn_workers);
	if (!wn->wn_workers)
		return -EFAULT;

	for (worker=0; worker<wn->wn_worker_count; worker++) {
		rc = td_worker_start(wn->wn_workers + worker);
		if (rc)
			goto error_worker_start;
	}

	return 0;

error_worker_start:
	for (worker=0; worker<wn->wn_worker_count; worker++)
		td_worker_stop(wn->wn_workers + worker);
	return rc;
}

void td_work_node_stop(struct td_work_node *wn)
{
	int worker;

	if (!wn->wn_workers)
		return;

	for (worker=0; worker<wn->wn_worker_count; worker++) {
		td_worker_stop(wn->wn_workers + worker);
	}
}

int td_work_node_attach_device(struct td_work_node *wn, struct td_device *dev)
{
	int i;
	struct td_work_item *wi = NULL;

	WARN_ON(!wn->wn_workers);
	if (!wn->wn_workers)
		return -EINVAL;

	if (wn->wn_work_item_count >= TD_WORK_ITEM_MAX_PER_NODE)
		goto error_no_room;

	for (i = 0; i<TD_WORK_ITEM_MAX_PER_NODE; i++) {
		if (wn->wn_work_items[i].wi_device)
			continue;

		wi = wn->wn_work_items + i;
		break;
	}

	if (!wi)
		goto error_no_room;

	td_work_item_init(wi, dev);

	/* make the device work item available */

	dev->td_work_item = wi;
	wn->wn_work_item_count ++;
	mb();

	atomic_inc(&wn->wn_work_item_available_count);

	return 0;

error_no_room:
	td_eng_err(td_device_engine(dev),
			"too many devices (%u) on %s\n",
			wn->wn_work_item_count,
			wn->wn_devgroup->dg_name);
	return -EBUSY;
}

void td_work_node_detach_device(struct td_work_node *wn, struct td_device *dev)
{
	struct td_work_item *wi = NULL;

	wi = dev->td_work_item;
	WARN_ON(!wi);
	if (!wi)
		return;

	/* Last ditch effort to make sure any scouts/etc are done */
	mdelay(500);

	WARN_ON(atomic_read(&wi->wi_scouted_count));
	WARN_ON(wi->wi_scout_worker);
	WARN_ON(wi->wi_active_worker);

	dev->td_work_item = NULL;
	wi->wi_device = NULL;
	wn->wn_work_item_count --;
	mb();

	atomic_dec(&wn->wn_work_item_available_count);
}


