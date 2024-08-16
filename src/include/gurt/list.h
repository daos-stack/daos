/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __GURT_LIST_H__
#define __GURT_LIST_H__
/**
 * \file
 *
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

/** @addtogroup GURT
 * @{
 */
#ifdef __GNUC__
#define prefetch(a) __builtin_prefetch((a), 0, 1)
#else
#define prefetch(a) ((void)a)
#endif

struct d_list_head {
	struct d_list_head *next, *prev;
};

typedef struct d_list_head d_list_t;

#define D_LIST_HEAD_INIT(name) { &(name), &(name) }

#define D_LIST_HEAD(name) \
	d_list_t name = D_LIST_HEAD_INIT(name)

#define D_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void
__gurt_list_add(d_list_t *newe, d_list_t *prev, d_list_t *next)
{
	next->prev = newe;
	newe->next = next;
	newe->prev = prev;
	prev->next = newe;
}

/**
 * Insert an entry at the start of a list.
 * This is useful for implementing stacks.
 *
 * \param[in] newe	new entry to be inserted
 * \param[in] head	list to add it to
 */
static inline void
d_list_add(d_list_t *newe, d_list_t *head)
{
	__gurt_list_add(newe, head, head->next);
}

/**
 * Insert an entry at the end of a list.
 * This is useful for implementing queues.
 *
 * \param[in] newe	new entry to be inserted
 * \param[in] head	list to add it to
 *
 */
static inline void
d_list_add_tail(d_list_t *newe, d_list_t *head)
{
	__gurt_list_add(newe, head->prev, head);
}

/**
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * \param[in] prev	previous entry
 * \param[in] next	next entry
 *
 * \note This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void
__gurt_list_del(d_list_t *prev, d_list_t *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * Remove an entry from the list it is currently in.
 *
 * \param[in]	entry the entry to remove
 *
 * \note list_empty(entry) does not return true after this, the entry is in an
 * undefined state.
 */
static inline void
d_list_del(d_list_t *entry)
{
	__gurt_list_del(entry->prev, entry->next);
}

/**
 * Remove an entry from the list it is currently in and reinitialize it.
 *
 * \param[in] entry	the entry to remove.
 */
static inline void
d_list_del_init(d_list_t *entry)
{
	__gurt_list_del(entry->prev, entry->next);
	D_INIT_LIST_HEAD(entry);
}

/**
 * Remove an entry from the list it is currently in and insert it at the start
 * of another list.
 *
 * \param[in] list	the entry to move
 * \param[in] head	the list to move it to
 */
static inline void
d_list_move(d_list_t *list, d_list_t *head)
{
	__gurt_list_del(list->prev, list->next);
	d_list_add(list, head);
}

/**
 * Remove an entry from the list it is currently in and insert it at the end of
 * another list.
 *
 * \param[in] list	the entry to move
 * \param[in] head	the list to move it to
 */
static inline void
d_list_move_tail(d_list_t *list, d_list_t *head)
{
	__gurt_list_del(list->prev, list->next);
	d_list_add_tail(list, head);
}

/**
 * Test whether a list is empty
 *
 * \param[in] head	the list to test.
 */
static inline int
d_list_empty(d_list_t *head)
{
	return head->next == head;
}

/**
 * Tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * \param[in] head the list to test
 *
 * \note using d_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is d_list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int
d_list_empty_careful(const d_list_t *head)
{
	d_list_t *next = head->next;

	return (next == head) && (next == head->prev);
}

static inline void
__gurt_list_splice(d_list_t *list, d_list_t *head)
{
	d_list_t *first = list->next;
	d_list_t *last = list->prev;
	d_list_t *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * Join two lists
 * The contents of \p list are added at the start of \p head.  \p list is in an
 * undefined state on return.
 *
 * \param[in] list	the new list to add.
 * \param[in] head	the place to add it in the first list.
 *
 */
static inline void
d_list_splice(d_list_t *list, d_list_t *head)
{
	if (!d_list_empty(list))
		__gurt_list_splice(list, head);
}

/**
 * Join two lists and reinitialize the emptied list.
 * The contents of \p list are added at the start of \p head.  \p list is empty
 * on return.
 *
 * \param[in,out] list	the new list to add.
 * \param[in] head	the place to add it in the first list.
 */
static inline void
d_list_splice_init(d_list_t *list, d_list_t *head)
{
	if (!d_list_empty(list)) {
		__gurt_list_splice(list, head);
		D_INIT_LIST_HEAD(list);
	}
}

/**
 * Get the container of a list
 * \param[in] ptr	the embedded list.
 * \param[in] type	the type of the struct this is embedded in.
 * \param[in] member	the member name of the list within the struct.
 */
#define d_list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-offsetof(type, member)))

#define d_list_pop_entry(list, type, member)			\
	({							\
		type *__r = NULL;				\
		if (!d_list_empty(list)) {				\
			__r = d_list_entry((list)->next, type, member);	\
			d_list_del_init((list)->next);			\
		}						\
		__r;						\
	})

/**
 * Iterate over a list
 * Behavior is undefined if \p pos is removed from the list in the body of the
 * loop.
 *
 * \param[in] pos	the iterator
 * \param[in] head	the list to iterate over
 *
 */
#define d_list_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
		pos = pos->next, prefetch(pos->next))

/**
 * Iterate over a list safely
 *
 * This is safe to use if \a pos could be removed from the list in the body of
 * the loop.
 *
 * \param[in] pos	the iterator
 * \param[in] n		temporary storage
 * \param[in] head	the list to iterate over
 */
#define d_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * Iterate over a list continuing after existing point
 *
 * \param[in] pos	the type * to use as a loop counter
 * \param[in] head	the list head
 * \param[in] member	the name of the list_struct within the struct
 */
#define d_list_for_each_entry_continue(pos, head, member)                 \
	for (pos = d_list_entry(pos->member.next, __typeof__(*pos), member);\
	     prefetch(pos->member.next), &pos->member != (head);             \
	     pos = d_list_entry(pos->member.next, __typeof__(*pos), member))

typedef struct d_hlist_node {
	struct d_hlist_node *next, **pprev;
} d_hlist_node_t;

typedef struct d_hlist_head {
	d_hlist_node_t *first;
} d_hlist_head_t;


/*
 * "NULL" might not be defined at this point
 */
#ifdef NULL
#define NULL_P NULL
#else
#define NULL_P ((void *)0)
#endif


#define D_HLIST_HEAD_INIT { NULL_P }
#define D_HLIST_HEAD(name) d_hlist_head_t name = { NULL_P }
#define D_INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL_P)
#define D_INIT_HLIST_NODE(ptr) ((ptr)->next = NULL_P, (ptr)->pprev = NULL_P)

static inline int
d_hlist_unhashed(const d_hlist_node_t *h)
{
	return !h->pprev;
}

static inline int
d_hlist_empty(const d_hlist_head_t *h)
{
	return !h->first;
}

static inline void
__gurt_hlist_del(d_hlist_node_t *n)
{
	d_hlist_node_t *next = n->next;
	d_hlist_node_t **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline void
d_hlist_del(d_hlist_node_t *n)
{
	__gurt_hlist_del(n);
}

static inline void
d_hlist_del_init(d_hlist_node_t *n)
{
	if (n->pprev)  {
		__gurt_hlist_del(n);
		D_INIT_HLIST_NODE(n);
	}
}

static inline void
d_hlist_add_head(d_hlist_node_t *n, d_hlist_head_t *h)
{
	d_hlist_node_t *first = h->first;

	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

/* next must be != NULL */
static inline void
d_hlist_add_before(d_hlist_node_t *n, d_hlist_node_t *next)
{
	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

/* prev must be != NULL */
static inline void
d_hlist_add_after(d_hlist_node_t *n, d_hlist_node_t *prev)
{
	n->pprev = &prev->next;
	n->next = prev->next;
	prev->next = n;

	if (n->next)
		n->next->pprev  = &n->next;
}

#define d_hlist_entry(ptr, type, member) d_list_entry(ptr, type, member)

#define dhlist_for_each(pos, head) \
	for (pos = (head)->first; pos && (prefetch(pos->next), 1); \
	     pos = pos->next)

#define dhlist_for_each_safe(pos, n, head) \
	for (pos = (head)->first; pos && (n = pos->next, 1); \
	     pos = n)

/**
 * Iterate over an hlist of given type
 *
 * \param[in] tpos	the type * to use as a loop counter.
 * \param[in] pos	the &struct hlist_node to use as a loop counter.
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the hlist_node within the struct.
 */
#define dhlist_for_each_entry(tpos, pos, head, member)                   \
	for (pos = (head)->first;                                           \
	     pos && ({ prefetch(pos->next); 1; }) &&                        \
		({ tpos = d_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                   \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing after existing point
 *
 * \param[in] tpos	the type * to use as a loop counter.
 * \param[in] pos	the &struct hlist_node to use as a loop counter.
 * \param[in] member	the name of the hlist_node within the struct.
 */
#define dhlist_for_each_entry_continue(tpos, pos, member)                \
	for (pos = (pos)->next;                                             \
	     pos && ({ prefetch(pos->next); 1; }) &&                        \
		({ tpos = d_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                   \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing from an existing point
 *
 * \param[in] tpos	the type * to use as a loop counter.
 * \param[in] pos	the &struct hlist_node to use as a loop counter.
 * \param[in] member	the name of the hlist_node within the struct.
 */
#define dhlist_for_each_entry_from(tpos, pos, member)		    \
	for (; pos && ({ prefetch(pos->next); 1; }) &&                      \
		({ tpos = d_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                   \
	     pos = pos->next)

/**
 * Iterate over an hlist of given type safe against removal of list entry
 *
 * \param[in] tpos	the type * to use as a loop counter.
 * \param[in] pos	the &struct hlist_node to use as a loop counter.
 * \param[in] n		another &struct hlist_node to use as temporary storage
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the hlist_node within the struct.
 */
#define dhlist_for_each_entry_safe(tpos, pos, n, head, member)           \
	for (pos = (head)->first;                                            \
	     pos && ({ n = pos->next; 1; }) &&                               \
		({ tpos = d_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = n)


#ifndef d_list_for_each_prev
/**
 * Iterate over a list in reverse order
 *
 * \param[in] pos	the &struct list_head to use as a loop counter.
 * \param[in] head	the head for your list.
 */
#define d_list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head);     \
		pos = pos->prev, prefetch(pos->prev))

#endif /* d_list_for_each_prev */

#ifndef d_list_for_each_entry
/**
 * Iterate over a list of given type
 *
 * \param[in] pos	the type * to use as a loop counter.
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the list_struct within the struct.
 */
#define d_list_for_each_entry(pos, head, member)                          \
	for (pos = d_list_entry((head)->next, __typeof__(*pos), member),  \
		     prefetch(pos->member.next);                             \
	     &pos->member != (head);                                         \
	     pos = d_list_entry(pos->member.next, __typeof__(*pos), member),\
	     prefetch(pos->member.next))
#endif /* d_list_for_each_entry */

#ifndef d_list_for_each_entry_rcu
#define d_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef d_list_for_each_entry_rcu
#define d_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef d_list_for_each_entry_reverse
/**
 * Iterate backwards over a list of given type.
 *
 * \param[in] pos	the type * to use as a loop counter.
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the list_struct within the struct.
 */
#define d_list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = d_list_entry((head)->prev, __typeof__(*pos), member);  \
	     prefetch(pos->member.prev), &pos->member != (head);             \
	     pos = d_list_entry(pos->member.prev, __typeof__(*pos), member))
#endif /* d_list_for_each_entry_reverse */

#ifndef d_list_for_each_entry_safe
/**
 * Iterate over a list of given type safe against removal of list entry
 *
 * \param[in] pos	the type * to use as a loop counter.
 * \param[in] n		another type * to use as temporary storage
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the list_struct within the struct.
 */
#define d_list_for_each_entry_safe(pos, n, head, member)                   \
	for (pos = d_list_entry((head)->next, __typeof__(*pos), member),   \
		n = d_list_entry(pos->member.next, __typeof__(*pos),       \
				    member);                                  \
	     &pos->member != (head);                                          \
	     pos = n, n = d_list_entry(n->member.next, __typeof__(*n),     \
					  member))

#endif /* d_list_for_each_entry_safe */

#ifndef d_list_for_each_entry_reverse_safe
/**
 * Iterate backwards over a list of given type safe against removal of list entry
 *
 * \param[in] pos	the type * to use as a loop counter.
 * \param[in] n		another type * to use as temporary storage
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the list_struct within the struct.
 */
#define d_list_for_each_entry_reverse_safe(pos, n, head, member)               \
	for (pos = d_list_entry((head)->prev, __typeof__(*pos), member),       \
	     n = d_list_entry(pos->member.prev, __typeof__(*pos), member);     \
	     &pos->member != (head);                                           \
	     pos = n, n = d_list_entry(pos->member.prev, __typeof__(*pos),     \
				       member))
#endif /* d_list_for_each_entry_reverse_safe */

#ifndef d_list_for_each_entry_safe_from
/**
 * Iterate over a list continuing from an existing point
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 *
 * \param[in] pos	the type * to use as a loop cursor.
 * \param[in] n		another type * to use as temporary storage
 * \param[in] head	the head for your list.
 * \param[in] member	the name of the list_struct within the struct.
 */
#define d_list_for_each_entry_safe_from(pos, n, head, member)             \
	for (n = d_list_entry(pos->member.next, __typeof__(*pos), member);\
	     &pos->member != (head);                                         \
	     pos = n, n = d_list_entry(n->member.next, __typeof__(*n),    \
					  member))
#endif /* d_list_for_each_entry_safe_from */

#define d_list_for_each_entry_typed(pos, head, type, member)		\
	for (pos = d_list_entry((head)->next, type, member),		\
	     prefetch(pos->member.next);				\
	     &pos->member != (head);                                    \
	     pos = d_list_entry(pos->member.next, type, member),	\
	     prefetch(pos->member.next))

#define d_list_for_each_entry_reverse_typed(pos, head, type, member)	\
	for (pos = d_list_entry((head)->prev, type, member);		\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = d_list_entry(pos->member.prev, type, member))

#define d_list_for_each_entry_safe_typed(pos, n, head, type, member)	\
	for (pos = d_list_entry((head)->next, type, member),		\
	     n = d_list_entry(pos->member.next, type, member);		\
	     &pos->member != (head);					\
	     pos = n, n = d_list_entry(n->member.next, type, member))

#define d_list_for_each_entry_safe_from_typed(pos, n, head, type, member)  \
	for (n = d_list_entry(pos->member.next, type, member);             \
	     &pos->member != (head);                                       \
	     pos = n, n = d_list_entry(n->member.next, type, member))

#define dhlist_for_each_entry_typed(tpos, pos, head, type, member)	\
	for (pos = (head)->first;					\
	     pos && (prefetch(pos->next), 1) &&				\
		(tpos = d_hlist_entry(pos, type, member), 1);		\
	     pos = pos->next)

#define dhlist_for_each_entry_safe_typed(tpos, pos, n, head, type, member) \
	for (pos = (head)->first;                                              \
	     pos && (n = pos->next, 1) &&                                      \
		(tpos = d_hlist_entry(pos, type, member), 1);               \
	     pos = n)

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_LIST_H__ */
