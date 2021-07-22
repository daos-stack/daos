/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_PIPE_H__
#define __DAOS_PIPE_H__

#if defined(__cplusplus)
extern "C" {
#endif


/** logic functions */
#define DAOS_FILTER_FUNC_EQ		0
#define DAOS_FILTER_FUNC_NE		1
#define DAOS_FILTER_FUNC_LT		2
#define DAOS_FILTER_FUNC_LE		3
#define DAOS_FILTER_FUNC_GE		4
#define DAOS_FILTER_FUNC_GT		5
#define DAOS_FILTER_FUNC_LIKE		6
#define DAOS_FILTER_FUNC_ISNULL		7
#define DAOS_FILTER_FUNC_ISNOTNULL	8
#define DAOS_FILTER_FUNC_AND		9
#define DAOS_FILTER_FUNC_OR		10

/** aggregation functions */
#define DAOS_FILTER_FUNC_SUM		100
#define DAOS_FILTER_FUNC_MIN		101
#define DAOS_FILTER_FUNC_MAX		102
#define DAOS_FILTER_FUNC_AVG		103

/** keys, constants */
#define DAOS_FILTER_DKEY		200
#define DAOS_FILTER_AKEY		201
#define DAOS_FILTER_CONST		202

/** types used in a filter object */
#define DAOS_FILTER_TYPE_BINARY		0
#define DAOS_FILTER_TYPE_STRING		1
#define DAOS_FILTER_TYPE_INTEGER	2
#define DAOS_FILTER_TYPE_REAL		3

/** types of pipeline nodes */
#define DAOS_PIPELINE_CONDITION		0
#define DAOS_PIPELINE_AGGREGATION	1


/**
 * A filter object, used to build operations for a pipeline node.
 *
 */
typedef struct {
	/**
	 * filter type can be any of the following:
	 *   -- functions:
	 *      - logical functions:
	 *          DAOS_FILTER_FUNC_EQ:		==
	 *          DAOS_FILTER_FUNC_NE:		!=
	 *          DAOS_FILTER_FUNC_LT:		<
	 *          DAOS_FILTER_FUNC_LE:		<=
	 *          DAOS_FILTER_FUNC_GE:		>=
	 *          DAOS_FILTER_FUNC_GT:		>
	 *          DAOS_FILTER_FUNC_LIKE:		== (reg exp.)
	 *          DAOS_FILTER_FUNC_ISNULL:		==NULL
	 *          DAOS_FILTER_FUNC_ISNOTNULL:		!=NULL
	 *          DAOS_FILTER_FUNC_AND:		&&
	 *          DAOS_FILTER_FUNC_OR:		||
	 *      - aggeration functions:
	 *          DAOS_FILTER_FUNC_SUM:		SUM()
	 *          DAOS_FILTER_FUNC_MIN:		MIN()
	 *          DAOS_FILTER_FUNC_MAX:		MAX()
	 *          DAOS_FILTER_FUNC_AVG:		AVG()
	 *   -- key:
	 *          DAOS_FILTER_DKEY:	Filter object represents dkey
	 *          DAOS_FILTER_AKEY	Filter object represents akey
	 *   -- constant:
	 *          DAOS_FILTER_CONST:	Filter object is a constant
	 */
	int		filter_type;
	/**
	 * Type of data. Only relevant for keys and constant filter type objects:
	 *          DAOS_FILTER_TYPE_BINARY
	 *          DAOS_FILTER_TYPE_STRING
	 *          DAOS_FILTER_TYPE_INTEGER
	 *          DAOS_FILTER_TYPE_REAL
	 */
	int		data_type;
	/**
	 * Number of parameters for this filter object. For example, for '=='
	 * we have 2 parameters.
	 */
	uint32_t	num_params;
	/**
	 * If filtering by akey, this tells us which one.
	 */
	d_iov_t		akey;
	/**
	 * If filter object is a constant, this holds its value.
	 */
	d_iov_t		constant;
	/**
	 * If filter should only be applied starting at an offset of the data.
	 */
	size_t		data_offset;
	/**
	 * Size of the data to be filtered.
	 */
	size_t		data_len;
} daos_pipeline_filter_t;

/**
 * A pipeline node, used to build a pipeline.
 */
typedef struct {
	/**
	 * Node type can be any of the following:
	 *   -- DAOS_PIPELINE_CONDITION:
	 *          Records in, and records (meeting condition) out
	 *   -- DAOS_PIPELINE_AGGREGATION:
	 *          Records in, a single value out
	 *
	 * NOTE: Pipeline nodes can only be chained the following way:
	 *             (condition) --> (condition)
	 *             (condition) --> (aggregation)
	 */
	int			node_type;
	/**
	 * Number of filters inside this pipeline node
	 */
	size_t			num_filters;
	/**
	 * Array of filters for this node
	 */
	daos_pipeline_filter_t	*filters;
} daos_pipeline_node_t;

/**
 * A pipeline.
 */
typedef struct {
	/**
	 * DAOS object to which this pipeline applies
	 */
	daos_handle_t		oh;
	/**
	 * Number of nodes chained in this pipeline
	 */
	size_t			num_nodes;
	/**
	 * Array of nodes for this pipeline
	 */
	daos_pipeline_node_t	*nodes;
} daos_pipeline_t;


/**
 * Adds a new pipeline node to the pipeline 'pipe' object. The effect of this
 * function is to "push back" the new node at the end of the pipeline.
 *
 * \param[in,out]	pipe	Pipeline object.
 *
 * \param[in]		node	Node object to be added to the pipeline.
*/
int
daos_pipeline_push(daos_pipeline_t *pipe, daos_pipeline_node_t *node);

/**
 * Adds a new filter object to the pipeline node object 'node'.
 *
 * \param[in,out]	node	Pipeline node object.
 *
 * \param[in]		filter	Filter object to be added to a pipeline node.
 */
int
daos_pipeline_node_push(daos_node_t *node, daos_pipeline_filter_t *filter);

/**
 * Checks that a pipeline object is well built. If the pipeline object is well
 * built, the function will return 0 (no error).
 *
 * \param[in]		pipe	Pipeline object.
 */
int
daos_pipeline_check(daos_pipeline_t *pipe);

/**
 * Runs a pipeline on DAOS, returning objects and/or aggregated results.
 *
 * \param[in]		pipe		Pipeline object.
 *
 * \param[in]		th		Optional transaction handle. Use
 *					DAOS_TX_NONE for an independent
 *					transaction.
 *
 * \param[in]		flags		Conditional operations.
 *
 * \param[in]		dkey		Optional dkey. When passed, no iteration
 *					is done and processing isonly performed
 *					on this specific dkey.
 *
 * \param[in]		nr_iods		Number of I/O descriptors in the iods
 *					table.
 *
 * \param[in,out]	iods		[in]: Array of I/O descriptors. Each
 *					descriptor is associated with a given
 *					akey and describes the list of
 *					record extents to fetch from the array.
 *					[out]: Checksum of each extent is
 *					returned via
 *					\a iods[]::iod_csums[]. If the record
 *					size of an extent is unknown (i.e. set
 *					to DAOS_REC_ANY as input), then the
 *					actual record size will be returned in
 *					\a iods[]::iod_size.
 *
 * \param[in,out]	anchor		Hash anchor for the next call, it should
 *					be set to zeroes for the first call, it
 *					should not be changed by caller
 *					between calls.
 *
 * \param[in,out]	nr_kds		[in]: Number of key descriptors in
 *					\a kds.
 *					[out:] Number of returned key descriptors.
 *
 * \param[in,out]	kds		[in]: Optional preallocated array of \nr
 *					key descriptors.
 *					[out]: Size of each individual key along
 *					with checksum type and size stored just
 *					after the key in \a sgl_keys.
 *
 * \param[out]		sgl_keys	Optional sgl storing all dkeys to be
 *					returned.
 *
 * \param[out]		sgl_recx	Optional sgl storing all the records to
 *					be returned.
 *
 * \param[out]		sgl_agg		Optional sgl with the returned value of
 *					the aggregator(s).
 *
 * \param[in]		ev		Completion event. It is optional.
 *					Function will run in blocking mode if
 *					\a ev is NULL.
 */
int
daos_pipeline_run(daos_pipeline_t *pipe, daos_handle_t th, uint64_t flags,
		  daos_key_t *dkey, uint32_t nr_iods, daos_iod_t *iods,
		  daos_anchor_t *anchor, uint32_t *nr_kds, daos_key_desc_t *kds,
		  d_sg_list_t *sgl_keys, d_sg_list_t *sgl_recx,
		  d_sg_list_t *sgl_agg, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PIPE_H__ */
