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

#include "td_kdefn.h"


#include "td_stash.h"


struct td_stash_info* td_stash_init (void *x, unsigned alloc_size, unsigned alloc_count)
{
	struct td_stash_info *info;
	int i;

	info = kzalloc(sizeof(struct td_stash_info), GFP_KERNEL);
	BUG_ON(info == NULL);

	INIT_LIST_HEAD(&info->elem_free_list);
	info->alloc_size = alloc_size;
	info->alloc_count = 0;
	
	for (i = 0; i < alloc_count; i++) {
		struct td_stash_element *elem = kzalloc(info->alloc_size, GFP_KERNEL);
		if (elem) {
			list_add(&elem->link, &info->elem_free_list);
			elem->info = info;
			info->alloc_count ++;
		}
	}

	WARN_ON(alloc_count != info->alloc_count);
	return info;
}

void td_stash_destroy(void *x, struct td_stash_info *info)
{
	while (! list_empty(&info->elem_free_list) ) {
		struct td_stash_element *elem =
				list_first_entry(&info->elem_free_list,
				struct td_stash_element, link);
		list_del(&elem->link);
		kfree(elem);
		info->alloc_count--;
	}

	WARN_ON(info->alloc_count);
	kfree(info);
}


