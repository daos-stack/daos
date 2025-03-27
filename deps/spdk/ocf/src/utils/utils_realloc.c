/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "utils_realloc.h"
#include "ocf_env.h"

#define OCF_REALLOC_K_MAX	(128 * KiB)

static int _ocf_realloc_with_cp(void **mem, size_t size, size_t count,
		size_t *limit, bool cp)
{
	size_t alloc_size = size * count;

	ENV_BUG_ON(!mem);
	ENV_BUG_ON(!limit);

	if (size && count) {
		/* Memory reallocation request */

		if (alloc_size > *limit) {
			/* The space is not enough, we need allocate new one */

			void *new_mem;

			if (alloc_size > OCF_REALLOC_K_MAX)
				new_mem = env_vzalloc(alloc_size);
			else
				new_mem = env_zalloc(alloc_size, ENV_MEM_NOIO);

			if (!new_mem) {
				/* Allocation error */
				return -1;
			}

			/* Free previous memory */
			if (*mem) {
				if (cp) {
					/* copy previous content into new allocated
					 * memory
					 */
					ENV_BUG_ON(env_memcpy(new_mem, alloc_size, *mem, *limit));

				}

				if (*limit > OCF_REALLOC_K_MAX)
					env_vfree(*mem);
				else
					env_free(*mem);
			}

			/* Update limit */
			*limit = alloc_size;

			/* Update memory pointer */
			*mem = new_mem;

			return 0;
		}

		/*
		 * The memory space is enough, no action required.
		 * Space after allocation set to '0'
		 */
		if (cp)
			ENV_BUG_ON(env_memset(*mem + alloc_size, *limit - alloc_size, 0));

		return 0;

	}

	if ((size == 0) && (count == 0)) {

		if ((*mem) && (*limit)) {
			/* Need to free memory */
			if (*limit > OCF_REALLOC_K_MAX)
				env_vfree(*mem);
			else
				env_free(*mem);

			/* Update limit */
			*((size_t *)limit) = 0;
			*mem = NULL;

			return 0;
		}

		if ((!*mem) && (*limit == 0)) {
			/* No allocation before do nothing */
			return 0;

		}
	}

	ENV_BUG();
	return -1;
}

int ocf_realloc(void **mem, size_t size, size_t count, size_t *limit)
{
	return _ocf_realloc_with_cp(mem, size, count, limit, false);
}

int ocf_realloc_cp(void **mem, size_t size, size_t count, size_t *limit)
{
	return _ocf_realloc_with_cp(mem, size, count, limit, true);
}

void ocf_realloc_init(void **mem, size_t *limit)
{
	ENV_BUG_ON(!mem);
	ENV_BUG_ON(!limit);

	*mem = NULL;
	*((size_t *)limit) = 0;
}
