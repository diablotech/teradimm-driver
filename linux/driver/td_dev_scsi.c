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
#include "td_eng_hal.h"
#include "td_ata_cmd.h"
#include "td_params.h"
#include "td_dev_scsi.h"
#include "td_dev_ata.h"

static int td_scsi_cmd_ata12_pass(struct td_scsi_cmd *cmd)
{
	int rc = 0;
	struct td_ata_pt_cmd *ata_pt_cmd = (struct td_ata_pt_cmd *)cmd->request;

	switch(ata_pt_cmd->p12.cmd) {
	case ATA_CMD_ID_ATA:
		rc = td_dev_ata_ident(cmd);
		break;
	default:
		printk("Error ATA12 pass cmd %02X unknown\n", ata_pt_cmd->p12.cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int td_scsi_cmd_ata16_pass(struct td_scsi_cmd *cmd)
{
	int rc = 0;
	int data_from_device;
	int data_to_device;

	struct td_ata_pt_cmd *ata_pt_cmd = (struct td_ata_pt_cmd *)cmd->request;

	switch(ata_pt_cmd->p16.cmd) {
	case ATA_CMD_ID_ATA:
		rc = td_dev_ata_ident(cmd);
		break;
	case ATA_CMD_SMART:
		rc = td_dev_ata16_smart(cmd);
		break;
	case ATA_CMD_SEC_ERASE_PREP:
	case ATA_CMD_SEC_ERASE_UNIT:
		rc = td_dev_ata16_security(cmd);
		break;
	case 0x2F:
		printk("Retrieve log treated as generic\n");
		data_from_device = 512;
		data_to_device = 0;
		rc = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
		break;
	default:
		printk("Error ATA16 pass cmd %02X unknown\n", ata_pt_cmd->p16.cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int td_scsi_cmd_inquiry(struct td_scsi_cmd *cmd)
{
	struct td_inq_cmd *inq = (struct td_inq_cmd *)cmd->request;
	td_ata_inq_resp_t *response = (td_ata_inq_resp_t *)cmd->dxferp;
	struct td_page00r *page00r = &response->page00r;
	unsigned char *sb = cmd->sense;
	int rc = 0;

	/* check that return buffer can hold a standard INQUIRY response */
	if (cmd->dxfer_len < 36) { /* SPC-4 */
		rc = -EINVAL;
		goto too_small;
	}

	/* handle only standard inquiry */
	if ((inq->_b1_1) || (inq->evpd)) {
		sb[0] = 0x70; /* error code */
		sb[2] = ILLEGAL_REQUEST;
		sb[7] = 0xa; /* additional sense length */
		sb[12] = 0x24; /* INVALID FIELD IN CDB */

		cmd->sense_len = 18;
		cmd->status = SAM_STAT_CHECK_CONDITION;
		rc = DRIVER_SENSE;

		goto error;
	}

	memset(page00r, 0, sizeof(*page00r));

	/* direct access block device */
	page00r->dev_type = 0x0;

	/* connected */
	page00r->qualifier = 0x0;

	/* claim SPC-4 conformance */
	page00r->version = 0x6;

	/* response format complies to SPC-4 standard */
	page00r->resp_format = 0x2;

	/* response length count does not include header size */
	page00r->size = cmd->dxfer_len - 5; /* FIXME: set this more manually */

	/* T10 vendor identification */
	memcpy(page00r->t10, cmd->odev->vendor, sizeof(cmd->odev->vendor) - 1);

	/* product identification */
	memcpy(page00r->pid, cmd->odev->model, sizeof(cmd->odev->model) - 1);

	/* product revision level */
	memcpy(page00r->rev, cmd->odev->revision, sizeof(cmd->odev->revision) - 1);

	cmd->status = SAM_STAT_GOOD;
	cmd->resid = cmd->dxfer_len - 36;

too_small:
error:
	return rc;
}

int td_osdev_scsi_command(struct td_scsi_cmd *cmd)
{
	int rc = -EINVAL;

	switch (*cmd->request) {
	case ATA_16:
	{
		rc = td_scsi_cmd_ata16_pass(cmd);

		break;
	}
	case ATA_12:
	{
		rc = td_scsi_cmd_ata12_pass(cmd);

		break;
	}
	case INQUIRY:
	{
		rc = td_scsi_cmd_inquiry(cmd);

		break;
	}
	default:
	{
		unsigned char *sb = cmd->sense;
		sb[0] = 0x70; /* error code */
		sb[2] = ILLEGAL_REQUEST;
		sb[7] = 0xa; /* additional sense length */
		sb[12] = 0x20; /* ILLEGAL COMMAND */

		cmd->sense_len = 18;
		cmd->status = SAM_STAT_CHECK_CONDITION;
		rc = DRIVER_SENSE;

		break;
	}
	}

	return rc;
}
