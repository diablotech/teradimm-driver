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

#ifndef _TR_MIRROR_H_
#define _TR_MIRROR_H_

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_ioctl.h"
#include "td_engine.h"
#include "td_engine.h"
#include "td_raid.h"
#include "td_bio.h"

struct td_ucmd;


struct trms_state {
	struct td_ucmd **ucmds;
	struct td_ucmd **read_queue;
	struct td_ucmd **write_queue;

	int qd;
	int pos_ucmd;
	int pos_read;
	int pos_write;
};

/* Per raid type params */
struct tr_mirror {
	atomic_t        last_read_dev;

	uint64_t                                conf[TR_CONF_MIRROR_MAX];
	uint64_t                                counter[TR_MIRROR_COUNT_MAX];
	
	
	struct trms_state resync;
	uint64_t lock_ahead;
	uint64_t lock_start;
	uint64_t lock_end;
};


static inline struct tr_mirror * tr_mirror(struct td_raid *rdev)
{
	return (struct tr_mirror *) rdev->ops_priv;
}


#define tr_mirror_var_get(rdev, which)                             \
	(tr_mirror(rdev)->conf[TR_CONF_MIRROR_##which])
#define tr_mirror_var_set(rdev, which, val)                            \
	do { tr_mirror(rdev)->conf[TR_CONF_MIRROR_##which] = val;                   \
	td_raid_debug(rdev, "CONF MIRROR_%s set to %llu\n", __stringify(which), tr_mirror(rdev)->conf[TR_CONF_MIRROR_##which]); \
	} while (0)


int tr_mirror_request_optimal (struct td_raid *rdev, td_bio_ref bio);

extern int tr_mirror_resync(struct td_raid *rdev);
extern int tr_mirror_resync_thread (void *arg);


#endif
