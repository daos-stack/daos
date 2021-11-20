/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include <daos_pipeline.h>
#include <daos/task.h>
#include <daos_task.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos_types.h>
#include <daos/dtx.h>
#include <daos/placement.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <math.h>
#include "pipeline_rpc.h"

static void
pipeline_filter_get_data(daos_filter_part_t *part, d_iov_t *dkey,
			 uint32_t nr_iods, daos_iod_t *iods, d_sg_list_t *akeys,
			 uint32_t const_idx, d_iov_t **data_item)
{
	uint32_t i;

	*data_item = NULL;
	if (!strcmp(part->part_type, "DAOS_FILTER_DKEY"))
	{
		*data_item = dkey;
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_AKEY"))
	{
		for (i = 0; i < nr_iods; i++)
		{
			if (!strncmp((char *) part->akey.iov_buf,
				     (char *) iods[i].iod_name.iov_buf,
				              iods[i].iod_name.iov_len))
			{
				*data_item = akeys[i].sg_iovs;
				break;
			}
		}
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_CONST") &&
		const_idx < part->num_constants)
	{
		*data_item = &(part->constant[const_idx]);
	}
}

static int
pipeline_filter_like(d_iov_t *left, d_iov_t *right)
{
	char *left_val, *right_val;
	size_t left_data_size, right_data_size;
	size_t left_pos, right_pos, right_anchor;
	uint8_t right_anchor_set;
	uint8_t scaping;

	left_val		= (char *) left->iov_buf;
	right_val		= (char *) right->iov_buf;
	left_data_size		= left->iov_len;
	right_data_size		= right->iov_len;
	left_pos 		= 0;
	right_pos		= 0;
	right_anchor		= 0;
	right_anchor_set	= 0;
	scaping			= 0;

	while (left_pos < left_data_size && right_pos < right_data_size)
	{
		if (right_val[right_pos] == '\\') {
			scaping = 1;
			right_pos++;
			if (right_pos == right_data_size)
			{
				return 1; /** We should never reach this. */
			}
		}
		if (right_val[right_pos] == '%' && scaping == 0)
		{
			right_anchor_set = 1;
			right_anchor = ++right_pos;
			if (right_pos == right_data_size)
			{
				return 0; /** '%' is at the end. Pass. */
			}
		}
		if ((right_val[right_pos] == '_' && scaping == 0) ||
		     left_val[left_pos] == right_val[right_pos])
		{
			left_pos++;
			right_pos++;
		}
		else if (!right_anchor_set)
		{
			return 1; /** Mismatch and no wildcard. No pass. */
		}
		else
		{
			right_pos = right_anchor;
			left_pos++;
		}
		scaping = 0;
	}
	if (left_pos == left_data_size && right_pos == right_data_size)
	{
		return 0; /** Function pass. */
	}
	return 1; /** No pass. */
}

static int
pipeline_filter_cmp(d_iov_t *d_left, d_iov_t *d_right,
		    size_t offset_left, size_t size_left,
		    size_t offset_right, size_t size_right,
		    const char *data_type)
{
	size_t cmp_size;
	char *left_s, *right_s;

	left_s  = (char *) d_left->iov_buf;
	right_s = (char *) d_right->iov_buf;
	left_s  = &left_s[offset_left];
	right_s = &right_s[offset_right];
	cmp_size = (size_left <= size_right) ? size_left : size_right;

	/** Typed comparison */

	if (!strcmp(data_type, "DAOS_FILTER_TYPE_INTEGER1"))
	{
		signed char *left_i, *right_i;
		left_i  = (signed char *) left_s;
		right_i = (signed char *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_INTEGER2"))
	{
		short int *left_i, *right_i;
		left_i  = (short int *) left_s;
		right_i = (short int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_INTEGER4"))
	{
		int *left_i, *right_i;
		left_i  = (int *) left_s;
		right_i = (int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_INTEGER8"))
	{
		long long int *left_i, *right_i;
		left_i  = (long long int *) left_s;
		right_i = (long long int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_REAL4"))
	{
		float *left_f, *right_f;
		left_f  = (float *) left_s;
		right_f = (float *) right_s;
		if      (*left_f < *right_f)  return -1;
		else if (*left_f == *right_f) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_REAL8"))
	{
		double *left_f, *right_f;
		left_f  = (double *) left_s;
		right_f = (double *) right_s;
		if      (*left_f < *right_f)  return -1;
		else if (*left_f == *right_f) return 0;
		else                          return 1;
	}
	else if (!strcmp(data_type, "DAOS_FILTER_TYPE_STRING"))
	{
		return strcmp(left_s, right_s);
	}

	/** -- Raw cmp byte by byte */

	return memcmp(left_s, right_s, cmp_size);
}

static int
pipeline_filter_func(daos_filter_t *filter, d_iov_t *dkey, uint32_t nr_iods,
		     daos_iod_t *iods, d_sg_list_t *akeys, uint32_t *part_idx)
{
	uint32_t		i;
	uint32_t		comparisons = 1;
	int			rc;
	daos_filter_part_t	*part;
	daos_filter_part_t	*left;
	daos_filter_part_t	*right;
	d_iov_t			*d_left;
	d_iov_t			*d_right;

	part = filter->parts[*part_idx];
	*part_idx += 1;
	left = filter->parts[*part_idx];
	*part_idx += 1;
	right = filter->parts[*part_idx];

	/** -- Check if we have multiple constants to check */

	if (!strcmp(right->part_type, "DAOS_FILTER_CONST") &&
	    right->num_constants > 1)
	{
		comparisons = right->num_constants;
	}

	pipeline_filter_get_data(left, dkey, nr_iods, iods, akeys, 0, &d_left);
	for (i = 0; i < comparisons; i++)
	{
		pipeline_filter_get_data(right, dkey, nr_iods, iods, akeys,
					 i, &d_right);

		if (d_left == NULL || d_right == NULL)
		{
			return -DER_INVAL;
		}
		if (strcmp(left->data_type, right->data_type))
		{
			return -DER_INVAL;
		}
		if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_LIKE"))
		{
			/** filter 'LIKE' only works for strings */
			if (strcmp(left->data_type,
				   "DAOS_FILTER_TYPE_STRING") ||
			    strcmp(right->data_type,
				   "DAOS_FILTER_TYPE_STRING"))
			{
				return -DER_INVAL;
			}
			rc = pipeline_filter_like(d_left, d_right);
		}
		else
		{
			rc = pipeline_filter_cmp(d_left, d_right,
						 left->data_offset,
						 left->data_len,
						 right->data_offset,
						 right->data_len,
						 left->data_type);
		}

		if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_EQ") ||
		    !strcmp(part->part_type, "DAOS_FILTER_FUNC_IN") ||
		    !strcmp(part->part_type, "DAOS_FILTER_FUNC_LIKE"))
		{
			if (rc == 0) return 0;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_NE"))
		{
			if (rc != 0) return 0;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_LT"))
		{
			if (rc < 0)  return 0;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_LE"))
		{
			if (rc <= 0) return 0;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_GE"))
		{
			if (rc >= 0) return 0;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_GT"))
		{
			if (rc > 0)  return 0;
		}
	}

	return 1; /** Filter does not pass. */
}

static int
pipeline_filter_isnull(daos_filter_t *filter, d_iov_t *dkey, uint32_t nr_iods,
		       daos_iod_t *iods, d_sg_list_t *akeys, uint32_t *part_idx)
{
	daos_filter_part_t	*part;
	d_iov_t			*data;

	*part_idx += 1;
	part = filter->parts[*part_idx];

	if (!strcmp(part->part_type, "DAOS_FILTER_DKEY") ||
	    !strcmp(part->part_type, "DAOS_FILTER_CONST"))
	{
		return 1; /**
			   *  dkeys or constants can't be null in this context
			   */
	}

	pipeline_filter_get_data(part, dkey, nr_iods, iods, akeys, 0, &data);

	return data == NULL ? 0 : 1;
}

static int
pipeline_filter(daos_filter_t *filter, d_iov_t *dkey, uint32_t *nr_iods,
		daos_iod_t *iods, d_sg_list_t *akeys, uint32_t *part_idx)
{
	daos_filter_part_t	*part = filter->parts[*part_idx];

	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_EQ") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_IN") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_NE") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_LT") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_LE") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_GE") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_GT") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_LIKE"))
	{
		return pipeline_filter_func(filter, dkey, *nr_iods, iods, akeys,
					    part_idx);
	}
	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_ISNULL"))
	{
		*part_idx += 1;
		return pipeline_filter_isnull(filter, dkey, *nr_iods, iods,
					      akeys, part_idx);
	}
	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_ISNOTNULL"))
	{
		int rc;
		*part_idx += 1;
		if ((rc = pipeline_filter_isnull(filter, dkey, *nr_iods, iods,
						 akeys, part_idx)) < 0)
		{
			return rc; /** error */
		}
		return 1 - rc;
	}
	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_NOT"))
	{
		int rc;
		*part_idx += 1;
		if ((rc = pipeline_filter(filter, dkey, nr_iods, iods, akeys,
				     part_idx)) < 0)
		{
			return rc; /** error */
		}
		return 1 - rc; /** NOT */
	}
	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_AND"))
	{
		int rc_l, rc_r;
		*part_idx += 1;
		if ((rc_l = pipeline_filter(filter, dkey, nr_iods, iods, akeys,
				     part_idx)) < 0)
		{
			return rc_l; /** error */
		}
		*part_idx += 1;
		if ((rc_r = pipeline_filter(filter, dkey, nr_iods, iods, akeys,
				     part_idx)) < 0)
		{
			return rc_r; /** error */
		}
		return (rc_l == 0 && rc_r == 0) ? 0 : 1; /** AND */
	}
	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_OR"))
	{
		int rc_l, rc_r;
		*part_idx += 1;
		if ((rc_l = pipeline_filter(filter, dkey, nr_iods, iods, akeys,
				     part_idx)) < 0)
		{
			return rc_l; /** error */
		}
		*part_idx += 1;
		if ((rc_r = pipeline_filter(filter, dkey, nr_iods, iods, akeys,
				     part_idx)) < 0)
		{
			return rc_r; /** error */
		}
		return (rc_l == 0 || rc_r == 0) ? 0 : 1; /** OR */
	}

	return -DER_NOSYS; /** Unsupported function. */
}

static int
pipeline_filters(daos_pipeline_t *pipeline, d_iov_t *dkey, uint32_t *nr_iods,
		 daos_iod_t *iods, d_sg_list_t *akeys)
{
	int		rc;
	uint32_t	part_idx;
	uint32_t 	i;

	if (pipeline->num_filters == 0)
	{
		return 0; /** No filters means all records pass */
	}

	for (i = 0; i < pipeline->num_filters; i++)
	{
		part_idx = 0;
		if ((rc = pipeline_filter(pipeline->filters[i], dkey, nr_iods,
					  iods, akeys, &part_idx)))
		{
			return rc; /** error, or filter does not pass */
		}
	}

	return 0;
}

static int
read_iov_as_double(char *data, size_t offset, char *type, double *result)
{
	char *data_ = &data[offset];

	if (!strcmp(type, "DAOS_FILTER_TYPE_INTEGER1"))
	{
		signed char *val = (signed char *) data_;
		*result = (double) *val;
	}
	else if (!strcmp(type, "DAOS_FILTER_TYPE_INTEGER2"))
	{
		short int *val = (short int *) data_;
		*result = (double) *val;
	}
	else if (!strcmp(type, "DAOS_FILTER_TYPE_INTEGER4"))
	{
		int *val = (int *) data_;
		*result = (double) *val;
	}
	else if (!strcmp(type, "DAOS_FILTER_TYPE_INTEGER8"))
	{
		long long int *val = (long long int *) data_;
		*result = (double) *val;
	}
	else if (!strcmp(type, "DAOS_FILTER_TYPE_REAL4"))
	{
		float *val = (float *) data_;
		*result = (double) *val;
	}
	else if (!strcmp(type, "DAOS_FILTER_TYPE_REAL8"))
	{
		double *val = (double *) data_;
		*result = *val;
	}
	else
	{
		return -DER_INVAL;
	}
	return 0;
}

static int
pipeline_aggregation(daos_filter_t *filter, d_iov_t *dkey, uint32_t *nr_iods,
		     daos_iod_t *iods, d_sg_list_t *akeys, uint32_t *part_idx,
		     double *total)
{
	int			rc;
	daos_filter_part_t	*part = filter->parts[*part_idx];
	double			total_rec;

	if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_SUM") ||
	    !strcmp(part->part_type, "DAOS_FILTER_FUNC_AVG"))
	{
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_rec)))
		{
			return rc; /** error */
		}
		*total += total_rec;
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_MAX"))
	{
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_rec)))
		{
			return rc; /** error */
		}
		if (total_rec > *total)
		{
			*total = total_rec;
		}
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_MIN"))
	{
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_rec)))
		{
			return rc; /** error */
		}
		if (total_rec < *total)
		{
			*total = total_rec;
		}
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_DKEY") ||
		 !strcmp(part->part_type, "DAOS_FILTER_AKEY") ||
		 !strcmp(part->part_type, "DAOS_FILTER_CONST"))
	{
		d_iov_t *data;

		pipeline_filter_get_data(part, dkey, *nr_iods, iods, akeys, 0,
					 &data);
		if ((rc = read_iov_as_double((char *) data->iov_buf,
					     part->data_offset,
					     part->data_type, total)))
		{
			return rc; /** error */
		}
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_ADD"))
	{
		double total_left_rec, total_right_rec;
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_left_rec)))
		{
			return rc; /** error */
		}
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_right_rec)))
		{
			return rc; /** error */
		}
		*total += total_left_rec + total_right_rec; /** ADD */
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_SUB"))
	{
		double total_left_rec, total_right_rec;
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_left_rec)))
		{
			return rc; /** error */
		}
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_right_rec)))
		{
			return rc; /** error */
		}
		*total += total_left_rec - total_right_rec; /** SUB */
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_MUL"))
	{
		double total_left_rec, total_right_rec;
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_left_rec)))
		{
			return rc; /** error */
		}
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_right_rec)))
		{
			return rc; /** error */
		}
		*total += total_left_rec * total_right_rec; /** MUL */
	}
	else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_DIV"))
	{
		double total_left_rec, total_right_rec;
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_left_rec)))
		{
			return rc; /** error */
		}
		*part_idx += 1;
		if ((rc = pipeline_aggregation(filter, dkey, nr_iods, iods,
					       akeys, part_idx,
					       &total_right_rec)))
		{
			return rc; /** error */
		}
		if (total_right_rec == 0.0)
		{
			return -DER_DIV_BY_ZERO;
		}

		*total += total_left_rec / total_right_rec; /** DIV */
	}
	else
	{
		return -DER_NOSYS; /** Unsupported function. */
	}

	return 0;
}

static int
pipeline_aggregations(daos_pipeline_t *pipeline, d_iov_t *dkey,
		      uint32_t *nr_iods, daos_iod_t *iods, d_sg_list_t *akeys,
		      d_sg_list_t *sgl_agg)
{
	int		rc;
	uint32_t	i;
	uint32_t	part_idx;

	if (pipeline->num_aggr_filters == 0)
	{
		return 0; /** No filters means no aggregation */
	}
	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part_idx = 0;
		if ((rc = pipeline_aggregation(
				       pipeline->aggr_filters[i], dkey, nr_iods,
				       iods, akeys, &part_idx,
				       (double *) sgl_agg[i].sg_iovs->iov_buf
					      )) < 0)
		{
			return rc; /** error */
		}
	}

	return 0;
}

static void
pipeline_aggregations_fixavgs(daos_pipeline_t *pipeline, double total,
			      d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	daos_filter_part_t	*part;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part = pipeline->aggr_filters[i]->parts[0];
		if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_AVG"))
		{
			buf = (double *) sgl_agg[i].sg_iovs->iov_buf;
			*buf = *buf / total;
		}
	}
}

static void
pipeline_aggregations_init(daos_pipeline_t *pipeline, d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	daos_filter_part_t	*part;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part = pipeline->aggr_filters[i]->parts[0];
		buf  = (double *) sgl_agg[i].sg_iovs->iov_buf;

		if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_MAX"))
		{
			*buf = -INFINITY;
		}
		else if (!strcmp(part->part_type, "DAOS_FILTER_FUNC_MIN"))
		{
			*buf = INFINITY;
		}
		else
		{
			*buf = 0;
		}
	}
}

static uint32_t
pipeline_part_nops(const char *part_type)
{
	if (!strcmp(part_type, "DAOS_FILTER_FUNC_EQ")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_IN")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_NE")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_LT")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_LE")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_GE")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_GT")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_AND") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_OR")  ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_ADD") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_SUB") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_MUL") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_DIV"))
	{
		return 2;
	}
	else if (!strcmp(part_type, "DAOS_FILTER_FUNC_LIKE")      ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_ISNULL")    ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_NOT")       ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_SUM")       ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_MIN")       ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_MAX")       ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_AVG"))
	{
		return 1;
	}
	return 0; /** Everything else has zero operands */
}

static bool
pipeline_part_checkop(const char *part_type, const char *operand_type)
{
	if (!strcmp(part_type, "DAOS_FILTER_FUNC_NOT") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_AND") ||
	    !strcmp(part_type, "DAOS_FILTER_FUNC_OR"))
	{
		return !strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"));
	}
	else if (!strcmp(part_type, "DAOS_FILTER_FUNC_EQ")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_IN")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_NE")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_LT")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_LE")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_GE")  ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_GT"))
	{
		return strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"));
	}
	else if (!strcmp(part_type, "DAOS_FILTER_FUNC_SUM") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_MIN") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_MAX") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_AVG") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_ADD") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_SUB") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_MUL") ||
		 !strcmp(part_type, "DAOS_FILTER_FUNC_DIV"))
	{
		return strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"))           ||
			!strcmp(operand_type, "DAOS_FILTER_FUNC_ADD") ||
			!strcmp(operand_type, "DAOS_FILTER_FUNC_SUB") ||
			!strcmp(operand_type, "DAOS_FILTER_FUNC_MUL") ||
			!strcmp(operand_type, "DAOS_FILTER_FUNC_DIV");
	}
	return false; /** we should not reach this ever */
}

static bool
pipeline_filter_checkops(daos_filter_t *ftr, size_t *p)
{
	uint32_t	i;
	uint32_t	num_operands;
	bool		res;
	char		*part_type;
	char		*child_part_type;

	num_operands 	= ftr->parts[*p]->num_operands;
	part_type	= ftr->parts[*p]->part_type;
	for (i = 0; i < num_operands; i++)
	{
		*p		+= 1;
		child_part_type	= ftr->parts[*p]->part_type;
		res = pipeline_part_checkop(part_type, child_part_type);
		if (res == false)
		{
			return res;
		}
		res = pipeline_filter_checkops(ftr, p);
		if (res == false)
		{
			return res;
		}
	}
	return true;
}


/**************************/


int dc_pipeline_check(daos_pipeline_t *pipeline)
{
	size_t i;

	/** -- Check 0: Check that pipeline is not NULL. */

	if (pipeline == NULL)
	{
		return -DER_INVAL;
	}

	/** -- Check 1: Check that filters are chained together correctly. */

	{
		for (i = 0; i < pipeline->num_filters; i++)
		{
			if (strcmp(pipeline->filters[i]->filter_type,
				    "DAOS_FILTER_CONDITION"))
			{
				return -DER_INVAL;
			}
		}
		for (i = 0; i < pipeline->num_aggr_filters; i++)
		{
			if (strcmp(pipeline->aggr_filters[i]->filter_type,
				    "DAOS_FILTER_AGGREGATION"))
			{
				return -DER_INVAL;
			}
		}
	}

	/** -- Rest of the checks are done for each filter */

	for (i = 0;
	     i < pipeline->num_filters + pipeline->num_aggr_filters;
	     i++)
	{
		daos_filter_t *ftr;
		size_t p;
		uint32_t num_parts = 0;
		uint32_t num_operands;
		bool res;

		if (i < pipeline->num_filters)
		{
			ftr = pipeline->filters[i];
		}
		else
		{
			ftr = pipeline->aggr_filters[i-pipeline->num_filters];
		}
		if (ftr->num_parts)
		{
			num_parts = 1;
		}

		/**
		 * -- Check 2: Check that all parts have a correct
		 *             number of operands and also that the
		 *             number of total parts is correct.
		 */

		for (p = 0; p < ftr->num_parts; p++)
		{
			daos_filter_part_t *part = ftr->parts[p];
			num_operands = pipeline_part_nops(part->part_type);

			if (num_operands != part->num_operands)
			{
				return -DER_INVAL;
			}
			num_parts += part->num_operands;
		}
		if (num_parts != ftr->num_parts)
		{
			return -DER_INVAL;
		}

		/**
		 * -- Check 3: Check that all parts have the right
		 *             type of operands.
		 */

		p = 0;
		res = pipeline_filter_checkops(ftr, &p);
		if (res == false)
		{
			return -DER_INVAL;
		}
	}

	return 0;
}

struct pipeline_auxi_args {
	int		opc;
	int		result;
	uint32_t	map_ver_req;
	uint32_t	initial_shard;
	daos_obj_id_t	omd_id;
	tse_task_t	*pipeline_task;
	d_list_t	shard_task_head;
};

struct shard_pipeline_run_args {
	struct dtx_epoch		pra_epoch;
	uint32_t			pra_map_ver;
	uint32_t			pra_shard;
	uint32_t			pra_target;

	daos_pipeline_run_t		*pra_api_args;
	struct dtx_id			pra_dti;
	uuid_t				pra_coh_uuid;
	uuid_t				pra_cont_uuid;
	//uint64_t			pra_dkey_hash; // ??
	
	struct pipeline_auxi_args	*pipeline_auxi;
};

struct pipeline_run_cb_args {
	crt_rpc_t		*rpc;
	unsigned int		*map_ver;
	struct dtx_epoch	epoch;
	daos_handle_t		th;
};

static int
pipeline_comp_cb(tse_task_t *task, void *data)
{
	if (task->dt_result != 0)
	{
		D_DEBUG(DB_IO, "pipeline_comp_db task=%p result=%d\n",
			task, task->dt_result);
	}

	return 0;
}

static int
pipeline_shard_run_cb(tse_task_t *task, void *data)
{
	struct pipeline_run_cb_args	*cb_args;
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				opc;
	int				ret = task->dt_result;
	int				rc = 0;
	crt_rpc_t			*rpc;

	cb_args		= (struct pipeline_run_cb_args *) data;
	rpc		= cb_args->rpc;
	pri		= crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	opc = opc_get(rpc->cr_opc);

	if (ret != 0)
	{
		D_ERROR("RPC %d failed, "DF_RC"\n", opc, DP_RC(ret));
		D_GOTO(out, ret);
	}

	pro	= crt_reply_get(rpc);
	//rc	= obj_reply_get_status(rpc); //TODO ?
	/*if (daos_handle_is_valid(cb_args->th)) // ???
	{
		int rc_tmp;

		rc_tmp = dc_tx_op_end(task, cb_args->th, &cb_args->epoch, rc,
				      pro->pro_epoch);
		if (rc_tmp != 0)
		{
			D_ERROR("failed to end transaction operation (rc=%d "
				"epoch="DF_U64": "DF_RC"\n", rc,
				pro->pro_epoch, DP_RC(rc_tmp));
			goto out;
		}
	}*/

	if (rc != 0)
	{
		if (rc == -DER_NONEXIST)
		{
			D_GOTO(out, rc = 0);
		}
		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY)
		{
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: %d\n",
				rpc, opc, rc);
		}
		else
		{
			D_ERROR("rpc %p RPC %d failed: %d\n", rpc, opc, rc);
		}
		D_GOTO(out, rc);
	}
	pro = (struct pipeline_run_out *) crt_reply_get(rpc);

	fprintf(stdout, "(shard callback) RESULT = %lu\n", pro->pro_pong);
	fflush(stdout);

out:
	crt_req_decref(rpc);
	if (ret == 0) // || obj_retry_error(rc)) TODO
	{
		ret = rc;
	}
	return ret;
}

static int
shard_pipeline_run_task(tse_task_t *task)
{
	struct shard_pipeline_run_args	*args;
	crt_rpc_t			*req;
	crt_context_t			crt_ctx;
	crt_opcode_t			opcode;
	crt_endpoint_t			tgt_ep;
	daos_handle_t			coh;
	daos_handle_t			poh;
	struct dc_pool			*pool = NULL;
	struct pool_target		*map_tgt;
	struct pipeline_run_cb_args	cb_args;
	struct pipeline_run_in		*pri;
	int				rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	crt_ctx	= daos_task2ctx(task);
	opcode	= DAOS_RPC_OPCODE(DAOS_PIPELINE_RPC_RUN,
				  DAOS_PIPELINE_MODULE,
				  DAOS_PIPELINE_VERSION);

	coh	= dc_obj_hdl2cont_hdl(args->pra_api_args->oh);
	poh	= dc_cont_hdl2pool_hdl(coh);
	pool	= dc_hdl2pool(poh);
	if (pool == NULL)
	{
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	rc = dc_cont_tgt_idx2ptr(coh, args->pra_target, &map_tgt);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	tgt_ep.ep_grp	= pool->dp_sys->sy_group;
	tgt_ep.ep_tag	= daos_rpc_tag(DAOS_REQ_IO, map_tgt->ta_comp.co_index);
	tgt_ep.ep_rank	= map_tgt->ta_comp.co_rank;

	rc = crt_req_create(crt_ctx, &tgt_ep, opcode, &req);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}
	crt_req_addref(req);
	cb_args.rpc		= req;
	cb_args.map_ver		= &args->pra_map_ver;
	cb_args.epoch		= args->pra_epoch;
	cb_args.th		= args->pra_api_args->th;

	rc = tse_task_register_comp_cb(task, pipeline_shard_run_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
	{
		D_GOTO(out_req, rc);
	}

	pri = crt_req_get(req); //TODO
	D_ASSERT(pri != NULL);
	pri->pri_ping		= 12345;
	pri->pri_epoch		= args->pra_epoch.oe_value;
	pri->pri_epoch_first	= args->pra_epoch.oe_first;

	rc = daos_rpc_send(req, task);
	dc_pool_put(pool);
	return rc;
out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	if (pool)
	{
		dc_pool_put(pool);
	}
	tse_task_complete(task, rc);
	return rc;
}

static int
shard_pipeline_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	tse_task_list_del(task);
	tse_task_decref(task);
	tse_task_complete(task, rc);

	return 0;
}

static int
queue_shard_pipeline_run_task(tse_task_t *api_task, struct pl_obj_layout *layout,
			      struct pipeline_auxi_args *pipeline_auxi,
			      struct dtx_epoch *epoch, int shard,
			      unsigned int map_ver, struct dtx_id *dti,
			      uuid_t coh_uuid, uuid_t cont_uuid)
{
	daos_pipeline_run_t		*api_args;
	tse_sched_t			*sched;
	tse_task_t			*task;
	struct shard_pipeline_run_args	*args;
	int				rc;

	/** queue task for leader shard */
	api_args	= dc_task_get_args(api_task);
	sched		= tse_task2sched(api_task);
	rc		= tse_task_create(shard_pipeline_run_task, // TODO
					  sched, NULL, &task);
	if (rc != 0)
	{
		tse_task_complete(task, rc);
		D_GOTO(out_task, rc);
	}

	args = tse_task_buf_embedded(task,
				     sizeof(*args));
	args->pra_api_args	= api_args;
	args->pra_epoch		= *epoch;
	args->pra_map_ver	= map_ver;
	args->pra_shard		= shard;
	args->pra_dti		= *dti;
	args->pipeline_auxi	= pipeline_auxi;
	uuid_copy(args->pra_coh_uuid, coh_uuid);
	uuid_copy(args->pra_cont_uuid, cont_uuid);
	args->pra_target	= layout->ol_shards[shard].po_target;
	rc = tse_task_register_deps(api_task, 1, &task);
	if (rc != 0)
	{
		tse_task_complete(task, rc);
		D_GOTO(out_task, rc);
	}
	tse_task_addref(task);
	tse_task_list_add(task, &pipeline_auxi->shard_task_head);

out_task:
	if (rc)
	{
		tse_task_complete(task, rc);
	}
	return rc;
}

struct shard_task_sched_args {
	struct dtx_epoch	tsa_epoch;
	bool			tsa_scheded;
};

static int
shard_task_sched(tse_task_t *task, void *arg)
{
	struct shard_task_sched_args	*sched_arg = arg;
	int				rc = 0;

	/** TODO: Retry I/O 
	* ...
	* ...
	* ...
	*/

	tse_task_schedule(task, true);
	sched_arg->tsa_scheded = true;

	return rc;
}

int
dc_pipeline_run__dummy(tse_task_t *api_task)
{
	daos_pipeline_run_t		*api_args = dc_task_get_args(api_task);
	struct pl_obj_layout		*layout = NULL;
	struct dc_pool			*pool;
	struct pl_map			*map;
	daos_handle_t			coh;
	daos_handle_t			poh;
	struct daos_obj_md		obj_md;
	struct daos_oclass_attr		*oca;
	int				rc;
	uint32_t			i;
	d_list_t			*head = NULL;
	struct dtx_id			dti;
	struct dtx_epoch		epoch;
	uint32_t			map_ver;
	uuid_t				coh_uuid;
	uuid_t				cont_uuid;
	struct pipeline_auxi_args	*pipeline_auxi;
	bool				priv;
	struct shard_task_sched_args	sched_arg;

	/** -- Layout create */

	coh	= dc_obj_hdl2cont_hdl(api_args->oh);
	rc	= dc_obj_hdl2obj_md(api_args->oh, &obj_md);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}
	poh	= dc_cont_hdl2pool_hdl(coh);
	pool	= dc_hdl2pool(poh);
	if (pool == NULL)
	{
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	map = pl_map_find(pool->dp_pool, obj_md.omd_id);
	if (map == NULL)
	{
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		dc_pool_put(pool);
		D_GOTO(out, rc = -DER_INVAL);
	}
	obj_md.omd_ver = dc_pool_get_version(pool);
	dc_pool_put(pool);
	rc = pl_obj_place(map, &obj_md, NULL, &layout);
	pl_map_decref(map);
	if (rc != 0)
	{
		D_DEBUG(DB_PL, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, "Place object on %d targets ver %d\n", layout->ol_nr,
		layout->ol_ver);
	D_ASSERT(layout->ol_nr == layout->ol_grp_size * layout->ol_grp_nr);

	if (daos_handle_is_valid(api_args->th))
	{
		rc = dc_tx_hdl2dti(api_args->th, &dti);
		D_ASSERTF(rc == 0, "%d\n", rc);
		rc = dc_tx_hdl2epoch_pmv(api_args->th, &epoch, &map_ver);
		if (rc != 0)
		{
			D_GOTO(out, rc);
		}
	}
	else
	{
		daos_dti_gen(&dti, true /* zero */);
		dc_io_set_epoch(&epoch);
		D_DEBUG(DB_IO, "set fetch epoch "DF_U64"\n", epoch.oe_value);
	}
	if (map_ver == 0)
	{
		map_ver = layout->ol_ver;
	}

	/** -- Create auxi object */

	pipeline_auxi = tse_task_stack_push(api_task, sizeof(*pipeline_auxi));
	pipeline_auxi->opc		= DAOS_PIPELINE_RPC_RUN;
	pipeline_auxi->map_ver_req	= map_ver;
	pipeline_auxi->omd_id		= obj_md.omd_id;
	head				= &pipeline_auxi->shard_task_head;
	D_INIT_LIST_HEAD(head);
	rc = tse_task_register_comp_cb(api_task, pipeline_comp_cb, NULL, 0);
	if (rc != 0)
	{
		D_ERROR("task %p, register_comp_cb "DF_RC"\n", api_task, DP_RC(rc));
		tse_task_stack_pop(api_task, sizeof(struct pipeline_auxi_args));
		D_GOTO(out, rc);
	}

	/** -- Iterate over shards */

	oca = daos_oclass_attr_find(obj_md.omd_id, &priv);
	if (oca == NULL)
	{
		D_DEBUG(DB_PL, "Failed to find oclass attr\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ASSERT(d_list_empty(head));

	for (i = 0; i < layout->ol_grp_nr; i++)
	{
		int start_shard;
		int j;

		/** Try leader for current group */
		start_shard = i * layout->ol_grp_size;
		if (!daos_oclass_is_ec(oca) ||
				likely(!DAOS_FAIL_CHECK(DAOS_OBJ_SKIP_PARITY)))
		{
			int leader;

			leader =
			     pl_select_leader(obj_md.omd_id,
					      start_shard / layout->ol_grp_size,
					      layout->ol_grp_size, NULL,
					      pl_obj_get_shard, layout);
			if (leader >= 0)
			{
				rc = queue_shard_pipeline_run_task(api_task, layout,
								   pipeline_auxi,
								   &epoch, leader,
								   map_ver, &dti,
								   coh_uuid, cont_uuid);
				if (rc)
				{
					D_GOTO(out, rc);
				}
				continue;
			}
			if (!daos_oclass_is_ec(oca))
			{
				/* There has to be a leader for non-EC object */
				D_ERROR(DF_OID" no valid shard, rc " DF_RC"\n",
					DP_OID(obj_md.omd_id), DP_RC(leader));
				D_GOTO(out, rc = leader);
			}
		}

		/** Then try non-leader shards */
		D_DEBUG(DB_IO, DF_OID" try non-leader shards for group %d.\n",
			DP_OID(obj_md.omd_id), i);
		for (j = start_shard; j < start_shard + oca->u.ec.e_k; j++) {
			rc = queue_shard_pipeline_run_task(api_task, layout, pipeline_auxi,
							   &epoch, j, map_ver, &dti,
							   coh_uuid, cont_uuid);
			if (rc)
			{
				D_GOTO(out, rc);
			}
		}
	}

	D_ASSERT(!d_list_empty(head));
	sched_arg.tsa_scheded	= false;
	sched_arg.tsa_epoch	= epoch;
	tse_task_list_traverse(head, shard_task_sched, &sched_arg);
	if (sched_arg.tsa_scheded == false)
	{
		tse_task_complete(api_task, 0);
	}

	return rc;
out:
	if (head != NULL && !d_list_empty(head))
	{
		tse_task_list_traverse(head, shard_pipeline_task_abort, &rc);
	}
	
	tse_task_complete(api_task, rc);

	return rc;
}

int
dc_pipeline_run(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t pipeline,
		daos_handle_t th, uint64_t flags, daos_key_t *dkey,
		uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
		uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
		d_sg_list_t *sgl_recx, d_sg_list_t *sgl_agg, daos_event_t *ev)
{
	uint32_t		i, j, k, l;
	int			rc;
	uint32_t		nr_kds_iter, nr_kds_pass;
	uint32_t		nr_kds_param, nr_iods_param;
	daos_key_desc_t		*kds_iter		= NULL;
	d_iov_t			*sg_iovs_keys_iter	= NULL;
	d_sg_list_t		*sgl_keys_iter		= NULL;
	d_iov_t			*sg_iovs_recx_iter	= NULL;
	d_sg_list_t		*sgl_recx_iter		= NULL;

	if ((rc = dc_pipeline_check(&pipeline)) < 0)
	{
		return rc; /** Bad pipeline */
	}
	if (pipeline.version != 1)
	{
		return -DER_MISMATCH; /** wrong version */
	}
	if (daos_anchor_is_eof(anchor))
	{
		return 0; /** no more rows */
	}
	if (*nr_iods == 0)
	{
		return 0; /** nothing to return */
	}
	nr_iods_param = *nr_iods;

	if (*nr_kds == 0 && pipeline.num_aggr_filters == 0)
	{
		return 0; /** nothing to return */
	}
	else if (*nr_kds == 0)
	{
		nr_kds_param = 64; /**
				    * -- Full aggregation. We fetch at most 64
				    *    records at a time.
				    */
	}
	else
	{
		nr_kds_param  = *nr_kds;
	}

	/** -- memory allocation for temporary buffers */

	kds_iter = (daos_key_desc_t *)
			calloc(nr_kds_param, sizeof(daos_key_desc_t));

	sg_iovs_keys_iter = (d_iov_t *) calloc(nr_kds_param, sizeof(d_iov_t));

	sgl_keys_iter = (d_sg_list_t *)
			calloc(nr_kds_param, sizeof(d_sg_list_t));

	sg_iovs_recx_iter = (d_iov_t *)
			calloc(nr_iods_param*nr_kds_param, sizeof(d_iov_t));

	sgl_recx_iter = (d_sg_list_t *)
			calloc(nr_iods_param*nr_kds_param, sizeof(d_sg_list_t));

	if (kds_iter == NULL || sg_iovs_keys_iter == NULL ||
		sgl_keys_iter == NULL || sgl_recx_iter == NULL)
	{
		rc = -DER_NOMEM;
		goto exit;
	}
	for (i = 0; i < nr_kds_param; i++)
	{
		void *buf;

		sgl_keys_iter[i].sg_nr		= sgl_keys[i].sg_nr;
		sgl_keys_iter[i].sg_nr_out	= sgl_keys[i].sg_nr_out;
		sgl_keys_iter[i].sg_iovs	= &sg_iovs_keys_iter[i];

		buf = malloc(sgl_keys[i].sg_iovs->iov_buf_len);
		if (buf == NULL)
		{
			rc = -DER_NOMEM;
			goto exit;
		}

		d_iov_set(&sg_iovs_keys_iter[i], buf,
			  sgl_keys[i].sg_iovs->iov_buf_len);

		for (j = 0; j < nr_iods_param; j++)
		{
			l = i*nr_iods_param+j;
			sgl_recx_iter[l].sg_nr     = sgl_recx[l].sg_nr;
			sgl_recx_iter[l].sg_nr_out = sgl_recx[l].sg_nr_out;
			sgl_recx_iter[l].sg_iovs   = &sg_iovs_recx_iter[l];

			buf = malloc(sgl_recx[l].sg_iovs->iov_buf_len);
			if (buf == NULL)
			{
				rc = -DER_NOMEM;
				goto exit;
			}

			d_iov_set(&sg_iovs_recx_iter[l], buf,
				  sgl_recx[l].sg_iovs->iov_buf_len);
		}
	}

	/**
	 * -- Init all aggregation counters.
	 */
	pipeline_aggregations_init(&pipeline, sgl_agg);

	/**
	 * -- Iterating over dkeys and doing filtering and aggregation. The
	 *    variable nr_kds_pass stores the number of dkeys in total that
	 *    pass the filter. Since we want to return at most nr_kds_param, we
	 *    try to fetch (nr_kds_param - nr_kds_pass) in each iteration.
	 */

	nr_kds_pass = 0;
	while (!daos_anchor_is_eof(anchor))
	{
		if (pipeline.num_aggr_filters == 0)
		{
			nr_kds_iter = nr_kds_param - nr_kds_pass;
			if (nr_kds_iter == 0) /** all asked records read */
				break;
		}
		else /** for aggr, we read all (nr_kds_param at a time) */
		{
			nr_kds_iter = nr_kds_param;
		}
		if ((rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr_kds_iter,
					kds_iter, sgl_keys_iter, anchor, NULL)))
		{
			goto exit;
		}
		if (nr_kds_iter == 0)
			continue; /** no more records? */

		/** -- Fetching the akey data for each dkey */

		for (i = 0; i < nr_kds_iter; i++)
		{
			if ((rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0,
						sgl_keys_iter[i].sg_iovs,
						nr_iods_param, iods,
						&sgl_recx_iter[i*nr_iods_param],
						NULL, NULL)))
			{
				goto exit;
			}

			/** -- Doing filtering ... */

			if ((rc = pipeline_filters(
					       &pipeline,
					       sgl_keys_iter[i].sg_iovs,
					       &nr_iods_param, iods,
					       &sgl_recx_iter[i*nr_iods_param]
						)) < 0)
			{
				goto exit; /** error */
			}
			if (rc == 1) /** Filters don't pass */
			{
				continue;
			}

			/** -- dkey+akeys pass filters */

			nr_kds_pass++;

			/** -- Aggregations */

			if ((rc = pipeline_aggregations(
						&pipeline,
						sgl_keys_iter[i].sg_iovs,
						&nr_iods_param, iods,
						&sgl_recx_iter[i*nr_iods_param],
						sgl_agg)) < 0)
			{
				goto exit; /** error */
			}
	
			/**
			 * -- Returning matching records. We don't need to
			 *    return all matching records if aggregation is
			 *    being performed: at most one is returned.
			 */

			if (*nr_kds == 0 ||
			     (nr_kds_pass > 1 && pipeline.num_aggr_filters > 0))
			{
				continue; /** not returning (any/more) rcx */
			}

			memcpy((void *) &kds[nr_kds_pass-1],
			       (void *) &kds_iter[i],
			       sizeof(daos_key_desc_t));

			sgl_keys[nr_kds_pass-1].sg_nr = sgl_keys_iter[i].sg_nr;
			sgl_keys[nr_kds_pass-1].sg_nr_out =
						     sgl_keys_iter[i].sg_nr_out;
			memcpy(sgl_keys[nr_kds_pass-1].sg_iovs->iov_buf,
			       sgl_keys_iter[i].sg_iovs->iov_buf,
			       sgl_keys_iter[i].sg_iovs->iov_buf_len);

			for (j = 0; j < nr_iods_param; j++)
			{
				l = i*nr_iods_param+j;
				k = (nr_kds_pass-1)*nr_iods_param+j;
				sgl_recx[k].sg_nr = sgl_recx_iter[l].sg_nr;
				sgl_recx[k].sg_nr_out =
						     sgl_recx_iter[l].sg_nr_out;
				memcpy(sgl_recx[k].sg_iovs->iov_buf,
				       sgl_recx_iter[l].sg_iovs->iov_buf,
				       sgl_recx_iter[l].sg_iovs->iov_buf_len);
			}
		}
	}
	/** -- fixing averages: during aggregation, we don't know how many
	 *     records will pass the filters*/

	pipeline_aggregations_fixavgs(&pipeline, (double) nr_kds_pass, sgl_agg);

	/* -- umber of records returned */

	if (*nr_kds != 0 && pipeline.num_aggr_filters == 0)
	{
		*nr_kds = nr_kds_pass; /** returning passing rcx */
	}
	else if (*nr_kds != 0 && pipeline.num_aggr_filters > 0)
	{
		*nr_kds = 1; /** in aggregation, we return only one record */
	} /** else, we leave it at 0 */

	rc = 0;
exit:

	/** -- Freeing allocated memory for temporary buffers */

	for (i = 0; i < nr_kds_param; i++)
	{
		if (sg_iovs_keys_iter && sg_iovs_keys_iter[i].iov_buf)
		{
			free(sg_iovs_keys_iter[i].iov_buf);
		}
		for (j = 0; j < nr_iods_param; j++)
		{
			l = i*nr_iods_param+j;
			if (sg_iovs_recx_iter && sg_iovs_recx_iter[l].iov_buf)
			{
				free(sg_iovs_recx_iter[l].iov_buf);
			}
		}
	}
	if (kds_iter)
	{
		free(kds_iter);
	}
	if (sg_iovs_keys_iter)
	{
		free(sg_iovs_keys_iter);
	}
	if (sgl_keys_iter)
	{
		free(sgl_keys_iter);
	}
	if (sg_iovs_recx_iter)
	{
		free(sg_iovs_recx_iter);
	}
	if (sgl_recx_iter)
	{
		free(sgl_recx_iter);
	}

	return rc;
}
