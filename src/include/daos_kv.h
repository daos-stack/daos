/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#include <daos_types.h>
#include <daos_obj.h>

/** Conditional Op: Insert key if it doesn't exist, fail otherwise */
#define DAOS_COND_KEY_INSERT	DAOS_COND_DKEY_INSERT
/** Conditional Op: Update key if it exists, fail otherwise */
#define DAOS_COND_KEY_UPDATE	DAOS_COND_DKEY_UPDATE
/** Conditional Op: Get key if it exists, fail otherwise */
#define DAOS_COND_KEY_GET	DAOS_COND_DKEY_FETCH
/** Conditional Op: Remove key if it exists, fail otherwise */
#define DAOS_COND_KEY_REMOVE	DAOS_COND_PUNCH

/**
 * Open a KV object. This is a local operation (no RPC involved).
 * The type bits in the oid must set DAOS_OT_KV_*.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the object type
 *			be set to DAOS_OT_KV_*.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW
 * \param[out]	oh	Returned kv object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_kv_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	     daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened KV object.
 *
 * \param[in]	oh	KV object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
daos_kv_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Destroy the kV object by punching all data (keys) in the kv object.
 * daos_obj_punch() is called underneath. The oh still needs to be closed with a
 * call to daos_kv_close().
 *
 * \param[in]	oh	KV object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_kv_destroy(daos_handle_t oh, daos_handle_t th, daos_event_t *ev);

/**
 * Insert or update a single object KV pair. If a value existed before it will
 * be overwritten (punched first if not previously an atomic value) with the new
 * atomic value described by the sgl.
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
