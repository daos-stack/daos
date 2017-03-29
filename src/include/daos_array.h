/**
 * (C) Copyright 2016 Intel Corporation.
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
 * DAOS ARRAY APIs
 */

#ifndef __DAOS_ARRAY_API_H__
#define __DAOS_ARRAY_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	daos_size_t		len;
	daos_off_t		index;
} daos_range_t;

/** describe ranges of an array object to access */
typedef struct {
	/** Number of ranges to access */
	daos_size_t		ranges_nr;
	/** Array of index/len pairs */
	daos_range_t	       *ranges;
} daos_array_ranges_t;

/**
 * Read data from an array object.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the read.
 *
 * \param range	[IN]	Ranges to read from the array.
 *
 * \param sgl   [IN/OUT]
 *			A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does. User allocates the
 *			buffer(s) and sets the length of each buffer.
 *
 * \param csums	[OUT]	Array of checksums for each buffer in the sgl.
 *			This is optional (pass NULL to ignore).
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_array_read(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev);

/**
 * Write data to an array object.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the write.
 *
 * \param range	[IN]	Ranges to write to the array.
 *
 * \param sgl   [IN]	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \param csums	[IN]	Array of checksums for each buffer in the sgl.
 *			This is optional (pass NULL to ignore).
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_array_write(daos_handle_t oh, daos_epoch_t epoch,
		 daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		 daos_csum_buf_t *csums, daos_event_t *ev);

int
daos_array_get_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t *size,
		    daos_event_t *ev);
int
daos_array_set_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t size,
		    daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_ARRAY_API_H__ */
