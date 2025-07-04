/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DLCK_H__
#define __DAOS_DLCK_H__

#include <daos/mem.h>

/**
 * Trace a single DTX record.
 */
struct dlck_dtx_rec {
	uint32_t   lid;
	umem_off_t umoff;
};

/**
 * Execution statistics.
 */
struct dlck_stats {
	unsigned touched;
};

/**
 * @defgroup dlck_array
 * @{
 */

/**
 * Array able to grow as necessary.
 */
struct dlck_array {
	uint32_t da_len;        /** Current length of the array. */
	uint32_t da_max_len;    /** Allocated length of the array. */
	size_t   da_entry_size; /** Size of a single entry. */
	unsigned da_grow_by;    /** The number of entries by which to expand the array. */
	char    *da_entries;    /** Entries of the array */
};

/**
 * Initialize the array.
 *
 * \param[in]		entry_size	Size of a single entry.
 * \param[in]		grow_by		The number of entries by which to expand the array.
 * \param[in,out]	da	Array to which the record will be appended.
 */
void
dlck_array_init(size_t entry_size, unsigned grow_by, struct dlck_array *da);

/**
 * Get an array entry by index.
 *
 * \param[in]		da	Array.
 * \param[in]		idx	Index of the demanded entry.
 *
 * \return A pointer to the specified entry.
 */
void *
dlck_array_entry(struct dlck_array *da, uint32_t idx);

/**
 * Append an entry to the array.
 *
 * \param[in,out]	da	Array to which the entry will be appended.
 * \param[in]		entry	Entry to append.
 *
 * \retval 0		Success.
 * \retval -DER_NOMEM	Cannot resize the array to accommodate the new entry.
 */
int
dlck_array_append(struct dlck_array *da, void *entry);

/**
 * Move records from \p src to \p dst. \p src is left empty.
 *
 * \param[in,out]	dst	Destination array.
 * \param[in,out]	src	Source array.
 */
void
dlck_array_move(struct dlck_array *dst, struct dlck_array *src);

/**
 * Release the attached resources. \p da is left empty.
 *
 * \param[in,out]	da	Array to process.
 */
void
dlck_array_free(struct dlck_array *da);

/**
 * @}
 * end of the dlck_array group
 */

/**
 * \brief Collect the records for active DTX entries.
 *
 * Scan the entire provided \p coh container for records referencing active DTX entries.
 *
 * \param[in]	coh	The container to process.
 * \param[out]	da	Array of active DAE records.
 *
 * \retval 0		Success.
 * \retval -DER_*	Error.
 */
int
dlck_vos_cont_rec_get_active(daos_handle_t coh, struct dlck_array *da, struct dlck_stats *ds);

/**
 * \brief Remove records from all active DTX entries.
 *
 * This process is intended for catastrophic recovery in case the records of active DTX entries have
 * been corrupted.
 *
 * \param[in]	coh	Parent container.
 *
 * \retval 0		Success.
 * \retval -DER_*	The transaction has failed.
 */
int
dlck_dtx_act_recs_remove(daos_handle_t coh);

/**
 * \brief Set active DTX entries' records as provided by \p dda.
 *
 * This process assumes the exising DAE records have been first removed. Please find a relevant API
 * call to do it for you. This process is intended for catastrophic recovery in case the records of
 * DAEs have been corrupted.
 *
 *
 * \param[in]	coh	Parent container.
 * \param[in]	dda	Array of active DAE records.
 *
 * \retval 0			Success.
 * \retval -DER_NOTSUPPORTED	DAE records are not removed.
 * \retval -DER_NOMEM		Run out of memory.
 * \retval -DER_*		The transaction has failed.
 */
int
dlck_dtx_act_recs_set(daos_handle_t coh, struct dlck_array *da);

/** DLCK callbacks */

typedef bool (*DLCK_ask_yes_no)(const char *);

struct DLCK_callbacks {
	DLCK_ask_yes_no dc_ask_yes_no;
};

extern struct DLCK_callbacks *DLCK_Callbacks;

#endif /* __DAOS_DLCK_H__ */
