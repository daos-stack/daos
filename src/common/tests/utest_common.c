/**
 * (C) Copyright 2019-2023 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "utest_common.h"

struct utest_context {
	char			uc_pool_name[UTEST_POOL_NAME_MAX + 1];
	struct umem_instance	uc_umm;
	struct umem_attr	uc_uma;
	umem_off_t		uc_root;
	daos_size_t		initial_value;
	daos_size_t		prev_value;
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
		  struct umem_store *store, struct utest_context **utx)
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
	if (store)
		ctx->uc_uma.uma_id = umempobj_backend_type2class_id(store->store_type);
	else
		ctx->uc_uma.uma_id = UMEM_CLASS_PMEM;

	ctx->uc_uma.uma_pool = umempobj_create(name, "utest_pool",
				UMEMPOBJ_ENABLE_STATS, pool_size, 0666, store);

	if (ctx->uc_uma.uma_pool == NULL) {
		perror("Utest pmem pool couldn't be created");
		rc = -DER_NOMEM;
		goto free_ctx;
	}

	root = umempobj_get_rootptr(ctx->uc_uma.uma_pool,
				sizeof(*root) + root_size);
	if (root == NULL) {
		perror("Could not get pmem root");
		rc = -DER_MISC;
		goto destroy;
	}

	umem_class_init(&ctx->uc_uma, &ctx->uc_umm);

	ctx->uc_root = umem_ptr2off(&ctx->uc_umm, root);

	rc = utest_tx_begin(ctx);
	if (rc != 0)
		goto destroy;

	rc = utest_tx_add_ptr(ctx, root);
	if (rc != 0)
		goto end;

	root->ur_class = ctx->uc_umm.umm_id;
	root->ur_root_size = root_size;
	root->ur_ref_cnt = 1;
end:
	rc = utest_tx_end(ctx, rc);
	if (rc != 0)
		goto destroy;

	*utx = ctx;
	return 0;
destroy:
	umempobj_close(ctx->uc_uma.uma_pool);
	if (remove(ctx->uc_pool_name) != 0)
		D_ERROR("Failed to remove %s: %s\n", ctx->uc_pool_name, strerror(errno));
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

	if (UMOFF_IS_NULL(ctx->uc_root)) {
		rc = ctx->uc_umm.umm_nospc_rc;
		goto free_ctx;
	}

	root = umem_off2ptr(&ctx->uc_umm, ctx->uc_root);

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

	root = umem_off2ptr(&utx->uc_umm, utx->uc_root);

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

	umempobj_close(utx->uc_uma.uma_pool);
	if (remove(utx->uc_pool_name) != 0) {
		D_ERROR("Failed to remove %s: %s\n", utx->uc_pool_name, strerror(errno));
		rc = -DER_IO;
	}
	D_FREE(utx);
	return rc;
}

void *
utest_utx2root(struct utest_context *utx)
{
	struct utest_root	*root;

	root = umem_off2ptr(&utx->uc_umm, utx->uc_root);

	return &root->ur_root[0];
}

umem_off_t
utest_utx2rootoff(struct utest_context *utx)
{
	return utx->uc_root;
}

int
utest_alloc(struct utest_context *utx, umem_off_t *off, size_t size,
	    utest_init_cb cb, const void *cb_arg)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	*off = umem_alloc(&utx->uc_umm, size);
	if (UMOFF_IS_NULL(*off)) {
		rc = utx->uc_umm.umm_nospc_rc;
		goto end;
	}

	if (cb)
		cb(umem_off2ptr(&utx->uc_umm, *off), size, cb_arg);
end:
	return utest_tx_end(utx, rc);
}

int
utest_free(struct utest_context *utx, umem_off_t umoff)
{
	int	rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		return rc;

	rc = umem_free(&utx->uc_umm, umoff);

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

int
utest_get_scm_used_space(struct utest_context *utx,
	daos_size_t *used_space)
{
	int			rc;
	struct umem_instance	*um_ins;

	um_ins = utest_utx2umm(utx);
	if (um_ins->umm_id != UMEM_CLASS_VMEM) {
		rc = umempobj_get_heapusage(um_ins->umm_pool,
			used_space);
	} else {
		/* VMEM . Just return zero */
		rc = 0;
	}
	return rc;
}

int
utest_sync_mem_status(struct utest_context	*utx)
{
	int			rc;
	daos_size_t		scm_used;
	struct umem_instance	*um_ins;

	um_ins = utest_utx2umm(utx);
	if (um_ins->umm_id == UMEM_CLASS_VMEM)
		return 0;
	rc = utest_get_scm_used_space(utx, &scm_used);
	if (utx->initial_value == 0)
		utx->initial_value = scm_used;
	utx->prev_value = scm_used;
	return rc;
}

int
utest_check_mem_increase(struct utest_context *utx)
{
	int			rc;
	daos_size_t		scm_used;
	struct umem_instance	*um_ins;

	um_ins = utest_utx2umm(utx);
	if (um_ins->umm_id == UMEM_CLASS_VMEM)
		return 0;
	rc = utest_get_scm_used_space(utx, &scm_used);
	if (rc) {
		D_ERROR("Get SCM Usage failed\n");
		return rc;
	}
	if (utx->prev_value > scm_used) {
		D_ERROR("SCM Usage not increased\n");
		return 1;
	}
	return 0;
}

int
utest_check_mem_decrease(struct utest_context *utx)
{
	int			rc;
	daos_size_t		scm_used;
	struct umem_instance	*um_ins;

	um_ins = utest_utx2umm(utx);
	if (um_ins->umm_id == UMEM_CLASS_VMEM)
		return 0;
	rc = utest_get_scm_used_space(utx, &scm_used);
	if (rc) {
		D_ERROR("Get SCM Usage failed\n");
		return rc;
	}
	if (utx->prev_value < scm_used) {
		D_ERROR("SCM Usage not decreased\n");
		return 1;
	}
	return 0;
}

int
utest_check_mem_initial_status(struct utest_context *utx)
{
	int			rc;
	daos_size_t		scm_used;
	struct umem_instance	*um_ins;

	um_ins = utest_utx2umm(utx);
	if (um_ins->umm_id == UMEM_CLASS_VMEM)
		return 0;
	rc = utest_get_scm_used_space(utx, &scm_used);
	if (rc) {
		D_ERROR("Get SCM Usage failed\n");
		return rc;
	}
	if (utx->initial_value != scm_used) {
		D_ERROR("SCM not freed up in full\n");
		return 1;
	}
	return 0;
}

/*
 * A failed allocation with UMEM_FLAG_NO_ABORT must leave the transaction
 * alive so in-tx fallbacks (e.g. vos_dtx_reuse_cmt_blob) keep working;
 * if without the flag, the failure aborts the TX and any further tx_add
 * is a fatal usage error in the allocator.
 */
int
utest_tx_fail_no_abort(struct utest_context *utx)
{
	struct umem_instance *umm = utest_utx2umm(utx);
	int                  *value;
	umem_off_t            umoff = UMOFF_NULL;
	umem_off_t            huge;
	int                   rc;

	rc = utest_tx_begin(utx);
	if (rc != 0)
		goto done;

	/* Larger than the pool: must fail, but must NOT abort the TX */
	huge = umem_alloc_verb(umm, UMEM_FLAG_ZERO | UMEM_FLAG_NO_ABORT, POOL_SIZE,
			       UMEM_DEFAULT_MBKT_ID);
	if (!UMOFF_IS_NULL(huge)) {
		D_ERROR("huge allocation unexpectedly succeeded\n");
		D_GOTO(end, rc = 1);
	}

	/* TX still alive: alloc + snapshot + write must all work */
	umoff = umem_zalloc(umm, 4);
	if (UMOFF_IS_NULL(umoff)) {
		D_ERROR("small alloc failed after NO_ABORT failure\n");
		D_GOTO(end, rc = 2);
	}

	value = umem_off2ptr(umm, umoff);

	rc = umem_tx_xadd_ptr(umm, value, (uint64_t)(-2), UMEM_FLAG_NO_ABORT);
	if (rc == 0) {
		D_ERROR("large sized add_ptr() should fail\n");
		D_GOTO(end, rc = 3);
	}

	rc = umem_tx_add_ptr(umm, value, 4);
	if (rc != 0) {
		D_ERROR("tx_add_ptr failed after NO_ABORT failure\n");
		D_GOTO(end, rc = 4);
	}

	*value = 42;

	rc = umem_free(umm, umoff);

end:
	rc = utest_tx_end(utx, rc);

done:
	return rc;
}
