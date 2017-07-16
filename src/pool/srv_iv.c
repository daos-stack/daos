/**
 * (C) Copyright 2017 Intel Corporation.
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
 * ds_pool: Pool IV cache
 */

#define DDSUBSYS	DDFAC(pool)
#include <daos_srv/pool.h>
#include <daos/pool_map.h>
#include "srv_internal.h"
#include <daos_srv/iv.h>

static int
pool_buf_iv_alloc(struct ds_iv_key *iv_key, void *data,
		  d_sg_list_t *sgl)
{
	unsigned int buf_size;
	uint32_t     pool_nr;
	int	     rc;

	/* XXX Let's decide how to get pool map size from
	 * key or data later
	 */
	crt_group_size(NULL, &pool_nr);
	buf_size = pool_buf_size((int)pool_nr * 2);

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, buf_size);
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = buf_size;
free:
	if (rc)
		daos_sgl_fini(sgl, true);

	return rc;
}

static int
pool_buf_iv_get(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	if (sgl->sg_iovs != NULL && sgl->sg_iovs[0].iov_buf == NULL) {
		D_ERROR("pool map does not exist locally\n");
		return -DER_NONEXIST;
	}

	daos_sgl_copy(sgl, &entry->value);

	return 0;
}

static int
pool_buf_iv_put(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	if (sgl->sg_iovs == NULL || sgl->sg_iovs[0].iov_buf == NULL) {
		D_ERROR("pool map does not exist locally\n");
		return -DER_NONEXIST;
	}

	return 0;
}

static int
pool_buf_iv_destroy(d_sg_list_t *sgl)
{
	daos_sgl_fini(sgl, true);
	return 0;
}

struct ds_iv_entry_ops pool_iv_ops = {
	.iv_ent_alloc	= pool_buf_iv_alloc,
	.iv_ent_get	= pool_buf_iv_get,
	.iv_ent_put	= pool_buf_iv_put,
	.iv_ent_destroy = pool_buf_iv_destroy,
};

int
ds_pool_iv_init(void)
{
	int rc;

	rc = ds_iv_key_type_register(IV_POOL_MAP, &pool_iv_ops);

	return rc;
}

int
ds_pool_iv_fini(void)
{
	ds_iv_key_type_unregister(IV_POOL_MAP);
	return 0;
}

