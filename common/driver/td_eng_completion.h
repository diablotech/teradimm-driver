/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("Diablo").  All       *
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
 *    Modified BSD License                                               *
 *                                                                       *
 *    If you have explicit permission from Diablo, then (1) use in       *
 *    source and binary forms, with or without modification; as well as  *
 *    (2) redistribution ONLY in binary form, with or without            *
 *    modification; are permitted provided that the following conditions *
 *    are met:                                                           *
 *                                                                       *
 *    * Redistributions in binary form must reproduce the above          *
 *    copyright notice, this list of conditions and the following        *
 *    disclaimer in the documentation and/or other materials provided    *
 *    with the distribution.                                             *
 *                                                                       *
 *    * Neither the name of the DIABLO nor the names of its contributors *
 *    may be used to endorse or promote products derived from this       *
 *    software without specific prior written permission.                *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR *
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) *
 *    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN          *
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR       *
 *    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,     *
 *    EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                 *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _TD_ENG_TIMEOUTS_H_
#define _TD_ENG_TIMEOUTS_H_

#include "td_kdefn.h"

#include <linux/types.h>
#include <linux/kernel.h>

#include "td_defs.h"
#include "td_trace.h"
#include "td_util.h"
#include "td_cpu.h"
#include "td_eng_conf.h"
#include "td_token.h"
#include "td_token_list.h"

/* ------------------------------------------------------------------------ */

/* The following functions are helpers used by the IO completion code */

/**
 * abort all tokens
 *
 * @param eng            - engine on which errors occured
 * @param tofail_list    - list of tokens to be aborted
 * @param complete_list  - append aborted tokens to this list
 *
 * This is a simple helper that pops all commands from @tofail_list, marks
 * them as failed, and pushes them to the @complete_list.
 */
static inline void td_engine_io_abort_tok_list(struct td_engine *eng,
		struct list_head *tofail_list, struct list_head *complete_list)
{
	struct td_token *tok;

	while (!list_empty(tofail_list)) {
		tok = list_entry(tofail_list->next,
				struct td_token, link);
		list_del(&tok->link);
		tok->result = TD_TOK_RESULT_FAIL_ABORT;
		list_add_tail(&tok->link, complete_list);
	}
}

/**
 * abort all tokens, and kill driver
 *
 * @param eng            - engine on which errors occured
 * @param tofail_list    - list of tokens to be aborted
 * @param complete_list  - append aborted tokens to this list
 *
 * Like td_engine_io_abort_tok_list(), but also puts engine into DEAD state.
 */
extern void td_eng_die_timeout_handler(struct td_engine *eng,
		struct list_head *timedout_list,   // tokens that timedout
		struct list_head *complete_list);  // failed ones get put on this list


/* ------------------------------------------------------------------------ */

/* The following functions implement different ways of handling
 * timeouts.
 *
 * The engine code accesses these via td_eng_hal_handle_timeouts()
 * function for which is assigned in the engine's _enable() implementation.
 */

/** timeout handler which moves a command to a new token */
extern void td_eng_migrate_timeout_handler(struct td_engine *eng,
		struct list_head *timedout_list,    // tokens that timedout
		struct list_head *complete_list);   // failed ones get put on this list

/** timeout handler which resets a token and set the tok->needs_retry bit */
extern void td_eng_reset_retry_timeout_handler(struct td_engine *eng,
		struct list_head *resets_list,      // tokens that need resets
		struct list_head *complete_list);   // failed ones get put on this list


#endif
