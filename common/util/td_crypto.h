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
#ifndef __td_crypto_h__
#define __td_crypto_h__

#ifndef SHA1_LENGTH
#define SHA1_LENGTH 20
#endif

union td_sha1_block {
	uint8_t c[64];
	uint32_t l[16];
};

struct td_sha1_context {
	uint32_t	        state[5];
	uint32_t	        count[2];
	uint32_t	        reserved0[1]; // Memory alignment padding
	uint8_t		        buffer[64];
	uint8_t		        digest[SHA1_LENGTH];
	uint32_t	        reserved1[3]; // Memory alignment padding

	uint8_t                  workspace[64];
	union td_sha1_block     *pblock; // SHA1 pointer to the byte array above
};



struct td_sha1_state {
	struct td_sha1_context  ctx;
};

int td_sha1_init(struct td_sha1_state*);
int td_sha1_update(struct td_sha1_state*, void*, uint len);
int td_sha1_final(struct td_sha1_state*, uint8_t* out);
int td_sha1_free(struct td_sha1_state*);

#endif
