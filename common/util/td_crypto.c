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


/*
 * This is an adaptation of the C++ version of SHA1, based on work by
 * Dominik Reichl.
 *
 *  100% free public domain implementation of the SHA-1 algorithm
 *   by Dominik Reichl <dominik.reichl@t-online.de>
 *      Web: http://www.dominik-reichl.de/
 */

#ifdef __KERNEL__ 
#include "td_kdefn.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif

#include "td_compat.h"

#include "td_crypto.h"

#ifndef ROL32
#define ROL32(p_val32,p_nBits) (((p_val32)<<(p_nBits))|((p_val32)>>(32-(p_nBits))))
#endif


/*
 * These are the public side of our manifest sign keys
 */
struct td_rsa_pub_key td_rsa_pub_keys[] = {
	/* Pairs to construct RSA keys in hex*/
	/* Modulus */
	{"00cef09f0ccf8564e386e68b84e268307600e58cf6e6b1f6d678dad178a22e625f5dafd37d44e91c5be118f7e6c3b00446074f770d50395b78a4ef20c2341d8d8c8d1dbce2f2020ff5c5a5a699e1a7441aced5c32c9129dcdc5063fa696c9c3fe1ccd32a2b258f2d81272666aded950f185571e13f34cc5c2a268e3f8d5208f894687033cd48b96e19ce83ee52d14cd0a359c88883492a6bcc41ce3bbc63faa85c436a190c1e93ee176535d5a90588a43aa1713d78d63a3f51d544739187931d949d90515293bcc7a7fc52c225542dd7c82ec47669003b1a698aaece5ea19878b7db4603e543fa83e6f2324a1221d6b846aa03860adf3f6317ea7b2229242d7471",
		/* Exponent */
		"10001"},
/*	{"",
		""},
	{"",
		""},*/
};

/*
 * Thankfully, we are x86_64 - Little Endian only
 */
static inline uint32_t SHABLK0(struct td_sha1_context *ctx, int i)
{
	ctx->pblock->l[i] = (ROL32(ctx->pblock->l[i],24) & 0xFF00FF00) |
		(ROL32(ctx->pblock->l[i],8) & 0x00FF00FF);
	return ctx->pblock->l[i];
}

static inline uint32_t SHABLK(struct td_sha1_context *ctx, int i)
{
	ctx->pblock->l[i&15] = ROL32(ctx->pblock->l[(i+13)&15] ^
				ctx->pblock->l[(i+8)&15] ^
				ctx->pblock->l[(i+2)&15] ^
				ctx->pblock->l[i&15],1);
	return ctx->pblock->l[i&15];
}

// SHA-1 rounds
#define S_R0(_ctx, v,w,x,y,z,i) do {z+=((w&(x^y))^y)+SHABLK0(_ctx, i)+0x5A827999+ROL32(v,5);w=ROL32(w,30);} while (0)
#define S_R1(_ctx, v,w,x,y,z,i) do {z+=((w&(x^y))^y)+SHABLK(_ctx, i)+0x5A827999+ROL32(v,5);w=ROL32(w,30);} while (0)
#define S_R2(_ctx, v,w,x,y,z,i) do {z+=(w^x^y)+SHABLK(_ctx, i)+0x6ED9EBA1+ROL32(v,5);w=ROL32(w,30);} while (0)
#define S_R3(_ctx, v,w,x,y,z,i) do {z+=(((w|x)&y)|(w&x))+SHABLK(_ctx, i)+0x8F1BBCDC+ROL32(v,5);w=ROL32(w,30);} while (0)
#define S_R4(_ctx, v,w,x,y,z,i) do {z+=(w^x^y)+SHABLK(_ctx, i)+0xCA62C1D6+ROL32(v,5);w=ROL32(w,30);} while (0)



static void sha1_transform (struct td_sha1_context *ctx, uint32_t* pState, const uint8_t* pBuffer)
{
	uint32_t a = pState[0], b = pState[1], c = pState[2], d = pState[3], e = pState[4];

	memcpy(ctx->pblock, pBuffer, 64);

	// 4 rounds of 20 operations each, loop unrolled
	S_R0(ctx,a,b,c,d,e, 0); S_R0(ctx,e,a,b,c,d, 1); S_R0(ctx,d,e,a,b,c, 2); S_R0(ctx,c,d,e,a,b, 3);
	S_R0(ctx,b,c,d,e,a, 4); S_R0(ctx,a,b,c,d,e, 5); S_R0(ctx,e,a,b,c,d, 6); S_R0(ctx,d,e,a,b,c, 7);
	S_R0(ctx,c,d,e,a,b, 8); S_R0(ctx,b,c,d,e,a, 9); S_R0(ctx,a,b,c,d,e,10); S_R0(ctx,e,a,b,c,d,11);
	S_R0(ctx,d,e,a,b,c,12); S_R0(ctx,c,d,e,a,b,13); S_R0(ctx,b,c,d,e,a,14); S_R0(ctx,a,b,c,d,e,15);
	S_R1(ctx,e,a,b,c,d,16); S_R1(ctx,d,e,a,b,c,17); S_R1(ctx,c,d,e,a,b,18); S_R1(ctx,b,c,d,e,a,19);
	S_R2(ctx,a,b,c,d,e,20); S_R2(ctx,e,a,b,c,d,21); S_R2(ctx,d,e,a,b,c,22); S_R2(ctx,c,d,e,a,b,23);
	S_R2(ctx,b,c,d,e,a,24); S_R2(ctx,a,b,c,d,e,25); S_R2(ctx,e,a,b,c,d,26); S_R2(ctx,d,e,a,b,c,27);
	S_R2(ctx,c,d,e,a,b,28); S_R2(ctx,b,c,d,e,a,29); S_R2(ctx,a,b,c,d,e,30); S_R2(ctx,e,a,b,c,d,31);
	S_R2(ctx,d,e,a,b,c,32); S_R2(ctx,c,d,e,a,b,33); S_R2(ctx,b,c,d,e,a,34); S_R2(ctx,a,b,c,d,e,35);
	S_R2(ctx,e,a,b,c,d,36); S_R2(ctx,d,e,a,b,c,37); S_R2(ctx,c,d,e,a,b,38); S_R2(ctx,b,c,d,e,a,39);
	S_R3(ctx,a,b,c,d,e,40); S_R3(ctx,e,a,b,c,d,41); S_R3(ctx,d,e,a,b,c,42); S_R3(ctx,c,d,e,a,b,43);
	S_R3(ctx,b,c,d,e,a,44); S_R3(ctx,a,b,c,d,e,45); S_R3(ctx,e,a,b,c,d,46); S_R3(ctx,d,e,a,b,c,47);
	S_R3(ctx,c,d,e,a,b,48); S_R3(ctx,b,c,d,e,a,49); S_R3(ctx,a,b,c,d,e,50); S_R3(ctx,e,a,b,c,d,51);
	S_R3(ctx,d,e,a,b,c,52); S_R3(ctx,c,d,e,a,b,53); S_R3(ctx,b,c,d,e,a,54); S_R3(ctx,a,b,c,d,e,55);
	S_R3(ctx,e,a,b,c,d,56); S_R3(ctx,d,e,a,b,c,57); S_R3(ctx,c,d,e,a,b,58); S_R3(ctx,b,c,d,e,a,59);
	S_R4(ctx,a,b,c,d,e,60); S_R4(ctx,e,a,b,c,d,61); S_R4(ctx,d,e,a,b,c,62); S_R4(ctx,c,d,e,a,b,63);
	S_R4(ctx,b,c,d,e,a,64); S_R4(ctx,a,b,c,d,e,65); S_R4(ctx,e,a,b,c,d,66); S_R4(ctx,d,e,a,b,c,67);
	S_R4(ctx,c,d,e,a,b,68); S_R4(ctx,b,c,d,e,a,69); S_R4(ctx,a,b,c,d,e,70); S_R4(ctx,e,a,b,c,d,71);
	S_R4(ctx,d,e,a,b,c,72); S_R4(ctx,c,d,e,a,b,73); S_R4(ctx,b,c,d,e,a,74); S_R4(ctx,a,b,c,d,e,75);
	S_R4(ctx,e,a,b,c,d,76); S_R4(ctx,d,e,a,b,c,77); S_R4(ctx,c,d,e,a,b,78); S_R4(ctx,b,c,d,e,a,79);

	// Add the working vars back into state
	pState[0] += a;
	pState[1] += b;
	pState[2] += c;
	pState[3] += d;
	pState[4] += e;

}



int td_sha1_init(struct td_sha1_state*st)
{
	memset(st, 0, sizeof(*st));

	// SHA1 initialization constants
	st->ctx.state[0] = 0x67452301;
	st->ctx.state[1] = 0xEFCDAB89;
	st->ctx.state[2] = 0x98BADCFE;
	st->ctx.state[3] = 0x10325476;
	st->ctx.state[4] = 0xC3D2E1F0;

	st->ctx.count[0] = 0;
	st->ctx.count[1] = 0;

	st->ctx.pblock = (union td_sha1_block*)st->ctx.workspace;
	return 0;
}

int td_sha1_update(struct td_sha1_state *st, void *data, uint len)
{
	if (data && len) {
		uint32_t i;
		uint32_t j = ((st->ctx.count[0] >> 3) & 0x3F);

		if ((st->ctx.count[0] += (len << 3)) < (len << 3))
			++st->ctx.count[1]; // Overflow

		st->ctx.count[1] += (len >> 29);

		if((j + len) > 63)
		{
			i = 64 - j;
			memcpy(&st->ctx.buffer[j], data, i);
			sha1_transform(&st->ctx, st->ctx.state, st->ctx.buffer);

			for( ; (i + 63) < len; i += 64)
				sha1_transform(&st->ctx, st->ctx.state, PTR_OFS(data, i) );

			j = 0;
		}
		else i = 0;

		if((len - i) != 0)
			memcpy(&st->ctx.buffer[j], PTR_OFS(data,i), len - i);
	}

	return 0;
}

int td_sha1_final(struct td_sha1_state *st, uint8_t* out)
{
	uint32_t i;
	uint8_t pbFinalCount[8];

	for(i = 0; i < 8; ++i) {
		pbFinalCount[i] = (uint8_t)((st->ctx.count[((i >= 4) ? 0 : 1)] >> ((3 - (i & 3)) * 8) ) & 0xFF); // Endian independent
	}

	td_sha1_update(st, (uint8_t*)"\200", 1);

	while((st->ctx.count[0] & 504) != 448) {
		td_sha1_update(st, (uint8_t*)"\0", 1);
	}

	td_sha1_update(st, pbFinalCount, 8); // Cause a Transform()

	for(i = 0; i < SHA1_LENGTH; ++i) {
		out[i] = (uint8_t)((st->ctx.state[i >> 2] >> ((3 - (i & 3)) * 8)) & 0xFF);
	}

	return 0;
}

int td_sha1_free(struct td_sha1_state *st)
{
	return 0;
}
