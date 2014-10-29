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
