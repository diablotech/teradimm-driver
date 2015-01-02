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

#ifndef _TD_STASH_H_
#define _TD_STASH_H_

/*
 * The intent of this file is to make it easy to implement wrappers around
 * any OS native block request structures
 */

#include "td_kdefn.h"

#include "td_defs.h"
#include "td_compat.h"


/*
 * This "stash" allocator keeps a "stash" of allocated structures
 */

struct td_stash_info {
	struct list_head	elem_free_list;
	unsigned                alloc_size;
	unsigned                alloc_count;
};

struct td_stash_element {
	struct list_head    link;           /**< on td_active_tokens or td_free_tokens */
	struct td_stash_info *info;
	union {
		uint64_t    u64;            /**< uint64_t for alignment */
	} payload [0];
};


struct td_stash_info* td_stash_init (void *x,
		unsigned alloc_size, unsigned alloc_count);

void td_stash_destroy(void *x, struct td_stash_info *info);



static inline void* td_stash_alloc (struct td_stash_info *info, unsigned extra)
{
	struct td_stash_element *elem;
	int size = sizeof(struct td_stash_element) + extra;
	
	BUG_ON(info == NULL);

	if (unlikely(size > info->alloc_size) )
		return NULL;

	if (list_empty(&info->elem_free_list))
		return NULL;

	elem = list_first_entry(&info->elem_free_list,
				struct td_stash_element, link);
	list_del(&elem->link);
	return elem->payload;
}

static inline void td_stash_dealloc(void* payload)
{
	struct td_stash_element *elem = container_of(payload, struct td_stash_element, payload);
	struct td_stash_info *info = elem->info;

	memset(elem, 0, info->alloc_size);

	list_add(&elem->link, &info->elem_free_list);
}


#endif
