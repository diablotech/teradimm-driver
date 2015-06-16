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



#include "td_device.h"
#include "td_ioctl.h"
#include "td_engine.h"
#include "td_compat.h"
#include "td_ucmd.h"
#include "td_ata_cmd.h"
#include "td_command.h"
#include "td_devgroup.h"
#include "td_dev_scsi.h"
#include "td_eng_hal.h"
#include "td_ata_cmd.h"
#include "td_params.h"

#define CDB_BUF_SIZE 16
#define SENSE_BUF_SIZE 252 /* SPC-4 */
#define DXFERP_BUF_SIZE 1024

int td_filter_sgio(enum td_osdev_type odev_type, uint8_t *scsi_cmd)
{
	int rc = -EINVAL;
	switch (odev_type) {
	case TD_OSDEV_DEVICE:
		rc = td_cmd_scsi_filter(scsi_cmd);
		break;
	case TD_OSDEV_RAID:
		switch (scsi_cmd[0]) {
		case INQUIRY:
			rc = 0;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return rc;
}

int td_sgio_v3(struct td_osdev *odev, sg_io_hdr_t *hdr)
{
	int rc;
	struct td_scsi_cmd cmd;

	if (hdr->flags & SG_FLAG_MMAP_IO) {
		if (hdr->flags & SG_FLAG_DIRECT_IO) {
			rc = -EINVAL;
			goto mmap_direction_inval;
		}
	}

	if (!access_ok(VERIFY_READ, hdr->cmdp, hdr->cmd_len)) {
		rc = -EPERM;
		goto perm_fail;
	}

	if ((!hdr->cmdp) || (hdr->cmd_len < 6 ) || (hdr->cmd_len > CDB_BUF_SIZE)) {
		rc = -EMSGSIZE;
		goto sgio_invalid;
	}

	/* scatter gather not supported */
	if (hdr->iovec_count) {
		rc = -EINVAL;
		goto sgio_invalid;
	}

	/* fill the SCSI command structure */
	memset(&cmd, 0, sizeof(cmd));
	cmd.odev = odev;
	cmd.request = kzalloc(CDB_BUF_SIZE, GFP_KERNEL);
	cmd.dxferp = kzalloc(DXFERP_BUF_SIZE, GFP_KERNEL);
	cmd.sense = kzalloc(SENSE_BUF_SIZE, GFP_KERNEL);
	cmd.dxfer_len = hdr->dxfer_len;

	if (!cmd.request || !cmd.dxferp || !cmd.sense) {
		rc = -ENOMEM;
		goto alloc_fail;
	}

	/* transfer userspace data */
	if (copy_from_user(cmd.request, hdr->cmdp, hdr->cmd_len)) {
		rc = -EFAULT;
		goto error_copy;
	}
	if (hdr->dxfer_direction == SG_DXFER_TO_DEV) { /* SG_DXFER_TO_FROM_DEV ? */
		if (copy_from_user(cmd.dxferp, hdr->dxferp, hdr->dxfer_len)) {
			rc = -EFAULT;
			goto error_copy;
		}
	}

	/* execute the SCSI command */
	hdr->driver_status = td_osdev_scsi_command(&cmd);

	/* format output members with result */
	hdr->status = cmd.status;
	hdr->masked_status = ((cmd.status & 0x3e) >> 1);
	hdr->host_status = 0;
	hdr->sb_len_wr = 0;

	if (hdr->dxfer_direction == SG_DXFER_FROM_DEV) {
		if (copy_to_user(hdr->dxferp, cmd.dxferp, hdr->dxfer_len)) {
			rc = -EFAULT;
			goto error_copy;
		}
	}

	hdr->resid = cmd.resid;

	if (cmd.sense_len && hdr->sbp) {
		int len = min_t(unsigned char, hdr->mx_sb_len, cmd.sense_len);

		if (copy_to_user(hdr->sbp, cmd.sense, len)) {
			rc = -EFAULT;
			goto error_copy;
		}

		hdr->sb_len_wr = len;
	}

	rc = 0;

error_copy:
alloc_fail:
	if (cmd.request)
		kfree(cmd.request);
	if (cmd.dxferp)
		kfree(cmd.dxferp);
	if (cmd.sense)
		kfree(cmd.sense);
sgio_invalid:
perm_fail:
mmap_direction_inval:

	return rc;
}

int td_dev_validate_sgio_v4_hdr(struct sg_io_v4 *hdr)
{
	int ret = 0;

	switch (hdr->protocol) {
		case BSG_PROTOCOL_SCSI:
			switch (hdr->subprotocol) {
			case BSG_SUB_PROTOCOL_SCSI_CMD:
			        break;
			default:
			        ret = -EINVAL;
			}
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

int td_sgio_v4(struct td_osdev *odev, struct sg_io_v4 *hdr)
{
	int rc;
	struct td_scsi_cmd cmd;

	if (td_dev_validate_sgio_v4_hdr(hdr)) {
		rc = -EINVAL;
		goto sgio_invalid_header;
	}

	if (!access_ok(VERIFY_READ, hdr->request, hdr->request_len)) {
		rc = -EPERM;
		goto perm_fail;
	}

	if ( (!hdr->request) || (hdr->request_len < 6) || (hdr->request_len > CDB_BUF_SIZE) ) {
		rc = -EMSGSIZE;
		goto sgio_invalid;
	}

	/* per upstream commit 0c6a89, dout_iovec_count and din_iovec_count
	 * are not yet supported */
	if (hdr->dout_iovec_count || hdr->din_iovec_count) {
		rc = -EINVAL;
		goto sgio_invalid;
	}

	/* fill the SCSI command structure */
	memset(&cmd, 0, sizeof(cmd));
	cmd.odev = odev;
	cmd.request = kzalloc(CDB_BUF_SIZE, GFP_KERNEL);
	cmd.dxferp = kzalloc(DXFERP_BUF_SIZE, GFP_KERNEL);
	cmd.sense = kzalloc(SENSE_BUF_SIZE, GFP_KERNEL);
	cmd.dxfer_len = hdr->dout_xfer_len ? hdr->dout_xfer_len : hdr->din_xfer_len;

	if (!cmd.request || !cmd.dxferp || !cmd.sense) {
		rc = -ENOMEM;
		goto alloc_fail;
	}

	/* transfer userspace data */
	if (copy_from_user(cmd.request, (void *)hdr->request, hdr->request_len)) {
		rc = -EFAULT;
		goto error_copy;
	}
	if (hdr->dout_xfer_len) {
		if (copy_from_user(cmd.dxferp, (void *)hdr->dout_xferp, hdr->dout_xfer_len)) {
			rc = -EFAULT;
			goto error_copy;
		}
	}

	/* execute the SCSI command */
	hdr->driver_status = td_osdev_scsi_command(&cmd);

	/* format output members with result */
	hdr->device_status = cmd.status;
	hdr->transport_status = 0;
	hdr->response_len = 0;

	if (hdr->din_xfer_len) {
		if (copy_to_user((void *)hdr->din_xferp, cmd.dxferp, hdr->din_xfer_len)) {
			rc = -EFAULT;
			goto error_copy;
		}
	}

	hdr->din_resid = hdr->din_xfer_len ? cmd.resid : 0;
	hdr->dout_resid = hdr->dout_xfer_len ? cmd.resid : 0;

	if (cmd.sense_len && hdr->response) {
		int len = min_t(unsigned int, hdr->max_response_len, cmd.sense_len);

		if (copy_to_user((void *)hdr->response, cmd.sense, len)) {
			rc = -EFAULT;
			goto error_copy;
		}

		hdr->response_len = len;
	}
		
	rc = 0;

error_copy:
alloc_fail:
	if (cmd.request)
		kfree(cmd.request);
	if (cmd.dxferp)
		kfree(cmd.dxferp);
	if (cmd.sense)
		kfree(cmd.sense);
sgio_invalid:
perm_fail:
sgio_invalid_header:
	return rc;
}

int td_block_sgio(struct td_osdev *odev, void *hdr)
{
	int rc;

	switch (*(int *)hdr) {
	case 'S': /* V3 */
		rc = td_filter_sgio(odev->type, ((sg_io_hdr_t *)hdr)->cmdp);
		if (rc)
			goto filtered;

		rc = td_sgio_v3(odev, (sg_io_hdr_t *)hdr);
		break;
	case 'Q': /* V4 */
		rc = td_filter_sgio(odev->type, (uint8_t *)((struct sg_io_v4 *)hdr)->request);
		if (rc)
			goto filtered;

		rc = td_sgio_v4(odev, (struct sg_io_v4 *)hdr);
		break;
	default:
		pr_err("TSA: SG_IO %d not supported.\n", *(int *)hdr);
		rc = -EINVAL;
		break;
	}

filtered:
	return rc;
}
