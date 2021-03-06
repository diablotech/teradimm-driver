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


#if defined(__KERNEL__)
/***************************************************************************
 *                                                                         *
 * Linux, KERNEL/DRIVER mode                                               *
 *                                                                         *
 ***************************************************************************/
#include <linux/types.h>
#include <linux/timex.h>    // cycles_t
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/list.h>

#ifndef KABI__bool
typedef uint8_t bool;
#define false 0
#define true 1
#endif

#ifndef KABI__is_err_or_null
#include "linux/err.h"
static inline long __must_check IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}
#endif

#define CLFLUSH_SIZE	boot_cpu_data.x86_clflush_size

#define U64x "lx"

#define IS_KERNEL_SYMBOL(_sym) (((_sym) & 0xfffffff000000000) == 0xfffffff000000000)

#ifdef num_online_cpus
#define MAX_CPU_NUMBER (num_online_cpus())
#else
#define MAX_CPU_NUMBER (NR_CPUS)
#endif

#ifndef INIT_COMPLETION
#define INIT_COMPLETION(x) reinit_completion(&x)
#endif

#ifdef KABI__bio_bi_size
/* Linux git commit 4f024f37 changed the bio struct*/
#define bio_size        bi_size
#define bio_sector      bi_sector
#define bio_idx         bi_idx
#define td_bvec_iter    int
#define td_bv_idx(x)    x

/* Create our own for_each to use bvec and not *bvec. */
#define td_bio_for_each_segment(bvec, bio, iter)                       \
	for (iter = (bio)->bi_idx;                                     \
		bvec = (bio)->bi_io_vec[iter], iter < (bio)->bi_vcnt;  \
		iter++)

#else

#define bio_size        bi_iter.bi_size
#define bio_sector      bi_iter.bi_sector
#define bio_idx         bi_iter.bi_idx
#define td_bvec_iter    struct bvec_iter
#define td_bv_idx(x)    x.bi_idx

#define td_bio_for_each_segment(bvec, bio, iter) \
	bio_for_each_segment(bvec, bio, iter)
#endif


#else
/***************************************************************************
 *                                                                         *
 * Linux, User mode                                                        *
 *                                                                         *
 ***************************************************************************/
#include <stdint.h>
#include <limits.h>
#include <string.h>
typedef uint64_t cycles_t;
struct list_head { struct list_head *next, *prev; };

#define U64x "lx"

#endif


/***************************************************************************
 *                                                                         *
 * Linux, generic mode                                                     *
 *                                                                         *
 ***************************************************************************/
