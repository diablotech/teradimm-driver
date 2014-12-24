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

#ifndef _TD_ENG_MCEFREE_H_
#define _TD_ENG_MCEFREE_H_

#include "td_compat.h"
#include "td_defs.h"
#include "td_engine_def.h"

struct list_head;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS

extern int __td_engine_mcefree_read_buffer_matching(struct td_engine *eng,
		struct list_head *timeout_completed,
		struct list_head *failed_completed);

static inline int td_engine_mcefree_read_buffer_matching(struct td_engine *eng,
		struct list_head *timeout_completed,
		struct list_head *failed_completed)
{
	int rc;

	if (!eng->mcefree_nested_count) {

		eng->mcefree_nested_count ++;

		td_eng_trace(eng, TR_STATUS, "mcefree_nested_count",
			eng->mcefree_nested_count);

		/* only work on making progress if we are expecting reads to complete */
		rc = 0;
		if (eng->td_early_completed_reads_tokens.count)
			rc = __td_engine_mcefree_read_buffer_matching(eng,
					timeout_completed, failed_completed);

		eng->mcefree_nested_count --;
	}

	return rc;
}

#else

#define td_engine_mcefree_read_buffer_matching(eng,comp) do { /* nothing */ } while(0)

#endif

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
static inline bool td_engine_hold_back_status_access(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t nsec = 0, max;

	if (!td_eng_using_fwstatus(eng))
		return false;

	if (!eng->td_last_hardware_command)
		return false;

	now = td_get_cycles();
	diff = now - eng->td_last_hardware_command;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, STATUS_HOLD_NSEC);

	if (max && nsec < max)
		return true;

	/* we were holding back, but no more */

	eng->td_last_hardware_command = 0;
	td_eng_trace(eng, TR_CMD, "hold-back-status-done", diff);

	return false;
}

static inline bool td_engine_hold_back_fwstatus_request(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t nsec = 0, max;

	if (!td_eng_using_fwstatus(eng))
		return false;

	if (!eng->td_last_write_command_for_fwstatus)
		return false;

	now = td_get_cycles();
	diff = now - eng->td_last_write_command_for_fwstatus;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, FWSTATUS_HOLD_NSEC);

	if (max && nsec < max)
		return true;

	/* we were holding back, but no more */

	eng->td_last_write_command_for_fwstatus = 0;
	td_eng_trace(eng, TR_CMD, "hold-back-status-done", diff);

	return false;
}


static inline bool td_engine_hold_back_deallocate(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t nsec = 0, max;

	if (!td_eng_using_fwstatus(eng))
		return false;

	if (!eng->td_last_write_command_for_dealloc)
		return false;

	now = td_get_cycles();
	diff = now - eng->td_last_write_command_for_dealloc;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, DEALLOC_HOLD_NSEC);

	if (max && nsec < max)
		return true;

	/* we were holding back, but no more */

	eng->td_last_write_command_for_dealloc = 0;
	td_eng_trace(eng, TR_CMD, "hold-back-deallocate-done", diff);

	return false;
}

static inline bool td_engine_no_write_while_fwstatus(struct td_engine *eng)
{
	if (!td_eng_using_fwstatus(eng))
		return false;
	return (bool)td_eng_conf_mcefree_var_get(eng, NO_WRITE_WHILE_FWSTATUS);
}

static inline bool td_engine_hold_back_write(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t nsec = 0, max;

	if (!td_eng_using_fwstatus(eng))
		return false;

	/* we could be holding writes because there are FWSTATUS requests pending */
	if (td_engine_no_write_while_fwstatus(eng)
			&& eng->td_last_fwstatus_request_posted)
		return true;

	/* we could be holding writes because deallocates were sent recently */
	if (!eng->td_last_deallocate_command)
		return false;

	now = td_get_cycles();
	diff = now - eng->td_last_deallocate_command;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, WRITE_HOLD_NSEC);

	if (max && nsec < max)
		return true;

	/* we were holding back, but no more */

	eng->td_last_deallocate_command = 0;
	td_eng_trace(eng, TR_CMD, "hold-back-write-done", diff);

	return false;
}

static inline bool td_engine_jit_deallocations(struct td_engine *eng)
{
	if (!td_eng_using_fwstatus(eng))
		return false;
	return (bool)td_eng_conf_mcefree_var_get(eng, JIT_DEALLOCATES);
}
#endif


#endif
