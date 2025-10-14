/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_PRINT__
#define __DLCK_PRINT__

#include <stdio.h>

#include <daos/dlck.h>

#define DLCK_PRINT_MAIN_MAGIC 0x17A28DC5626110A5

/**
 * \struct dlck_print_main
 *
 * Custom payload of the main print utility.
 */
struct dlck_print_main {
	uint64_t  magic;
	FILE     *stream;
	ABT_mutex stream_mutex;
};

#define DLCK_PRINT_MAIN_LOCK_FAIL_FMT                                                              \
	"Failed to lock the stream's synchronization mutex: " DF_RC "\n"
#define DLCK_PRINT_MAIN_UNLOCK_FAIL_FMT                                                            \
	"Failed to unlock the stream's synchronization mutex: " DF_RC "\n"

/**
 * \brief Init the main print utility.
 *
 * Prints to stdout and it is guarded by a mutex.
 *
 * \param[out]	dp	Initialized utility.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
int
dlck_print_main_init(struct dlck_print *dp);

/**
 * Finalize the main print utility.
 *
 * \param[in]	dp	Utility to finalize.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	An error.
 */
int
dlck_print_main_fini(struct dlck_print *dp);

/**
 * Get the custom payload from the main print utility.
 *
 * \note Only for advance use-cases. Please see DLCK_PRINT*() macros first.
 *
 * \param[in]   dp      Print utility (only the main one will work).
 *
 * \return The custom payload.
 */
static inline struct dlck_print_main *
dlck_print_main_get_custom(struct dlck_print *dp)
{
	struct dlck_print_main *dpm = dp->printf_custom;
	D_ASSERT(dpm->magic == DLCK_PRINT_MAIN_MAGIC);
	return dpm;
}

/**
 * \brief Init a worker print utility.
 *
 * Creates and opens a logfile. The created printer utility will direct log into the created file.
 *
 * \param[in]	log_dir	Directory where a logfile will be created.
 * \param[in]	po_uuid	Pool's UUID.
 * \param[in]	tgt_id	Target's ID.
 * \param[in]	main_dp	Main print utility. To report errors when they occur.
 * \param[out]	dp	Created print utility.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other error.
 */
int
dlck_print_worker_init(const char *log_dir, uuid_t po_uuid, int tgt_id, struct dlck_print *main_dp,
		       struct dlck_print *dp);

/**
 * \brief Finalize the worker print utility.
 *
 * \note The worker output stream will be closed.
 *
 * \param[in]	dp	Utility to finalize.
 */
void
dlck_print_worker_fini(struct dlck_print *dp);

#endif /** __DLCK_PRINT__ */
