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


