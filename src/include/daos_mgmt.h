/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS Management API.
 */

#ifndef __DAOS_MGMT_H__
#define __DAOS_MGMT_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <uuid/uuid.h>

#include <daos_event.h>
#include <daos_types.h>
#include <daos_pool.h>

/*
 * DAOS management pool information
 */
typedef struct {
	/* TODO? same pool info structure as a pool query?
	 * requires back-end RPC to each pool service.
	 * daos_pool_info_t		 mgpi_info;
	 */
	uuid_t				 mgpi_uuid;
	/** List of current pool service replica ranks */
	d_rank_list_t			*mgpi_svc;
} daos_mgmt_pool_info_t;

/**
 * Stop the current pool service leader.
 *
 * \param[in] poh	Pool connection handle
 * \param[in] ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_stop_svc(daos_handle_t poh, daos_event_t *ev);

/**
 * The operation code for DAOS client to set different parameters globally
 * on all servers.
 */
enum {
	DMG_KEY_FAIL_LOC	 = 0,
	DMG_KEY_FAIL_VALUE,
	DMG_KEY_FAIL_NUM,
	DMG_KEY_NUM,
};

/**
 * Set parameter on servers.
 *
 * \param[in] grp	Process set name of the DAOS servers managing the pool
 * \param[in] rank	Ranks to set parameter. -1 means setting on all servers.
 * \param[in] key_id	key ID of the parameter.
 * \param[in] value	value of the parameter.
 * \param[in] value_extra
 *			optional extra value to set the fail value when
 *			\a key_id is DMG_CMD_FAIL_LOC and \a value is in
 *			DAOS_FAIL_VALUE mode.
 * \param[in] ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_debug_set_params(const char *grp, d_rank_t rank, unsigned int key_id,
		      uint64_t value, uint64_t value_extra, daos_event_t *ev);

/**
 * Add mark to servers.
 *
 * \param[in] mark	mark to add to the debug log.
 */
int
daos_debug_add_mark(const char *mark);

/**
 * Query internal blobstore state for given blobstore uuid in the specified
 * DAOS system.
 *
 * \param[in] group		Name of DAOS system managing the service.
 * \param[in] blobstore_uuid	UUID of the blobstore to query.
 * \param[out] blobstore_state	Will return an enum integer that will
 *				later be converted to a blobstore state:
 *				SETUP, NORMAL, FAULTY, TEARDOWN, or OUT
 * \param[in] ev		Completion event. Optional and can be NULL.
 *				The function will run in blocking mode
 *				if \a ev is NULL.
 *
 * \return			0		Success
 *
 */
int
daos_mgmt_get_bs_state(const char *group, uuid_t blobstore_uuid,
		       int *blobstore_state, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_MGMT_H__ */
