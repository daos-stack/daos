/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * DAOS flat Key-Value Store
 *
 * The KV API simplify the DAOS 2 key-level object into a simple KV interface
 * for users who are just interested in a traditional KV store API.
 */

#ifndef __DAOS_KV_H__
#define __DAOS_KV_H__

#if defined(__cplusplus)
extern "C" {
#endif

/* Conditional Op: Insert key if it doesn't exist, fail otherwise */
#define DAOS_COND_KEY_INSERT	DAOS_COND_DKEY_INSERT
/* Conditional Op: Update key if it exists, fail otherwise */
#define DAOS_COND_KEY_UPDATE	DAOS_COND_DKEY_UPDATE
/* Conditional Op: Get key if it exists, fail otherwise */
#define DAOS_COND_KEY_GET	DAOS_COND_DKEY_FETCH
/* Conditional Op: Remove key if it exists, fail otherwise */
#define DAOS_COND_KEY_REMOVE	DAOS_COND_PUNCH

/**
 * Insert or update a single object KV pair. The key specified will be mapped to
 * a dkey in DAOS. The object akey will be the same as the dkey. If a value
 * existed before it will be overwritten (punched first if not previously an
 * atomic value) with the new atomic value described by the sgl.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	flags	Update flags.
 * \param[in]	key	Key associated with the update operation.
 * \param[in]	size	Size of the buffer to be inserted as an atomic val.
 * \param[in]	buf	Pointer to user buffer of the atomic value.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_kv_put(daos_handle_t oh, daos_handle_t th, uint64_t flags, const char *key,
	    daos_size_t size, const void *buf, daos_event_t *ev);

/**
 * Fetch value of a key.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	flags	Fetch flags.
 * \param[in]	key	key associated with the update operation.
 * \param[in,out]
 *		size	[in]: Size of the user buf. if the size is unknown, set
 *			to DAOS_REC_ANY). [out]: The actual size of the value.
 * \param[out]	buf	Pointer to user buffer. If NULL, only size is returned.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_REC2BIG	Record does not fit in buffer
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_kv_get(daos_handle_t oh, daos_handle_t th, uint64_t flags, const char *key,
	    daos_size_t *size, void *buf, daos_event_t *ev);

/**
 * Remove a Key and it's value from the KV store
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	flags	Remove flags.
 * \param[in]	key	Key to be punched/removed.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_kv_remove(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       const char *key, daos_event_t *ev);

/**
 * List/enumerate all keys in an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 * \param[in,out]
 *		kds	[in]: preallocated array of \a nr key descriptors.
 *			[out]: size of each individual key.
 * \param[in]	sgl	Scatter/gather list to store the dkey list.
 *			All keys are written contiguously, with actual
 *			boundaries that can be calculated using \a kds.
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_kv_list(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
	     daos_key_desc_t *kds, d_sg_list_t *sgl, daos_anchor_t *anchor,
	     daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_KV_H__ */
