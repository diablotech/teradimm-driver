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

#include "td_devgroup.h"
#include "td_device.h"
#include "td_worker.h"
#include "td_eng_hal.h"
#include "td_compat.h"

uint td_endio_msec = 0;

module_param_named(endio_msec, td_endio_msec, uint, 0444);
MODULE_PARM_DESC(endio_msec, "Delay for endio");

#  define TD_DG_CONF_GENERAL_ENTRY(what, when, min, max) \
        [TD_DEVGROUP_CONF_GENERAL_##what] = { check_##when, min, max },
#  define TD_DG_CONF_WORKER_ENTRY(what, when, min, max) \
        [TD_DEVGROUP_CONF_WORKER_##what] = { check_##when, min, max },

static int check_always(struct td_devgroup *dg, uint32_t u1, uint64_t u2) {
	return 0;
}

static int check_token_recompute(struct td_devgroup *dg, uint32_t u1, uint64_t u2)
{
	atomic_set(&dg->dg_work_node.wn_token_recompute_needed, 1);
	return 0;
}

const struct td_dg_conf_var_desc td_dg_conf_general_var_desc[TD_DEVGROUP_CONF_GENERAL_MAX] = {
#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	TD_DG_CONF_GENERAL_ENTRY(ENDIO_ENABLE,                            always,   0, UINT_MAX)
	TD_DG_CONF_GENERAL_ENTRY(ENDIO_SPIN_MSEC,                         always,   1, UINT_MAX)
#endif
};

const struct td_dg_conf_var_desc td_dg_conf_worker_var_desc[TD_DEVGROUP_CONF_WORKER_MAX] = {
	TD_DG_CONF_WORKER_ENTRY(MAX_OCCUPANCY,                 token_recompute,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(EXTRA_TOKENS,                  token_recompute,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(MAX_LOOPS,                              always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(WITHOUT_DEVS_NSEC,                      always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(SHARE_NSEC,                             always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(RELEASE_NSEC,                           always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(FORCE_RELEASE_NSEC,                     always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(DEV_IDLE_JIFFIES,                       always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(DEV_WAIT_JIFFIES,                       always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(SLEEP_JIFFIES_MIN,                      always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(SLEEP_JIFFIES_MAX,                      always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(SYNC_JIFFIES,                           always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(WAKE_SHARE,                             always,  0, UINT_MAX)
	TD_DG_CONF_WORKER_ENTRY(WAKE_SLEEP,                             always,  0, UINT_MAX)
};

/* ---- database of all device groups ---- */

static struct mutex td_devgroup_list_mutex; /**< prevents races with/between creates/deletes */
static struct list_head td_devgroup_pool;
static unsigned td_devgroup_pool_count;
static spinlock_t td_devgroup_pool_lock;

/** finds a named devgroup, without taking a lock
 * @sa td_devgroup_get_by_name()
 */
static struct td_devgroup *__td_devgroup_find_by_name(const char *name)
{
	struct td_devgroup *dg;

	list_for_each_entry(dg, &td_devgroup_pool, dg_pool_link) {
		if (strncmp(name, dg->dg_name, TD_DEVGROUP_NAME_MAX))
			continue;

		return dg;
	}

	return NULL;
}

static struct td_devgroup *__td_devgroup_find_by_node(int socket)
{
	struct td_devgroup *dg;

	list_for_each_entry(dg, &td_devgroup_pool, dg_pool_link) {
		if (dg->dg_socket != socket)
			continue;

		return dg;
	}

	return NULL;
}

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
static int td_devgroup_endio(void *thread_data);
#endif

static int __td_devgroup_start(struct td_devgroup *dg)
{
	int rc;


#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	dg->dg_endio_task = kthread_create(td_devgroup_endio, dg,
			TD_DEVGROUP_THREAD_NAME_PREFIX "%s/endio",
			dg->dg_name);
	if (unlikely(IS_ERR_OR_NULL(dg->dg_endio_task))) {
		pr_err("Failed "TD_DEVGROUP_THREAD_NAME_PREFIX "%s/endio "
				"thread creation: err=%ld\n", dg->dg_name,
				PTR_ERR(dg->dg_endio_task));
		rc = PTR_ERR(dg->dg_endio_task) ?: -EFAULT;
		dg->dg_endio_task = NULL;
		goto error_start_endio;
	}
	wake_up_process(dg->dg_endio_task);
#endif


	rc = td_work_node_init(&dg->dg_work_node, dg);
	if (rc < 0)
		return rc;

	rc = td_work_node_start(&dg->dg_work_node);
	if (rc < 0)
		goto error_work_node_start;

	return 0;

error_work_node_start:
	td_work_node_exit(&dg->dg_work_node);

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	/* Stop our ENDIO thread */
	kthread_stop(dg->dg_endio_task);
	dg->dg_endio_task = NULL;

error_start_endio:
#endif

	return rc;
}

static int __td_devgroup_stop(struct td_devgroup *dg)
{
	td_work_node_stop(&dg->dg_work_node);

	td_work_node_exit(&dg->dg_work_node);

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	if (dg->dg_endio_task) {
		kthread_stop(dg->dg_endio_task);
		dg->dg_endio_task = NULL;
		pr_warn("Stopped endio thread "TD_DEVGROUP_THREAD_NAME_PREFIX "%s/endio\n",
			dg->dg_name);
	}
#endif

	return 0;
}

/** allocate, initialize, and return a new device group with refcnt=1 */
static struct td_devgroup *__td_devgroup_create(const char *name, int socket,
		int nice)
{
	int rc;
	struct td_devgroup *dg;

	rc = -ENOMEM;
	dg = kzalloc_node(sizeof(*dg), GFP_KERNEL, socket);
	if (!dg)
		goto error_alloc;

	strncpy(dg->dg_name, name, TD_DEVGROUP_NAME_MAX);

	init_waitqueue_head(&dg->dg_event);

	INIT_LIST_HEAD(&dg->dg_devs_list);
	spin_lock_init(&dg->dg_devs_lock);

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	init_waitqueue_head(&dg->dg_endio_event);
	spin_lock_init(&dg->dg_endio_lock);
	bio_list_init(&dg->dg_endio_success);
	bio_list_init(&dg->dg_endio_failure);
#endif

	mutex_init(&dg->dg_mutex);

	dg->dg_socket = socket;
	dg->dg_nice = nice;
	dg->dg_next_pause_jiffies = 0;

	if (td_endio_msec) {
#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
		td_dg_conf_general_var_set(dg, ENDIO_ENABLE, 1);
		td_dg_conf_general_var_set(dg, ENDIO_SPIN_MSEC, td_endio_msec);
#else
		pr_warn("Module option endio_msec ignored\n");
#endif
	}

	td_dg_conf_worker_var_set(dg, MAX_OCCUPANCY, TD_WORKER_MAX_OCCUPANCY);
	td_dg_conf_worker_var_set(dg, EXTRA_TOKENS, TD_WORKER_EXTRA_TOKENS);
	td_dg_conf_worker_var_set(dg, MAX_LOOPS, TD_WORKER_MAX_LOOPS);
	td_dg_conf_worker_var_set(dg, WITHOUT_DEVS_NSEC, TD_WORKER_WITHOUT_DEVS_NSEC);
	td_dg_conf_worker_var_set(dg, SHARE_NSEC, TD_WORKER_SHARE_NSEC);
	td_dg_conf_worker_var_set(dg, RELEASE_NSEC, TD_WORKER_RELEASE_NSEC);
	td_dg_conf_worker_var_set(dg, FORCE_RELEASE_NSEC, TD_WORKER_FORCE_RELEASE_NSEC);
	td_dg_conf_worker_var_set(dg, DEV_IDLE_JIFFIES, TD_WORKER_DEV_IDLE_JIFFIES);
	td_dg_conf_worker_var_set(dg, DEV_WAIT_JIFFIES, TD_WORKER_DEV_WAIT_JIFFIES);
	td_dg_conf_worker_var_set(dg, SLEEP_JIFFIES_MIN, TD_WORKER_SLEEP_JIFFIES_MIN);
	td_dg_conf_worker_var_set(dg, SLEEP_JIFFIES_MAX, TD_WORKER_SLEEP_JIFFIES_MAX);
	td_dg_conf_worker_var_set(dg, SYNC_JIFFIES, TD_WORKER_SYNC_JIFFIES);
	td_dg_conf_worker_var_set(dg, WAKE_SHARE, TD_WORKER_WAKE_SHARE);
	td_dg_conf_worker_var_set(dg, WAKE_SLEEP, TD_WORKER_WAKE_SLEEP);

	atomic_set(&dg->dg_refcnt, 1);

	rc = __td_devgroup_start(dg);
	if (rc)
		goto error_start;

	return dg;

error_start:
	kfree(dg);
error_alloc:
	return ERR_PTR(rc);
}

/* ---- init/exit ---- */

int __init td_devgroup_init(void)
{
	mutex_init(&td_devgroup_list_mutex);
	INIT_LIST_HEAD(&td_devgroup_pool);
	spin_lock_init(&td_devgroup_pool_lock);
	td_devgroup_pool_count = 0;

	return 0;
}

void td_devgroup_exit(void)
{
	WARN_ON(!list_empty(&td_devgroup_pool));
	WARN_ON(td_devgroup_pool_count);

	return;
}

/* ---- reference counting interface ---- */

struct td_devgroup *td_devgroup_get_by_name(const char *name)
{
	struct td_devgroup *dg;

	spin_lock_bh(&td_devgroup_pool_lock);
	dg = __td_devgroup_find_by_name(name);
	if (dg)
		atomic_inc(&dg->dg_refcnt);
	spin_unlock_bh(&td_devgroup_pool_lock);

	return dg;
}

struct td_devgroup *td_devgroup_get_by_node(int socket)
{
	struct td_devgroup *dg;

	spin_lock_bh(&td_devgroup_pool_lock);
	dg = __td_devgroup_find_by_node(socket);
	if (dg)
		atomic_inc(&dg->dg_refcnt);
	spin_unlock_bh(&td_devgroup_pool_lock);

	return dg;
}

void td_devgroup_put(struct td_devgroup *dg)
{
	if (!atomic_dec_and_test(&dg->dg_refcnt))
		return;

	/* getting here indicates that the last reference was removed */

	WARN_ON(!list_empty(&dg->dg_devs_list));
	WARN_ON(dg->dg_devs_count);

	kfree(dg);
}

/* Check that socket of dg matches socket of dev.
 * Returns: 0 on failure, 1 on success. */
static inline int td_devgroup_socket_matches(struct td_devgroup *dg, struct td_device *dev)
{
	return !! (dg->dg_socket == dev->td_cpu_socket);
}

/* ---- device management ---- */

int td_devgroup_add_device(struct td_devgroup *dg, struct td_device *dev)
{
	int rc;

	td_devgroup_lock(dg);

	rc = -EBUSY;
	if (unlikely (dev->td_devgroup))
		goto error_busy;

	rc = -EINVAL;
	if (unlikely (!td_devgroup_is_running(dg)))
		goto error_not_running;

	rc = -EXDEV;
	if (unlikely (!td_devgroup_socket_matches(dg, dev)))
		goto error_wrong_cpu;

	/**! Now that we are going to add it to the list, we need a reference
	 * to the device
	 */
	td_device_hold(dev);

	rc = td_work_node_attach_device(&dg->dg_work_node, dev);
	if (rc < 0)
		goto error_work_node_attach;

	/* put it on the request list */
	spin_lock_bh(&dg->dg_devs_lock);
	list_add_tail(&dev->td_devgroup_link, &dg->dg_devs_list);
	dg->dg_devs_count ++;
	dev->td_devgroup = dg;
	spin_unlock_bh(&dg->dg_devs_lock);

	td_work_item_enable(dev->td_work_item);

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* wait until it had a chance to complete a pass */
	(void) td_devgroup_sync_device(dg, dev);

	td_devgroup_unlock(dg);

	return 0;

error_work_node_attach:
	td_device_put(dev);
error_wrong_cpu:
error_not_running:
error_busy:

	td_devgroup_unlock(dg);

	pr_err("Failed to attach device '%s' to group '%s', error=%d.\n",
			dev->os.name, dg->dg_name, rc);

	return rc;
}

int td_devgroup_remove_device(struct td_devgroup *dg, struct td_device *dev)
{
	int rc;

	td_work_item_disable(dev->td_work_item);

	(void) td_devgroup_sync_device(dg, dev);

	td_devgroup_lock(dg);

	rc = -ENOENT;
	if (unlikely (dev->td_devgroup != dg))
		goto bail_wrong_dg;

	rc = -EINVAL;
	if (unlikely (!td_devgroup_is_running(dg)))
		goto bail_not_running;

	/* request it */

	spin_lock_bh(&dg->dg_devs_lock);
	list_del(&dev->td_devgroup_link);
	dg->dg_devs_count --;
	dev->td_devgroup = NULL;
	spin_unlock_bh(&dg->dg_devs_lock);

	/* wake up the thread */

	td_devgroup_poke(dg);

	/* wait until it had a chance to complete a pass */
	td_work_node_detach_device(&dg->dg_work_node, dev);

	(void) td_devgroup_sync_device(dg, dev);

	td_devgroup_unlock(dg);

	td_device_put(dev);

	return 0;

bail_not_running:
bail_wrong_dg:

	td_devgroup_unlock(dg);

	return rc;
}

/* ---- external interface ---- */

int td_devgroup_dump_names(char *buf, size_t len, uint32_t *count)
{
	int rc = 0;
	struct td_devgroup *dg, *safe;
	char *p, *e;

	*count = 0;

	if (len < TD_DEVGROUP_NAME_MAX)
		return -ETOOSMALL;

	p = buf;
	e = buf + len - TD_DEVGROUP_NAME_MAX;

	mutex_lock(&td_devgroup_list_mutex);

	list_for_each_entry_safe(dg, safe, &td_devgroup_pool, dg_pool_link) {

		p += rc = snprintf(p, e-p, "%s\n", dg->dg_name);
		if (rc<0)
			break;

		(*count) ++;

		if ((e-p) < TD_DEVGROUP_NAME_MAX) {
			rc = -ETOOSMALL;
			break;
		}
	}

	mutex_unlock(&td_devgroup_list_mutex);

	if (rc<0)
		return rc;

	*p=0;

	return p-buf;
}

int td_devgroup_create(const char *name, int node, int nice)
{
	int rc;
	struct td_devgroup *dg;

	mutex_lock(&td_devgroup_list_mutex);

	/* see if we have a group by this name already */
	spin_lock_bh(&td_devgroup_pool_lock);
	dg = __td_devgroup_find_by_name(name);
	spin_unlock_bh(&td_devgroup_pool_lock);

	/* if we have one, stop now */
	rc = -EEXIST;
	if (dg)
		goto bail_with_mutex;

	/* see if we have a group for this node */
	spin_lock_bh(&td_devgroup_pool_lock);
	dg = __td_devgroup_find_by_node(node);
	spin_unlock_bh(&td_devgroup_pool_lock);

	/* if we have one, stop now */
	rc = -EEXIST;
	if (dg)
		goto bail_with_mutex;

	/* allocate/create a new group */
	dg = __td_devgroup_create(name, node, nice);
	if (IS_ERR(dg)) {
		rc = PTR_ERR(dg);
		goto bail_with_mutex;
	}
	
	/* add the new device to the list */
	spin_lock_bh(&td_devgroup_pool_lock);
	list_add_tail(&dg->dg_pool_link, &td_devgroup_pool);
	td_devgroup_pool_count ++;
	spin_unlock_bh(&td_devgroup_pool_lock);

	__module_get(THIS_MODULE);

	/* success */
	rc = 0;

bail_with_mutex:
	mutex_unlock(&td_devgroup_list_mutex);

	return rc;
}


int td_devgroup_delete(const char *name)
{
	int rc;
	bool has_devices;
	struct td_devgroup *dg;

	mutex_lock(&td_devgroup_list_mutex);

	rc = -ENOENT;
	dg = td_devgroup_get_by_name(name);
	if (!dg)
		goto bail_no_ref;

	td_devgroup_lock(dg);

	rc = -EBUSY;
	has_devices = !list_empty(&dg->dg_devs_list);
	if (has_devices || dg->dg_devs_count)
		goto bail_with_mutex;

	spin_lock_bh(&td_devgroup_pool_lock);
	list_del(&dg->dg_pool_link);
	td_devgroup_pool_count --;
	spin_unlock_bh(&td_devgroup_pool_lock);

	rc = __td_devgroup_stop(dg);
	if (rc)
		goto bail_with_mutex;

	/* return the reference held by the list */
	td_devgroup_put(dg);


	module_put(THIS_MODULE);

	rc = 0;

bail_with_mutex:
	td_devgroup_unlock(dg);
	/* return the reference obtained above with _get() */
	td_devgroup_put(dg);
bail_no_ref:
	mutex_unlock(&td_devgroup_list_mutex);
	return rc;
}

int td_devgroup_start(const char *name)
{
	int rc;
	struct td_devgroup *dg;

	rc = -ENOENT;
	dg = td_devgroup_get_by_name(name);
	if (!dg)
		goto bail_no_ref;

	td_devgroup_lock(dg);

	rc = __td_devgroup_start(dg);

	td_devgroup_unlock(dg);
	/* return the reference obtained above with _get() */
	td_devgroup_put(dg);
bail_no_ref:

	return rc;
}

int td_devgroup_stop(const char *name)
{
	int rc;
	struct td_devgroup *dg;

	rc = -ENOENT;
	dg = td_devgroup_get_by_name(name);
	if (!dg)
		goto bail_no_ref;

	td_devgroup_lock(dg);

	rc = __td_devgroup_stop(dg);
	td_busy_dump(dg);

	td_devgroup_unlock(dg);
	/* return the reference obtained above with _get() */
	td_devgroup_put(dg);
bail_no_ref:

	return rc;
}

int td_devgroup_sync_device(struct td_devgroup *dg,
		struct td_device *dev)
{
	int rc;

	td_devgroup_hold(dg);
	td_device_hold(dev);

	rc = td_work_node_synchronize_item(&dg->dg_work_node,
			dev->td_work_item);

	td_device_put(dev);
	td_devgroup_put(dg);

	return rc;
}

void td_busy_dump(struct td_devgroup *dg)
{
#ifdef CONFIG_TERADIMM_TRACK_CPU_USAGE
	struct td_cpu_stats *s = &dg->dg_cpu_stats;
	cycles_t drv_main  = s->cpu_totals[TD_CPU_DRV_MAIN],
		 drv_task  = s->cpu_totals[TD_CPU_DRV_TASK],
		 drv_cmd   = s->cpu_totals[TD_CPU_DRV_CMD],
		 drv_data  = s->cpu_totals[TD_CPU_DRV_DATA],
		 drv_poll  = s->cpu_totals[TD_CPU_DRV_POLL];
	cycles_t drv_ttl   = drv_main + drv_cmd + drv_data + drv_poll;
	cycles_t sim_token = s->cpu_totals[TD_CPU_SIM_TOKEN],
		 sim_stat  = s->cpu_totals[TD_CPU_SIM_STATUS],
		 sim_invl  = s->cpu_totals[TD_CPU_SIM_INVALIDATE];
	cycles_t sim_ttl   = sim_token + sim_stat + sim_invl;
	cycles_t ttl = drv_ttl + sim_ttl;

	ulong drv_main_usec  = td_cycles_to_usec(drv_main),
	      drv_task_usec  = td_cycles_to_usec(drv_task),
	      drv_cmd_usec   = td_cycles_to_usec(drv_cmd),
	      drv_data_usec  = td_cycles_to_usec(drv_data),
	      drv_poll_usec  = td_cycles_to_usec(drv_poll),
	      drv_ttl_usec   = td_cycles_to_usec(drv_ttl),
	      sim_token_usec = td_cycles_to_usec(sim_token),
	      sim_stat_usec  = td_cycles_to_usec(sim_stat),
	      sim_invl_usec  = td_cycles_to_usec(sim_invl),
	      sim_ttl_usec   = td_cycles_to_usec(sim_ttl),
	      ttl_usec       = td_cycles_to_usec(ttl);

	/* avoid division by zero */
	ttl_usec = ttl_usec ?: 1;

	printk("TeraDIMM %s: ttl %lu.%06lu sec { "
			"drv %lu%% %lu.%06lu sec, "
			"sim %lu%% %lu.%06lu sec }\n",
			dg->dg_name,

			ttl_usec / 1000000, ttl_usec % 1000000,

			(drv_ttl_usec * 100) / ttl_usec,
			drv_ttl_usec / 1000000, drv_ttl_usec % 1000000,

			(sim_ttl_usec * 100) / ttl_usec,
			sim_ttl_usec / 1000000, sim_ttl_usec % 1000000);

	if (drv_ttl_usec)
		pr_info("  drv { "
			"main %lu%% %lu.%06lu sec, "
			"task %lu%% %lu.%06lu sec, "
			"cmd %lu%% %lu.%06lu sec, "
			"data %lu%% %lu.%06lu sec, "
			"poll %lu%% %lu.%06lu sec }\n",

			(drv_main_usec * 100) / drv_ttl_usec,
			drv_main_usec / 1000000, drv_main_usec % 1000000,

			(drv_task_usec * 100) / drv_ttl_usec,
			drv_task_usec / 1000000, drv_task_usec % 1000000,

			(drv_cmd_usec * 100) / drv_ttl_usec,
			drv_cmd_usec / 1000000, drv_cmd_usec % 1000000,

			(drv_data_usec * 100) / drv_ttl_usec,
			drv_data_usec / 1000000, drv_data_usec % 1000000,

			(drv_poll_usec * 100) / drv_ttl_usec,
			drv_poll_usec / 1000000, drv_poll_usec % 1000000);

	if (sim_ttl_usec)
		pr_info("  sim { "
			"tok %lu%% %lu.%06lu sec, "
			"stat %lu%% %lu.%06lu sec, "
			"invl %lu%% %lu.%06lu sec }\n",

			(sim_token_usec * 100) / sim_ttl_usec,
			sim_token_usec / 1000000, sim_token_usec % 1000000,

			(sim_stat_usec * 100) / sim_ttl_usec,
			sim_stat_usec / 1000000, sim_stat_usec % 1000000,

			(sim_invl_usec * 100) / sim_ttl_usec,
			sim_invl_usec / 1000000, sim_invl_usec % 1000000);
#endif
}

int td_devgroup_get_counter(struct td_devgroup *dg,
		struct td_worker *w,
		enum td_devgroup_counter_type type,
		uint32_t cntr_num, uint64_t *val)
{
	int rc = -EINVAL;
	uint64_t *reg = NULL;

	if (type >= TD_DEVGROUP_COUNTER_TYPE_MAX)
		goto bail;

	switch (type) {
	default:
		goto bail;

	case TD_DEVGROUP_COUNTER_ENDIO:
		if (cntr_num >= TD_DEVGROUP_ENDIO_COUNT_MAX)
			goto bail;
		reg = &dg->counters.endio[cntr_num];
		break;

	case TD_DEVGROUP_COUNTER_WORKER:
		if (cntr_num >= TD_DEVGROUP_WORKER_COUNT_MAX)
			goto bail;
		if (!w)
			goto bail;
		reg = &w->counters[cntr_num];
		break;

	}

	*val = *reg;
	rc = 0;

bail:
	return rc;
}

int td_devgroup_get_conf(struct td_devgroup *dg,
		enum td_devgroup_conf_type type,
		uint32_t conf_num, uint64_t *val)
{
	int rc = -ENOENT;
	uint64_t *reg = NULL;

	if (type >= TD_DEVGROUP_CONF_TYPE_MAX)
		goto bail;

	switch (type) {
	default:
		goto bail;

	case TD_DEVGROUP_CONF_GENERAL:
		if (conf_num >= TD_DEVGROUP_CONF_GENERAL_MAX)
			goto bail;
		reg = &dg->conf.general[conf_num];
		break;

	case TD_DEVGROUP_CONF_WORKER:
		if (conf_num >= TD_DEVGROUP_CONF_WORKER_MAX)
			goto bail;
		reg = &dg->conf.worker[conf_num];
		break;

	}

	td_devgroup_lock(dg);
	*val = *reg;
	td_devgroup_unlock(dg);
	rc = 0;

bail:
	return rc;
}

int td_devgroup_set_conf(struct td_devgroup *dg,
		enum td_devgroup_conf_type type,
		uint32_t conf_num, uint64_t val)
{
	int rc = -EINVAL;
	const struct td_dg_conf_var_desc *descr;
	uint64_t *reg;
	if (type >= TD_DEVGROUP_CONF_TYPE_MAX)
		goto bail;

	switch (type) {

	default:
		goto bail;

	case TD_DEVGROUP_CONF_GENERAL:
		if (conf_num >= TD_DEVGROUP_CONF_GENERAL_MAX)
			goto bail;
		descr = &td_dg_conf_general_var_desc[conf_num];
		reg = &dg->conf.general[conf_num];
		break;

	case TD_DEVGROUP_CONF_WORKER:
		if (conf_num >= TD_DEVGROUP_CONF_WORKER_MAX)
			goto bail;
		descr = &td_dg_conf_worker_var_desc[conf_num];
		reg = &dg->conf.worker[conf_num];
		break;
	}

	rc = -ENOENT;
	if(descr->check == NULL)
		goto bail;

	rc = -EBUSY;
	if (descr->check(dg, conf_num, val))
		goto bail;

	rc = -EINVAL;
	if (val < descr->min || val > descr->max)
		goto bail;

	td_devgroup_lock(dg);
	*reg = val;
	td_devgroup_unlock(dg);
	rc = 0;

bail:
	return rc;
}

/* ---- ---- */

#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
/**
 * Add a bio to the back of the "done" list
 * @param eng    - the device
 * @param bio    - BIO to queue for completion
 */
void td_devgroup_queue_endio(struct td_devgroup *dg, td_bio_ref bio, int result)
{
	spin_lock_bh(&dg->dg_endio_lock);

	if (likely(result == 0))
		bio_list_add(&dg->dg_endio_success, bio);
	else
		bio_list_add(&dg->dg_endio_failure, bio);

	dg->dg_endio_count ++;
	dg->dg_endio_ts = jiffies;

	spin_unlock_bh(&dg->dg_endio_lock);
	td_dg_counter_var_endio_inc(dg, QUEUED);
}

unsigned __td_devgroup_do_endio (struct td_devgroup* dg)
{
	unsigned count = 0;
	unsigned long ts_delta;
	td_bio_ref bio;
	struct bio_list good, bad;

	if (dg == NULL || ! dg->dg_endio_count)
		return 0;

	bio_list_init(&good);
	bio_list_init(&bad);

	ts_delta = jiffies;
	/* Grab the lock */
	spin_lock_bh(&dg->dg_endio_lock);

	ts_delta -= dg->dg_endio_ts;

	/* Stash good ones */
	bio_list_merge(&good, &dg->dg_endio_success);
	bio_list_init(&dg->dg_endio_success);

	/* Stash bad ones */
	bio_list_merge(&bad, &dg->dg_endio_failure);
	bio_list_init(&dg->dg_endio_failure);


	dg->dg_endio_count = 0;
	dg->dg_endio_ts = 0;

	spin_unlock_bh(&dg->dg_endio_lock);

	if (ts_delta > 3) {
		pr_warn("DG %s endio lost %lu jiffies\n", dg->dg_name,
				ts_delta);
	}
	
	/* Now we complete things, good first */
	while((bio = bio_list_pop(&good))) {
		count++;
		td_bio_complete_success(bio);
	}

	while((bio = bio_list_pop(&bad))) {
		count++;
		td_bio_complete_failure(bio);
	}

	td_dg_counter_var_endio_add(dg, DONE, count);

	return count;
}

static int td_devgroup_endio_condition(struct td_devgroup *dg)
{
	if (dg->dg_endio_count)
		return 1;

	if (kthread_should_stop())
		return 1;
	
	return 0;
}


static int td_devgroup_endio(void *thread_data)
{
	struct td_devgroup *dg = thread_data;

	set_user_nice(current, dg->dg_nice);

	dg->dg_endio_last_activity = jiffies;

	while(! kthread_should_stop() ) {
		if (dg->dg_endio_count) {
			unsigned count = __td_devgroup_do_endio(dg);
			if (count) {
				td_dg_counter_var_endio_add(dg, STOLEN, count);
				/* Make sure the completion worldlet gets to run */
				dg->dg_endio_last_activity = jiffies;
				schedule();
			}
		}

		/* Maybe something happend while we were completing IO */
		if (! dg->dg_endio_count) {
			unsigned idle_timeout = 1 + msecs_to_jiffies(td_dg_conf_general_var_get(dg, ENDIO_SPIN_MSEC));
			if (time_after(jiffies, dg->dg_endio_last_activity + idle_timeout)) {
#ifdef TERADIMM_CONFIG_AVOID_EVENTS
				while (!  td_devgroup_endio_condition(dg))
					schedule();
#else
				wait_event_interruptible_timeout(
					dg->dg_endio_event,
					td_devgroup_endio_condition(dg),
					5*HZ);
#endif
			}
		}
	}
	
	return 0;
}

#endif
