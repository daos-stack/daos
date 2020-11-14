/**
 * (C) Copyright 2020 Intel Corporation.
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
#ifndef __DAOS_DATAFLOW_H__
#define __DAOS_DATAFLOW_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_event.h>
#include <daos_obj.h>

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to update with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	flags	Update flags (currently ignored).
 *
 * \param[in]	dkey	Distribution key associated with the update operation.
 *
 * \param[in]	nr	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in]	iods	Array of I/O descriptor. Each descriptor is associated
 *			with an array identified by its akey and describes the
 *			list of record extent to update.
 *			Checksum of each record extent is stored in
 *			\a iods[]::iod_csums[]. If the record size of an extent
 *			is zero, then it is effectively a punch for the
 *			specified index range.
 *
 * \param[in]	sgls	Scatter/gather list (sgl) to store the input data
 *			records. Each I/O descriptor owns a separate sgl in
 *			\a sgls.
 *			Different records of the same extent can either be
 *			stored in separate iod of the sgl, or contiguously
 *			stored in arbitrary iods as long as total buffer size
 *			can match the total extent size.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_REC2BIG	Record is larger than the buffer in
 *					input \a sgls buffer.
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 *			-DER_EP_OLD	Related RPC is resent too late as to
 *					related resent history may have been
 *					aggregated. Update result is undefined.
 */
int
daos_obj_update(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		d_sg_list_t *sgls, daos_event_t *ev);

/**
 * Distribution key enumeration.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 *
 * \param[in,out]
 *		kds	[in]: preallocated array of \nr key descriptors. [out]:
 *			size of each individual key along with checksum type
 *			and size stored just after the key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the dkey list.
 *			All dkeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be fit into
 *					the \a sgl, the required minimal length
 *					to fit the key is returned by
 *					\a kds[0].kd_key_len. This error code
 *					only returned for the first key in this
 *					enumeration, then user can provide a
 *					larger buffer (for example two or three
 *					times \a kds[0].kd_key_len) and do the
 *					enumerate again.
 */
int
daos_obj_list_dkey(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		   daos_key_desc_t *kds, d_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev);

-------------------
| enumerate dkeys |
-------------------
        |
-------------------
|  filter dkeys   |
-------------------
        |
-------------------
|  filter dkeys   |
-------------------

int
daos_df_create(daos_handle_t oh, daos_handle_t *dfh);

int
daos_df_destroy(daos_handle_t dfh);

/**
 * dkey selection/filtering
 */

int
daos_df_filter_dkey(daos_handle_t dfh, "", );

int
daos_df_adjust_anchor_dkey(daos_handle_t dfh, daos_anchor_t *anchor);

int
daos_df_select_dkey(daos_handle_t dfh, daos_key_t *dkey);

/**
 * akey selection/filtering
 */

int
daos_df_filter_akey(daos_handle_t dfh, daos_key_t *akey)

int
daos_df_adjust_anchor_akey(daos_handle_t dfh, daos_anchor_t *anchor);

int
daos_df_select_akey(daos_handle_t dfh, daos_key_t *akey);

/**
 * record selection/filtering
 */

int
daos_df_filter_rec(daos_handle_t dfh, daos_rec_t *rec);


/**
 * Altering
 */
int
daos_df_alter(daos_handle_t dfh, daos_key_t *akey)

/**
 * Aggregator defines what results should be returned
 */
int
daos_df_aggreg(daos_handle_t dfh, 
uint32_t *nr_iods, daos_iod_t *iods, d_sg_list_t *sgl_agg, daos_event_t *ev)


filler

int
daos_df_set_output(daos_handle_t dfh, uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,  d_sg_list_t *sgl_recx);

/**
 * Once ready, just ship the dataflow to the server side
 */

int
daos_df_ship(daos_handle_t dfh, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_DATAFLOW_H__ */
