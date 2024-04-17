/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DTS_UTILS_H__
#define __DTS_UTILS_H__

#include <stddef.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos/object.h>

#include "vts_io.h"
#include "dtx_internal.h"

#define DKEY_ID0 0
#define DKEY_ID1 1
#define DKEY_ID2 2
#define DKEY_ID3 3

#define DKEY_NUM 4

struct dts_local_args {
	daos_unit_oid_t oid;

	char            dkey_buf[DKEY_NUM][UPDATE_DKEY_SIZE];
	daos_key_t      dkey[DKEY_NUM];

	char            akey_buf[UPDATE_AKEY_SIZE];
	daos_key_t      akey;

	daos_iod_t      iod;

	d_sg_list_t     sgl;
	d_sg_list_t     fetch_sgl;

	daos_epoch_t    epoch;
};

#define BUF_SIZE            32

#define UPDATE_FORMAT       "- update at DKEY[%u] epoch=%" PRIu64 " (rc=%d)"

#define FMT_INDENT          "             "

#define DTS_PRINT(FMT, ...) print_message(FMT_INDENT FMT "\n", ##__VA_ARGS__)

void
dts_global_init();

void
dts_print_start_message(void);

struct dtx_handle *
dts_local_begin(daos_handle_t poh, uint16_t sub_modification_cnt);

void
dts_local_commit(struct dtx_handle *dth);

void
dts_local_abort(struct dtx_handle *dth);

void
dts_update(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id, const char *value,
	   struct dtx_handle *dth);

void
dts_punch_dkey(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
	       struct dtx_handle *dth);

void
dts_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch, daos_key_t *dkey,
	  daos_iod_t *iod, d_sg_list_t *sgl);

#define EXISTING     true
#define NON_EXISTING false

/**
 * Validate the fetch results.
 *
 * \param[in] exp_buf	The expected contents of the buffer after fetch
 * \param[in] existing	The fetched value was expected to exist (true) or not exist (false)
 */
void
dts_validate(daos_iod_t *iod, d_sg_list_t *sgl, const char *exp_buf, bool existing);

/**
 * Fetch and validate the result.
 *
 * It is intended to be used via DTS_FETCH_*() macros.
 */
void
_dts_fetch_and_validate(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
			const char *exp_buf, bool existing, const char *msg);

#define FETCH_EXISTING_STR     "fetch existing value(s)"
#define FETCH_NON_EXISTING_STR "fetch non-existing value(s)"

#define DTS_FETCH_EXISTING(coh, la, dkey_id, exp_buf)                                              \
	_dts_fetch_and_validate((coh), (la), (dkey_id), exp_buf, EXISTING, FETCH_EXISTING_STR)

#define DTS_FETCH_NON_EXISTING(coh, la, dkey_id)                                                   \
	_dts_fetch_and_validate((coh), (la), (dkey_id), NULL, NON_EXISTING, FETCH_NON_EXISTING_STR)

/** Setup and teardown functions */

int
setup_local_args(void **state);

int
teardown_local_args(void **state);

#define BASIC_UT(NO, NAME, FUNC)                                                                   \
	{                                                                                          \
		"DTX" #NO ": " NAME, FUNC, setup_local_args, teardown_local_args                   \
	}

#endif /* __DTS_UTILS_H__ */
