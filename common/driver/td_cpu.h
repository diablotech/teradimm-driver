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

#ifndef _TD_CPU_H_
#define _TD_CPU_H_

#include "td_kdefn.h"
#include "td_compat.h"

#include "td_util.h"

enum td_cpu_state {
	TD_CPU_DRV_MAIN,
	TD_CPU_DRV_TASK,
	TD_CPU_DRV_CMD,
	TD_CPU_DRV_DATA,
	TD_CPU_DRV_POLL,
	TD_CPU_SIM_TOKEN,
	TD_CPU_SIM_STATUS,
	TD_CPU_SIM_INVALIDATE,
	TD_CPU_MAX
};

struct td_cpu_stats {
	cycles_t busy_time_start;               /**< reset when idle */
	enum td_cpu_state cur_state;        /**< current state */
	cycles_t cpu_totals[TD_CPU_MAX];    /**< total ticks spent in different sections */
};

static inline void td_cpu_start(struct td_cpu_stats *s,
		enum td_cpu_state n)
{
	if (!s->busy_time_start) {
		s->busy_time_start = td_get_cycles();
		s->cur_state = n;
	}
}

static inline cycles_t __td_cpu_update(struct td_cpu_stats *s)
{
	cycles_t now = td_get_cycles();
	enum td_cpu_state cur = s->cur_state;

	if (s->busy_time_start && (unsigned)cur < TD_CPU_MAX) {
		s->cpu_totals[cur] +=
			now - s->busy_time_start;
	}

	return now;
}

static inline void td_cpu_end(struct td_cpu_stats *s)
{
	__td_cpu_update(s);
	s->busy_time_start = 0;
	s->cur_state = TD_CPU_MAX;

}

/** 
 * switch cpu state
 * @param s - accounting structure
 * @param n - new state
 * @return previous state
 */
static inline enum td_cpu_state td_cpu_switch(
		struct td_cpu_stats *s, enum td_cpu_state n)
{
	enum td_cpu_state o = s->cur_state;
	s->busy_time_start = __td_cpu_update(s);
	s->cur_state = n;
	return o;
}

#endif
