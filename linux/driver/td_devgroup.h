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

#ifndef _TD_DEVGROUP_H_
#define _TD_DEVGROUP_H_

#include "td_kdefn.h"

#include <linux/notifier.h>

#include "td_defs.h"
#include "td_cpu.h"
#include "td_bio.h"
#include "td_worker.h"

struct td_device;

#define td_dg_counter_var_endio_get(dg, which)                                 \
	((dg)->counters.endio[TD_DEVGROUP_ENDIO_COUNT_##which])
#define td_dg_counter_var_endio_add(dg, which, amount)                         \
	do { (dg)->counters.endio[TD_DEVGROUP_ENDIO_COUNT_##which] += amount; } while (0)
#define td_dg_counter_var_endio_sub(dg, which, amount)                         \
	do { (dg)->counters.endio[TD_DEVGROUP_ENDIO_COUNT_##which] -= amount; } while (0)
#define td_dg_counter_var_endio_inc(dg, which)                                 \
	do { (dg)->counters.endio[TD_DEVGROUP_ENDIO_COUNT_##which] += 1; } while (0)
#define td_dg_counter_var_endio_dec(dg, which)                                 \
	do { (dg)->counters.endio[TD_DEVGROUP_ENDIO_COUNT_##which] -= 1; } while (0)

struct __packed td_dg_counters {
	uint64_t endio[TD_DEVGROUP_ENDIO_COUNT_MAX];
};

struct td_dg_conf {
	uint64_t general[TD_DEVGROUP_CONF_GENERAL_MAX];
	uint64_t worker[TD_DEVGROUP_CONF_WORKER_MAX];
};

struct td_devgroup {
	char dg_name[TD_DEVGROUP_NAME_MAX];

	/**
	 * thread is ref counted
	 *
	 * use td_devgroup_get_by_*() and td_devgroup_put() to modify reference count
	 *
	 * First caller to _get() creates a thread, ref count is 1.
	 * Last caller to _put() destroys it, because ref count is 0.
	 */
	atomic_t            dg_refcnt;

	cycles_t            dg_next_pause_jiffies;   /**< when this thread will yield the CPU next */

	struct list_head    dg_devs_list;   /**< devices that the thread owns */
	spinlock_t          dg_devs_lock;   /**< protects dg_dev_lock */

	unsigned            dg_devs_count;  /**< number of devices handled */

	struct list_head    dg_pool_link;   /**< link in list of all threads */

	struct mutex        dg_mutex;       /**< prevents concurrent state changes / deletes */

	struct td_work_node dg_work_node;

#ifdef CONFIG_TERADIMM_TRACK_CPU_USAGE
	struct td_cpu_stats dg_cpu_stats;   /**< statistics on where time is spent */
#endif

	int                 dg_socket;      /**< thread configured on a single socket */
	int                 dg_nice;        /**< thread configured to this UNIX nice value */
	
#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	struct task_struct  *dg_endio_task;
#ifndef TERADIMM_CONFIG_AVOID_EVENTS
	wait_queue_head_t   dg_endio_event;       /**< events that the worker thread waits on */
#endif
	unsigned long       dg_endio_last_activity;

	spinlock_t          dg_endio_lock;
	struct bio_list     dg_endio_success;
	uint64_t            dg_endio_count;
	unsigned long       dg_endio_ts;
#endif

	struct td_dg_counters counters;
	struct td_dg_conf   conf;
};

struct td_dg_conf_var_desc {
        int (*check)(struct td_devgroup *dg, uint32_t conf_num, uint64_t val);
        uint64_t min, max;
};

extern const struct td_dg_conf_var_desc td_dg_conf_general_var_desc[TD_DEVGROUP_CONF_GENERAL_MAX];

extern int __init td_devgroup_init(void);
extern void td_devgroup_exit(void);

extern int td_devgroup_dump_names(char *buf, size_t len, uint32_t *count);
extern int td_devgroup_create(const char *name, int node, int nice);
extern int td_devgroup_delete(const char *name);
extern int td_devgroup_start(const char *name);
extern int td_devgroup_stop(const char *name);

extern int td_devgroup_get_counter(struct td_devgroup *dg,
                struct td_worker *w,
                enum td_devgroup_counter_type type,
                uint32_t cntr_num, uint64_t *val);

extern int td_devgroup_get_conf(struct td_devgroup *dg,
                enum td_devgroup_conf_type type,
                uint32_t cntr_num, uint64_t *val);
extern int td_devgroup_set_conf(struct td_devgroup *dg,
                enum td_devgroup_conf_type type,
                uint32_t cntr_num, uint64_t val);
#define td_dg_conf_general_var_get(dg, which)                                 \
	((dg)->conf.general[TD_DEVGROUP_CONF_GENERAL_##which])
#define td_dg_conf_general_var_set(dg, which, val)                           \
	do { (dg)->conf.general[TD_DEVGROUP_CONF_GENERAL_##which] = val;              \
	} while (0)
#define td_dg_conf_worker_var_get(dg, which)                                 \
	((dg)->conf.worker[TD_DEVGROUP_CONF_WORKER_##which])
#define td_dg_conf_worker_var_set(dg, which, val)                           \
	do { (dg)->conf.worker[TD_DEVGROUP_CONF_WORKER_##which] = val;              \
	} while (0)

/* access to objects */
extern struct td_devgroup *td_devgroup_get_by_name(const char *name);
extern struct td_devgroup *td_devgroup_get_by_node(int node);
extern void td_devgroup_put(struct td_devgroup *);
static inline void td_devgroup_hold(struct td_devgroup *dg)
{
	atomic_inc(&dg->dg_refcnt);
}

extern int td_devgroup_sync_device(struct td_devgroup *dg, struct td_device *dev);

extern int td_devgroup_add_device(struct td_devgroup*, struct td_device*);
extern int td_devgroup_remove_device(struct td_devgroup*, struct td_device*);

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
extern void td_devgroup_queue_endio_success(struct td_devgroup *eng, td_bio_ref bio);
#endif


static inline bool td_devgroup_endio_needs_poke(struct td_devgroup *dg, unsigned idle_jiffies)
{
#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	long diff;

	if (!dg)
		return false;

	diff = jiffies - dg->dg_endio_last_activity;
	if (diff > idle_jiffies)
		return true;
#endif
	return false;
}

static inline void td_devgroup_poke(struct td_devgroup *dg)
{
	if (unlikely (!dg))
		return;

	td_work_node_poke(&dg->dg_work_node);
}

/* locking the devgroup mutex */
static inline void td_devgroup_lock(struct td_devgroup *dg)
{
	mutex_lock(&dg->dg_mutex);
}

static inline void td_devgroup_unlock(struct td_devgroup *dg)
{
	mutex_unlock(&dg->dg_mutex);
}

/* go through list safely */

#define td_devgroup_for_each_active_device(dg,dev) \
	list_for_each_entry(dev,&dg->dg_devs_list_active,td_devgroup_active_link)

/* tracking thread usage */

static inline int td_devgroup_is_running(struct td_devgroup *dg)
{
	return !!dg->dg_work_node.wn_worker_count;
}

#ifdef CONFIG_TERADIMM_TRACK_CPU_USAGE
static inline void td_busy_reset(struct td_devgroup *dg)
{
	memset(&dg->dg_cpu_stats, 0, sizeof(dg->dg_cpu_stats));
}

static inline void td_busy_start(struct td_devgroup *dg)
{
	if (dg) {
		td_cpu_start(&dg->dg_cpu_stats, TD_CPU_DRV_MAIN);
	}
}

static inline void td_busy_end(struct td_devgroup *dg)
{
	if (dg) {
		td_cpu_end(&dg->dg_cpu_stats);
	}
}

static inline int td_switch_task(struct td_devgroup *dg, int which)
{
	if (dg) {
		return td_cpu_switch(&dg->dg_cpu_stats, which);
	}
	return -1;
}

#else

static inline void td_busy_reset(struct td_devgroup *dg) {}
static inline void td_busy_start(struct td_devgroup *dg) {}
static inline void td_busy_end(struct td_devgroup *dg)   {}
static inline int td_switch_task(struct td_devgroup *dg, int which) { return -1; }

#endif

extern void td_busy_dump(struct td_devgroup *dg);

#endif
