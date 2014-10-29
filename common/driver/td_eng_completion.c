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

#include "td_eng_completion.h"

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>

#include "td_engine.h"
#include "td_eng_hal.h"

/* ------------------------------------------------------------------------ */

/* The following functions implement different ways of handling timeouts.
 *
 * The engine code accesses these via td_eng_hal_handle_timeouts() function
 * for which is assigned in the engine's _enable() implementation.
 */

/** used on the megadimm to force a reset on a command, and then retry it
 * all on the same token */
void td_eng_reset_retry_timeout_handler(struct td_engine *eng,
		struct list_head *resets_list,     // tokens that need resets
		struct list_head *complete_list)   // failed ones get put on this list
{
	struct td_token *tok;
	int max_resets;

	if (list_empty(resets_list))
		return;

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

	/* if it's not possible to reissue, abort all */
	if (unlikely (!td_state_can_start_requests(eng)
			&& !td_state_is_purging_read_buffers(eng))) {
		td_engine_io_abort_tok_list(eng, resets_list, complete_list);
		return;
	}

	max_resets = (int)td_eng_conf_var_get(eng, RESETS);

	/*
	 * this is a command reset after it timedout
	 */
	do {
		int rc;
		struct td_token_list *act_tok_list;

		tok = list_entry(resets_list->next,
				struct td_token, link);
		list_del(&tok->link);

		/* reset count is under limit, send another reset */
		if (++tok->reset_count > max_resets) {
			/* over limit, stop it now */
			switch (tok->result) {
			case TD_TOK_RESULT_OK:
			case TD_TOK_RESULT_ACTIVE:
				/* make sure that it's marked as a failed request */
				tok->result = TD_TOK_RESULT_FAIL_ABORT;
				break;
			}
			list_add_tail(&tok->link, complete_list);
			continue;
		}

		/* reset command needs to have opposite odd/even bit */
		tok->odd = !tok->odd;

		/* send the reset command */
		td_eng_trace(eng, TR_TOKEN, "reset:start    ", tok->tokid);
		rc = td_eng_hal_reset_token(eng, tok);
		if (rc) {
			/* failed to restart, really complete */
			tok->result = rc;
			list_add_tail(&tok->link, complete_list);
			continue;
		}
		eng->td_counters.token.resets_cnt ++;

		/* when this reset command completes, it will be retried */
		tok->needs_retry = 1;

		/* it restarted, so add it to the active list */
		tok->result = TD_TOK_RESULT_ACTIVE;

		act_tok_list = &eng->tok_pool[td_token_type(tok)].td_active_tokens;
		td_tokens_enqueue(act_tok_list, tok);

		/* restart the counter */
		td_reset_token_timeout(eng, tok);
	} while (!list_empty(resets_list));
}


/** migrate a command to another token, after a timeout */
static int td_engine_io_migrate_token(struct td_engine *eng,
				struct td_token *old)
{
	struct td_token *tok;

	tok = td_alloc_token(eng, td_token_type(old));
	if (!tok)
		return -ENOENT;

	td_eng_trace(eng, TR_TOKEN, "migrate:old:tok", old->tokid);
	td_eng_trace(eng, TR_TOKEN, "migrate:new:tok", tok->tokid);

	/* migrate data from old token to new one */
	td_token_migrate(tok, old);

	/* reissue the new one */
	/* TODO: this could be done better since the command is
	 * already created... just polarity needs to be fixed */
	td_engine_start_token(eng, tok);
	
	/* correct counters the old token was holding
	 * (which _start_token() incremented for the new token) */

	if (old->rmw)
		eng->td_active_rmw_tokens_count --;

	if (td_token_is_write(old))
		eng->td_stats.write.req_active_cnt --;
	else if (td_token_is_read(old))
		eng->td_stats.read.req_active_cnt --;
	else
		eng->td_stats.control.req_active_cnt --;

	/* success or not, the above function took care of it */
	return 0;
}



void td_eng_die_timeout_handler(struct td_engine *eng,
		struct list_head *timedout_list,   // tokens that timedout
		struct list_head *complete_list)   // failed ones get put on this list
{
	if (list_empty(timedout_list))
		return;

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

	/* mark everything as failed */
	td_engine_io_abort_tok_list(eng, timedout_list, complete_list);

	/* now kill the driver */
	td_eng_trace(eng, TR_TOKEN, "FW:TIMEOUT:HALT", 0);
	td_run_state_enter(eng, TERMINATING);
	td_terminate_all_outstanding_bios(eng, 0);
	return;
}


void td_eng_migrate_timeout_handler(struct td_engine *eng,
		struct list_head *timedout_list,   // tokens that timedout
		struct list_head *complete_list)   // failed ones get put on this list
{
	struct td_token *tok;

	if (list_empty(timedout_list))
		return;

	/* if things completed, we are done polling */
	td_engine_exit_polling_loop(eng);

	/* if it's not possible to reissue, abort all */
	if (unlikely (!td_state_can_start_requests(eng)
			&& !td_state_is_purging_read_buffers(eng))) {
		td_engine_io_abort_tok_list(eng, timedout_list, complete_list);
		return;
	}


	/*
	 * this is a command migration after it timedout
	 */
	do {
		int rc;

		tok = list_entry(timedout_list->next,
				struct td_token, link);
		list_del(&tok->link);

		if (! td_eng_hal_can_retry(eng, tok) ) {
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			list_add_tail(&tok->link, complete_list);
			continue;
		}

		if ( td_token_is_write(tok) && ! tok->host.bio ) {
			/* We can only redo writes if we have a bio */
			if (tok->result == TD_TOK_RESULT_OK)
				tok->result = TD_TOK_RESULT_FAIL_ABORT;
			list_add_tail(&tok->link, complete_list);
			continue;
		}
		rc = td_engine_io_migrate_token(eng, tok);
		if (rc < 0) {
			/* migration failed, report failure */
			tok->result = rc;
			list_add_tail(&tok->link, complete_list);
			continue;
		}

		/* migration successful; release token */
		td_free_token(eng, tok);

	} while (!list_empty(timedout_list));
}


