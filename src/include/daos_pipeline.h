/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * The Pipeline API is under heavy development and should not be used in production. The API is
 * subject to change.
 */

#ifndef __DAOS_PIPE_H__
#define __DAOS_PIPE_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdint.h>
#include <daos_types.h>
#include <daos_obj.h>

/**
 * A filter part object, used to build a filter object for a pipeline.
 *
 */
typedef struct {
	/**
	 *  Part type can be any of the following:
	 *   -- function:
	 *      - logical functions:
	 *          DAOS_FILTER_FUNC_EQ:		==
	 *          DAOS_FILTER_FUNC_NE:		!=
	 *          DAOS_FILTER_FUNC_LT:		<
	 *          DAOS_FILTER_FUNC_LE:		<=
	 *          DAOS_FILTER_FUNC_GE:		>=
	 *          DAOS_FILTER_FUNC_GT:		>
	 *          DAOS_FILTER_FUNC_IN:		IN (const1, const2, ...)
	 *          DAOS_FILTER_FUNC_LIKE:		== (reg exp.)
	 *          DAOS_FILTER_FUNC_ISNULL:		==NULL
	 *          DAOS_FILTER_FUNC_ISNOTNULL:		!=NULL
	 *          DAOS_FILTER_FUNC_AND:		&&
	 *          DAOS_FILTER_FUNC_OR:		||
	 *          DAOS_FILTER_FUNC_NOT:		!
	 *      - arithmetic functions:
	 *          DAOS_FILTER_FUNC_ADD:		+
	 *          DAOS_FILTER_FUNC_SUB:		-
	 *          DAOS_FILTER_FUNC_MUL:		*
	 *          DAOS_FILTER_FUNC_DIV:		/
	 *          DAOS_FILTER_FUNC_BITAND:		&
	 *      - aggregation functions:
	 *          DAOS_FILTER_FUNC_SUM:		SUM(a1, a2, ..., an)
	 *          DAOS_FILTER_FUNC_MIN:		MIN(a1, a2, ..., an)
	 *          DAOS_FILTER_FUNC_MAX:		MAX(a1, a2, ..., an)
	 *          DAOS_FILTER_FUNC_AVG:		AVG(a1, a2, ..., an)
	 *   -- key:
	 *          DAOS_FILTER_OID:	Filter part object represents object id
	 *          DAOS_FILTER_DKEY:	Filter part object represents dkey
	 *          DAOS_FILTER_AKEY	Filter part object represents akey
	 *   -- constant:
	 *          DAOS_FILTER_CONST:	Filter part object is a constant
	 */
	d_iov_t    part_type;
	/**
	 * Type of data. Only relevant for keys and constant filter part type
	 * objects:
	 *          DAOS_FILTER_TYPE_BINARY	Raw string (or array of bytes)
	 *          DAOS_FILTER_TYPE_STRING	First 8B (size_t) indicate size
	 *          DAOS_FILTER_TYPE_CSTRING	Always null ('\0') terminated
	 *          DAOS_FILTER_TYPE_UINTEGER1	Unsigned integers
	 *          DAOS_FILTER_TYPE_UINTEGER2
	 *          DAOS_FILTER_TYPE_UINTEGER4
	 *          DAOS_FILTER_TYPE_UINTEGER8
	 *          DAOS_FILTER_TYPE_INTEGER1	Signed integers
	 *          DAOS_FILTER_TYPE_INTEGER2
	 *          DAOS_FILTER_TYPE_INTEGER4
	 *          DAOS_FILTER_TYPE_INTEGER8
	 *          DAOS_FILTER_TYPE_REAL4	Floating point numbers
	 *          DAOS_FILTER_TYPE_REAL8
	 */
	d_iov_t    data_type;
	/**
	 * Number of operands for this filter part object. For example, for '=='
	 * we have 2 operands.
	 */
	uint32_t   num_operands;
	/**
	 * If filtering by akey, this tells us which one.
	 */
	daos_key_t akey;
	/**
	 * How many constants we have in \a constant
	 */
	size_t     num_constants;
	/**
	 * This object holds the value of the constants
	 */
	d_iov_t   *constant;
	/**
	 * If filter should only be applied starting at an offset of the data. Only relevant for
	 * keys part type objects. If object is an akey, and the akey is an array, data_offset
	 * corresponds to the first record in the extent (same as rx_idx in daos_recx_t).
	 */
	size_t     data_offset;
	/**
	 * Size of the data to be filtered. Only relevant for keys part type objects. If key is
	 * akey, and the akey is an array, data_len corresponds to the number of contiguous records
	 * in the extent (same as rx_nr in daos_recx_t). If 0, the stored length inside DAOS will be
	 * used instead.
	 */
	size_t     data_len;
} daos_filter_part_t;

/**
 * A filter object, used to build a pipeline.
 */
typedef struct {
	/**
	 * Filter type can be any of the following:
	 *   -- DAOS_FILTER_CONDITION:
	 *          Records in, and records (meeting condition) out
	 *   -- DAOS_FILTER_AGGREGATION:
	 *          Records in, a single value out (see aggregation functions above)
	 *
	 * NOTE: Pipeline nodes can only be chained the following way:
	 *             (condition) --> (condition)
	 *             (condition) --> (aggregation)
	 *             (aggregation) --> (aggregation)*
	 *
	 *       *chained aggregations are actually done in parallel. For
	 *        example, the following pipeline:
	 *            (condition) --> (aggregation1) --> (aggregation2)
	 *        is actually executed as:
	 *                          -> (aggregation1)
	 *            (condition) -|
	 *                          -> (aggregation2)
	 */
	d_iov_t              filter_type;
	/**
	 * Number of filter parts inside this pipeline filter
	 */
	uint32_t             num_parts;
	/**
	 * Array of filter parts for this filter object
	 */
	daos_filter_part_t **parts;
} daos_filter_t;

/**
 * A pipeline.
 */
typedef struct {
	/**
	 * Version number of the data structure.
	 */
	uint64_t        version;
	/**
	 * Number of total filters chained in this pipeline.
	 */
	uint32_t        num_filters;
	/**
	 * Array of filters for this pipeline.
	 */
	daos_filter_t **filters;
	/**
	 * Number of aggregation filters chained in this pipeline.
	 */
	uint32_t        num_aggr_filters;
	/**
	 * Pointer to the first aggregation filter in the array of filters.
	 */
	daos_filter_t **aggr_filters;
} daos_pipeline_t;

/**
 * Gather some statistics of daos_pipeline_run(); like the number of items that have been scanned.
 */
typedef struct {
	/**
	 * If filtering by object ids, \a nr_objs will register the number of objects
	 * considered. Otherwise (i.e., if an object handle is passed), \a nr_objs will always be
	 * zero (not one).
	 */
	uint64_t nr_objs;
	/**
	 * If filtering by dkey or akeys (or a combination of both), \a nr_dkeys will register the
	 * total number of dkeys scanned. If a dkey is provided to daos_pipeline_run(), \a nr_dkeys
	 * will always be zero (not one).
	 */
	uint64_t nr_dkeys;
	/**
	 * This variable will only be non-zero when a dkey is provided to daos_pipeline_run(), where
	 * akeys are being filtered from a particular dkey.
	 */
	uint64_t nr_akeys;
} daos_pipeline_stats_t;

/**
 * Initializes a new pipeline object.
 *
 * \param[in,out]	pipeline	Pipeline object.
 */
void
daos_pipeline_init(daos_pipeline_t *pipeline);

/**
 * Initializes a new filter object.
 *
 * \param[in,out]	filter		Filter object.
 */
void
daos_filter_init(daos_filter_t *filter);

/**
 * Adds a new filter object to the pipeline \a pipeline object. The effect of this function is
 * equivalent to "pushing back" the new filter at the end of the pipeline.
 *
 * \param[in,out]	pipeline	Pipeline object.
 *
 * \param[in]		filter		Filter object to be added to the pipeline.
 */
int
daos_pipeline_add(daos_pipeline_t *pipeline, daos_filter_t *filter);

/**
 * Adds a new filter part object to the filter object \a filter. The effect of this function is
 * equivalent to "pushing back" the new filter part at the end of the filter stack.
 *
 * \param[in,out]	filter	Filter object.
 *
 * \param[in]		part	Filter part object to be added to a filter.
 */
int
daos_filter_add(daos_filter_t *filter, daos_filter_part_t *part);

/**
 * Checks that a pipeline object is well built. If the pipeline object is well built, the function
 * will return 0 (no error).
 *
 * \param[in]		pipeline	Pipeline object.
 */
int
daos_pipeline_check(daos_pipeline_t *pipeline);

/**
 * Frees all memory allocated by DAOS for the pipeline during construction. More specifically, it
 * frees memory for filter and filter_part objects created during calls to \a daos_filter_add() and
 * \a daos_pipeline_add().
 *
 * \param[in,out]	pipeline	Pipeline object.
 */
int
daos_pipeline_free(daos_pipeline_t *pipeline);

/**
 * Runs a pipeline on DAOS, returning objects and/or aggregated results.
 *
 * \params[in]		coh		Container open handle.
 *
 * \param[in]		oh		Optional object open handle.
 *
 * \param[in]		pipeline	Pipeline object.
 *
 * \param[in]		th		Optional transaction handle. Use DAOS_TX_NONE for an
 *					independent transaction.
 *
 * \param[in]		flags		Conditional operations.
 *
 * \param[in]		dkey		Optional dkey. When passed, no key iteration is done and
 *					processing is only performed on this specific dkey.
 *
 * \param[in/out]	nr_iods		[in]: Number of I/O descriptors in the iods table.
 *					[out]: Number of returned I/O descriptors. Only relevant
 *					when \dkey is passed (in that case filtering is done to
 *					return those akeys that pass a particular filter for a
 *					given dkey).
 *
 * \param[in/out]	iods		[in]: Array of I/O descriptors. Each descriptor is
 *					associated with a given akey and describes the list of
 *					record extents to fetch from the array.
 *					[out]: Only relevant when \dkey is passed (see comment for
 *					\nr_iods).
 *
 * \param[in,out]	anchor		Hash anchor for the next call, it should be set to zeroes
 *					for the first call, it should not be changed by caller
 *					between calls.
 *
 * \param[in,out]	nr_kds		[in]: Number of key descriptors in \a kds.
 *					[out]: Number of returned key descriptors.
 *
 * \param[in,out]	kds		[in]: Preallocated array of \nr_kds key descriptors.
 *					Optional if \dkey passed.
 *					[out]: Size of each individual key along with checksum type
 *					and size stored just after the key in \a sgl_keys.
 *
 * \param[in]		sgl_keys	[in]: Preallocated array to store all the dkeys to be
 *					returned (at most \nr_kds). Optional when \dkey passed.
 *					When doing aggregations, or when \dkey is passed, only
 *					one dkey at most is returned (no matter the size of
 *					\nr_kds).
 *					[out]: All returned dkeys.
 *
 * \param[in]		sgl_recx	[in]: Preallocated array to store all the records to be
 *					returned (at most \nr_kds x \nr_iods). When doing
 *					aggregations, or when \dkey is passed, only one record (the
 *					data corresponding to \nr_iods I/O descriptors) at most is
 *					returned (no matter the size of \nr_kds).
 *					[out]: All returned records.
 *
 * \param[in]		recx_size	[in]: Optional preallocated array to store all the records'
 *					sizes to be returned (at most \nr_kds x \nr_iods). When
 *					doing aggregations, or when \dkey is passed, only the sizes
 *					for one dkey's records (the data corresponding to \nr_iods
 *					I/O descriptors) at most is returned (no matter the size of
 *					\nr_kds).
 *
 * \param[in]		sgl_agg		[in]: Optional preallocated array of iovs for aggregated
 *					values (number of iovs has to match the number of
 *					aggregation filters defined in the pipeline object). All
 *					aggregated values are returned as doubles, no matter the
 *					numeric type of the akey being aggregated. This means that
 *					the buffer for each iov should be at least 8 bytes.
 *					[out]: All returned aggregated values.
 *
 * \param[out]		stats		[in]: Optional preallocated object.
 *					[out]: The total number of items (objects, dkeys, and akeys)
 *					scanned while filtering and/or aggregating.
 *
 * \param[in]		ev		Completion event. It is optional. Function will run in
 *					blocking mode if \a ev is NULL.
 */
int
daos_pipeline_run(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t *pipeline, daos_handle_t th,
		  uint64_t flags, daos_key_t *dkey, uint32_t *nr_iods, daos_iod_t *iods,
		  daos_anchor_t *anchor, uint32_t *nr_kds, daos_key_desc_t *kds,
		  d_sg_list_t *sgl_keys, d_sg_list_t *sgl_recx, daos_size_t *recx_size,
		  d_sg_list_t *sgl_agg, daos_pipeline_stats_t *stats, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PIPE_H__ */
