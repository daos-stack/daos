/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos_pipeline.h>
#include <daos_api.h>
#include <math.h>

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
		size_t num_parts = 0;
		uint32_t num_operands;

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

		for (p = 0; p < ftr->num_parts; p++) {
			/**
			 * -- Check 2: Check that all parts have a correct
			 *             number of operands and also that the
			 *             number of total parts is correct.
			 */
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
	}

	return 0;
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
