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
