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


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "td_trace.h"
#include "td_drv.h"
#include "td_util.h"
#include "td_ioctl.h"

static uint td_ttrace = TD_TRACE_DEFAULT_SIZE;
static uint td_twrap = 1;
static uint td_tmask = TD_TRACE_DEFAULT_MASK;
module_param_named(trace, td_ttrace, uint, 0444);
module_param_named(trace_wrap, td_twrap, uint, 0444);
module_param_named(trace_mask, td_tmask, uint, 0444);
MODULE_PARM_DESC(trace, "Number of thread event traces to accumulate");
MODULE_PARM_DESC(trace_wrap, "Default setting for trace wrap.");
MODULE_PARM_DESC(trace_mask, "Default setting for trace mask.");

#ifdef CONFIG_TERADIMM_TRACE

/* --- init */

int td_trace_init(struct td_trace *trc, const char *name, int node)
{
	ulong allocated_size;

	trc->tt_entries = NULL;
	trc->tt_max = 0;
	trc->tt_next = 0;
	trc->tt_wrap = !!td_twrap;

	if (!td_ttrace)
		return 0;

	if (!trc || !name)
		return -EINVAL;

	allocated_size = td_ttrace
		* sizeof(struct td_trace_entry);
	trc->tt_entries = vmalloc_node(allocated_size, node);
	if (!trc->tt_entries) {
		pr_warning("couldn't allocate %lu bytes for thread trace\n",
				allocated_size);
		return -ENOMEM;
	}

	memset(trc->tt_entries, 0, allocated_size);


	trc->tt_max = td_ttrace;
	trc->tt_mask = td_tmask;

	pr_info("%s trace enabled\n", name);

	return 0;
}


void td_trace_cleanup(struct td_trace *trc)
{
	if (trc->tt_entries) {
		trc->tt_mask = TD_TRACE_DEFAULT_MASK;
		trc->tt_max = 0;
		mb();
		vfree(trc->tt_entries);
		trc->tt_entries = NULL;
	}
}

void td_trace_reset(struct td_trace *trc)
{
	if (trc->tt_entries) {
		trc->tt_next = 0;
		memset(trc->tt_entries,0, trc->tt_max
				* sizeof(struct td_trace_entry));
	}
}

/* --- dumper */

ssize_t td_trace_iterator_read (struct td_trace *trc,
		struct td_trace_iterator *iter,
		char __user *ubuf, size_t cnt)
{
	char line[128];
	char since_start[20], since_last[20];
	int rc;
	ssize_t len = 0;
	struct td_trace_entry *t;
	uint i;

	if (!trc->tt_entries
			|| !trc->tt_max)
		return -ENODEV;

	if (!iter->offset) {
		uint tmp_idx;

		iter->first_id = trc->tt_next;

		iter->index = 0;
		i = (iter->first_id + iter->index) % trc->tt_max;

		while (iter->index < trc->tt_max) {
			t = trc->tt_entries + i;

			if (t->ts || t->label || t->data)
				break;

			iter->index ++;
			i = (iter->first_id + iter->index) % trc->tt_max;
		}

		iter->last_cycles = iter->start_cycles = trc->tt_entries[i].ts;

		/* find the minimum time, that will be the base */
		tmp_idx = iter->index + 1;
		i = (iter->first_id + tmp_idx) % trc->tt_max;
		while (tmp_idx < trc->tt_max) {
			t = trc->tt_entries + i;

			if (iter->start_cycles > trc->tt_entries[i].ts)
				iter->start_cycles = trc->tt_entries[i].ts;

			tmp_idx ++;
			i = (iter->first_id + tmp_idx) % trc->tt_max;
		}
	}

	while (iter->index < trc->tt_max
			&& (len+sizeof(line)) < cnt) {

		u64 delta;
		char deltasign='?';

		i = (iter->first_id + iter->index) % trc->tt_max;
		t = trc->tt_entries + i;

		if (!t->label)
			break;

		/* populate since_start */
		delta = td_cycles_to_nsec(t->ts - iter->start_cycles);
		rc = snprintf(since_start, sizeof(since_start),
			"%lu.%03u",
			(ulong)(delta / 1000),
			(uint)(delta % 1000));
		if (rc <= 0)
			break;

		/* populate since_last */
		if (t->ts >= iter->last_cycles) {
			delta = td_cycles_to_nsec(t->ts - iter->last_cycles);
			deltasign = '+';
		} else {
			delta = td_cycles_to_nsec(iter->last_cycles - t->ts);
			deltasign = '-';
		}
		if (delta > 100000000)
			rc = snprintf(since_last, sizeof(since_last),
					"(%cinf)", deltasign);
		else
			rc = snprintf(since_last, sizeof(since_last),
					"%c%lu.%03u",
					deltasign,
					(ulong)(delta / 1000),
					(uint)(delta % 1000));
		if (rc <= 0)
			break;

		switch (t->print_index) {
		case TP_LEGACY:
			rc = snprintf(line, sizeof(line),
				"%-14s %10s   CPU_%-5u %-25s %20lld %016llx\n",
				since_start,
				since_last,
				t->cpu,
				t->label,
				t->data,
				t->data);
			break;
		case TP_1X64:
			rc = snprintf(line, sizeof(line),
				"%-14s %10s   CPU_%-5u ",
					since_start,
					since_last,
					t->cpu);

			if (rc > 0) {
				rc += snprintf(line+rc, sizeof(line)-rc, t->label,
					t->data);

				if (rc > 0)
					rc += snprintf(line+rc, sizeof(line)-rc, "\n");
			}
			break;
		case TP_2X32:
			rc = snprintf(line, sizeof(line),
				"%-14s %10s   CPU_%-5u ",
					since_start,
					since_last,
					t->cpu);

			if (rc > 0) {
				rc += snprintf(line+rc, sizeof(line)-rc, t->label,
					(uint32_t)(t->data >> 32),
					(uint32_t)(t->data >> 0));

				if (rc > 0)
					rc += snprintf(line+rc, sizeof(line)-rc, "\n");
			}
			break;
		case TP_4X16:
			rc = snprintf(line, sizeof(line),
				"%-14s %10s   CPU_%-5u ",
					since_start,
					since_last,
					t->cpu);

			if (rc > 0) {
				rc += snprintf(line+rc, sizeof(line)-rc, t->label,
					(uint16_t)(t->data >> 48),
					(uint16_t)(t->data >> 32),
					(uint16_t)(t->data >> 16),
					(uint16_t)(t->data >> 0));

				if (rc > 0)
					rc += snprintf(line+rc, sizeof(line)-rc, "\n");
			}
			break;
		default:
			/* not supported */
			rc = -1;
			break;
		}

		if (rc <= 0)
			break;
		if (copy_to_user(ubuf + len, line, rc)) {
			if (!len)
				return -EFAULT;
			break;
		}
		iter->index ++;
		len += rc;
		iter->last_cycles = t->ts;
	}

	iter->offset += len;

	return len;
}

void td_trace_dump(struct td_trace *trc)
{
	unsigned ti;
	cycles_t last_cycles;

	last_cycles = 0;
	for (ti=0; ti<trc->tt_max; ti++) {
		int i = (trc->tt_next + ti) % trc->tt_max;
		struct td_trace_entry *t = trc->tt_entries + i;
		cycles_t delta = t->ts - last_cycles;
		last_cycles = t->ts;

		if (!last_cycles || !t->label)
			continue;

		pr_info("+%-5lu CPU_%5u %25s %16lld\n",
				td_cycles_to_usec(delta),
				t->cpu,
				t->label,
				t->data);
	}
}
#endif


