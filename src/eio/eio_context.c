/**
 * (C) Copyright 2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(eio)

#include "eio_internal.h"

int
eio_ioctxt_create(uuid_t uuid, struct eio_xs_context *xs_ctxt)
{
	/* TODO create SPDK blob, update per-server pool table */
	return 0;
}

int
eio_ioctxt_open(struct eio_io_context **pctxt, struct eio_xs_context *xs_ctxt,
		struct umem_instance *umem, uuid_t uuid)
{
	struct eio_io_context *ctxt;
	umem_id_t root_oid;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	/* TODO lookup per-server pool table, open SPDK blob */
	ctxt->eic_umem = umem;
	root_oid = pmemobj_root(umem->umm_u.pmem_pool, 0);
	D_ASSERT(!UMMID_IS_NULL(root_oid));
	ctxt->eic_pmempool_uuid = root_oid.pool_uuid_lo;
	ctxt->eic_blob = NULL;
	ctxt->eic_xs_ctxt = xs_ctxt;

	*pctxt = ctxt;
	return 0;
}

void
eio_ioctxt_close(struct eio_io_context *ctxt)
{
	/* TODO close SPDK blob */
	D_FREE_PTR(ctxt);
}
