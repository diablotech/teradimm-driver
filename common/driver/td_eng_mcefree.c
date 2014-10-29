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

#include "td_kdefn.h"
#include "td_compat.h"

#include "td_engine.h"
#include "td_token.h"
#include "td_token_list.h"
#include "td_eng_mcefree.h"
#include "td_eng_hal.h"
#include "td_device.h"
#include "td_devgroup.h"
#include "td_util.h"
#include "td_protocol.h"

#ifndef CONFIG_TERADIMM_MCEFREE_FWSTATUS
#error this file should only be compiled into an MCEFREE driver
#endif

static void td_mcefree_debug_dump(struct td_engine *eng);

#ifdef CONFIG_TERADIMM_MCEFREE_RDBUF_HOLD_STATS
/* collects min/avg/max each second and makes them available in /sys
 * the following shell code can be used to dump them to the screen...
 *
 *	parms=/sys/module/teradimm/parameters/
 *	(
 *	echo -n "device,"
 *	tdctl --dl | xargs | sed 's/ /   ,/g'
 *	for n in {error,delay}_{min,avg,max} ; do
 *		echo -n "$n,"
 *		cat $parms/$n
 *	done
 *	) | column -s, -t
 */
#define MAX_STAT_DEVS 8
static int td_max_stat_devs = MAX_STAT_DEVS;

static ulong td_error_min[MAX_STAT_DEVS] = { 0, };
static ulong td_error_avg[MAX_STAT_DEVS] = { 0, };
static ulong td_error_max[MAX_STAT_DEVS] = { 0, };

static ulong td_delay_min[MAX_STAT_DEVS] = { 0, };
static ulong td_delay_avg[MAX_STAT_DEVS] = { 0, };
static ulong td_delay_max[MAX_STAT_DEVS] = { 0, };

static cycles_t td_last_updated[MAX_STAT_DEVS] = { 0, };

module_param_array_named(error_min, td_error_min, ulong, &td_max_stat_devs, 0444);
module_param_array_named(error_avg, td_error_avg, ulong, &td_max_stat_devs, 0444);
module_param_array_named(error_max, td_error_max, ulong, &td_max_stat_devs, 0444);

module_param_array_named(delay_min, td_delay_min, ulong, &td_max_stat_devs, 0444);
module_param_array_named(delay_avg, td_delay_avg, ulong, &td_max_stat_devs, 0444);
module_param_array_named(delay_max, td_delay_max, ulong, &td_max_stat_devs, 0444);

static uint64_t td_priv_error_min[MAX_STAT_DEVS] = { 0, };
static uint64_t td_priv_error_max[MAX_STAT_DEVS] = { 0, };
static uint64_t td_priv_error_ttl[MAX_STAT_DEVS] = { 0, };

static uint64_t td_priv_error_start[MAX_STAT_DEVS] = { 0, };

static uint64_t td_priv_delay_min[MAX_STAT_DEVS] = { 0, };
static uint64_t td_priv_delay_max[MAX_STAT_DEVS] = { 0, };
static uint64_t td_priv_delay_ttl[MAX_STAT_DEVS] = { 0, };

static uint32_t td_priv_update_cnt[MAX_STAT_DEVS] = { 0, };

static void update_rdbuf_stats(struct td_engine *eng, uint64_t error, uint64_t hold_delay)
{
	const char *name;
	unsigned dev_index;
	uint64_t err_this_sec;
	cycles_t now;
	uint64_t diff, msec;

	name = td_eng_name(eng);

	if (name[0] != 't' || name[1] != 'd' || !name[2])
		return;
	dev_index = name[2] - 'a';

	if (dev_index >= MAX_STAT_DEVS)
		return;

	err_this_sec = error - td_priv_error_start[dev_index];

	switch (td_priv_update_cnt[dev_index]) {
	default:
		td_priv_error_ttl[dev_index] += err_this_sec;
		td_priv_delay_ttl[dev_index] += hold_delay;
		td_priv_update_cnt[dev_index] ++;

		if (td_priv_error_min[dev_index] > err_this_sec)
			td_priv_error_min[dev_index] = err_this_sec;
		if (td_priv_error_max[dev_index] < err_this_sec)
			td_priv_error_max[dev_index] = err_this_sec;

		if (td_priv_delay_min[dev_index] > hold_delay)
			td_priv_delay_min[dev_index] = hold_delay;
		if (td_priv_delay_max[dev_index] < hold_delay)
			td_priv_delay_max[dev_index] = hold_delay;

		break;

	case 0:
		td_priv_update_cnt[dev_index] = 1;
		td_priv_error_ttl[dev_index]
			= td_priv_error_min[dev_index]
			= td_priv_error_max[dev_index] = err_this_sec;
		td_priv_delay_ttl[dev_index]
			= td_priv_delay_min[dev_index]
			= td_priv_delay_max[dev_index] = hold_delay;
		break;
	}

	if (!td_priv_update_cnt[dev_index])
		return;

	now = td_get_cycles();
	diff = now - td_last_updated[dev_index];
	msec = td_cycles_to_msec(diff);
	if (msec < 1000)
		return;

	td_error_min[dev_index] = td_priv_error_min[dev_index];
	td_error_max[dev_index] = td_priv_error_max[dev_index];
	td_error_avg[dev_index] = td_priv_error_ttl[dev_index]
		/ td_priv_update_cnt[dev_index];

	td_delay_min[dev_index] = td_priv_delay_min[dev_index];
	td_delay_max[dev_index] = td_priv_delay_max[dev_index];
	td_delay_avg[dev_index] = td_priv_delay_ttl[dev_index]
		/ td_priv_update_cnt[dev_index];

	td_last_updated[dev_index] = now;
	td_priv_error_start[dev_index] = error;

	td_priv_error_min[dev_index] = 0;
	td_priv_error_max[dev_index] = 0;
	td_priv_error_ttl[dev_index] = 0;
	td_priv_delay_min[dev_index] = 0;
	td_priv_delay_max[dev_index] = 0;
	td_priv_delay_ttl[dev_index] = 0;
	td_priv_update_cnt[dev_index] = 0;
}
#else
static void update_rdbuf_stats(struct td_engine *eng, uint64_t error, uint64_t hold_delay) {}
#endif


/* tried reading a read buffer, but found stale data */
static void mcefree_rdbuf_marker_error_occured(struct td_engine *eng)
{
	uint64_t val, max;

	eng->td_counters.misc.rdbuf_marker_error_cnt ++;
	eng->td_last_rdbuf_marker_error = td_get_cycles();

	/* current value */
	val = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_NSEC);

	/* climb because we got an error */
	val += td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_CLIMB_NSEC);

	/* cap it at the ceiling */
	max = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_MAX_NSEC);
	val = min_t(uint64_t, val, max);

	/* put it back */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_NSEC, val);

	/* next time, we will wait longer */
}

/* read buffer read correctly on the first try */
static void mcefree_rdbuf_marker_successfully_read(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t msec = 0, cooling;
	uint64_t val, min;

	now = td_get_cycles();
	diff = now - eng->td_last_rdbuf_marker_error;
	msec = td_cycles_to_msec(diff);
	cooling = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_COOL_MSEC);

	/* if cool-off has not been reached, do nothing */
	if (cooling && msec < cooling)
		return;

	/* cooling off */

	/* current value */
	val = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_NSEC);

	/* drop the hold time */
	val -= td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_DROP_NSEC);

	/* cap it at the floor */
	min = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_MIN_NSEC);
	val = max_t(int64_t, val, min);

	/* put it back */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_NSEC, val);

	/* next time, we will wait for less time */

	/* also don't decrement right away */
	eng->td_last_rdbuf_marker_error = td_get_cycles();
}

/* avoid the read buffer? */
static bool mcefree_rdbuf_hold_in_progress(struct td_engine *eng,
		unsigned rd_bufid)
{
	cycles_t now, diff;
	uint64_t nsec = 0, max;
	struct td_rdbuf *rdbuf;

	WARN_ON(rd_bufid >= TD_HOST_RD_BUFS_PER_DEV);
	if (unlikely (rd_bufid >= TD_HOST_RD_BUFS_PER_DEV))
		return false;

	rdbuf = &eng->td_rdbufs[rd_bufid];

	if (!rdbuf->rb_last_dealloc)
		return false;

	now = td_get_cycles();
	diff = now - rdbuf->rb_last_dealloc;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_NSEC);

	if (max && nsec < max)
		return true;

	/* we were holding back, but no more */

	rdbuf->rb_last_dealloc = 0;
	td_eng_trace(eng, TR_CMD, "hold-back-rdbuf-done", diff);

	return false;
}

struct mcefree_completion_knowledge {
	struct td_engine *engine;

	struct list_head completed;

	/* working variables */
	td_cmd_t cmd;
	uint rdbuf;
	uint last_core;
	uint this_core;
	uint tokid;

	/* counters */
	uint finished;
	uint loops;
};

static void mcefree_completion_init(struct mcefree_completion_knowledge *mck,
		struct td_engine *eng)
{
	memset(mck, 0, sizeof(*mck));

	INIT_LIST_HEAD(&mck->completed);

	mck->engine       = eng;

	mck->rdbuf     = TD_INVALID_RD_BUFID;
	mck->last_core = TD_INVALID_CORE_BUFID;
	mck->this_core = TD_INVALID_CORE_BUFID;
	mck->tokid     = TD_INVALID_TOKEN_ID;
}

#define MCEFREE_MATCHED_CORE 1 /* old way */
#define MCEFREE_MATCHED_CMD  2 /* new way */
static int mcefree_decode_rdbuf_marker_to_corebuf(
		struct mcefree_completion_knowledge *mck,
		const void *metadata, bool complain)
{
	struct td_engine *eng = mck->engine;
	const uint64_t *p = metadata;
	uint8_t guess;
	uint64_t word;
	uint i;

	mck->this_core = TD_INVALID_CORE_BUFID;
	mck->cmd.cmd.u64 = 0;

	/* first word is the string McEfree or McEfre2 */
	if (0==memcmp(metadata, "McEfre2", 8)) {

		/* the encoding is:
		 * p[0] - "McEfre2"
		 * p[1] - cmd[0]   ... cmd word
		 * p[2] - cmd[1]   ... cmd source
		 * p[3] - cmd[2]   ... cmd dest
		 * p[4] - "McEfre2"
		 * p[5] - cmd[0]
		 * p[6] - cmd[1]
		 * p[7] - cmd[2]
		 */

		/* first 4 and last 4 words are copies */

		if (memcmp(p, p+4, 8*4))
			goto missmatch;

		/* this looks like a valid encoding
		 * copy out the command word */
		memcpy(&mck->cmd, p+1, 8*3);

		return MCEFREE_MATCHED_CMD;

	} else if (0==memcmp(metadata, "McEfree", 8)) {

		guess = p[1] & 0xFF; /* byte 64 is the guess, rest has to match it */

		/* next 7 words are the core buffer ID */

		word = guess * 0x0101010101010101;
		for (i=1; i<=7; i++) {
			if (p[i] != word)
				goto missmatch;
		}

		/* the read buffer matches core buffer marker */
		mck->this_core = guess;

		return MCEFREE_MATCHED_CORE;
	}

	/* no match */

missmatch:
	if (complain) {
		td_eng_err(eng, "RDBUF %u invalid marker found\n",
				mck->rdbuf);
		td_dump_data("    ", metadata, 64);
	}
	return -EINVAL;
}

static int mcefree_match_token(struct mcefree_completion_knowledge *mck,
		bool last_one)
{
	struct td_engine *eng = mck->engine;
	struct td_token *match_tok = NULL;
	uint64_t last_cmd;
	int match_mode;
	int rc;

	/* read the data */
	rc = td_eng_hal_read_rdbuf_metadata(eng, mck->rdbuf, 64,
			eng->td_rdbuf_metadata, 64);
	if (rc<0) {
		td_eng_err(eng, "failed to read metadata for "
				"read buffer %d\n", mck->rdbuf);
		return -EIO;
	}

	if (last_one) {
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-0  ", ((uint64_t*)eng->td_rdbuf_metadata)[0]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-1  ", ((uint64_t*)eng->td_rdbuf_metadata)[1]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-2  ", ((uint64_t*)eng->td_rdbuf_metadata)[2]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-3  ", ((uint64_t*)eng->td_rdbuf_metadata)[3]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-4  ", ((uint64_t*)eng->td_rdbuf_metadata)[4]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-5  ", ((uint64_t*)eng->td_rdbuf_metadata)[5]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-6  ", ((uint64_t*)eng->td_rdbuf_metadata)[6]);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:meta-7  ", ((uint64_t*)eng->td_rdbuf_metadata)[7]);
	}

	/* parse it, to see what core buffer it belongs to */
	match_mode = mcefree_decode_rdbuf_marker_to_corebuf(mck,
			eng->td_rdbuf_metadata, last_one);

	switch (match_mode) {
	case MCEFREE_MATCHED_CMD:
		goto match_using_command;

	case MCEFREE_MATCHED_CORE:
		goto match_using_core;

	default:
		/* something went wrong, try again later */
		return -EAGAIN;
	}

match_using_command:
	/* this one looks different and valid */

if (!last_one) {
td_eng_trace(eng, TR_RDBUF, "find:via-cmd:meta-0  ", ((uint64_t*)eng->td_rdbuf_metadata)[0]);
td_eng_trace(eng, TR_RDBUF, "find:via-cmd:meta-1  ", ((uint64_t*)eng->td_rdbuf_metadata)[1]);
td_eng_trace(eng, TR_RDBUF, "find:via-cmd:meta-2  ", ((uint64_t*)eng->td_rdbuf_metadata)[2]);
td_eng_trace(eng, TR_RDBUF, "find:via-cmd:meta-3  ", ((uint64_t*)eng->td_rdbuf_metadata)[3]);
td_eng_trace(eng, TR_RDBUF, "find:via-cmd:cmd     ", mck->cmd.cmd.u64);
}

	/* validate command */

	/* index in command must be valid */
	mck->tokid = mck->cmd.cmd.index;
	if (!TD_IS_TOKEN_ID_VALID(mck->tokid)) {
		td_eng_trace(eng, TR_RDBUF, "find:via-cmd:bad-tokid", mck->tokid);
		goto cannot_get_token_from_marker;
	}

	/* looks like a valid command word */

	WARN_ON(mck->tokid >= TD_TOKENS_PER_DEV);
	match_tok = eng->td_tokens + mck->tokid;

	/* check if the command from the marker matches the token's command */
	if (mck->cmd.cmd.u64 != match_tok->cmd_bytes[0]) {
		td_eng_trace(eng, TR_RDBUF, "find:via-cmd:mismatch-cmd", match_tok->cmd_bytes[0]);
		goto cannot_get_token_from_marker;
	}

	/* we can also check the second word */
	if (mck->cmd.src.u64 != match_tok->cmd_bytes[1]) {
		td_eng_trace(eng, TR_RDBUF, "find:via-cmd:mismatch-src", match_tok->cmd_bytes[1]);
		goto cannot_get_token_from_marker;
	}

	return 0;

cannot_get_token_from_marker:

	if (!last_one)
		return -EAGAIN;

	/* be more verbose on the last mis-match */

td_eng_trace(eng, TR_RDBUF, "ERROR:no-match:rdbuf", mck->rdbuf);
td_eng_trace(eng, TR_RDBUF, "ERROR:no-match:tok  ", mck->tokid);

	last_cmd = td_rdbuf_last_used_by_cmd_word(eng, mck->rdbuf);

	if (mck->cmd.cmd.u64 && mck->cmd.cmd.u64 != last_cmd) {
		td_eng_err(eng,
			"RDBUF %u finished, "
			"CORE was %u, "
			"CMD %016llx, "
			"TOK %u, "
			"does not match command in flight "
			"[%16llx,%u]\n",
			mck->rdbuf, mck->last_core,
			mck->cmd.cmd.u64, mck->tokid,
			match_tok ? match_tok->cmd_bytes[0] : -1LL,
			match_tok ? match_tok->core_bufid : -1);
		td_mcefree_debug_dump(eng);
	}

	return -EINVAL;


match_using_core:

if (!last_one) {
td_eng_trace(eng, TR_RDBUF, "find:via-core:meta-0  ", ((uint64_t*)eng->td_rdbuf_metadata)[0]);
td_eng_trace(eng, TR_RDBUF, "find:via-core:meta-1  ", ((uint64_t*)eng->td_rdbuf_metadata)[1]);
td_eng_trace(eng, TR_RDBUF, "find:via-core:meta-7  ", ((uint64_t*)eng->td_rdbuf_metadata)[7]);
td_eng_trace(eng, TR_RDBUF, "find:via-core:core    ", mck->this_core);
}

	if (!TD_IS_CORE_BUFID_VALID(mck->this_core))
		goto cannot_get_match_from_core;

	/* when matching core index, there is a chance that we see the
	* same one over and over again.  keep trying until the last
	* attempt */
	if (mck->last_core == mck->this_core && ! last_one) {
		td_eng_trace(eng, TR_RDBUF, "find:via-core:stale", mck->this_core);
		goto cannot_get_match_from_core;
	}

	/* find a token that claims to be using this core buffer */
	mck->tokid = td_lookup_token_for_core_buffer(eng, mck->this_core);
	if (!TD_IS_TOKEN_ID_VALID(mck->tokid)) {
		td_eng_trace(eng, TR_RDBUF, "find:rdbuf:no-token", mck->this_core);
		goto cannot_get_match_from_core;
	}

	return 0;

cannot_get_match_from_core:

	return -EAGAIN;
}

/* attempt to make forward progress on matching up read buffers with tokens
 * return 0 on success, or negative error otherwise */
static int mcefree_find_next_read(struct mcefree_completion_knowledge *mck)
{
	struct td_engine *eng = mck->engine;
	struct td_token *match_tok;
	int i, rc;
	uint64_t retry_nsec;
	bool first_try;
	unsigned dealloc_tokid;

	/* emergency token cleanup can kick in */
#define MCK_LOW_HW_TOK 16
	if (td_available_tokens(eng, TD_TOK_FOR_HW) < MCK_LOW_HW_TOK) {
td_eng_trace(eng, TR_RDBUF, "emergency-token-cleanup", td_available_tokens(eng, TD_TOK_FOR_HW));
		rc = td_eng_hal_read_status(eng, TD_TOK_FOR_HW);
		if (rc == TD_ENG_STATUS_GOOD)
			td_engine_io_complete(eng, TD_TOK_FOR_HW);
	}

	++ mck->loops;
#define MCK_LOOPS_MAX 150
	if (mck->loops >= MCK_LOOPS_MAX) {
td_eng_trace(eng, TR_RDBUF, "find:loops         ", mck->loops);
		if (mck->loops == MCK_LOOPS_MAX)
			td_eng_err(eng, "mcefree: looped too many times\n");
		return -ETIMEDOUT;
	}

	/* need to scan read buffer metadata now */

	/* expecting the data to arrive in this buffer next */
	mck->rdbuf = td_next_rdbuf_expected(eng);
td_eng_trace(eng, TR_RDBUF, "find:rdbuf:rdbuf   ", mck->rdbuf);
	if (!TD_IS_RD_BUFID_VALID(mck->rdbuf)) {
		return -EBUSY;
	}

	/* we are expecting a read to arrive on a valid read buffer; now need
	 * to make sure that the deallocate didn't happen too recently */
	if (mcefree_rdbuf_hold_in_progress(eng, mck->rdbuf)) {
		if (!td_engine_jit_deallocations(eng))
			return -EAGAIN;
		while(mcefree_rdbuf_hold_in_progress(eng, mck->rdbuf)) ;
	}

	/* this is the last core buffer associated with this read buffer */
	mck->last_core = td_rdbuf_last_used_by_core_bufid(eng, mck->rdbuf);

	td_eng_trace(eng, TR_RDBUF, "find:rdbuf:lastcore", mck->last_core);

	/* read the metadata and try to determine the core buffer */
	retry_nsec = td_eng_conf_mcefree_var_get(eng, RDBUF_RETRY_NSEC);

#define MCK_REREAD_MARKER 10
	first_try = true;
	for (i=MCK_REREAD_MARKER-1; i>=0; i--) {

		bool last_one = (i==0);

		rc = mcefree_match_token(mck, last_one);

		/* found what we wanted */
		if (!rc)
			break;

		/* unless we were told to go again, bail out now */
		if (rc != -EAGAIN || last_one) {
			mcefree_rdbuf_marker_error_occured(eng);
			return rc;
		}

		/* we will continue, but it's not the first try */
		first_try = false;

		/* wait a bit and try again */
		if (retry_nsec)
			ndelay(retry_nsec);
	}

	/* this is the token that actually has data in rd_bufid */
	WARN_ON(mck->tokid >= TD_TOKENS_PER_DEV);
	match_tok = eng->td_tokens + mck->tokid;

td_eng_trace(eng, TR_RDBUF, "find:match:tokid   ", match_tok->tokid);
td_eng_trace(eng, TR_RDBUF, "find:match:result  ", match_tok->result);
td_eng_trace(eng, TR_RDBUF, "find:match:status  ", match_tok->last_status);
td_eng_trace(eng, TR_RDBUF, "find:match:bio     ", (long)match_tok->host.bio);
td_eng_trace(eng, TR_RDBUF, "find:match:cmd     ", match_tok->cmd_bytes[0]);

	/* in order to qualify it must be a read */
	if (!td_token_is_read(match_tok)) {
		td_eng_trace(eng, TR_RDBUF, "find:rdbuf:not-a-read",
				mck->tokid);
		/* assumption here is that the data is stale, and will
		 * recover if we come back a bit later */
		mcefree_rdbuf_marker_error_occured(eng);
		return -EINVAL;
	}

	/* found a match on the first attempt... awesome */
	if (first_try)
		mcefree_rdbuf_marker_successfully_read(eng);
	else
		mcefree_rdbuf_marker_error_occured(eng);

	/* if the token was on the early completed read list, migrate it to
	 * the completed list */
	if (match_tok->early_read) {
		td_eng_trace(eng, TR_RDBUF, "find:match:current ", match_tok->tokid);
		td_tokens_del(&eng->td_early_completed_reads_tokens, match_tok);
		match_tok->early_read = 0;
		list_add_tail(&match_tok->link, &mck->completed);
	} else if (match_tok->result == TD_TOK_RESULT_ACTIVE) {
		/* update anyways, but cannot complete it */
		td_eng_trace(eng, TR_RDBUF, "find:match:future  ", match_tok->tokid);
	} else {
		td_eng_trace(eng, TR_RDBUF, "find:match:unknown ", match_tok->tokid);
		return -EINVAL;
	}

	/* the fact that we saw the read buffer be filled, indicates that a
	 * deallocate of that buffer was successful.  so if there is a
	 * deallocate in flight for this read buffer, it can be assumes to
	 * have completed */
	dealloc_tokid = eng->td_active_rdbuf_deallocation_token[mck->rdbuf];
	if (TD_IS_TOKEN_ID_VALID(dealloc_tokid)) {
		struct td_token *dtok;

		/* complete the token if it's really a deallocate */

		dtok = eng->td_tokens + dealloc_tokid;
		if (td_token_is_deallocate(dtok)
				&& dtok->independent_deallocate
				&& dtok->result == TD_TOK_RESULT_ACTIVE) {

			enum td_token_type dtype;
			struct td_token_list *alist;

			td_eng_trace(eng, TR_RDBUF, "find:complete:dealloc:tok",
					dealloc_tokid);

			/* it was on the active list, remove it */
			dtype = td_token_type(dtok);
			alist = &eng->tok_pool[dtype].td_active_tokens;
			td_tokens_del(alist, dtok);

			/* update counters */
			eng->td_stats.control.req_active_cnt --;
			eng->loop_counters.completed.control ++;

			td_tok_event(dtok, TD_TOK_EVENT_RDMETA, mck->rdbuf);

			/* complete the deallocate */
			dtok->last_status = 0;
			dtok->result = TD_TOK_RESULT_OK;
			dtok->ops.completion(dtok);

			/* we didn't see the command complete, but the result
			 * of it completing... */
			td_token_received_update(eng, dtok);
		}

		/* clear the fact that it was deallocating this read buffer */

		eng->td_active_rdbuf_deallocation_token[mck->rdbuf]
			= TD_INVALID_TOKEN_ID;
	}

	/* a read buffer was found to contain a core buffer marker */
	td_claim_expected_rdbuf(eng, mck->rdbuf, mck->this_core, mck->cmd.cmd.u64);

	/* update token using this core buffer */
	match_tok->rd_bufid = mck->rdbuf;

	/* matching means, we get to stay in the loop longer */
	mck->loops = 0;

	mck->finished ++;
	eng->td_early_completed_reads_last_progress = td_get_cycles();

	return 0;
}

#if 0
	tok = td_tokens_dequeue(&eng->td_early_completed_reads_tokens);
	tok->early_read = 0;
	td_tokens_enqueue(&eng->td_early_completed_reads_tokens, tok);
#endif

/**
 * \brief advance early reads into fill reads
 *
 * @param eng - engine that has some early-completed reads
 * @param timeout_completions - list where timed out reads are placed
 * @param failed_completions - list where failed reads are placed
 * @return number of completions, or negative error
 *
 *	called when there are reads completing from FW that don't have a read
 *	buffer assigned to them yet.
 */
int __td_engine_mcefree_read_buffer_matching(struct td_engine *eng,
		struct list_head *timeout_completions,
		struct list_head *failed_completions)
{
	struct mcefree_completion_knowledge mck;
	cycles_t now, delta;
	uint64_t usec, max;
	struct td_token *tok;
	int rc = 0;

	if (!td_eng_using_fwstatus(eng))
		return -EINVAL;

	mcefree_completion_init(&mck, eng);

	/* come back when there is a read buffer deallocated */
	if (!TD_IS_RD_BUFID_VALID(td_next_rdbuf_expected(eng))) {
		if (!td_engine_jit_deallocations(eng))
			goto free_next_rdbuf;

		rc = td_eng_send_next_throttled_rdbuf_deallocate(eng);
		if (rc)
			return -EBUSY;
	}

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

td_eng_trace(eng, TR_RDBUF, "==========================", 0);

	td_eng_trace(eng, TR_RDBUF, "fwstatus:early_reads       ",
		eng->td_early_completed_reads_tokens.count);
	td_eng_trace(eng, TR_RDBUF, "fwstatus:rdbuf:in_rush     ",
		eng->td_rdbufs_in_rush_count);
	td_eng_trace(eng, TR_RDBUF, "fwstatus:rdbuf:need_dealloc",
		eng->td_rdbufs_needing_deallocation_count);

	/* first, while we are expecting read data, complete reads */

	while (eng->td_early_completed_reads_tokens.count) {

		rc = mcefree_find_next_read(&mck);
		if (rc)
			break;

		while (!list_empty(&mck.completed)) {

			tok = list_entry(mck.completed.next,
					struct td_token, link);
			list_del(&tok->link);

			td_eng_trace(eng, TR_RDBUF, "fwstatus:complete:tok      ",
					tok->tokid);

			WARN_ON(!td_token_is_read(tok));

			td_counter_dec_in_flight(eng, tok, 0);
			eng->loop_counters.completed.reads ++;

			td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_DATA);

			tok->result = TD_TOK_RESULT_OK;
			tok->ops.completion(tok);

			td_switch_task(td_engine_devgroup(eng), TD_CPU_DRV_POLL);
		}
	}

	update_rdbuf_stats(eng, eng->td_counters.misc.rdbuf_marker_error_cnt,
			td_eng_conf_mcefree_var_get(eng, RDBUF_HOLD_NSEC));

	td_eng_trace(eng, TR_RDBUF, "fwstatus:find:rc    ", rc);
	td_eng_trace(eng, TR_RDBUF, "fwstatus:early_reads",
		eng->td_early_completed_reads_tokens.count);

	if (rc == -ETIMEDOUT) {
		eng->td_counters.misc.rdbuf_matching_timeout_cnt ++;
		goto rdbuf_timeout;
	}

	/* make sure that reads are not stuck here for too long */

	if (eng->td_early_completed_reads_tokens.count) {

		/* unfortunatley we have to pop them off to check */

		tok = td_tokens_dequeue(&eng->td_early_completed_reads_tokens);

		if (td_has_token_timedout(tok)) {

			td_token_error_dump(tok, "TIMEOUT EARLY-READ");

			tok->early_read = 0;
			tok->result = TD_TOK_RESULT_TIMEOUT;

			/* this is a time out, let upper layer deal with it */
			td_eng_trace(eng, TR_RDBUF, "fwstatus:timeout:tok", tok->tokid);
			list_add_tail(&tok->link, timeout_completions);

		} else {
			/* no timeout, now put it back */
			td_tokens_push(&eng->td_early_completed_reads_tokens, tok);
		}
	}

	/* we ran out of read buffers */

	if (!td_engine_jit_deallocations(eng)
			&& list_empty(&eng->td_rdbufs_in_rush))
		goto free_next_rdbuf;

	/* if anything finished we're done */

td_eng_trace(eng, TR_RDBUF, "==========================", mck.finished);

	if (mck.finished || !eng->td_early_completed_reads_tokens.count)
		return mck.finished;

rdbuf_timeout:
	/* nothing finished, is there a timeout on the read buffer update */

	now = td_get_cycles();
	delta = now - eng->td_early_completed_reads_last_progress;
	usec = td_cycles_to_usec(delta);
	max = td_eng_conf_mcefree_var_get(eng, PROGRESS_TIMEOUT_USEC);

	if (unlikely (usec <= max))
		return 0;

	/* we get here if we have a read buffer that cannot be matched back to
	 * a completed read, and it's been like this for a while */

	td_eng_err(eng, "RDBUF %u finished, "
			"CORE %u (was %u), "
			"no token (%llu usec)\n", mck.rdbuf,
			mck.this_core, mck.last_core, usec);

	td_mcefree_debug_dump(eng);

	td_eng_trace(eng, TR_RDBUF, "fwstatus:rdbuf:timeout", usec);

	/* count the mismatch */
	eng->td_counters.misc.rdbuf_progress_timeout_cnt ++;

	td_eng_trace(eng, TR_RDBUF, "fwstatus:forced_claim:rdbuf", mck.rdbuf);

	/* this read buffer contained data for a core buffer that was not tied
	 * to a valid token, so it's being skipped */
	td_claim_expected_rdbuf(eng, mck.rdbuf, mck.this_core, 0);
	td_schedule_rdbuf_deallocation(eng, mck.rdbuf);

free_next_rdbuf:

	/* the next buffer will need to be deallocated */
	rc = td_eng_send_next_throttled_rdbuf_deallocate(eng);

	td_eng_trace(eng, TR_RDBUF, "fwstatus:forced_advance", rc);

	/* we didn't really make progress, but this avoids the next one from
	 * timing out as well */
	if (!rc)
		eng->td_early_completed_reads_last_progress = td_get_cycles();

	return mck.finished ?: -EBUSY;
}


static int td_mcefree_dump_enable = 0;
module_param_named(mcefree_dump, td_mcefree_dump_enable, uint, 0644);
MODULE_PARM_DESC(mcefree_dump, "Dump read buffer info on the next error.");

static void td_mcefree_debug_dump(struct td_engine *eng)
{
	int rd_bufid, rc;
	struct mcefree_completion_knowledge mck;
	struct td_token *tok, *nxt;
	struct td_token_pool *tok_pool;

	if (likely (!td_mcefree_dump_enable))
		return;
	else if (likely (td_mcefree_dump_enable > 0))
		td_mcefree_dump_enable --;

	mcefree_completion_init(&mck, eng);

	td_eng_warn(eng, "MCEFREE rdbuf tracking state ------------\n");

	for (rd_bufid = 0; rd_bufid < TD_HOST_RD_BUFS_PER_DEV; rd_bufid++) {
		struct td_rdbuf *rdbuf = eng->td_rdbufs + rd_bufid;

		mck.cmd.cmd.u64 = -1LL;

		rc = td_eng_hal_read_rdbuf_metadata(eng, rd_bufid, 64,
				eng->td_rdbuf_metadata, 64);
		if (!rc)
			mcefree_decode_rdbuf_marker_to_corebuf(&mck,
					eng->td_rdbuf_metadata, 0);

		td_eng_warn(eng, "rd[%u] %s %s %s lcore=%u lcmd=%016llx ccmd=%016llx\n",
				rd_bufid,
				rdbuf->rb_in_rush       ? "RUSH" : "    ",
				rdbuf->rb_needs_dealloc ? "USED" : "    ",
				(rdbuf->rb_last_cmd_word != mck.cmd.cmd.u64) ? "DIFF" : "    ",
				rdbuf->rb_last_core_bufid,
				rdbuf->rb_last_cmd_word,
				mck.cmd.cmd.u64);
	}

	td_eng_warn(eng, "active tokens ---------------------------\n");

	tok_pool = &eng->tok_pool[TD_TOK_FOR_FW];
	for_each_token_list_token(tok, nxt, &tok_pool->td_active_tokens) {
		td_token_error_dump(tok, "active");
	}

	td_eng_warn(eng, "early read tokens -----------------------\n");

	for_each_token_list_token(tok, nxt, &eng->td_early_completed_reads_tokens) {
		td_token_error_dump(tok, "early-read");
	}

	td_eng_warn(eng, "-----------------------------------------\n");
}



