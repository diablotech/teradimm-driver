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

#include "td_token.h"
#include "td_util.h"
#include "td_engine.h"
#include "td_bio.h"
#include "td_checksum.h"
#include "td_memcpy.h"
#include "td_protocol.h"
#include "td_memspace.h"
#include "td_cache.h"

/* ======================== normal copy to/from bio ======================== */

/**
 * \brief copy from device buffer to tok->host.bio
 *
 * @param tok - token with outstanding command
 * @param dev_data_src - kernel virtual address of device buffer
 * @param dev_meta_src 
 * @return number of bytes copied
 *
 * Used by eng->ops->read_page when used for bio reads.
 */
int td_token_dev_to_bio(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint accumulated = 0;
	const char *src = dev_data_src;
	const char *alias = dev_data_alias;
	struct bio_vec bvec;
	td_bvec_iter i;

	/* If we have LBA_OFS, we need to adjust the src */
	src += tok->lba_ofs;
	alias += tok->lba_ofs;

	to_copy = min_t(int, tok->len_dev_to_host,
			td_bio_get_byte_size(tok->host.bio));

	if (TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
	if (td_cache_flush_test(eng, PRE, RDBUF))
		mb();
	}

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		char *dst;
		int copy_len;
		TD_MAP_BIO_DECLARE;

		copy_len = min_t(int, bvec.bv_len, to_copy);
		if (copy_len <= 0)
			break;

		TD_MAP_BIO_PAGE(dst, &bvec);

		if (!TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
			/* optimized read fro WC/UC mapping */
			td_memcpy_movntdqa_64(dst, src, copy_len);

		} else if (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF)) {
			/* cached mapping; read buffer was already filled
			 * with the fill word with NT-writes */
			void const * const src_alias[2] = {src, alias};
			td_memcpy_8x8_movq_bad_clflush(dst, src_alias,
					copy_len, TERADIMM_NTF_FILL_WORD);
		} else {
			/* cached mapping; builtin memcpy */
			memcpy(dst, src, copy_len);
		}

		if (src == dev_data_src)
			td_eng_trace(eng, TR_COPYOPS, "dev_to_bio:bufvirt[0]",
					((uint64_t*)dst)[0]);

		TD_UNMAP_BIO_PAGE(dst, &bvec);

		src += copy_len;            /* advance source */
		alias += copy_len;            /* advance source */
		to_copy -= copy_len;        /* fewer bytes to copy */
		accumulated += copy_len;    /* account for copied bytes */
	}

	return accumulated;
}

/* local helper that can copy to one or two device buffers,
 * __always_inline is used so that the compiler can generate the right kind of
 * code depending if dev_data2_dst is defined or not */
static __always_inline int __td_token_bio_to_3_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst,
		void *dev_data2_dst, void *dev_meta2_dst,
		void *dev_data3_dst, void *dev_meta3_dst)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint accumulated = 0;
	char *dst = dev_data_dst;
	char *dst2 = dev_data2_dst;
	char *dst3 = dev_data3_dst;
	struct bio_vec bvec;
	td_bvec_iter i;

	WARN_ON(tok->lba_ofs);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	to_copy = min_t(int, tok->len_host_to_dev,
			td_bio_get_byte_size(tok->host.bio));

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		const char *src;
		int copy_len;
		TD_MAP_BIO_DECLARE;

		copy_len = min_t(int, bvec.bv_len, to_copy);
		if (copy_len <= 0)
			break;

		TD_MAP_BIO_PAGE(src, &bvec);

		if (dst == dev_data_dst)
			td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:bufvirt[0]",
					((uint64_t*)src)[0]);

		if (TE_INJECT(eng, TE_SKIP_WEP_ROW) ) {
			td_eng_info(eng, "EI: Skipping row on tok %u wep %u\n",
					tok->tokid, tok->wr_bufid);

			td_eng_trace(eng, TR_TOKEN, "EI:skip_row:tok", tok->tokid);
			eng->td_errors_count --;
			if (dst2) {
				if (dst3) {
					td_double_memcpy_8x8_movnti_xsum128(dst2, dst3, src, 64, tok->data_xsum);
					dst3 += 64;
				} else
					td_memcpy_8x8_movnti_xsum128(dst2, src, 64, tok->data_xsum);

				dst2 += 64;
			} else
				td_checksum128(src, 64, tok->data_xsum);
			dst += 64;
			src += 64;
			copy_len -= 64;
			to_copy -= 64;
			accumulated += 64;
		}

		if (dst2) {
			if (dst3) {
				/* used by TripleSEC code to write to three WEPs */
				td_triple_memcpy_8x8_movnti_xsum128(dst, dst2, dst3,
						src, copy_len, tok->data_xsum);

				dst3 += copy_len;

			} else {
				/* used by SEC code to write to two WEPs */
				td_double_memcpy_8x8_movnti_xsum128(dst, dst2,
						src, copy_len, tok->data_xsum);
			}

			dst2 += copy_len;

		} else {
			/* boring single buffer writes */
			td_memcpy_8x8_movnti_xsum128(dst,
					src, copy_len, tok->data_xsum);
		}

		if (TE_INJECT(eng, TE_CORRUPT_WEP) ) {
			uint32_t buf_bit, word, bit;
			uint64_t data;

			buf_bit = td_get_cycles();
#if 1
			/* sequential bit corruption */
			{
			static uint32_t next_bit = 0;
			buf_bit = next_bit++;
			}
#endif
			buf_bit %= (copy_len * 8);
			word = buf_bit / 64;
			bit = buf_bit % 64;

			td_eng_info(eng, "EI: Corrupting WEP %u data for tok %u bit %u\n",
					tok->wr_bufid, tok->tokid, buf_bit);
			eng->td_errors_count --;

			/* read from source */
			memcpy(&data, (uint64_t*)src + word, sizeof(uint64_t));

			/* corrupt a bit */
			data ^= 1ULL << bit;

			/* write it to destination corrupted */
			td_memcpy_8B_movnti((uint64_t*)dst + word, &data);
		}

		TD_UNMAP_BIO_PAGE(src, &bvec);

		dst += copy_len;            /* advance destination */
		to_copy -= copy_len;        /* fewer bytes to copy */
		accumulated += copy_len;    /* account for copied bytes */
	}

	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:xsum[0]",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:xsum[1]",
			tok->data_xsum[1]);

	return accumulated;
}

/**
 * \brief copy from tok->host.bio to device buffer
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device data buffer
 * @param dev_meta_dst - kernel virtual address of device meta buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_bio_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	return __td_token_bio_to_3_dev(tok,
			dev_data_dst, dev_meta_dst,
			NULL, NULL,
			NULL, NULL);
}

/**
 * \brief copy from tok->host.bio to two device buffers
 *
 * @param tok - token with outstanding command
 * @param mt - target buffers to copy to
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_bio_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	/* splitting it up allows the compiler to optimize the inlined code
	 * based on the known to be NULL arguments */
	switch (mt->used) {
	default:
		return __td_token_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			mt->buf[2].data, mt->buf[2].meta);
	case 2:
		return __td_token_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			NULL, NULL);
	case 1:
		return __td_token_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			NULL, NULL,
			NULL, NULL);
	}
}

/* ======================== 4k E2E copy to/from bio ======================== */

/**
 * \brief copy from device buffer to tok->host.bio and check 4k E2E
 *
 * @param tok - token with outstanding command
 * @param dev_data_src - kernel virtual address of device buffer
 * @param dev_meta_src 
 * @return number of bytes copied
 *
 * Used by eng->ops->read_page when used for bio reads.
 */
int td_token_e2e_4kB_dev_to_bio(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint accumulated = 0;
	const char *src = dev_data_src;
	struct bio_vec bvec;
	td_bvec_iter i;
	int chk = 0;
	struct td_e2e_4k *e2e = (void*)tok->cmd_metadata;

	WARN_ON(tok->lba_ofs);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_src);
	if(unlikely (!dev_meta_src))
		return -EIO;

	if (unlikely (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF))) {
		if (td_ratelimit())
			td_eng_err(eng, "bs/e2e not compatible with flush method\n");
		return -EIO;
	}

	to_copy = min_t(int, tok->len_dev_to_host,
			td_bio_get_byte_size(tok->host.bio));

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		char *dst;
		int copy_len;
		TD_MAP_BIO_DECLARE;

		copy_len = min_t(int, bvec.bv_len, to_copy);
		if (copy_len <= 0)
			break;

		TD_MAP_BIO_PAGE(dst, &bvec);

		/* read updating the checksum (caching writes) */
		td_memcpy_8x8_movq_xsum128(dst, src, copy_len, tok->data_xsum);

		if (src == dev_data_src)
			td_eng_trace(eng, TR_COPYOPS, "dev_to_bio:e2e_4kB:buf[0]",
					((uint64_t*)dst)[0]);

		TD_UNMAP_BIO_PAGE(dst, &bvec);

		src += copy_len;            /* advance source */
		to_copy -= copy_len;        /* fewer bytes to copy */
		accumulated += copy_len;    /* account for copied bytes */
	}

	td_eng_trace(eng, TR_COPYOPS, "dev_to_bio:e2e_4kB:xsum[0]",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "dev_to_bio:e2e_4kB:xsum[1]",
			tok->data_xsum[1]);

	memcpy(e2e, dev_meta_src, TD_E2E_4k_SIZE);

	/* verify the checksum matches the metadata */
	chk = td_e2e_4k_check(e2e, tok->data_xsum, tok->lba);
	if (likely(!chk))
		return accumulated;

#ifdef CONFIG_TERADIMM_SILENCE_ZERO_E2E_ERRORS
	if (tok->data_xsum[0] || tok->data_xsum[1] || e2e->lba || e2e->fletcher0 || e2e->fletcher1)
#endif
	if (td_ratelimit())
		pr_warn("BAD 4k E2E: %d now [%08llu] %016llx:%016llx "
				"metadata [%08u] %016llx:%016llx\n",
				chk,
				tok->lba,
				tok->data_xsum[0],
				tok->data_xsum[1],
				e2e->lba,
				e2e->fletcher0,
				e2e->fletcher1);
#ifdef CONFIG_TERADIMM_SKIP_E2E
#warning skipping E2E
	return accumulated;
#endif

	return chk;
}

/* local helper that can copy to one or two device buffers,
 * __always_inline is used so that the compiler can generate the right kind of
 * code depending if dev_data2_dst is defined or not */
static __always_inline int __td_token_e2e_4kB_bio_to_3_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst,
		void *dev_data2_dst, void *dev_meta2_dst,
		void *dev_data3_dst, void *dev_meta3_dst)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint accumulated = 0;
	char *dst = dev_data_dst;
	char *dst2 = dev_data2_dst;
	char *dst3 = dev_data3_dst;
	struct bio_vec bvec;
	td_bvec_iter i;
	struct td_e2e_4k *e2e = (void*)tok->cmd_metadata;

	WARN_ON(tok->lba_ofs);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_dst);
	if (!dev_meta_dst)
		return -EIO;

	to_copy = min_t(int, tok->len_host_to_dev,
			td_bio_get_byte_size(tok->host.bio));

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		const char *src;
		int copy_len;
		TD_MAP_BIO_DECLARE;

		copy_len = min_t(int, bvec.bv_len, to_copy);
		if (copy_len <= 0)
			break;

		TD_MAP_BIO_PAGE(src, &bvec);

		if (dst == dev_data_dst)
			td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:e2e_4kB:buffer[0]",
					((uint64_t*)src)[0]);

		/* write updating the checksum (non caching writes) */
		if (dst2) {
			if (dst3) {
				/* used by TripleSEC code to write to three WEPs */
				td_triple_memcpy_8x8_movnti_xsum128(dst, dst2, dst3,
						src, copy_len, tok->data_xsum);

				dst3 += copy_len;

			} else {

				/* used by SEC code to write to two WEPs */
				td_double_memcpy_8x8_movnti_xsum128(dst, dst2,
						src, copy_len, tok->data_xsum);
			}

			dst2 += copy_len;

		} else {
			td_memcpy_8x8_movnti_xsum128(dst,
					src, copy_len, tok->data_xsum);
		}

		TD_UNMAP_BIO_PAGE(src, &bvec);

		dst += copy_len;            /* advance destination */
		to_copy -= copy_len;        /* fewer bytes to copy */
		accumulated += copy_len;    /* account for copied bytes */
	}

	td_e2e_4k_gen(e2e, tok->data_xsum, tok->lba,
			/* TODO: server_id ===> */ 0,
			/* TODO: timestamp ===> */ 0);

	/* write e2e block updating the xsum (non caching writes) */

	if (dev_meta2_dst) {
		if (dev_meta3_dst) {
			/* used by TripleSEC code to write to three WEPs */
			td_triple_memcpy_8x8_movnti_xsum128(dev_meta_dst, dev_meta2_dst,
					dev_meta3_dst, e2e, TD_E2E_4k_SIZE, tok->data_xsum);
		} else {
			/* used by SEC code to write to two WEPs */
			td_double_memcpy_8x8_movnti_xsum128(dev_meta_dst, dev_meta2_dst,
					e2e, TD_E2E_4k_SIZE, tok->data_xsum);
		}
	} else {
		td_memcpy_8x8_movnti_xsum128(dev_meta_dst,
				e2e, TD_E2E_4k_SIZE, tok->data_xsum);
	}
	accumulated += TD_E2E_4k_SIZE;

	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:e2e_4kB:xsum[0]",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:e2e_4kB:xsum[1]",
			tok->data_xsum[1]);

	return accumulated;
}

/**
 * \brief copy from tok->host.bio to device buffer and generate 4k E2E
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device data buffer
 * @param dev_meta_dst - kernel virtual address of device meta buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_e2e_4kB_bio_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	return __td_token_e2e_4kB_bio_to_3_dev(tok,
			dev_data_dst, dev_meta_dst,
			NULL, NULL,
			NULL, NULL);
}

/**
 * \brief copy from tok->host.bio to two device buffers and generate 4k E2E
 *
 * @param tok - token with outstanding command
 * @param mt - target buffers to copy to
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_e2e_4kB_bio_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	/* splitting it up allows the compiler to optimize the inlined code
	 * based on the known to be NULL arguments */
	switch (mt->used) {
	default:
		return __td_token_e2e_4kB_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			mt->buf[2].data, mt->buf[2].meta);
	case 2:
		return __td_token_e2e_4kB_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			NULL, NULL);
	case 1:
		return __td_token_e2e_4kB_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			NULL, NULL,
			NULL, NULL);
	}
}

/* ======================= 520B E2E copy to/from bio ======================= */
/**
 * In 520B mode, we need read/write data in 520 byte sectors to the device
 * but our BIO is in 512 byte sectors.  So we need to skip 8 bytes after every
 * sector.
 *
 * A complication arises then the data written/read is 4k in size, or 8x 512B,
 * with its 64B of E2E metadata.  In this case the resulting 4160 bytes are
 * actually composed of 8x { 512B + 8B } of data and metadata.
 *
 * The complication comes in the 8th data block which is split between the end
 * of the hardware data buffer and hardware metadata buffer.
 */

/**
 * \brief copy from device buffer to tok->host.bio ignoring extra metadata
 *
 * @param tok - token with outstanding command
 * @param dev_data_src - kernel virtual address of device buffer
 * @param dev_meta_src - kernel virtual address of device metadata buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->read_page when used for bio reads.
 *
 * NOTE: this version makes assumptions about the bio structure; it currently
 * expects the data to be:
 *  - multiples of 512B
 *  - limited to 4k
 */
int td_token_520B_dev_to_bio(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint64_t xsum[2];
	struct bio_vec bvec;
	char *dst;

	td_bvec_iter i;
	int scnt = 2;
	int sidx, src_left;
	uint accumulated_data = 0, accumulated_e2e = 0;

	struct {
		const void* buf;
		int len;
		int off;
	} svec[2] = {
		{ dev_data_src, TERADIMM_DATA_BUF_SIZE, 0 },
		{ dev_meta_src, TERADIMM_META_BUF_SIZE, 0 },
	};

	WARN_ON(tok->lba_ofs);

	if (unlikely (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF))) {
		if (td_ratelimit())
			td_eng_err(eng, "bs/e2e not compatible with flush method\n");
		return -EIO;
	}

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	/* no e2e for now.
	   WARN_ON(!bio_meta_dst);
	   if (!bio_meta_dst)
	   return -EIO;
	 */

	xsum[0] = xsum[1] = 0;
	sidx = 0;
	src_left = svec[sidx].len;

	td_eng_trace(eng, TR_COPYOPS, "d2b:520B:sidx", sidx);
	td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src",
			(uint64_t)(svec[sidx].buf+svec[sidx].off));
	td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src_len", src_left);

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		int bv_left = bvec.bv_len;
		TD_MAP_BIO_DECLARE;

		TD_MAP_BIO_PAGE(dst, &bvec);


		td_eng_trace(eng, TR_COPYOPS, "d2b:520B:bv_idx", td_bv_idx(i));

		while (bv_left > 0 && src_left > 0) {
			int clen;

			to_copy = min_t(int, bv_left, src_left);


			td_eng_trace(eng, TR_COPYOPS, "d2b:520B:bv_left", bv_left);
			td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src_left", src_left);
			td_eng_trace(eng, TR_COPYOPS, "d2b:520B:dst", (uint64_t)dst);
			td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src",
					(uint64_t)(svec[sidx].buf+svec[sidx].off));

			if (unlikely(3584 == accumulated_data)) { /* Last 512 chunk. */
				/* Take the last 64 x7 bytes */
				clen = min_t(int, to_copy, 448);
				td_eng_trace(eng, TR_COPYOPS, "d2b:520B:clen", clen);
				td_eng_trace(eng, TR_COPYOPS, "d2b:520B:last_call", bv_left);
				td_memcpy_8x8_movq_xsum128(dst,
						svec[sidx].buf + svec[sidx].off,
						clen, xsum);

				dst += clen;
				svec[sidx].off += clen;
				accumulated_data += clen;
				to_copy -= clen;
				src_left -= clen;
				bv_left -= clen;


				clen = min_t(int, to_copy, 8);
				td_eng_trace(eng, TR_COPYOPS, "d2b:520B:clen", clen);
				if(clen) {
					/* Take the last 8 bytes. */
					td_memcpy_8B_movq_xsum128(dst,
							svec[sidx].buf +
							svec[sidx].off, xsum);
					dst += 8;
					svec[sidx].off += 8; /* end of the line for you */
					src_left -= clen;
					bv_left -= 8;
					to_copy -= clen;
					accumulated_data += clen;
				}


				if (src_left == 0 && sidx < scnt) {
					/* The rest is in the metadata buf */
					sidx++;
					src_left = svec[sidx].len;
					td_eng_trace(eng, TR_COPYOPS, "d2b:520B:sidx", sidx);
					td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src_buf", (uint64_t)svec[sidx].buf);
					td_eng_trace(eng, TR_COPYOPS, "d2b:520B:src",
							(uint64_t)(svec[sidx].buf+svec[sidx].off));
				}
				to_copy = min_t(int, bv_left, src_left);

				clen = to_copy;
				td_eng_trace(eng, TR_COPYOPS, "d2b:520B:clen", clen);
				if (clen) {
					BUG_ON(clen < 56);
					td_memcpy_56B_movq_xsum128(dst,
							svec[sidx].buf +
							svec[sidx].off, xsum);
					/* Keep on counting. */
					src_left -= 56;
					bv_left -= 56;
					svec[sidx].off += 56;
					dst += 56;
					accumulated_data += 56;
				}
				/* final metadata does not need to be skipped. */
			}
			else { /* Regular execution */
				/* But we need to stop every 512 bytes for E2E */
				clen = min_t(int, to_copy,
						512 - (accumulated_data & 511));
				td_eng_trace(eng, TR_COPYOPS, "d2b:520B:clen", clen);

				/* write updating the checksum (caching write) */
				td_memcpy_8x8_movq_xsum128(dst,
						svec[sidx].buf +
						svec[sidx].off, clen, xsum);
				src_left -= clen;
				bv_left -= clen;
				accumulated_data += clen;
				svec[sidx].off += clen;
				dst += clen;
			}


			if ((accumulated_data & 511) == 0) {
				/* reset the per-block checksum */
				xsum[0] = xsum[1] = 0;
				/* Skip e2e. */
				src_left -= 8;
				svec[sidx].off += 8;
			}

		}
		TD_UNMAP_BIO_PAGE(dst, &bvec);
	}

	return accumulated_data + accumulated_e2e;
}

/* ======================= 512B E2E copy to/from bio ======================= */
/**
 * In 512B E2E protection scheme the driver interleaves the 512-byte data
 * blocks with the 8-byte E2E metadata (encoded in td_e2e_512B structures)
 * blocks.
 *
 * A complication arises then the data written/read is 4k in size, or 8x 512B,
 * with its 64B of E2E metadata.  In this case the resulting 4160 bytes are
 * actually composed of 8x { 512B + 8B } of data and metadata.
 *
 * The complication comes in the 8th data block which is split between the end
 * of the hardware data buffer and hardware metadata buffer.
 */

/**
 * \brief copy from device buffer to tok->host.bio and check 512B E2E
 *
 * @param tok - token with outstanding command
 * @param dev_data_src - kernel virtual address of device buffer
 * @param dev_meta_src - kernel virtual address of device metadata buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->read_page when used for bio reads.
 *
 * NOTE: this version makes assumptions about the bio structure; it currently
 * expects the data to be:
 *  - organized in a single bvec buffer
 *  - multiples of 512B
 *  - limited to 4k
 */
int td_token_e2e_512B_dev_to_bio(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint64_t xsum[2];
	struct bio_vec bvec;
	char *dst;

	td_bvec_iter i;
	int scnt = 2;
	int sidx, src_left;
	uint accumulated_data = 0, accumulated_e2e = 0;

	struct {
		const void* buf;
		int len;
		int off;
	} svec[2] = {
		{ dev_data_src, TERADIMM_DATA_BUF_SIZE, 0 },
		{ dev_meta_src, TERADIMM_META_BUF_SIZE, 0 },
	};

	/* e2e stuff */
	uint64_t lba = tok->lba;
	struct td_e2e_512B e2e_512B;
	int chk = 0;

	WARN_ON(tok->lba_ofs);

	if (unlikely (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF))) {
		if (td_ratelimit())
			td_eng_err(eng, "bs/e2e not compatible with flush method\n");
		return -EIO;
	}

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_src);
	if (!dev_meta_src)
		return -EIO;

	xsum[0] = xsum[1] = 0;
	sidx = 0;
	src_left = svec[sidx].len;

	td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:sidx", sidx);
	td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src",
			(uint64_t)(svec[sidx].buf+svec[sidx].off));
	td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src_len", src_left);

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		int bv_left = bvec.bv_len;
		TD_MAP_BIO_DECLARE;

		TD_MAP_BIO_PAGE(dst, &bvec);


		td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:bv_idx",
				td_bv_idx(i));

		while (bv_left > 0 && src_left > 0) {
			int clen;

			to_copy = min_t(int, bv_left, src_left);


			td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:bv_left", bv_left);
			td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src_left", src_left);
			td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:dst", (uint64_t)dst);
			td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src",
					(uint64_t)(svec[sidx].buf+svec[sidx].off));

			if (unlikely(3584 == accumulated_data)) { /* Last 512 chunk. */
				/* Take the last 64 x7 bytes */
				clen = min_t(int, to_copy, 448);
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:clen", clen);
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:last_call", bv_left);
				td_memcpy_8x8_movq_xsum128(dst,
						svec[sidx].buf + svec[sidx].off,
						clen, xsum);

				dst += clen;
				svec[sidx].off += clen;
				accumulated_data += clen;
				to_copy -= clen;
				src_left -= clen;
				bv_left -= clen;


				clen = min_t(int, to_copy, 8);
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:clen", clen);
				if(clen) {
					/* Take the last 8 bytes. */
					td_memcpy_8B_movq_xsum128(dst,
							svec[sidx].buf +
							svec[sidx].off, xsum);
					dst += 8;
					svec[sidx].off += 8; /* end of the line for you */
					src_left -= clen;
					bv_left -= 8;
					to_copy -= clen;
					accumulated_data += clen;
				}


				if (src_left == 0 && sidx < scnt) {
					/* The rest is in the metadata buf */
					sidx++;
					src_left = svec[sidx].len;
					td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:sidx", sidx);
					td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src_buf", (uint64_t)svec[sidx].buf);
					td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:src",
							(uint64_t)(svec[sidx].buf+svec[sidx].off));
				}
				to_copy = min_t(int, bv_left, src_left);

				clen = to_copy;
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:clen", clen);
				if (clen) {
					BUG_ON(clen < 56);
					td_memcpy_56B_movq_xsum128(dst,
							svec[sidx].buf +
							svec[sidx].off, xsum);
					/* Keep on counting. */
					src_left -= 56;
					bv_left -= 56;
					svec[sidx].off += 56;
					dst += 56;
					accumulated_data += 56;
				}
				/* final metadata does not need to be skipped. */
			}
			else { /* Regular execution */
				/* But we need to stop every 512 bytes for E2E */
				clen = min_t(int, to_copy,
						512 - (accumulated_data & 511));
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:clen", clen);

				/* write updating the checksum (caching write) */
				td_memcpy_8x8_movq_xsum128(dst,
						svec[sidx].buf +
						svec[sidx].off, clen, xsum);
				src_left -= clen;
				bv_left -= clen;
				accumulated_data += clen;
				svec[sidx].off += clen;
				dst += clen;
			}


			if ((accumulated_data & 511) == 0) {
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:e2e", TD_E2E_512_SIZE);

				memcpy(&e2e_512B, svec[sidx].buf +
						svec[sidx].off,
						TD_E2E_512_SIZE);
				chk = td_e2e_512B_check(&e2e_512B, xsum, lba++);
				if (unlikely(chk)) {
#ifdef CONFIG_TERADIMM_SKIP_E2E
#warning skipping E2E_512B
#ifdef CONFIG_TERADIMM_SILENCE_ZERO_E2E_ERRORS
					if (xsum[0] || xsum[1] || e2e_512B.lba || e2e_512B.folded_fletcher)
#endif
					if (td_ratelimit())
						pr_warn("BAD 512 E2E: %d now [%08llu/%08llu] %016llx:%016llx (%08x) "
							"metadata [%08u] %08x\n",
							chk,
							tok->lba,                  /* start LBA */
							lba,                       /* failing LBA */
							xsum[0], xsum[1],          /* computed checksum */
							td_e2e_512B_fold_fletcher(xsum[0], xsum[1]),
							e2e_512B.lba,              /* e2e lba */
							e2e_512B.folded_fletcher); /* e2e checksum */
#else
					goto bail;
#endif
				}

				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:xsum[0]", tok->data_xsum[0]);
				td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:xsum[1]", tok->data_xsum[1]);
				/* reset the per-block checksum */
				xsum[0] = xsum[1] = 0;
				/* e2e. */
				src_left -= TD_E2E_512_SIZE;
				svec[sidx].off += TD_E2E_512_SIZE;
				accumulated_e2e += TD_E2E_512_SIZE;
			}

		}
		TD_UNMAP_BIO_PAGE(dst, &bvec);
	}


	return accumulated_data + accumulated_e2e;

#ifndef CONFIG_TERADIMM_SKIP_E2E
bail:
#endif
	TD_UNMAP_BIO_PAGE(dst, &bvec);

	td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:xsum[0]", tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "d2b:e2e_512B:xsum[1]", tok->data_xsum[1]);

#ifdef CONFIG_TERADIMM_SILENCE_ZERO_E2E_ERRORS
	if (xsum[0] || xsum[1] || e2e_512B.lba || e2e_512B.folded_fletcher)
#endif
	if (td_ratelimit())
		pr_warn("BAD 512 E2E: %d now [%08llu/%08llu] %016llx:%016llx (%08x) "
				"metadata [%08u] %08x\n",
				chk,
				tok->lba,                  /* start LBA */
				lba,                       /* failing LBA */
				xsum[0], xsum[1],          /* computed checksum */
				td_e2e_512B_fold_fletcher(xsum[0], xsum[1]),
				e2e_512B.lba,              /* e2e lba */
				e2e_512B.folded_fletcher); /* e2e checksum */

	return chk;
}

/**
 * \brief copy from tok->host.bio to device buffer and generate 512B E2E
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device buffer
 * @param dev_meta_dst - kernel virtual address of device metadata buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 *
 */

/* local helper that can copy to one or two device buffers,
 * __always_inline is used so that the compiler can generate the right kind of
 * code depending if dev_data2_dst is defined or not */
static __always_inline int __td_token_e2e_512B_bio_to_3_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst,
		void *dev_data2_dst, void *dev_meta2_dst,
		void *dev_data3_dst, void *dev_meta3_dst)
{
	int te_errors_skip = 0;

	struct td_engine *eng = td_token_engine(tok);
	char *dst, *dst2;
	struct bio_vec bvec;
	const char *src;
	uint64_t xsum[2];
	uint64_t lba = tok->lba;
	struct td_e2e_512B e2e_512B;
	int to_copy;
	td_bvec_iter i;
	struct {
		void* buf;
		int len;
	} dst_vec[2] = {
		{ dev_data_dst, TERADIMM_DATA_BUF_SIZE},
		{ dev_meta_dst, TERADIMM_META_BUF_SIZE},
	},
	 dst2_vec[2] = {
		{ dev_data2_dst, TERADIMM_DATA_BUF_SIZE},
		{ dev_meta2_dst, TERADIMM_META_BUF_SIZE},
	};

	int dst_cnt = 2;
	int dst_idx, dst_left;
	uint accumulated_data = 0, accumulated_e2e = 0;

	WARN_ON(tok->lba_ofs);

	if (dev_data3_dst || dev_meta3_dst)
		if (td_ratelimit())
			td_eng_err(eng, "TripleSEC not implemented for 512B\n");

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_dst);
	if (!dev_meta_dst)
		return -EIO;

	xsum[0] = xsum[1] = 0;

	dst_idx = 0;
	dst_left = dst_vec[dst_idx].len;
	dst = dst_vec[dst_idx].buf;
	if (dev_data2_dst)
		dst2  = dst2_vec[dst_idx].buf;
	else
		dst2 = NULL;

	if (TE_INJECT(eng, TE_SKIP_WEP_ROW)) {
		td_eng_info(eng, "EI: Skipping WEP row in tok %u\n", tok->tokid);
		td_eng_trace(eng, TR_TOKEN, "EI:skip_row:tok", tok->tokid);
		eng->td_errors_count --;
		te_errors_skip = 8;

	}
	td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_idx", dst_idx);
	td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_buf", (uint64_t)dst);
	td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_len", dst_left);
	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		int bv_left = bvec.bv_len;
		TD_MAP_BIO_DECLARE;

		TD_MAP_BIO_PAGE(src, &bvec);

		td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:bv_idx",
				td_bv_idx(i));

		while (bv_left > 0) {
			int copy_pos, copy_len;
			
			to_copy = min_t(int, bv_left, dst_left);

			/* But we need to stop every 512 bytes for E2E */
			copy_len = min_t(int, to_copy, 512 - (accumulated_data & 511));

			td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:bv_left", bv_left);
			td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_left", dst_left);
			td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:copy_len", copy_len);
			td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst", (uint64_t)dst);
			td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:src", (uint64_t)src);


			/* write updating the checksum (non caching writes) */
			for (copy_pos = 0; copy_pos < copy_len; copy_pos += 8) {
				if (te_errors_skip) {
					char nowhere[32];
					td_memcpy_8B_movnti_xsum128(nowhere, src+copy_pos,
							xsum);
					td_eng_info(eng, "EI: copy_pos = %u\n", copy_pos);
					te_errors_skip--;
				} else
				td_memcpy_8B_movnti_xsum128(dst+copy_pos, src+copy_pos,
						xsum);
				if (dst2)
					td_memcpy_8B_movnti(dst2+copy_pos, src+copy_pos);
			}

			dst_left -= copy_len;
			bv_left -= copy_len;
			accumulated_data += copy_len;
			dst += copy_len;
			if (dst2)
				dst2 += copy_len;
			src += copy_len;
		
			if (dst_left == 0 && dst_idx < dst_cnt) {
				dst_idx++;
				dst_left = dst_vec[dst_idx].len;
				dst = dst_vec[dst_idx].buf;
				if (dst2)
					dst2 = dst2_vec[dst_idx].buf;
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_idx", dst_idx);
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_buf", (uint64_t)dst);
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:dst_len", dst_left);
			}
			
			if ((accumulated_data & 511) == 0) {
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:sector",
						accumulated_data/512);

				WARN_ON(dst_left < 8);

				/* generate the next E2E metadata block */
				td_e2e_512B_gen(&e2e_512B, xsum, lba++);

				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:xsum[0]",
						xsum[0]);
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:xsum[1]",
						xsum[1]);

				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:e2e",
						*(uint64_t*)&e2e_512B);

				/* update the global fletcher xsum */
				td_global_fletcher_update(tok->data_xsum, xsum, 512);

				/* write E2E block updating the checksum (non caching writes) */
				td_memcpy_8B_movnti_xsum128(dst, &e2e_512B, tok->data_xsum);
				if (dst2)
					td_memcpy_8B_movnti(dst2, &e2e_512B);

				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:gxsum[0]",
						tok->data_xsum[0]);
				td_eng_trace(eng, TR_COPYOPS, "b2d:e2e_512B:gxsum[1]",
						tok->data_xsum[1]);

				/* reset the per-block checksum */
				xsum[0] = xsum[1] = 0;

				/* And advance our dst */
				dst += 8;
				if (dst2)
					dst2 += 8;
				dst_left -= 8;
				accumulated_e2e += 8;
			}
	
		}
		TD_UNMAP_BIO_PAGE(src, &bvec);
	}

	return accumulated_data + accumulated_e2e;
}

/**
 * \brief copy from tok->host.bio to device buffer and generate 512 E2E
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device data buffer
 * @param dev_meta_dst - kernel virtual address of device meta buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_e2e_512B_bio_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	return __td_token_e2e_512B_bio_to_3_dev(tok,
			dev_data_dst, dev_meta_dst,
			NULL, NULL,
			NULL, NULL);
}

/**
 * \brief copy from tok->host.bio to two device buffers and generate 512 E2E
 *
 * @param tok - token with outstanding command
 * @param mt - target buffers to copy to
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes.
 */
int td_token_e2e_512B_bio_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	/* splitting it up allows the compiler to optimize the inlined code
	 * based on the known to be NULL arguments */
	switch (mt->used) {
	default:
		return __td_token_e2e_512B_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			mt->buf[2].data, mt->buf[2].meta);
	case 2:
		return __td_token_e2e_512B_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			NULL, NULL);
	case 1:
		return __td_token_e2e_512B_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			NULL, NULL,
			NULL, NULL);
	}
}



/* ======================= 512B copy from kernel bio ======================= */
/*
 * This is a copy and modification of the 512B E2E copy from regular bio.
 */

/**
 * \brief copy from kernel allocated bio tok->host.bio to device buffer
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device buffer
 * @param dev_meta_dst - kernel virtual address of device metadata buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for bio writes allocated int he
 * kernel.
 *
 * NOTE: this version makes assumptions about the bio structure; it currently
 * expects the data to be:
 *  - organized in a single bvec buffer
 *  - limited to 4k
 */

int td_token_512B_kbio_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	struct td_engine *eng = td_token_engine(tok);
	char *dst = dev_data_dst;
	struct bio_vec bvec;
	void *src;
	uint64_t xsum[2];
	int copy_len, copy_pos;
	td_bvec_iter i;
	struct {
		void* buf;
		int len;
	} dst_vec[2] = {
		{ dev_data_dst, TERADIMM_DATA_BUF_SIZE},
		{ dev_meta_dst, TERADIMM_META_BUF_SIZE},
	};
	int dst_idx, dst_left;
	uint accumulated_data = 0;

	WARN_ON(tok->lba_ofs);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_dst);
	if (!dev_meta_dst)
		return -EIO;

	xsum[0] = xsum[1] = 0;

	dst_idx = 0;
	dst_left = dst_vec[dst_idx].len;
	dst = dst_vec[dst_idx].buf;

	td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:dst_idx", dst_idx);
	td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:dst_buf", (uint64_t)dst);
	td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:dst_len", dst_left);
	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		src = bvec.bv_page;

		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:bv_idx",
				td_bv_idx(i));

		copy_len = min_t(int, bvec.bv_len, dst_left);

		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:dst_left", dst_left);
		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:copy_len", copy_len);
		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:dst", (uint64_t)dst);
		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:src", (uint64_t)src);

		/* write updating the checksum (non caching writes) */
		for (copy_pos = 0; copy_pos < copy_len; copy_pos += 8) {
			td_memcpy_8B_movnti_xsum128(dst+copy_pos, src+copy_pos, xsum);
		}
		dst_left -= copy_len;
		accumulated_data += copy_len;

		/* The rest of this function exists to create the correct
		 * gxsum. */

		/* update the global fletcher xsum */
		td_global_fletcher_update(tok->data_xsum, xsum, 512);

		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:gxsum[0]",
				tok->data_xsum[0]);
		td_eng_trace(eng, TR_COPYOPS, "b2d:kbio_512B:gxsum[1]",
				tok->data_xsum[1]);

		/* reset the per-block checksum */
		xsum[0] = xsum[1] = 0;

	}

	return accumulated_data;
}

/* ======================== 4k + 0-meta copy to/from bio ==================== */

/* this is a HACK to support 4k + metadata full of zeros */
static __always_inline int __td_token_4kB_zero_meta_bio_to_3_dev(
		struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst,
		void *dev_data2_dst, void *dev_meta2_dst,
		void *dev_data3_dst, void *dev_meta3_dst)
{
	struct td_engine *eng = td_token_engine(tok);
	int to_copy;
	uint accumulated = 0;
	char *dst = dev_data_dst;
	char *dst2 = dev_data2_dst;
	char *dst3 = dev_data3_dst;
	struct bio_vec bvec;
	td_bvec_iter i;

	WARN_ON(tok->lba_ofs);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	WARN_ON(!dev_meta_dst);
	if (!dev_meta_dst)
		return -EIO;

	to_copy = min_t(int, tok->len_host_to_dev,
			td_bio_get_byte_size(tok->host.bio));

	td_bio_for_each_segment(bvec, tok->host.bio, i) {
		const char *src;
		int copy_len;
		TD_MAP_BIO_DECLARE;

		copy_len = min_t(int, bvec.bv_len, to_copy);
		if (copy_len <= 0)
			break;

		TD_MAP_BIO_PAGE(src, &bvec);

		if (dst == dev_data_dst)
			td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:4kB_zero:buf[0]",
					((uint64_t*)src)[0]);

		/* write updating the checksum (non caching writes) */
		if (dst2) {
			if (dst3) {
				/* used by TripleSEC code to write to three WEPs */
				if (TE_INJECT(eng, TE_SKIP_WEP_ROW) ) {
					td_eng_trace(eng, TR_TOKEN, "EI:skip_row:buddy", tok->tokid);
					eng->td_errors_count --;
					td_checksum128(src, copy_len, tok->data_xsum);
				} else
					td_triple_memcpy_8x8_movnti_xsum128(dst, dst2, dst3,
							src, copy_len, tok->data_xsum);

				dst3 += copy_len;
			} else {

				/* used by SEC code to write to two WEPs */
				if (TE_INJECT(eng, TE_SKIP_WEP_ROW) ) {
					td_eng_trace(eng, TR_TOKEN, "EI:skip_row:buddy", tok->tokid);
					eng->td_errors_count --;
					td_checksum128(src, copy_len, tok->data_xsum);
				} else
					td_double_memcpy_8x8_movnti_xsum128(dst, dst2,
						src, copy_len, tok->data_xsum);
			}

			dst2 += copy_len;
		} else {
			if (TE_INJECT(eng, TE_SKIP_WEP_ROW) ) {
				td_eng_trace(eng, TR_TOKEN, "EI:skip_row:tok", tok->tokid);
				eng->td_errors_count --;
				td_checksum128(src, copy_len, tok->data_xsum);
			} else
				td_memcpy_8x8_movnti_xsum128(dst,
					src, copy_len, tok->data_xsum);
		}


		TD_UNMAP_BIO_PAGE(src, &bvec);

		dst += copy_len;            /* advance destination */
		to_copy -= copy_len;        /* fewer bytes to copy */
		accumulated += copy_len;    /* account for copied bytes */
	}

	if (accumulated < TERADIMM_DATA_BUF_SIZE) {
		/* We need to copy the rest of the page as zeros */
		int needed = TERADIMM_DATA_BUF_SIZE - accumulated;

		td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:4kB_zero:needed", needed);

		td_checksum128_advance_zeroed(tok->data_xsum, needed);
		td_zero_movnti(dst, needed);
		if (dst2)
			td_zero_movnti(dst2, needed);

		accumulated += needed;
	}

	WARN_ON(sizeof(tok->cmd_metadata) < TERADIMM_META_BUF_SIZE);

	/* write e2e block updating the xsum (non caching writes) */
	if (dev_meta2_dst)
		td_zero_8x8_movnti(dev_meta2_dst, TERADIMM_META_BUF_SIZE);
	td_zero_8x8_movnti(dev_meta_dst, TERADIMM_META_BUF_SIZE);
	td_checksum128_advance_zeroed(tok->data_xsum, TERADIMM_META_BUF_SIZE);
	accumulated += TERADIMM_META_BUF_SIZE;

	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:4kB_zero:xsum[0]",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "bio_to_dev:4kB_zero:xsum[1]",
			tok->data_xsum[1]);

	return accumulated;
}

int td_token_4kB_zero_meta_bio_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	return __td_token_4kB_zero_meta_bio_to_3_dev(tok,
			dev_data_dst, dev_meta_dst,
			NULL, NULL,
			NULL, NULL);
}

int td_token_4kB_zero_meta_bio_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	/* splitting it up allows the compiler to optimize the inlined code
	 * based on the known to be NULL arguments */
	switch (mt->used) {
	default:
		return __td_token_4kB_zero_meta_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			mt->buf[2].data, mt->buf[2].meta);
	case 2:
		return __td_token_4kB_zero_meta_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			mt->buf[1].data, mt->buf[1].meta,
			NULL, NULL);
	case 1:
		return __td_token_4kB_zero_meta_bio_to_3_dev(tok,
			mt->buf[0].data, mt->buf[0].meta,
			NULL, NULL,
			NULL, NULL);
	}
}


