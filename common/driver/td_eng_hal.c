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

#include "td_eng_hal.h"
#include "td_eng_teradimm.h"
#ifdef CONFIG_TERADIMM_SIMULATOR
#include "td_eng_sim_md.h"
#include "td_eng_sim_td.h"
#endif
#ifdef CONFIG_TERADIMM_MEGADIMM
#include "td_eng_megadimm.h"
#endif
#ifdef CONFIG_TERADIMM_STM
/* Linux only serial driver for Eval-bard */
#include "td_eng_stm_td.h"
#endif

struct td_token;

static struct td_eng_hal_ops td_eng_null_ops;

struct td_eng_dev_type {
	const char *name;
	unsigned name_len;
	struct td_eng_hal_ops *ops;
} td_eng_dev_types[] = {
	{ "td", 2, &td_eng_teradimm_ops },
#define TD_ENG_DEV_ENTRY(_name,_ops) \
	{ .name = _name, .name_len = __builtin_strlen(_name), .ops = &_ops }
	TD_ENG_DEV_ENTRY( "td", td_eng_teradimm_ops ),
#ifdef CONFIG_TERADIMM_SIMULATOR
	TD_ENG_DEV_ENTRY( "ts", td_eng_sim_td_ops ),
#endif
#ifdef CONFIG_TERADIMM_MEGADIMM
#ifdef CONFIG_TERADIMM_SIMULATOR
	TD_ENG_DEV_ENTRY( "ms", td_eng_sim_md_ops ),
#endif
	TD_ENG_DEV_ENTRY( "zap", td_eng_megadimm_phase1_ops ),
	TD_ENG_DEV_ENTRY( "md",  td_eng_megadimm_phase2_ops ),
#endif
#ifdef CONFIG_TERADIMM_STM
	TD_ENG_DEV_ENTRY( "stm", td_eng_stm_td_ops ),
#endif
	{ NULL, 0, NULL }
};

struct td_eng_hal_ops *td_eng_hal_ops_for_name(const char *want_ops)
{
	struct td_eng_dev_type *t;
	unsigned wo_len;

	/* default implementation */
	if (!want_ops)
		return &td_eng_null_ops;

	wo_len = strlen(want_ops);

	for (t=td_eng_dev_types; t->name; t++) {
		if (wo_len <= t->name_len)
			continue;
			
		if (0==strncmp(t->name, want_ops, t->name_len))
			return t->ops;
	}

	return NULL;
}

/* ---- */

static int td_null_ops_init(struct td_engine *eng)
{
	return 0;
}
static int td_null_ops_enable(struct td_engine *eng)
{
	return -EFAULT;
}
static int td_null_ops_disable(struct td_engine *eng)
{
	return -EFAULT;
}
static int td_null_ops_exit(struct td_engine *eng)
{
	return 0;
}
static int td_null_ops_read_status(struct td_engine *eng
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
			, enum td_token_type tt
#endif
			)
{
	return -EFAULT;
}
static int td_null_ops_create_cmd(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_reverse_cmd_polarity(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_start_token(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_reset_token(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_write_page(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_read_page(struct td_engine *eng,
		struct td_token *tok)
{
	return -EFAULT;
}
static int td_null_ops_out_of_order(struct td_engine *eng,
		struct list_head *repeatit,
		uint64_t OoO)
{
	return -EFAULT;
}

static inline int td_null_ops_trim(struct td_engine *eng,
		struct td_token *tok) {
	return -EOPNOTSUPP;
}
static struct td_eng_hal_ops td_eng_null_ops = {
	._name        = "null",
	._init        = td_null_ops_init,
	._exit        = td_null_ops_exit,
	._enable      = td_null_ops_enable,
	._disable     = td_null_ops_disable,
	._read_status = td_null_ops_read_status,
	._create_cmd  = td_null_ops_create_cmd,
	._reverse_cmd_polarity = td_null_ops_reverse_cmd_polarity,
	._start_token = td_null_ops_start_token,
	._reset_token = td_null_ops_reset_token,
	._write_page  = td_null_ops_write_page,
	._read_page   = td_null_ops_read_page,
	._out_of_order = td_null_ops_out_of_order,
	._trim        = td_null_ops_trim,
};
