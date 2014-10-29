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
	uint data_len = tok->len_dev_to_host;

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

	td_eng_trace(eng, TR_TOKEN, "dev_to_virt:bufvirt[0] ",
			((uint64_t*)dst)[0]);

	return data_len;
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
