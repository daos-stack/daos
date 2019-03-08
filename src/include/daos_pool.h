/*
 * (C) Copyright 2015-2019 Intel Corporation.
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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * \file
 *
 * DAOS Pool API methods
 */

#ifndef __DAOS_POOL_H__
#define __DAOS_POOL_H__

#include <daos_types.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Connect to the DAOS pool identified by UUID \a uuid. Upon a successful
 * completion, \a poh returns the pool handle, and \a info returns the latest
 * pool information.
 *
 * \param[in]	uuid	UUID to identify a pool.
 * \param[in]	grp	Process set name of the DAOS servers managing the pool
 * \param[in]	svc	Pool service replica ranks, as reported by
 *			daos_pool_create().
 * \param[in]	flags	Connect mode represented by the DAOS_PC_ bits.
 * \param[out]	poh	Returned open handle.
 * \param[out]	info	Returned pool info.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
daos_pool_connect(const uuid_t uuid, const char *grp,
		  const d_rank_list_t *svc, unsigned int flags,
		  daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);

/**
 * Disconnect from the DAOS pool. It should revoke all the container open
 * handles of this pool.
 *
 * \param[in]	poh	Pool connection handle
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_disconnect(daos_handle_t poh, daos_event_t *ev);

/**
 * Convert a local pool connection to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	poh	Valid local pool connection handle to be shared
 * \param[out]	glob	Pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool  handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, a larger
 *					buffer is required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
daos_pool_local2global(daos_handle_t poh, daos_iov_t *glob);

/**
 * Create a local pool connection for global representation data.
 *
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	poh	Returned local pool connection handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 */
int
daos_pool_global2local(daos_iov_t glob, daos_handle_t *poh);

/**
 * Query pool information. User should provide at least one of \a info and
 * \a tgts as output buffer.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[out]	tgts	Optional, returned storage targets in this pool.
 * \param[out]	info	Optional, returned pool information.
 * \param[out]	pool_prop
 *			Optional, returned pool properties.
 *			If it is NULL, then needs not query the properties.
 *			If pool_prop is non-NULL but its dpp_entries is NULL,
 *			will query all pool properties, DAOS internally
 *			allocates the needed buffers and assign pointer to
 *			dpp_entries.
 *			If pool_prop's dpp_nr > 0 and dpp_entries is non-NULL,
 *			will query the properties for specific dpe_type(s), DAOS
 *			internally allocates the needed buffer for dpe_str or
 *			dpe_val_ptr, if the dpe_type with immediate value then
 *			will directly assign it to dpe_val.
 *			User can free the associated buffer by calling
 *			daos_prop_free().
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_query(daos_handle_t poh, d_rank_list_t *tgts, daos_pool_info_t *info,
		daos_prop_t *pool_prop, daos_event_t *ev);

/**
 * Query information of storage targets within a DAOS pool.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	tgts	A list of targets to query.
 * \param[out]	failed	Optional, buffer to store faulty targets on failure.
 * \param[out]	info_list
 *			Returned storage information of \a tgts, it is an array
 *			and array size must equal to tgts::rl_llen.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	No pool on specified targets
 */
int
daos_pool_query_target(daos_handle_t poh, d_rank_list_t *tgts,
		       d_rank_list_t *failed, daos_target_info_t *info_list,
		       daos_event_t *ev);

/**
 * List the names of all user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle.
 * \param[out]	buffer	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param[in,out]
 *		size	[in]: Buffer size. [out]: Aggregate size of all
 *			attribute names (excluding terminating null
 *			characters), regardless of the actual buffer
 *			size.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_list_attr(daos_handle_t poh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined pool attribute values.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[out]	buffers	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding buffer sizes
 *			will be truncated. NULL values are permitted and will be
 *			treated identical to zero-length buffers, in which case
 *			only the sizes of attribute values will be retrieved.
 * \param[in,out]
 *		sizes	[in]: Array of \a n buffer sizes. [out]: Array of actual
 *			sizes of \a n attribute values, regardless of given
 *			buffer sizes.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_get_attr(daos_handle_t poh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[in]	values	Array of \a n attribute values
 * \param[in]	sizes	Array of \a n elements containing the sizes of
 *			respective attribute values.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_set_attr(daos_handle_t poh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
		   daos_event_t *ev);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_POOL_H__ */
