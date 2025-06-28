/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dlck)

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/common.h>
#include <daos_srv/dlck.h>

void
dlck_array_init(size_t entry_size, unsigned grow_by, struct dlck_array *da)
{
	assert(entry_size % sizeof(char) == 0);
	da->da_entry_size = entry_size / sizeof(char);
	da->da_grow_by    = grow_by;
}

void *
dlck_array_entry(struct dlck_array *da, uint32_t idx)
{
	assert(idx < da->da_max_len);
	return &da->da_entries[da->da_entry_size * idx];
}

static inline size_t
entry_size(struct dlck_array *da)
{
	return sizeof(char) * da->da_entry_size;
}

int
dlck_array_append(struct dlck_array *da, void *entry)
{
	char    *newptr = NULL;
	uint32_t count;

	/** Array has to grow. */
	if (da->da_len == da->da_max_len) {
		count = da->da_max_len + da->da_grow_by;
		D_REALLOC_COMMON(newptr, da->da_entries, entry_size(da) * da->da_max_len,
				 entry_size(da), count);
		if (newptr == NULL) {
			return -DER_NOMEM;
		}
		da->da_max_len = count;
		da->da_entries = newptr;
	}

	/** Append the new record. */
	memcpy(dlck_array_entry(da, da->da_len), entry, entry_size(da));
	da->da_len += 1;

	return DER_SUCCESS;
}

void
dlck_array_move(struct dlck_array *dst, struct dlck_array *src)
{
	D_FREE(dst->da_entries);

	/** Copy/move everything from the source. */
	dst->da_entries = src->da_entries;
	dst->da_len     = src->da_len;
	dst->da_max_len = src->da_max_len;

	/** Reset the source. */
	src->da_entries = NULL;
	src->da_len     = 0;
	src->da_max_len = 0;
}

void
dlck_array_free(struct dlck_array *da)
{
	/** Free the resources. */
	D_FREE(da->da_entries);

	/** Reset. */
	da->da_entries = NULL;
	da->da_len     = 0;
	da->da_max_len = 0;
}
