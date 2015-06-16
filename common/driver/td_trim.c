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

#include "td_defs.h"

#include "td_bio.h"
#include "td_device.h"
#include "td_engine.h"

/* returns the number of discard bios that need to be created from this bio */
int td_bio_trim_count(td_bio_ref bio, struct td_engine *eng)
{
	uint64_t start, end; /*start and end lba*/
	uint64_t start_off, end_off; /*start and end lba offset*/
	uint64_t lbas, ssd_stripes;
	uint64_t stripe_size = td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS);
	uint64_t stripes = 0;
	uint64_t hw_sec = td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE);
	uint64_t count = 0;
	uint64_t max_stripe;
	uint8_t ssd_count = (uint8_t)td_eng_conf_hw_var_get(eng, SSD_COUNT);

	/* Fast path. */
	if(likely(td_bio_is_discard(bio) == 0))
		goto not_discard;

	/* Set to byte size */
	start = td_bio_get_sector_offset(bio) << SECTOR_SHIFT;
	end = start + td_bio_get_byte_size(bio) - 1;

	/* Convert to LBA */
	start /= hw_sec;
	end /= hw_sec;

	/* Calc the LBAs */
	lbas = 1 + end - start;
	/* LBA start fragment */
	start_off = start % stripe_size;
	if (start_off)
		start_off = stripe_size - start_off;

	end_off = (end + 1) % stripe_size;

	/* For small files, the start_off is the full discard. */
	if (start_off >= lbas) {
		count++;
		goto just_one;
	}

	if (unlikely(start_off)) {
		/* The front is not aligned. */
		start -= start_off;
		lbas -= start_off;
		/* an entire trim for a part of a stripe? */
		count++;
	}

	if (unlikely(end_off)) {
		/* The back is not aligned. */
		end -= end_off;
		lbas -= end_off;
		/* an entire trim for a part of a stripe? */
		count++;
	}

	if (unlikely(!lbas))
		goto just_fragments;

	/* Get the rest of the stripe count. */
	stripes = lbas / stripe_size;

	if (stripes/ssd_count == 0) {
		count += stripes;
		goto under1_per_ssd;
	}
	/* Overcautious.. make sure we won't overflow the command.. */
	ssd_stripes = stripes/ssd_count +
		(stripes % ssd_count ? 1: 0);

	max_stripe = TD_MAX_DISCARD_LBA_COUNT/stripe_size;

	if (unlikely(ssd_stripes > max_stripe)) {
		/* If we would overflow the command, then plan for more
		 * bio's..although this is not supported in td_split_bio right
		 * now. */
		count += (ssd_stripes/max_stripe) * ssd_count;
		/* Left over stripes */
		stripes %= ssd_count;
		count += stripes;
		goto massive_trim;
	}

	count += ssd_count;

massive_trim:
under1_per_ssd:
just_fragments:
just_one:
not_discard:
	return (int)count;

}

