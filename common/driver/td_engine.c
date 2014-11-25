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
#include "td_devgroup.h"
#include "td_engine.h"
#include "td_ucmd.h"
#include "td_eng_hal.h"
#include "td_ioctl.h"
#include "td_token.h"
#include "td_biogrp.h"
#include "td_bio.h"
#include "td_eng_completion.h"
#include "td_eng_mcefree.h"
#include "td_ioctl.h"
#include "td_histogram.h"
#include "td_memspace.h"

/* Because of MCEFREE, we need to know the protocol */
#include "td_protocol.h"
#include "td_command.h"


#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
#define TD_TOKEN_DUMP_OFF    0
#define TD_TOKEN_DUMP_ERROR  1
#define TD_TOKEN_DUMP_DEAD   2
#define TD_TOKEN_DUMP_NOW    -1
static int td_token_dump_enable = TD_TOKEN_DUMP_OFF;
module_param_named(token_dump, td_token_dump_enable, int, 0644);
MODULE_PARM_DESC(token_dump, "Dump token history info when engine goes DEAD.");
#endif

#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
#define TD_FWSTATUS_DUMP_OFF    0
#define TD_FWSTATUS_DUMP_ERROR  1
#define TD_FWSTATUS_DUMP_DEAD   2
#define TD_FWSTATUS_DUMP_NOW    -1
static int td_fwstatus_dump_enable = TD_FWSTATUS_DUMP_OFF;
module_param_named(fwstatus_dump, td_fwstatus_dump_enable, int, 0644);
MODULE_PARM_DESC(fwstatus_dump, "Dump fwstatus history info when engine goes DEAD.");
extern void td_fwstatus_dump(struct td_engine *eng);
#endif

/* debug? */
int td_engine_debug = 0;

/** \endcond */

/* structure shared between td_engine_io_begin_block() and helpers */
struct td_io_begin_state {
	int tok_avail;                  /* number of tokens available */
	int core_avail;                 /* number of core buffers available */
	int wr_avail;                   /* number of write buffers available */

	uint tok_started;               /* number of tokens started */

	td_bio_ref bio;                /* bio currently being handled */
};

/* miscellaneous forward declarations */

static void td_release_tok_bio(struct td_token *tok, int result);
static void td_request_start_write(struct td_token*);
static void td_request_start_read(struct td_token*);
static void td_request_eng_write(struct td_token*);
static void td_request_end_read(struct td_token*);
static void td_request_end_common(struct td_token *tok);

static void td_update_sampled_rate(struct td_engine *eng);
static int td_engine_deallocate_rdbuf(struct td_engine *eng, unsigned rdbuf);

/*
 * td_eng_run_thread_work
 *  Insert work into our "thread_work".  This is *not* running from DG context,
 *  that has already been checked by the td_eng_thread_work/td_eng_safe_work()
 *  entry API.
 *  Here, we exchange the atomic64, and make sure to replace it afterwards
 *  if necessary.
 *  Since we're not in the DG, we're here through an IOCTL that must have the
 *  device locked already.  The device/engine has a reference to the DG, so
 *  it will not go away.  Since we (eng) are in it, it can't be stopped, so we
 *  can rely on the fact that it will run our function
 */
int __td_eng_run_thread_work (struct td_engine *eng, struct td_eng_thread_work *work)
{
	int rc;
	void* prev_work;

	/* We need to be locked, to make sure everything stays around */
	WARN_TD_DEVICE_UNLOCKED(td_engine_device(eng));

	rc = -EIO;
	if (! td_engine_devgroup(eng) ) {
		td_eng_err(eng, "No DEVGROUP; can't run work in device thread\n");
		goto error;
	}
	
	if (! td_devgroup_is_running(td_engine_devgroup(eng)) ) {
		td_eng_err(eng, "DEVGROUP not running; can't run work in device thread\n");
		goto error;
	}

	if (work->verbose) td_eng_info(eng, "Submitting work \"%s\"\n", work->name);
	init_completion(&work->work_done);
	
	prev_work = td_atomic_ptr_xchg(&eng->td_thread_work, work);
	td_engine_sometimes_poke(eng);

	wait_for_completion( &work->work_done);

	if (work->verbose) td_eng_info(eng, "work result = %d (%d)\n", work->result, rc);
	rc = work->result;

	while (prev_work) {
		prev_work = td_atomic_ptr_xchg(&eng->td_thread_work, prev_work);
		if (prev_work) {
			struct td_eng_thread_work* work = prev_work;
			td_eng_warn(eng, "work swap storm noticed: '%s'....\n", work->name );
			msleep(100);
		}
	};

error:
	return rc;
}



#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
/**
 * Queue a work task to be processed by the engine
 * @param eng - the device engine
 * @param work_function - The work function to call in the engine thread
 */
int td_engine_queue_task (struct td_engine *eng, 
		void (*work_function)(struct td_engine*, void*),
		void* data)
{
	struct td_task *n_work;
	n_work = mempool_alloc(eng->work_pool, GFP_NOWAIT);
	if (n_work == NULL) {
		td_eng_err(eng, "Couldn't alloc work\n");
		return -ENOMEM;
	}
	n_work->work_function = work_function;
	n_work->data = data;

	spin_lock_bh(&eng->td_queued_task_lock);
	list_add_tail(&n_work->queue_link, &eng->td_queued_task_list);
	eng->td_queued_task_count ++;
	spin_unlock_bh(&eng->td_queued_task_lock);

	td_engine_sometimes_poke(eng);

	return 0;
}


void td_engine_process_tasks (struct td_engine *eng)
{
	spin_lock_bh(&eng->td_queued_task_lock);
	while (!list_empty(&eng->td_queued_task_list)) {
		struct td_task *work = list_first_entry(&eng->td_queued_task_list,
				struct td_task, queue_link);

		td_eng_trace(eng, TR_VMK_AIDAN, "td:scsi:count", eng->td_queued_task_count);
		WARN_ON(eng->td_queued_task_count == 0);
		list_del(&work->queue_link);
		memset(&work->queue_link, 0, sizeof(work->queue_link));
		eng->td_queued_task_count --;
		spin_unlock_bh(&eng->td_queued_task_lock);

		work->work_function(eng, work->data);

		mempool_free(work, eng->work_pool);
		spin_lock_bh(&eng->td_queued_task_lock);
	}
	spin_unlock_bh(&eng->td_queued_task_lock);
}
#endif

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_SLEEP
/* return 0 if it's OK to proceed, negative to kill the IO */
static int td_test_waiting_incoming_capacity(struct td_engine *eng)
{
	int rc = 0;
	uint64_t high_mark = td_eng_conf_var_get(eng, INCOMING_SLEEP);
	uint64_t wake_after = td_eng_conf_var_get(eng, INCOMING_WAKE);
	uint64_t all_bios(struct td_engine *eng)
	{
		return eng->td_queued_bio_writes
		     + eng->td_incoming_bio_writes
		     + eng->td_queued_bio_reads
		     + eng->td_incoming_bio_reads
		     + eng->td_stats.read.req_active_cnt
		     + eng->td_stats.write.req_active_cnt;
	}

	wake_after = max_t(uint64_t,1,wake_after);
	wake_after = min_t(uint64_t,HZ,wake_after);

	atomic64_inc(&eng->td_incoming_backpressure_sleep_count);

	while (unlikely (all_bios(eng) > high_mark)) {
		rc = schedule_timeout_interruptible(wake_after);
		if (rc) {
#if 0
			if (rc == -ERESTARTSYS)
				return -EAGAIN;
#endif
			break;
		}
	}


	/* the device might have gone DEAD while we waited */
	if (unlikely(!td_state_can_accept_requests(eng)))
		return -EIO;

	return 0;
}
#endif

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
/* return 0 if it's OK to proceed, negative to kill the IO */
int td_test_waiting_incoming_capacity(struct td_engine *eng)
{

	int rc, got_it;
	uint64_t sleep_thrsh;
	unsigned active;

	/* this submission task will wait until either:
	 * - system active level drops below the sleep threshold
	 * - engine can no longer accept requests (DEAD)
	 */
	bool __positive_event_condition(struct td_engine *eng)
	{
		/* device may have died */
		if (unlikely(!td_state_can_accept_requests(eng)))
			return 1;

		/* try to become active */
		active = atomic_inc_return(&eng->td_total_system_bios);
		if (likely (active <= sleep_thrsh)) {
			got_it = 1;
			return 1;
		}

		/* again failed to become active */
		atomic_dec(&eng->td_total_system_bios);
		return 0;
	}

	/* the active level represents all bios that were submitted and
	 * includes: queued and in-flight requests */
	active = atomic_inc_return(&eng->td_total_system_bios);

	/* if the feature is disabled, or the active level is less than sleep
	 * threshold, there is nothing to do */
	sleep_thrsh = td_eng_conf_var_get(eng, INCOMING_SLEEP);
	if (likely (!sleep_thrsh || active < sleep_thrsh))
		return 0;

	/* we didn't get in, reduce the active count */
	atomic_dec(&eng->td_total_system_bios);

	/* this task will now wait until things get below the wake threshold
	 * and be woken up by td_eng_account_bio_completion() */
	got_it = 0;
	atomic_inc(&eng->td_total_incoming_waiters);
	rc = wait_event_interruptible(eng->td_incoming_sleep,
			__positive_event_condition(eng));

	atomic_dec(&eng->td_total_incoming_waiters);
#if 0
	/* interrupted */
	if (rc == -ERESTARTSYS)
		return -EAGAIN;
#endif

	/*
	 * If we were interrupted, we couldn't wait, but this BIO is going
	 * in anyways, we need it accounted for (since it will be accounted
	 * for on the way out)
	 */
	if (!got_it)
		active = atomic_inc_return(&eng->td_total_system_bios);

	/* the device might have gone DEAD while we waited */
	if (unlikely(!td_state_can_accept_requests(eng)))
		return -EIO;

	/* OK */
	return 0;
}
#endif

/**
 * Add bio to back of pending list
 * @param eng   - the device
 * @param bio  - block to enqueue for processing
 */
void td_queue_incoming_bio(struct td_engine *eng, td_bio_ref bio)
{
	unsigned is_write;

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE != TD_BACKPRESSURE_NONE
	int rc;
	/* see if it's OK to send the IO, or wait for over capacity condition
	 * to clear up... on occasion we may have to kill the IO */
	rc = td_test_waiting_incoming_capacity(eng);
	if (unlikely(rc)) {
		if (td_ratelimit())
			td_eng_err(eng, "IO canceled by signal\n");
		td_bio_endio(eng, bio, rc, 0);
		return;
	}
#endif

	/* TODO will have to handle barriers / flushes here */
	is_write = td_bio_is_write (bio);

	spin_lock_bh(&eng->td_incoming_bio_lock);

#ifdef CONFIG_TERADIMM_ABSOLUTELY_NO_READS
	if (is_write)
#endif
	td_eng_latency_start(&eng->td_bio_latency, bio);

	bio_list_add(&eng->td_incoming_bios, bio);

	if (is_write)
		eng->td_incoming_bio_writes ++;
	else
		eng->td_incoming_bio_reads ++;

	spin_unlock_bh(&eng->td_incoming_bio_lock);
}

void td_migrate_incoming_to_queued(struct td_engine *eng)
{
	/* avoid cache thrashing just to check that there is nothing there */
	if (!eng->td_incoming_bio_writes
			&& !eng->td_incoming_bio_reads)
		return;

	spin_lock_bh(&eng->td_incoming_bio_lock);

	/* move everything over to the queued list */

	bio_list_merge(&eng->td_queued_bios, &eng->td_incoming_bios);

	eng->td_queued_bio_writes = eng->td_incoming_bio_writes;
	eng->td_queued_bio_reads  = eng->td_incoming_bio_reads;

	/* purge the incoming queue */

	bio_list_init(&eng->td_incoming_bios);

	eng->td_incoming_bio_writes = 0;
	eng->td_incoming_bio_reads = 0;

	spin_unlock_bh(&eng->td_incoming_bio_lock);
}

/**
 * \brief push control message back to the head of the pending list
 * 
 * @param eng    - the device
 * @param ucmd  - control message info
 * 
 * this is an internal engine function... outside users don't
 * get to push to the head of the pending list, they enqueue
 */
static  void td_push_ucmd (struct td_engine *eng, struct td_ucmd *ucmd)
{
	spin_lock_bh(&eng->td_queued_ucmd_lock);

	list_add(&ucmd->queue_link, &eng->td_queued_ucmd_list);

	eng->td_queued_ucmd_count ++;

	spin_unlock_bh(&eng->td_queued_ucmd_lock);


}

/** 
 * \brief add control message to the pending list 
 * 
 * @param eng    - the device
 * @param ucmd  - control message info
 * 
 *	TODO: add comments here
 */
void td_enqueue_ucmd(struct td_engine *eng, struct td_ucmd *ucmd)
{

	/*
	int cpu;
	int write;
	struct hd_struct *part;
	if (ucmd->ioctl.data_len_to_device ||
			ucmd->ioctl.data_len_from_device){

		write = (ucmd->ioctl.data_len_to_device ? 1 : 0);
		*/
		/* ucmds are generally not going to a specific partition
		 * so use sector 1. */
	/*
		part = disk_map_sector_rcu(td_engine_device(eng)->td_disk, 512);
		cpu = part_stat_lock();
		part_inc_in_flight(part, write);
		part_stat_unlock();
	}
	*/

	if (td_state_pm(eng))
		return;

	spin_lock_bh(&eng->td_queued_ucmd_lock);

	list_add_tail(&ucmd->queue_link, &eng->td_queued_ucmd_list);

	eng->td_queued_ucmd_count ++;

	spin_unlock_bh(&eng->td_queued_ucmd_lock);
}

static struct td_ucmd *td_pop_ucmd(struct td_engine *eng, int core_avail)
{
	struct td_ucmd *ucmd = NULL;

	/* dequeue first control message */
	spin_lock_bh(&eng->td_queued_ucmd_lock);
	if (!list_empty(&eng->td_queued_ucmd_list)) {

		ucmd = list_first_entry(&eng->td_queued_ucmd_list,
				struct td_ucmd, queue_link);

		td_eng_trace(eng, TR_CMD, "ucmd:count", eng->td_queued_ucmd_count);
		if (ucmd->hw_cmd) {
			td_eng_trace(eng, TR_CMD, "ucmd:hw", td_available_hardware_commands(eng));
			if (td_available_hardware_commands(eng))
				td_increment_hardware_command(eng);
			else {
				spin_unlock_bh(&eng->td_queued_ucmd_lock);
				return NULL;
			}
		} else if (! ucmd->locked) {
			if (! core_avail) {
				spin_unlock_bh(&eng->td_queued_ucmd_lock);
				return NULL;
			}
		}

		list_del(&ucmd->queue_link);
		memset(&ucmd->queue_link, 0, sizeof(ucmd->queue_link));

		eng->td_queued_ucmd_count --;
	}
	spin_unlock_bh(&eng->td_queued_ucmd_lock);

	td_eng_trace(eng, TR_CMD, "ucmd:pop", (uint64_t)ucmd);
	return ucmd;
}

/**
 * return true if this bio would collide with active requests
 *
 * this function behaves differently based on the collision_check variable:
 * 0 - collision check is disabled, always returns zero
 * 1 - returns non-zero if an active RMW token matches new bio's LBA
 * 2 - returns non-zero if an active write token matches new bio's LBA
 * (all other values alias to 1)
 */
static int td_engine_bio_collision(struct td_engine *eng, td_bio_ref bio)
{
	int collision, mode;
	uint64_t bio_lba;
	struct td_token *tok, *nxt;
	td_bio_ref tbio;
//	uint64_t start;

//	start = (uint64_t)td_get_cycles();

	mode = (int)td_eng_conf_var_get(eng, COLLISION_CHECK);

	switch (mode) {
	case 0: /* if feature disabled, everything goes */
		return 0;

	default:
	case 1: /* need to test any R-M-W */
		if (0 == td_active_rmw_tokens(eng))
			return 0;
		break;

	case 2: /* need to test any writes */
		if (0 == td_active_tokens(eng, TD_TOK_FOR_FW))
			return 0;
		break;

	case 3: /* anything goes */
		break;
	}

	bio_lba = td_bio_lba(eng, bio);

	td_trace(&eng->td_trace, TR_RMW, "collision:LBA", bio_lba);

	/* this is a crude search over the active list of requests */

	collision = 0;
	td_active_tokens_lock(eng);

	switch (mode) {
	default:
	case 1: /* any inflight R-M-W to this LBA could cause issues */

		for_each_token_list_token(tok, nxt,
				&eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens) {
			/* first check the LBA for overlap */
			if (tok->lba != bio_lba)
				continue;

			/* skip things that are not R-M-W's */
			if (!tok->rmw)
				continue;

			/* the new bio is attempting to access an LBA on which
			 * there is a RMW */
			collision = 1;
			goto done;
		}
		break;

	case 2: /* any inflight writes to this LBA could cause issues */

		for_each_token_list_token(tok, nxt,
				&eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens) {
			/* first check the LBA for overlap */
			if (tok->lba != bio_lba)
				continue;

			/* this skips tokens that don't have a write bio */
			tbio = tok->host.bio;
			if (!tbio || ! td_bio_is_write(tbio) )
				continue;

			/* the new bio is attempting to access an LBA on which
			 * there is a write */
			collision = 1;
			goto done;
		}
		break;

	case 3: /* any inflight IO to this LBA could cause issues */

		for_each_token_list_token(tok, nxt,
				&eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens) {
			/* first check the LBA for overlap */
			if (tok->lba != bio_lba)
				continue;

			/* skip it if the token doesn't have a bio */
			if (!tok->host.bio)
				continue;

			/* the new bio is attempting to access an LBA on which
			 * there is a write */
			collision = 1;
			goto done;
		}
		break;
	}

done:
	td_active_tokens_unlock(eng);

	td_trace(&eng->td_trace, TR_RMW, "collision:tok",
			collision ? tok->tokid : -1ULL);

	return collision;
}

/**
 * \brief get the next bio to execute
 * @param eng       - engine used
 * @param bios      - initialized bio list to return bios on
 * @param bs        - resource availability info
 *
 * @returns  and sets biop if bio is available, 0 otherwise
 * @return number of bios returned
 */
static inline int td_engine_get_bios(struct td_engine *eng,
		struct bio_list *bios, struct td_io_begin_state *bs)
{
	int span, rc, discard;
	td_bio_ref first, bio = NULL;
//#if defined(_MSC_VER)
//#else
	struct td_biogrp *split_req = NULL;
//#endif
	if (bio_list_empty(&eng->td_queued_bios))
		td_migrate_incoming_to_queued(eng);

	/* peek at the next upcoming bio */
	first = bio_list_peek(&eng->td_queued_bios);
	if (!first)
		return 0;

	if (td_engine_bio_collision(eng, first))
		return 0;

	if (td_bio_is_write(first) ) {
		/* writes are not allowed at this time */
		if (td_engine_hold_back_write(eng))
			return 0;
		/* write is not possible, no WEPs */
		if (! bs->wr_avail)
			return 0;
		/* we have enough write buffers to run this one */
		eng->td_queued_bio_writes --;

	} else
		/* reads are always ok */
		eng->td_queued_bio_reads --;

	/* it's safe to pop it off from the queue */
	bio = bio_list_pop(&eng->td_queued_bios);

	if (unlikely (!bio))
		return 0;

td_eng_trace(eng, TR_BIO, "BIO:pop:bio  ", (uint64_t)bio);
td_eng_trace(eng, TR_BIO, "BIO:pop:write", td_bio_is_write(bio));
td_eng_trace(eng, TR_BIO, "BIO:pop:sctr ", td_bio_get_sector_offset(bio));
td_eng_trace(eng, TR_BIO, "BIO:pop:size ", td_bio_get_byte_size(bio));

	if (unlikely (td_bio_is_empty_barrier(bio)))  {
		td_eng_err(eng, "Empty barriers are not supported in this release\n");
		rc = -EIO;
		td_bio_endio(eng, bio, rc, 0);
		return rc;
	}
#if 0
	if (unlikely (td_bio_is_part(bio))) {
		/* already split, return it */
		bio_list_add(bios, bio);
		return 1;
	}
#endif
	discard = td_bio_is_discard(bio);
	span = td_bio_page_span(bio, TERADIMM_DATA_BUF_SIZE);
	if (likely (span == 1 && discard == 0)) {
		/* normal bio, single LBA span */
		bio_list_add(bios, bio);
		return 1;
	}

	rc = td_split_req_create_list(eng, bio, &split_req, bios);
	if (unlikely (rc<0)) {
		td_eng_err(eng, "failed to split bio, rc=%d dir = %s\n", rc,
				td_bio_is_write(bio) ? "write" : "read");
		td_bio_endio(eng, bio, rc, 0);
		return rc;
	}

	if (unlikely (rc<span && !td_bio_is_discard(bio))) {
		td_eng_err(eng, "split returned too few parts, "
				"span=%u, splits=%d\n", span, rc);
		td_biogrp_free(split_req);
		rc = -EIO;
		td_bio_endio(eng, bio, rc, 0);
		return rc;
	}

	/* increment stats */
	if (td_bio_is_write(first))
		eng->td_stats.write.split_req_cnt ++;
	else
		eng->td_stats.read.split_req_cnt ++;

	return rc;
}

/* push a bit to the start of the queue;
 * used to return a bio that cannot be started now to the head of the queue */
static void td_engine_push_bio(struct td_engine *eng, td_bio_ref bio)
{
	bio_list_add_head(&eng->td_queued_bios, bio);

	if (td_bio_is_write(bio))
		eng->td_queued_bio_writes ++;
	else
		eng->td_queued_bio_reads ++;
}
/* like td_engine_push_bio(), but works on a list */
static void td_engine_push_bio_list(struct td_engine *eng,
		struct bio_list *bios)
{
	td_bio_ref bio;
	bio_list_for_each(bio, bios) {
		if (td_bio_is_write(bio))
			eng->td_queued_bio_writes ++;
		else
			eng->td_queued_bio_reads ++;
	}
	bio_list_merge_head(&eng->td_queued_bios, bios);
}

void __td_terminate_all_outstanding_bios(struct td_engine *eng,
		int reset_active_tokens, int result)
{
	uint cnt = 0;
	td_bio_ref bio;
	struct bio_list terminating;
	enum td_token_type tt;
	struct td_token *tok = NULL, *nxt;

	bio_list_init(&terminating);

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
	atomic_set(&eng->td_total_incoming_waiters, 0);
	wake_up_interruptible_all(&eng->td_incoming_sleep);
#endif
#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_SLEEP
	atomic64_set(&eng->td_incoming_backpressure_sleep_count, 0);
#endif

	td_migrate_incoming_to_queued(eng);

	while ((bio = bio_list_pop(&eng->td_queued_bios) )) {
		if (td_bio_is_write(bio)) {
			eng->td_queued_bio_writes --;
		} else {
			eng->td_queued_bio_reads --;
		}
		bio_list_add(&terminating, bio);
	}

	while((bio = bio_list_pop(&terminating))) {
		int is_write = td_bio_is_write(bio);
		td_bio_endio(eng, bio, result, 0);
		if (is_write)
			eng->td_stats.write.req_failed_cnt ++;
		else
			eng->td_stats.read.req_failed_cnt ++;
		cnt ++;
	}

	if (cnt)
		td_eng_warn(eng, "terminated %u queued bios\n", cnt);

	tt = 0;     /* walk each token type */
	cnt = 0;
	while (tt < TD_TOK_TYPE_MAX) {

		tok = td_tokens_dequeue(&eng->tok_pool[tt].td_active_tokens);
		if (!tok) {
			/* none left in this token type, try the next one */
			tt ++;
			continue;
		}

		/* force reset on token */

		if (reset_active_tokens) {
			td_eng_hal_reset_token(eng, tok);
			tok->reset_count = 0;
		}

		/* complete the command with error */

		if (td_token_is_write(tok)) {
			td_counter_dec_in_flight(eng, tok, 1);
		} else if (td_token_is_read(tok)) {
			td_counter_dec_in_flight(eng, tok, 0);
		} else {
			eng->td_stats.control.req_active_cnt --;
		}

		tok->result = result;
		tok->ops.completion(tok);

		cnt ++;
	}

	for_each_token_list_token(tok, nxt,
			&eng->td_early_completed_reads_tokens) {

		/* force reset on token */
		__td_tokens_del(&eng->td_early_completed_reads_tokens, tok);

		if (reset_active_tokens) {
			td_eng_hal_reset_token(eng, tok);
			tok->reset_count = 0;
		}

		/* complete the command with error */

		td_counter_dec_in_flight(eng, tok, 0);

		tok->result = result;
		tok->ops.completion(tok);

		cnt ++;
	}

	if (cnt)
	td_eng_warn(eng, "... terminated %u active tokens\n", cnt);
}

static int td_engine_io_begin_ucmd(struct td_engine *eng, uint *max);


static void td_engine_run_ucmd_direct(struct td_engine *eng,
		struct td_ucmd *ucmd)
{
	uint max = 1;
	td_ucmd_ready(ucmd);
	spin_lock_bh(&eng->td_queued_ucmd_lock);
	list_add_tail(&ucmd->queue_link, &eng->td_queued_ucmd_list);
	eng->td_queued_ucmd_count ++;
	spin_unlock_bh(&eng->td_queued_ucmd_lock);
	td_engine_io_begin_ucmd(eng, &max);
}

/*
 * Send shutdown command to the device.
 *
 * @param eng       - engine to shutdown.
 *
 * @returns: 0 on success or if not necessary, error code otherwise.
 *
 */
static int td_engine_send_shutdown(struct td_engine *eng)
{
	struct td_ucmd *ucmd;
	int rc;

	if (!eng->pm_power_cycle || !td_eng_conf_var_get(eng, SHUTDOWN) )
		return 0;

	rc = -ENOMEM;
	ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
	if (!ucmd)
		goto nomem;

	rc = td_eng_cmdgen(eng, shutdown, ucmd->ioctl.cmd);
	WARN_ON(rc < 0);
	if (rc)
		goto cmdgen_failed;

	/* Run the command directly. */
	td_engine_run_ucmd_direct(eng, ucmd);

cmdgen_failed:
	if(rc)
		td_ucmd_put(ucmd);

nomem:
	return rc;
}


/* return 0 if things are OK to perform IO */
int td_engine_io_sanity_check(struct td_engine *eng)
{
	/* update the sampled IOPS rate */
	td_update_sampled_rate(eng);

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	if (unlikely (td_token_dump_enable == TD_TOKEN_DUMP_NOW)) {
		td_eng_dump_tok_events(eng);
		td_token_dump_enable = TD_TOKEN_DUMP_OFF;
	}
#endif
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	if (unlikely (td_fwstatus_dump_enable == TD_FWSTATUS_DUMP_NOW)) {
		td_fwstatus_dump(eng);
		td_fwstatus_dump_enable = TD_FWSTATUS_DUMP_OFF;
	}
#endif

	/* Quickly sneak in any safe work */
	if (unlikely(td_atomic_ptr_read(&eng->td_thread_work))) {
		struct td_eng_thread_work *work = td_atomic_ptr_xchg(&eng->td_thread_work, 0);
		if (work) {
			td_eng_trace(eng, TR_ENGWORK, "thread_work:ptr", (uint64_t)work);
			td_eng_trace(eng, TR_ENGWORK, "thread_work:func", (uint64_t)work->func);
			if (work->verbose)
				td_eng_debug(eng, "Running \"%s\" thread_work\n", work->name);
			work->result = work->func(eng, work);
			complete_all(&work->work_done);
		}
	}

	if (td_state_can_talk_to_hw(eng))
		return 0;

	if (td_run_state_check(eng, TERMINATING)) {
		if ( !td_eng_rdbuf_throttling(eng) &&
				td_pending_rdbuf_deallocations(eng) &&
				!td_is_state_stuck(eng))
			return 0;

		td_eng_trace(eng, TR_STATE, "end-termination", td_run_state(eng));
		td_terminate_all_outstanding_bios(eng, 1);
		td_run_state_enter(eng, DEAD);
		return -EIO;
	}

	if (td_run_state_check(eng, PM_DRAIN)) {
		if (!td_engine_queued_work(eng)
				&& !td_all_active_tokens(eng)
				&& !td_early_completed_reads(eng) ) {
			int rc = 0;

			/* if we power cycle, we may need to shutdown the devices. */
			rc = td_engine_send_shutdown(eng);
			if (rc)
				td_eng_err(eng, "Error sending "
						"shutdown: %d\n", rc);
			td_run_state_enter(eng, PM_SHUTDOWN);
		}
		return 0;
	}

	if (td_run_state_check(eng, PM_SHUTDOWN)) {
		td_eng_trace(eng, TR_STATE, "pm_shutdown", td_run_state(eng));
		if (!td_engine_queued_work(eng)
				&& !td_all_active_tokens(eng)
				&& !td_early_completed_reads(eng)) {
			td_run_state_enter(eng, PM_SLEEP);
		}
		return 0;
	}
	if (td_run_state_check(eng, PM_SLEEP)) {
		td_eng_trace(eng, TR_STATE, "pm_sleep", td_run_state(eng));
		return 0;
	}

	if (td_run_state_check(eng, BIO_DRAIN)) {
		if(!td_active_bio_count(eng)) {
			td_run_state_enter(eng, UCMD_ONLY);
		}
		return 0;
	}

	if (td_run_state_check(eng, INIT))
		return td_eng_hal_hw_init(eng);

	/* unhandled, probably DEAD */
	return -EINVAL;
}

/* return non-zero */
int td_engine_needs_cpu_time(struct td_engine *eng)
{
	if (td_state_can_talk_to_hw(eng)) {
		return td_engine_queued_work(eng)
			|| td_all_active_tokens(eng)
			|| td_early_completed_reads(eng)
			|| (td_engine_has_thread_work(eng));
	}

	if (td_run_state_check(eng, PM_DRAIN) ||
			td_run_state_check(eng, PM_SHUTDOWN) ||
			td_run_state_check(eng, BIO_DRAIN) ||
			td_run_state_check(eng, UCMD_ONLY))
		return 1;

	switch (td_run_state(eng)) {
	default:
		break;
	case TD_RUN_STATE_INIT:
	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_FW_PROBE:
	case TD_RUN_STATE_TERMINATING:
		return 1;
	}

	if (td_engine_has_thread_work(eng))
		return 1;

	return 0;
}

/* Windows needs this casted down */
#define SAMPLED_RATE(_a, _b, _d)     (uint32_t)((((_b)-(_a)) * HZ) / _d)

static void td_update_sampled_rate(struct td_engine *eng)
{
	uint64_t delta;
	uint32_t rd_iops, wr_iops, ct_iops, ttl_iops;
	uint32_t rd_bw, wr_bw;

	delta = jiffies - eng->td_sample_start;
	if (delta < eng->td_sample_window)
		return;

	if (delta > (1000*eng->td_sample_window))
		goto reset_and_exit;

	rd_iops = wr_iops = ct_iops = 0;
	rd_bw = wr_bw = 0;

	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:update", jiffies);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:delta", delta);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:HZ",    HZ);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:reads",    eng->td_stats.read.req_completed_cnt - eng->td_sample_reads);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:writes",   eng->td_stats.write.req_completed_cnt - eng->td_sample_writes);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:control",  eng->td_stats.control.req_completed_cnt - eng->td_sample_control);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:rbytes",   eng->td_stats.read.bytes_transfered - eng->td_sample_rbytes);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:wbytes",   eng->td_stats.write.bytes_transfered - eng->td_sample_wbytes);
	if (delta) {
		/*
		 * IOPS = bios / seconds
		 *      = bios * HZ / jiffies
		 *
		 * BW   = bytes / seconds
		 *      = bytes * HZ / jiffies
		 *
		 * For IOPS, we multiple first, to keep as much precision
		 * But for BW, we need to device first to avoid overflow
		 */
		rd_iops = SAMPLED_RATE(eng->td_sample_reads, eng->td_stats.read.req_completed_cnt, delta);
		wr_iops = SAMPLED_RATE(eng->td_sample_writes, eng->td_stats.write.req_completed_cnt, delta);
		ct_iops = SAMPLED_RATE(eng->td_sample_control, eng->td_stats.control.req_completed_cnt, delta);
		rd_bw = SAMPLED_RATE(eng->td_sample_rbytes, eng->td_stats.read.bytes_transfered, delta);
		wr_bw = SAMPLED_RATE(eng->td_sample_wbytes, eng->td_stats.write.bytes_transfered, delta);
	}
	ttl_iops = rd_iops + wr_iops + ct_iops;

	if (ttl_iops && td_eng_conf_var_get(eng, TARGET_IOPS)
			&& ttl_iops > td_eng_conf_var_get(eng, TARGET_IOPS)) {

		uint64_t expected_delta;
		uint64_t expected_jiffies;
		int64_t wait_for, ttl_count;

		ttl_count = eng->td_sample_reads + eng->td_sample_writes
			+ eng->td_sample_control;

		/* how much time should we have taken for this number of IOs ? */
		expected_delta = (ttl_count * HZ)
			/ td_eng_conf_var_get(eng, TARGET_IOPS);

		/* this is what the cycle counter should be at when we're done */
		expected_jiffies = eng->td_sample_start
			+ expected_delta;

		/* how much time should we still wait? */
		wait_for = expected_jiffies - jiffies;

		if (wait_for > 0) {
			if (wait_for > HZ)
				wait_for = HZ;
			td_eng_trace(eng, TR_SAMPLE, "ENG:sample:wait_for", wait_for);
			schedule_timeout_interruptible(wait_for);

			/* recompute */
			delta = jiffies - eng->td_sample_start;
			td_eng_trace(eng, TR_SAMPLE, "ENG:sample:delta", delta);
			if (delta) {
				rd_iops = SAMPLED_RATE(eng->td_sample_reads, eng->td_stats.read.req_completed_cnt, delta);
				wr_iops = SAMPLED_RATE(eng->td_sample_writes, eng->td_stats.write.req_completed_cnt, delta);
				ct_iops = SAMPLED_RATE(eng->td_sample_control, eng->td_stats.control.req_completed_cnt, delta);
				rd_bw = SAMPLED_RATE(eng->td_sample_rbytes, eng->td_stats.read.bytes_transfered, delta);
				wr_bw = SAMPLED_RATE(eng->td_sample_wbytes, eng->td_stats.write.bytes_transfered, delta);
			}
		}
	}

	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:rd_iops", rd_iops);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:wr_iops", wr_iops);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:ct_iops", ct_iops);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:rd_bw",   rd_bw);
	td_eng_trace(eng, TR_SAMPLE, "ENG:sample:wr_bw",   wr_bw);

	eng->td_stats.read.iops = rd_iops;
	eng->td_stats.write.iops = wr_iops;
	eng->td_stats.control.iops = ct_iops;
	eng->td_stats.read.bandwidth = rd_bw;
	eng->td_stats.write.bandwidth = wr_bw;

reset_and_exit:
	eng->td_sample_start = jiffies;

	eng->td_sample_reads  = eng->td_stats.read.req_completed_cnt;
	eng->td_sample_writes  = eng->td_stats.write.req_completed_cnt;
	eng->td_sample_control  = eng->td_stats.control.req_completed_cnt;
	eng->td_sample_rbytes  = eng->td_stats.read.bytes_transfered;
	eng->td_sample_wbytes  = eng->td_stats.write.bytes_transfered;

	td_eng_latency_clear(&eng->td_counters.read.req);
	td_eng_latency_clear(&eng->td_counters.write.req);
	td_eng_latency_clear(&eng->td_counters.control.req);
	td_eng_latency_clear(&eng->td_counters.read.hw);
	td_eng_latency_clear(&eng->td_counters.write.hw);
	td_eng_latency_clear(&eng->td_counters.control.hw);
}
#undef SAMPLED_RATE

/**
 * td_engine_queue_bio helper handling early split of large requests
 */
#if 0
static void td_engine_queue_sreq_bio(struct td_biogrp *bg,
		td_bio_ref bio, void *opaque)
{
	struct td_engine *eng = opaque;
	td_queue_incoming_bio(eng, bio);
	td_engine_poke(eng);
}
#endif

/**
 * \brief receive and queue a bio from the block layer
 *
 * This function is called by the block layer and given a bio to queue.
 */
int td_engine_queue_bio(struct td_engine *eng, td_bio_ref bio)
{

	/* Discard? */
	if (td_bio_is_discard(bio) && td_eng_conf_hw_var_get(eng, DISCARD) == 0) {
		td_eng_err(eng, "Received DISCARD BIO: %p, discarding\n", bio);
		goto bad_request;
	}
	/* we are a 4k device */
	if (unlikely (! td_bio_is_discard(bio) && ! td_bio_aligned_correctly(eng, bio)))
	{
		goto bad_request;
	}

	if (unlikely (! td_bio_is_discard(bio) &&
			td_bio_page_span(bio, TERADIMM_DATA_BUF_SIZE) > TD_SPLIT_REQ_PART_MAX))
	{
		goto bad_request;
	}

	if (td_state_pm(eng))
	{
		if (td_ratelimit())
			td_eng_warn(eng, "request in state %d (pm)\n",
					td_run_state(eng));
		td_bio_endio(eng, bio, -EAGAIN, 0);
		goto done;
	}

#ifdef CONFIG_TERADIMM_BIO_SLEEP
	if (td_state_no_bio(eng))
	{
		/* Give ourselves 59 seconds before failing. */
		int timeout = 59 * 1000;
		while(td_state_no_bio(eng) && timeout > 0) {
			msleep(100);
			timeout -= 100;
		}
		if(timeout <= 0)
			td_eng_warn(eng, "no_bio fall-through to queue\n");
		/* Fall-through expected.  Once delayed, the IO will be
		 * queued.  Upgrades take about 42 seconds as of 1.2 */
	}
#endif

	if (!td_state_can_accept_requests(eng)) {
		if (td_ratelimit())
			td_eng_warn(eng, "request in state %d\n",
					td_run_state(eng));
		td_bio_endio(eng, bio, -EIO, 0);
		goto done;
	}

	/* TODO: should support plugged operation and call blk_queue_plug() */
#if 0
/* this is disabled because td_biogrp_alloc() doesn't lock, and devgroup
 * now uses multiple threads so it's harder to determine when to do it */
	if (unlikely (td_eng_conf_var_get(eng, EARLY_SPLIT_REQ))) {
		int span = td_bio_page_span(bio);
		if (likely (span > 1)) {
			int rc = td_split_req_create(eng, bio,
					td_engine_queue_sreq_bio, eng);
			if (unlikely (rc<0))
				goto bad_request;
			goto done;
		}
	}
#endif

	td_queue_incoming_bio(eng, bio);

	td_engine_sometimes_poke(eng);

	goto done;

bad_request:
	td_eng_err(eng, "bad bio: size=%u sector=%llu direction=%s\n",
			td_bio_get_byte_size(bio),
			td_bio_get_sector_offset(bio),
			td_bio_is_write(bio) ? "write" : "read");
	td_bio_endio(eng, bio, -EIO, 0);
done:
	return 0;
}

static void td_request_start_read(struct td_token *tok)
{
	/* nothing */
}

static void td_request_end_read(struct td_token *tok)
{
	int rc;
	int prev;
	struct td_engine *eng = td_token_engine(tok);

	if (unlikely(!TD_IS_RD_BUFID_VALID(tok->rd_bufid)))
		goto skip_data;

	/* when throttling read buffers, and we have a read buffer to read
	 * from, it's safe to deallocate the next one now */
	if (td_eng_rdbuf_throttling(eng))
		td_eng_send_next_throttled_rdbuf_deallocate(eng);

	/* track completions on tokens */
	td_seen_read_complete(eng, tok->rd_bufid);

	td_eng_trace(eng, TR_CMD, "read-data-start", tok->tokid);

	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_DATA);

	rc = td_eng_hal_read_page(eng, tok);

	td_switch_task(td_engine_devgroup(eng), prev);

	td_eng_trace(eng, TR_CMD, "read-data-end  ", tok->tokid);

	/* the read buffer is no longer needed */
	td_schedule_rdbuf_deallocation(eng, tok->rd_bufid);
	tok->rd_bufid = TD_INVALID_RD_BUFID;

	/* TODO: read_page failures require a retry, which is not handled very
	 * nicely at this time */
	if (unlikely(rc<0)) {
		td_eng_err(eng, "read_page failure: %d\n", rc);
		td_eng_debug(eng, "  TOK %d SSD %d PORT %d LBA %llu OFS %u\n", tok->tokid, tok->to_ssd, tok->port, tok->lba, tok->lba_ofs);
		if (tok->host.bio) {
			td_bio_ref bio = tok->host.bio;
			td_eng_debug(eng, "  BIO %p SECTOR %llu BYTES %u\n",
					bio, td_bio_get_sector_offset(bio),
					td_bio_get_byte_size(bio));
		}
		tok->result = rc;
	}

skip_data:
	td_request_end_common(tok);
}

static void td_request_start_trim(struct td_token *tok)
{
	int rc;
	int prev;
	struct td_engine *eng = td_token_engine(tok);

	td_eng_trace(eng, TR_CMD, "trim-data-start", tok->tokid);

	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_DATA);

	rc = td_eng_hal_trim(eng, tok);

	td_switch_task(td_engine_devgroup(eng), prev);

	td_eng_trace(eng, TR_CMD, "trim-data-end  ", tok->tokid);
}

static void td_request_eng_trim(struct td_token *tok)
{
	td_request_end_common(tok);
}


static void td_request_start_write(struct td_token *tok)
{
	int rc;
	int prev;
	struct td_engine *eng = td_token_engine(tok);

	td_eng_trace(eng, TR_CMD, "write-data-start", tok->tokid);

	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_DATA);

	rc = td_eng_hal_write_page(eng, tok);

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	if (!rc) {
		switch (td_eng_conf_var_get(eng, DOUBLE_WEP_WRITES)) {
		default:
			break;
		case 2:
			tok->use_wep_alias = 1;
			/* fall though desired */
		case 1:
			td_eng_trace(eng, TR_CMD, "write-data-again", tok->use_wep_alias);
			rc = td_eng_hal_write_page(eng, tok);
			break;
		}
	}
#endif
	
	td_switch_task(td_engine_devgroup(eng), prev);

	td_eng_trace(eng, TR_CMD, "write-data-end  ", tok->tokid);
}

static void td_request_eng_write(struct td_token *tok)
{
	td_request_end_common(tok);
}

static void td_update_request_end_counters(struct td_engine *eng,
		struct td_token *tok, unsigned size, int result)
{

	/* update counters */
	if (td_token_is_write(tok)) {
		switch (result) {
		case 0:
			eng->td_stats.write.req_completed_cnt ++;
			eng->td_stats.write.bytes_transfered += size;
			break;
		case -ETIMEDOUT:
			eng->td_stats.write.req_timedout_cnt ++;
			break;
		default:
			eng->td_stats.write.req_failed_cnt ++;
			break;
		}
	} else if(td_token_is_read(tok)) {
		switch (result) {
		case 0:
			eng->td_stats.read.req_completed_cnt ++;
			eng->td_stats.read.bytes_transfered += size;
			break;
		case -ETIMEDOUT:
			eng->td_stats.read.req_timedout_cnt ++;
			break;
		default:
			eng->td_stats.read.req_failed_cnt ++;
			break;
		}
	} else {
		switch (result) {
		case 0:
			eng->td_stats.control.req_completed_cnt ++;
			eng->td_stats.control.bytes_transfered += size;
			eng->td_sample_control ++;
			break;
		case -ETIMEDOUT:
			eng->td_stats.control.req_timedout_cnt ++;
			break;
		default:
			eng->td_stats.control.req_failed_cnt ++;
			break;
		}
	}
}

static void td_release_tok_bio(struct td_token *tok, int result)
{
	td_bio_ref bio;
	unsigned size;
	struct td_engine *eng = td_token_engine(tok);

	bio = tok->host.bio;
	if (!bio)
		return;

	size = td_bio_get_byte_size(bio);

	/* done with this R-M-W */
	if (tok->rmw)
		eng->td_active_rmw_tokens_count --;


	/* complete */
	td_bio_endio(eng, bio, result, tok->ts_end - tok->ts_start);
	tok->host.bio = NULL;

	/* update counters */
	td_update_request_end_counters(eng, tok, size, result);

}

/* Can't be static, teradimm_ops_hw_init needs it for reset... */
void td_request_end_common_bio(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	if (unlikely(td_eng_conf_var_get(eng, VERBOSE_TOK_FAILURES)
				&& tok->result)) {
		td_token_error_dump(tok, "END CMD");
#ifdef CONFIG_MEGADIMM_TRACK_ODDS
		dump_megadimm_last_token_array(
				td_eng_conf_var_get(eng, TOKENS));
#endif
	}

	td_release_tok_bio(tok, tok->result);

	if (unlikely (td_run_state_check(eng, SYNC_IO))) {
		/* IO completed in SYNC mode, we now go to RUNNING mode */
		td_run_state_enter(eng, RUNNING);
	}
}

/* Can't be static, teradimm_ops_hw_init needs it for reset... */
void td_request_end_common_ucmd(struct td_token *tok)
{
	struct td_ucmd *ucmd;

	ucmd = tok->host.ucmd;
	if (!ucmd)
		return;

	td_eng_trace(td_token_engine(tok), TR_CMD, "ucmd:end", (uint64_t)ucmd);
	td_eng_trace(td_token_engine(tok), TR_CMD, "ucmd:result", tok->result);


	if (td_eng_conf_var_get(td_token_engine(tok), SPY_UCMD) > 1)
		td_token_error_dump(tok, "UCMD");

	tok->host.ucmd = NULL;
	tok->host_buf_virt = NULL;

	if(ucmd->hw_cmd)
		td_decrement_hardware_command(td_token_engine(tok));

	if (ucmd->locked) {
		td_eng_debug(td_token_engine(tok),
				"MCEFREE UCMD FIXUP: detach locked core buffer %u\n",
				tok->core_bufid);
		tok->core_bufid = TD_INVALID_CORE_BUFID;
	}

	/* update and wake up waiting process */
	
	td_ucmd_ending(ucmd, tok->result);

	/* release context */
	td_ucmd_put(ucmd);
}

static void td_complete_deallocation(struct td_token *tok, int result)
{
	struct td_engine *eng = td_token_engine(tok);
	unsigned rd_bufid;

	/* this code processes a deallocated read buffer */
	rd_bufid = tok->free_rd_bufid;
	if (!TD_IS_RD_BUFID_VALID(rd_bufid))
		return;

	/* this token is no longer tracking a deallocate */
	eng->td_active_rdbuf_deallocation_token[rd_bufid]
		= TD_INVALID_TOKEN_ID;

	/* reset token flags */
	tok->free_rd_bufid = TD_INVALID_RD_BUFID;

	if (tok->ops.clear_deallocation)
		tok->ops.clear_deallocation(tok);

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	/* mark read buffer as good/failed for orphan tracker */
	eng->td_rd_bufs[rd_bufid].drb_last_failed = !!result;
#endif

	/* if the command was successful, there is nothing else to do */
	switch (result) {
	case 0:
		return;

	case -EFAULT:
		/* if the command is OoO, we should resend the deallocate */
		eng->td_counters.misc.deallocation_error_cnt ++;
		break;

	case -ETIMEDOUT:
		eng->td_counters.misc.deallocation_timeout_cnt ++;
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
		/* if read-buffers are being tracked, and the user requested
		 * to not replay timedout deallocations, do so. */
		if (!td_eng_conf_var_get(eng, RETRY_TIMEDOUT_DEALLOCATES)) {
			return;
		}
#endif
		/* timedout deallocations can be retried */
		break;

	default:
		eng->td_counters.misc.deallocation_error_cnt ++;
		return;
	}

	/* Reschedule the deallocation of a read buffer for command that */
	/* failed, but only if we are still in a pre-termination state. */
	if (!td_state_can_accept_requests(eng))
		return;

	td_schedule_rdbuf_deallocation(eng, rd_bufid);
}

static void td_request_end_common(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	/* finalize a deallocation */
	td_complete_deallocation(tok, tok->result);

	/* update the latency */
	if (td_token_is_read(tok))
		td_eng_latency_end(&eng->td_tok_latency, tok,
				"hw-rd-latency",
				&eng->td_counters.read.hw);
	else if (td_token_is_write(tok))
		td_eng_latency_end(&eng->td_tok_latency, tok,
				"hw-wr-latency",
				&eng->td_counters.write.hw);
	else
		td_eng_latency_end(&eng->td_tok_latency, tok,
				"hw-ctrl-latency",
				&eng->td_counters.control.hw);


	/* allow a kernel caller to act on this completion */
	if (tok->ops.pre_completion_hook) {
		int rc = tok->ops.pre_completion_hook(tok);

		/* the pre-completion hook will cleanup */
		if (rc == TD_TOKEN_PRE_COMPLETION_STOLEN) {
			td_tok_event(tok, TD_TOK_EVENT_STOLEN, 0);
			return;
		}
	}

	td_tok_event(tok, TD_TOK_EVENT_END, (uint16_t)tok->result);

	td_eng_trace(eng, TR_TOKEN, "TD_ENG:end:cmd", tok->cmd_bytes[0]);

	/* finalize data xfer */
	td_request_end_common_bio(tok);
	td_request_end_common_ucmd(tok);

	/* release resources - this _MUST_ come after the end completions
	 * above, becuase UCMDs might touchup the resources in the token
	 * due to MCE-FREE fixups */
	td_free_all_buffers(eng, tok);
	td_free_token(eng, tok);
}

/* This can't be static, because it's a hacked up way to get to FW_PROBE */
/* TD HAL needs to get here, with out running an zillion deallocate
 * commands, because it can deallocate everything with a put-reg
 */
void td_request_end_deallocate(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	if (td_state_is_purging_read_buffers(eng)) {
		/* in the purge state, getting to a point where there are
		 * - no more deallocations pending and
		 * - no commands running,
		 * gets us to the FW_PROBE state */
		if (!td_pending_rdbuf_deallocations(eng)
				&& !td_all_active_tokens(eng)) {
			td_eng_hal_fw_handshake(eng);
		}
	}

	td_request_end_common(tok);
}

/* starting new tokens */

/** given a token prepared with a command, executes it in the device
 * @param eng - engine to operate on
 * @param tok - token to execute command on
 */
void td_engine_start_token(struct td_engine *eng, struct td_token *tok)
{
	int rc;
	struct td_token_list *tok_list;

	td_engine_exit_polling_loop(eng);

	/* let the control command know it's starting */
	if (unlikely (tok->host.ucmd))
		td_ucmd_starting(tok->host.ucmd);
	
	if (tok->sec_buddy && td_token_is_active(tok->sec_buddy) ) {
		/*
		 * If we have a sec_buddy active, don't have to do much,
		 * things have been done for him already.
		 */
		td_eng_trace(eng, TR_TOKEN, "TD:sec_buddy:active", tok->sec_buddy->tokid);
		td_token_set_default_op(tok, completion, td_request_eng_write);
	} else
	{
		/* handle writes differently than reads */
		if ((tok->host.bio) && td_bio_is_discard(tok->host.bio)) {
			td_request_start_trim(tok);
			td_token_set_default_op(tok, completion, td_request_eng_trim);
		} else if (td_token_is_write(tok)) {
			td_request_start_write(tok);
			td_token_set_default_op(tok, completion, td_request_eng_write);
		} else if (td_token_is_read(tok)) {
			td_request_start_read(tok);
			td_token_set_default_op(tok, completion, td_request_end_read);
		} else {
			/* otherwise it's a control message and no data is copied */
			td_token_set_default_op(tok, completion, td_request_end_common);
		}
		td_eng_trace(eng, TR_TOKEN, "TD:sec_buddy:enabled", !!tok->sec_buddy);
	}

	/* last modification before dispatching: set odd bit, free_rdbuf */
	tok->odd = !tok->odd;

	if (!TD_IS_RD_BUFID_VALID(tok->free_rd_bufid)
			&& !td_eng_conf_var_get(eng, INDEPENDENT_DEALLOCATE)) {
		tok->free_rd_bufid = (uint8_t)td_next_rdbuf_to_deallocate(eng);
td_eng_trace(eng, TR_RDBUF, "deallocate:pig:rdbuf", tok->free_rd_bufid);
td_eng_trace(eng, TR_RDBUF, "deallocate:pig:queued", td_pending_rdbuf_deallocations(eng));
	}

	td_eng_trace(eng, TR_BIO, "deallocate", tok->free_rd_bufid);
	td_eng_trace(eng, TR_RDBUF, "rdbuf:drv:free", tok->free_rd_bufid);

	/* construct command */
	rc = td_eng_hal_create_cmd(eng, tok);
	if (rc) {
		td_eng_trace(eng, TR_ERR, "BUG: td_eng_hal_create_cmd returned", rc);
		td_eng_err(eng, "create_cmd rc=%d\n", rc);
		goto failed_to_start;
	}
	if (tok->host.ucmd && tok->host.ucmd->locked) {
		/*
		 * Now we need to fixup for MCE-FREE, which needs knowledge of
		 * core buffers, even if locked
		 */
		td_cmd_t *tdcmd = (td_cmd_t *)tok->cmd_bytes;
		if (tdcmd->cmd.decode.to_host) {
			/* We need to know the corebuffer this is coming from */
			td_eng_debug(eng,
					"MCEFREE UCMD FIXUP: attaching locked core buffer %u\n",
					tdcmd->dst.bufid);
			tok->core_bufid = tdcmd->dst.bufid;
			td_start_token_core_usage(tok);
		}
	}


	/* expecting _create_cmd() call to set the status check function */
	WARN_ON(!tok->ops.status_check);

	/* schedule the command */
	tok->result = TD_TOK_RESULT_ACTIVE;

	if (td_token_is_write(tok)) {
		td_eng_trace(eng, TR_CMD, "write-cmd-start", tok->tokid);
	} else if (td_token_is_read(tok)) {
		td_eng_trace(eng, TR_CMD, "read-cmd-start", tok->tokid);
	} else {
		td_eng_trace(eng, TR_CMD, "cntl-cmd-start", tok->tokid);
	}

	/* when this token will timeout */
	td_reset_token_timeout(eng, tok);

	/* send command to device */
	rc = td_eng_hal_start_token(eng, tok);

	if (td_token_is_write(tok)) {
		/* in super early commit case, we can release the request if
		 * us and our sec_buddy are both active */
		if (tok->sec_buddy && td_token_is_active(tok->sec_buddy) ) {
			td_release_tok_bio(tok, 0);
			tok->sec_buddy->host.bio = NULL;
		}
		td_eng_trace(eng, TR_CMD, "write-cmd-end  ", tok->tokid);

		eng->loop_counters.started.writes ++;

	} else if (td_token_is_read(tok)) {
		td_eng_trace(eng, TR_CMD, "read-cmd-end  ", tok->tokid);

		eng->loop_counters.started.reads ++;

	} else {
		td_eng_trace(eng, TR_CMD, "cntl-cmd-end  ", tok->tokid);

		eng->loop_counters.started.control ++;
	}

	if (td_token_is_deallocate(tok)) {
		struct td_rdbuf *rdbuf;
		uint8_t freeing = tok->free_rd_bufid;

		WARN_ON(!TD_IS_RD_BUFID_VALID(freeing));
		rdbuf = &eng->td_rdbufs[freeing];
		rdbuf->rb_last_dealloc = td_get_cycles();

		/* used for fast completion of independent deallocates when
		 * the read buffer is used, the hardware status of this
		 * command will not need to be read */
		if (tok->independent_deallocate) {
			eng->td_active_rdbuf_deallocation_token[tok->free_rd_bufid]
				= tok->tokid;
		}
	}

	td_engine_update_token_sent_timestamps(eng, tok);

	if (unlikely (rc || td_eng_conf_var_get(eng, QUICK_WRBUF))) {
		/* release the write buffer right away */
		td_free_wr_buffer(eng, tok->wr_bufid);
		tok->wr_bufid = TD_INVALID_WR_BUFID;
	}

	if (likely(rc==0)) {
		tok->cmd_issue_count ++;

		/* track number of tokens w/o an update yet */
		td_token_awaits_update(eng, tok);

		/* inform tracking that this was sent */
		if (TD_IS_RD_BUFID_VALID(tok->free_rd_bufid))
			td_rdbuf_deallocation_sent(eng, tok->free_rd_bufid);
	} else {
		td_eng_err(eng, "start rc=%d\n", rc);
		goto failed_to_start;
	}

	/* TODO: td_trace_token(eng, tok); */

	tok_list = &eng->tok_pool[td_token_type(tok)].td_active_tokens;
	td_tokens_enqueue(tok_list, tok);
	if (tok->rmw)
		eng->td_active_rmw_tokens_count ++;

	/* passed all deallocation to the simulator */

	if (td_token_is_write(tok)) {
		td_counter_inc_in_flight(eng, tok, 1);
	}
	else if (td_token_is_read(tok)) {
		td_counter_inc_in_flight(eng, tok, 0);
	}
	else {
		eng->td_stats.control.req_active_cnt ++;
	}

	return;

failed_to_start:

	tok->result = TD_TOK_RESULT_FAIL_ABORT;

	td_request_end_common(tok);
	td_free_wr_buffers(eng, tok);
	td_end_token_core_usage(tok);
	td_free_core_buffer(eng, tok->core_bufid);
	tok->core_bufid = TD_INVALID_CORE_BUFID;
	tok->wr_bufid = TD_INVALID_WR_BUFID;
	tok->rd_bufid = TD_INVALID_RD_BUFID;
	td_free_token(eng, tok);

	return;
}


static int td_engine_io_begin_block(struct td_engine *eng, uint *max);
static unsigned td_engine_deallocate_rdbufs(struct td_engine *eng, uint *max);


void td_engine_io_begin(struct td_engine *eng)
{
	int prev;
	uint max = (uint)td_eng_conf_var_get(eng, MAX_START_BURST);
	uint total = 0;

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_TASK);
	td_engine_process_tasks(eng);
	td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_CMD);
#else
	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_CMD);
#endif

	/* send deallocates first */
	if (unlikely (td_pending_rdbuf_deallocations(eng)
			&& td_eng_conf_var_get(eng, INDEPENDENT_DEALLOCATE))) {
		/* There are rdbufs to deallocate, and each deallocation is
		 * to be done independently.  This means that that a
		 * DEALLOCATE command has prioirty over IO commands. */
		total += td_engine_deallocate_rdbufs(eng, &max);

		/* if we are purging read buffers now, don't bother with other
		 * commands */
		if (total && td_state_is_purging_read_buffers(eng))
			return;
	}

//warning should call td_io_begin_state_init() here and pass 'bs' to all begin functions

	/* first start any control messages */
	if (td_state_can_start_requests(eng)
			&& td_engine_queued_ucmds(eng))
		total += td_engine_io_begin_ucmd(eng, &max);

	/* now send IO commands */
	if (td_engine_queued_bios(eng) ) {
		unsigned to_tok_cnt = td_all_timedout_tokens(eng);
		if (unlikely (to_tok_cnt >= td_eng_conf_var_get(eng, TIMEDOUT_CNT_THRESHOLD))) {
			td_eng_crit(eng, "Error: %u/%llu tokens timedout.\n",
					to_tok_cnt,
					td_eng_conf_var_get(eng, TOKENS));
			td_eng_err(eng, " - active_tokens %u\n", td_all_active_tokens(eng));
			td_eng_err(eng, " - available_tokens %u\n", td_all_available_tokens(eng));
			td_eng_err(eng, " - timedout_tokens %u\n", to_tok_cnt);
			td_terminate_all_outstanding_bios(eng, 0);

		} else if (td_state_can_start_io_requests(eng)) {
			/* then start block requests */
			total += td_engine_io_begin_block(eng, &max);
		}
	}

	/* follow up deallocations */
	if (td_pending_rdbuf_deallocations(eng)
			&& (!total
				|| td_state_is_purging_read_buffers(eng)
				|| !td_engine_queued_commands(eng))) {
		/* There are rdbufs to deallocate, but there is no more work pending.
		 * We have to create DEALLOCATE commands to flush them out. */
		total += td_engine_deallocate_rdbufs(eng, &max);
	}

	td_switch_task(td_engine_devgroup(eng), prev);

	td_histogram_update(&eng->hist_started, total);
	td_histogram_update(&eng->hist_post_start_active, td_active_tokens(eng));
	td_histogram_update(&eng->hist_post_start_queued, td_engine_queued_bios(eng));
}

static int td_engine_io_begin_ucmd(struct td_engine *eng, uint *max)
{
	struct td_token *tok;
	int tok_avail, core_avail;
	unsigned total = 0;
	struct td_ucmd *ucmd;
	struct td_ioctl_device_cmd_pt *ioctl;
	enum td_token_type tt;

	/* starting new work is limited by number of available tokens on a
	 * channel */
	tok_avail = td_all_available_tokens(eng);
	td_eng_trace(eng, TR_LOOP, "io:ucmd:tokens", tok_avail);
	if (!tok_avail)
		return 0;

	/* the core has a limited number of buffers, we cannot start more work
	 * than what's available */
	core_avail = td_available_core_buffers(eng);
	td_eng_trace(eng, TR_LOOP, "io:ucmd:cores", core_avail);

	while (tok_avail > 0) {

		if (!*max)
			break;

		ucmd = td_pop_ucmd(eng, core_avail);
		if (!ucmd)
			break;

		ioctl = &ucmd->ioctl;
		tt = td_eng_hal_cmd_to_token_type(eng, &ioctl->cmd);

		if(unlikely(ucmd->locked)) {
			tok = td_alloc_token_when_locked(eng, tt,
					ioctl->data_len_to_device,
					ioctl->data_len_from_device);

			if (unlikely(!tok))
				goto failed_to_get_token;
		}
		else {
			/* allocate a token with buffers */
			tok = td_alloc_token_with_buffers(eng, tt,
					ioctl->data_len_to_device,
					ioctl->data_len_from_device);
			if (unlikely(!tok))
				goto failed_to_get_token;

			core_avail --;
		}
		tok_avail --;

		/* set magic flags as needed */
		tok->magic_flags = (uint8_t)td_eng_conf_var_get(eng, MAGIC_FLAGS);

		/* fill in data access specifics */
		tok->host.ucmd = ucmd;
		tok->host_buf_virt = ucmd->data_virt;
		tok->copy_ops = eng->td_virt_copy_ops;
		tok->hw = ucmd->hw_cmd;

		/* send it to the hardware */
		td_engine_start_token(eng, tok);

		/* done with this one */
		tok = NULL;
		total ++;
		(*max) --;

		if (core_avail<0)
			if (td_ratelimit())
				pr_warning("miscounted CORE buffers "
						"(%d)\n",
						core_avail);
	}

	return total;

failed_to_get_token:
#if 0
	/*
	 * With monitor continuously sending them now, and with MCE-FREE
	 * calling us when there are "tokens", but not the write type, we hit
	 * this case regularly.
	 */
	if (td_ratelimit())
		td_eng_warn(eng, "failed to allocate token resources for ucmd\n");
#endif

	/* This has to go back to the head of the queue */
	if (ucmd)
		td_push_ucmd(eng, ucmd);

	return total;
}

static inline int td_io_begin_state_init(struct td_engine *eng,
		struct td_io_begin_state *bs, uint max)
{
	memset (bs, 0, sizeof(*bs));

	/* we are waiting for a sequence number that's very old, cannot start
	 * a new one */
	if (unlikely (!td_engine_sequence_limit_check(eng)))
		return -EBUSY;

	/* number of tokens is limited based on availability and artificial cap */
	bs->tok_avail = min_t(uint, td_available_tokens(eng, TD_TOK_FOR_FW), max);
#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
	/* also cap based on how much room we have in the noupdate range */
	bs->tok_avail = min_t(uint, bs->tok_avail, td_noupdate_headroom(eng));
#endif
	if (unlikely (!bs->tok_avail))
		return -ENOMEM;

	/* the core has a limited number of buffers, we cannot start more work
	 * than what's available */
	bs->core_avail = td_available_core_buffers(eng);
	if (unlikely (!bs->core_avail))
		return -ENOMEM;

	/* writes are also limited by write entry points */
	bs->wr_avail = td_available_write_buffers(eng);

	/* if we have no reads (ie only writes) and there are no write
	 * buffers, we cannot continue */
	if (unlikely (!td_engine_queued_bio_reads(eng) && !bs->wr_avail))
		return -ENOMEM;
	
#if 0
	/* on a per channel basis it doesn't make sense to start more than one
	 * burst at a time; doing so would cause starvation of the other
	 * channels */
	if (unlikely (td_eng_conf_var_get(eng, CHANNELS) > 1)) {
		int wr_max = min_t(uint, td_eng_conf_var_get(eng, LUNS), TD_WR_BURST);
		if (unlikely (bs->wr_avail > wr_max))
			bs->wr_avail = wr_max;
	}
#endif
	return 0;
}


static int td_engine_rmw_write_completion(struct td_token*);
static int td_engine_rmw_read_completion(struct td_token*);

/*
 * handle read-modify write
 *
 * This is a special token that will start off reading 4k into a bounce
 * buffer, update the data read by what's in the bio, and write the page back
 */
static struct td_token *td_engine_construct_rmw_token_for_bio(struct td_engine *eng,
		struct td_io_begin_state *bs)
{
#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	uint wr_bufid;
#endif
	struct td_token *tok;

	/* Check if too many other RMW are in progress, optimized for TD_RMW_INFLIGHT == 1 */
	if (unlikely(td_eng_conf_var_get(eng, COLLISION_CHECK) == 0
				&& eng->td_rmw_inflight > (TD_RMW_INFLIGHT_LIMIT-1))) {
		bio_list_add(&eng->td_rmw_bios, bs->bio);
		bs->bio = NULL;
		return NULL;
	}

#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	/* best thing right now would be a free write buffer */
	wr_bufid = td_alloc_wr_buffer(eng);
	if (unlikely (!TD_IS_WR_BUFID_VALID(wr_bufid)))
		goto no_wr_buffers;
#endif

	/* allocate a token with a page to read into */
	tok = td_alloc_token_with_host_page(eng, TD_TOK_FOR_FW,
			0, (uint)td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));
	if (unlikely(!tok))
		goto no_token;

	/* update resources remaining */
	bs->core_avail --;
	bs->tok_avail --;
#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	bs->wr_avail --;
#endif

	/* set magic flags as needed */
	tok->magic_flags = (uint8_t)td_eng_conf_var_get(eng, MAGIC_FLAGS);

	/* mark as read-modify-write */
	tok->rmw = 1;

	/* always reading the whole bio to the base of the page */
	tok->host.bio = bs->bio;

	/* assign the token LBA and offset in LBA */
	td_token_assign_lba_and_offset_from_bio(tok, bs->bio);
	tok->lba_ofs = 0;

#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	/* already acquired a write buffer */
	tok->wr_bufid = (uint8_t)wr_bufid;
#endif

	/* update latency if needed */
	td_eng_latency_start(&eng->td_tok_latency, tok);

	/* the completion of this read will be done via a special handler */
	tok->ops.pre_completion_hook = td_engine_rmw_read_completion;

	/* increment stats */
	eng->td_counters.misc.read_modify_write_cnt ++;

	return tok;

no_token:
#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	td_free_wr_buffer(eng, (uint8_t)wr_bufid);
no_wr_buffers:
#endif
	return NULL;
}

/*
 * completes a wrtie from RMW operation
 *
 * At this point, the write is done, all we need to do is possibly
 * unblock the side-queued RMW bios
 */
static int td_engine_rmw_write_completion(struct td_token *tok)
{
	td_bio_ref bio;
	struct td_engine *eng = td_token_engine(tok);

	eng->td_rmw_inflight--;

	bio = bio_list_pop(&eng->td_rmw_bios);
	if (unlikely (bio))
		td_engine_push_bio(eng, bio);

	return TD_TOKEN_PRE_COMPLETION_DONE;
}

/*
 * completes a read from RMW operation
 *
 * At this point the read finished, 4k data is stored in the virt buffer.
 * This function copies the bio data into the buffer at the right offset, and
 * starts the write of the 4k page.
 */
static int td_engine_rmw_read_completion(struct td_token *tok)
{
#ifndef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	uint wr_bufid;
#endif
	uint lba_ofs, data_size;
	struct td_engine *eng = td_token_engine(tok);

	lba_ofs = (uint)td_bio_lba_offset(eng, tok->host.bio);
	data_size = td_bio_get_byte_size(tok->host.bio);

	WARN_ON(lba_ofs + data_size > td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));

//warning this only works on MD for now
	/* copy the bio data over what was read */
	eng->td_bio_copy_ops.host_to_dev(
			tok, PTR_OFS(tok->host_buf_virt, lba_ofs), NULL);

#ifdef CONFIG_TERADIMM_RMW_EARLY_WRBUF
	/* already acquired a write buffer earlier */
	WARN_ON(!TD_IS_WR_BUFID_VALID(tok->wr_bufid));
#else
	/* best thing right now would be a free write buffer */
	wr_bufid = td_alloc_wr_buffer(eng);
	if (!TD_IS_WR_BUFID_VALID(wr_bufid))
		goto no_wr_buffers;

	tok->wr_bufid = (uint8_t)wr_bufid;
#endif

	/* convert it into a write write */
	tok->len_dev_to_host = 0;
	tok->len_host_to_dev = (uint16_t)td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);

	/* hook up the post-write completion */
	tok->ops.pre_completion_hook = td_engine_rmw_write_completion;

	/* prepare for sending this command again */
	tok->ops.completion = NULL;
	tok->retry_count = 0;
	tok->reset_count = 0;
	memset(tok->cmd_bytes, 0, sizeof(tok->cmd_bytes));

	td_eng_trace(eng, TR_BIO, "BIO:continue:bio", (uint64_t)tok->host.bio);

	/* send it to the hardware */
	td_engine_start_token(eng, tok);

	return TD_TOKEN_PRE_COMPLETION_STOLEN;

#ifndef CONFIG_TERADIMM_RMW_EARLY_WRBUF
no_wr_buffers:
	/* since there are no write buffers at this time, the write will have
	 * to wait until one is available */

	td_eng_err(eng, "RMW couldn't get a write buffer "
			"and this is currenlty unhandled\n");
	tok->result = -ENXIO;
	return TD_TOKEN_PRE_COMPLETION_DONE;
#endif
}


static int td_engine_SEC_completion(struct td_token*);
/*
 * Handle SEC writes
 *
 * SEC requires that we write every command twice, so that we have at least
 * one good copy if there was a bit error somewhere.  This funciton allocates
 * 2 tokens, the 2nd as a "sec_buddy" to the 1st token.
 *
 * The 2nd token will have it's own resources (token, core buffer, wep), but
 * the associated data with it will all be the same as the 1st.
 */
static struct td_token *td_engine_construct_token_for_SEC_bio(struct td_engine *eng,
		struct td_io_begin_state *bs, int use_extra_wep)
{
	struct td_token *tok1, *tok2;
	uint write_size=0, read_size=0;

	/* bio requests are either write or read, discards are special writes */
	if (unlikely(td_bio_is_discard(bs->bio)))
		write_size = 512;
	else if (td_bio_is_write(bs->bio) )
		write_size = td_bio_get_byte_size(bs->bio);
	else
		read_size = td_bio_get_byte_size(bs->bio);

	/* allocate a token with buffers */
	tok1 = td_alloc_token_with_buffers(eng, TD_TOK_FOR_FW, write_size, read_size);
	if (unlikely(!tok1))
		goto error_alloc_tok1;

	tok2 = td_alloc_token_with_buffers(eng, TD_TOK_FOR_FW, write_size, read_size);
	if (unlikely(!tok2))
		goto error_alloc_tok2;

	if (use_extra_wep) {
		tok1->extra_wr_bufid = td_alloc_wr_buffer(eng);
		if (!TD_IS_WR_BUFID_VALID(tok1->extra_wr_bufid))
			goto error_alloc_extra_wep;
	}

	/* update resources remaining */
	bs->core_avail -= 2;
	bs->tok_avail -= 2;
	if (write_size)
		bs->wr_avail -= 2;
	if (use_extra_wep)
		bs->wr_avail --;

	/* set magic flags as needed */
	tok2->magic_flags = tok1->magic_flags = (uint8_t)td_eng_conf_var_get(eng, MAGIC_FLAGS);

	/* fill in the token bits */
	tok2->host.bio = tok1->host.bio = bs->bio;

	/* assign the token LBA and offset in LBA */
	td_token_assign_lba_and_offset_from_bio(tok1, bs->bio);
	tok2->to_ssd     = tok1->to_ssd;
	tok2->port     = tok1->port;
	tok2->lba     = tok1->lba;
	tok2->lba_ofs = tok1->lba_ofs;

	/* assign the copy handlers */
	/* discards do not have data, we construct the 512 byte page later.*/
#warning THIS DISCARD HAS TO GO
	if (unlikely(td_bio_is_discard(bs->bio)))
		tok2->copy_ops = tok1->copy_ops = td_token_copy_ops_kbio_512B;
	else
		tok2->copy_ops = tok1->copy_ops = eng->td_bio_copy_ops;

	/* support early commit, no early commit function needed, because early
	 * commit will * happen automatically in start_one_bio */
	
	/* the completion of this read will be done via a special handler */
	
	tok2->ops.pre_completion_hook =
		tok1->ops.pre_completion_hook = td_engine_SEC_completion;

	/* Tie them together */
	tok1->sec_buddy = tok2;
	tok2->sec_buddy = tok1;

	/* update latency if needed */
	td_eng_latency_start(&eng->td_tok_latency, tok1);

	return tok1;

error_alloc_extra_wep:
	td_free_all_buffers(eng, tok2);
	td_free_token(eng, tok2);
error_alloc_tok2:
	td_free_all_buffers(eng, tok1);
	td_free_token(eng, tok1);
error_alloc_tok1:
	return NULL;
}

static int td_engine_SEC_completion(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	/* First, let's free our resources */
	td_free_all_buffers(eng, tok);

	if (WARN_ON(tok->host.bio) ) {
		td_eng_err(eng, "SEC ERROR: Completion shouldn't have a host.bio on sec "
				"buddies %u, %u\n",
				tok->tokid, tok->sec_buddy->tokid);
	}

	if (tok->result != TD_TOK_RESULT_OK ) {
		td_eng_debug(eng, "SEC: error on token %d buddy %d (%s)\n",
				tok->tokid, tok->sec_buddy->tokid,
				tok->sec_buddy->sec_buddy ? "active" : "done");
	}

	/* Is our buddy still running */
	if (tok->sec_buddy->sec_buddy) {
		tok->sec_buddy = NULL;
		/* We keep the token around so our buddy can examine this one */
		return TD_TOKEN_PRE_COMPLETION_STOLEN;
	}
	
	/* Only one of us had to succeed we *BOTH* have errors? */
	if (unlikely(tok->result == TD_TOK_RESULT_OK) ||
			unlikely(tok->sec_buddy->result == TD_TOK_RESULT_OK) ) {
		/* We need to free our buddy, who was not freed last time */
		td_free_token(eng, tok->sec_buddy);

		tok->sec_buddy = NULL;
		return TD_TOKEN_PRE_COMPLETION_DONE;
	}

	/* If we're already dead, we're not responsible initiating the death */
	if (! td_run_state_check(eng, DEAD) ) {
		td_eng_err(eng, "SEC: fatal error on token %d buddy %d\n",
				tok->tokid, tok->sec_buddy->tokid);
		td_token_error_dump(tok, "SEC TOK");

		/* If we both had errors, we are dead.... */
		td_run_state_enter(eng, TERMINATING);
	}
	return TD_TOKEN_PRE_COMPLETION_DONE;
}



static struct td_token *td_engine_construct_token_for_bio(struct td_engine *eng,
		struct td_io_begin_state *bs)
{
	struct td_token *tok;
	uint write_size=0, read_size=0;
	td_bio_ref bio = bs->bio;

	/* bio requests are either write or read, discards are special writes */
	if (unlikely(td_bio_is_discard(bio)))
		write_size = 512;
	else if (td_bio_is_write(bio)) {
		int weps_needed = 0;
		/*
		 * If we can do SEC, we'll do it...
		 * These values should be tunable
		 */
		switch (td_bio_flags_ref(bio)->commit_level) {
		case TD_SUPER_EARLY_COMMIT:
			/* this is the original SEC mode that uses 2 WEPs */
			weps_needed = 2;
			break;

		case TD_TRIPLE_SEC:
			/* enhanced reliability from a third WEP */
			weps_needed = 3;
			break;

		default:
			/* everything else is normal */
			break;
		}

		if (weps_needed && (bs->tok_avail > 2)
				&& (bs->wr_avail > weps_needed)
				&& (bs->core_avail > 2) ) {
			td_eng_trace(eng, TR_BIO, "BIO:construct:sec",
					bs->tok_avail << 16 
					| bs->wr_avail << 8 
					| bs->core_avail);
			return td_engine_construct_token_for_SEC_bio(eng, bs,
					weps_needed == 3);
		}

		write_size = td_bio_get_byte_size(bio);

	} else
		read_size = td_bio_get_byte_size(bio);

	/* allocate a token with buffers */
	tok = td_alloc_token_with_buffers(eng, TD_TOK_FOR_FW, write_size, read_size);
	if (unlikely(!tok))
		return NULL;

	/* update resources remaining */
	bs->core_avail --;
	bs->tok_avail --;
	if (write_size)
		bs->wr_avail --;

	/* set magic flags as needed */
	tok->magic_flags = (uint8_t)td_eng_conf_var_get(eng, MAGIC_FLAGS);

	/* fill in the token bits */
	tok->host.bio = bio;

	/* assign the token LBA and offset in LBA */
	td_token_assign_lba_and_offset_from_bio(tok, bio);

	/* assign the copy handlers */
	/* discards do not have data, we construct the 512 byte page later.*/
	if (unlikely(td_bio_is_discard(bio)))
		tok->copy_ops = td_token_copy_ops_kbio_512B;
	else
		tok->copy_ops = eng->td_bio_copy_ops;

	/* support early commit */
	if (write_size)
		tok->ops.early_commit = td_release_tok_bio;

	/* update latency if needed */
	td_eng_latency_start(&eng->td_tok_latency, tok);

	return tok;
}

static int td_engine_io_begin_one_bio(struct td_engine *eng,
		struct td_io_begin_state *bs)
{
	struct td_token *tok;

	WARN_ON(!bs->bio);

	/*
	 * Stamp our commit state info on this BIO right now
	 */
	td_bio_flags_ref(bs->bio)->commit_level = (uint8_t)td_eng_conf_var_get(eng, EARLY_COMMIT);

	if (unlikely (td_bio_needs_rmw(eng, bs->bio)))
		tok = td_engine_construct_rmw_token_for_bio(eng, bs);
	else
		tok = td_engine_construct_token_for_bio(eng, bs);

	if (unlikely(!tok))
		goto failed_to_get_token;

	td_eng_trace(eng, TR_BIO, "BIO:start:bio ", (uint64_t)bs->bio);
	td_eng_trace(eng, TR_BIO, "BIO:start:port", tok->port);
	td_eng_trace(eng, TR_BIO, "BIO:start:lba ", tok->lba);
	td_eng_trace(eng, TR_BIO, "BIO:start:size", td_bio_get_byte_size(bs->bio));


	/* send it to the hardware */
	td_engine_start_token(eng, tok);
	bs->tok_started ++;

	if (tok->sec_buddy) {
		td_engine_start_token(eng, tok->sec_buddy);
		bs->tok_started ++;
	}

	/* don't accidentally access the bio any more */
	bs->bio = NULL;

	return 0;

failed_to_get_token:
	if (bs->bio)
		td_engine_push_bio(eng, bs->bio);
	bs->bio = NULL;
	return -EIO;
}

/* --- top level handling of block requests --- */

static int td_engine_io_begin_block(struct td_engine *eng, uint *max)
{
	struct bio_list bios;
	unsigned count;
	struct td_io_begin_state bs;

	if (td_io_begin_state_init(eng, &bs, *max))
		/* no resources */
		return 0;

	while (bs.tok_avail>0 && bs.core_avail>0) {

		bio_list_init(&bios);


		count = td_engine_get_bios(eng, &bios, &bs);

		if (unlikely(!count)) {
			WARN_ON(!bio_list_empty(&bios));
			goto bail;
		}

		while((likely(bs.bio = bio_list_pop(&bios))))
		{
			int rc;

			if (unlikely(!td_bio_get_byte_size(bs.bio))) {
				/* this was a zero sized IO */
				td_eng_trace(eng, TR_TOKEN, "zero-sized-bio-skipped",
						0);
				td_bio_endio(eng, bs.bio, 0, 0);
				bs.bio = NULL;
				continue;
			}

#ifdef CONFIG_TERADIMM_ABSOLUTELY_NO_READS
			if (td_bio_is_write(bs.bio)) {
				td_eng_trace(eng, TR_TOKEN, "HACK:skip-read:bio", (long)bs.bio);
				td_bio_endio(eng, bs.bio, 0);
				bs.bio = NULL;
				continue;
			}
#endif

			rc = td_engine_io_begin_one_bio(eng, &bs);
			if (unlikely (rc<0))
				goto failed_to_get_token;
		}

		if (bs.wr_avail<0)
			if (td_ratelimit())
				pr_warning("miscounted WR buffers "
					"(%d)\n",
					bs.wr_avail);
		if (bs.core_avail<0)
			if (td_ratelimit())
				pr_warning("miscounted CORE buffers "
					"(%d)\n",
					bs.core_avail);
	}

bail:
	(*max) -= min_t(uint, *max, bs.tok_started);
	return bs.tok_started;

failed_to_get_token:
#if 0
	if (td_ratelimit())
		td_eng_warn(eng, "failed to allocate token for block\n");
#endif

	/* requeue the requests at the head of the bio queue */
	td_engine_push_bio_list(eng, &bios);
	if (bs.bio)
		td_engine_push_bio(eng, bs.bio);
	bs.bio = NULL;


	return bs.tok_started;
}

static void td_engine_io_complete_list(struct td_engine *eng,
		struct list_head *token_list)
{
	unsigned finished = 0;
	struct td_token *tok;

	if (list_empty(token_list))
		return;

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

	do {

		tok = list_entry(token_list->next,
				struct td_token, link);
		list_del(&tok->link);

		if (td_token_is_write(tok)) {
			td_counter_dec_in_flight(eng, tok, 1);
			eng->loop_counters.completed.writes ++;

		} else if (td_token_is_read(tok)) {

			td_counter_dec_in_flight(eng, tok, 0);
			eng->loop_counters.completed.reads ++;
		} else {
			eng->td_stats.control.req_active_cnt --;
			eng->loop_counters.completed.control ++;
		}

		td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_DATA);

		tok->ops.completion(tok);

		td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_POLL);

		finished++;

		/* speed up early deallocations */

		if (td_eng_conf_var_get(eng, EARLY_DEALLOCATE)
				&& !(finished % td_eng_conf_var_get(eng, EARLY_DEALLOCATE))
		   ) {
			if (td_pending_rdbuf_deallocations(eng)
					&& td_eng_conf_var_get(eng, INDEPENDENT_DEALLOCATE)) {
				uint max = (uint)td_eng_conf_var_get(eng, MAX_START_BURST);
				td_engine_deallocate_rdbufs(eng, &max);
			}
		}

		/* start up more now */
		if (td_eng_conf_var_get(eng, EARLY_START)
				&& !(finished % td_eng_conf_var_get(eng, EARLY_START)))
			td_engine_io_begin(eng);

	} while (!list_empty(token_list));
}

static void td_failed_retry_sequence_advancing_pre_completion(struct td_token *tok)
{
	/* force the original bio to die */
	tok->result = TD_TOK_RESULT_FAIL_ABORT;
	td_request_end_common(tok);
}

/* called when td_engine_io_repeat_list() found a token that was retried too
 * many times */
static void td_engine_handle_repeat_exhausted(struct td_engine *eng,
		struct td_token *tok, // token that had retry problems
		struct list_head *complete_list)   // failed ones get put on this list
{
	int rc;
	struct td_token_list *tok_list;

	/* If it's not a to_ssd command, it's not sequenced */
	if (unlikely (!tok->to_ssd) )
		goto error_out;

	/* And we don't advannce on sec_budy failures, because of our buddy */
	if (likely (tok->sec_buddy) )
		goto error_out;

	td_token_error_dump(tok, "ADVANCE");

	if (tok->result == TD_TOK_RESULT_TIMEOUT) {
		/* if there was a write buffer allocated, release it */
		/* TODO: What if it didn't reach queued? */
		td_free_wr_buffer(eng, tok->wr_bufid);
	} else {
		/* There was an error, so FW isn't using these buffers */
		td_free_all_buffers(eng, tok);
	}

	td_eng_trace(eng, TR_TOKEN, "forceful:advance:tok  ", tok->tokid);

	/* construct a FW command to advance the sequence */
	rc = td_cmd_gen_seq_advance(eng, tok);
	if (rc)
		goto error_out;

	/* flip the bit */
	td_eng_hal_reverse_cmd_polarity(eng, tok);

	td_eng_info(eng, "Forcing sequence advance tok %u seq %llu cmd %016llx\n",
			tok->tokid, tok->cmd_seq, tok->cmd_bytes[0]);
	rc = td_eng_hal_start_token(eng, tok);
	if (rc)
		/* failed to restart, really complete */
		goto error_out;

	/* count a token that's resent */
	eng->td_counters.token.forced_seq_advance ++;

	/* it restarted, so add it to the active list */
	tok->result = TD_TOK_RESULT_ACTIVE;

	/* don't do normal completion */
	tok->ops.completion = td_failed_retry_sequence_advancing_pre_completion;

	/* we are active again */
	tok_list = &eng->tok_pool[td_token_type(tok)].td_active_tokens;
	td_tokens_enqueue(tok_list, tok);

	/* restart the counter */
	td_reset_token_timeout(eng, tok);

	return;

error_out:
	td_token_error_dump(tok, "FAILED");
	/* over limit or error, stop it now */
	tok->result = TD_TOK_RESULT_FAIL_ABORT;
	list_add_tail(&tok->link, complete_list);
}

static void td_engine_io_repeat_list(struct td_engine *eng,
		struct list_head *repeat_list,     // tokens that need repeats
		struct list_head *complete_list)   // failed ones get put on this list
{
	struct td_token *tok;
	struct td_token_list *tok_list;

	if (list_empty(repeat_list))
		return;

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

	/* if it's not possible to reissue, abort all */
	if (unlikely (!td_state_can_start_io_requests(eng)
			&& !td_state_is_purging_read_buffers(eng))) {
		td_engine_io_abort_tok_list(eng, repeat_list, complete_list);
		return;
	}

	do {
		int rc;

		tok = list_entry(repeat_list->next,
				struct td_token, link);
		list_del(&tok->link);

		/*
		* If we are a SEC token, we won't do any retries once our
		* buddy is safe in HW (or done)
		*/
		if (tok->sec_buddy) {
			if ( tok->sec_buddy->result == TD_TOK_RESULT_OK
					|| tok->sec_buddy->safe_in_hw) {
				if (td_eng_conf_var_get(eng, VERBOSE_TOK_FAILURES))
						td_token_error_dump(tok, "SUPPRESS");
				tok->result = TD_TOK_RESULT_FAIL_ABORT;
				list_add_tail(&tok->link, complete_list);
				eng->td_counters.token.sec_ignore_retry ++;
				continue;
			}
		}
		if (! td_eng_hal_can_retry(eng, tok) ) {
			td_engine_handle_repeat_exhausted(eng, tok, complete_list);
			continue;
		}

		td_eng_trace(eng, TR_TOKEN, "retry:cnt                ",
				tok->retry_count);


		/* this is a special replay due to a timeout after an OOO
		 * error was observed */
		if (tok->ooo_replay) {
			/* clear the ooo_replay flag so we don't come here again */
			tok->ooo_replay = 0;

			rc = td_replay_command_on_ooo_token(tok, tok->last_status);
			if (rc) {
				/* this was replayed via OOO replay */
				goto token_is_active_again;
			}
		}

		/* when replaying a write, write the data again */
		if (td_token_is_write(tok) ) {
			if (tok->host.bio || tok->host.ucmd) {
				/* we have the data, or it's a ucmd */
				td_eng_trace(eng, TR_TOKEN, "retry:data               ",
						tok->tokid);
				td_request_start_write(tok);
			} else {
				/* we have to rely on the data in the WEP */
				td_eng_trace(eng, TR_TOKEN, "refresh:data             ",
						tok->tokid);
				td_eng_hal_refresh_wep(eng, tok);
			}

			/* account for replays while the buddy had replays */
			if (tok->sec_buddy && tok->sec_buddy->cmd_issue_count > 1)
				eng->td_counters.token.double_sec_replay ++;
		}

		/* command had checksum failure, have the hardware
		 * see the repeat, the odd bit is flipped */
		td_eng_hal_reverse_cmd_polarity(eng, tok);

		td_eng_debug(eng, "Retrying tok %u cmd %016llx\n",
				tok->tokid, tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "retry:cmd                ", tok->tokid);
		rc = td_eng_hal_start_token(eng, tok);
		if (rc) {
			/* failed to restart, really complete */
			tok->result = rc;
			list_add_tail(&tok->link, complete_list);
			continue;
		}

token_is_active_again:
		/* count a token that's resent */
		eng->td_counters.token.retries_cnt ++;

		/* it restarted, so add it to the active list */
		tok->result = TD_TOK_RESULT_ACTIVE;

		tok_list = &eng->tok_pool[td_token_type(tok)].td_active_tokens;
		td_tokens_enqueue(tok_list, tok);

		/* restart the counter */
		td_reset_token_timeout(eng, tok);
	} while (!list_empty(repeat_list));
}

void td_engine_io_complete(struct td_engine *eng, enum td_token_type tt)
{
	struct td_token *tok, *nxt;
	struct td_token_pool *tok_pool;
#ifdef CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
	struct list_head write_completed;
#endif
	struct list_head completed;
	struct list_head repeatit;
	struct list_head timedout;
	uint64_t oldest_seq;
	int prev;
	int had_timeouts = 0;

	tok_pool = &eng->tok_pool[tt];

	/* starting polling now */
	td_engine_enter_polling_loop(eng);

	prev = td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_POLL);

	td_eng_trace(eng, TR_STATUS, "parse-status-start",
			td_all_active_tokens(eng));

#ifdef CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
	INIT_LIST_HEAD(&write_completed);
#endif
	INIT_LIST_HEAD(&completed);
	INIT_LIST_HEAD(&repeatit);
	INIT_LIST_HEAD(&timedout);

	td_active_tokens_lock(eng);
	oldest_seq = -1ULL;
	for_each_token_list_token(tok, nxt, &tok_pool->td_active_tokens) {

		if (tok->cmd_seq && tok->cmd_seq < oldest_seq)
			oldest_seq = tok->cmd_seq;

		if (! tok->ops.status_check(tok))
			continue;

		/* remove from active list */
		__td_tokens_del(&tok_pool->td_active_tokens, tok);

		switch (tok->result) {
		case TD_TOK_RESULT_OK:
			/* successful completion of this token */

#ifdef CONFIG_TERADIMM_MEGADIMM
			/* old way of completing resets (MegaDIMM) */
			if (td_token_is_reset_cmd(tok) && tok->needs_retry) {
				/* a reset command completed, we can now
				 * resume the original command */
				eng->td_counters.token.recovered_cnt ++;
				tok->reset_count = 0;
				tok->needs_retry = 0;
				td_eng_trace(eng, TR_TOKEN, "reset:done     ",
						tok->tokid);
				if (td_eng_conf_var_get(eng, TIMEOUT_RETRY)) {
					/* retry things that previously timed out */
					list_add_tail(&tok->link, &repeatit);
				} else {
					/* timeouts are treated as errors, after reset */
					tok->result = TD_TOK_RESULT_FAIL_ABORT;
					list_add_tail(&tok->link, &completed);
				}
				break;
			}
#endif

			/* add to (temporary) completed list */
#ifdef CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
			/* if completing writes first, put them on a separate list
			 * this prioritizes writes, and keeps their latency lower */
			if (td_token_is_write(tok)) {
				list_add_tail(&tok->link, &write_completed);
				break;
			}
#endif
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
			/* if mcefree is on, reads don't always have their read
			 * buffer known; these will go through a matching process
			 * before they show up on the completed list */
			if (td_token_is_read(tok)
					&& !TD_IS_RD_BUFID_VALID(tok->rd_bufid)) {
				td_engine_stage_early_read(eng, tok);
				break;
			}
#endif
			/* everything else is completed now */
			list_add_tail(&tok->link, &completed);
			break;

#if 0
		case -EFAULT:
			if(OoO == 0)
				OoO = tok->cmd_seq;
			else if (OoO > tok->cmd_seq);
				OoO = tok->cmd_seq;

			/* Fall-through expected.*/
		case -EAGAIN:
#endif
		case TD_TOK_RESULT_FAIL_CAN_RETRY:
			/* repeat the command, after we release the lock */

			/* the deallocation, if any is not repeated */
			td_complete_deallocation(tok, tok->result);
			list_add_tail(&tok->link, &repeatit);
			break;

		case TD_TOK_RESULT_TIMEOUT:
			/* this is a timeout, we need to reset the command */
			had_timeouts++;

			/* the deallocation, if any is not repeated */

			td_complete_deallocation(tok, tok->result);

			if (td_token_is_read(tok))
				td_seen_read_timeout(eng, tok);

			if (likely(!td_eng_conf_var_get(eng, HALT_ON_TIMEOUT)
					|| td_eng_conf_var_get(eng, HALT_ON_TIMEOUT) > 1)) {
				if (td_eng_conf_var_get(eng, HALT_ON_TIMEOUT) > 1) {
					td_eng_conf_var_dec(eng, HALT_ON_TIMEOUT);
					td_eng_err(eng, "TIMEOUTS REMAINING: %llu\n",
						td_eng_conf_var_get(eng, HALT_ON_TIMEOUT));
				}

				/* normal operation */
				list_add_tail(&tok->link, &timedout);
				break;
			}
			if (tok->quick_n_quiet) {
				td_eng_warn(eng, "suppressing HALT on expected tiemout\n");
				list_add_tail(&tok->link, &timedout);
				break;
			}

			/* otherwise, kill the device */
			td_eng_err(eng, "force DEAD after timout "
					"(tok=%d, cmd[0]=%16llx)\n",
					tok->tokid, tok->cmd_bytes[0]);

			td_run_state_enter(eng, TERMINATING);

			/* mark this token for completion */
			list_add_tail(&tok->link, &completed);

			/* mark to-be-repeated tokens for completion */
			td_engine_io_abort_tok_list(eng, &repeatit, &completed);

			/* mark to-be-reset tokens for completion */
			td_engine_io_abort_tok_list(eng, &timedout, &completed);
			goto skip_retries;

		case TD_TOK_RESULT_FAIL_ABORT_HARD:
			/* Set the result to the correct return code */
			tok->result = TD_TOK_RESULT_FAIL_ABORT;

			/* this is a failure, just complete it now */
			list_add_tail(&tok->link, &completed);

			/* kill the device */
			td_eng_err(eng, "force DEAD after hard failure"
					"(tok=%d, cmd[0]=%16llx)\n",
					tok->tokid, tok->cmd_bytes[0]);

			td_token_error_dump(tok, "END CMD");

			td_run_state_enter(eng, TERMINATING);

			/* mark to-be-repeated tokens for completion */
			td_engine_io_abort_tok_list(eng, &repeatit, &completed);

			/* mark to-be-reset tokens for completion */
			td_engine_io_abort_tok_list(eng, &timedout, &completed);

			goto skip_retries;

		case TD_TOK_RESULT_WARN:
			/* Set the result to the correct return code */
			tok->result = TD_TOK_RESULT_OK;
			/* Request is done.. */
			/* add to (temporary) completed list */
#ifdef CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
			if (td_token_is_write(tok))
				list_add_tail(&tok->link, &write_completed);
			else
#endif
				list_add_tail(&tok->link, &completed);
			break;
			td_eng_err(eng, "Command assumed OKAY but returned an"
					" ssd warning.\n"
					"(tok=%d, cmd[0]=%16llx)\n",
					tok->tokid, tok->cmd_bytes[0]);
			break;

		case TD_TOK_RESULT_FAIL_ABORT:
		default:
			/* this is a failure, just complete it now */
			list_add_tail(&tok->link, &completed);
			break;
		}

	}
	eng->td_sequence_oldest = oldest_seq;
	td_eng_trace(eng, TR_SEQ, "tok:seq:oldest", eng->td_sequence_oldest);
	td_eng_trace(eng, TR_SEQ, "tok:seq:delta",  eng->td_sequence_next - eng->td_sequence_oldest);
	td_eng_trace(eng, TR_SEQ, "tok:seq:next",   eng->td_sequence_next);
	td_active_tokens_unlock(eng);

#if 0
	if(OoO) {
		if (!td_state_is_ooo(eng))
			td_eng_hal_out_of_order(eng, &repeatit, OoO);
	}
#endif

#ifdef CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
	/* writes are prioritized */
	td_engine_io_complete_list(eng, &write_completed);
#endif

	/* pop read buffers */
	td_engine_mcefree_read_buffer_matching(eng, &timedout, &completed);

	if (unlikely (!list_empty(&timedout)))
		td_eng_hal_handle_timeouts(eng, &timedout, &completed);

	/* handle repeats */
	td_engine_io_repeat_list(eng, &repeatit, &completed);

skip_retries:

	td_eng_trace(eng, TR_STATUS, "parse-status-end  ",
			eng->loop_counters.completed.reads
			+ eng->loop_counters.completed.writes
			+ eng->loop_counters.completed.control);

	/* complete any remaining commands */
	td_engine_io_complete_list(eng, &completed);

	/* detect and deallocate orphaned read buffers */
	td_engine_recover_read_buffer_orphans(eng);

	/* success */

	td_histogram_update(&eng->hist_completed, rc);
	td_histogram_update(&eng->hist_post_comp_active, td_active_tokens(eng));
	td_histogram_update(&eng->hist_post_comp_queued, td_engine_queued_bios(eng));

	/* done: */
	td_switch_task(td_engine_devgroup(eng), prev);

	if (eng->td_stats.read.max_concurrent_completed
			< eng->loop_counters.completed.reads)
		eng->td_stats.read.max_concurrent_completed
			= eng->loop_counters.completed.reads;

	if (eng->td_stats.write.max_concurrent_completed
			< eng->loop_counters.completed.writes)
		eng->td_stats.write.max_concurrent_completed
			= eng->loop_counters.completed.writes;

	if (eng->td_stats.control.max_concurrent_completed
			< eng->loop_counters.completed.control)
		eng->td_stats.control.max_concurrent_completed
			= eng->loop_counters.completed.control;
}

#ifdef CONFIG_TERADIMM_DEBUG_STATUS_CHANGES
void td_eng_dump_status_changes(struct td_engine *eng)
{
	int size = 256;
	if (!memcmp(eng->td_status, eng->td_last_status, size)) {
		eng->td_no_events ++;
	} else {
		td_eng_trace(eng, TR_TOKEN,
			"STM:status:no-events      ",
			eng->td_no_events);

#define dump_status_N(n) ({ \
		uint64_t *pre = (void*)(((uint8_t*)eng->td_last_status)+n); \
		uint64_t *now = (void*)(((uint8_t*)eng->td_status)+n); \
		if (*pre != *now) \
			td_eng_trace(eng, TR_TOKEN, "TD:status+" \
				__stringify(n)"              ", *now); \
		})

		dump_status_N(0x00); dump_status_N(0x08);
		dump_status_N(0x10); dump_status_N(0x18);
		dump_status_N(0x20); dump_status_N(0x28);
		dump_status_N(0x30); dump_status_N(0x38);

		dump_status_N(0x40); dump_status_N(0x48);
		dump_status_N(0x50); dump_status_N(0x58);
		dump_status_N(0x60); dump_status_N(0x68);
		dump_status_N(0x70); dump_status_N(0x78);

		dump_status_N(0x80); dump_status_N(0x88);
		dump_status_N(0x90); dump_status_N(0x98);
		dump_status_N(0xa0); dump_status_N(0xa8);
		dump_status_N(0xb0); dump_status_N(0xb8);

		dump_status_N(0xc0); dump_status_N(0xc8);
		dump_status_N(0xd0); dump_status_N(0xd8);
		dump_status_N(0xe0); dump_status_N(0xe8);
		dump_status_N(0xf0); dump_status_N(0xf8);

		memcpy(eng->td_last_status, eng->td_status, size);
		eng->td_no_events = 0;
	}
}
#endif

/* thread stuff */


/** generate a DEALLOCATE command for a given read buffer;
 * return 0 on success, or negative error code */
static int td_engine_deallocate_rdbuf(struct td_engine *eng, unsigned rdbuf)
{
	struct td_token *tok;

	if (!TD_IS_RD_BUFID_VALID(rdbuf))
		return -EINVAL;

	/* allocate a command token */
	tok = td_alloc_token(eng, TD_TOK_FOR_HW);
	if (!tok)
		return -ENOMEM;

	td_token_reset(tok);

	/* set magic flags as needed */
	tok->magic_flags = (uint8_t)td_eng_conf_var_get(eng, MAGIC_FLAGS);

	tok->free_rd_bufid = (uint8_t)rdbuf;

	tok->independent_deallocate = 1;
	tok->hw = 1;

	if (td_state_is_purging_read_buffers(eng))
		tok->ops.completion = td_request_end_deallocate;

	/* send it to the hardware */
	td_engine_start_token(eng, tok);

	return 0;
}


/** generate an DEALLOCATE command for each rdbuf that needs to be deallocated.
 * return number of such commands sent */
static unsigned td_engine_deallocate_rdbufs(struct td_engine *eng, uint *max)
{
	int rc;
	unsigned rdbuf;
	unsigned total = 0;
	
	if (td_engine_hold_back_deallocate(eng))
		return -EBUSY;

	while (td_available_tokens(eng, TD_TOK_FOR_HW)) {
		
		if (!*max)
			break;

		/* if we are doing JIT deallocations, only do them if there is a read
		 * waiting */
		if (td_engine_jit_deallocations(eng)
				&& !eng->td_early_completed_reads_tokens.count)
			break;

		rdbuf = td_next_rdbuf_to_deallocate(eng);
		if (!TD_IS_RD_BUFID_VALID(rdbuf))
			break;

td_eng_trace(eng, TR_RDBUF, "deallocate:ind:rdbuf", rdbuf);
td_eng_trace(eng, TR_RDBUF, "deallocate:ind:queued", td_pending_rdbuf_deallocations(eng));

		rc = td_engine_deallocate_rdbuf(eng, rdbuf);
		if (unlikely(rc))
			goto failed_to_deallocate;

		/* done with this one */
		total ++;
		(*max) --;
	}

	return total;

failed_to_deallocate:
	/* do it next time */
	td_schedule_rdbuf_deallocation(eng, rdbuf);

	return total;

}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
int td_eng_send_next_throttled_rdbuf_deallocate(struct td_engine *eng)
{
	uint rdbuf;
	int rc;

	if (!td_state_can_query_mcefree_status(eng))
		return -EIO;

	/* if we are doing JIT deallocations, only do them if there is a read
	 * waiting */
	if (td_engine_jit_deallocations(eng)
			&& !eng->td_early_completed_reads_tokens.count)
		return -ENOENT;

	if (td_engine_hold_back_deallocate(eng))
		return -EBUSY;

	rdbuf = __td_next_rdbuf_to_deallocate(eng);

	td_eng_trace(eng, TR_RDBUF, "deallocate:thr:rdbuf", rdbuf);

	if (!TD_IS_RD_BUFID_VALID(rdbuf))
		return -ENOENT;

	td_eng_trace(eng, TR_RDBUF, "deallocate:thr:queued",
			td_pending_rdbuf_deallocations(eng));

	rc = td_engine_deallocate_rdbuf (eng, rdbuf);

	if (rc) {
		td_eng_trace(eng, TR_RDBUF, "deallocate:thr:failed", rc);
		td_schedule_rdbuf_deallocation(eng, rdbuf);
	}

	return rc;
}
#endif

struct td_eng_bio_change_work_state {
	struct td_eng_thread_work work;
	void *bio_context;
};

static int __td_engine_bio_stop (struct td_engine *eng, void* data)
{
	struct td_eng_bio_change_work_state *state =
		container_of(data, struct td_eng_bio_change_work_state, work);

	eng->td_prev_state = eng->td_run_state;
	td_run_state_enter(eng, BIO_DRAIN);
	eng->bio_context = state->bio_context;

	return 0;
}

static int __td_engine_bio_start (struct td_engine *eng, void* data)
{
	int rc;
#if 0
	struct td_eng_bio_change_work_state *state =
		container_of(data, struct td_eng_bio_change_work_state, work);
#endif

	rc = -EINVAL;
	if(!eng->bio_context)
		goto not_locked;

	if (td_run_state_check(eng, BIO_DRAIN) ||
			td_run_state_check(eng, UCMD_ONLY) )
		__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, eng->td_prev_state);

	eng->bio_context = NULL;
	rc = 0;

not_locked:
	return rc;
}

int td_engine_stop_bio(struct td_engine *eng, void *bio_context)
{
	struct td_eng_bio_change_work_state thread_work = {
		.work.name = "bio_stop",
		.work.func = __td_engine_bio_stop,
		.bio_context = bio_context,
	};
	int rc = 0;

	rc = td_eng_thread_work(eng, &thread_work.work);
	if (rc == 0)
		rc = thread_work.work.result;

	return rc;
}

int td_engine_start_bio(struct td_engine *eng)
{
	struct td_eng_bio_change_work_state thread_work = {
		.work.name = "bio_start",
		.work.func = __td_engine_bio_start,
		.bio_context = NULL,
	};
	int rc = 0;

	rc = td_eng_thread_work(eng, &thread_work.work);
	if (rc == 0)
		rc = thread_work.work.result;

	return rc;
}

struct td_eng_lock_work_state {
	struct td_eng_thread_work work;
	struct td_ioctl_device_lock *lock;
	void * locker_context;
};

static int __td_engine_lock(struct td_engine *eng, void* data)
{

	struct td_eng_lock_work_state *state =
		container_of(data, struct td_eng_lock_work_state, work);
	uint16_t rc;
	int ret = -EBUSY;
	
	if (eng->locker_context)
		goto already_locked;
	
	ret = -EIO;
	rc = td_alloc_core_buffer_range(eng,
			state->lock->core_start, state->lock->core_size);

	if (TD_INVALID_CORE_BUFID == rc) {
		goto core_alloc_failed;
	}

	eng->locker_context = state->locker_context;
	eng->td_core_lock_start = state->lock->core_start;
	eng->td_core_lock_size = state->lock->core_size;
	
	td_eng_notice(eng, "Locked buffers %u to %u\n", eng->td_core_lock_start,
				eng->td_core_lock_start + eng->td_core_lock_size -1);

	return 0;


core_alloc_failed:
already_locked:
	return ret;

}

static int __td_engine_unlock(struct td_engine *eng, void* data)
{
	struct td_eng_lock_work_state *state =
		container_of(data, struct td_eng_lock_work_state, work);
	uint16_t start, end;
	int ret = -EINVAL;

	if (!eng->locker_context)
		goto not_locked;

	if (state->locker_context != eng->locker_context)
		goto inval_task;

	start = eng->td_core_lock_start;
	end = start + eng->td_core_lock_size;
	for (; start < end; start++)
		td_free_core_buffer(eng, start);

	td_eng_notice(eng, "Unlocked buffers %u to %u\n", eng->td_core_lock_start,
				eng->td_core_lock_start + eng->td_core_lock_size -1);
	eng->td_core_lock_start = eng->td_core_lock_size = 0;
	eng->locker_context = NULL;
	ret = 0;

inval_task:
not_locked:
	return ret;
}

int td_engine_lock(struct td_engine *eng, struct td_ioctl_device_lock *lock, void *locker_context)
{
	struct td_eng_lock_work_state thread_work = {
		.work.name = "lock",
		.work.func = __td_engine_lock,
		.lock = lock,
		.locker_context = locker_context,
	};
	int rc = 0;

	rc = td_eng_thread_work(eng, &thread_work.work);
	if (rc == 0)
		rc = thread_work.work.result;

	return rc;
}

int td_engine_unlock(struct td_engine *eng, void *locker_context)
{
	struct td_eng_lock_work_state thread_work = {
		.work.name = "ulock",
		.work.func = __td_engine_unlock,
		.locker_context = locker_context,
	};
	int rc = 0;

	rc = td_eng_thread_work(eng, &thread_work.work);
	if (rc == 0)
		rc = thread_work.work.result;

	return rc;
};

int td_engine_recover_read_buffer_orphans(struct td_engine *eng)
{
	int recovered = 0;
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	unsigned rdbuf;

	rdbuf = td_get_read_buffer_orphan(eng);
	if (!TD_IS_RD_BUFID_VALID(rdbuf))
		return 0;

	eng->td_counters.misc.orphan_read_buffer_cnt ++;

	td_schedule_rdbuf_deallocation(eng, rdbuf);

	recovered = 1;
#endif
	return recovered;
}


/* ---- async state changes ---- */
static void td_engine_run_state_change(struct td_engine *eng,
		enum td_engine_run_state prev_state,
		enum td_engine_run_state run_state)
{
	switch (run_state) {
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_DEAD:
	case TD_RUN_STATE_PM_SLEEP:
	case TD_RUN_STATE_UCMD_ONLY:
		td_eng_info(eng, "Signaling state change: %d\n", run_state);
		complete_all(&eng->td_state_change_completion);
		break;
	default:
		break;
	}
}


/** switch state, wrapper for td_run_state_enter() */
void __td_run_state_enter(struct td_engine *eng,
		const char *func, const char* file, int line,
		enum td_engine_run_state new_state)
{
	enum td_engine_run_state prev_state = td_run_state(eng);

	if (prev_state == new_state)
		return;

#ifdef CONFIG_TERADIMM_DONT_TRACE_IN_DEAD_STATE
	/* tracing control, re-enable tracing when exiting DEAD state */
	if (prev_state == TD_RUN_STATE_DEAD)
		td_trace_set_wrap(&eng->td_trace, 1);
#endif

	td_eng_debug(eng, "STATE %d -> %d\n",
			prev_state, new_state);

	td_eng_trace(eng, TR_STATE, func, line);
	td_eng_trace(eng, TR_STATE, "STATE", new_state);

	eng->td_run_state = (enum td_engine_run_state)new_state;
	eng->td_run_state_jiffies = jiffies;

	if(new_state == TD_RUN_STATE_DEAD
			&& prev_state != TD_RUN_STATE_TERMINATING) {
		td_eng_err(eng, "RUN ERROR: run_state going DEAD "
				"in %s() [%s:%d]\n",
				func, file, line);
		td_terminate_all_outstanding_bios(eng, 0);
	}

	td_engine_run_state_change(eng, prev_state, new_state);

	if (new_state == TD_RUN_STATE_DEAD) {
#ifdef CONFIG_TERADIMM_DONT_TRACE_IN_DEAD_STATE
		/* tracing control, don't trace in DEAD state */
		td_trace_set_wrap(&eng->td_trace, 0);
#endif
		/* dump out a bunch of token traces when we die */
#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
		switch (td_token_dump_enable) {
		case TD_TOKEN_DUMP_ERROR:
			if (prev_state != TD_RUN_STATE_TERMINATING)
				break;
		case TD_TOKEN_DUMP_DEAD:
			td_eng_dump_tok_events(eng);
			break;
		default:
			break;
		}
#endif
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
		switch (td_fwstatus_dump_enable) {
		case TD_FWSTATUS_DUMP_ERROR:
			if (prev_state != TD_RUN_STATE_TERMINATING)
				break;
		case TD_FWSTATUS_DUMP_DEAD:
			td_fwstatus_dump(eng);
			break;
		default:
			break;
		}
#endif
	}
}


int __td_run_state_wait(struct td_engine *eng,
		enum td_engine_run_state wanted_state,
		int timeout)
{
	int rc;

	if (td_run_state(eng) == wanted_state)
		return 0;

	rc = wait_for_completion_interruptible_timeout(
			&eng->td_state_change_completion,
			timeout);
	if (rc <= 0) {
#if 0
		td_eng_err(eng, "TIMEOUT waiting for %d: %d\n",
				wanted_state,
				eng->td_run_state);
#endif

		/* Don't return restartsys to user. */
		if (rc == -ERESTARTSYS) {
			td_eng_debug(eng, "TIMEOUT = %d, -ERESTARTSYS\n", rc);
			WARN_ON(1);
			rc = -EAGAIN;
		} else if (rc == 0) {
			td_eng_debug(eng, "TIMEOUT = 0, complete timeout\n");
			rc = -ETIMEDOUT;
		}

		goto error_completion;
	}

	if (td_run_state(eng) == wanted_state)
		rc = 0;
	else
		rc = -EIO;

error_completion:
	return rc;
};



int td_engine_init(struct td_engine *eng, struct td_device *dev)
{
	int rc;
	unsigned tokid;
	enum td_token_type tt;
#if defined(CONFIG_TERADIMM_RDBUF_TRACKING) || defined(CONFIG_TERADIMM_MCEFREE_FWSTATUS)
	unsigned rd_bufid;
#endif
	struct td_eng_hal_ops *eng_ops;
	struct td_token *tok;
	const char *name;

	name = eng->td_name = td_device_name(dev);

	init_completion(&eng->td_state_change_completion);

	INIT_BITMAP(eng->td_wr_buf_used_map, TD_HOST_WR_BUFS_PER_DEV);

	rc = -ENOENT;
	eng_ops = td_eng_hal_ops_for_name(name);
	if (!eng_ops) {
		td_eng_err(eng, "Could not assign a valid implementation "
				"to %s\n", name);
		goto error_find_ops;
	}

	td_eng_conf_init(eng, &eng->conf);
	td_monitor_bins_init(&eng->ecc_bins.ddr3);
	td_monitor_bins_init(&eng->ecc_bins.internal);

	/* core buffers are limited by CORE_BUFS, set in engine init */
	td_reset_core_buffer_tracking(eng);

	/* similarly, read buffers are limited by HOST_READ_BUFS, also set in engine init */
	td_reset_rdbuf_deallocation(eng);

	/* No error injection */
	eng->td_errors_mask = 0;
	eng->td_errors_count = 0;

	/* histograms */
	td_histogram_init(&eng->hist_started,
			         "%s:started", name);
	td_histogram_init(&eng->hist_post_start_active,
			         "%s:post_start_active",  name);
	td_histogram_init(&eng->hist_post_start_queued,
			         "%s:post_start_queued",  name);
	td_histogram_init(&eng->hist_completed,
			         "%s:completed", name);
	td_histogram_init(&eng->hist_post_comp_active,
			         "%s:post_comp_active",  name);
	td_histogram_init(&eng->hist_post_comp_queued,
			         "%s:post_comp_queued",  name);

	/* ATOMIC set our td_thread_work */
	td_atomic_ptr_set(&eng->td_thread_work, 0);

	/* initially the copy ops do nothing */
	eng->td_bio_copy_ops = td_token_copy_ops_null;
	eng->td_virt_copy_ops = td_token_copy_ops_null;

	memset(&eng->td_counters, 0, sizeof(eng->td_counters));

	/* initialize tokens */
	if (td_all_active_tokens(eng))
		td_eng_warning(eng, "active count is %d\n",
				td_all_active_tokens(eng));
	WARN_ON(td_active_tokens(eng, TD_TOK_FOR_FW));
	WARN_ON(td_active_tokens(eng, TD_TOK_FOR_HW));

	/* initialize tokens */
	for_each_token_type(tt) {
		struct td_token_pool *tok_pool = &eng->tok_pool[tt];
		td_token_list_init(&tok_pool->td_free_tokens);
		td_token_list_init(&tok_pool->td_active_tokens);
		td_token_list_init(&tok_pool->td_timedout_tokens);
		td_token_list_init(&tok_pool->td_resumable_tokens);
	}

	eng->td_sequence_oldest = -1ULL;

#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
	INIT_BITMAP(eng->td_no_update_map, TD_TOKENS_PER_DEV);
#endif

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	for (rd_bufid=0; rd_bufid<TD_HOST_RD_BUFS_PER_DEV; rd_bufid++) {
		eng->td_active_rdbuf_deallocation_token[rd_bufid]
			= TD_INVALID_RD_BUFID;
	}

	td_token_list_init(&eng->td_early_completed_reads_tokens);
	eng->td_early_completed_reads_last_progress = 0;
#endif

	/* initialize the token array */
	for (tokid=0; tokid<TD_TOKENS_PER_DEV; tokid++) {
		tok = &eng->td_tokens[tokid];
		tok->td_engine = eng;
		tok->tokid = (uint16_t)tokid;

		td_token_reset(tok);
	}

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING

	/* reset orphan read buffer tracking */
	INIT_LIST_HEAD(&eng->td_rd_buf_lru);
	for (rd_bufid=0; rd_bufid<TD_HOST_RD_BUFS_PER_DEV; rd_bufid++) {
		struct td_rd_buf *rb;
		rb = eng->td_rd_bufs + rd_bufid;

		memset(&rb->drb_lru_link, 0, sizeof(rb->drb_lru_link));
		rb->drb_rd_bufid = (uint16_t)rd_bufid;
		rb->drb_last_used = 0;
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
		rb->drb_was_orphaned = 0;
#endif
	}
	eng->td_rd_buf_lru_seen_other_reads = 0;
	eng->td_rd_buf_lru_seen_read_timeouts = 0;
#endif

	spin_lock_init(&eng->td_incoming_bio_lock);
	bio_list_init(&eng->td_incoming_bios);
	eng->td_incoming_bio_writes = 0;
	eng->td_incoming_bio_reads = 0;

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
	init_waitqueue_head(&eng->td_incoming_sleep);
	atomic_set(&eng->td_total_system_bios, 0);
	atomic_set(&eng->td_total_incoming_waiters, 0);
#endif
#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_SLEEP
	atomic64_set(&eng->td_incoming_backpressure_sleep_count, 0);
#endif

	bio_list_init(&eng->td_queued_bios);
	eng->td_queued_bio_writes = 0;
	eng->td_queued_bio_reads = 0;

	spin_lock_init(&eng->td_queued_ucmd_lock);
	INIT_LIST_HEAD(&eng->td_queued_ucmd_list);
	eng->td_queued_ucmd_count = 0;

	eng->td_sample_window = td_eng_conf_var_get(eng, IOPS_SAMPLE_MSEC) * 1000UL / HZ; /* ms -> jiffies */
	eng->td_sample_start  = jiffies;
	eng->td_sample_reads   = 0;
	eng->td_sample_writes  = 0;
	eng->td_sample_control = 0;
	eng->td_sample_rbytes = 0;
	eng->td_sample_wbytes = 0;
	eng->td_total_bios    = 0;

	eng->td_polling_loop_count = 0;
	eng->td_last_was_write = 0;

	/* initialize the hardware/simulator */
	eng->ops = eng_ops;
	eng->ops_priv = NULL;

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
	eng->td_queued_task_count = 0;
	spin_lock_init(&eng->td_queued_task_lock);
	INIT_LIST_HEAD(&eng->td_queued_task_list);
	eng->work_slab = kmem_cache_create("td_task_slab", sizeof(struct td_task), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);

	eng->work_pool = mempool_create_slab_pool(256, (struct kmem_cache*)eng->work_slab);
#endif
	/* initialize RMW list */
	bio_list_init(&eng->td_rmw_bios);
	eng->td_rmw_inflight = 0;

	/* initialize trace */
	rc = td_trace_init(&eng->td_trace, eng->td_name, dev->td_cpu_socket);
	if (rc < 0) {
		td_eng_err(eng, "Initialization of trace buffer for %s failed, rc=%d\n",
				eng->td_name, rc);
		goto error_ops_trace;
	}

	/* initialize default config */
	rc = td_eng_hal_init(eng);
	if (rc < 0) {
		td_eng_err(eng, "Initialization of device %s failed, rc=%d\n",
				eng->td_name, rc);
		goto error_ops_init;
	}

	/* add only those tokens which are used */
	for (tokid=0; tokid<TD_TOKENS_PER_DEV; tokid++) {
		struct td_token_list *tok_list;

		/* limit count to configuration */
		if (tokid >= td_eng_conf_var_get(eng, TOKENS))
			break;

		tok = &eng->td_tokens[tokid];

		/* initialize token's knowledge of it's type, so that
		 * td_token_type() can now be used */
		tok->token_type = td_tok_type_from_tokid(eng, tokid);

		tok_list = &eng->tok_pool[td_token_type(tok)].td_free_tokens;

		td_tokens_enqueue(tok_list, tok);
	}

	eng->td_sample_window = td_eng_conf_var_get(eng, IOPS_SAMPLE_MSEC) * 1000UL / HZ; /* ms -> jiffies */

#ifdef CONFIG_TERADIMM_PRIVATE_SPLIT_STASH
	/* 512 is enough for 2 bios, which is common for un-aligned IO */
	eng->td_split_stash = td_stash_init(eng, 512, 256);
#endif

	td_run_state_enter(eng, INIT);


	return 0;

error_ops_init:
error_ops_trace:
error_find_ops:
	return rc;
}

#ifdef CONFIG_PM
int td_engine_resume(struct td_engine *eng)
{
	enum td_engine_run_state want = eng->td_prev_state;
	enum td_engine_run_state p_state = eng->td_run_state;
	enum td_device_state_type prev_dstate;
	int ret;
	struct td_devgroup *dg;
	struct td_device *dev;
	int to; /* timeout on wait. */

	ret = -EINVAL;
	if (!td_state_pm(eng)) {
		ret = 0;
		td_eng_trace(eng, TR_TOKEN, "resume:not sleeping\n",
				td_run_state(eng));
		goto not_sleeping;
	}

	dev = td_engine_device(eng);
	prev_dstate = dev->td_state;
	dg = dev->td_devgroup;

	td_device_lock(dev);

	if (!dg || !td_devgroup_is_running(dg))
	{
		/* We are not running, so we can go directly to the wanted
		 * state, device state has not changed.  */
		__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, want);
		td_device_unlock(dev);
		goto easy_out;
	}

	/* detach from current group */
	if (td_devgroup_remove_device(dg, dev))
		goto error_remove_grp;

	/* sync */
	(void) td_devgroup_sync_device(dg, dev);


	/* Do the switch */
	if (eng->pm_power_cycle) {

		eng->pm_power_cycle = 0;
		/* if we power cycle, we need to re-train until UEFI does this. */
		td_device_train(dev);

		/* We need to re-init the hardware by starting the engine.. */
		td_run_state_enter(eng, INIT);
		/* Re-add the group. */
		if (td_devgroup_add_device(dg, dev))
			goto error_init_add_grp;

		init_completion(&eng->td_state_change_completion);

		td_device_unlock(dev);

		/* wake up the thread */
		td_devgroup_poke(dg);

		/* sync */
		(void) td_devgroup_sync_device(dg, dev);

		/* Wait for final state: RUNNING */
		to = td_run_state_wait_ms(eng, RUNNING, 25000);
		if (to < 0) {
			ret = -ETIMEDOUT;
			td_eng_err(eng, "hardware init failed!, stuck in state %d\n", td_run_state(eng));
			goto error_init_fail;
		}
		/* detach from current group */
		if (td_devgroup_remove_device(dg, dev))
			goto error_init_remove_grp;

		/* sync */
		(void) td_devgroup_sync_device(dg, dev);
	}

	if (td_run_state(eng) != want) {
		/* Do the run state switch */
		__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, want);
		init_completion(&eng->td_state_change_completion);
	}


	/* Re-add the group. */
	if (td_devgroup_add_device(dg, dev))
		goto error_add_grp;

	td_device_unlock(dev);

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* Need to use internal call because we don't know what is stored in
	 * the "want" run level. */
	to = __td_run_state_wait(eng, want, 30*HZ);

	if (to < 0)
		goto engine_state_fail;


	if (dev->td_state != prev_dstate) {
		/* Reset notifier to be safe */
		init_completion(&dev->td_state_change_completion);
		__td_device_enter_state(dev, prev_dstate);
		to = td_dev_state_wait(dev, prev_dstate, 3000*HZ/1000);
		/* Catch transition that may have occurred prior to waiting */
		if(!!(dev->td_state == prev_dstate))
			to = 0;
	}

	/* Device state is okay, and engine state is okay.*/
	if(to >= 0)
		ret = 0;

	td_eng_trace(eng, TR_TOKEN, "pm_exit:dev_state", dev->td_state);

	eng->td_prev_state = p_state;
not_sleeping:
easy_out:
	return ret;

/* init failures */
error_init_remove_grp:
error_init_fail:
error_init_add_grp:
	return ret;

/* Other failures */
error_add_grp:
	/* Restore the state to the PM_SLEEP state..
	 * should this be the dead state ? */
	__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, eng->td_prev_state);

	td_devgroup_add_device(dg, dev);

error_remove_grp:
	td_device_unlock(dev);
engine_state_fail:
	return ret;
}

/* enter lower power state.  */
int td_engine_suspend(struct td_engine *eng, pm_message_t state)
{
	enum td_engine_run_state current_state = eng->td_run_state;
	int ret = -EIO;
	struct td_devgroup *dg;
	struct td_device *dev;
	int to;

	dev = td_engine_device(eng);
	dg = dev->td_devgroup;

	td_device_lock(dev);
	if (!dg || !td_devgroup_is_running(dg)) {
		/* We are not running, so we can go directly to PM_SLEEP. */
		td_run_state_enter(eng, PM_SLEEP);
		td_device_unlock(dev);
		goto easy_out;
	}

	/* detach from current group */
	if (td_devgroup_remove_device(dg, dev))
		goto error_remove_grp;

	/* sync */
	(void) td_devgroup_sync_device(dg, dev);

	if (!td_state_can_enter_pm(eng))
		goto error_enter_pm;

	if (PM_EVENT_HIBERNATE == state.event)
		eng->pm_power_cycle = 1;

	/* Do the state switch */
	td_run_state_enter(eng, PM_DRAIN);

	/* Reset notifier to be safe */
	INIT_COMPLETION(eng->td_state_change_completion);

	/* Re-add to the group. */
	if (td_devgroup_add_device(dg, dev))
		goto error_add_grp;


	td_device_unlock(dev);

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* sync */
	(void) td_devgroup_sync_device(dg, dev);

	/* Wait for final state: PM_SLEEP */
	to = td_run_state_wait_ms(eng, PM_SLEEP, 10200);

	if (to < 0) {
		td_eng_err(eng, "wait returned %d\n", to);
		ret = -ETIMEDOUT;
	}
	/* Catch the final transition that may have occurred prior to waiting */
	if(td_run_state_check(eng, PM_SLEEP)) {
		ret = 0;
	}
	else {
		td_eng_err(eng, "State != PM_SLEEP; state = %d q = %d, a = %d\n",
				td_run_state(eng),
				td_engine_queued_work(eng),
				td_all_active_tokens(eng));
		goto error_not_sleeping;
	}


	td_eng_trace(eng, TR_TOKEN, "pm_start:sleeping", ret);
easy_out:
	eng->td_prev_state = current_state;
	return ret;

/* Errors */
error_add_grp:
	/* Restore the state to the previous state */
	__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, eng->td_prev_state);

error_enter_pm:
	/* Re-add to group.*/
	td_devgroup_add_device(dg, dev);

error_remove_grp:
	td_device_unlock(dev);
	return ret;

/* Aborts */
error_not_sleeping:
	eng->td_prev_state = current_state;
	/* Reset the device including SATA links, if necessary. */
	td_engine_resume(eng);
	return ret;
}
#endif

int td_engine_start(struct td_engine *eng, int force_init)
{
	int rc;

	BUG_ON(td_device_group(td_engine_device(eng)));

	td_trace_reset(&eng->td_trace);

	rc = td_eng_hal_enable(eng);
	if (rc < 0) {
		td_eng_err(eng, "Enabling of device %s failed, rc=%d\n",
				eng->td_name, rc);
		goto error_ops_enable;
	}

	/* Now that it is ready, prepare to wait for notification */
	init_completion(&eng->td_state_change_completion);
	if (force_init || ! td_run_state_check(eng, RUNNING) ) {
		/* IF we are running, we don't bother doing
		 * re-init, unless we force init when we start */
		td_run_state_enter(eng, INIT);
	}

	/* success */

	return 0;

error_ops_enable:
	return rc;
}

/* device is down */
int td_engine_stop(struct td_engine *eng)
{
	td_busy_end(td_engine_devgroup(eng));

	init_completion(&eng->td_state_change_completion);

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_SLEEP
	td_eng_info(eng, "BACKPRESURE: %lu\n", atomic64_read(&eng->td_incoming_backpressure_sleep_count));
#endif

	td_run_state_enter(eng, TERMINATING);
	mb();

	td_eng_hal_disable(eng);

	td_histogram_dump(&eng->hist_started);
	td_histogram_dump(&eng->hist_post_start_active);
	td_histogram_dump(&eng->hist_post_start_queued);
	td_histogram_dump(&eng->hist_completed);
	td_histogram_dump(&eng->hist_post_comp_active);
	td_histogram_dump(&eng->hist_post_comp_queued);

	td_engine_poke(eng);


	return 0;
}

static int __td_engine_reset (struct td_engine *eng, void* data)
{
	int rc;

	td_trace_reset(&eng->td_trace);

	rc = td_eng_hal_enable(eng);
	if (rc < 0) {
		td_eng_err(eng, "Enabling of device %s failed, rc=%d\n",
				eng->td_name, rc);
		goto error_ops_enable;
	}

	td_run_state_enter(eng, INIT);

error_ops_enable:
	return rc;
}

int td_engine_reset (struct td_engine *eng)
{
	int rc;

	struct td_eng_thread_work work = {
		.name = "reset",
		.func = __td_engine_reset,
	};

	td_eng_info(eng, "Resetting engine\n");
	rc = td_eng_thread_work(eng, &work);
	if (!rc)
		rc= work.result;

	return rc;

}


int td_engine_exit(struct td_engine *eng)
{
	td_eng_hal_exit(eng);

	td_trace_cleanup(&eng->td_trace);

#ifdef CONFIG_TERADIMM_PRIVATE_SPLIT_STASH
	/* 512 is enough for 2 bios, which is common for un-aligned IO */
	td_stash_destroy(eng, eng->td_split_stash);
#endif

	return 0;
}



