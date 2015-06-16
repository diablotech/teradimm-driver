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

#include "td_kdefn.h"

#include <linux/types.h>

#include "td_ucmd.h"
#include "td_engine.h"
#include "td_device.h"
#include "td_devgroup.h"


void td_ucmd_init(struct td_ucmd *ucmd)
{
	/* clear everything after ucmd->ioctl */
	memset((&ucmd->ioctl)+1, 0, sizeof(*ucmd) - sizeof(ucmd->ioctl));

	init_waitqueue_head(&ucmd->wq);
	atomic_set(&ucmd->ref, 1);
}

struct td_ucmd* td_ucmd_alloc(int virt_size)
{
	struct td_ucmd* ucmd = kzalloc(sizeof(*ucmd), GFP_KERNEL);
	if (!ucmd)
		goto error_no_ucmd;


	if (virt_size > 0)
	{
		ucmd->data_virt = kmalloc(virt_size, GFP_KERNEL);

		if (! ucmd->data_virt)
			goto error_no_virt;
	}

	init_waitqueue_head(&ucmd->wq);
	atomic_set(&ucmd->ref, 1);

	return ucmd;

error_no_virt:
	kfree(ucmd);
error_no_ucmd:
	return NULL;
}


int td_ucmd_map_virt(struct td_ucmd *ucmd, void* ptr)
{
	ucmd->data_page = NULL;
	ucmd->data_virt = ptr;
	return 0;
}

int td_ucmd_map(struct td_ucmd *ucmd,
		struct task_struct *task, unsigned long addr)
{
	int rc;

	if (! task) {
		ucmd->data_page = (struct page*)addr;
		ucmd->data_virt = kmap(ucmd->data_page);
		return 0;
	}

	ucmd->user_task = task;

	/* make sure the pointer is page aligned */
	if (addr & (~PAGE_MASK)) {
		printk("UCMD MAP ERROR: Not page aligned - %lx\n", addr);
		return -EINVAL;
	}

	/* pin data page in memory */
	down_read(&task->mm->mmap_sem);
	rc = get_user_pages(task,
			task->mm,
			addr,
			1, /* 1 page */
			1, /* allow writes */
			0, /* don't force if shared */
			&ucmd->data_page,
			NULL);
	up_read(&task->mm->mmap_sem);
	/* return from GUP is # of pages pinned */
	if (rc!=1) {
		printk("UCMD MAP ERROR: not found: %d\n", rc);
		return -EFAULT;
	}
	if (rc<0) {
		printk("UCMD MAP ERROR: page error: %d\n", rc);
		return rc; }
	ucmd->data_virt = kmap(ucmd->data_page);
	return 0;
}

void td_ucmd_unmap(struct td_ucmd *ucmd)
{
	if (! ucmd->data_virt)
		return;


	if (ucmd->user_task) {
		set_page_dirty_lock(ucmd->data_page);
		/* mark_page_accessed(ucmd->data_page); */
		put_page(ucmd->data_page);
	} else {
		if (ucmd->data_page) {
			__free_page(ucmd->data_page);
		} else {
			kfree(ucmd->data_virt);
		}
	}

	ucmd->data_virt = NULL;
	ucmd->data_page = NULL;
}


int td_ucmd_run(struct td_ucmd *ucmd, struct td_engine *eng)
{
	td_ucmd_ready(ucmd);
	td_enqueue_ucmd(eng, ucmd);
	td_engine_sometimes_poke(eng);
	return td_ucmd_wait(ucmd);
}


