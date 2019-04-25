/**
 * (C) Copyright 2019 Intel Corporation.
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
 *
 * Helper library for low level tests (e.g. btree/evtree)
 */
#include "utest_common.h"

struct utest_context {
	char			uc_pool_name[UTEST_POOL_NAME_MAX + 1];
	struct umem_instance	uc_umm;
	struct umem_attr	uc_uma;
	umem_id_t		uc_root;
};

struct utest_root {
	uint32_t	ur_class;
	uint32_t	ur_ref_cnt;
	size_t		ur_root_size;
	uint64_t	ur_root[0];
};

int
utest_tx_begin(struct utest_context *utx)
{
	if (!umem_has_tx(&utx->uc_umm))
		return 0;

	return umem_tx_begin(&utx->uc_umm, NULL);
}

int
utest_tx_end(struct utest_context *utx, int rc)
{
	if (!umem_has_tx(&utx->uc_umm))
		return rc;

	if (rc != 0)
		return umem_tx_abort(&utx->uc_umm, rc);

	return umem_tx_commit(&utx->uc_umm);
}

static int
utest_tx_add(struct utest_context *utx, void *ptr, size_t size)
{
	if (!umem_has_tx(&utx->uc_umm))
		return 0;

	return umem_tx_add_ptr(&utx->uc_umm, ptr, size);
}

#define utest_tx_add_ptr(utx, ptr) utest_tx_add(utx, ptr, sizeof(*ptr))

int
utest_pmem_create(const char *name, size_t pool_size, size_t root_size,
		  struct utest_context **utx)
{
	struct utest_context	*ctx;
	struct utest_root	*root;
	int			 rc;

	if (strnlen(name, UTEST_POOL_NAME_MAX + 1) > UTEST_POOL_NAME_MAX)
		return -DER_INVAL;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	strcpy(ctx->uc_pool_name, name);
	ctx->uc_uma.uma_id = UMEM_CLASS_PMEM;
	ctx->uc_uma.uma_pool = pmemobj_create(name, "utest_pool", pool_size,
					      0666);

	if (ctx->uc_uma.uma_pool == NULL) {
		perror("Utest pmem pool couldn't be created");
		rc = -DER_NOMEM;
		goto free_ctx;
	}

	ctx->uc_root = pmemobj_root(ctx->uc_uma.uma_pool,
				 sizeof(*root) + root_size);
	if (OID_IS_NULL(ctx->uc_root)) {
		perror("Could not get pmem root");
		rc = -DER_MISC;
		goto destroy;
	}


	umem_class_init(&ctx->uc_uma, &ctx->uc_umm);

	root = umem_id2ptr(&ctx->uc_umm, ctx->uc_root);

	rc = utest_tx_begin(ctx);
	if (rc != 0)
		goto destroy;

	rc = utest_tx_add_ptr(ctx, root);
	if (rc != 0)
		goto end;

	root->ur_class = UMEM_CLASS_PMEM;
	root->ur_root_size = root_size;
	root->ur_ref_cnt = 1;
end:
	rc = utest_tx_end(ctx, rc);
	if (rc != 0)
		goto destroy;

	*utx = ctx;
	return 0;
destroy:
	pmemobj_close(ctx->uc_uma.uma_pool);
	remove(ctx->uc_pool_name);
free_ctx:
	D_FREE(ctx);
	return rc;
}

int
utest_vmem_create(size_t root_size, struct utest_context **utx)
{
	struct utest_context	*ctx;
	struct utest_root	*root;
	int			 rc = 0;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	ctx->uc_uma.uma_id = UMEM_CLASS_VMEM;

	umem_class_init(&ctx->uc_uma, &ctx->uc_umm);

	ctx->uc_root = umem_zalloc(&ctx->uc_umm, sizeof(*root) + root_size);

	if (UMMID_IS_NULL(ctx->uc_root)) {
		rc = -DER_NOMEM;
		goto free_ctx;
	}

	root = umem_id2ptr(&ctx->uc_umm, ctx->uc_root);

	root->ur_class = UMEM_CLASS_VMEM;
	root->ur_root_size = root_size;
	root->ur_ref_cnt = 1;

	*utx = ctx;

	return 0;

free_ctx:
	D_FREE(ctx);
	return rc;
}

int
utest_utx_destroy(struct utest_context *utx)
{
	struct utest_root	*root;
	int			 refcnt = 0;
	int			 rc = 0;

	root = umem_id2ptr(&utx->uc_umm, utx->uc_root);

	if (utx->uc_uma.uma_id == UMEM_CLASS_VMEM) {
		root->ur_ref_cnt--;
		if (root->ur_ref_cnt == 0) {
			umem_free(&utx->uc_umm, utx->uc_root);
			D_FREE(utx);
		}
		return 0;
	}

	/* Ok, PMEM is a bit more complicated */
	rc = utest_tx_begin(utx);
	if (rc != 0) {
		D_ERROR("Problem in tx begin\n");
		return rc;
	}

	rc = utest_tx_add_ptr(utx, root);
	if (rc != 0) {
		D_ERROR("Problem in tx add\n");
		goto end;
	}

	root->ur_ref_cnt--;
	refcnt = root->ur_ref_cnt;
end:
	rc = utest_tx_end(utx, rc);
	if (rc != 0) {
		D_ERROR("Problem in tx end\n");
		return rc;
	}

	if (refcnt != 0)
		return 0;

	pmemobj_close(utx->uc_uma.uma_pool);
	remove(utx->uc_pool_name);
	D_FREE(utx);
	return 0;
}

void *
utest_utx2root(struct utest_context *utx)
{
	struct utest_root	*root;

	root = umem_id2ptr(&utx->uc_umm, utx->uc_root);

	return &root->ur_root[0];
}

umem_id_t
utest_utx2rootmmid(struct utest_context *utx)
{
	return utx->uc_root;
}

int
utest_alloc(struct utest_context *utx, umem_id_t *mmid, size_t size,
	    utest_init_cb cb, const void *cb_arg)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	*mmid = umem_alloc(&utx->uc_umm, size);
	if (UMMID_IS_NULL(*mmid)) {
		rc = -DER_NOMEM;
		goto end;
	}

	if (cb)
		cb(umem_id2ptr(&utx->uc_umm, *mmid), size, cb_arg);
end:
	return utest_tx_end(utx, rc);
}

int
utest_alloc_off(struct utest_context *utx, umem_off_t *off, size_t size,
		utest_init_cb cb, const void *cb_arg)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	*off = umem_alloc_off(&utx->uc_umm, size);
	if (UMOFF_IS_NULL(*off)) {
		rc = -DER_NOMEM;
		goto end;
	}

	if (cb)
		cb(umem_off2ptr(&utx->uc_umm, *off), size, cb_arg);
end:
	return utest_tx_end(utx, rc);
}

int
utest_free(struct utest_context *utx, umem_id_t mmid)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	rc = umem_free(&utx->uc_umm, mmid);

	return utest_tx_end(utx, rc);
}

int
utest_free_off(struct utest_context *utx, umem_off_t umoff)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	rc = umem_free_off(&utx->uc_umm, umoff);

	return utest_tx_end(utx, rc);
}

struct umem_instance *
utest_utx2umm(struct utest_context *utx)
{
	return &utx->uc_umm;
}

struct umem_attr *
utest_utx2uma(struct utest_context *utx)
{
	return &utx->uc_uma;
}
