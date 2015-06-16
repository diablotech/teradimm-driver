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

#ifndef _TD_ENG_COUNTERS_H_
#define _TD_ENG_COUNTERS_H_

#include "td_kdefn.h"

#include "td_compat.h"
#include "td_defs.h"

#define td_eng_counter_read_inc(eng,_which) \
	do { (eng)->counters.read[TD_DEV_GEN_COUNT_##_which] += 1; } while (0)
#define td_eng_counter_read_dec(eng,_which) \
	do { (eng)->counters.read[TD_DEV_GEN_COUNT_##_which] -= 1; } while (0)
#define td_eng_counter_read_get(eng, which)                                 \
	((eng)->counters.read[TD_DEV_GEN_COUNT_##which])

#define td_eng_counter_write_inc(eng,_which) \
	do { (eng)->counters.write[TD_DEV_GEN_COUNT_##_which] += 1; } while (0)
#define td_eng_counter_write_dec(eng,_which) \
	do { (eng)->counters.write[TD_DEV_GEN_COUNT_##_which] -= 1; } while (0)
#define td_eng_counter_write_get(eng, which)                                 \
	((eng)->counters.write[TD_DEV_GEN_COUNT_##which])

#define td_eng_counter_control_inc(eng,_which) \
	do { (eng)->counters.control[TD_DEV_GEN_COUNT_##_which] += 1; } while (0)
#define td_eng_counter_control_dec(eng,_which) \
	do { (eng)->counters.control[TD_DEV_GEN_COUNT_##_which] -= 1; } while (0)
#define td_eng_counter_control_get(eng, which)                                 \
	((eng)->counters.control[TD_DEV_GEN_COUNT_##which])

#define td_eng_counter_token_inc(eng,_which) \
	do { (eng)->counters.token[TD_DEV_TOKEN_##_which] += 1; } while (0)
#define td_eng_counter_token_dec(eng,_which) \
	do { (eng)->counters.token[TD_DEV_TOKEN_##_which] -= 1; } while (0)
#define td_eng_counter_token_get(eng, which)                                 \
	((eng)->counters.token[TD_DEV_TOKEN_##which])

#define td_eng_counter_misc_inc(eng,_which) \
	do { (eng)->counters.misc[TD_DEV_MISC_##_which] += 1; } while (0)
#define td_eng_counter_misc_dec(eng,_which) \
	do { (eng)->counters.misc[TD_DEV_MISC_##_which] -= 1; } while (0)
#define td_eng_counter_misc_get(eng, which)                                 \
	((eng)->counters.misc[TD_DEV_MISC_##which])

struct td_eng_counters {
	uint64_t read[TD_DEV_GEN_COUNT_MAX];
	uint64_t write[TD_DEV_GEN_COUNT_MAX];
	uint64_t control[TD_DEV_GEN_COUNT_MAX];
	uint64_t token[TD_DEV_TOKEN_COUNT_MAX];
	uint64_t misc[TD_DEV_MISC_COUNT_MAX];
};

#endif
