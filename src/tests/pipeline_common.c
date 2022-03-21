
#include "pipeline_common.h"

static void
free_filters(daos_filter_t **filters, uint32_t num_filters)
{
	uint32_t		i, j, k;
	daos_filter_t		*filter;
	daos_filter_part_t	*part;

	for (i = 0; i < num_filters; i++)
	{
		filter = filters[i];
		free(filter->filter_type.iov_buf);
		for (j = 0; j < filter->num_parts; j++)
		{
			part = filter->parts[j];
			free(part->part_type.iov_buf);
			if (part->data_type.iov_buf_len > 0)
			{
				free(part->data_type.iov_buf);
			}
			if (part->akey.iov_buf_len > 0)
			{
				free(part->akey.iov_buf);
			}
			for (k = 0; k < part->num_constants; k++)
			{
				free(part->constant[k].iov_buf);
			}
			free(part);
		}
		free(filter);
	}
	if (num_filters > 0)
	{
		free(filters);
	}
}

void
free_pipeline(daos_pipeline_t *pipe)
{
	free_filters(pipe->filters, pipe->num_filters);
	free_filters(pipe->aggr_filters, pipe->num_aggr_filters);
	daos_pipeline_init(pipe);
}
