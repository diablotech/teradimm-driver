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

#include "td_worker.h"
#include "td_devgroup.h"
#include "td_device.h"
#include "td_eng_hal.h"
#include "td_compat.h"

#define wi_trace(_wi_,_t_,_l_,_x_) \
	td_eng_trace(td_device_engine((_wi_)->wi_device), _t_, _l_, _x_)

static uint td_workers_per_node = TD_WORKER_MAX_PER_NODE;
module_param_named(max_workers_per_node, td_workers_per_node, uint, 0644);
MODULE_PARM_DESC(max_workers_per_node, "How many workers run in each node");

static uint td_idle_msec = 2;
module_param_named(idle_msec, td_idle_msec, uint, 0644);
MODULE_PARM_DESC(idle_msec, "How long the thread remains idle in milliseconds");

static uint td_yield_msec = 100;
module_param_named(yield_msec, td_yield_msec, uint, 0644);
MODULE_PARM_DESC(yield_msec, "How often to yield the thread in milliseconds");


#if 1
/* debug crap */
#define MAX_DEBUG_WORKERS 32
static int td_max_workers = MAX_DEBUG_WORKERS;

static ulong td_worker_addr[MAX_DEBUG_WORKERS] = { 0, };
module_param_array_named(worker_addr, td_worker_addr, ulong, &td_max_workers, 0444);

#define td_worker_debug_counter(_w,_name) \
(td_worker_##_name[(_w)->w_cpu % MAX_DEBUG_WORKERS])

#define td_worker_counter_inc(_w,_which) \
	do { (_w)->counters[TD_DEVGROUP_WORKER_COUNT_##_which] += 1; } while (0)

#define MAX_DEBUG_DEVGROUPS 8
static int td_max_devgroup_cpus = MAX_DEBUG_DEVGROUPS;

static ulong td_devgroup_addr[MAX_DEBUG_DEVGROUPS] = { 0, };
module_param_array_named(devgroup_cpu_addr, td_devgroup_addr, ulong, &td_max_devgroup_cpus, 0444);

#define td_devgroup_debug_counter(_dg,_name) \
(td_worker_##_name[(_dg)->dg_socket % MAX_DEBUG_DEVGROUPS])

#endif

static void td_work_item_sync_event(struct td_work_item *wi);
static int td_worker_thread(void *thread_data);
static void td_work_node_check_and_recalc_work_tokens(struct td_work_node *wn);

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

	w->w_loops = 0;
	w->w_devices_shared = 0;
	w->w_has_work_token = 0;
	w->w_going_down = 0;

	w->w_task = kthread_create(td_worker_thread, w,
			TD_DEVGROUP_THREAD_NAME_PREFIX "%s/%u",
			dg->dg_name, w->w_cpu);

	if (unlikely(IS_ERR_OR_NULL(w->w_task)))
		goto error_create;

	kthread_bind(w->w_task, w->w_cpu);
	wake_up_process(w->w_task);

	return 0;

error_create:
	pr_err("Failed "TD_DEVGROUP_THREAD_NAME_PREFIX "%s/%u "
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
	wake_up_interruptible_all(&dg->dg_event);
#endif

	rc = kthread_stop(w->w_task);
	w->w_task = NULL;
	pr_warn("Stopped thread "TD_DEVGROUP_THREAD_NAME_PREFIX "%s/%u\n",
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

	wi_trace(wi, TR_SCOUT, "WORKER-ACTIVATE", w->w_loops);
}

static inline void __td_worker_unscout_device(struct td_worker *w,
		struct td_work_item *wi)
{
	struct td_work_node *wn = w->w_work_node;

	WARN_ON(wi->wi_scout_worker != w);

	if(wi->wi_active_worker == w)
		wi_trace(wi, TR_SCOUT, "WORKER-UNSCOUT", w->w_loops);

	list_del(&wi->wi_scout_link);

	wmb();

	wi->wi_scout_worker = NULL;
	w->w_scout_devs.count --;
	atomic_dec(&wi->wi_scouted_count);

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

	atomic_dec(&wn->wn_work_item_active_count);
	td_work_item_sync_event(wi);
	td_device_put(wi->wi_device);
}

static inline int td_worker_acquire_work_token(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	int after;

	if ( w->w_has_work_token )
		return 1;

	after = atomic_sub_return(1, &wn->wn_worker_tokens);
	if (likely (after >= 0)) {
		/* got a token */
		w->w_has_work_token = 1;
		return 1;
	}

	/* didn't get one */
	atomic_inc(&wn->wn_worker_tokens);
	return 0;
}

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
	struct td_work_node *wn = w->w_work_node;

	if ( !w->w_has_work_token )
		return;

	atomic_inc(&wn->wn_worker_tokens_idle);
	w->w_has_work_token = 0;
}

static inline bool td_work_item_has_work_to_do(struct td_work_item *wi)
{
	struct td_device *dev = NULL;
	struct td_engine *eng = NULL;
	struct td_devgroup *dg = NULL;
	long diff;

	if (!td_work_item_can_run(wi))
		return false;

	dev = wi->wi_device;
	eng = &dev->td_engine;
	dg = dev->td_devgroup;

	if (td_engine_needs_cpu_time(eng))
		return true;

	/* does not need cpu time now, but we will hold it
	* if it had activity recently */
	diff = jiffies - wi->wi_last_activity;
	if (diff <= td_dg_conf_worker_var_get(dg, DEV_WAIT_JIFFIES))
		/* we accessed the device recently, keep it hot */
		return true;

	/* it's been too long */
	return false;
}


static inline void td_worker_starting_active_loop(struct td_worker *w)
{
	struct td_work_node *wn = w->w_work_node;
	struct td_devgroup *dg = wn->wn_devgroup;

	cycles_t now = td_get_cycles();

	w->w_cycles_wake = now;
	w->w_cycles_without_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, WITHOUT_DEVS_NSEC));
	w->w_cycles_share_devices   = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, SHARE_NSEC));
	w->w_cycles_release_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, RELEASE_NSEC));
	w->w_cycles_force_release_devices = now + td_nsec_to_cycles(td_dg_conf_worker_var_get(dg, FORCE_RELEASE_NSEC));

	WARN_ON(w->w_cycles_share_devices < now);
	WARN_ON(w->w_cycles_release_devices < now);
	WARN_ON(w->w_cycles_force_release_devices < now);

	w->w_loops = 0;
	w->w_devices_shared = 0;
	w->w_total_activity = 0;
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

		if ( w->w_scout_devs.count >= td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY))
			/* we can't take on more work */
			continue;

		if (wi->wi_active_worker == w)
			/* not scouted, but already worked on by us */
			continue;

		if (! td_work_item_has_work_to_do(wi))
			continue;

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
	wn = w->w_work_node;
	dg = wn->wn_devgroup;

	/* we have enough devices */
	if ( w->w_active_devs.count >= td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY) )
		return 0;

	/* we are ramping down now, don't get any more */
	if (w->w_devices_shared)
		return 0;

	td_worker_acquire_a_device(w);

	return 0;
}

static inline int td_worker_make_devices_available_for_scouting(struct td_worker *w)
{
	struct td_work_item *wi;

	/* only once per outer loop pass */
	if (w->w_devices_shared)
		return -EAGAIN;
	w->w_devices_shared = 1;

	/* walk the active list, anything we are scouting can be
	 * released for other workers to scout */
	td_worker_for_each_work_item(w, active, wi) {
		if (wi->wi_scout_worker == w) {
			__td_worker_unscout_device(w, wi);
		}
	}

	td_worker_release_work_token(w);

	return 0;
}

static inline void td_worker_release_scouted_active_devices(struct td_worker *w)
{
	struct td_work_item *wi, *tmp_wi;

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
	if (wi->wi_scout_worker == w) {
		__td_worker_unscout_device(w, wi);
	}
	__td_worker_deactivate_device(w, wi);
}

static inline void td_worker_release_all_devices(struct td_worker *w)
{
	struct td_work_item *wi, *tmp_wi;

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
#if 0
	struct td_work_node *wn = w->w_work_node;
#endif
#if 0
	unsigned long diff;
#endif

	td_worker_counter_inc(w, WAKE_CHECK);

#if 0
	/* don't wake up right away */
	if (td_worker_sleep_jiffies_min) {
		diff = jiffies - w->w_sleep_start;
		if (diff >= 0 && diff < td_worker_sleep_jiffies_min) {
			td_worker_counter_inc(w, NO_WAKE_TOKEN);
			td_worker_release_work_token(w);
			return 0;
		}
	}
#endif

	if ( w->w_going_down )
		return 1;

	if ( td_worker_acquire_work_token(w) )
		return 1;

	td_worker_counter_inc(w, NO_WAKE_TOKEN);
	return 0;
#if  0
	/* TODO: does this need to be more complex? */
	return atomic_read(&wn->wn_work_item_available_count) > 0;
#endif
}



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

	td_worker_debug_counter(w, addr) = (ulong)w;
	td_devgroup_debug_counter(dg, addr) = (ulong)dg;

	while(!kthread_should_stop()) {
		unsigned total_future_work = 0;

		td_worker_counter_inc(w, LOOP1);

		w->w_sleep_start = jiffies;

#ifdef TERADIMM_CONFIG_AVOID_EVENTS
		while (! td_worker_wake_condition(w))
			schedule_timeout_uninterruptible(td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN));
#else
		
		while (!w->w_has_work_token && !w->w_going_down) {

			td_worker_counter_inc(w, LOOP3);

			if (td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN))
				schedule_timeout_uninterruptible(td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MIN));

			rc = wait_event_interruptible_timeout(
				dg->dg_event,
				td_worker_wake_condition(w),
				td_dg_conf_worker_var_get(dg, SLEEP_JIFFIES_MAX));

		}
#endif

		if (w->w_going_down)
			break;

		/* woke up, will be doing more work */

		td_worker_starting_active_loop(w);


		for(w->w_loops=0; w->w_loops < td_dg_conf_worker_var_get(dg, MAX_LOOPS); w->w_loops++) {

			struct td_work_item *wi = NULL;
			unsigned total_activity = 0;
			cycles_t now;

			td_worker_counter_inc(w, LOOP2);

			/* get more devices if needed */

			rc = td_worker_acquire_more_devices(w);
			if (rc < 0)
				break;

			/* we have work to do */

			td_busy_start(dg);

			total_future_work = 0;
			td_worker_for_each_work_item(w, active, wi) {
				struct td_device *dev = wi->wi_device;
				struct td_engine *eng = &dev->td_engine;
				int dev_activity = 0, dev_future_work = 0;

				wi->wi_loops ++;

				dev_activity = -EIO;
				if (td_work_item_can_run(wi)) {
					dev_activity = td_device_do_work(dev);

					dev_future_work = td_engine_queued_work(eng)
						+ td_all_active_tokens(eng)
						+ td_early_completed_reads(eng);

				}

				/* work on this device is done, signal to
				 * other threads, if needed */
				td_work_item_sync_event(wi);

				if (dev_activity < 0) {
					/* this device can no longer participate */
					wi_trace(wi, TR_SCOUT, "WORKER-ERROR", (uint64_t)rc);
					td_worker_release_an_active_device(w, wi);
					break;
				}

				if (!dev_activity && !dev_future_work) {
					/* this device has no work, and if that
					 * happens for too long get rid of it */
					long diff = jiffies - wi->wi_last_activity;
					if (diff > td_dg_conf_worker_var_get(dg, DEV_IDLE_JIFFIES)) {
						wi_trace(wi, TR_SCOUT, "WORKER-IDLE", w->w_loops);
						td_worker_release_an_active_device(w, wi);
						break;
					}
				}

				total_activity += dev_activity;
				total_future_work += dev_future_work;

				if ((dev_activity > 0) || total_future_work)
					wi->wi_last_activity = jiffies;
				mb();
			}

			w->w_total_activity += total_activity;

			td_busy_end(dg);

			/* time management */

			now = td_get_cycles();

			if ( now > w->w_cycles_force_release_devices ) {
				/* been running too long? */
				td_worker_counter_inc(w, EXIT_FINAL);
				break;
			}

			if (!w->w_devices_shared) {

				bool do_share = false;

				if (now > w->w_cycles_share_devices) {
					/* we ran for a bit, it's time to let other
					 * threads scout our devices */
					do_share = true;
				}

				if (do_share && total_future_work ) {
					if (!td_worker_make_devices_available_for_scouting(w)) {
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
						/* we successfully shared and have future work, so
						 * wake up some other workers */
						if ( td_dg_conf_worker_var_get(dg, WAKE_SHARE))
							wake_up_interruptible_nr(&dg->dg_event, td_dg_conf_worker_var_get(dg, WAKE_SHARE));
#endif
					}
				}

			}

			if ( w->w_devices_shared && now > w->w_cycles_release_devices ) {
				/* devices have been up for scouting, if they
				 * were grabbed, then it's time to release them */
				td_worker_release_scouted_active_devices(w);
			}

			if (!w->w_scout_devs.count && now > w->w_cycles_without_devices) {
				/* could not scout devices after configured
				 * timeout period, and if we have no active
				 * devices it is time to go idle */
				if (!w->w_active_devs.count) {
					if (w->w_devices_shared)
						td_worker_counter_inc(w, EXIT_CORRECTLY);
					else
						td_worker_counter_inc(w, EXIT_IDLE);
					break;
				}
			}

		}

		/* ran out of quanta */

		if (!w->w_scout_devs.count && !w->w_active_devs.count)
			td_worker_consume_work_token(w);
		else {
			td_worker_release_all_devices(w);
			td_worker_release_work_token(w);
		}

		/* wake up a replacement */

#ifndef TERADIMM_CONFIG_AVOID_EVENTS
		if (w->w_total_activity && td_dg_conf_worker_var_get(dg, WAKE_SLEEP))
			wake_up_interruptible_nr(&dg->dg_event, td_dg_conf_worker_var_get(dg, WAKE_SLEEP));
#endif

		/* check on token recalculations */

		td_work_node_check_and_recalc_work_tokens(w->w_work_node);

		/* go to sleep */
	}

	/* make sure everything is released */
	td_worker_release_work_token(w);
	td_worker_release_all_devices(w);
	mb();

	return 0;
}

/* ------------------------------------------------------------------------ */

static int td_work_item_sync_condition(struct td_work_node *wn,
		struct td_work_item *wi, unsigned long start)
{
	struct td_devgroup *dg;

	if (wi->wi_loops != start)
		return 1;

	if (!td_work_item_can_run(wi)
			&& !wi->wi_scout_worker
			&& !wi->wi_active_worker)
		return 1;

	/* set the request */
	atomic_set(&wi->wi_sync_req, 1);

	/* make sure thread is not sleeping */
	dg = wn->wn_devgroup;
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	wake_up_interruptible_all(&dg->dg_event);
#endif

	return 0;
}

/** wait for upto a second for one of the following conditions:
 * - thread completed one pass through the loop
 * - thread is no longer running
 * - 1 second has elapsed
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
	while (!td_work_item_sync_condition(wn, wi, start) && rc < 1) {
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

int td_work_node_init(struct td_work_node *wn, struct td_devgroup *dg)
{
	unsigned size, cpus_per_socket;
	unsigned cpu, worker;
#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	unsigned socket_cpu_start, socket_cpu_end;
#endif

	if (wn->wn_workers)
		return 0;

	atomic_set(&wn->wn_worker_tokens, 0);
	atomic_set(&wn->wn_worker_tokens_idle, 0);
	wn->wn_total_worker_tokens = 0;

	atomic_set(&wn->wn_token_recompute_needed, 0);

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

	if (cpus_per_socket >= td_workers_per_node)
	      cpus_per_socket = td_workers_per_node;

	size = cpus_per_socket * sizeof(struct td_worker);
	wn->wn_workers = kzalloc_node(size, GFP_KERNEL, dg->dg_socket);
	if (!wn->wn_workers)
		return -ENOMEM;
	
	worker = 0;
	for (cpu=0; cpu<MAX_CPU_NUMBER; cpu++) {
#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	        if (cpu < socket_cpu_start || cpu > socket_cpu_end)
			continue;
#else
		if (dg->dg_socket != topology_physical_package_id(cpu))
			continue;
#endif

		WARN_ON(worker >= cpus_per_socket);
		if(worker >= cpus_per_socket)
			break;

#if 0
		/* Do not get HT siblings */
		if (cpu % 2 == 0)
			continue;
#endif
		td_worker_init(wn->wn_workers + worker,
				cpu, wn);
		worker++;
	}
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

static void td_work_node_check_and_recalc_work_tokens(struct td_work_node *wn)
{
	struct td_devgroup *dg;
	long needed, delta;
	int flag;

	if (!atomic_read(&wn->wn_token_recompute_needed))
		return;
	
	/* the one that swaps out the 1 wins */
	flag = atomic_xchg(&wn->wn_token_recompute_needed, 0);
	if (flag != 1) {
		/* someone else got the 1, so we have nothing to do */
		return;
	}

	dg = wn->wn_devgroup;

	/* we know how many work items there are, and how many work items
	 * should be active on a worker */
	needed =
		/* round up */
		( wn->wn_work_item_count + td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY) - 1 )
		/ td_dg_conf_worker_var_get(dg, MAX_OCCUPANCY);

	/* we may also want some extra for scouting to be done efficiently */
	needed += td_dg_conf_worker_var_get(dg, EXTRA_TOKENS);

	/* cap range at sensible values [1,100] */
	needed = max_t(long, needed, 1);
	needed = min_t(long, needed, 100);

	/* how far are we off from where we currently are */
	delta = needed - wn->wn_total_worker_tokens;

	if (!delta)
		return;

	pr_info("%s: %s tokens by %lu to %lu\n",
			wn->wn_devgroup->dg_name,
			delta>0 ? "increasing" : "decreasing",
			delta>0 ? delta : -delta,
			needed);

	wn->wn_total_worker_tokens += delta;
	atomic_add(delta, &wn->wn_worker_tokens_idle);
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

	/* recalculate the tokens */

	atomic_set(&wn->wn_token_recompute_needed, 1);
	td_work_node_check_and_recalc_work_tokens(wn);

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

	/* recalculate the tokens */

	atomic_set(&wn->wn_token_recompute_needed, 1);
	td_work_node_check_and_recalc_work_tokens(wn);
}


