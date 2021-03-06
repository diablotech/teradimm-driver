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

#include "td_token.h"
#include "td_util.h"
#include "td_engine.h"
#include "td_bio.h"
#include "td_checksum.h"
#include "td_memcpy.h"
#include "td_protocol.h"
#include "td_cache.h"

/* ==================== normal to/copy from virt pointer ==================== */

/**
 * \brief copy from device buffer to tok->host_buf_virt
 *
 * @param tok - token with outstanding command
 * @param dev_data_src - kernel virtual address of device buffer
 * @param dev_meta_src -
 * @return number of bytes copied
 *
 * Used by eng->ops->read_page when used for ucmd and kernel reads.
 */
static int td_token_dev_to_virt(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	struct td_engine *eng = td_token_engine(tok);
	char *dst = (char*)tok->host_buf_virt;
	const char *src = (char*)dev_data_src;
	const char *alias = (char*)dev_data_alias;
	uint data_len = tok->len_dev_to_host;
	int rc;

#if 0
	if (TD_MAPPER_TYPE_TEST(READ_DATA, WC)) {
		td_memcpy_movntdqa_64(dst, src, data_len);

	} else if (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF)) {
		/* read buffer was already filled with the fill word with NT-writes */
		void const *const src_alias[2] = {dev_data_src, dev_data_alias};
		td_memcpy_8x8_movq_bad_clflush(dst, src_alias,
				data_len, TERADIMM_NTF_FILL_WORD);
	} else {
		memcpy(dst, src, data_len);
	}
#else
	int use_read_aliases;
	char *cache;

	use_read_aliases = td_eng_conf_var_get(eng, USE_READ_ALIASES);
	cache = eng->td_read_data_cache + (PAGE_SIZE * tok->rd_bufid);
	if (use_read_aliases >= 2 && !eng->td_read_data_cache)
		use_read_aliases = 1;


		if (!TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
			/* optimized read fro WC/UC mapping */
			switch (use_read_aliases) {
			default:
				/* copy from source to dest */
				td_memcpy_movntdqa_64(dst, src, data_len);
				break;
			case 1:
				/* read source and alias, if equal write to
				 * dest, otherwise fail */
				rc = td_memcpy_movntdqa_64_alias_compare(
						dst, src, alias, data_len);
				if (rc<0)
					goto bail;
				break;
			case 2:
				/* read from source and cache, if different
				 * read from alias, write to destination */
				td_memcpy_movntdqa_64_cached_alias_compare(
						dst, src, cache, alias, data_len);
				break;
			case 3:
				/* read from source and cache, if different
				 * read from alias, if alias differs from
				 * source fail, otherwise write to dest */
				rc = td_memcpy_movntdqa_64_cached_alias_compare_test(
						dst, src, cache, alias, data_len);
				td_eng_trace(eng, TR_TOKEN, "DI:dev2bio:compare", rc);
				switch(rc) {
				case 0:
					break;
				case 1:
					td_eng_counter_token_inc(eng, RDBUF_ALIAS_DUPLICATE);
					break;
				case -1:
					td_eng_counter_token_inc(eng, RDBUF_ALIAS_CORRECTION);
					break;
				}
				break;
			}
		} else {
			/* cached read fom WB mapping */
			switch (use_read_aliases) {
			default:
				if (td_cache_flush_exact_test(eng,PRE,NTF,RDBUF)) {
					/* cached mapping; read buffer was already filled
					* with the fill word with NT-writes */
					void const * const src_alias[2] = {src, alias};
					if (0) td_eng_info(eng, "D2B: WB[%d] %s\n", use_read_aliases, "td_memcpy_8x8_movq_bad_clflush");
					td_memcpy_8x8_movq_bad_clflush(dst, src_alias,
							data_len, TERADIMM_NTF_FILL_WORD);
				} else {
					/* cached mapping; builtin memcpy */
					memcpy(dst, src, data_len);
				}
				break;
			case 1:
				/* read source and alias, if equal write to
				 * dest, otherwise fail */
				if (0) td_eng_info(eng, "D2B: WB[%d] %s\n", use_read_aliases, "td_memcpy_alias_compare");
				rc = td_memcpy_alias_compare(
						dst, src, alias, data_len);
				if (rc<0)
					goto bail;
				break;
			case 2:
				/* read from source and cache, if different
				 * read from alias, write to destination */
				if (0) td_eng_info(eng, "D2B: WB[%d] %s\n", use_read_aliases, "td_memcpy_cached_alias_compare");
				td_memcpy_cached_alias_compare(
						dst, src, cache, alias, data_len);
				break;
			case 3:
				/* Need a block for our src_alias */
				do {
					/* read from source and cache, if different
					 * read from alias, if alias differs from
					 * source, track */
					void const * const src_alias[2] = {src, alias};

					if (0) td_eng_info(eng, "D2B: WB[%d] %s\n", use_read_aliases, "td_memcpy_cached_alias_compare_test");
					td_eng_trace(eng, TR_DI, "DI:compare_test:tok", tok->tokid);
					rc = td_memcpy_cached_alias_compare_test(dst, src_alias, cache, data_len);
					if (0) td_eng_info(eng, "  - rc %d [%016llx]\n", rc, *(uint64_t*)cache);
					switch(rc) {
					case 0:
						break;
					case 1:
						td_eng_counter_token_inc(eng, RDBUF_ALIAS_DUPLICATE);
						break;
					case -1:
						td_eng_counter_token_inc(eng, RDBUF_ALIAS_CORRECTION);
						break;
					}
				} while (0);
				break;
			}
		}
#endif

	td_eng_trace(eng, TR_TOKEN, "dev_to_virt:bufvirt[0] ",
			((uint64_t*)dst)[0]);

	rc = data_len;
bail:
	return rc;
}

/**
 * \brief copy from tok->host_buf_virt to device buffer
 *
 * @param tok - token with outstanding command
 * @param dev_data_dst - kernel virtual address of device data buffer
 * @param dev_meta_dst - kernel virtual address of device meta buffer
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for ucmd and kernel writes.
 */
static int td_token_virt_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	struct td_engine *eng = td_token_engine(tok);
	char *dst = (char*)dev_data_dst;
	char *src = (char*)tok->host_buf_virt;
	uint data_len = tok->len_host_to_dev;

	if (tok->cache.data) {
		/* If we have a cache, use it */
		src = tok->cache.data;
		td_eng_trace(eng, TR_TOKEN, "virt_to_dev:cache", (uint64_t)src);
	}

	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:bufvirt[0] ",
			((uint64_t*)src)[0]);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;


	td_memcpy_8x8_movnti_xsum128(dst, src,
			data_len, tok->data_xsum);

	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:xsum[0]    ",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:xsum[1]    ",
			tok->data_xsum[1]);

	return data_len;
}

/**
 * \brief copy from tok->host_buf_virt to device buffer
 *
 * @param tok - token with outstanding command
 * @param mt - target buffers to copy to
 * @return number of bytes copied
 *
 * Used by eng->ops->write_page when used for ucmd and kernel writes.
 */
static int td_token_virt_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	struct td_engine *eng = td_token_engine(tok);
	char *src = (char*)tok->host_buf_virt;
	uint data_len = tok->len_host_to_dev;

	if (tok->cache.data) {
		/* If we have a cache, use it */
		src = tok->cache.data;
		td_eng_trace(eng, TR_TOKEN, "virt_to_dev:cache", (uint64_t)src);
	}

	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:bufvirt[0] ",
			((uint64_t*)src)[0]);

	tok->data_xsum[0] = tok->data_xsum[1] = 0;

	switch (mt->used) {
	default:
		td_triple_memcpy_8x8_movnti_xsum128(
			mt->buf[0].data,
			mt->buf[1].data,
			mt->buf[2].data,
			src, data_len, tok->data_xsum);
		break;
	case 2:
		td_double_memcpy_8x8_movnti_xsum128(
			mt->buf[0].data,
			mt->buf[1].data,
			src, data_len, tok->data_xsum);
		break;
	case 1:
		td_memcpy_8x8_movnti_xsum128(
			mt->buf[0].data,
			src, data_len, tok->data_xsum);
		break;
	}

	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:xsum[0]    ",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_TOKEN, "virt_to_dev:xsum[1]    ",
			tok->data_xsum[1]);

	return data_len;
}

/* ==================== null ops ============================================ */

static int td_token_dev_to_null(struct td_token *tok,
		const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias)
{
	WARN_ON(1);
	return -EINVAL;
}

static int td_token_null_to_dev(struct td_token *tok,
		void *dev_data_dst, void *dev_meta_dst)
{
	WARN_ON(1);
	return -EINVAL;
}

static int td_token_null_to_multi_dev(struct td_token *tok,
		struct td_multi_target *mt)
{
	WARN_ON(1);
	return -EINVAL;
}


/*
 * The following are the OS specific BIO functions, provided in the OS
 * specific td_token_bio.c
 */
int td_token_dev_to_bio(struct td_token *tok, const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void *dev_meta_alias);
int td_token_bio_to_dev(struct td_token *tok, void *dev_data_dst, void *dev_meta_dst);
int td_token_bio_to_multi_dev(struct td_token *tok, struct td_multi_target *mt);

int td_token_e2e_4kB_dev_to_bio(struct td_token *tok, const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void* dev_meta_alias);
int td_token_e2e_4kB_bio_to_dev(struct td_token *tok, void *dev_data_dst, void *dev_meta_dst);
int td_token_e2e_4kB_bio_to_multi_dev(struct td_token *tok, struct td_multi_target *mt);

int td_token_e2e_512B_dev_to_bio(struct td_token *tok, const void *dev_data_src, const void *dev_meta_src,
		const void *dev_data_alias, const void* dev_meta_alias);
int td_token_e2e_512B_bio_to_dev(struct td_token *tok, void *dev_data_dst, void *dev_meta_dst);
int td_token_e2e_512B_bio_to_multi_dev(struct td_token *tok, struct td_multi_target *mt);

int td_token_520B_dev_to_bio(struct td_token *tok, const void *dev_data_dst, const void *dev_meta_dst,
		const void *dev_data_alias, const void* dev_meta_alias);

int td_token_512B_kbio_to_dev(struct td_token *tok, void *dev_data_dst, void *dev_meta_dst);

/* ==================== entry points ======================================== */

struct td_token_copy_ops td_token_copy_ops_null = {
	/* dev_to_host */ td_token_dev_to_null,
	/* host_to_dev */ td_token_null_to_dev,
	/* host_to_multi_dev */ td_token_null_to_multi_dev
};

struct td_token_copy_ops td_token_copy_ops_bio = {
	/* dev_to_host */ td_token_dev_to_bio,
	/* host_to_dev */ td_token_bio_to_dev,
	/* host_to_multi_dev */ td_token_bio_to_multi_dev
};

struct td_token_copy_ops td_token_copy_ops_virt = {
	/* dev_to_host */ td_token_dev_to_virt,
	/* host_to_dev */ td_token_virt_to_dev,
	/* host_to_multi_dev */ td_token_virt_to_multi_dev
};

struct td_token_copy_ops td_token_copy_ops_bio_e2e_4kB = {
	/* dev_to_host */ td_token_e2e_4kB_dev_to_bio,
	/* host_to_dev */ td_token_e2e_4kB_bio_to_dev,
	/* host_to_multi_dev */ td_token_e2e_4kB_bio_to_multi_dev
};

struct td_token_copy_ops td_token_copy_ops_bio_e2e_512B = {
	/* dev_to_host */ td_token_e2e_512B_dev_to_bio,
	/* host_to_dev */ td_token_e2e_512B_bio_to_dev,
	/* host_to_multi_dev */ td_token_e2e_512B_bio_to_multi_dev
};

struct td_token_copy_ops td_token_copy_ops_bio_520B = {
	/* dev_to_host */ td_token_520B_dev_to_bio,
	/* host_to_dev */ td_token_e2e_512B_bio_to_dev,
	/* host_to_multi_dev */ td_token_e2e_512B_bio_to_multi_dev
};

/* For kernel allocated bio's (eg: discard) */
struct td_token_copy_ops td_token_copy_ops_kbio_512B = {
	/* dev_to_host */ td_token_dev_to_null,
	/* host_to_dev */ td_token_512B_kbio_to_dev,
	/* host_to_multi_dev */ td_token_null_to_multi_dev
};

/* this one is a HACK */
int td_token_4kB_zero_meta_bio_to_dev(struct td_token *tok, void *dev_data_dst, void *dev_meta_dst);
int td_token_4kB_zero_meta_bio_to_multi_dev(struct td_token *tok, struct td_multi_target *mt);
struct td_token_copy_ops td_token_copy_ops_bio_4kB_zero_meta = {
	/* dev_to_host */ td_token_dev_to_bio,
	/* host_to_dev */ td_token_4kB_zero_meta_bio_to_dev,
	/* host_to_multi_dev */ td_token_4kB_zero_meta_bio_to_multi_dev
};

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
static inline char * td_tok_hist_event_str(char *buf, int buf_len,
		enum td_tok_event event)
{
	switch (event) {
	case TD_TOK_EVENT_START:  return "START";
	case TD_TOK_EVENT_STATUS: return "STATUS";
	case TD_TOK_EVENT_STOLEN: return "STOLEN";
	case TD_TOK_EVENT_RDMETA: return "RDMETA";
	case TD_TOK_EVENT_END:    return "END";
	default:
		buf[0] = 0;
		snprintf(buf, buf_len, "0x%04x", event);
		return buf;
	}
};
#endif

void td_dump_tok_events(struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	int n;
	char buf[32];

	for (n = 0; n < CONFIG_TERADIMM_TOKEN_HISTORY; n ++) {
		struct td_tok_hist *hist;
		int idx = (n + tok->tok_hist_next)
			% CONFIG_TERADIMM_TOKEN_HISTORY;
		uint64_t nsec;

		hist = tok->tok_hist + idx;

		if (!hist->ts)
			continue;

		nsec = td_cycles_to_nsec(hist->ts);

		td_eng_err(tok->td_engine,
			"%llu.%03u  tok[%u]  #%04x  c=%02x  s=%02x  %s/%u\n",
			nsec/1000LLU, (unsigned)(nsec%1000),
			tok->tokid,
			hist->cmd_seq,
			hist->cmd_id,
			hist->cmd_status,
			td_tok_hist_event_str(buf, sizeof(buf), hist->hist_event),
			hist->hist_data);
	}
#endif
}
