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

#ifndef _TD_MONITOR_H_
#define _TD_MONITOR_H_

#include "td_kdefn.h"
#include "td_defs.h"

#define TD_MONITOR_PERIOD  (1)

// common header
struct td_bin {
	int index;           // index into counts, next bin to update
	int num_valid;       // number of valid counts recorded
	int triggered;       // set when difference exceeds threshold
	void (*action)(struct td_bin*); // what to do when triggered
	uint64_t threshold;
	uint64_t difference; // difference between first and last
	uint64_t counts[0];  // recorded counts
};

struct td_bin_secs {
	struct td_bin bin;
	uint64_t counts[10];  // last 10 seconds
};

struct td_bin_hours {
	struct td_bin bin;
	uint64_t counts[24];  // last 24 hours
};

struct td_bins {
	unsigned long last_time;  // jiffies count when last updated
	uint64_t curr_count;
	int second_count;
	struct td_bin_secs  secs;
	struct td_bin_hours hours;
};

enum ecc_alram_bit_masks {
	ECC_ALARM_INTERNAL_INVALID   = 0x01,
	ECC_ALARM_INTERNAL_10_SECOND = 0x02,
	ECC_ALARM_INTERNAL_24_HOUR   = 0x04,
};

struct td_ecc_bins {
	int ecc_alarm_read;         // set after reading ecc_alarm
	uint32_t ecc_alarm;         // from params page 1
	struct td_ucmd *ucmd;
	struct list_head wait_link;
	struct td_bins ddr3;
	struct td_bins internal;
};

extern void td_monitor_bins_init(struct td_bins *bins);
extern int  td_monitor_get_bin_counts(const struct td_bin *bp,
		uint64_t *counts, int limit);
extern void td_monitor_ecc_poll_all_devs(void);

extern void td_monitor_set_rate(int);

#endif
