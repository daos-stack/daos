/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <math.h>
#include "pipeline_internal.h"


static void
pipeline_filter_get_data(daos_filter_part_t *part, d_iov_t *dkey,
			 uint32_t nr_iods, daos_iod_t *iods, d_sg_list_t *akeys,
			 uint32_t const_idx, d_iov_t **data_item)
{
	uint32_t i;

	*data_item = NULL;
	if (!strncmp((char *) part->part_type.iov_buf, "DAOS_FILTER_DKEY",
		     part->part_type.iov_len))
	{
		*data_item = dkey;
	}
	else if (!strncmp((char *) part->part_type.iov_buf, "DAOS_FILTER_AKEY",
			  part->part_type.iov_len))
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
	else if (!strncmp((char *) part->part_type.iov_buf, "DAOS_FILTER_CONST",
			  part->part_type.iov_len) &&
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
		    const char *data_type, size_t data_type_s)
{
	size_t cmp_size;
	char *left_s, *right_s;

	left_s  = (char *) d_left->iov_buf;
	right_s = (char *) d_right->iov_buf;
	left_s  = &left_s[offset_left];
	right_s = &right_s[offset_right];
	cmp_size = (size_left <= size_right) ? size_left : size_right;

	/** Typed comparison */

	if (!strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER1", data_type_s))
	{
		signed char *left_i, *right_i;
		left_i  = (signed char *) left_s;
		right_i = (signed char *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER2", data_type_s))
	{
		short int *left_i, *right_i;
		left_i  = (short int *) left_s;
		right_i = (short int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER4", data_type_s))
	{
		int *left_i, *right_i;
		left_i  = (int *) left_s;
		right_i = (int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER8", data_type_s))
	{
		long long int *left_i, *right_i;
		left_i  = (long long int *) left_s;
		right_i = (long long int *) right_s;
		if      (*left_i < *right_i)  return -1;
		else if (*left_i == *right_i) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_REAL4", data_type_s))
	{
		float *left_f, *right_f;
		left_f  = (float *) left_s;
		right_f = (float *) right_s;
		if      (*left_f < *right_f)  return -1;
		else if (*left_f == *right_f) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_REAL8", data_type_s))
	{
		double *left_f, *right_f;
		left_f  = (double *) left_s;
		right_f = (double *) right_s;
		if      (*left_f < *right_f)  return -1;
		else if (*left_f == *right_f) return 0;
		else                          return 1;
	}
	else if (!strncmp(data_type, "DAOS_FILTER_TYPE_STRING", data_type_s))
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

	if (!strncmp((char *) right->part_type.iov_buf, "DAOS_FILTER_CONST",
		     right->part_type.iov_len) &&
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
		if (strncmp((char *) left->data_type.iov_buf,
			    (char *) right->data_type.iov_buf,
			    left->data_type.iov_len))
		{
			return -DER_INVAL;
		}
		if (!strncmp((char *) part->part_type.iov_buf,
			     "DAOS_FILTER_FUNC_LIKE",
			     part->part_type.iov_len))
		{
			/** filter 'LIKE' only works for strings */
			if (strncmp((char *) left->data_type.iov_buf,
				    "DAOS_FILTER_TYPE_STRING",
				    left->data_type.iov_len) ||
			    strncmp((char *) right->data_type.iov_buf,
				    "DAOS_FILTER_TYPE_STRING",
				    right->data_type.iov_len))
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
						 (char *) left->data_type.iov_buf,
						 left->data_type.iov_len);
		}

		if (!strncmp((char *) part->part_type.iov_buf,
			     "DAOS_FILTER_FUNC_EQ",
			     part->part_type.iov_len) ||
		    !strncmp((char *) part->part_type.iov_buf,
			     "DAOS_FILTER_FUNC_IN",
			     part->part_type.iov_len) ||
		    !strncmp((char *) part->part_type.iov_buf,
			     "DAOS_FILTER_FUNC_LIKE",
			     part->part_type.iov_len))
		{
			if (rc == 0) return 0;
		}
		else if (!strncmp((char *) part->part_type.iov_buf,
				  "DAOS_FILTER_FUNC_NE",
				  part->part_type.iov_len))
		{
			if (rc != 0) return 0;
		}
		else if (!strncmp((char *) part->part_type.iov_buf,
				  "DAOS_FILTER_FUNC_LT",
				  part->part_type.iov_len))
		{
			if (rc < 0)  return 0;
		}
		else if (!strncmp((char *) part->part_type.iov_buf,
				  "DAOS_FILTER_FUNC_LE",
				  part->part_type.iov_len))
		{
			if (rc <= 0) return 0;
		}
		else if (!strncmp((char *) part->part_type.iov_buf,
				  "DAOS_FILTER_FUNC_GE",
				  part->part_type.iov_len))
		{
			if (rc >= 0) return 0;
		}
		else if (!strncmp((char *) part->part_type.iov_buf,
				  "DAOS_FILTER_FUNC_GT",
				  part->part_type.iov_len))
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
	char			*part_type;
	size_t			part_type_s;

	*part_idx += 1;
	part = filter->parts[*part_idx];

	part_type   = (char *) part->part_type.iov_buf;
	part_type_s = part->part_type.iov_len;

	if (!strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_CONST", part_type_s))
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
	char *part_type = (char *) filter->parts[*part_idx]->part_type.iov_buf;
	size_t part_type_s = filter->parts[*part_idx]->part_type.iov_len;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))
	{
		return pipeline_filter_func(filter, dkey, *nr_iods, iods, akeys,
					    part_idx);
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s))
	{
		*part_idx += 1;
		return pipeline_filter_isnull(filter, dkey, *nr_iods, iods,
					      akeys, part_idx);
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s))
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
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s))
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
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s))
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
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s))
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

int
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
read_iov_as_double(char *data, size_t offset, char *type, size_t type_s,
		   double *result)
{
	char *data_ = &data[offset];

	if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER1", type_s))
	{
		signed char *val = (signed char *) data_;
		*result = (double) *val;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER2", type_s))
	{
		short int *val = (short int *) data_;
		*result = (double) *val;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER4", type_s))
	{
		int *val = (int *) data_;
		*result = (double) *val;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER8", type_s))
	{
		long long int *val = (long long int *) data_;
		*result = (double) *val;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL4", type_s))
	{
		float *val = (float *) data_;
		*result = (double) *val;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL8", type_s))
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
	char			*part_type = (char *) part->part_type.iov_buf;
	size_t			part_type_s = part->part_type.iov_len;
	double			total_rec;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_AKEY", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_CONST", part_type_s))
	{
		d_iov_t *data;

		pipeline_filter_get_data(part, dkey, *nr_iods, iods, akeys, 0,
					 &data);
		if ((rc = read_iov_as_double((char *) data->iov_buf,
					     part->data_offset,
					     (char  *) part->data_type.iov_buf,
					     part->data_type.iov_len,
					     total)))
		{
			return rc; /** error */
		}
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s))
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
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
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

int
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

void
pipeline_aggregations_fixavgs(daos_pipeline_t *pipeline, double total,
			      d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	char			*part_type;
	size_t			part_type_s;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part_type = (char *) pipeline->aggr_filters[i]->parts[0]->part_type.iov_buf;
		part_type_s = pipeline->aggr_filters[i]->parts[0]->part_type.iov_len;
		if (!strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
		{
			buf = (double *) sgl_agg[i].sg_iovs->iov_buf;
			*buf = *buf / total;
		}
	}
}

void
pipeline_aggregations_init(daos_pipeline_t *pipeline, d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	daos_filter_part_t	*part;
	char			*part_type;
	size_t			part_type_s;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part      = pipeline->aggr_filters[i]->parts[0];
		buf       = (double *) sgl_agg[i].sg_iovs->iov_buf;
		part_type   = (char *) part->part_type.iov_buf;
		part_type_s = part->part_type.iov_len;

		if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
		{
			*buf = -INFINITY;
		}
		else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
		{
			*buf = INFINITY;
		}
		else
		{
			*buf = 0;
		}
	}
}
