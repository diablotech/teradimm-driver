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

#ifndef _TD_UTIL_H_
#define _TD_UTIL_H_

#include "td_kdefn.h"
#include "td_compat.h"

#if BITS_PER_LONG == 32
#define debugfs_create_long(n,f,d,p) debugfs_create_u32(n,f,d,(u32*)(p))
#elif BITS_PER_LONG == 64
#define debugfs_create_long(n,f,d,p) debugfs_create_u64(n,f,d,(u64*)(p))
#endif

extern int td_ratelimit(void);

/* ---- list manipulation ---- */

/* based on list_first_entry(),
 * list is expected to be non empty. */
#ifndef list_last_entry
#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)
#endif


/* ---- timing ---- */

/** return CPU cycle counter */
static inline cycles_t td_get_cycles(void)
{
#if defined(__KERNEL__) && defined(CONFIG_TERADIMM_USE_NATIVE_GET_CYCLES)
	return get_cycles();
#else
	register uint32_t low, high;

	__asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return ((low) | ((uint64_t)(high) << 32));
#endif
}

/** convert number of nanoseconds to CPU clock ticks */
static inline cycles_t td_nsec_to_cycles(uint64_t nsec)
{
	uint64_t tmp = (cycles_t)nsec * cpu_khz;
	/* do_div(tmp, 1000000); */
	tmp /= 1000;
	tmp /= 1000;
	return tmp;
}

/** convert number of microseconds to CPU clock ticks */
static inline cycles_t td_usec_to_cycles(unsigned long usec)
{
	uint64_t tmp = (cycles_t)usec * cpu_khz;
	/* do_div(tmp, 1000); */
	tmp /= 1000;
	return tmp;
}

/** convert number of CPU clock ticks to milliseconds */
static inline unsigned long td_cycles_to_msec(cycles_t ticks)
{
	uint64_t tmp = ticks;
	/* do_div(tmp, cpu_khz); */
	tmp /= cpu_khz;
	return tmp;
}
/** convert number of CPU clock ticks to microseconds */
static inline unsigned long td_cycles_to_usec(cycles_t ticks)
{
	uint64_t tmp = ticks * 1000ULL;
	/* do_div(tmp, cpu_khz); */
	tmp /= cpu_khz;
	return tmp;
}

/** convert number of CPU clock ticks to nanoseconds */
static inline unsigned long long td_cycles_to_nsec(cycles_t ticks)
{
	uint64_t tmp = ticks * 1000ULL * 1000ULL;
	/* do_div(tmp, cpu_khz); */
	tmp /= cpu_khz;
	return tmp;
}

/** convert number of CPU clock ticks to jiffies */
static inline unsigned long td_cycles_to_jiffies(cycles_t ticks)
{
	uint64_t usec = td_cycles_to_usec(ticks);
	uint64_t tmp = usec * HZ;
	/* do_div(tmp, 1000000); */
	tmp /= 1000000;
	return tmp;
}

/** convert number of CPU clock ticks to hpet ticks */
static inline unsigned long long td_cycles_to_hpet_ticks(cycles_t cycles,
		unsigned long long hpet_ticks_per_second)
{
	unsigned long cps;
	unsigned long long ticks;

	/* number of CPU cycles per second */
	cps = cpu_khz * 1000;

	/* ticks = cycles * tps / cps */
	ticks = cycles * hpet_ticks_per_second;
	/* do_div(ticks, cps); */
	ticks /= cps;

	return ticks;
}


/* ---- parsing ---- */


static inline int td_parse_ulong(const char *buf, int base, ulong *val)
{
	char *end = NULL;

	*val = 0;

	if (!buf || !*buf)
		return -EINVAL;

	*val = simple_strtoul(buf, &end, base);
	if (!end || end == buf)
		return -EINVAL;

	switch (*end) {
	case 'G':
	case 'g':
		*val <<= 10;
	case 'M':
	case 'm':
		*val <<= 10;
	case 'K':
	case 'k':
		*val <<= 10;
		end++;
	default:
		break;
	}

	while(*end == '\n')
		end++;

	if (*end)
		return -EINVAL;

	return 0;
}

static inline int td_parse_long(const char *buf, int base, long *val)
{
	int rc;
	int negative = 0;

	while(isspace(*buf)) buf++;

	if (*buf == '-') {
		negative = 1;
		buf ++;
	}

	rc = td_parse_ulong(buf, base, (ulong *)val);
	if (rc)
		return rc;

	if (negative) {
		if (*val > LONG_MAX)
			return -ERANGE;
		*val = - *val;
	}

	return 0;
}


static inline uint64_t td_convert_low_high (uint32_t low, uint32_t high)
{
	return (((uint64_t)high) << 32) + low;
}

/* ---- dump hex data ---- */

extern void td_dump_data(const char *prefix, const void *void_data,
		unsigned len);


#endif
