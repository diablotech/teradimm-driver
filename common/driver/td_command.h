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

#ifndef _td_COMMAND_H_
#define _td_COMMAND_H_

#include "td_engine.h"
#include "td_protocol.h"

struct td_engine;
struct td_token;

/* these commands update the tok->cmd_bytes appropriately */
extern int td_cmdgen_ucmd (uint64_t bytes[8], struct td_ucmd* ucmd, uint16_t core_buf, uint8_t wep);
extern int td_cmd_gen_ucmd(struct td_engine *eng, struct td_token *tok);
extern int td_cmd_gen_bio(struct td_engine *eng, struct td_token *tok);
extern int td_cmd_gen_deallocate(struct td_engine *eng, struct td_token *tok);

extern int td_cmd_gen_seq_advance(struct td_engine *eng, struct td_token *tok);

extern int td_cmd_gen_SEC_dup(struct td_engine *eng, struct td_token *tok);

/** check status for a token; if finished or timedout update tok->result and
 * return 1; return 0 otherwise. */
extern int td_cmd_status_check(struct td_token *tok);
extern int td_cmd_ata_ok(td_cmd_t *tdcmd);
extern int td_cmd_ata_filter(uint8_t *ata_cmd);
/** clear deallocation byte from command attached to token */
extern void td_cmd_clear_deallocation(struct td_token *tok);

extern int td_cmd_gen_ssd_pt(struct td_engine *eng, struct td_ucmd *ucmd, struct td_ioctl_device_ssd_pt *pt);

extern struct td_command_generator td_cmdgen_teradimm;

extern int td_replay_command_on_ooo_token(struct td_token *tok, uint8_t last_status_byte);
#endif
