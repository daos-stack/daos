/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_LIST_H__
#define __UTILS_LIST_H__

#include "ocf_env.h"
#include "../ocf_ctx_priv.h"
#include "ocf/ocf_cache.h"

#define OCF_LST_DBG 1

#if 1 == OCF_LST_DBG
#define OCF_LST_DBG_ON(lst, cond) ({ \
	if (cond) { \
		ocf_log(ocf_cache_get_ctx(lst->cache), log_crit, \
				"OCF list critical problem (%s:%u)\n", \
				__func__, __LINE__); \
		ocf_log_stack_trace(ocf_cache_get_ctx(lst->cache)); \
	} \
})
#else
#define OCF_LST_DBG_ON(lst, cond)
#endif

#define OCF_LST_ENTRY_OUT(lst) ((lst)->invalid + 1)

struct ocf_lst_entry {
	ocf_cache_line_t next;
	ocf_cache_line_t prev;
};

typedef struct ocf_lst_entry *(*ocf_mlst_getter)(
		struct ocf_cache *cache, ocf_cache_line_t idx);

typedef int (*ocf_mlst_cmp)(struct ocf_cache *cache,
		struct ocf_lst_entry *e1, struct ocf_lst_entry *e2);

struct ocf_lst {
	struct ocf_lst_entry *head;
	ocf_cache_line_t invalid;
	struct {
		uint32_t active : 1;
	} flags;

	ocf_mlst_getter getter;
	ocf_mlst_cmp cmp;
	struct ocf_cache *cache;
};

static inline void ocf_lst_init_entry(struct ocf_lst *lst,
		struct ocf_lst_entry *entry)
{
	entry->next = entry->prev = OCF_LST_ENTRY_OUT(lst);
}

static inline bool ocf_lst_is_entry(struct ocf_lst *lst,
		struct ocf_lst_entry *entry)
{
	if (entry->next == OCF_LST_ENTRY_OUT(lst) &&
			entry->prev == OCF_LST_ENTRY_OUT(lst))
		return false;

	if (entry->next < OCF_LST_ENTRY_OUT(lst) &&
			entry->prev < OCF_LST_ENTRY_OUT(lst))
		return true;

	ENV_BUG();
	return false;
}

static inline void ocf_lst_init(struct ocf_cache *cache,
		struct ocf_lst *lst, ocf_cache_line_t invalid,
		ocf_mlst_getter getter, ocf_mlst_cmp cmp)
{
	ocf_cache_line_t idx;

	ENV_BUG_ON(env_memset(lst, sizeof(*lst), 0));

	lst->head = getter(cache, invalid);
	lst->head->next = invalid;
	lst->head->prev = invalid;
	lst->invalid = invalid;
	lst->getter = getter;
	lst->cmp = cmp;
	lst->cache = cache;

	for (idx = 0; idx < lst->invalid; idx++) {
		struct ocf_lst_entry *entry = getter(cache, idx);

		ocf_lst_init_entry(lst, entry);
	}
}

static inline void ocf_lst_add_after(struct ocf_lst *lst,
		ocf_cache_line_t at, ocf_cache_line_t idx)
{
	struct ocf_lst_entry *after = lst->getter(lst->cache, at);
	struct ocf_lst_entry *next = lst->getter(lst->cache, after->next);
	struct ocf_lst_entry *this = lst->getter(lst->cache, idx);

	OCF_LST_DBG_ON(lst, ocf_lst_is_entry(lst, this));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, after));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, next));

	this->next = after->next;
	this->prev = at;
	after->next = idx;
	next->prev = idx;
}

static inline void ocf_lst_add_before(struct ocf_lst *lst,
		ocf_cache_line_t at, ocf_cache_line_t idx)
{
	struct ocf_lst_entry *before = lst->getter(lst->cache, at);
	struct ocf_lst_entry *prev = lst->getter(lst->cache, before->prev);
	struct ocf_lst_entry *this = lst->getter(lst->cache, idx);

	OCF_LST_DBG_ON(lst, ocf_lst_is_entry(lst, this));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, before));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, prev));

	this->next = at;
	this->prev = before->prev;
	before->prev = idx;
	prev->next = idx;
}

static inline void ocf_lst_add(struct ocf_lst *lst, ocf_cache_line_t idx)
{
	struct ocf_lst_entry *this = lst->getter(lst->cache, idx);
	struct ocf_lst_entry *next = lst->getter(lst->cache, lst->head->next);

	OCF_LST_DBG_ON(lst, ocf_lst_is_entry(lst, this));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, next));

	this->next = lst->head->next;
	next->prev = idx;
	lst->head->next = idx;
	this->prev = lst->invalid;
}

static inline void ocf_lst_add_tail(struct ocf_lst *lst, ocf_cache_line_t idx)
{
	struct ocf_lst_entry *this = lst->getter(lst->cache, idx);
	struct ocf_lst_entry *prev = lst->getter(lst->cache, lst->head->prev);

	OCF_LST_DBG_ON(lst, ocf_lst_is_entry(lst, this));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, prev));

	this->next = lst->invalid;
	this->prev = lst->head->prev;
	prev->next = idx;
	lst->head->prev = idx;
}

static inline void ocf_lst_del(struct ocf_lst *lst, ocf_cache_line_t idx)
{
	struct ocf_lst_entry *this = lst->getter(lst->cache, idx);
	struct ocf_lst_entry *next = lst->getter(lst->cache, this->next);
	struct ocf_lst_entry *prev = lst->getter(lst->cache, this->prev);

	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, this));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, next));
	OCF_LST_DBG_ON(lst, !ocf_lst_is_entry(lst, prev));

	prev->next = this->next;
	next->prev = this->prev;

	ocf_lst_init_entry(lst, this);
}

static inline ocf_cache_line_t ocf_lst_head(struct ocf_lst *lst)
{
	return lst->head->next;
}

static inline ocf_cache_line_t ocf_lst_tail(struct ocf_lst *lst)
{
	return lst->head->prev;
}

static inline bool ocf_lst_empty(struct ocf_lst *lst)
{
	if (lst->head->next == lst->invalid)
		return true;
	else
		return false;
}

void ocf_lst_sort(struct ocf_lst *lst);

#define for_each_lst(lst, entry, id) \
for (id = (lst)->head->next, entry = (lst)->getter((lst)->cache, id); \
	entry != (lst)->head; id = entry->next, \
	entry = (lst)->getter((lst)->cache, id))

#define for_each_lst_entry(lst, entry, id, type, member) \
for (id = (lst)->head->next, \
	entry = container_of((lst)->getter((lst)->cache, id), type, member); \
	entry != container_of((lst)->head, type, member); \
	id = entry->member.next, \
	entry = container_of((lst)->getter((lst)->cache, id), type, member))

#endif /* __UTILS_LIST_H__ */
