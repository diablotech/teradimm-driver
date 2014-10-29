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

#ifndef _TD_ENGINE_H_
#define _TD_ENGINE_H_

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_defs.h"
#include "td_memspace.h"
#include "td_trace.h"
#include "td_devgroup.h"
#include "td_device.h"
#include "td_util.h"
#include "td_cpu.h"
#include "td_eng_conf.h"
#include "td_ioctl.h"
#include "td_eng_latency.h"
#include "td_token.h"
#include "td_token_list.h"
#include "td_trace.h"
#include "td_stash.h"
#include "td_monitor.h"
#include "td_worker.h"

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include "td_bio.h"
#include "td_bitmap.h"
#include "td_histogram.h"

#include "td_engine_def.h"

struct request_queue;
struct td_ioctl_device_lock;

#define td_eng_name(eng) ((eng) && (eng)->td_name) \
			? (eng)->td_name : TERADIMM_NAME

#define td_eng_pk(eng,lvl,fmt,...) \
	printk(lvl "%s: " fmt, td_eng_name(eng), ##__VA_ARGS__)

extern int td_engine_debug;

#define td_eng_debug_enabled(eng) ( td_engine_debug )

#define td_eng_emerg(e,fmt,...)   td_eng_pk(e, KERN_EMERG, fmt, ##__VA_ARGS__)
#define td_eng_alert(e,fmt,...)   td_eng_pk(e, KERN_ALERT, fmt, ##__VA_ARGS__)
#define td_eng_crit(e,fmt,...)    td_eng_pk(e, KERN_CRIT, fmt, ##__VA_ARGS__)
#define td_eng_err(e,fmt,...)     td_eng_pk(e, KERN_ERR, fmt, ##__VA_ARGS__)
#define td_eng_warning(e,fmt,...) td_eng_pk(e, KERN_WARNING, fmt, ##__VA_ARGS__)
#define td_eng_notice(e,fmt,...)  td_eng_pk(e, KERN_NOTICE, fmt, ##__VA_ARGS__)
#define td_eng_info(e,fmt,...)    td_eng_pk(e, KERN_INFO, fmt, ##__VA_ARGS__)
#define td_eng_debug(e,fmt,...)   { if (td_eng_debug_enabled(e)) \
		td_eng_pk(e, KERN_DEBUG, fmt, ##__VA_ARGS__);}
#define td_eng_warn td_eng_warning
#define td_eng_dbg td_eng_debug

#define td_eng_trace(_e_,_t_,_l_,_x_) \
	td_trace(&(_e_)->td_trace, _t_, _l_, _x_)

/* external interface */

extern int td_engine_init(struct td_engine *eng, struct td_device *dev);
extern int td_engine_exit(struct td_engine *eng);

extern int td_engine_start(struct td_engine *eng, int force_init);
extern int td_engine_reconfig(struct td_engine *eng);
extern int td_engine_stop(struct td_engine *eng);
extern int td_engine_resume(struct td_engine *eng);
extern int td_engine_suspend(struct td_engine *eng, pm_message_t state);

static inline void td_engine_reset_loop_counters(struct td_engine *eng)
{
	memset(&eng->loop_counters, 0, sizeof(eng->loop_counters));
}

extern void td_migrate_incoming_to_queued(struct td_engine *eng);

extern void td_engine_io_complete(struct td_engine *eng, enum td_token_type tt);
extern void td_engine_io_begin(struct td_engine *eng);

/** return true if engine needs to perform some actions.
 * used by devgroup to check if the thread needs to wake up */
extern int td_engine_needs_cpu_time(struct td_engine *eng);

/** tests the status of the device, returns non-zero error
 * if engine should be skipped in devgroup thread's processing */
extern int td_engine_io_sanity_check(struct td_engine *eng);

/* Lock/unlock of engine for upgrade.*/
extern int td_engine_lock(struct td_engine *eng,
		struct td_ioctl_device_lock *lock, void *locker_context);
extern int td_engine_unlock(struct td_engine *eng, void *locker_context);

extern int td_engine_stop_bio(struct td_engine *eng, void *bio_context);
extern int td_engine_start_bio(struct td_engine *eng);

extern int td_engine_queue_bio(struct td_engine *eng, td_bio_ref bio);

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
static inline void td_eng_account_bio_completion(struct td_engine *eng)
{
	uint64_t wake_thrsh, sleep_thrsh;
	int64_t inflight, waiting, room, waking;

	/* update the total number of queued/in-flight bios */
	inflight = atomic_dec_return(&eng->td_total_system_bios);

	/* how many tasks are put to sleep waiting for more room */
	waiting = atomic_read(&eng->td_total_incoming_waiters);
	if (likely (!waiting))
		return;

	wake_thrsh  = td_eng_conf_var_get(eng, INCOMING_WAKE);
	sleep_thrsh = td_eng_conf_var_get(eng, INCOMING_SLEEP);
	if (unlikely (wake_thrsh >= sleep_thrsh))
		sleep_thrsh = wake_thrsh + 1;

	/* we wake if the queued/in-flight bios fall below the WAKE threshold
	 * or if WAKE threshold is zero -- which means always wake */
	if (unlikely(!wake_thrsh)) {
		/* disabled, wake up everything */

		waking = waiting;

	} else if (likely(inflight <= wake_thrsh)) {
		/* below wake threshold */

		/* wake up so that we hit the high watermark of INCOMING_SLEEP,
		* but limit the wake up to the number that are sleeping */
		room = sleep_thrsh - inflight;
		waking = min_t(int64_t, room, waiting);

		WARN_ON(waking<1);
		if (unlikely(waking<1))
			return;

	} else
		/* nothing to do this time */
		return;

	/* wake up the held tasks. */
	wake_up_interruptible_nr(&eng->td_incoming_sleep, waking);
}
#else
#define td_eng_account_bio_completion(_eng) do { } while (0)
#endif

static inline void td_engine_endio(struct td_engine *eng, td_bio_ref bio, int result)
{
#ifdef CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
	struct td_devgroup *dg = td_engine_devgroup(eng);
	if (td_dg_conf_general_var_get(dg, ENDIO_ENABLE)) {
		td_devgroup_queue_endio(dg, bio, result);

		/* if incoming requests are waiting, release them */
		td_eng_account_bio_completion(eng);

		return;
	}
#endif
	if (unlikely(result)) {
		/*
		 * We have to let the OSDEV handle errors.  So we can't call:
		 *      td_bio_complete_failure(bio)
		 * directoy ourselves anymore.
		 */
		td_osdev_error_bio(bio);
	} else
		td_bio_complete_success(bio);

	/* if incoming requests are waiting, release them */
	td_eng_account_bio_completion(eng);
}

extern void __td_terminate_all_outstanding_bios(struct td_engine *eng, int reset_active_tokens, int result);

static inline void td_terminate_all_outstanding_bios(struct td_engine *eng,
		int reset_active_tokens)
{
	__td_terminate_all_outstanding_bios(eng, reset_active_tokens, -EIO);
}

static inline void td_engine_poke (struct td_engine *eng)
{
	td_device_poke(td_engine_device(eng));
}

static inline void td_engine_sometimes_poke (struct td_engine *eng)
{
	struct td_device *dev = td_engine_device(eng);
	struct td_devgroup *dg = td_engine_devgroup(eng);

	if (td_work_item_needs_poke(dev->td_work_item, td_dg_conf_worker_var_get(dg, DEV_IDLE_JIFFIES)))
		td_devgroup_poke(dg);
}


extern void td_enqueue_ucmd(struct td_engine *eng, struct td_ucmd *ucmd);

/** given a token prepared with a command, executes it in the device */
extern void td_engine_start_token(struct td_engine *eng, struct td_token *tok);


/** Queue a work task to the engine for processing */
extern int td_engine_queue_task (struct td_engine *eng, 
		void (*work_function)(struct td_engine*, void*),
		void* data);

#ifdef CONFIG_TERADIMM_DEBUG_STATUS_CHANGES
extern void td_eng_dump_status_changes(struct td_engine *eng);
#else
#define td_eng_dump_status_changes(eng) do { /* nothing */ } while(0)
#endif


#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
static inline void td_eng_dump_tok_events(struct td_engine *eng)
{
	int n;
	struct td_token *tok;

	for (n=0; n<TD_TOKENS_PER_DEV; n++) {
		tok = eng->td_tokens + n;

		if (!tok->td_engine)
			continue;

		td_dump_tok_events(tok);

		/* yield */
		schedule();
	}
}
#endif

/*
 * The comamdn generator is used by the engine to get the HAL to generate
 * commands for us
 */
struct td_command_generator {
	int (*_dealloc)(uint64_t cmd[8]);
	int (*_ucmd)(uint64_t bytes[8], struct td_ucmd* ucmd, uint16_t core_buf, uint8_t wep);

	int (*_ata)(uint64_t bytes[8], uint8_t*atacmd, int ssd, int data_size);

	int (*_get_reg)(uint64_t bytes[8], uint32_t reg);
	int (*_put_reg)(uint64_t bytes[8], uint32_t reg, uint32_t data, uint32_t mask);

	int (*_get_params)(uint64_t bytes[8], uint32_t page);
	int (*_set_params)(uint64_t bytes[8], uint32_t page);

	int (*_OoO)(uint64_t bytes[8]);

	int (*_shutdown)(uint64_t bytes[8]);

	const char *_name;
};

#define td_eng_cmdgen(eng, cmd,...) \
	(eng->ops->_generator->_ ## cmd ? \
		 eng->ops->_generator->_ ## cmd (__VA_ARGS__) : \
		 -EINVAL)
/*
 * the #define below checks if the _generator has a function pointer to
 * "_cmd", if the pointer exists, the function is then called with "bytes" and,
 * if they exist, "args".  If this returns 0, the _filter function will be
 * called on "bytes" to make sure the command can be run on this engine right
 * now.  That is, the engine is in the correct state to accept the type of
 * command contained within "bytes".
 */
#define td_eng_cmdgen_chk(eng, cmd, bytes,...) \
		(eng->ops->_generator->_ ## cmd ? \
			((eng->ops->_generator->_ ## cmd (bytes, ##__VA_ARGS__)) ?: \
				(eng->ops->_filter(eng, bytes)) )                   \
		 : -EINVAL)

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
struct td_task {
	struct list_head         queue_link;
	void (*work_function)(struct td_engine*, void*);
	void* data;
};
#endif

#ifndef TD_RMW_INFLIGHT_LIMIT
#define TD_RMW_INFLIGHT_LIMIT 1
#endif




/*
 * TODO: a lot of the stuff below this should be removed or moved to td_engine.c
 */

/* token locking functions */

#define TD_DEFINE_TOKEN_LOCK_FUNCTIONS(_type_)                         \
static inline void td_##_type_##_tokens_lock(struct td_engine *eng)    \
{                                                                      \
	int i;                                                         \
	for (i=0; i<TD_TOK_TYPE_MAX; i++)                              \
		td_tokens_lock(&eng->tok_pool[i]                       \
				.td_##_type_##_tokens);                \
}                                                                      \
static inline void td_##_type_##_tokens_unlock(struct td_engine *eng)  \
{                                                                      \
	int i;                                                         \
	for (i=TD_TOK_TYPE_MAX-1; i>=0; i--)                           \
		td_tokens_unlock(&eng->tok_pool[i]                     \
				.td_##_type_##_tokens);                \
}

TD_DEFINE_TOKEN_LOCK_FUNCTIONS(free);
TD_DEFINE_TOKEN_LOCK_FUNCTIONS(active);
TD_DEFINE_TOKEN_LOCK_FUNCTIONS(timedout);
TD_DEFINE_TOKEN_LOCK_FUNCTIONS(resumable);


/* bio stuff */

/* compute and return the LBA for a given sector */
static inline uint64_t td_sector_lba(struct td_engine *eng,
		uint64_t sector)
{
	if (td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) != 1<<SECTOR_SHIFT) {
		uint64_t ofs = sector << SECTOR_SHIFT;
		return ofs / td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);
	}
	return sector;
}

/* compute and return the LBA for a given bio */
static inline uint64_t td_bio_lba(struct td_engine *eng,
		td_bio_ref bio)
{
	return td_sector_lba(eng, td_bio_get_sector_offset(bio));
}

/* compute and return the bio offset within a hardware LBA */
static inline uint64_t td_sector_lba_offset(struct td_engine *eng,
		uint64_t sector)
{
	if (td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) != 1<<SECTOR_SHIFT);
	{
		uint64_t ofs = sector << SECTOR_SHIFT;
		return ofs % td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);
	}
	return 0;
}

/* compute and return the bio offset within a hardware LBA */
static inline uint64_t td_bio_lba_offset(struct td_engine *eng,
		td_bio_ref bio)
{
	return td_sector_lba_offset(eng, td_bio_get_sector_offset(bio));
}

/* returns the number of hardware sectors the bio spans */
static inline int td_bio_lba_span(td_bio_ref bio, uint hw_sector_size)
{
	uint64_t start = td_bio_get_sector_offset(bio) << SECTOR_SHIFT;
	uint64_t end = start + td_bio_get_byte_size(bio) - 1;

	uint64_t lba0 = start / hw_sector_size;
	uint64_t lba1 = end / hw_sector_size;

	return (int)(1 + lba1 - lba0);
}

/* returns the number of hardware pages (commands) the bio spans */
static inline int td_bio_page_span(td_bio_ref bio, uint64_t size)
{
	uint64_t start = td_bio_get_sector_offset(bio) << SECTOR_SHIFT;
	uint64_t end = start + td_bio_get_byte_size(bio) - 1;

	uint64_t lba0 = start / size;
	uint64_t lba1 = end / size;

	return (int)(1 + lba1 - lba0);
}

/* convert a BIO sector number to a hardware LBA and offset */
static inline void td_token_assign_lba_and_offset_from_bio(struct td_token *tok,
		td_bio_ref bio)
{
	struct td_engine *eng = td_token_engine(tok);

	if (td_eng_conf_hw_var_get(eng, SSD_COUNT) > 1) {
		uint64_t lba = td_bio_lba(eng, bio);
		uint64_t lba_ofs = td_bio_lba_offset(eng, bio);
		
		uint64_t stride = td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS);
		uint64_t piece = lba / stride;
		uint64_t offset = lba % stride;

#ifdef CONFIG_TERADIMM_FORCE_SSD_HACK
		extern int force_ssd;
		if (unlikely(force_ssd >= 0)) {
			uint64_t ssd_sec_cnt = td_eng_conf_hw_var_get(eng, SSD_SECTOR_COUNT);
			tok->port = force_ssd;
			tok->to_ssd = 1;
			tok->lba = lba % ssd_sec_cnt;
			tok->lba_ofs = (uint16_t)lba_ofs;
			return;
		}
#endif

		tok->to_ssd = 1;
		tok->port = (uint16_t)(piece % td_eng_conf_hw_var_get(eng, SSD_COUNT));
		tok->lba = stride * (piece / td_eng_conf_hw_var_get(eng, SSD_COUNT)) + offset;
		tok->lba_ofs = (uint16_t)lba_ofs;
	} else {
		tok->lba     = td_bio_lba(eng, bio);
		tok->lba_ofs = (uint16_t)td_bio_lba_offset(eng, bio);
	}
}

/* return non-zero if access needs read-modify-write;
 * this basically means that it spans multiple hw LBAs */
static inline int td_bio_needs_rmw(struct td_engine *eng,
		td_bio_ref bio)
{
	return td_bio_is_write(bio)
		&& td_bio_get_byte_size(bio) < td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE)
#ifdef REQ_DISCARD
		&& !td_bio_is_discard(bio)
#endif
	       ;
}

/* return non-zero if bi_sector and bi_size are multiples of our sector size */
static inline int td_bio_aligned_correctly(struct td_engine *eng,
		td_bio_ref bio)
{
	uint     bi_byte_size = td_bio_get_byte_size(bio);
	uint64_t bi_byte_ofs = td_bio_get_sector_offset(bio) << SECTOR_SHIFT;
	uint     bio_sector_size = (uint)td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);

	return bio_sector_size
		&& !(bi_byte_size % bio_sector_size)
		&& !(bi_byte_ofs % bio_sector_size);
}

static inline void td_set_token_timeout(struct td_token *tok, uint32_t timeout_us)
{
	tok->active_timeout = td_get_cycles() + td_usec_to_cycles(timeout_us);
}

/** reset token timeout */
static inline void td_reset_token_timeout(struct td_engine *eng,
		struct td_token *tok)
{
	if (tok->quick_n_quiet) {
		td_set_token_timeout(tok, TD_TOKEN_QUICK_N_QUIET_TIMEOUT);
	} else {
		tok->active_timeout = td_get_cycles()
			+ td_usec_to_cycles(td_eng_conf_var_get(eng, START_TIMEOUT_USEC));
	}
}

/** rest if a timeout has occurred */
static inline bool __td_has_token_timedout(struct td_token *tok, cycles_t now)
{
	return !!(now > tok->active_timeout);
}
#define td_has_token_timedout(tok) __td_has_token_timedout(tok, td_get_cycles())

/** return true if token is executing a reset command */
static inline unsigned td_token_is_reset_cmd(struct td_token *tok)
{
	return tok->reset_count > 0;
}

/** return true if token is executing a write */
static inline unsigned td_token_is_write(struct td_token *tok)
{
	return !!tok->len_host_to_dev;
}

/** return true if token is a hardware command. */
static inline unsigned td_token_is_hw(struct td_token *tok)
{
	return !!tok->hw;
}
/** return true if token is a hardware command. */
static inline unsigned td_token_is_deallocate(struct td_token *tok)
{
	return !!(TD_IS_RD_BUFID_VALID(tok->free_rd_bufid));
}
/** return true if token is executing a read */
static inline unsigned td_token_is_read(struct td_token *tok)
{
	return !!tok->len_dev_to_host;
}

/** return true if token is executing a control command */
static inline unsigned td_token_is_control(struct td_token *tok)
{
	return !td_token_is_write(tok)
		&& !td_token_is_read(tok);
}

/* state macros */

#define td_run_state(eng) ( (eng)->td_run_state )

extern void __td_run_state_enter(struct td_engine *eng,
		const char *func, const char* file, int line,
		enum td_engine_run_state new_state);
#define td_run_state_enter(eng,_state) { \
	__td_run_state_enter(eng, __FUNCTION__, __FILENAME__, __LINE__, TD_RUN_STATE_ ## _state); }


#define td_run_state_check(eng,_state) \
	!!(td_run_state(eng) == TD_RUN_STATE_ ## _state)


/* Linux uses jiffies */
extern int __td_run_state_wait(struct td_engine *eng, enum td_engine_run_state wanted_state, int timeout);
#define td_run_state_wait(eng, _state) __td_run_state_wait(eng, TD_RUN_STATE_ ## _state, 30*HZ)
#define td_run_state_wait_ms(eng, _state, _ms) __td_run_state_wait(eng, TD_RUN_STATE_ ## _state, _ms*HZ/1000)

/** return true if the state seems to be stuck */
static inline int td_is_state_stuck(struct td_engine *eng)
{
	uint64_t diff;
	uint64_t retry_cnt;
	uint64_t max;

	switch (td_run_state(eng)) {
	default:
		return 0;

	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_FW_PROBE:
	case TD_RUN_STATE_TERMINATING:
		diff = (uint64_t)(jiffies - eng->td_run_state_jiffies);

		retry_cnt = 1 + td_eng_conf_var_get(eng, TIMEOUT_RETRY);
		max = (td_eng_conf_var_get(eng, STUCK_TIMEOUT_USEC) * HZ / 1000000)
			* retry_cnt;

		if (diff > max)
			return 1;

		return 0;
	}
}

/** return the next sequence for use, advancing */
static inline uint64_t td_engine_next_sequence (struct td_engine* eng)
{
	/* Post increment, because next is the one we want */
	return eng->td_sequence_next++;
}

/** return the next sequence for use, without advancing */
static inline uint64_t td_engine_peek_next_sequence (struct td_engine* eng)
{
	return eng->td_sequence_next;
}

/** return the oldest sequence number in flight */
static inline uint64_t td_engine_oldest_active_sequence(struct td_engine *eng)
{
	return eng->td_sequence_oldest;
}

/** Check if our sequence spread is still ok */
static inline bool td_engine_sequence_limit_check(struct td_engine *eng)
{
	uint64_t next_seq, oldest_seq, allowed;
	int64_t seq_diff;

	oldest_seq = td_engine_oldest_active_sequence(eng);
	if (oldest_seq == -1ULL)
		return true;

	next_seq = td_engine_peek_next_sequence(eng);

	td_eng_trace(eng, TR_SEQ, "next_seq  ", next_seq);
	td_eng_trace(eng, TR_SEQ, "oldest_seq", oldest_seq);

	seq_diff = next_seq - oldest_seq;

	td_eng_trace(eng, TR_SEQ, "seq_diff  ", seq_diff);

	allowed = td_eng_conf_var_get(eng, FW_SEQ_WINDOW);

	return seq_diff < allowed;
}


/** return true if the device is not accepting any more BIOs */
static inline int td_state_no_bio(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_BIO_DRAIN:
	case TD_RUN_STATE_UCMD_ONLY:
		return 1;
	default:
		return 0;
	}
}

/** a check for an upgrade to stop ECC counters.  Not really a good check, but
 * works right now. */
static inline int td_state_upgrade_in_progress(struct td_engine *eng)
{
	return (eng->bio_context ? 1 : 0);
}

/** return true if the device can transition to running state. */
static inline int td_state_can_enter_running(struct td_engine *eng)
{
	return (eng->bio_context ? 0 : 1);
}

/** return true if the device is accepting BIOs */
static inline int td_state_can_accept_requests(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_INIT:
	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_FW_PROBE:
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_OOO_SEQUENCE:
	case TD_RUN_STATE_BIO_DRAIN:
	case TD_RUN_STATE_UCMD_ONLY:
		return 1;
	default:
		return 0;
	}
}

/** return true if the device is entering or in a power management state */
static inline int td_state_pm(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_PM_DRAIN:
	case TD_RUN_STATE_PM_SHUTDOWN:
	case TD_RUN_STATE_PM_SLEEP:
		return 1;
	default:
		return 0;
	}
}

/** return true if the device can enter a pm state */
static inline int td_state_can_enter_pm(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_PM_SLEEP:
		return 1;
	default:
		return 0;
	}
}

/** return true if the device can query mcefree_status */
static inline int td_state_can_query_mcefree_status(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_DEAD:
		return 0;
	default:
		return 1;
	}
}

/** return true if the device is accepting more commands */
static inline int td_state_can_talk_to_hw(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_FW_PROBE:
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_OOO_SEQUENCE:
	case TD_RUN_STATE_UCMD_ONLY:
		return 1;
	default:
		return 0;
	}
}

/** return true if the device is purging read buffers */
static inline int td_state_is_purging_read_buffers(struct td_engine *eng)
{
	return td_run_state_check(eng, READ_PURGE);
}

/** return true if the device is waiting for firmware probe to finish */
static inline int td_state_is_probing_firmware(struct td_engine *eng)
{
	return td_run_state_check(eng, FW_PROBE);
}

/** return true if the device is draining before a sync */
static inline int td_state_is_pre_sync(struct td_engine *eng)
{
	return td_run_state_check(eng, PRE_SYNC_IO);
}

/** return true if the device is performing a sync IO */
static inline int td_state_is_sync(struct td_engine *eng)
{
	return td_run_state_check(eng, SYNC_IO);
}

/** return true if the device is currently re-sequencing with firmware */
static inline int td_state_is_ooo(struct td_engine *eng)
{
	return td_run_state_check(eng, OOO_SEQUENCE);
}

/** return true if the device can start queued requests */
static inline int td_state_can_start_requests(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_PM_DRAIN:
	case TD_RUN_STATE_PM_SHUTDOWN:
	case TD_RUN_STATE_BIO_DRAIN:
	case TD_RUN_STATE_UCMD_ONLY:
		return 1;
	default:
		return 0;
	}
}

static inline int td_state_can_start_io_requests(struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_PM_DRAIN:
	case TD_RUN_STATE_PM_SHUTDOWN:
		return 1;
	default:
		return 0;
	}
}

static inline int td_state_can_issue_monitor_requests (struct td_engine *eng)
{
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_RUNNING:
	case TD_RUN_STATE_PRE_SYNC_IO:
	case TD_RUN_STATE_SYNC_IO:
	case TD_RUN_STATE_PM_SHUTDOWN:
	case TD_RUN_STATE_BIO_DRAIN:
	case TD_RUN_STATE_UCMD_ONLY:
		return 1;
	default:
		return 0;
	}

}

/* token stuff */

static inline enum td_token_type td_tok_type_from_tokid(struct td_engine *eng,
		unsigned tokid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	unsigned hw_start = td_eng_conf_var_get(eng, HW_TOKID_START);
	if (tokid < hw_start)
		return TD_TOK_FOR_FW;
	return TD_TOK_FOR_HW;
#else
	return TD_TOK_SAME_TYPE_FOR_EVERYTHING;
#endif
}

/** this function is used to sum return codes over all token types */
static inline unsigned td_tok_type_sum(struct td_engine *eng,
		unsigned (*func)(struct td_engine *eng, enum td_token_type tt))
{
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	enum td_token_type tt;
	unsigned ret = 0;

	for_each_token_type(tt)
		ret += func(eng, tt);

	return ret;
#else
	/* with no token types, it's just a single call */
	return func(eng, TD_TOK_SAME_TYPE_FOR_EVERYTHING);
#endif
}


/** return number of active tokens */
static inline unsigned td_active_rmw_tokens(struct td_engine *eng)
{
	return eng->td_active_rmw_tokens_count;
}

/** return number of free tokens, use td_available_tokens() instead */
static inline unsigned __td_free_tokens(struct td_engine *eng, enum td_token_type tt)
{
	return eng->tok_pool[tt].td_free_tokens.count;
}

/** return number of active tokens */
static inline unsigned td_active_tokens(struct td_engine *eng, enum td_token_type tt)
{
	return eng->tok_pool[tt].td_active_tokens.count;
}
#define td_all_active_tokens(eng) td_tok_type_sum(eng,td_active_tokens)

static inline unsigned td_active_bio_count(struct td_engine *eng)
{
	return td_active_tokens(eng, TD_TOK_FOR_FW)
		- (unsigned int)eng->td_queued_ucmd_count;
}

/** return number of timedout tokens */
static inline unsigned td_timedout_tokens(struct td_engine *eng, enum td_token_type tt)
{
	return eng->tok_pool[tt].td_timedout_tokens.count;
}
#define td_all_timedout_tokens(eng) td_tok_type_sum(eng,td_timedout_tokens)

/** return number of resumable tokens */
static inline unsigned td_resumable_tokens(struct td_engine *eng, enum td_token_type tt)
{
	return eng->tok_pool[tt].td_resumable_tokens.count;
}
#define td_all_resumable_tokens(eng) td_tok_type_sum(eng,td_resumable_tokens)

/** return number of available tokens */
static inline unsigned td_available_tokens(struct td_engine *eng, enum td_token_type tt)
{
	return __td_free_tokens(eng, tt);
}
#define td_all_available_tokens(eng) td_tok_type_sum(eng,td_available_tokens)

/** return number of partially completed read tokens */
static inline unsigned td_early_completed_reads(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	return eng->td_early_completed_reads_tokens.count;
#else
	return 0;
#endif
}

static inline unsigned td_noupdate_tokens(struct td_engine *eng)
{
	return eng->td_counters.token.noupdate_cnt;
}

#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
/* number of tokens that could be sent before limiting kicks in */
static inline uint64_t td_noupdate_headroom(struct td_engine *eng)
{
	uint64_t limit, count;

	limit = td_eng_conf_var_get(eng, NOUPDATE_CMD_LIMIT);
	if (!limit)
		return TD_TOKENS_PER_DEV;

	count = eng->td_counters.token.noupdate_cnt;

	return (limit > count) ? (limit - count) : 0;
}
#endif

static inline void td_token_awaits_update(struct td_engine *eng,
		struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
	uint64_t limit, count;
	BITMAP_POINTER(map, eng->td_no_update_map);

	/* already in flight? */
	if (test_bit(tok->tokid, map))
		return;

	__set_bit(tok->tokid, map);

	count = eng->td_counters.token.noupdate_cnt ++;

	limit = td_eng_conf_var_get(eng, NOUPDATE_CMD_LIMIT);
	if (limit && count >= limit) 
		eng->td_counters.token.noupdate_limit_reached ++;
#endif
}

static inline void td_token_received_update(struct td_engine *eng,
		struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
	BITMAP_POINTER(map, eng->td_no_update_map);

	/* not in flight? */
	if (!test_bit(tok->tokid, map))
		return;

	__clear_bit(tok->tokid, map);
	eng->td_counters.token.noupdate_cnt --;
#endif
}


/* keep track of polling loops */

static inline void td_engine_enter_polling_loop(struct td_engine *eng)
{
	if (!eng->td_polling_loop_count) {
		td_eng_trace(eng, TR_CMD, "polling-loop-start",
				td_all_active_tokens(eng));
	}

	eng->td_polling_loop_count ++;
}

static inline void td_engine_exit_polling_loop(struct td_engine *eng)
{
	if (!eng->td_polling_loop_count)
		return;

	td_eng_trace(eng, TR_CMD, "polling-loop-end",
			eng->td_polling_loop_count);

	eng->td_polling_loop_count = 0;
}



/* token list management helpers */

/** find a free token */
static inline struct td_token *td_alloc_token(struct td_engine *eng,
		enum td_token_type tt)
{
	struct td_token *tok = NULL;

again:
	
	if (!td_available_tokens(eng, tt))
		return NULL;

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	tok = td_tokens_dequeue(&eng->tok_pool[tt].td_free_tokens);
#else
	tok = td_tokens_dequeue(&eng->td_free_tokens);
#endif

	td_eng_trace(eng, 32, "TD_ENG:alloc:tok", tok->tokid);

	/* HACK: this needs to be resolved in a better way
	 * 
	 * in TD mode, we seem to be getting 512 tokens in the free token list
	 */
	if (tok->tokid >= td_eng_conf_var_get(eng, TOKENS)) {
		td_eng_trace(eng, 32, "TD_ENG:alloc:tok:skip", tok->tokid);
		td_eng_err(eng, "tokid(%u) > allowed max(%llu)\n", tok->tokid,
				td_eng_conf_var_get(eng, TOKENS));
		goto again;
	}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	switch (tok->tokid) {
	case 255:
		td_eng_err(eng, "allocation of token %u is skipped\n", tok->tokid);
		goto again;
	default:
		break;
	}
#endif

	return tok;
}

/** return token if it's on the free list */
static inline struct td_token *td_alloc_token_id(struct td_engine *eng,
		uint16_t tokid)
{
	struct td_token *tok, *nxt;
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	enum td_token_type tt;
#endif
	struct td_token_list *tok_list;

	td_free_tokens_lock(eng);

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	tt = td_tok_type_from_tokid(eng, tokid);
	tok_list = &eng->tok_pool[tt].td_free_tokens;
#else
	tok_list = &eng->td_free_tokens;
#endif

	for_each_token_list_token(tok, nxt, tok_list) {

		if (likely (tokid != tok->tokid))
			continue;

		__td_tokens_del(tok_list, tok);
		goto bail;
	}

	tok = NULL;

bail:
	td_free_tokens_unlock(eng);

	return tok;
}

static inline void td_free_token(struct td_engine *eng,
		struct td_token *tok)
{
	enum td_token_type tt;

	tt = td_token_type(tok);

	/* if this token is waiting for status updates, it's
	 * not going to see them if it's being released, but
	 * it would count against noupdate throttling. */
	td_token_received_update(eng, tok);

	/* completion is done, clear the function pointer */
	tok->ops.completion = NULL;
	
	/* Done with cmd_bytes */
	memset(tok->cmd_bytes, 0, sizeof(tok->cmd_bytes));

	switch (tok->result) {
	case TD_TOK_RESULT_TIMEOUT:
		/* timeouts can be held for recovery, based on config option */
		if (td_eng_conf_var_get(eng, HOLD_TIMEDOUT_TOKENS)) {
			td_eng_trace(eng, TR_TOKEN, "TD_ENG:hold:tok",
					tok->tokid);
			/* this token needs to be pushed for future cleanup */
			td_tokens_enqueue(&eng->tok_pool[tt].td_timedout_tokens, tok);
			return;
		}

		/* fall through desired */
	default:
		td_eng_trace(eng, TR_TOKEN, "TD_ENG:free:tok", tok->tokid);
		/* token can be reused right away */
		if (unlikely (td_eng_conf_var_get(eng, QUICK_TOK_REUSE)))
			td_tokens_push(&eng->tok_pool[tt].td_free_tokens, tok);
		else
			td_tokens_enqueue(&eng->tok_pool[tt].td_free_tokens, tok);
	}
}

/** number of available hardware commands */
static inline uint td_available_hardware_commands(struct td_engine *eng)
{
	int avail;

	avail = (int)(td_eng_conf_var_get(eng, HW_CMDS) - eng->td_hw_cmds_used);

	return avail < 0 ? 0 : avail;
}

static inline void td_increment_hardware_command(struct td_engine *eng) {
	eng->td_hw_cmds_used--;
}

static inline void td_decrement_hardware_command(struct td_engine *eng) {
	eng->td_hw_cmds_used++;
}
/** number of available core buffers */
static inline uint td_available_core_buffers(struct td_engine *eng)
{
	int avail;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	avail = eng->td_corebufs_available_count;
#else
	avail = (int)(td_eng_conf_var_get(eng, CORE_BUFS) - eng->td_core_buf_used);
#endif

	return avail < 0 ? 0 : avail;
}

/** number of available write entry points */
static inline uint td_available_write_buffers(struct td_engine *eng)
{
	int avail;

	avail = (int)(td_eng_conf_var_get(eng, HOST_WRITE_BUFS) - eng->td_wr_buf_used);

	return avail < 0 ? 0 : avail;
}

static inline void td_engine_update_token_sent_timestamps(struct td_engine *eng,
		struct td_token *tok)
{
	if (td_token_is_hw(tok))
		eng->td_last_hardware_command = td_get_cycles();

	if (td_token_is_write(tok))
		eng->td_last_write_command_for_dealloc
			= eng->td_last_write_command_for_fwstatus
			= td_get_cycles();

	if (td_token_is_deallocate(tok))
		eng->td_last_deallocate_command = td_get_cycles();
}




/* core buffer management helpers */

/** reset mapping between core buffers and who uses them */
static inline void td_reset_core_buffer_tracking(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	uint core_bufid, max;

	INIT_LIST_HEAD(&eng->td_corebufs_available);
	eng->td_corebufs_available_count = 0;

	max = td_eng_conf_var_get(eng, CORE_BUFS);

	for (core_bufid=0; core_bufid<TD_CORE_BUFS_PER_DEV; core_bufid++) {
		struct td_corebuf *corebuf;
		
		corebuf = eng->td_corebufs + core_bufid;

		memset(corebuf, 0, sizeof(struct td_corebuf));

		corebuf->cb_core_bufid = core_bufid;
		corebuf->cb_used_by_tokid = TD_INVALID_TOKEN_ID;

		if (core_bufid < max) {
			list_add_tail(&corebuf->cb_lru_link, &eng->td_corebufs_available);
			eng->td_corebufs_available_count ++;
			corebuf->cb_available = 1;
		}
	}
#else
	INIT_BITMAP(eng->td_core_buf_used_map, TD_CORE_BUFS_PER_DEV);
#endif
}

static inline unsigned td_lookup_token_for_core_buffer(struct td_engine *eng, uint core_bufid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_corebuf *corebuf;

	if (!TD_IS_CORE_BUFID_VALID(core_bufid))
		return TD_INVALID_TOKEN_ID;

	corebuf = eng->td_corebufs + core_bufid;

	return corebuf->cb_used_by_tokid;
#else
	return TD_INVALID_TOKEN_ID;
#endif
}

/** remember when a token started to use a core buffer */
static inline void td_start_token_core_usage(struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_engine *eng;
	struct td_corebuf *corebuf;

	if (!TD_IS_CORE_BUFID_VALID(tok->core_bufid))
		return;

	eng = tok->td_engine;
	corebuf = eng->td_corebufs + tok->core_bufid;

	corebuf->cb_used_by_tokid = tok->tokid;
#endif
}

/** clear the link between core buffer and the token that uses it */
static inline void td_end_token_core_usage(struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_engine *eng;
	struct td_corebuf *corebuf;

	if (!TD_IS_CORE_BUFID_VALID(tok->core_bufid))
		return;

	eng = tok->td_engine;
	corebuf = eng->td_corebufs + tok->core_bufid;

	corebuf->cb_used_by_tokid = TD_INVALID_TOKEN_ID;
#endif
}

/** reserve a core buffer so that it's not used for general allocation */
static inline int td_reserve_core_buffer(struct td_engine *eng, uint core_bufid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_corebuf *corebuf;

	if (!TD_IS_CORE_BUFID_VALID(core_bufid))
		return -EINVAL;

	corebuf = eng->td_corebufs + core_bufid;
	if (!corebuf->cb_available)
		return -EBUSY;

	list_del(&corebuf->cb_lru_link);
	eng->td_corebufs_available_count --;
	corebuf->cb_available = 0;

	return 0;

#else
	BITMAP_POINTER(map, eng->td_core_buf_used_map);
	if (!TD_IS_CORE_BUFID_VALID(core_bufid))
		return -EINVAL;
	if (test_bit(core_bufid, map))
		return -EBUSY;
	set_bit(core_bufid, map);
	eng->td_core_buf_used ++;
	return 0;
#endif
}


/** allocate and return a core buffer ID,
 * returns buffer ID, or TD_INVALID_BUFID if none are available */
static inline uint16_t td_alloc_core_buffer(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_corebuf *corebuf;

	if (unlikely(list_empty(&eng->td_corebufs_available)))
		return TD_INVALID_CORE_BUFID;

	corebuf = list_first_entry(&eng->td_corebufs_available,
			struct td_corebuf, cb_lru_link);
	list_del(&corebuf->cb_lru_link);
	eng->td_corebufs_available_count --;

	corebuf->cb_available = 0;

	return corebuf->cb_core_bufid;

#else
	BITMAP_POINTER(map, eng->td_core_buf_used_map);
	uint max, bit;

	max = (uint)td_eng_conf_var_get(eng, CORE_BUFS);

	bit = find_first_zero_bit(map, max);
	if (likely (bit < max)) {
		__set_bit(bit, map);
		eng->td_core_buf_used ++;
		return (uint16_t)bit;
	}

	return TD_INVALID_CORE_BUFID;
#endif
}

/** release the previously allocated core buffer */
static inline void td_free_core_buffer(struct td_engine *eng,
		uint16_t bufid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_corebuf *corebuf;

	corebuf = eng->td_corebufs + bufid;

	WARN_ON(corebuf->cb_available);
	if (unlikely(corebuf->cb_available))
		return;

	list_add_tail(&corebuf->cb_lru_link, &eng->td_corebufs_available);
	eng->td_corebufs_available_count ++;
	corebuf->cb_available = 1;
#else
	BITMAP_POINTER(map, eng->td_core_buf_used_map);

	if (!TD_IS_CORE_BUFID_VALID(bufid))
		return;

	__clear_bit(bufid, map);
	eng->td_core_buf_used --;
#endif
}

/** allocate a range of core buffer IDs,
 * start of 1 or larger, size + start > max -1
 * returns last buffer ID, or TD_INVALID_BUFID if all of the buffers are not available */
static inline uint16_t td_alloc_core_buffer_range(struct td_engine *eng,
		uint8_t start, uint8_t size)
{
#ifndef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	BITMAP_POINTER(map, eng->td_core_buf_used_map);
	uint bit = 0;
#endif

	uint max = (uint) td_eng_conf_var_get(eng, CORE_BUFS);
	uint16_t end = start + size;
	uint16_t offset;

	td_eng_debug(eng, "CoreBufferAllocRange: start %u to %u, size %u\n",
			start, end-1, size);

	if (end > max) {
		td_eng_err(eng, "CoreBufferAllocRange: Invalid end: %d > %d\n", end, max);
		goto inval_end;
	}

	if (!size || size > td_available_core_buffers(eng)) {
		td_eng_err(eng, "CoreBufferAllocRange: %d  buffers not available\n", size);
		goto inval_num;
	}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	for (offset = start; offset < end; offset++) {
		struct td_corebuf *corebuf = eng->td_corebufs + offset;
		if (!corebuf->cb_available) {
			td_eng_err(eng, "CoreBufferAllocRange: Can't reserve %u, "
					"already taken\n", offset);
			return TD_INVALID_CORE_BUFID;
		}
	}

	for (offset = start; offset < end; offset++) {
		struct td_corebuf *corebuf = eng->td_corebufs + offset;

		list_del(&corebuf->cb_lru_link);
		eng->td_corebufs_available_count --;
		corebuf->cb_available = 0;
	}

	/* return last buffer id allocated */
	return offset-1;

#else
	for (offset = start; offset < end; offset++) {
		bit = find_next_zero_bit(map, max, offset);
		if (unlikely (bit != offset)) {
			td_eng_err(eng, "CoreBufferAllocRange: Can't reserve %u, "
					"already taken; next free is %u\n",
					offset, bit);
			goto bit_taken;
		}
		/* We know bit < max. */
		__set_bit(bit, map);
		eng->td_core_buf_used++;
	}

	return (uint16_t)bit;

bit_taken:
	/* undo! Up to offset has been set. */
	for(bit = start; bit < offset; bit++) {
		td_eng_info(eng, "CoreBufferAllocRange: undo %u\n", bit);
		__clear_bit(bit, map);
		eng->td_core_buf_used--;
	}
#endif

inval_num:
inval_end:
	return TD_INVALID_CORE_BUFID;
}

/** allocate and return a write entry point buffer,
 * returns buffer ID, or TD_INVALID_BUFID if non are available */
static inline uint8_t td_alloc_wr_buffer(struct td_engine *eng)
{
	BITMAP_POINTER(map, eng->td_wr_buf_used_map);
	uint max, bit;

	max = (uint)td_eng_conf_var_get(eng, HOST_WRITE_BUFS);

	bit = find_first_zero_bit(map, max);
	if (likely (bit < max)) {
		__set_bit(bit, map);
		eng->td_wr_buf_used ++;
		return (uint8_t)bit;
	}

	return TD_INVALID_WR_BUFID;
}

/** release the previously allocated write entry point buffer */
static inline void td_free_wr_buffer(struct td_engine *eng,
		uint8_t bufid)
{
	BITMAP_POINTER(map, eng->td_wr_buf_used_map);

	if (!TD_IS_WR_BUFID_VALID(bufid))
		return;

	__clear_bit(bufid, map);
	eng->td_wr_buf_used --;
}

/** allocation helper, grabs a token, core buffer, write entry point, etc */
static inline struct td_token *td_alloc_token_with_buffers(
		struct td_engine *eng, enum td_token_type tt,
		uint host_to_dev_len, uint dev_to_host_len)
{
	struct td_token *tok = NULL;
	uint core_bufid = -1;
	uint wr_bufid = -1;

	if (host_to_dev_len > TERADIMM_DATA_BUF_SIZE
			|| dev_to_host_len > TERADIMM_DATA_BUF_SIZE)
		return NULL;

	if (!td_available_tokens(eng, tt))
		goto error_no_tokens_availabe;

	if (host_to_dev_len || dev_to_host_len) {
		core_bufid = td_alloc_core_buffer(eng);
		if (!TD_IS_CORE_BUFID_VALID(core_bufid))
			goto error_no_core_buffers;
	}

	if (host_to_dev_len) {
		wr_bufid = td_alloc_wr_buffer(eng);
		if (!TD_IS_WR_BUFID_VALID(wr_bufid))
			goto error_no_wr_buffers;
	}

	tok = td_alloc_token(eng, tt);
	if (!tok)
		goto error_alloc_token;

	td_token_reset(tok);

	tok->len_host_to_dev = (uint16_t)host_to_dev_len;
	tok->len_dev_to_host = (uint16_t)dev_to_host_len;
	tok->core_bufid = (uint16_t)core_bufid;
	tok->wr_bufid = (uint8_t)wr_bufid;

	td_start_token_core_usage(tok);

	return tok;

error_alloc_token:
	td_free_wr_buffer(eng, (uint8_t)wr_bufid);
error_no_wr_buffers:
	td_free_core_buffer(eng, (uint16_t)core_bufid);
error_no_core_buffers:
error_no_tokens_availabe:
	return NULL;
}

/* Allocation helper, grabs a token and write entry point */
static inline struct td_token *td_alloc_token_when_locked(
		struct td_engine *eng, enum td_token_type tt,
		uint host_to_dev_len, uint dev_to_host_len)
{
	struct td_token *tok = NULL;
	uint wr_bufid = -1;

	if (host_to_dev_len > TERADIMM_DATA_BUF_SIZE
			|| dev_to_host_len > TERADIMM_DATA_BUF_SIZE)
		return NULL;

	if (!td_available_tokens(eng, tt))
		goto error_no_tokens_availabe;

	if (host_to_dev_len) {
		wr_bufid = td_alloc_wr_buffer(eng);
		if (!TD_IS_WR_BUFID_VALID(wr_bufid))
			goto error_no_wr_buffers;
	}

	tok = td_alloc_token(eng, tt);
	if (!tok)
		goto error_alloc_token;

	td_token_reset(tok);

	tok->len_host_to_dev = (uint16_t)host_to_dev_len;
	tok->len_dev_to_host = (uint16_t)dev_to_host_len;
	tok->core_bufid = TD_INVALID_CORE_BUFID;
	tok->wr_bufid = (uint8_t)wr_bufid;

	return tok;

error_alloc_token:
	td_free_wr_buffer(eng, (uint8_t)wr_bufid);
error_no_wr_buffers:
error_no_tokens_availabe:
	return NULL;

}

/** allocation helper, grabs a token, core buffer, write entry point, and 
 * kernel page for transferring data */
static inline struct td_token *td_alloc_token_with_host_page(
		struct td_engine *eng, enum td_token_type tt,
		uint host_to_dev_len, uint dev_to_host_len)
{
	struct page *page;
	struct td_token *tok;

	if (host_to_dev_len > PAGE_SIZE
			|| dev_to_host_len > PAGE_SIZE)
		return NULL;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		goto error_no_page_available;

	tok = td_alloc_token_with_buffers(eng, tt,
			host_to_dev_len, dev_to_host_len);
	if (!tok)
		goto error_alloc_token;

	tok->host.page = page;
	tok->host_buf_virt = page_address(page);
	tok->copy_ops = eng->td_virt_copy_ops;

	return tok;

error_alloc_token:
	__free_page(page);
error_no_page_available:
	return NULL;
}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
static inline void td_remove_rdbuf_from_circulatiion(struct td_engine *eng,
		uint rd_bufid)
{
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	if (unlikely (rdbuf->rb_needs_dealloc)) {
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_needing_deallocation_count --;
		rdbuf->rb_needs_dealloc = 0;
	}

	if (rdbuf->rb_in_rush) {
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_in_rush_count --;
		rdbuf->rb_in_rush = 0;
	}
}
#endif

/** mark a read buffer ID as one that needs deallocation */
static inline void td_schedule_rdbuf_deallocation(struct td_engine *eng,
		uint rd_bufid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	/* never deallocate read buffer zero in read-buffer status mode */
	if (unlikely (td_eng_conf_var_get(eng, MCEFREE_FWSTATUS)
				== TD_MCEFREE_FWSTATUS_RDBUF0
			&& rd_bufid == 0)) {

		td_remove_rdbuf_from_circulatiion(eng, rd_bufid);
		return;
	}

	if (unlikely (rdbuf->rb_in_rush)) {
		/* already deallocated, put it at the tail of the list */
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_in_rush_count --;
		rdbuf->rb_in_rush = 0;
		WARN_ON(rdbuf->rb_needs_dealloc);
	}

	if (unlikely (rdbuf->rb_needs_dealloc))
		/* already pending deallocation -- nothing to do */
		return;

	list_add_tail(&rdbuf->rb_lru_link, &eng->td_rdbufs_needing_deallocation);
	eng->td_rdbufs_needing_deallocation_count ++;
	rdbuf->rb_needs_dealloc = 1;

#else
	BITMAP_POINTER(map, eng->td_rdbuf_needs_dealloc_map);

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);

	__set_bit(rdbuf, map);
#endif
}

/** return true if any deallocation must be done */
static inline int td_pending_rdbuf_deallocations(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	return eng->td_rdbufs_needing_deallocation_count;
#else
	BITMAP_POINTER(map, eng->td_rdbuf_needs_dealloc_map);

	uint max, bit;
	max = TD_HOST_RD_BUFS_PER_DEV;
	bit = find_first_bit(map, max);
	return bit < max;
#endif
}

/** return a buffer to deallocate next */
static inline uint __td_next_rdbuf_to_deallocate(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_rdbuf *rdbuf;

	if (unlikely(list_empty(&eng->td_rdbufs_needing_deallocation)))
		return TD_INVALID_RD_BUFID;

	rdbuf = list_first_entry(&eng->td_rdbufs_needing_deallocation,
			struct td_rdbuf, rb_lru_link);
	list_del(&rdbuf->rb_lru_link);
	eng->td_rdbufs_needing_deallocation_count --;
	rdbuf->rb_needs_dealloc = 0;

	return rdbuf->rb_rd_bufid;

#else
	BITMAP_POINTER(map, eng->td_rdbuf_needs_dealloc_map);
	uint bufid, bit, max;

	max = TD_HOST_RD_BUFS_PER_DEV;
	bit = find_first_bit(map, max);

	/* !? */
	bufid = TD_INVALID_RD_BUFID;
	if (likely (bit < max)) {
		bufid = bit;
		__clear_bit(bit, map);
	}

	return bufid;
#endif
}

/** return true if engine is using status from WEP-7, and read buffers are now tagged */
static inline int td_eng_using_fwstatus(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	return td_eng_conf_var_get(eng, MCEFREE_FWSTATUS);
#else
	return 0;
#endif
}

/** this one throttles to 1 deallocation at a time */
static inline uint td_next_rdbuf_to_deallocate(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/* how many are waiting for deallocation */
	uint waiting = eng->td_rdbufs_needing_deallocation_count;

	/* throttling deallocations until 32 are used */
	if (td_eng_using_fwstatus(eng)
			&& waiting < 32
			&& !td_run_state_check(eng, TERMINATING))
		return TD_INVALID_RD_BUFID;
#endif
	return __td_next_rdbuf_to_deallocate(eng);
}

/** indicate that the deallocation was sent off */
static inline void td_rdbuf_deallocation_sent(struct td_engine *eng,
		uint rd_bufid)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	if (unlikely (rdbuf->rb_needs_dealloc)) {
		/* item still on the td_rdbufs_needing_deallocation list */
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_needing_deallocation_count --;
		rdbuf->rb_needs_dealloc = 0;
		WARN_ON(rdbuf->rb_in_rush);
	}

	if (unlikely (rdbuf->rb_in_rush)) {
		/* already deallocated, put it at the tail of the list */
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_in_rush_count --;
	}

	list_add_tail(&rdbuf->rb_lru_link, &eng->td_rdbufs_in_rush);
	eng->td_rdbufs_in_rush_count ++;
	rdbuf->rb_in_rush = 1;

#endif
}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
static inline uint td_next_rdbuf_expected(struct td_engine *eng)
{
	struct td_rdbuf *rdbuf;

again:
	td_eng_trace(eng, TR_MCEFREE, "fwstatus:expected:count   ", eng->td_rdbufs_in_rush_count);

	if (unlikely(list_empty(&eng->td_rdbufs_in_rush)))
		return TD_INVALID_RD_BUFID;

	rdbuf = list_first_entry(&eng->td_rdbufs_in_rush,
			struct td_rdbuf, rb_lru_link);

	td_eng_trace(eng, TR_MCEFREE, "fwstatus:expected:rd_bufid", rdbuf->rb_rd_bufid);

	/* special case, skip buffer 0 */
	if (unlikely(td_eng_conf_var_get(eng, MCEFREE_FWSTATUS)
				== TD_MCEFREE_FWSTATUS_RDBUF0
			&& rdbuf->rb_rd_bufid == 0)) {
		td_remove_rdbuf_from_circulatiion(eng, 0);
		goto again;
	}

	return rdbuf->rb_rd_bufid;
}

static inline uint td_rdbuf_last_used_by_core_bufid(struct td_engine *eng,
		uint rd_bufid)
{
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return 0;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	return rdbuf->rb_last_core_bufid;
}

static inline uint64_t td_rdbuf_last_used_by_cmd_word(struct td_engine *eng,
		uint rd_bufid)
{
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return 0;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	return rdbuf->rb_last_cmd_word;
}

static inline void td_claim_expected_rdbuf(struct td_engine *eng,
		uint rd_bufid, uint related_core_bufid, uint64_t related_cmd_word)
{
	struct td_rdbuf *rdbuf;

	td_eng_trace(eng, TR_MCEFREE, "fwstatus:claim:rd_bufid  ", rd_bufid);
	td_eng_trace(eng, TR_MCEFREE, "fwstatus:claim:core_bufid", related_core_bufid);
	td_eng_trace(eng, TR_MCEFREE, "fwstatus:claim:cmd_word  ", related_cmd_word);

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	td_eng_trace(eng, TR_MCEFREE, "fwstatus:claim:prev_core ", rdbuf->rb_last_core_bufid);
	td_eng_trace(eng, TR_MCEFREE, "fwstatus:claim:prev_cmd  ", rdbuf->rb_last_cmd_word);

	rdbuf->rb_last_core_bufid = related_core_bufid;
	rdbuf->rb_last_cmd_word = related_cmd_word;

	WARN_ON(!rdbuf->rb_in_rush);
	if (rdbuf->rb_in_rush) {
		list_del(&rdbuf->rb_lru_link);
		eng->td_rdbufs_in_rush_count --;
		rdbuf->rb_in_rush = 0;
	}
}
#endif

/** return true if engine will be deallcating read buffers one at a time */
static inline int td_eng_rdbuf_throttling(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	switch (td_eng_using_fwstatus(eng)) {
	default:
		return 0;
	case TD_MCEFREE_FWSTATUS_WEP7:
		if (eng->td_rdbufs_needing_deallocation_count < 31)
			return 0;
		break;
	case TD_MCEFREE_FWSTATUS_RDBUF0:
		if (eng->td_rdbufs_needing_deallocation_count < 30)
			return 0;
		break;
	}
	return 1;
#else
	return 0;
#endif
}

static inline void td_reset_rdbuf_deallocation(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	uint rd_bufid, max;

	INIT_LIST_HEAD(&eng->td_rdbufs_needing_deallocation);
	eng->td_rdbufs_needing_deallocation_count = 0;

	INIT_LIST_HEAD(&eng->td_rdbufs_in_rush);
	eng->td_rdbufs_in_rush_count = 0;

	max = td_eng_conf_var_get(eng, HOST_READ_BUFS);

	for (rd_bufid=0; rd_bufid<TD_HOST_RD_BUFS_PER_DEV; rd_bufid++) {
		struct td_rdbuf *rdbuf = eng->td_rdbufs + rd_bufid;

		memset(rdbuf, 0, sizeof(struct td_rdbuf));

		rdbuf->rb_rd_bufid = rd_bufid;
		rdbuf->rb_needs_dealloc = 0;
		rdbuf->rb_last_core_bufid = TD_INVALID_CORE_BUFID;
		rdbuf->rb_last_cmd_word = 0;

		if (rd_bufid < max) {
			list_add_tail(&rdbuf->rb_lru_link, &eng->td_rdbufs_in_rush);
			eng->td_rdbufs_in_rush_count ++;
			rdbuf->rb_in_rush = 1;
		}
	}

#else
	INIT_BITMAP(eng->td_rdbuf_needs_dealloc_map, TD_HOST_RD_BUFS_PER_DEV);
#endif
}

/** send the next throttled deallocate */
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
extern int td_eng_send_next_throttled_rdbuf_deallocate(struct td_engine *eng);
#else
#define td_eng_send_next_throttled_rdbuf_deallocate(eng) do { /* nothing */ } while(0)
#endif

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
static inline void td_engine_stage_early_read(struct td_engine *eng,
		struct td_token *tok)
{
	/* first entry added resets the progress timer */
	if (!eng->td_early_completed_reads_tokens.count)
		eng->td_early_completed_reads_last_progress = td_get_cycles();

	/* update the timeout */
	td_set_token_timeout(tok,
			td_eng_conf_mcefree_var_get(eng, TOKEN_TIMEOUT_USEC));

	tok->early_read = 1;
	td_tokens_enqueue(&eng->td_early_completed_reads_tokens, tok);
}
#endif

static inline unsigned td_engine_queued_bio_reads(struct td_engine *eng)
{
	return (unsigned)eng->td_incoming_bio_reads
	     + (unsigned)eng->td_queued_bio_reads;
}

static inline unsigned td_engine_queued_bio_writes(struct td_engine *eng)
{
	return (unsigned)eng->td_incoming_bio_writes
	     + (unsigned)eng->td_queued_bio_writes;
}


/** return total number of bios */
static inline unsigned td_engine_queued_bios(struct td_engine *eng)
{
	unsigned ret = td_engine_queued_bio_reads(eng)
		     + td_engine_queued_bio_writes(eng)
	     ;

	return ret;
}

/** return total number of control messages */
static inline unsigned td_engine_queued_ucmds(struct td_engine *eng)
{
	return (unsigned int)eng->td_queued_ucmd_count;
}

/** return total number of commands waiting to run */
static inline unsigned td_engine_queued_commands(struct td_engine *eng)
{
	return td_engine_queued_bios(eng)
	     + td_engine_queued_ucmds(eng);
}

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
/** Return numbe of queued tasks waiting to run */
static inline unsigned td_engine_queued_tasks(struct td_engine *eng)
{
	return eng->td_queued_task_count;
}
#endif

static inline int td_engine_has_dg_work (struct td_engine *eng)
{
	return !! td_atomic_ptr_read(&eng->td_safe_work);
}

/** return number of commands and deallocations left to run */
static inline unsigned td_engine_queued_work(struct td_engine *eng)
{
	unsigned count = td_engine_queued_commands(eng);

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
	count += td_engine_queued_tasks(eng);
#endif

	if (!td_eng_rdbuf_throttling(eng))
		count += td_pending_rdbuf_deallocations(eng);

	return count;
}

/** update read buffer least-recently-used tracking */
static inline void td_seen_read_complete(struct td_engine *eng,
		uint rd_bufid)
{
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	struct td_rd_buf *rb;
	bool rb_is_lru, used_before;

	if (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV)
		return;

	rb = eng->td_rd_bufs + rd_bufid;

	/*  update the usage timestamp */
	rb->drb_last_used = jiffies;

	/*  this entry was likely used before */
	used_before = !!rb->drb_lru_link.next;

	/*  figure out if the read buffer which was used was LRU/MRU */
	rb_is_lru = rb->drb_lru_link.prev == &eng->td_rd_buf_lru;

	if (rb_is_lru) {
		/*  there will be a new LRU, reset the counters */
		eng->td_rd_buf_lru_seen_other_reads = 0;
		eng->td_rd_buf_lru_seen_read_timeouts = 0;

	} else {
		/*  LRU remains the same, increment other-read counter */
		eng->td_rd_buf_lru_seen_other_reads ++;
	}

	/*  migrate to the MRU position */
	if (used_before)
		list_del(&rb->drb_lru_link);
	list_add_tail(&rb->drb_lru_link, &eng->td_rd_buf_lru);
#endif
}

/** update read-buffer tracking to indicate a read timed out */
static inline void td_seen_read_timeout(struct td_engine *eng,
		struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	eng->td_rd_buf_lru_seen_read_timeouts ++;
#endif
}

/** check the read buffer tracking database and try to clean up read buffers
 * return number of orphans that were recovered */
extern int td_engine_recover_read_buffer_orphans(struct td_engine *eng);

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
/** return a valid read buffer ID, if it has not been used recently, but it's
 * seen other tokens go by while it was last in the queue */
static inline uint td_get_read_buffer_orphan(struct td_engine *eng)
{
	struct td_rd_buf *lru, *mru;
	uint64_t lru_age, mru_age;
	bool bad_order=0, timeout_thr, other_thr, stale_mru;

	/*  skip if there are no entries */
	if (list_empty(&eng->td_rd_buf_lru))
		return TD_INVALID_RD_BUFID;

	lru = list_first_entry(&eng->td_rd_buf_lru,
			struct td_rd_buf, drb_lru_link);

	mru = list_last_entry(&eng->td_rd_buf_lru,
			struct td_rd_buf, drb_lru_link);

	if (lru == mru)
		return TD_INVALID_RD_BUFID;

	/*  skip if it hasn't been waiting long enough */
	lru_age = jiffies_to_msecs(jiffies - lru->drb_last_used);
	if (lru_age <= td_eng_conf_var_get(eng, ORPHAN_RDBUF_REQ_MSEC))
		return TD_INVALID_RD_BUFID;

	/*  if LRU ID < MRU ID, the read buffers are suspicious */
	if (td_eng_conf_var_get(eng, ORPHAN_RDBUF_ORDER_CHECK))
		bad_order = lru->drb_rd_bufid < mru->drb_rd_bufid;

	/*  if LRU seen enough other reads occur, something could be wrong */
	other_thr = eng->td_rd_buf_lru_seen_other_reads
			>= td_eng_conf_var_get(eng, ORPHAN_RDBUF_REQ_CNT);

	if (bad_order && other_thr)
		goto found_orphan;

	/*  if LRU seen enough timeouts, something could be wrong */
	timeout_thr = eng->td_rd_buf_lru_seen_read_timeouts
			>= td_eng_conf_var_get(eng, ORPHAN_RDBUF_TIMEOUT_CNT);

	if (!bad_order && timeout_thr)
		goto found_orphan;

	/*  check MRU age too to properly evaluate timeouts observed */
	mru_age = jiffies_to_msecs(jiffies - mru->drb_last_used);
	stale_mru = mru_age > td_eng_conf_var_get(eng, ORPHAN_RDBUF_REQ_MSEC);

	if (timeout_thr && stale_mru)
		goto found_orphan;

	/*  no orphan */
	return TD_INVALID_RD_BUFID;

found_orphan:
	td_eng_trace(eng, 32, "orphan:found", lru->drb_rd_bufid);
	td_eng_trace(eng, 32, "orphan:age  ", lru_age);
	td_eng_trace(eng, 32, "orphan:TOes ", eng->td_rd_buf_lru_seen_read_timeouts);
	td_eng_trace(eng, 32, "orphan:seen ", eng->td_rd_buf_lru_seen_other_reads);
	td_eng_trace(eng, 32, "orphan:last ", lru->drb_last_failed);

	/*  reset counters */
	eng->td_rd_buf_lru_seen_other_reads = 0;
	eng->td_rd_buf_lru_seen_read_timeouts = 0;
	lru->drb_last_used = jiffies;

	/*  update it's state in the LRU list */
	list_del(&lru->drb_lru_link);
	list_add_tail(&lru->drb_lru_link, &eng->td_rd_buf_lru);

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
	lru->drb_was_orphaned ++;
#endif

	return lru->drb_rd_bufid;
}
#endif

static inline void td_free_wr_buffers(struct td_engine *eng,
		struct td_token *tok)
{
	if (TD_IS_WR_BUFID_VALID(tok->wr_bufid)) {
		td_free_wr_buffer(eng, tok->wr_bufid);
		tok->wr_bufid = TD_INVALID_WR_BUFID;
	}

	if (TD_IS_WR_BUFID_VALID(tok->extra_wr_bufid)) {
		td_free_wr_buffer(eng, tok->extra_wr_bufid);
		tok->extra_wr_bufid = TD_INVALID_WR_BUFID;
	}
}

/** release token buffers */
static inline void td_free_all_buffers(struct td_engine *eng,
		struct td_token *tok)
{
	if (TD_IS_CORE_BUFID_VALID(tok->core_bufid)) {
		td_end_token_core_usage(tok);
		td_free_core_buffer(eng, tok->core_bufid);
		tok->core_bufid = TD_INVALID_CORE_BUFID;
	}

	td_free_wr_buffers(eng, tok);

	if (TD_IS_RD_BUFID_VALID(tok->rd_bufid)) {
		td_schedule_rdbuf_deallocation(eng, tok->rd_bufid);
		tok->rd_bufid = TD_INVALID_RD_BUFID;
	}

	if (tok->host.page) {
		__free_page(tok->host.page);
		tok->host.page = NULL;
		tok->host_buf_virt = NULL;
	}
}

/** return true if bios are queued and LUNs are available */
static inline int td_can_start_work_now(struct td_engine *eng)
{
	int pre_sync_flushing = 0;
	int sync_mode = 0;
	unsigned avail_toks, active_toks, avail_core_bufs, bio_writes;
	td_bio_ref bio;

	/* need a token to start anything */
	avail_toks = td_all_available_tokens(eng);
	if (!avail_toks)
		return 0;

	avail_core_bufs = td_available_core_buffers(eng);

	/* control messages can be sent regardless of sync mode */
	if (td_engine_queued_ucmds(eng) && avail_core_bufs)
		return 1;

	/* we have work to do if we have pending deallocations */
	if (td_pending_rdbuf_deallocations(eng))
		return 1;

	/* some things are state dependent */
	switch (td_run_state(eng)) {
	case TD_RUN_STATE_READ_PURGE:
	case TD_RUN_STATE_RUNNING:
		/* deffult case, start any command if there are resources */
		break;
	case TD_RUN_STATE_PRE_SYNC_IO:
		/* preparing for sync/ordered IO mode, wait for things to drain out */
		pre_sync_flushing = 1;
		break;
	case TD_RUN_STATE_SYNC_IO:
		/* sync/ordered IO mode, can only start if there is nothing active */
		sync_mode = 1;
		break;
	default:
		/* other states cannot start work */
		return 0;
	}

	/* don't start any IO in sync mode */
	active_toks = td_active_tokens(eng, TD_TOK_FOR_FW);
	if (sync_mode && active_toks)
		return 0;

	/* checking for read start is easy */
	if (td_engine_queued_bio_reads(eng) && avail_core_bufs)
		return 1;

	/* checking for write start takes a few steps */
	bio_writes = td_engine_queued_bio_writes(eng);

	/* if there are no write bios and tokens, or there are no
	 * write buffers, or no write entry points, then bail */
	if (!bio_writes || !avail_core_bufs
		   || !td_available_write_buffers(eng))
		return 0;

	if (bio_list_empty(&eng->td_queued_bios))
		td_migrate_incoming_to_queued(eng);

	/* have an incomplete burst and the next bio is a write */
	bio = bio_list_peek(&eng->td_queued_bios);
	if (bio && td_bio_is_write(bio))
		return 1;

	/* there is no more work until something happens:
	 *  - get a write bio
	 *  - get a token
	 *  - get a write buffer
	 *  - delayed writes (if any) expire
	 */

	return 0;
}


static inline uint64_t td_engine_capacity(struct td_engine* eng)
{
	/* We can't set the capacity to the full raw capacity, because the
	 * last set of sectors need to be striped properly too.  If the total
	 * SSD_SECTOR_COUNT on each device isn't a proper multlipe of the
	 * stripe lbas, we have a "hole" between the end of the first monet,
	 * and where they start on the next device.
	 */
	return td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE)
		* (td_eng_conf_hw_var_get(eng, SSD_SECTOR_COUNT) & ~(td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS)-1))
		* td_eng_conf_hw_var_get(eng, SSD_COUNT);
}

/*
 * The "safe work" API is a way to guarentee work is run in a device group
 * context.  If we are in that context, we can just run the function.  If we
 * aren't, we pass it off to the engine to run in devgroup context.
 */

typedef int (*td_eng_thread_work_func_t)(struct td_engine *, void*);
struct td_eng_thread_work {
	td_eng_thread_work_func_t func;
	const char* name;
	struct completion work_done;
	int result;
	int verbose;
};

int __td_eng_run_safe_work (struct td_engine *eng, struct td_eng_thread_work *work);
static inline int td_eng_safe_work(struct td_engine* eng, struct td_eng_thread_work* work)
{
	struct td_devgroup *dg;
	int rc, cpu, socket;

	cpu = get_cpu();

#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	for (socket = 0; socket < td_socketmap_size; socket++) {
		if (cpu <= td_socketmap[socket])
			break;
	}
#else
	socket = topology_physical_package_id(cpu);
#endif

 	dg = td_engine_devgroup(eng);
	BUG_ON(!dg);

	if (socket == dg->dg_socket) {
		/* we can execute it here, it's the right socket */
		if (work->verbose)
			td_eng_info(eng,
				"Running work \"%s\" immediately on CPU %u\n",
				work->name, cpu);
		rc = work->func(eng, work);

		put_cpu();

		return rc;
	}

	put_cpu();

	return __td_eng_run_safe_work(eng, work);
}	




static inline void td_counter_inc_in_flight(struct td_engine *eng,
		struct td_token *tok, int rw)
{
#ifdef CONFIG_TERADIMM_LINUX_BLOCK_STATS
	if (!!tok->host.bio) {
#if defined(part_stat_lock)
		int cpu = part_stat_lock();
		struct hd_struct *part = disk_map_sector_rcu(
				td_engine_device(eng)->td_disk,
				tok->host.bio->bio_sector);
		part_inc_in_flight(part, rw);
		part_stat_unlock();
#else
		disk_round_stats(td_engine_device(eng)->td_disk);
		td_engine_device(eng)->td_disk->in_flight++;
#endif
	}
#endif
	tok->ts_start = td_get_cycles();
	if (rw)
		eng->td_stats.write.req_active_cnt ++;
	else
		eng->td_stats.read.req_active_cnt ++;

}

static inline void td_counter_dec_in_flight(struct td_engine *eng,
		struct td_token *tok, int rw)
{
#ifdef CONFIG_TERADIMM_LINUX_BLOCK_STATS
	if (!!tok->host.bio) {
#if defined(part_stat_lock)
		int cpu = part_stat_lock();
		struct hd_struct *part = disk_map_sector_rcu(
				td_engine_device(eng)->td_disk,
				tok->host.bio->bio_sector);
		part_dec_in_flight(part, rw);
		part_stat_unlock();
#else
		disk_round_stats(td_engine_device(eng)->td_disk);
		td_engine_device(eng)->td_disk->in_flight--;
#endif
	}
#endif
	tok->ts_end = td_get_cycles();
	if (rw)
		eng->td_stats.write.req_active_cnt --;
	else
		eng->td_stats.read.req_active_cnt --;
}


static inline void __td_token_error_dump(struct td_token *tok, const char* subject)
{
	pr_err("%s: %s tok=%u, hw=%d, cmd=%16llx, tokseq=%llu, odd=%d, rd=%u, "
		"wr=%u, core=%u, result=%d, status=0x%02x, extstatus=0x%016llx,free=0x%02x, "
		"resets=%u, retries=%u)\n",
		td_eng_name(td_token_engine(tok)), subject,
		tok->tokid, tok->hw, tok->cmd_bytes[0], tok->cmd_seq, tok->odd,
		tok->rd_bufid, tok->wr_bufid, tok->core_bufid,
		tok->result, tok->last_status, tok->last_xstatus,
		tok->free_rd_bufid, tok->reset_count, tok->retry_count);
	//td_dump_data(KERN_DEBUG, &tok->cmd_bytes, sizeof(tok->cmd_bytes));
}

static inline void td_token_error_dump(struct td_token *tok, const char* subject)
{
	__td_token_error_dump(tok, subject);
	if (tok->sec_buddy)
		__td_token_error_dump(tok->sec_buddy, "SEC DUP");
}




#endif
