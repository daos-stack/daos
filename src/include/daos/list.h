/**
 * (C) Copyright 2015, 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __DAOS_LIST_H__
#define __DAOS_LIST_H__

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

struct daos_list_head {
	struct daos_list_head *next, *prev;
};

typedef struct daos_list_head daos_list_t;

#define DAOS_LIST_HEAD_INIT(name) { &(name), &(name) }

#define DAOS_LIST_HEAD(name) \
	daos_list_t name = DAOS_LIST_HEAD_INIT(name)

#define DAOS_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/**
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void
__daos_list_add(daos_list_t *newe, daos_list_t *prev, daos_list_t *next)
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
daos_list_add(daos_list_t *newe, daos_list_t *head)
{
	__daos_list_add(newe, head, head->next);
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
daos_list_add_tail(daos_list_t *newe, daos_list_t *head)
{
	__daos_list_add(newe, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void
__daos_list_del(daos_list_t *prev, daos_list_t *next)
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
daos_list_del(daos_list_t *entry)
{
	__daos_list_del(entry->prev, entry->next);
}

/**
 * Remove an entry from the list it is currently in and reinitialize it.
 * \param entry the entry to remove.
 */
static inline void
daos_list_del_init(daos_list_t *entry)
{
	__daos_list_del(entry->prev, entry->next);
	DAOS_INIT_LIST_HEAD(entry);
}

/**
 * Remove an entry from the list it is currently in and insert it at the start
 * of another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void
daos_list_move(daos_list_t *list, daos_list_t *head)
{
	__daos_list_del(list->prev, list->next);
	daos_list_add(list, head);
}

/**
 * Remove an entry from the list it is currently in and insert it at the end of
 * another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void
daos_list_move_tail(daos_list_t *list, daos_list_t *head)
{
	__daos_list_del(list->prev, list->next);
	daos_list_add_tail(list, head);
}

/**
 * Test whether a list is empty
 * \param head the list to test.
 */
static inline int
daos_list_empty(daos_list_t *head)
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
 * NOTE: using daos_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is daos_list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int
daos_list_empty_careful(const daos_list_t *head)
{
	daos_list_t *next = head->next;
	return (next == head) && (next == head->prev);
}

static inline void
__daos_list_splice(daos_list_t *list, daos_list_t *head)
{
	daos_list_t *first = list->next;
	daos_list_t *last = list->prev;
	daos_list_t *at = head->next;

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
daos_list_splice(daos_list_t *list, daos_list_t *head)
{
	if (!daos_list_empty(list))
		__daos_list_splice(list, head);
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
daos_list_splice_init(daos_list_t *list, daos_list_t *head)
{
	if (!daos_list_empty(list)) {
		__daos_list_splice(list, head);
		DAOS_INIT_LIST_HEAD(list);
	}
}

/**
 * Get the container of a list
 * \param ptr	 the embedded list.
 * \param type	 the type of the struct this is embedded in.
 * \param member the member name of the list within the struct.
 */
#define daos_list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

/**
 * Iterate over a list
 * \param pos	the iterator
 * \param head	the list to iterate over
 *
 * Behaviour is undefined if \a pos is removed from the list in the body of the
 * loop.
 */
#define daos_list_for_each(pos, head) \
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
#define daos_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * Iterate over a list continuing after existing point
 * \param pos    the type * to use as a loop counter
 * \param head   the list head
 * \param member the name of the list_struct within the struct
 */
#define daos_list_for_each_entry_continue(pos, head, member)                 \
	for (pos = daos_list_entry(pos->member.next, __typeof__(*pos), member);\
	     prefetch(pos->member.next), &pos->member != (head);             \
	     pos = daos_list_entry(pos->member.next, __typeof__(*pos), member))

/**
 * \defgroup hlist Hash List
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is too
 * wasteful.  You lose the ability to access the tail in O(1).
 * @{
 */

typedef struct daos_hlist_node {
	struct daos_hlist_node *next, **pprev;
} daos_hlist_node_t;

typedef struct daos_hlist_head {
	daos_hlist_node_t *first;
} daos_hlist_head_t;

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

#define DAOS_HLIST_HEAD_INIT { NULL_P }
#define DAOS_HLIST_HEAD(name) daos_hlist_head_t name = { NULL_P }
#define DAOS_INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL_P)
#define DAOS_INIT_HLIST_NODE(ptr) ((ptr)->next = NULL_P, (ptr)->pprev = NULL_P)

static inline int
daos_hlist_unhashed(const daos_hlist_node_t *h)
{
	return !h->pprev;
}

static inline int
daos_hlist_empty(const daos_hlist_head_t *h)
{
	return !h->first;
}

static inline void
__daos_hlist_del(daos_hlist_node_t *n)
{
	daos_hlist_node_t *next = n->next;
	daos_hlist_node_t **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline void
daos_hlist_del(daos_hlist_node_t *n)
{
	__daos_hlist_del(n);
}

static inline void
daos_hlist_del_init(daos_hlist_node_t *n)
{
	if (n->pprev)  {
		__daos_hlist_del(n);
		DAOS_INIT_HLIST_NODE(n);
	}
}

static inline void
daos_hlist_add_head(daos_hlist_node_t *n, daos_hlist_head_t *h)
{
	daos_hlist_node_t *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

/* next must be != NULL */
static inline void
daos_hlist_add_before(daos_hlist_node_t *n, daos_hlist_node_t *next)
{
	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

static inline void
daos_hlist_add_after(daos_hlist_node_t *n, daos_hlist_node_t *next)
{
	next->next = n->next;
	n->next = next;
	next->pprev = &n->next;

	if(next->next)
		next->next->pprev  = &next->next;
}

#define daos_hlist_entry(ptr, type, member) container_of(ptr,type,member)

#define daos_hlist_for_each(pos, head) \
	for (pos = (head)->first; pos && (prefetch(pos->next), 1); \
	     pos = pos->next)

#define daos_hlist_for_each_safe(pos, n, head) \
	for (pos = (head)->first; pos && (n = pos->next, 1); \
	     pos = n)

/**
 * Iterate over an hlist of given type
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param head	 the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define daos_hlist_for_each_entry(tpos, pos, head, member)                   \
	for (pos = (head)->first;                                            \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = daos_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing after existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define daos_hlist_for_each_entry_continue(tpos, pos, member)                \
	for (pos = (pos)->next;                                              \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = daos_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing from an existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define daos_hlist_for_each_entry_from(tpos, pos, member)		     \
	for (; pos && ({ prefetch(pos->next); 1;}) &&                        \
		({ tpos = daos_hlist_entry(pos, __typeof__(*tpos), member);  \
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
#define daos_hlist_for_each_entry_safe(tpos, pos, n, head, member)           \
	for (pos = (head)->first;                                            \
	     pos && ({ n = pos->next; 1; }) &&                               \
		({ tpos = daos_hlist_entry(pos, __typeof__(*tpos), member);  \
		   1; });                                                    \
	     pos = n)

/* @} */

#ifndef daos_list_for_each_prev
/**
 * Iterate over a list in reverse order
 * \param pos	the &struct list_head to use as a loop counter.
 * \param head	the head for your list.
 */
#define daos_list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head);     \
		pos = pos->prev, prefetch(pos->prev))

#endif /* daos_list_for_each_prev */

#ifndef daos_list_for_each_entry
/**
 * Iterate over a list of given type
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define daos_list_for_each_entry(pos, head, member)                          \
	for (pos = daos_list_entry((head)->next, __typeof__(*pos), member),  \
		     prefetch(pos->member.next);                             \
	     &pos->member != (head);                                         \
	     pos = daos_list_entry(pos->member.next, __typeof__(*pos), member),\
	     prefetch(pos->member.next))
#endif /* daos_list_for_each_entry */

#ifndef daos_list_for_each_entry_rcu
#define daos_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef daos_list_for_each_entry_rcu
#define daos_list_for_each_entry_rcu(pos, head, member) \
	list_for_each_entry(pos, head, member)
#endif

#ifndef daos_list_for_each_entry_reverse
/**
 * Iterate backwards over a list of given type.
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define daos_list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = daos_list_entry((head)->prev, __typeof__(*pos), member);  \
	     prefetch(pos->member.prev), &pos->member != (head);             \
	     pos = daos_list_entry(pos->member.prev, __typeof__(*pos), member))
#endif /* daos_list_for_each_entry_reverse */

#ifndef daos_list_for_each_entry_safe
/**
 * Iterate over a list of given type safe against removal of list entry
 * \param pos        the type * to use as a loop counter.
 * \param n          another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define daos_list_for_each_entry_safe(pos, n, head, member)                   \
	for (pos = daos_list_entry((head)->next, __typeof__(*pos), member),   \
		n = daos_list_entry(pos->member.next, __typeof__(*pos),       \
				    member);                                  \
	     &pos->member != (head);                                          \
	     pos = n, n = daos_list_entry(n->member.next, __typeof__(*n),     \
					  member))

#endif /* daos_list_for_each_entry_safe */

#ifndef daos_list_for_each_entry_safe_from
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
#define daos_list_for_each_entry_safe_from(pos, n, head, member)             \
	for (n = daos_list_entry(pos->member.next, __typeof__(*pos), member);\
	     &pos->member != (head);                                         \
	     pos = n, n = daos_list_entry(n->member.next, __typeof__(*n),    \
					  member))
#endif /* daos_list_for_each_entry_safe_from */

#define daos_list_for_each_entry_typed(pos, head, type, member)		\
	for (pos = daos_list_entry((head)->next, type, member),		\
	     prefetch(pos->member.next);				\
	     &pos->member != (head);                                    \
	     pos = daos_list_entry(pos->member.next, type, member),	\
	     prefetch(pos->member.next))

#define daos_list_for_each_entry_reverse_typed(pos, head, type, member)	\
	for (pos = daos_list_entry((head)->prev, type, member);		\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = daos_list_entry(pos->member.prev, type, member))

#define daos_list_for_each_entry_safe_typed(pos, n, head, type, member)	\
	for (pos = daos_list_entry((head)->next, type, member),		\
	     n = daos_list_entry(pos->member.next, type, member);	\
	     &pos->member != (head);                                    \
	     pos = n, n = daos_list_entry(n->member.next, type, member))

#define daos_list_for_each_entry_safe_from_typed(pos, n, head, type, member)  \
	for (n = daos_list_entry(pos->member.next, type, member);             \
	     &pos->member != (head);                                          \
	     pos = n, n = daos_list_entry(n->member.next, type, member))

#define daos_hlist_for_each_entry_typed(tpos, pos, head, type, member)   \
	for (pos = (head)->first;                                        \
	     pos && (prefetch(pos->next), 1) &&                          \
		(tpos = daos_hlist_entry(pos, type, member), 1);         \
	     pos = pos->next)

#define daos_hlist_for_each_entry_safe_typed(tpos, pos, n, head, type, member) \
	for (pos = (head)->first;                                              \
	     pos && (n = pos->next, 1) &&                                      \
		(tpos = daos_hlist_entry(pos, type, member), 1);               \
	     pos = n)

#endif /* __DAOS_LIST_H__ */
