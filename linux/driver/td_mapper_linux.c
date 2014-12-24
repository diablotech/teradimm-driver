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
#include "td_compat.h"

#include "td_mapper.h"

/**
 * init_memory_mapping() is needed to create page tables on some systems.
 *
 * When build with CONFIG_TERADIMM_INIT_MEMORY_MAPPING the driver will contain
 * dormant code to issue a call to init_memory_mapping() if force_mem_mapping
 * is set through insmod/modprobe.  force_mem_mapping can be set to a boolean
 * (1) or a pointer to the init_memory_mapping symbol.
 */
#ifdef CONFIG_TERADIMM_INIT_MEMORY_MAPPING

static long td_force_mem_mapping = 0;
module_param_named(force_mem_mapping, td_force_mem_mapping, long, 0444);
MODULE_PARM_DESC(force_mem_mapping, "Force call to init_memory_mapping");

static unsigned long (*__init_memory_mapping)(unsigned long start, unsigned long end)
	 = (void*)SYM_init_memory_mapping;

int td_force_memory_mapping(struct td_mapper *m)
{
	if (!td_force_mem_mapping)
		return 0;

	if ((td_force_mem_mapping & 0xfffffff000000000) == 0xfffffff000000000) {
		pr_warn("using td_force_mem_mapping as the address for "
				"init_memory_mapping() at 0x%016lx\n",
				td_force_mem_mapping);
		__init_memory_mapping = (void*)td_force_mem_mapping;
	}

	if (!__init_memory_mapping) {
		pr_err("init_memory_mapping symbol is undefined\n");
		return -EFAULT;
	}
	pr_info ("mapping in memory 0x%llx+0x%llx (high_mem=0x%lx)\n",
		m->phys_base, m->phys_size, __pa(high_memory));

	__init_memory_mapping(m->phys_base, m->phys_base + m->phys_size);

	return 0;
}
#else
int td_force_memory_mapping(struct td_mapper *m)
{
	return 0;
}

#endif


void * td_mapper_map_wb(struct td_mapper *m, uint64_t off, uint64_t size)
{
#if defined(KABI__ioremap_cache)
	return ioremap_cache(m->phys_base + off, size);
#else
	pr_err("kernel lacks support for ioremap_cache()\n");
	return NULL;
#endif
}

void * td_mapper_map_wc(struct td_mapper *m, uint64_t off, uint64_t size)
{
#if defined(KABI__ioremap_wc)
	return ioremap_wc(m->phys_base + off, size);
#else
	pr_err("kernel lacks support for ioremap_wc()\n");
	return NULL;
#endif
}

void * td_mapper_map_uc(struct td_mapper *m, uint64_t off, uint64_t size)
{
	return ioremap(m->phys_base + off, size);
}

void td_mapper_unmap(struct td_mapper *m, void *virt)
{
	iounmap(virt);
}

