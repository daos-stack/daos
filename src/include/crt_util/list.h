/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __CRT_LIST_H__
#define __CRT_LIST_H__
/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

#define prefetch(a) ((void)a)

struct crt_list_head {
	struct crt_list_head *next, *prev;
};

typedef struct crt_list_head crt_list_t;

#define CRT_LIST_HEAD_INIT(name) { &(name), &(name) }

#define CRT_LIST_HEAD(name) \
	crt_list_t name = CRT_LIST_HEAD_INIT(name)

#define CRT_INIT_LIST_HEAD(ptr) do { \
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
__crt_list_add(crt_list_t *newe, crt_list_t *prev, crt_list_t *next)
{
	next->prev = newe;
	newe->next = next;
	newe->prev = prev;
	prev->next = newe;
}

/**
 * Insert an entry at the start of a list.
 * \param newe  new entry to be inserted
 * \param head list to add it to
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void
crt_list_add(crt_list_t *newe, crt_list_t *head)
{
	__crt_list_add(newe, head, head->next);
}

/**
 * Insert an entry at the end of a list.
 * \param newe  new entry to be inserted
 * \param head list to add it to
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void
crt_list_add_tail(crt_list_t *newe, crt_list_t *head)
{
	__crt_list_add(newe, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void
__crt_list_del(crt_list_t *prev, crt_list_t *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * Remove an entry from the list it is currently in.
 * \param entry the entry to remove
 * Note: list_empty(entry) does not return true after this, the entry is in an
 * undefined state.
 */
static inline void
crt_list_del(crt_list_t *entry)
{
	__crt_list_del(entry->prev, entry->next);
}

/**
 * Remove an entry from the list it is currently in and reinitialize it.
 * \param entry the entry to remove.
 */
static inline void
crt_list_del_init(crt_list_t *entry)
{
	__crt_list_del(entry->prev, entry->next);
	CRT_INIT_LIST_HEAD(entry);
}

/**
 * Remove an entry from the list it is currently in and insert it at the start
 * of another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void
crt_list_move(crt_list_t *list, crt_list_t *head)
{
	__crt_list_del(list->prev, list->next);
	crt_list_add(list, head);
}

/**
 * Remove an entry from the list it is currently in and insert it at the end of
 * another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void
crt_list_move_tail(crt_list_t *list, crt_list_t *head)
{
	__crt_list_del(list->prev, list->next);
	crt_list_add_tail(list, head);
}

/**
 * Test whether a list is empty
 * \param head the list to test.
 */
static inline int
crt_list_empty(crt_list_t *head)
{
	return head->next == head;
}

/**
 * Test whether a list is empty and not being modified
 * \param head the list to test
 *
 * Tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * NOTE: using crt_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is crt_list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int
crt_list_empty_careful(const crt_list_t *head)
{
	crt_list_t *next = head->next;
	return (next == head) && (next == head->prev);
}

static inline void
__crt_list_splice(crt_list_t *list, crt_list_t *head)
{
	crt_list_t *first = list->next;
	crt_list_t *last = list->prev;
	crt_list_t *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * Join two lists
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is in an
 * undefined state on return.
 */
static inline void
crt_list_splice(crt_list_t *list, crt_list_t *head)
{
	if (!crt_list_empty(list))
		__crt_list_splice(list, head);
}

/**
 * Join two lists and reinitialise the emptied list.
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is empty
 * on return.
 */
static inline void
crt_list_splice_init(crt_list_t *list, crt_list_t *head)
{
	if (!crt_list_empty(list)) {
		__crt_list_splice(list, head);
		CRT_INIT_LIST_HEAD(list);
	}
}

/**
 * Get the container of a list
 * \param ptr	 the embedded list.
 * \param type	 the type of the struct this is embedded in.
 * \param member the member name of the list within the struct.
 */
#define crt_list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

/**
 * Iterate over a list
 * \param pos	the iterator
 * \param head	the list to iterate over
 *
 * Behaviour is undefined if \a pos is removed from the list in the body of the
 * loop.
 */
#define crt_list_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
		pos = pos->next, prefetch(pos->next))

/**
 * Iterate over a list safely
 * \param pos	the iterator
 * \param n     temporary storage
 * \param head	the list to iterate over
 *
 * This is safe to use if \a pos could be removed from the list in the body of
 * the loop.
 */
#define crt_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * Iterate over a list continuing after existing point
 * \param pos    the type * to use as a loop counter
 * \param head   the list head
 * \param member the name of the list_struct within the struct
 */
#define crt_list_for_each_entry_continue(pos, head, member)                 \
	for (pos = crt_list_entry(pos->member.next, __typeof__(*pos), member);\
	     prefetch(pos->member.next), &pos->member != (head);             \
	     pos = crt_list_entry(pos->member.next, __typeof__(*pos), member))

/**
 * \defgroup hlist Hash List
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is too
 * wasteful.  You lose the ability to access the tail in O(1).
 * @{
 */

typedef struct crt_hlist_node {
	struct crt_hlist_node *next, **pprev;
} crt_hlist_node_t;

typedef struct crt_hlist_head {
	crt_hlist_node_t *first;
} crt_hlist_head_t;

/* @} */

/*
 * "NULL" might not be defined at this point
 */
#ifdef NULL
#define NULL_P NULL
#else
#define NULL_P ((void *)0)
#endif

/**
 * \addtogroup hlist
 * @{
 */

#define CRT_HLIST_HEAD_INIT { NULL_P }
#define CRT_HLIST_HEAD(name) crt_hlist_head_t name = { NULL_P }
#define CRT_INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL_P)
#define CRT_INIT_HLIST_NODE(ptr) ((ptr)->next = NULL_P, (ptr)->pprev = NULL_P)

static inline int
crt_hlist_unhashed(const crt_hlist_node_t *h)
{
	return !h->pprev;
}

static inline int
crt_hlist_empty(const crt_hlist_head_t *h)
{
	return !h->first;
}

static inline void
__crt_hlist_del(crt_hlist_node_t *n)
{
	crt_hlist_node_t *next = n->next;
	crt_hlist_node_t **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline void
crt_hlist_del(crt_hlist_node_t *n)
{
	__crt_hlist_del(n);
}

static inline void
crt_hlist_del_init(crt_hlist_node_t *n)
{
	if (n->pprev)  {
		__crt_hlist_del(n);
		CRT_INIT_HLIST_NODE(n);
	}
}

static inline void
crt_hlist_add_head(crt_hlist_node_t *n, crt_hlist_head_t *h)
{
	crt_hlist_node_t *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

/* next must be != NULL */
static inline void
crt_hlist_add_before(crt_hlist_node_t *n, crt_hlist_node_t *next)
{
	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

/* prev must be != NULL */
static inline void
crt_hlist_add_after(crt_hlist_node_t *n, crt_hlist_node_t *prev)
{
	n->pprev = &prev->next;
	n->next = prev->next;
	prev->next = n;

	if (n->next)
		n->next->pprev  = &n->next;
}

#define crt_hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define crt_hlist_for_each(pos, head) \
	for (pos = (head)->first; pos && (prefetch(pos->next), 1); \
	     pos = pos->next)

#define crt_hlist_for_each_safe(pos, n, head) \
	for (pos = (head)->first; pos && (n = pos->next, 1); \
	     pos = n)

/**
 * Iterate over an hlist of given type
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param head	 the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define crt_hlist_for_each_entry(tpos, pos, head, member)                   \
	for (pos = (head)->first;                                            \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = crt_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing after existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define crt_hlist_for_each_entry_continue(tpos, pos, member)                \
	for (pos = (pos)->next;                                              \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = crt_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing from an existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define crt_hlist_for_each_entry_from(tpos, pos, member)		     \
	for (; pos && ({ prefetch(pos->next); 1;}) &&                        \
		({ tpos = crt_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = pos->next)

/**
 * Iterate over an hlist of given type safe against removal of list entry
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param n	 another &struct hlist_node to use as temporary storage
 * \param head	 the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define crt_hlist_for_each_entry_safe(tpos, pos, n, head, member)           \
	for (pos = (head)->first;                                            \
	     pos && ({ n = pos->next; 1; }) &&                               \
		({ tpos = crt_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = n)

/* @} */

#ifndef crt_list_for_each_prev
/**
 * Iterate over a list in reverse order
 * \param pos	the &struct list_head to use as a loop counter.
 * \param head	the head for your list.
 */
#define crt_list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head);     \
		pos = pos->prev, prefetch(pos->prev))

#endif /* crt_list_for_each_prev */

#ifndef crt_list_for_each_entry
/**
 * Iterate over a list of given type
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define crt_list_for_each_entry(pos, head, member)                          \
	for (pos = crt_list_entry((head)->next, __typeof__(*pos), member),  \
		     prefetch(pos->member.next);                             \
	     &pos->member != (head);                                         \
	     pos = crt_list_entry(pos->member.next, __typeof__(*pos), member),\
	     prefetch(pos->member.next))
#endif /* crt_list_for_each_entry */

#ifndef crt_list_for_each_entry_rcu
#define crt_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef crt_list_for_each_entry_rcu
#define crt_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef crt_list_for_each_entry_reverse
/**
 * Iterate backwards over a list of given type.
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define crt_list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = crt_list_entry((head)->prev, __typeof__(*pos), member);  \
	     prefetch(pos->member.prev), &pos->member != (head);             \
	     pos = crt_list_entry(pos->member.prev, __typeof__(*pos), member))
#endif /* crt_list_for_each_entry_reverse */

#ifndef crt_list_for_each_entry_safe
/**
 * Iterate over a list of given type safe against removal of list entry
 * \param pos        the type * to use as a loop counter.
 * \param n          another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define crt_list_for_each_entry_safe(pos, n, head, member)                   \
	for (pos = crt_list_entry((head)->next, __typeof__(*pos), member),   \
		n = crt_list_entry(pos->member.next, __typeof__(*pos),       \
				    member);                                  \
	     &pos->member != (head);                                          \
	     pos = n, n = crt_list_entry(n->member.next, __typeof__(*n),     \
					  member))

#endif /* crt_list_for_each_entry_safe */

#ifndef crt_list_for_each_entry_safe_from
/**
 * Iterate over a list continuing from an existing point
 * \param pos        the type * to use as a loop cursor.
 * \param n          another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define crt_list_for_each_entry_safe_from(pos, n, head, member)             \
	for (n = crt_list_entry(pos->member.next, __typeof__(*pos), member);\
	     &pos->member != (head);                                         \
	     pos = n, n = crt_list_entry(n->member.next, __typeof__(*n),    \
					  member))
#endif /* crt_list_for_each_entry_safe_from */

#define crt_list_for_each_entry_typed(pos, head, type, member)		\
	for (pos = crt_list_entry((head)->next, type, member),		\
	     prefetch(pos->member.next);				\
	     &pos->member != (head);                                    \
	     pos = crt_list_entry(pos->member.next, type, member),	\
	     prefetch(pos->member.next))

#define crt_list_for_each_entry_reverse_typed(pos, head, type, member)	\
	for (pos = crt_list_entry((head)->prev, type, member);		\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = crt_list_entry(pos->member.prev, type, member))

#define crt_list_for_each_entry_safe_typed(pos, n, head, type, member)	\
	for (pos = crt_list_entry((head)->next, type, member),		\
	     n = crt_list_entry(pos->member.next, type, member);	\
	     &pos->member != (head);                                    \
	     pos = n, n = crt_list_entry(n->member.next, type, member))

#define crt_list_for_each_entry_safe_from_typed(pos, n, head, type, member)  \
	for (n = crt_list_entry(pos->member.next, type, member);             \
	     &pos->member != (head);                                          \
	     pos = n, n = crt_list_entry(n->member.next, type, member))

#define crt_hlist_for_each_entry_typed(tpos, pos, head, type, member)   \
	for (pos = (head)->first;                                        \
	     pos && (prefetch(pos->next), 1) &&                          \
		(tpos = crt_hlist_entry(pos, type, member), 1);         \
	     pos = pos->next)

#define crt_hlist_for_each_entry_safe_typed(tpos, pos, n, head, type, member) \
	for (pos = (head)->first;                                              \
	     pos && (n = pos->next, 1) &&                                      \
		(tpos = crt_hlist_entry(pos, type, member), 1);               \
	     pos = n)

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_LIST_H__ */
