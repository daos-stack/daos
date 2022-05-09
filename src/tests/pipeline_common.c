
#include "pipeline_common.h"

static void
free_filter_data(daos_filter_t **filters, uint32_t nfilters)
{
	uint32_t i, j, k;

	for (i = 0; i < nfilters; i++) {
		for (j = 0; j < filters[i]->num_parts; j++) {
			free(filters[i]->parts[j]->part_type.iov_buf);
			if (filters[i]->parts[j]->data_type.iov_buf_len > 0)
				free(filters[i]->parts[j]->data_type.iov_buf);
			if (filters[i]->parts[j]->akey.iov_buf_len > 0)
				free(filters[i]->parts[j]->akey.iov_buf);
			for (k = 0; k < filters[i]->parts[j]->num_constants; k++) {
				free(filters[i]->parts[j]->constant[k].iov_buf);
			}
			if (filters[i]->parts[j]->num_constants > 0)
				free(filters[i]->parts[j]->constant);
		}
		if (filters[i]->filter_type.iov_len > 0)
			free(filters[i]->filter_type.iov_buf);
	}
}

void
free_pipeline(daos_pipeline_t *pipe)
{
	/** freeing objects allocated by client */
	free_filter_data(pipe->filters, pipe->num_filters);
	free_filter_data(pipe->aggr_filters, pipe->num_aggr_filters);

	/** freeing objects allocated by DAOS */
	daos_pipeline_free(pipe);
}
