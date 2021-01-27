/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

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

/**
 * Circular queue definitions:
 *
 * A circular queue is headed by a structure defined by the CIRCLEQ_HEAD()
 * macro. This structure contains a pair of pointers, one to the first element
 * in the circular queue and the other to the last element in the circular
 * queue. The elements are doubly linked so that an arbitrary element can be
 * removed without traversing the queue. New elements can be added to the queue
 * after an existing element, before an existing element, at the head of
 * the queue, or at the end of the queue.
 *
 * A CIRCLEQ_HEAD structure is declared as follows:
 */
#define	D_CIRCLEQ_HEAD(name, type)					\
struct name {								\
	struct type *cqh_first;		/* first element */		\
	struct type *cqh_last;		/* last element */		\
}

#define	D_CIRCLEQ_HEAD_INIT(head)				\
	{ (void *)&head, (void *)&head }

#define	d_circleq_entry(type)						\
struct {								\
	struct type *cqe_next;		/* next element */		\
	struct type *cqe_prev;		/* previous element */		\
}

/*
 * Circular queue functions.
 */
#define	D_CIRCLEQ_INIT(head) do {					\
	(head)->cqh_first = (void *)(head);				\
	(head)->cqh_last  = (void *)(head);				\
} while (0)

#define	D_CIRCLEQ_INSERT_AFTER(head, listelm, elm, field) do {		\
	(elm)->field.cqe_next = (listelm)->field.cqe_next;		\
	(elm)->field.cqe_prev = (listelm);				\
	if ((listelm)->field.cqe_next == (void *)(head))		\
		(head)->cqh_last = (elm);				\
	else								\
		(listelm)->field.cqe_next->field.cqe_prev = (elm);	\
	(listelm)->field.cqe_next = (elm);				\
} while (0)

#define	D_CIRCLEQ_INSERT_BEFORE(head, listelm, elm, field) do {		\
	(elm)->field.cqe_next = (listelm);				\
	(elm)->field.cqe_prev = (listelm)->field.cqe_prev;		\
	if ((listelm)->field.cqe_prev == (void *)(head))		\
		(head)->cqh_first = (elm);				\
	else								\
		(listelm)->field.cqe_prev->field.cqe_next = (elm);	\
	(listelm)->field.cqe_prev = (elm);				\
} while (0)

#define	D_CIRCLEQ_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.cqe_next = (head)->cqh_first;			\
	(elm)->field.cqe_prev = (void *)(head);				\
	if ((head)->cqh_last == (void *)(head))				\
		(head)->cqh_last = (elm);				\
	else								\
		(head)->cqh_first->field.cqe_prev = (elm);		\
	(head)->cqh_first = (elm);					\
} while (0)

#define	D_CIRCLEQ_INSERT_TAIL(head, elm, field) do {			\
	(elm)->field.cqe_next = (void *)(head);				\
	(elm)->field.cqe_prev = (head)->cqh_last;			\
	if ((head)->cqh_first == (void *)(head))			\
		(head)->cqh_first = (elm);				\
	else								\
		(head)->cqh_last->field.cqe_next = (elm);		\
	(head)->cqh_last = (elm);					\
} while (0)

#define	D_CIRCLEQ_REMOVE(head, elm, field) do {				\
	if ((elm)->field.cqe_next == (void *)(head))			\
		(head)->cqh_last = (elm)->field.cqe_prev;		\
	else								\
		(elm)->field.cqe_next->field.cqe_prev =			\
		    (elm)->field.cqe_prev;				\
	if ((elm)->field.cqe_prev == (void *)(head))			\
		(head)->cqh_first = (elm)->field.cqe_next;		\
	else								\
		(elm)->field.cqe_prev->field.cqe_next =			\
		    (elm)->field.cqe_next;				\
} while (0)

/*
 * The macro CIRCLEQ_FOREACH() traverses the circle queue referenced by head
 * in the forward direction, assigning each element in turn to var. Each
 * element is assigned exactly once.
 */
#define	D_CIRCLEQ_FOREACH(var, head, field)				\
	for ((var) = ((head)->cqh_first);				\
		(var) != (const void *)(head);				\
		(var) = ((var)->field.cqe_next))

/*
 * The macro CIRCLEQ_FOREACH_REVERSE() traverses the circle queue referenced
 * by head in the reverse direction, assigning each element in turn to var.
 * Each element is assigned exactly once.
 */
#define	D_CIRCLEQ_FOREACH_REVERSE(var, head, field)			\
	for ((var) = ((head)->cqh_last);				\
		(var) != (const void *)(head);				\
		(var) = ((var)->field.cqe_prev))

/*
 * Circular queue access methods.
 */
/*
 * The macro CIRCLEQ_EMPTY() return true if the circular queue head has no
 * elements.
 */
#define	D_CIRCLEQ_EMPTY(head)		((head)->cqh_first == (void *)(head))
/*
 * The macro CIRCLEQ_FIRST() returns the first element of the circular queue
 * head.
 */
#define	D_CIRCLEQ_FIRST(head)		((head)->cqh_first)
/*
 * The macro CIRCLEQ_LAST() returns the last element of the circular queue head.
 */
#define	D_CIRCLEQ_LAST(head)		((head)->cqh_last)
/*
 * The macro CIRCLEQ_NEXT() returns the element after the element elm.
 */
#define	D_CIRCLEQ_NEXT(elm, field)	((elm)->field.cqe_next)
/*
 * The macro CIRCLEQ_PREV() returns the element before the element elm.
 */
#define	D_CIRCLEQ_PREV(elm, field)	((elm)->field.cqe_prev)

/*
 * The macro CIRCLEQ_LOOP_NEXT() returns the element after the element elm.
 * If elm was the last element in the queue, the first element is returned.
 */
#define D_CIRCLEQ_LOOP_NEXT(head, elm, field)				\
	(((elm)->field.cqe_next == (void *)(head))			\
	    ? ((head)->cqh_first)					\
	    : (elm->field.cqe_next))
/*
 * The macro CIRCLEQ_LOOP_PREV() returns the element before the element elm.
 * If elm was the first element in the queue, the last element is returned.
 */
#define D_CIRCLEQ_LOOP_PREV(head, elm, field)				\
	(((elm)->field.cqe_prev == (void *)(head))			\
	    ? ((head)->cqh_last)					\
	    : (elm->field.cqe_prev))

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_LIST_H__ */
