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

#ifndef _TD_LATENCY_H_
#define _TD_LATENCY_H_

#include "td_kdefn.h"


#include "td_defs.h"
#include "td_util.h"
#include "td_ioctl.h"

/**
 * This construct computes the latency of a single event from start to finish.
 * It's only capable of measuring the latency of one object at a time.
 *
 * In the engine it is used for measuring the latency of requests and tokens.
 * While the engine handles multiple requests and tokens in parallel, this is
 * used to track only one at a time.  The cookie field allows for the latency
 * tracking to be done for one object.
 */

struct td_eng_latency {
	void        *cookie;        /* current object tracked, NULL if unused */
	cycles_t    start;          /* cycles at start of current event */
};

/**
 * start tracking an event
 *
 * if the vent is actually being tracked, returns 1
 */
static inline int td_eng_latency_start(struct td_eng_latency *lat,
		void *object)
{
	if (lat->cookie)
		return 0;

	lat->cookie = object;
	lat->start = td_get_cycles();
	mb();

	return 1;
}

static inline void td_eng_latency_clear(struct td_io_latency_counters *cntrs)
{
	if (cntrs->lat_cnt) {
		/* best effort attempt at clearing both accumulator and count
		 * at the same time; this means the observer is more likely to
		 * see one of them as zero and discard that read */
		mb();
		cntrs->lat_acumu = 0;
		cntrs->lat_cnt   = 0;
		mb();
	}
}

/**
 * end an event
 *
 * this function will figure out how long this object took to complete.
 * it increments the counter by one and adds the latency to an accumulator.
 * the accumulator is not allowed to overflow.
 */
static inline int __td_eng_latency_end(struct td_eng_latency *lat,
		void *object, struct td_io_latency_counters *cntrs)
{
	volatile cycles_t now;
	volatile cycles_t diff;

	if (likely(lat->cookie != object))
		return 0;

	now = td_get_cycles();
	if (unlikely(now < lat->start))
		goto reset_and_bail;

	diff = now - lat->start;
	cntrs->latency = td_cycles_to_nsec(diff);

	if (unlikely (cntrs->lat_acumu > (1LL<<60))) {
		uint64_t new_accum, new_count;

		/* when the accumulator gets pretty big, both the accumulated
		 * latencies and the counter are divided by the same amount
		 * to maintain some backwards history. */

		new_accum = cntrs->lat_acumu >> 16;
		new_count = cntrs->lat_cnt >> 16;

		/* the history is cleared first, so that the observer is
		 * likely to not see a transition, but rather one of the
		 * numbers set to zero, at which point they can retry or
		 * ignore that sample */
		td_eng_latency_clear(cntrs);

		cntrs->lat_acumu = new_accum;
		cntrs->lat_cnt   = new_count;
	}

	cntrs->lat_acumu += cntrs->latency;
	cntrs->lat_cnt ++;

reset_and_bail:
	lat->start = 0;
	lat->cookie = NULL;
	return 1;
}

#define td_eng_latency_end(lat,obj,label,cntrs) do { \
	if (unlikely(__td_eng_latency_end((lat), (obj), (cntrs)))) { \
		td_eng_trace((eng), TR_LATENCY, label, (cntrs)->latency); \
	} \
} while(0)

#endif
