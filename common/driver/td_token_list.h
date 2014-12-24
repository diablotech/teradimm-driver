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

#ifndef _TD_TOKEN_LIST_H_
#define _TD_TOKEN_LIST_H_

#include "td_kdefn.h"


#include "td_compat.h"
#include "td_defs.h"
#include "td_token.h"

struct td_token_list {
	struct list_head        list;
#ifdef CONFIG_TD_TOKEN_LIST_LOCK
	spinlock_t              lock;
#endif
	unsigned                count;
};

static inline void td_token_list_init(struct td_token_list *tl)
{
	INIT_LIST_HEAD(&tl->list);
	tl->count = 0;

#ifdef CONFIG_TD_TOKEN_LIST_LOCK
	spin_lock_init(&tl->lock);
#endif
}


/* token locking functions */

static inline void td_tokens_lock(struct td_token_list *tl)
{
#ifdef CONFIG_TD_TOKEN_LIST_LOCK
	spin_lock_bh(&tl->lock);
#endif
}
static inline void td_tokens_unlock(struct td_token_list *tl)
{
#ifdef CONFIG_TD_TOKEN_LIST_LOCK
	spin_unlock_bh(&tl->lock);
#endif
}

/** dequeue a token from a list (lock already held by caller) */
static inline struct td_token *__td_tokens_dequeue(struct td_token_list *tl)
{
	struct td_token *tok = NULL;

	if (!list_empty(&tl->list)) {
		tok = list_first_entry(&tl->list,
				struct td_token, link);
		list_del(&tok->link);
		tl->count --;
	}

	return tok;
}

/** lock and dequeue from list */
static inline struct td_token *td_tokens_dequeue(struct td_token_list *tl)
{
	struct td_token *tok;

	td_tokens_lock(tl);

	tok = __td_tokens_dequeue(tl);

	td_tokens_unlock(tl);

	return tok;
}

/** enqueue a token at tail of list (lock already held by caller) */
static inline void __td_tokens_enqueue(struct td_token_list *tl,
		struct td_token *tok, int quick_reuse)
{
	list_add_tail(&tok->link, &tl->list);
	tl->count ++;
}

/** lock and enqueue at tail of list */
static inline void td_tokens_enqueue(struct td_token_list *tl,
		struct td_token *tok)
{
	td_tokens_lock(tl);
	__td_tokens_enqueue(tl, tok, 0);
	td_tokens_unlock(tl);
}

/** push a token to head of list (lock already held by caller) */
static inline void __td_tokens_push(struct td_token_list *tl,
		struct td_token *tok)
{
	list_add(&tok->link, &tl->list);
	tl->count ++;
}

/** lock and push to head of list */
static inline void td_tokens_push(struct td_token_list *tl,
		struct td_token *tok)
{
	td_tokens_lock(tl);
	__td_tokens_push(tl, tok);
	td_tokens_unlock(tl);
}

/** traverse the token list safely */
#define for_each_token_list_token(_tok,_nxt,_toklist)                   \
	list_for_each_entry_safe((_tok), (_nxt),                        \
			&(_toklist)->list, link)

/** remove an element already known to be on the list (lock already held by caller) */
static inline void __td_tokens_del(struct td_token_list *tl,
		struct td_token *tok)
{
	list_del(&tok->link);
	tl->count --;
}

/** lock and push to head of list */
static inline void td_tokens_del(struct td_token_list *tl,
		struct td_token *tok)
{
	td_tokens_lock(tl);
	__td_tokens_del(tl, tok);
	td_tokens_unlock(tl);
}


#endif
