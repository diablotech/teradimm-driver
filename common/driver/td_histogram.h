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

#ifndef _TD_HISTOGRAM_H_
#define _TD_HISTOGRAM_H_

#include "td_kdefn.h"


#ifdef CONFIG_TD_HISTOGRAM

#define TD_HISTOGRAM_SIZE 64
struct td_histogram {
	/* name is assumed to be first in structure, see td_histogram_init() */
	char     name[64];

	uint64_t bucket_floor[TD_HISTOGRAM_SIZE];
	uint64_t bucket_counter[TD_HISTOGRAM_SIZE];

	int64_t  bucket_size;

	uint64_t count;
	uint64_t total;
	uint64_t min, max;
};

#define TD_DECLARE_HISTOGRAM(hist) \
	struct td_histogram hist

extern void td_histogram_init(struct td_histogram *hist,
		const char *namef, ...);

extern void td_histogram_dump(struct td_histogram *hist);

static void td_histogram_group_buckets(
		struct td_histogram *hist, uint64_t val)
{
	uint i, g;
	uint group = 1;
	uint64_t max;
	uint64_t groupped[TD_HISTOGRAM_SIZE] = {0,};

	if (val >= INT_MAX)
		return;

	max = hist->bucket_floor[TD_HISTOGRAM_SIZE-1] + hist->bucket_size;

	while (val > (max * (1<<group))) {
		group ++;
		if (unlikely (group > 256)) {
			pr_err("%s: BUG group=%u, "
				"max=%lld, val=%lld\n",
				__FUNCTION__,
				group, max, val);
			return;
		}
	}

	for (i=0; i<TD_HISTOGRAM_SIZE; i++) {

		hist->bucket_floor[i] <<= group;

		g = i >> group;

		groupped[g] += hist->bucket_counter[i];
	}

	memcpy(hist->bucket_counter, groupped, sizeof(groupped));

	hist->bucket_size <<= group;
}

static inline unsigned td_histogram_find_bucket(
		struct td_histogram *hist, uint64_t val)
{
	uint s, m, e;
	uint64_t floor, ceiling;
	m = TD_HISTOGRAM_SIZE-1;

	ceiling = hist->bucket_floor[m] + hist->bucket_size;
	if (unlikely(val >= ceiling))
		td_histogram_group_buckets(hist, val);

	if (unlikely(val > hist->bucket_floor[m]))
		return m;
			
	s = 0, e = TD_HISTOGRAM_SIZE-2;
	while (likely(s < e)) {
		m = (s+e) / 2;
		floor = hist->bucket_floor[m];
		if (val < floor) {
			if (unlikely(e == m)) {
				pr_err("%s: BUG s=%u, e=m=%u, "
						"floor=%lld, val=%lld\n",
						__FUNCTION__,
						e, e, floor, val);
				break;
			}
			e = m;
			continue;
		}
		ceiling = hist->bucket_floor[m+1];
		if (val >= ceiling) {
			m++;
			if (unlikely(s == m)) {
				pr_err("%s: BUG s=m=%u, e=%u, "
						"ceiling=%lld, val=%lld\n",
						__FUNCTION__,
						s, e, ceiling, val);
				break;
			}
			s = m;
			continue;
		}
		break;
	}

	return m;
}

static inline void td_histogram_update(struct td_histogram *hist, uint64_t val)
{
	uint m;
	
	m = td_histogram_find_bucket(hist,val);
	hist->bucket_counter[m] ++;
	
	if (likely(hist->count)) {
		if (unlikely(hist->min > (val)))
			hist->min = (val);
		if (unlikely(hist->max < (val)))
			hist->max = (val);
	} else {
		hist->min = (val);
		hist->max = (val);
	}
	
	hist->count ++;
	hist->total += val;
}

#else /*  ! CONFIG_TD_HISTOGRAM */

#define TD_DECLARE_HISTOGRAM(hist)
#define td_histogram_init(hist,name,...) do { } while(0)
#define td_histogram_update(hist,val) do { } while(0)
#define td_histogram_dump(hist) do { } while(0)

#endif

#endif
