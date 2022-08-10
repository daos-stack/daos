/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

 #include <sys/types.h>
 #include <sys/stat.h>
 #include <daos_types.h>
 #include <daos_obj.h>
 #include <fcntl.h>
 #include <daos_srv/vos.h>
 #include <pmfs/vos_target_engine.h>

 #define USING_SCM_DEFAULT_SIZE (1ULL << 30)
 #define VOS_SINGLE_NODE_ENGIN
D_LIST_HEAD(g_container_list);
D_LIST_HEAD(g_pool_list);
D_LIST_HEAD(g_pool_fini_list);
static uint32_t count;
static pthread_mutex_t pmfs_cnt_lock = PTHREAD_MUTEX_INITIALIZER;
enum {
	/* Nothing has been initialized */
	VTS_INIT_NONE,
	/* Debug system has been initialized */
	VTS_INIT_DEBUG,
	/* Modules have been loaded */
	VTS_INIT_MODULE,
	/* Pool has been created */
	VTS_INIT_POOL,
};

static struct pmfs_pool *
check_valid_pool(struct pmfs_pool *pmfs_pool)
{
	if (!pmfs_pool || !pmfs_pool->tsc_pmem_file) {
		return NULL;
	}

	if (strncmp(pmfs_pool->tsc_pmem_file, "/mnt/daos/", 10)) {
		return NULL;
	}

	D_INFO("pool = %s\r\n", pmfs_pool->tsc_pmem_file);
	return pmfs_pool;
}

static int
engine_cont_single_node_init(struct pmfs_pool *pmfs_pool,
			     bool tsc_skip_cont_create)
{
	daos_handle_t coh = DAOS_HDL_INVAL;
	struct pmfs_container pmfs_container = { };
	int rc;

	uuid_generate(pmfs_container.tsc_cont_uuid);
	if (!tsc_skip_cont_create) {
		rc = vos_cont_create(pmfs_pool->tsc_poh,
				     pmfs_container.tsc_cont_uuid);
		if (rc) {
			return rc;
		}
	}

	rc = vos_cont_open(pmfs_pool->tsc_poh,
			   pmfs_container.tsc_cont_uuid, &coh);
	if (rc) {
		return rc;
	}

	pmfs_container.tsc_coh = coh;
	pmfs_container.is_open = true;

	d_list_add(&pmfs_container.cl, &g_container_list);

	return rc;
}

static void
engine_cont_single_node_fini(struct pmfs_container *pmfs_container)
{
	if (pmfs_container->is_open) {
		vos_cont_close(pmfs_container->tsc_coh);
	}
	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

static void
engine_cont_fini(struct pmfs_pool *pmfs_pool)
{
	struct pmfs_container *pmfs_container, *tmp_cont;

	pmfs_pool->pmfs_container.cl = g_container_list;

	if (!check_valid_pool(pmfs_pool) || d_list_empty(&pmfs_pool->pmfs_container.cl)) {
		return;
	}

	d_list_for_each_entry_safe(pmfs_container, tmp_cont,
				   &g_container_list, cl) {
		d_list_del_init(&pmfs_container->cl);
		engine_cont_single_node_fini(pmfs_container);
		D_FREE(pmfs_container);
	}
}

static int
engine_cont_init(struct pmfs_pool *pmfs_pool)
{
	int rc = 0;

	/* That allows no container initialization */

	rc = engine_cont_single_node_init(pmfs_pool,
					  pmfs_pool->tsc_skip_cont_create);
	if (rc) {
		goto exit;
	}

	pmfs_pool->pmfs_container.cl = g_container_list;
	D_INFO("container create success\r\n");
	return 0;
exit:
	engine_cont_fini(pmfs_pool);
	return rc;
}

int
engine_pool_single_node_init(struct pmfs_pool *pmfs_pool,
			     bool tsc_skip_pool_create)
{
	daos_handle_t poh = DAOS_HDL_INVAL;
	char *pmem_file = pmfs_pool->tsc_pmem_file;
	int rc, fd;

	D_INFO("pool single init\r\n");

	/* Set scm size for every pool */
	if (pmfs_pool->tsc_scm_size == 0) {
		pmfs_pool->tsc_scm_size = USING_SCM_DEFAULT_SIZE;
	}

	if (!daos_file_is_dax(pmem_file)) {
		rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (rc < 0) {
			return rc;
		}

		fd = rc;
		rc = fallocate(fd, 0, 0, pmfs_pool->tsc_scm_size);
		if (rc) {
			return rc;
		}
	}

	if (!tsc_skip_pool_create) {
		  /* Use pool size as blob size for this moment. */
		rc = vos_pool_create(pmem_file, pmfs_pool->tsc_pool_uuid, 0,
				     pmfs_pool->tsc_nvme_size, 0, &poh);
		if (rc) {
			return rc;
		}
	} else {
		rc = vos_pool_open(pmem_file, pmfs_pool->tsc_pool_uuid, 0,
				   &poh);
		if (rc) {
			return rc;
		}
	}

	pmfs_pool->tsc_poh = poh;
	d_list_add(&pmfs_pool->pl, &g_pool_fini_list);
	D_INFO("pool single fini\r\n");
	return rc;
}

static void
engine_pool_single_node_fini(struct pmfs_pool *pmfs_pool, bool is_pool_created)
{
	vos_pool_close(pmfs_pool->tsc_poh);
	if (is_pool_created) {
		vos_pool_destroy(pmfs_pool->tsc_pmem_file,
				 pmfs_pool->tsc_pool_uuid);
	}
}

static void
engine_pool_fini(struct pmfs_context *pmfs_ctx)
{
	struct pmfs_pool *pmfs_pool, *tmp_pool;

	if (d_list_empty(&pmfs_ctx->pmfs_pool.pl)) {
		return;
	}

	/* Pool owned list was added by test tool or user*/
	/* Container owned list was rebuilt according to cmds */

	d_list_for_each_entry_safe(pmfs_pool, tmp_pool, &g_pool_fini_list, pl) {
		if (check_valid_pool(pmfs_pool)) {
			d_list_del_init(&pmfs_pool->pl);
			engine_cont_fini(pmfs_pool);
			engine_pool_single_node_fini(pmfs_pool,
						     pmfs_ctx->tsc_skip_pool_create);
			D_FREE(pmfs_pool);
		}
	}
}

static int
pmfs_cont_add(daos_handle_t poh, uuid_t co_uuid, daos_handle_t coh,
	      struct scan_context *ctx)
{
	struct pmfs_container *pmfs_container;
	int rc = 0;

	pthread_mutex_lock(&pmfs_cnt_lock);

	rc = vos_cont_open(poh, co_uuid, &coh);
	if (rc) {
		D_ERROR("daos_cont_open() failed " DF_RC "\n", DP_RC(rc));
		return -1;
	}


	D_ALLOC(pmfs_container, sizeof(struct pmfs_container));
	D_ASSERT(pmfs_container != NULL);

	pmfs_container->tsc_coh = coh;
	pmfs_container->is_open = true;
	uuid_copy(pmfs_container->tsc_cont_uuid, co_uuid);
	d_list_add(&pmfs_container->cl, &g_container_list);
	ctx->cur_cont = *pmfs_container;
	ctx->cur_cont.cl = g_container_list;

	pthread_mutex_unlock(&pmfs_cnt_lock);

	return 0;
}

static int
cont_iter_scan_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		  vos_iter_type_t type, vos_iter_param_t *param,
		  void *cb_arg, unsigned int *acts)
{
	struct scan_context *ctx = cb_arg;
	daos_handle_t coh = DAOS_HDL_INVAL;
	int rc = 0;

	D_ASSERT(type == VOS_ITER_COUUID);

	rc = pmfs_cont_add(ctx->pool_hdl, entry->ie_couuid, coh, ctx);
	if (rc != 0) {
		D_ERROR("Add container '" DF_UUIDF "' failed: " DF_RC "\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
	}

	return rc;
}

int
pmfs_scan_pool(struct scan_context *ctx)
{
	vos_iter_param_t param = { 0 };
	struct vos_iter_anchors anchor = { 0 };
	int rc;

	param.ip_hdl = ctx->pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 NULL, cont_iter_scan_cb, ctx, NULL);
	ctx->cur_cont.cl = g_container_list;

	return rc;
}

void
pmfs_ctx_combine_pool_list(struct pmfs_context *pmfs_ctx)
{
	g_pool_list = pmfs_ctx->pmfs_pool.pl;
}

void
pmfs_combine_pool_fini_list(d_list_t *fini_list)
{
	*fini_list = g_pool_fini_list;
}

struct pmfs_pool *
pmfs_find_pool(const char *pmem)
{
	struct pmfs_pool *pmfs_pool;

	d_list_for_each_entry(pmfs_pool, &g_pool_fini_list, pl) {
		if (strcmp(pmfs_pool->tsc_pmem_file, pmem) == 0) {
			return pmfs_pool;
		}
	}

	return NULL;
}

static int
pmfs_get_key_info(struct pmfs_obj_info *tmp_uoi, vos_iter_entry_t *entry,
		  struct scan_context *ctx)
{
	int rc = 0;

	if (!entry->ie_key.iov_buf_len) {
		rc = -1;
		D_ERROR("Wrong object entry for dkey : " DF_RC "\r\n",
			DP_RC(rc));
	}

	tmp_uoi->nr = ctx->uoi.nr;
	tmp_uoi->len = ctx->uoi.len + (uint64_t)entry->ie_key.iov_buf_len;

	return rc;
}

static int
pmfs_list_keys_info(struct pmfs_obj_info *tmp_uoi, vos_iter_entry_t *entry,
		    struct scan_context *ctx)
{
	int rc = 0;

	if (!entry->ie_key.iov_buf_len) {
		rc = -1;
		D_ERROR("Wrong object entry for dkey : " DF_RC "\r\n",
			DP_RC(rc));
	}

	tmp_uoi->buf = ctx->uoi.buf;
	memcpy((char *)tmp_uoi->buf + ctx->uoi.len, (char *)entry->ie_key.iov_buf,
	       entry->ie_key.iov_buf_len);
	ctx->uoi.len += (uint64_t)entry->ie_key.iov_len;

	tmp_uoi->kds = ctx->uoi.kds;
	tmp_uoi->kds[count - 1].kd_key_len = entry->ie_key.iov_len;
	tmp_uoi->len = ctx->uoi.len;
	return rc;
}

static int
obj_list_iter_get_num_dkeys_cb(daos_handle_t ih, vos_iter_entry_t *entry,
			       vos_iter_type_t type, vos_iter_param_t *param,
			       void *cb_arg, unsigned int *acts)
{
	struct scan_context *ctx = cb_arg;
	struct pmfs_obj_info uoi = { 0 };
	int rc = 0;

	if (type != VOS_ITER_DKEY && type != VOS_ITER_AKEY) {
		return rc;
	}

	ctx->uoi.nr++;
	rc = pmfs_get_key_info(&uoi, entry, ctx);
	if (rc != DER_SUCCESS) {
		D_ERROR("Object get key info failed: " DF_RC
			"\r\n", DP_RC(rc));
	}

	ctx->uoi = uoi;

	return rc;
}

static int
obj_list_iter_list_dkeys_cb(daos_handle_t ih, vos_iter_entry_t *entry,
			    vos_iter_type_t type, vos_iter_param_t *param,
			    void *cb_arg, unsigned int *acts)
{
	struct scan_context *ctx = cb_arg;
	struct pmfs_obj_info  uoi = {0};
	int rc = 0;

	if (type != VOS_ITER_DKEY && type != VOS_ITER_AKEY) {
		return rc;
	}

	count++;
	rc = pmfs_list_keys_info(&uoi, entry, ctx);
	if (rc != DER_SUCCESS) {
		D_ERROR("Object get key info failed: " DF_RC
			"\r\n", DP_RC(rc));
	}

	uoi.nr = count;
	ctx->uoi = uoi;

	return rc;
}

int
pmfs_scan_cont(struct scan_context *ctx, struct pmfs_obj_info **uoi,
	       enum task_op opc)
{
	vos_iter_param_t param = { 0 };
	struct vos_iter_anchors anchors = { 0 };
	vos_iter_type_t type;
	int rc = 0;

	param.ip_hdl = ctx->cur_cont.tsc_coh;
	param.ip_oid = ctx->uoi.oid;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	if (opc == VOS_OBJ_GET_NUM_DKEYS) {
		type = VOS_ITER_DKEY;
		ctx->uoi.nr = 0;
		ctx->uoi.len = 0;
		ctx->uoi.kds = NULL;
		ctx->uoi.buf = NULL;
	} else if (opc == VOS_OBJ_LIST_DKEYS) {
		type = VOS_ITER_DKEY;
		ctx->uoi.len = 0;
		count = 0;
	} else if (opc == VOS_OBJ_PUNCH) {
		type = VOS_ITER_AKEY;
	} else {
		type = VOS_ITER_NONE;
	}

	rc = vos_iterate(&param, type, false, &anchors,
			 opc == VOS_OBJ_GET_NUM_DKEYS ? obj_list_iter_get_num_dkeys_cb :
			 obj_list_iter_list_dkeys_cb, NULL, ctx, NULL);


	if (rc != DER_SUCCESS) {
		D_ERROR("Object scan failed: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	*uoi = &ctx->uoi;

	return 0;
}

static int
engine_pool_init(struct pmfs_context *pmfs_ctx)
{
	struct pmfs_pool *pmfs_pool, *tmp_pool;
	int rc = 0;

	if (d_list_empty(&pmfs_ctx->pmfs_pool.pl)) {
		return -1;
	}

	D_INIT_LIST_HEAD(&g_pool_fini_list);

	d_list_for_each_entry_safe(pmfs_pool, tmp_pool, &g_pool_list, pl) {
		d_list_del(&pmfs_pool->pl);

		if (check_valid_pool(pmfs_pool)) {
			D_INFO("start pool init\r\n");
			rc = engine_pool_single_node_init(pmfs_pool,
							  pmfs_ctx->tsc_skip_pool_create);
			if (rc) {
				D_ERROR("engin_pool_signle_node_init() failed "
					DF_RC "\n", DP_RC(rc));
				goto exit;
			}

			D_INIT_LIST_HEAD(&pmfs_pool->pmfs_container.cl);

			/* For now, no need to add container at initial state of a
			 * pool. Just continue. we can let this just TODO.
			 */

			continue;

			rc = engine_cont_init(pmfs_pool);

			if (rc) {
				D_ERROR("engine_cont_init() failed " DF_RC "\n",
					DP_RC(rc));
				goto exit;
			}
		} else {
			break;
		}

		D_FREE(pmfs_pool);
	}

	D_INFO("pool create success\r\n");
	return 0;
exit:
	pmfs_ctx->pmfs_pool.pl = g_pool_fini_list;
	engine_pool_fini(pmfs_ctx);
	return rc;
}

static void
engine_fini(void)
{
	vos_self_fini();
}

static int
engine_init(void)
{
	return vos_self_init("/mnt/daos", true, -1);
}

/* Containers init are embedded in pool init */
struct vos_target_engine g_vos_target_engine = {
	.vte_name = "VOS_TARGET",
	.vte_init = engine_init,
	.vte_fini = engine_fini,
	.vte_pool_init = engine_pool_init,
	.vte_pool_fini = engine_pool_fini,
};


/* VOS target ctx init */
int
vt_ctx_init(struct pmfs_context *vtx)
{
	int rc;

	vtx->tsc_init = VTS_INIT_NONE;

	vtx->tsc_engine = &g_vos_target_engine;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		goto out;
	}
	vtx->tsc_init = VTS_INIT_DEBUG;

	D_ASSERT(vtx->tsc_engine->vte_init != NULL);
	rc = vtx->tsc_engine->vte_init();
	if (rc) {
		goto out;
	}
	vtx->tsc_init = VTS_INIT_MODULE;

	D_ASSERT(vtx->tsc_engine->vte_pool_init != NULL);
	rc = vtx->tsc_engine->vte_pool_init(vtx);
	if (rc) {
		goto out;
	}
	vtx->tsc_init = VTS_INIT_POOL;

	return 0;
out:
	fprintf(stderr, "Failed to initialize step=%d, rc=%d\n",
		vtx->tsc_init, rc);
	vt_ctx_fini(vtx);
	return rc;
}

/* VOS target ctx fini */
void
vt_ctx_fini(struct pmfs_context *vtx)
{
	switch (vtx->tsc_init) {
	case VTS_INIT_POOL:
		/* Close and destroy pool */
		D_ASSERT(vtx->tsc_engine->vte_pool_fini != NULL);
		vtx->tsc_engine->vte_pool_fini(vtx);
		/* Fall through */
	case VTS_INIT_MODULE:
		/* Finalize module */
		D_ASSERT(vtx->tsc_engine->vte_fini != NULL);
		vtx->tsc_engine->vte_fini();
		/* Fall through */
	case VTS_INIT_DEBUG:
		/* Finalize debug system */
		daos_debug_fini();
	}
}
