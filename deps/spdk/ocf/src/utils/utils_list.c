/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "utils_list.h"

void ocf_lst_sort(struct ocf_lst *lst)
{
	ocf_cache_line_t iter_idx;
	ocf_cache_line_t next_idx;
	struct ocf_lst_entry *iter;

	if (!lst->cmp) {
		/* No comparator, no needed to sort */
		return;
	}

	if (ocf_lst_empty(lst)) {
		/* List is empty nothing to do */
		return;
	}

	/* Get iterator - first element on the list, and one after */
	iter_idx = lst->head->next;
	iter = lst->getter(lst->cache, iter_idx);
	next_idx = iter->next;
	lst->getter(lst->cache, iter->next);

	/* Initialize list to initial empty state, it will be empty */
	lst->head->next = lst->invalid;
	lst->head->prev = lst->invalid;

	while (iter_idx != lst->invalid) {
		ocf_lst_init_entry(lst, iter);

		if (ocf_lst_empty(lst)) {
			/* Put first at the the list */
			ocf_lst_add(lst, iter_idx);
		} else {
			/* search for place where put element at the list */
			struct ocf_lst_entry *pos;
			ocf_cache_line_t pos_idx;

			for_each_lst(lst, pos, pos_idx)
				if (lst->cmp(lst->cache, pos, iter) > 0)
					break;

			if (lst->invalid == pos_idx) {
				/* Put at the end of list */
				ocf_lst_add_tail(lst, iter_idx);
			} else {
				/* Position is known, put it before */
				ocf_lst_add_before(lst, pos_idx, iter_idx);
			}
		}

		/* Switch to next */
		iter_idx = next_idx;
		iter = lst->getter(lst->cache, iter_idx);
		next_idx = iter->next;
	}
}
