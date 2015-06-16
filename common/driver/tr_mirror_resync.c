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

#include "td_compat.h"

#include "td_device.h"
#include "td_engine.h"
#include "td_raid.h"
#include "td_eng_conf.h"
#include "td_eng_hal.h"
#include "td_command.h"
#include "td_ucmd.h"

#include "tr_mirror.h"


static int DEBUG_RESYNC_PRINTK = 0;
module_param_named(DEBUG_RESYNC_PRINTK, DEBUG_RESYNC_PRINTK, uint, 0644);

static uint trms_queue_depth = 32;
module_param_named(mirror_sync_qd, trms_queue_depth, uint, 0644);




/*
 * This queues to the internal resync stuff
 * The bio lock *MUST* be held when this is called
 */
int trms_queue_bio (struct td_raid *rdev, td_bio_ref bio)
{
	bio_list_add(&rdev->resync_context.trs_bios, bio);
	return 0;
}


/*
 * Internal to the mirror resync, process incoming bios, and queue them
 */
void trms_run_queue  (struct td_raid *rdev)
{
	td_bio_ref bio;
	struct bio_list  temp_requests_list;

	bio_list_init(&temp_requests_list);

	spin_lock_bh(&rdev->resync_context.trs_bio_lock);
	bio_list_merge(&temp_requests_list, &rdev->resync_context.trs_bios);
	bio_list_init(&rdev->resync_context.trs_bios);
	spin_unlock_bh(&rdev->resync_context.trs_bio_lock);

	/*
	 * We don't need the bio lock anymore, since we are running
	 * from the resync thread here.  This means our engine bio locks
	 * are consistent all the time
	 */
	while ( (bio = bio_list_pop(&temp_requests_list)) ) {
		int rc = tr_mirror_request_optimal(rdev, bio);
		if (rc < 0) {
			td_bio_endio(NULL, bio, rc, 0);
		}
	}
};



/* the LBA space will be split up and synced in this many chunks */
#define LBA_CHUNKS 128

#define INDEX_OF_ARRAY(base, addr, type) \
	(((unsigned long)(addr) - (unsigned long)(base)) / sizeof(type))

static struct td_ucmd *trms_get_ucmd(struct tr_mirror *rm)
{
	struct td_ucmd *ucmd = rm->resync.ucmds[rm->resync.pos_ucmd];

	(rm->resync.pos_ucmd)++;
	rm->resync.pos_ucmd = rm->resync.pos_ucmd % (2 * rm->resync.qd);

	return ucmd;
}

static struct td_ucmd** trms_get_free_read_slot(struct tr_mirror *rm)
{
	struct td_ucmd **read_slot;

	if (rm->resync.read_queue[rm->resync.pos_read])
		return NULL;

	read_slot = &rm->resync.read_queue[rm->resync.pos_read];

	rm->resync.pos_read++;
	rm->resync.pos_read = rm->resync.pos_read % rm->resync.qd;

	return read_slot;
}

static struct td_ucmd** trms_get_free_write_slot(struct tr_mirror *rm)
{
	struct td_ucmd **write_slot;

	if (rm->resync.write_queue[rm->resync.pos_write])
		return NULL;

	write_slot = &rm->resync.write_queue[rm->resync.pos_write];

	(rm->resync.pos_write)++;
	rm->resync.pos_write = rm->resync.pos_write % rm->resync.qd;

	return write_slot;
}

static int trms_start_4k_read(struct td_device *src, struct td_ucmd **read_slot, uint64_t block)
{
	uint64_t sector;
	int ssd;
	struct td_ucmd *ucmd = *read_slot;

	/* cmdgen works in LBA's so convert */
	ssd = block % td_eng_conf_hw_var_get(td_device_engine(src), SSD_COUNT);
	sector = (block / td_eng_conf_hw_var_get(td_device_engine(src), SSD_COUNT))
		* 4096 / td_eng_conf_hw_var_get(td_device_engine(src), HW_SECTOR_SIZE);

	/* set up the read ucmd */
	ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;
	ucmd->ioctl.data_len_to_device = 0;
	if (td_eng_cmdgen_chk(td_device_engine(src),
			bio_read4k,
			&ucmd->ioctl.cmd[0],
			ssd,
			sector,
			TD_INVALID_CORE_BUFID,
			false)) {
		return -EINVAL;
	}
	//td_eng_info(td_device_engine(src), "READ 0x%llx [%u/0x%llx] %p\n", block, ssd, sector, ucmd);


	/* start the read */
	td_ucmd_ready(ucmd);
	td_enqueue_ucmd(td_device_engine(src), ucmd);
	td_engine_sometimes_poke(td_device_engine(src));

	return 0;
}

static int trms_start_4k_write(struct td_device *dst, struct td_ucmd **write_slot, uint64_t block)
{
	struct td_ucmd *ucmd = *write_slot;
	td_cmd_t *tdcmd;

	/* set up the write */
	tdcmd = (td_cmd_t *)&(ucmd->ioctl.cmd[0]);
	tdcmd->cmd.decode.to_host = 0;

	ucmd->ioctl.data_len_from_device = 0;
	ucmd->ioctl.data_len_to_device = TERADIMM_DATA_BUF_SIZE;

	//td_eng_info(td_device_engine(dst), "WRITE BLOCK 0x%llx [%u/0x%llx] %p [%016llx]\n", block, tdcmd->cmd.port, (uint64_t)tdcmd->src.lba.lba, ucmd, *(uint64_t*)ucmd->data_virt);

	if (td_eng_cmdgen_chk(td_device_engine(dst),
			bio_write4k,
			&ucmd->ioctl.cmd[0],
			tdcmd->cmd.port,
			tdcmd->src.lba.lba,
			TD_INVALID_CORE_BUFID,
			false,
			TD_INVALID_WR_BUFID)) {
		return -EINVAL;
	}

	/* start the write */
	td_ucmd_ready(ucmd);
	td_enqueue_ucmd(td_device_engine(dst), ucmd);
	td_engine_sometimes_poke(td_device_engine(dst));

	return 0;
}

static void trms_destroy (struct td_raid *rdev)
{
	struct tr_mirror *rm = tr_mirror(rdev);

	if (rm->resync.ucmds) {
		int i;
		for (i = 0 ; i < (2 * rm->resync.qd) ; i++) {
			if (rm->resync.ucmds[i])
				td_ucmd_put(rm->resync.ucmds[i]);
		}
		kfree(rm->resync.ucmds);
	}
	if (rm->resync.read_queue)
		kfree(rm->resync.read_queue);
	if (rm->resync.write_queue)
		kfree(rm->resync.write_queue);
	
	memset(&rm->resync, 0, sizeof(rm->resync));
}


static int trms_init (struct td_raid *rdev, int qd)
{
	struct tr_mirror *rm = tr_mirror(rdev);
	int i;
	
	if (qd > 64) {
		td_raid_warn(rdev, "RESYNC: Limiting QD to 64\n");
		qd = 64;
	}

	memset(&rm->resync, 0, sizeof(rm->resync));
	rm->resync.qd = qd;

	td_raid_info(rdev, "RESYNC: Initialized with queue depth=%d\n", qd);

	/* allocate ucmd pool and read/write queues */
	rm->resync.ucmds = kzalloc((2 * qd) * sizeof(struct td_ucmd*), GFP_KERNEL);
	if (!rm->resync.ucmds)
		goto alloc_fail;
	rm->resync.read_queue = kzalloc(qd * sizeof(struct td_ucmd*), GFP_KERNEL);
	if (!rm->resync.read_queue)
		goto alloc_fail;
	rm->resync.write_queue = kzalloc(qd * sizeof(struct td_ucmd*), GFP_KERNEL);
	if (!rm->resync.write_queue)
		goto alloc_fail;

	for (i = 0 ; i < (2 * qd) ; i++) {
		/* allocate UCMDs */
		rm->resync.ucmds[i] = td_ucmd_alloc(PAGE_SIZE);
		if (!rm->resync.ucmds[i])
			goto alloc_fail;
	}
	
	return 0;

alloc_fail:
	trms_destroy(rdev);
	return -ENOMEM;
}


static void trms_reset(struct td_raid *rdev)
{
	struct tr_mirror *rm = tr_mirror(rdev);
	int qd = rm->resync.qd;
	rm->resync.pos_read = 0;
	rm->resync.pos_write = 0;
	rm->resync.pos_ucmd = 0;
	memset(rm->resync.write_queue, 0, sizeof(struct td_ucmd*)*qd);
	memset(rm->resync.read_queue, 0, sizeof(struct td_ucmd*)*qd);
}

static int trms_lba(struct td_raid *rdev, struct tr_member *trm_src, struct tr_member *trm_dst,
		uint64_t first_lba, uint64_t last_lba)
{
	struct tr_mirror *rm = tr_mirror(rdev);
	struct td_ucmd **oldest_read = NULL;
	struct td_ucmd **oldest_write = NULL;
	uint64_t last_block = 0;
	uint64_t cur_block_in = 0;
	uint64_t cur_block_out = 0;
	int rc = -EINVAL;
	int i = 0;

	/* LBA's must be 4k aligned and 512 bytes in size */
	if ( (first_lba & 0x7) || (last_lba & 0x7)
			|| (td_eng_conf_hw_var_get(td_device_engine(trm_src->trm_device), HW_SECTOR_SIZE) != 512)
			|| (td_eng_conf_hw_var_get(td_device_engine(trm_dst->trm_device), HW_SECTOR_SIZE) != 512) )
	{
		WARN_ON(1);
		goto invalid_geometry;
	}

	/* convert LBA's to blocks */
	cur_block_in = first_lba /
		(4096 / (td_eng_conf_hw_var_get(td_device_engine(trm_src->trm_device), HW_SECTOR_SIZE)));

	cur_block_out = cur_block_in;

	last_block = last_lba /
		(4096 / (td_eng_conf_hw_var_get(td_device_engine(trm_src->trm_device), HW_SECTOR_SIZE)));


	rm->counter[TR_MIRROR_COUNT_RESYNC_NEXT] = first_lba;

	i = 0;
	do
	{
		struct td_ucmd **read_slot;
		struct td_ucmd **write_slot;

		/* assign all read slots to ucmd's and start reads */
		while (cur_block_in < last_block) {
			read_slot = trms_get_free_read_slot(rm);
			if (!read_slot)
				break;

			*read_slot = trms_get_ucmd(rm);

			if (trm_src->trm_state != TR_MEMBER_ACTIVE) {
				rc = -ENODEV;
				td_raid_err(rdev, "RESYNC: Source device not active (state:%d)\n",
					trm_src->trm_state);
				goto read_error;
			}

			if (trms_start_4k_read(trm_src->trm_device, read_slot, cur_block_in)) {
				td_raid_err(rdev, "RESYNC: Source device read submit error\n");
				rc = -EIO;
				goto read_error;
			}

			cur_block_in++;
			rm->counter[TR_MIRROR_COUNT_RESYNC_NEXT] += 8;

			if (!oldest_read)
				oldest_read = read_slot;
		}

		/* wait for oldest read to complete */
		if (oldest_read) {
			trms_run_queue(rdev);
			if (td_ucmd_wait(*oldest_read)) {
				td_raid_err(rdev, "RESYNC: Source device read error\n");
				rc = -EIO;
				goto ucmd_fail;
			}
		}

		while (oldest_read && !(*oldest_read)->ioctl.result) {
			int read_slot_array_index = INDEX_OF_ARRAY(&rm->resync.read_queue[0], oldest_read, struct td_ucmd **);

			/* get a write slot */
			write_slot = trms_get_free_write_slot(rm);
			if (!write_slot)
				break;

#if 0
			td_dev_info(trm_src->trm_device, "READ %d DONE %p [%016llx] %d (%lld cycles)\n",
					read_slot_array_index, *oldest_read,
					*(uint64_t*)((*oldest_read)->data_virt),
					(*oldest_read)->ioctl.result,
					(*oldest_read)->ioctl.cycles.io.end - (*oldest_read)->ioctl.cycles.io.start);
#endif

			/* transfer completed read slot to write slot */
			*write_slot = *oldest_read;
			*oldest_read = NULL;

			if (trm_dst->trm_state != TR_MEMBER_SYNC) {
				td_raid_err(rdev, "RESYNC: Destination device terminated sync (state:%d)\n",
					trm_dst->trm_state);
				rc = -ENODEV;
				goto write_error;
			}

			/* start write */
			if (trms_start_4k_write(trm_dst->trm_device, write_slot, cur_block_out)) {
				rc = -EIO;
				td_raid_err(rdev, "RESYNC: Destination device write submit error\n");
				goto write_error;
			}

			/* if necessary update oldest write */
			if (!oldest_write)
				oldest_write = write_slot;

			/* update oldest read */
			if (rm->resync.read_queue[(read_slot_array_index + 1) % rm->resync.qd])
				oldest_read = &rm->resync.read_queue[(read_slot_array_index + 1) % rm->resync.qd];
			else
				oldest_read = NULL;

			/* start a new read */
			if (cur_block_in < last_block) {
				read_slot = trms_get_free_read_slot(rm);
				if (!read_slot)
					break;

				*read_slot = trms_get_ucmd(rm);

				if (trm_src->trm_state != TR_MEMBER_ACTIVE) {
					td_raid_err(rdev, "RESYNC: Source device not active (state:%d)\n",
						trm_src->trm_state);
					rc = -ENODEV;
					goto read_error;
				}

				if (trms_start_4k_read(trm_src->trm_device, read_slot, cur_block_in)) {
					td_raid_err(rdev, "RESYNC: Source device read submit error2\n");
					rc = -EIO;
					goto read_error;
				}

				cur_block_in++;
				rm->counter[TR_MIRROR_COUNT_RESYNC_NEXT] += 8;
			}
			trms_run_queue(rdev);
		}

		/* wait for oldest write to complete */
		if (oldest_write) {
			trms_run_queue(rdev);
			if (td_ucmd_wait(*oldest_write)) {
				td_raid_err(rdev, "RESYNC: Destination device write error\n");
				rc = -EIO;
				goto ucmd_fail;
			}
		}

		/* free write slot and update oldest write */
		while (oldest_write
				&& !(*oldest_write)->ioctl.result) {

			int write_slot_array_index = INDEX_OF_ARRAY(&rm->resync.write_queue[0], oldest_write, struct td_ucmd **);

			/* free the write slot */
			*oldest_write = NULL;

			/* update oldest write */
			if (rm->resync.write_queue[(write_slot_array_index + 1) % rm->resync.qd])
				oldest_write = &rm->resync.write_queue[(write_slot_array_index + 1) % rm->resync.qd];
			else
				oldest_write = NULL;

			cur_block_out++;
			rm->counter[TR_MIRROR_COUNT_RESYNC_DONE] += 8;
		}
	} while (cur_block_out < last_block);

	rc = 0;

read_error:
write_error:
ucmd_fail:

invalid_geometry:
	return rc;
}

int tr_mirror_resync_thread(void *arg)
{
	int i;
	int src_idx = 0;
	int dst_idx = 0;
	int rc;
	uint64_t chunk_start, max_lba;

	struct td_raid *rdev = arg;
	struct tr_mirror *rm = tr_mirror(rdev);
	struct tr_member *trm_src = NULL;
	struct tr_member *trm_dst = NULL;

	trm_dst = NULL;
	rc = -ENODEV;
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;

		if (tr_raid_member_check_state(trm, ACTIVE)) {
			src_idx = i;
			trm_src = trm;
		}

		if (tr_raid_member_check_state(trm, SYNC)) {
			dst_idx = i;
			trm_dst = trm;
		}
	}

	if (!trm_src || !trm_dst)
		goto no_device;

	td_raid_info(rdev, "RESYNC started\n");
	td_raid_info(rdev, "RESYNC: Reading from source device: %s\n",
			td_device_name(trm_src->trm_device));
	td_raid_info(rdev, "RESYNC: Writing to destination device: %s\n",
			td_device_name(trm_dst->trm_device));

	/* Tell the world we're active */
	spin_lock_bh(&rdev->resync_context.trs_bio_lock);
	rdev->resync_context._trs_queue_bio = trms_queue_bio;
	spin_unlock_bh(&rdev->resync_context.trs_bio_lock);

	rc = trms_init(rdev, trms_queue_depth);
	if (rc)
		goto sync_error;

	chunk_start = 0;
	max_lba = tr_conf_var_get(rdev, CAPACITY)>>SECTOR_SHIFT;	
	//max_lba = min(max_lba, LBA_CHUNKS * 4096ULL);

	rm->counter[TR_MIRROR_COUNT_RESYNC_SRC] = src_idx;
	rm->counter[TR_MIRROR_COUNT_RESYNC_DST] = dst_idx;
	rm->counter[TR_MIRROR_COUNT_RESYNC_LAST] = max_lba;
	rm->counter[TR_MIRROR_COUNT_RESYNC_DONE] = 0;


	while (chunk_start < max_lba) {
		uint64_t last_lba = chunk_start + LBA_CHUNKS;

		if (last_lba >= max_lba)
			last_lba = max_lba;

		td_engine_block_bio_range(td_device_engine(trm_src->trm_device),
						chunk_start, last_lba, 1);
		td_engine_block_bio_range(td_device_engine(trm_dst->trm_device),
						chunk_start, last_lba, 1);

		rc = trms_lba(rdev, trm_src, trm_dst, chunk_start, last_lba);

		td_engine_unblock_bio_range(td_device_engine(trm_src->trm_device));
		td_engine_unblock_bio_range(td_device_engine(trm_dst->trm_device));

		chunk_start = last_lba;
		if (rc)
			break;
		
		if (chunk_start != max_lba) {
			if (rm->resync.qd != trms_queue_depth) {
				trms_destroy(rdev);
				trms_init(rdev, trms_queue_depth);
			} else {
				trms_reset(rdev);
			}
		}
	}
sync_error:
	trms_destroy(rdev);

	mb();
	rdev->resync_context._trs_queue_bio = NULL;
	mb();

	
	trms_run_queue(rdev);

	if (rc) {
		td_raid_err(rdev, "RESYNC: Failed to sync %s to %s.\n",
			td_device_name(trm_src->trm_device),
			td_device_name(trm_dst->trm_device));
		td_raid_change_member(rdev, dst_idx, TR_MEMBER_FAILED);
	} else {
		td_raid_info(rdev, "RESYNC: Synced %s to %s.\n",
			td_device_name(trm_src->trm_device),
			td_device_name(trm_dst->trm_device));
		td_raid_change_member(rdev, dst_idx, TR_MEMBER_ACTIVE);
	}

no_device:
	rdev->resync_context.resync_task = NULL;
	return rc;
}
