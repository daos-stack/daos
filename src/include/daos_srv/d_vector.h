/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __D_VECTOR_H__
#define __D_VECTOR_H__

#include <daos/mem.h>
#include <gurt/list.h>

/**
 * @defgroup d_vector
 * @{
 */

struct d_vector {
	d_list_t dv_list;
	size_t   dv_entry_size;
	uint32_t dv_segment_capacity;
};

#define D_VECTOR_SEGMENT_SIZE 4096

#define D_VECTOR_SEGMENT_HEADER                                                                    \
	struct {                                                                                   \
		d_list_t dvs_link;                                                                 \
		uint32_t dvs_len;                                                                  \
		uint32_t dvs_capacity;                                                             \
		size_t   dvs_entry_size;                                                           \
	}

#define D_VECTOR_SEGMENT_RAW_CAPACITY (D_VECTOR_SEGMENT_SIZE - sizeof(D_VECTOR_SEGMENT_HEADER))

struct d_vector_segment {
	D_VECTOR_SEGMENT_HEADER;
	char dvs_entries[D_VECTOR_SEGMENT_RAW_CAPACITY];
};

D_CASSERT(sizeof(struct d_vector_segment) == D_VECTOR_SEGMENT_SIZE);

typedef struct d_vector         d_vector_t;
typedef struct d_vector_segment d_vector_segment_t;

static inline int
d_vector_segment_is_full(d_vector_segment_t *dvs)
{
	return dvs->dvs_len == dvs->dvs_capacity;
}

static inline d_vector_segment_t *
d_vector_segment_alloc(d_vector_t *dv)
{
	d_vector_segment_t *dvs;
	D_ALLOC_PTR(dvs);
	if (dvs == NULL) {
		return NULL;
	}
	dvs->dvs_entry_size = dv->dv_entry_size;
	dvs->dvs_capacity   = dv->dv_segment_capacity;
	return dvs;
}

static inline void *
d_vector_segment_entry(d_vector_segment_t *dvs, uint32_t idx)
{
	D_ASSERT(idx < dvs->dvs_capacity);
	return &dvs->dvs_entries[dvs->dvs_entry_size * idx];
}

static inline void
d_vector_segment_append(d_vector_segment_t *dvs, void *entry)
{
	D_ASSERT(!d_vector_segment_is_full(dvs));
	memcpy(d_vector_segment_entry(dvs, dvs->dvs_len), entry, dvs->dvs_entry_size);
	dvs->dvs_len += 1;
}

/**
 * Append \p src to \p dst.
 *
 * \param[in,out]	dst	The vector the \p src element append to.
 * \param[in]		src	Address of the element to append.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_INVAL	\p dst or \p src are NULL.
 * \retval -DER_NOMEM	Run out of memory.
 */
static inline int
d_vector_append(d_vector_t *dst, void *src)
{
	d_vector_segment_t *dvs;
	bool                new_segment = false;

	if (dst == NULL || src == NULL) {
		return -DER_INVAL;
	}

	if (d_list_empty(&dst->dv_list)) {
		new_segment = true;
	} else {
		/** get the current tail */
		dvs = d_list_entry(dst->dv_list.prev, d_vector_segment_t, dvs_link);
		if (d_vector_segment_is_full(dvs)) {
			new_segment = true;
		}
	}

	if (new_segment) {
		dvs = d_vector_segment_alloc(dst);
		if (dvs == NULL) {
			return -DER_NOMEM;
		}

		d_list_add_tail(&dvs->dvs_link, &dst->dv_list);
	}

	d_vector_segment_append(dvs, src);

	return DER_SUCCESS;
}

static inline void
d_vector_init(size_t entry_size, d_vector_t *dv)
{
	memset(dv, 0, sizeof(d_vector_t));
	dv->dv_entry_size       = entry_size;
	dv->dv_segment_capacity = D_VECTOR_SEGMENT_RAW_CAPACITY / entry_size;
	D_INIT_LIST_HEAD(&dv->dv_list);
}

static inline void
d_vector_move(d_vector_t *dst, d_vector_t *src)
{
	dst->dv_list.next       = src->dv_list.next;
	src->dv_list.next->prev = &dst->dv_list;
	dst->dv_list.prev       = src->dv_list.prev;
	src->dv_list.prev->next = &dst->dv_list;
	D_INIT_LIST_HEAD(&src->dv_list);
}

static inline void
d_vector_free(d_vector_t *dv)
{
	d_vector_segment_t *pos;
	d_vector_segment_t *next;
	d_list_for_each_entry_safe(pos, next, &dv->dv_list, dvs_link) {
		d_list_del(&pos->dvs_link);
		D_FREE(pos);
	}
}

static inline uint32_t
d_vector_size(d_vector_t *dv)
{
	d_vector_segment_t *pos;
	uint32_t            size = 0;

	d_list_for_each_entry(pos, &dv->dv_list, dvs_link) {
		size += pos->dvs_len;
	}

	return size;
}

static inline void
_d_vector_foreach_init(void **entry, d_vector_segment_t **segment, uint32_t *idx, d_list_t *head)
{
	if (d_list_empty(head)) {
		*segment = NULL;
		*idx     = 0;
		*entry   = NULL;
	} else {
		*segment = d_list_entry((head)->next, __typeof__(**segment), dvs_link);
		prefetch((*segment)->dvs_link.next);
		*idx   = 0;
		*entry = d_vector_segment_entry(*segment, *idx);
	}
}

static inline void
_d_vector_foreach_next(void **entry, d_vector_segment_t **segment, uint32_t *idx, d_list_t *head)
{
	if (*idx + 1 < (*segment)->dvs_len) { /** there is another entry in the current segment */
		*idx += 1;
	} else { /** look for the next segment */
		d_list_t *next = (*segment)->dvs_link.next;
		if (next != head) { /** the next segment exists */
			(*segment) = d_list_entry(next, __typeof__(**segment), dvs_link);
			prefetch((*segment)->dvs_link.next);
			*idx = 0;
		} else { /** there are no more segments */
			*segment = NULL;
			*idx     = 0;
			*entry   = NULL;
		}
	}

	if (*segment != NULL) {
		*entry = d_vector_segment_entry(*segment, *idx);
	}
}

#define d_vector_for_each_entry(entry, segment, idx, head)                                         \
	for (_d_vector_foreach_init((void **)&entry, &segment, &idx, head); segment != NULL;       \
	     _d_vector_foreach_next((void **)&entry, &segment, &idx, head))

/**
 * @}
 * end of the d_vector group
 */

#endif /* __D_VECTOR_H__ */
