/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_wal.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vos_internal.h>
#include "vts_io.h"

#define WAL_IO_KEYS		31
#define WAL_POOL_REFILLS	3
#define WAL_IO_MULTI_KEYS	10000
#define WAL_OBJ_KEYS		31

/* Define WAL_IO_EXTRA_CHK to one for comprehensive type checking */
#define WAL_IO_EXTRA_CHK	0

static int type_list[] = {
	0,
	DAOS_OT_AKEY_UINT64,
#if WAL_IO_EXTRA_CHK == 1
	DAOS_OT_AKEY_LEXICAL,
	DAOS_OT_DKEY_UINT64,
	DAOS_OT_DKEY_LEXICAL,
	DAOS_OT_MULTI_LEXICAL,
#endif
	DAOS_OT_MULTI_UINT64,
};

static int num_keys;
static enum daos_otype_t otype;

struct io_test_flag {
	char		*tf_str;
	unsigned int	 tf_bits;
};

static struct io_test_flag io_test_flags[] = {
	{ .tf_str = "default", .tf_bits = 0, },
	{ .tf_str = "ZC", .tf_bits = TF_ZERO_COPY, }, /* atomic_copy */
	{ .tf_str = "extent", .tf_bits = TF_REC_EXT, },
	{ .tf_str = "ZC + extent", .tf_bits = TF_ZERO_COPY | TF_REC_EXT, },
	{ .tf_str = NULL, },
};

/* mirror of enum in vos/tests/vts_common.c */
enum {
	TCX_NONE,
	TCX_PO_CREATE_OPEN,
	TCX_CO_CREATE,
	TCX_CO_OPEN,
	TCX_READY,
};

struct wal_test_args {
	char	*wta_clone;
	void	*wta_buf;
	int	 wta_buf_sz;
	bool	 wta_no_replay;
	bool	 wta_checkpoint;
};

static int
teardown_wal_test(void **state)
{
	struct wal_test_args	*arg = *state;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup issue\n");
		return 0;
	}

	unlink(arg->wta_clone);
	D_FREE(arg->wta_clone);
	D_FREE(arg->wta_buf);
	D_FREE(arg);
	return 0;
}

static int
setup_wal_test(void **state)
{
	struct wal_test_args	*arg = NULL;
	char			*pool_name;
	int			 rc;

	D_ALLOC(arg, sizeof(struct wal_test_args));
	if (arg == NULL)
		return -1;

	arg->wta_buf_sz = (32UL << 20);	/* 32MB */
	D_ALLOC(arg->wta_buf, arg->wta_buf_sz);
	if (arg->wta_buf == NULL)
		goto error;

	D_ASPRINTF(arg->wta_clone, "%s/pool_clone", vos_path);
	if (arg->wta_clone == NULL)
		goto error;

	rc = vts_pool_fallocate(&pool_name);
	if (rc)
		goto error;

	rc = rename(pool_name, arg->wta_clone);
	if (rc) {
		unlink(pool_name);
		free(pool_name);
		goto error;
	}
	free(pool_name);

	*state = arg;
	return 0;
error:
	D_FREE(arg->wta_clone);
	D_FREE(arg->wta_buf);
	D_FREE(arg);
	return -1;
}

static int
copy_pool_file(struct wal_test_args *arg, const char *src_pool, const char *dst_pool)
{
	struct stat	lstat;
	uint64_t	copy_sz, left;
	int		src_fd, dst_fd, rc;

	rc = stat(src_pool, &lstat);
	if (rc != 0) {
		D_ERROR("Stat source pool:%s failed. %s\n", src_pool, strerror(errno));
		return -1;
	}

	src_fd = open(src_pool, O_RDONLY);
	if (src_fd < 0) {
		D_ERROR("Open source pool:%s failed. %s\n", src_pool, strerror(errno));
		return -1;
	}

	dst_fd = open(dst_pool, O_WRONLY);
	if (dst_fd < 0) {
		D_ERROR("Open dest pool:%s failed. %s\n", dst_pool, strerror(errno));
		close(src_fd);
		return -1;
	}

	left = lstat.st_size;
	while (left > 0) {
		ssize_t	read_sz;

		copy_sz = min(left, arg->wta_buf_sz);

		read_sz = read(src_fd, arg->wta_buf, copy_sz);
		if (read_sz < copy_sz) {
			D_ERROR("Failed to read "DF_U64" bytes from source pool:%s. %s\n",
				copy_sz, src_pool, strerror(errno));
			rc = -1;
			break;
		}

		read_sz = write(dst_fd, arg->wta_buf, copy_sz);
		if (read_sz < copy_sz) {
			D_ERROR("Failed to write "DF_U64" bytes to dest pool:%s. %s\n",
				copy_sz, dst_pool, strerror(errno));
			rc = -1;
			break;
		}
		left -= copy_sz;
	}

	close(src_fd);
	close(dst_fd);
	return rc;
}

static inline int
save_pool(struct wal_test_args *arg, const char *pool_name)
{
	return copy_pool_file(arg, pool_name, arg->wta_clone);
}

static inline int
restore_pool(struct wal_test_args *arg, const char *pool_name)
{
	return copy_pool_file(arg, arg->wta_clone, pool_name);
}

static inline char *
media2str(unsigned int media)
{
	switch (media) {
	case DAOS_MEDIA_SCM:
		return "SCM";
	case DAOS_MEDIA_NVME:
		return "NVMe";
	default:
		return "Unknown";
	}
}

static int
compare_pool_info(vos_pool_info_t *info1, vos_pool_info_t *info2)
{
	struct vos_pool_space	*vps1 = &info1->pif_space;
	struct vos_pool_space	*vps2 = &info2->pif_space;
	struct vea_attr		*attr1 = &vps1->vps_vea_attr;
	struct vea_attr		*attr2 = &vps2->vps_vea_attr;
	unsigned int		 media;

	if (info1->pif_cont_nr != info2->pif_cont_nr) {
		print_error("cont nr is different, %lu != %lu\n",
			    info1->pif_cont_nr, info2->pif_cont_nr);
		return 1;
	}

	for (media = DAOS_MEDIA_SCM; media < DAOS_MEDIA_MAX; media++) {
		if (vps1->vps_space.s_total[media] != vps2->vps_space.s_total[media]) {
			print_error("Total space for %s is different, %lu != %lu\n",
				    media2str(media), vps1->vps_space.s_total[media],
				    vps2->vps_space.s_total[media]);
			return 1;
		}
		if (vps1->vps_space.s_free[media] != vps2->vps_space.s_free[media]) {
			print_error("Free space for %s is different, %lu != %lu\n",
				    media2str(media), vps1->vps_space.s_free[media],
				    vps2->vps_space.s_free[media]);
			return 1;
		}
	}

	if (memcmp(&vps1->vps_vea_attr, &vps2->vps_vea_attr, sizeof(struct vea_attr))) {
		print_error("VEA attr is different:\n");
		print_error("compat:%u/%u, blk_sz:%u/%u, hdr_blks:%u/%u, large_thresh:%u/%u, "
			    "tot_blks:%lu/%lu, free_blks:%lu/%lu\n",
			    attr1->va_compat, attr2->va_compat, attr1->va_blk_sz, attr2->va_blk_sz,
			    attr1->va_hdr_blks, attr2->va_hdr_blks, attr1->va_large_thresh,
			    attr2->va_large_thresh, attr1->va_tot_blks, attr2->va_tot_blks,
			    attr1->va_free_blks, attr2->va_free_blks);
		return 1;
	}

	return 0;
}

static void
wait_cb(void *arg, uint64_t chkpt_tx, uint64_t *committed_tx)
{
	uint64_t *committed_id = arg;

	*committed_tx = *committed_id;
	return;
}

static void
update_cb(void *arg, uint64_t id, uint32_t used_blocks, uint32_t total_blocks)
{
	uint64_t *committed_id = arg;

	*committed_id = id;
	return;
}

/* Create pool & cont, clear content in tmpfs, open pool by meta blob loading & WAL replay */
static void
wal_tst_pool_cont(void **state)
{
	struct wal_test_args	*arg = *state;
	char			*pool_name;
	uuid_t			 pool_id, cont_id;
	daos_handle_t		 poh, coh;
	vos_pool_info_t		 pool_info1 = { 0 }, pool_info2 = { 0 };
	int			 rc;

	uuid_generate(pool_id);
	uuid_generate(cont_id);

	/* Create VOS pool file */
	rc = vts_pool_fallocate(&pool_name);
	assert_int_equal(rc, 0);

	/* Save the empty pool file */
	rc = save_pool(arg, pool_name);
	assert_int_equal(rc, 0);

	/* Create pool: Create meta & WAL blobs, write meta & WAL header */
	rc = vos_pool_create(pool_name, pool_id, 0, VPOOL_1G, 0, 0, NULL);
	assert_int_equal(rc, 0);

	/* Create cont: write WAL */
	rc = vos_pool_open(pool_name, pool_id, 0, &poh);
	assert_int_equal(rc, 0);

	rc = vos_cont_create(poh, cont_id);
	assert_int_equal(rc, 0);

	/* Query the pool info before restart */
	rc = vos_pool_query(poh, &pool_info1);
	assert_rc_equal(rc, 0);

	/* checkpoint pool */
	if (arg->wta_checkpoint) {
		struct umem_store	*store;
		uint64_t		 committed_id;

		vos_pool_checkpoint_init(poh, update_cb, wait_cb, &committed_id, &store);
		rc = vos_pool_checkpoint(poh);
		assert_rc_equal(rc, 0);
		vos_pool_checkpoint_fini(poh);
	}

	rc = vos_pool_close(poh);
	assert_int_equal(rc, 0);

	/* Restore pool content from the empty clone */
	rc = restore_pool(arg, pool_name);
	assert_int_equal(rc, 0);

	/* disable WAL replay to verify checkpoint works as expected */
	if (arg->wta_no_replay)
		daos_fail_loc_set(DAOS_WAL_NO_REPLAY | DAOS_FAIL_ALWAYS);

	/* Open pool: Open meta & WAL blobs, load meta & WAL header, replay WAL */
	rc = vos_pool_open(pool_name, pool_id, 0, &poh);
	assert_int_equal(rc, 0);

	/* Open cont */
	rc = vos_cont_open(poh, cont_id, &coh);
	assert_int_equal(rc, 0);

	/* Close cont */
	rc = vos_cont_close(coh);
	assert_rc_equal(rc, 0);

	/* Query pool info */
	rc = vos_pool_query(poh, &pool_info2);
	assert_rc_equal(rc, 0);

	/* Compare pool info */
	rc = compare_pool_info(&pool_info1, &pool_info2);
	assert_rc_equal(rc, 0);

	/* Destroy cont */
	rc = vos_cont_destroy(poh, cont_id);
	assert_rc_equal(rc, 0);

	/* Close pool: Flush meta & WAL header, close meta & WAL blobs */
	rc = vos_pool_close(poh);
	assert_int_equal(rc, 0);

	/* Destroy pool: Destroy meta & WAL blobs */
	rc = vos_pool_destroy(pool_name, pool_id);
	assert_int_equal(rc, 0);

	free(pool_name);
}

/* Re-open pool */
static void
wal_pool_refill(struct io_test_args *arg)
{
	daos_handle_t		poh, coh;
	vos_pool_info_t		pool_info1 = { 0 }, pool_info2 = { 0 };
	int			rc;
	struct vos_test_ctx	*tcx = &arg->ctx;

	rc = vos_cont_close(tcx->tc_co_hdl);
	assert_rc_equal(rc, 0);
	tcx->tc_step = TCX_CO_CREATE;
	poh = tcx->tc_po_hdl;

	/* Query pool usage */
	rc = vos_pool_query(poh, &pool_info1);
	assert_rc_equal(rc, 0);

	/* checkpoint pool if needed */
	if (arg->checkpoint || arg->fail_checkpoint) {
		struct umem_store	*store;
		uint64_t		 committed_id;

		vos_pool_checkpoint_init(poh, update_cb, wait_cb, &committed_id, &store);
		if (arg->fail_checkpoint) {
			daos_fail_loc_set(DAOS_MEM_FAIL_CHECKPOINT | DAOS_FAIL_ALWAYS);
			rc = vos_pool_checkpoint(poh);
			assert_rc_equal(rc, -DER_AGAIN);
		} else {
			rc = vos_pool_checkpoint(poh);
			assert_rc_equal(rc, 0);
		}
		vos_pool_checkpoint_fini(poh);
	}

	/* Close pool: Flush meta & WAL header, close meta & WAL blobs */
	rc = vos_pool_close(poh);
	assert_rc_equal(rc, 0);
	tcx->tc_step = TCX_NONE;

	if (arg->no_replay) {
		D_ASSERT(arg->fail_checkpoint == false);
		D_ASSERT(arg->checkpoint == true);
		daos_fail_loc_set(DAOS_WAL_NO_REPLAY | DAOS_FAIL_ALWAYS);
	}

	if (arg->fail_replay) {
		daos_fail_loc_set(DAOS_WAL_FAIL_REPLAY | DAOS_FAIL_ALWAYS);
		daos_fail_value_set(1000);
		poh = DAOS_HDL_INVAL;
		rc = vos_pool_open(tcx->tc_po_name, tcx->tc_po_uuid, 0, &poh);
		assert_rc_equal(rc, -DER_AGAIN);
		daos_fail_loc_set(0);
	}

	/* Open pool: Open meta & WAL blobs, load meta & WAL header, replay WAL */
	poh = DAOS_HDL_INVAL;
	rc = vos_pool_open(tcx->tc_po_name, tcx->tc_po_uuid, 0, &poh);
	assert_rc_equal(rc, 0);
	tcx->tc_po_hdl = poh;
	tcx->tc_step = TCX_CO_CREATE;

	/* Query pool info */
	rc = vos_pool_query(poh, &pool_info2);
	assert_rc_equal(rc, 0);

	/* Compare pool info */
	rc = compare_pool_info(&pool_info1, &pool_info2);
	assert_rc_equal(rc, 0);

	rc = vos_cont_open(poh, tcx->tc_co_uuid, &coh);
	assert_rc_equal(rc, 0);
	tcx->tc_co_hdl = coh;
	tcx->tc_step = TCX_READY;
}

/* Basic I/O test */
static void
wal_kv_basic(void **state)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey_sv_s[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey_ev_s[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey_sv_l[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey_ev_l[UPDATE_AKEY_SIZE] = { 0 };
	daos_epoch_t		 epc_lo = 100, epoch;
	daos_recx_t		 recx = {.rx_idx = 0, .rx_nr = 1};
	char			*buf_s[2], *buf_l[2];
	char			*buf_v;
	unsigned int		 small_sz = 16, large_sz = 8192;
	int			 i;

	oid = dts_unit_oid_gen(0, 0);

	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(akey_sv_s, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey_sv_l, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey_ev_s, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey_ev_l, UPDATE_AKEY_SIZE, UPDATE_AKEY);

	for (i = 0; i < 2; i++) {
		D_ALLOC(buf_s[i], small_sz);
		assert_non_null(buf_s[i]);

		D_ALLOC(buf_l[i], large_sz);
		assert_non_null(buf_l[i]);
	}
	D_ALLOC(buf_v, large_sz);
	assert_non_null(buf_v);

	/* Update small EV/SV, large EV/SV (located on data blob) */
	epoch = epc_lo;
	update_value(arg, oid, epoch++, 0, dkey, akey_ev_s, DAOS_IOD_ARRAY, small_sz,
		     &recx, buf_s[0]);
	update_value(arg, oid, epoch++, 0, dkey, akey_sv_s, DAOS_IOD_SINGLE, small_sz,
		     &recx, buf_s[1]);
	update_value(arg, oid, epoch++, 0, dkey, akey_ev_l, DAOS_IOD_ARRAY, large_sz,
		     &recx, buf_l[0]);
	update_value(arg, oid, epoch++, 0, dkey, akey_sv_l, DAOS_IOD_SINGLE, large_sz,
		     &recx, buf_l[1]);

	/* Re-open pool and repaly WAL */
	wal_pool_refill(arg);

	/* Verify all values */
	epoch = epc_lo;
	fetch_value(arg, oid, epoch++, 0, dkey, akey_ev_s, DAOS_IOD_ARRAY, small_sz,
		    &recx, buf_v);
	assert_memory_equal(buf_v, buf_s[0], small_sz);

	fetch_value(arg, oid, epoch++, 0, dkey, akey_sv_s, DAOS_IOD_SINGLE, small_sz,
		    &recx, buf_v);
	assert_memory_equal(buf_v, buf_s[1], small_sz);

	fetch_value(arg, oid, epoch++, 0, dkey, akey_ev_l, DAOS_IOD_ARRAY, large_sz,
		    &recx, buf_v);
	assert_memory_equal(buf_v, buf_l[0], large_sz);

	fetch_value(arg, oid, epoch++, 0, dkey, akey_sv_l, DAOS_IOD_SINGLE, large_sz,
		    &recx, buf_v);
	assert_memory_equal(buf_v, buf_l[1], large_sz);

	for (i = 0; i < 2; i++) {
		D_FREE(buf_s[i]);
		D_FREE(buf_l[i]);
	}
	D_FREE(buf_v);
}

static void
wal_kv_large(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_test_ctx	*tcx = &arg->ctx;
	struct umem_instance	*umm;
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey_sv[4][UPDATE_AKEY_SIZE] = { 0 };
	char			 akey_ev[4][UPDATE_AKEY_SIZE] = { 0 };
	daos_epoch_t		 epc_lo = 100, epoch;
	daos_recx_t		 recx = {.rx_idx = 0, .rx_nr = 1};
	char			*bufs[4][2];
	char			*buf_v;
	unsigned int		 sizes[4] = { 1024, 2048, 4096, 8192 };
	unsigned int		 large_sz = 8192;
	int			 i, j, rc;

	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);

	for (i = 0; i < 4; i++) {
		dts_key_gen(akey_sv[i], UPDATE_AKEY_SIZE, UPDATE_AKEY);
		dts_key_gen(akey_ev[i], UPDATE_AKEY_SIZE, UPDATE_AKEY);

		for (j = 0; j < 2; j++) {
			D_ALLOC(bufs[i][j], sizes[i]);
			assert_non_null(bufs[i][j]);
		}
	}
	D_ALLOC(buf_v, large_sz);
	assert_non_null(buf_v);

	/* Update small EV/SV, large EV/SV (located on data blob) */
	umm = &vos_hdl2cont(tcx->tc_co_hdl)->vc_pool->vp_umm;
	rc = umem_tx_begin(umm, vos_txd_get(true));
	assert_rc_equal(rc, 0);

	epoch = epc_lo;
	oid = arg->oid;
	for (i = 0; i < 4; i++) {
		update_value(arg, oid, epoch++, 0, dkey, akey_ev[i], DAOS_IOD_ARRAY,
			     sizes[i], &recx, bufs[i][0]);
		update_value(arg, oid, epoch++, 0, dkey, akey_sv[i], DAOS_IOD_SINGLE,
			     sizes[i], &recx, bufs[i][1]);
	}

	rc = umem_tx_end(umm, 0);
	assert_rc_equal(rc, 0);

	/* Re-open pool and repaly WAL */
	wal_pool_refill(arg);

	/* Verify all values */
	umm = &vos_hdl2cont(tcx->tc_co_hdl)->vc_pool->vp_umm;
	rc = umem_tx_begin(umm, vos_txd_get(true));
	assert_rc_equal(rc, 0);

	epoch = epc_lo;
	for (i = 0; i < 4; i++) {
		fetch_value(arg, oid, epoch++, 0, dkey, akey_ev[i], DAOS_IOD_ARRAY,
			    sizes[i], &recx, buf_v);
		assert_memory_equal(buf_v, bufs[i][0], sizes[i]);

		fetch_value(arg, oid, epoch++, 0, dkey, akey_sv[i], DAOS_IOD_SINGLE,
			    sizes[i], &recx, buf_v);
		assert_memory_equal(buf_v, bufs[i][1], sizes[i]);
	}

	rc = umem_tx_end(umm, 0);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 4; i++)
		for (j = 0; j < 2; j++)
			D_FREE(bufs[i][j]);
	D_FREE(buf_v);
}

static void
wal_args_reset(struct io_test_args *args)
{
	args->oid = gen_oid(otype);
	args->otype = otype;
	if (is_daos_obj_type_set(otype, DAOS_OT_AKEY_UINT64)) {
		args->akey = NULL;
		args->akey_size = sizeof(uint64_t);
	}
	if (is_daos_obj_type_set(otype, DAOS_OT_DKEY_UINT64)) {
		args->dkey = NULL;
		args->dkey_size = sizeof(uint64_t);
	}
}

static int
setup_wal_io(void **state)
{
	int rc;

	rc = setup_io(state);
	if (rc == -1)
		return rc;

	test_args_reset((struct io_test_args *)*state, VPOOL_2G);
	wal_args_reset((struct io_test_args *)*state);
	return 0;
}

static struct io_test_args test_args;

#define MDTEST_META_BLOB_SIZE (256 * 1024 * 1024)
#define MDTEST_VOS_SIZE       (160 * 1024 * 1024)
#define MDTEST_MB_SIZE        (16 * 1024 * 1024)
#define MDTEST_MB_CNT         (MDTEST_META_BLOB_SIZE / MDTEST_MB_SIZE)
#define MDTEST_MB_VOS_CNT     (MDTEST_VOS_SIZE / MDTEST_MB_SIZE)
#define MDTEST_MAX_NEMB_CNT   (MDTEST_MB_VOS_CNT * 8 / 10)
#define MDTEST_MAX_EMB_CNT    (MDTEST_MB_CNT - MDTEST_MAX_NEMB_CNT)

static int
setup_mb_io(void **state)
{
	int rc;

	memset(&test_args, 0, sizeof(test_args));
	rc     = vts_ctx_init_ex(&test_args.ctx, MDTEST_VOS_SIZE, MDTEST_META_BLOB_SIZE);
	*state = (void *)&test_args;
	return rc;
}

static int
teardown_mb_io(void **state)
{
	struct io_test_args *args = (struct io_test_args *)*state;

	vts_ctx_fini(&args->ctx);
	return 0;
}

/* refill:true - perform the pool re-load and refill after every key update/punch */
static int
wal_update_and_fetch_dkey(struct io_test_args *arg, daos_epoch_t update_epoch,
			  daos_epoch_t fetch_epoch, char *update_buf, char *fetch_buf,
			  char *akey_buf, char *dkey_buf, bool refill)
{	int			rc = 0;
	d_iov_t			val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			verify_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	unsigned int		recx_size;
	unsigned int		recx_nr;
	bool			update, fetch;

	/* Setup */
	update = refill || (fetch_buf == NULL);
	fetch = (fetch_buf != NULL);

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	if (arg->ta_flags & TF_REC_EXT) {
		iod.iod_type = DAOS_IOD_ARRAY;
		recx_size = UPDATE_REC_SIZE;
		recx_nr   = UPDATE_BUF_SIZE / UPDATE_REC_SIZE;
	} else {
		iod.iod_type = DAOS_IOD_SINGLE;
		recx_size = UPDATE_BUF_SIZE;
		recx_nr   = 1;
	}
	iod.iod_size	= recx_size;
	rex.rx_nr	= recx_nr;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	/* Generate a new A/D keys and data */
	if (update) {
		if (arg->ta_flags & TF_OVERWRITE) {
			memcpy(dkey_buf, last_dkey, arg->dkey_size);
			memcpy(akey_buf, last_akey, arg->akey_size);
		} else {
			vts_key_gen(dkey_buf, arg->dkey_size, true, arg);
			memcpy(last_dkey, dkey_buf, arg->dkey_size);

			vts_key_gen(akey_buf, arg->akey_size, false, arg);
			memcpy(last_akey, akey_buf, arg->akey_size);
		}

		dts_buf_render(update_buf, UPDATE_BUF_SIZE);
		d_iov_set(&val_iov, update_buf, UPDATE_BUF_SIZE);
	}

	set_iov(&dkey, dkey_buf,
		is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
	set_iov(&akey, akey_buf,
		is_daos_obj_type_set(arg->otype, DAOS_OT_AKEY_UINT64));

	rex.rx_idx	= hash_key(&dkey, is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;

	if (update) {
		/* Act */
		rc = io_test_obj_update(arg, update_epoch, 0, &dkey, &iod, &sgl,
					NULL, true);
		if (rc)
			goto exit;

		/* Count */
		inc_cntr(arg->ta_flags);

		memset(verify_buf, 0, UPDATE_BUF_SIZE);
		d_iov_set(&val_iov, verify_buf, UPDATE_BUF_SIZE);
		iod.iod_size = DAOS_REC_ANY;

		/* Act again */
		rc = io_test_obj_fetch(arg, fetch_epoch, 0, &dkey, &iod, &sgl, true);
		if (rc)
			goto exit;

		/* Verify initialized data */
		if (arg->ta_flags & TF_REC_EXT)
			assert_int_equal(iod.iod_size, UPDATE_REC_SIZE);
		else
			assert_int_equal(iod.iod_size, UPDATE_BUF_SIZE);
		assert_memory_equal(update_buf, verify_buf, UPDATE_BUF_SIZE);
	}

	/* Refill VOS file from WAL: reopen pool & container */
	if (refill)
		wal_pool_refill(arg);

	/* Verify reconstructed data */
	if (fetch) {
		d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
		set_iov(&iod.iod_name, akey_buf,
			is_daos_obj_type_set(arg->otype, DAOS_OT_AKEY_UINT64));
		set_iov(&dkey, dkey_buf,
			is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
		rex.rx_idx = hash_key(&dkey,
				      is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
		iod.iod_size = DAOS_REC_ANY;

		rc = io_test_obj_fetch(arg, fetch_epoch, 0, &dkey, &iod, &sgl, true);
		if (rc) {
			print_error("Failed to fetch reconstructed data: "DF_RC"\n", DP_RC(rc));
			goto exit;
		}

		if (arg->ta_flags & TF_REC_EXT)
			assert_int_equal(iod.iod_size, UPDATE_REC_SIZE);
		else
			assert_int_equal(iod.iod_size, UPDATE_BUF_SIZE);
		assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
	}
exit:
	return rc;
}

static void
wal_io_multiple_refills(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		 epoch;
	char			*update_buf = NULL;
	char			*fetch_buf = NULL;
	char			*akey_buf = NULL;
	char			*dkey_buf = NULL;
	int			 i, j, rc = 0;

	num_keys = WAL_POOL_REFILLS;

	D_ALLOC_NZ(update_buf, UPDATE_BUF_SIZE);
	assert_rc_equal(!!update_buf, true);
	D_ALLOC(fetch_buf, UPDATE_BUF_SIZE);
	assert_rc_equal(!!fetch_buf, true);
	D_ALLOC_NZ(akey_buf, UPDATE_AKEY_SIZE);
	assert_rc_equal(!!akey_buf, true);
	D_ALLOC_NZ(dkey_buf, UPDATE_DKEY_SIZE);
	assert_rc_equal(!!dkey_buf, true);

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) update/fetch/verify (%s) test, multiple pool refills\n",
			      i, io_test_flags[i].tf_str);

		/* Update/fetch/verify, refill and fetch/verify again */
		epoch = gen_rand_epoch();
		arg->ta_flags = io_test_flags[i].tf_bits;
		for (j = 0; j < num_keys; j++) {
			rc = wal_update_and_fetch_dkey(arg, epoch, epoch,
						       update_buf, fetch_buf, akey_buf, dkey_buf,
						       true); /* refill after each update */
			assert_rc_equal(rc, 0);
		}
	}
	D_FREE(update_buf);
	D_FREE(fetch_buf);
	D_FREE(akey_buf);
	D_FREE(dkey_buf);
}

static void
wal_io_multiple_updates(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		 epoch;
	char			*update_buf = NULL;
	char			*fetch_buf = NULL;
	char			*akey_buf = NULL;
	char			*dkey_buf = NULL;
	char			*up, *f, *ak, *dk;
	int			 i, j, rc = 0;

	if (arg->fail_checkpoint || arg->fail_replay)
		FAULT_INJECTION_REQUIRED();

	num_keys = WAL_IO_MULTI_KEYS;
	if (arg->fail_checkpoint)
		num_keys = WAL_IO_MULTI_KEYS * 4;

	D_ALLOC_NZ(update_buf, UPDATE_BUF_SIZE * num_keys);
	assert_rc_equal(!!update_buf, true);
	D_ALLOC(fetch_buf, UPDATE_BUF_SIZE * num_keys);
	assert_rc_equal(!!fetch_buf, true);
	D_ALLOC_NZ(akey_buf, UPDATE_AKEY_SIZE * num_keys);
	assert_rc_equal(!!akey_buf, true);
	D_ALLOC_NZ(dkey_buf, UPDATE_DKEY_SIZE * num_keys);
	assert_rc_equal(!!dkey_buf, true);

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) %dK update/fetch/verify (%s), verify after pool refill\n",
			      i, num_keys/1000, io_test_flags[i].tf_str);

		/* Update/fetch/verify */
		up = update_buf;
		ak = akey_buf;
		dk = dkey_buf;
		epoch = gen_rand_epoch();
		arg->ta_flags = io_test_flags[i].tf_bits;
		for (j = 0; j < num_keys; j++) {
			rc = wal_update_and_fetch_dkey(arg, epoch, epoch,
						       up, NULL, ak, dk,
						       false); /* don't refill */
			assert_rc_equal(rc, 0);

			up += UPDATE_BUF_SIZE;
			ak += UPDATE_AKEY_SIZE;
			dk += UPDATE_DKEY_SIZE;
		}

		/* Refill VOS file from WAL: reopen pool & container */
		wal_pool_refill(arg);

		/* Fetch/verify */
		up = update_buf;
		f = fetch_buf;
		ak = akey_buf;
		dk = dkey_buf;
		for (j = 0; j < num_keys; j++) {
			rc = wal_update_and_fetch_dkey(arg, epoch, epoch,
						       up, f, ak, dk,
						       false);
			assert_rc_equal(rc, 0);

			up += UPDATE_BUF_SIZE;
			f += UPDATE_BUF_SIZE;
			ak += UPDATE_AKEY_SIZE;
			dk += UPDATE_DKEY_SIZE;
		}

		if (arg->fail_replay || arg->fail_checkpoint)
			break;
	}
	D_FREE(update_buf);
	D_FREE(fetch_buf);
	D_FREE(akey_buf);
	D_FREE(dkey_buf);
}

static void
update_dkey(void **state, daos_unit_oid_t oid, daos_epoch_t epoch,
	    uint64_t dkey_value, const char *val)
{
	struct io_test_args	*arg = *state;
	daos_iod_t		iod = {0};
	d_sg_list_t		sgl = {0};
	daos_key_t		dkey;
	daos_key_t		akey;
	d_iov_t			val_iov;
	daos_recx_t		recx;
	uint64_t		akey_value = 0;
	int			rc = 0;

	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;

	/* Attach buffer to sgl */
	d_iov_set(&val_iov, &val, strnlen(val, 32) + 1);
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	iod.iod_size = 1;
	/* Set up rexs */
	recx.rx_idx = 0;
	recx.rx_nr = val_iov.iov_len;

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_rc_equal(rc, 0);
}

static void
wal_io_query_key_punch_update(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_epoch_t		epoch = 1;
	daos_key_t		dkey = { 0 };
	daos_key_t		akey;
	daos_recx_t		recx_read;
	daos_unit_oid_t		oid;
	uint64_t		dkey_value;
	uint64_t		akey_value = 0;

	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	oid = gen_oid(arg->otype);

	update_dkey(state, oid, epoch++, 0, "World");
	update_dkey(state, oid, epoch++, 12, "Goodbye");

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL, 0, 0, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("Goodbye"));
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 12);

	/* Now punch the last dkey */
	dkey_value = 12;
	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 0,
			   NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL, 0, 0, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("World"));
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 0);

	/* Ok, now update the last one again */
	update_dkey(state, oid, epoch++, 12, "Hello!");

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL, 0, 0, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("Hello!"));
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 12);

	/* Refill VOS file from WAL: reopen pool & container */
	wal_pool_refill(arg);

	/* Verify */
	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL, 0, 0, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("Hello!"));
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 12);
}

#define WAL_UPDATE_BUF_NR_SIZE 4
static uint64_t wal_key;

static inline void
wal_print_buf(char *buf, int val)
{
	char b[12];

	sprintf(b, "%0*d", WAL_UPDATE_BUF_NR_SIZE, val);
	memcpy(buf, b, WAL_UPDATE_BUF_NR_SIZE);
}

static inline void
wal_akey_gen(daos_key_t *akey, struct io_test_args *arg)
{
	char *buf = akey->iov_buf;

	if (is_daos_obj_type_set(arg->otype, DAOS_OT_AKEY_UINT64)) {
		memcpy(buf, &wal_key, sizeof(wal_key));
		akey->iov_len = akey->iov_buf_len = sizeof(wal_key);
	} else {
		akey->iov_len = akey->iov_buf_len =
			snprintf(buf, arg->akey_size,
				 "akey=%0*lu", WAL_UPDATE_BUF_NR_SIZE, wal_key);
	}
	wal_key++;
}

static inline void
wal_dkey_gen(d_iov_t *dkey, struct io_test_args *arg)
{
	char *buf = dkey->iov_buf;

	if (is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64)) {
		memcpy(buf, &wal_key, sizeof(wal_key));
		dkey->iov_len = dkey->iov_buf_len = sizeof(wal_key);
	} else {
		dkey->iov_len = dkey->iov_buf_len =
			snprintf(buf, arg->dkey_size,
				 "dkey=%0*lu", WAL_UPDATE_BUF_NR_SIZE, wal_key);
	}
	wal_key++;
}

static void
wal_objs_update_and_fetch(struct io_test_args *arg, daos_epoch_t epoch)
{
	daos_epoch_t	ep = epoch;
	int		obj_nr, dkey_nr, v_nr;
	int		oidx, didx, aidx, rc;
	d_iov_t		val_iov;
	daos_key_t	dkey;
	daos_recx_t	rex;
	daos_unit_oid_t	oids[num_keys];
	char		dkey_buf[UPDATE_DKEY_SIZE];
	char		akey_buf[UPDATE_AKEY_SIZE];
	char		update_buf[UPDATE_BUF_SIZE];
	char		fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t	iod;
	d_sg_list_t	sgl;
	bool		overwrite;

	wal_key = 1;
	obj_nr = dkey_nr = v_nr = num_keys;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, update_buf, UPDATE_BUF_SIZE);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &val_iov;
	if (arg->ta_flags & TF_REC_EXT) {
		iod.iod_type	= DAOS_IOD_ARRAY;
		iod.iod_size	= UPDATE_REC_SIZE;
		rex.rx_nr	= UPDATE_BUF_SIZE / UPDATE_REC_SIZE;
	} else {
		iod.iod_type	= DAOS_IOD_SINGLE;
		iod.iod_size	= UPDATE_BUF_SIZE;
		rex.rx_nr	= 1;
	}

	iod.iod_recxs		= &rex;
	iod.iod_nr		= 1;
	iod.iod_name.iov_buf	= akey_buf;
	dkey.iov_buf		= dkey_buf;

	overwrite = (arg->ta_flags & TF_OVERWRITE);
	if (overwrite) {
		wal_dkey_gen(&dkey, arg);
		rex.rx_idx = hash_key(&dkey,
				      is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
		wal_akey_gen(&iod.iod_name, arg);
	}

	/* Update KVs */
	for (oidx = 0; oidx < obj_nr; oidx++) {
		arg->oid = oids[oidx] = gen_oid(arg->otype);

		for (didx = 0; didx < dkey_nr; didx++) {
			if (!overwrite) {
				wal_dkey_gen(&dkey, arg);
				rex.rx_idx = hash_key(&dkey,
					is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
			}

			for (aidx = 0; aidx < v_nr; aidx++) {
				wal_print_buf(update_buf, aidx + v_nr * (didx + dkey_nr * oidx));
				if (!overwrite)
					wal_akey_gen(&iod.iod_name, arg);

				rc = io_test_obj_update(arg, ep++, 0, &dkey, &iod, &sgl,
							NULL, true);
				assert_rc_equal(rc, 0);

				/* Count */
				inc_cntr(arg->ta_flags);
			}
		}
		/* Refill VOS file from WAL: reopen pool & container */
		if (oidx == 0)
			wal_pool_refill(arg);
	}

	wal_key = 1;
	if (overwrite) {
		wal_dkey_gen(&dkey, arg);
		rex.rx_idx = hash_key(&dkey,
				      is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
		wal_akey_gen(&iod.iod_name, arg);
		ep = epoch;
	}

	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);

	/* Fetch/verify KVs */
	for (oidx = 0; oidx < obj_nr; oidx++) {
		arg->oid = oids[oidx];

		for (didx = 0; didx < dkey_nr; didx++) {
			if (!overwrite) {
				wal_dkey_gen(&dkey, arg);
				rex.rx_idx = hash_key(&dkey,
					is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64));
			}

			for (aidx = 0; aidx < v_nr; aidx++) {
				wal_print_buf(update_buf, aidx + v_nr * (didx + dkey_nr * oidx));
				if (!overwrite)
					wal_akey_gen(&iod.iod_name, arg);

				iod.iod_size = DAOS_REC_ANY;

				rc = io_test_obj_fetch(arg, ep++, 0, &dkey, &iod, &sgl, true);
				assert_rc_equal(rc, 0);

				if (arg->ta_flags & TF_REC_EXT)
					assert_int_equal(iod.iod_size, UPDATE_REC_SIZE);
				else
					assert_int_equal(iod.iod_size, UPDATE_BUF_SIZE);
				assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
			}
		}
	}
}

static void
wal_io_multiple_objects(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		 epoch;
	int i;

	num_keys = WAL_OBJ_KEYS;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) multiple objects update (%s) test\n",
			      i, io_test_flags[i].tf_str);

		epoch = gen_rand_epoch();
		arg->ta_flags = io_test_flags[i].tf_bits;

		/* Update KVs in num_keys objects, refill pool and fetch/verify all values */
		wal_objs_update_and_fetch(arg, epoch);
	}
}

static void
wal_io_multiple_objects_ovwr(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		 epoch;
	int i;

	num_keys = WAL_OBJ_KEYS;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) multiple objects overwrite (%s) test\n",
			      i, io_test_flags[i].tf_str);

		epoch = gen_rand_epoch();
		arg->ta_flags = io_test_flags[i].tf_bits;
		arg->ta_flags |= TF_OVERWRITE;

		/*
		 * Update same key value in num_keys objects,
		 * refill pool and fetch/verify the values
		 **/
		wal_objs_update_and_fetch(arg, epoch);
	}
}

static int
wal02_setup(void **state)
{
	struct wal_test_args	*arg = *state;

	arg->wta_no_replay = true;
	arg->wta_checkpoint = true;

	return 0;
}

static int
wal02_teardown(void **state)
{
	struct wal_test_args	*arg = *state;

	arg->wta_no_replay = false;
	arg->wta_checkpoint = false;
	daos_fail_loc_set(0);

	return 0;
}

static int
wal12_setup(void **state)
{
	struct io_test_args	*arg = *state;

	arg->no_replay = true;
	arg->checkpoint = true;

	return 0;
}

static int
wal_kv_teardown(void **state)
{
	struct io_test_args	*arg = *state;

	arg->no_replay = false;
	arg->checkpoint = false;
	arg->fail_replay = false;
	arg->fail_checkpoint = false;
	daos_fail_value_set(0);
	daos_fail_loc_set(0);

	return 0;
}

static int
wal13_setup(void **state)
{
	struct io_test_args	*arg = *state;

	arg->fail_replay = true;

	return 0;
}

static int
wal14_setup(void **state)
{
	struct io_test_args	*arg = *state;

	arg->fail_checkpoint = true;
	arg->checkpoint = true;

	return 0;
}

static void
wal_mb_tests(void **state)
{
	struct io_test_args  *arg = *state;
	struct vos_container *cont;
	struct umem_instance *umm;
	uint32_t              mb_id;
	uint64_t             *ptr;
	umem_off_t            umoff;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	mb_id = umem_allot_mb_evictable(umm, 0);
	assert_true(mb_id != 0);
	umem_tx_begin(umm, NULL);
	umoff = umem_alloc_from_bucket(umm, 1024, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	ptr  = umem_off2ptr(umm, umoff);
	*ptr = 0xdeadcab;
	umem_tx_commit(umm);

	wal_pool_refill(arg);

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	ptr = umem_off2ptr(umm, umoff);
	assert_true(*ptr == 0xdeadcab);

	umem_atomic_free(umm, umoff);
}

struct bucket_alloc_info {
	umem_off_t start_umoff;
	uint32_t   num_allocs;
	uint32_t   mb_id;
	uint32_t   alloc_size;
};

#define CHECKPOINT_FREQ 10000
static void
checkpoint_fn(void *arg)
{
	struct umem_store *store;
	uint64_t           committed_id;
	daos_handle_t      phdl = *(daos_handle_t *)arg;
	int                rc;

	vos_pool_checkpoint_init(phdl, update_cb, wait_cb, &committed_id, &store);
	rc = vos_pool_checkpoint(phdl);
	assert_rc_equal(rc, 0);
	vos_pool_checkpoint_fini(phdl);
}

static void
alloc_bucket_to_full(struct umem_instance *umm, struct bucket_alloc_info *ainfo,
		     void (*chkpt_fn)(void *arg), void                   *arg)
{
	umem_off_t              umoff, prev_umoff;
	size_t                  alloc_size = 512;
	umem_off_t             *ptr;
	struct umem_cache_range rg = {0};
	struct umem_pin_handle *p_hdl;
	uint32_t                id = ainfo->mb_id;

	if (ainfo->alloc_size)
		alloc_size = ainfo->alloc_size;
	else
		ainfo->alloc_size = alloc_size;

	rg.cr_off  = umem_get_mb_base_offset(umm, id);
	rg.cr_size = 1;
	assert_true(umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl) == 0);

	if (UMOFF_IS_NULL(ainfo->start_umoff)) {
		umem_tx_begin(umm, NULL);
		ainfo->start_umoff = umem_alloc_from_bucket(umm, alloc_size, id);
		umem_tx_commit(umm);
		assert_false(UMOFF_IS_NULL(ainfo->start_umoff));
		ainfo->num_allocs++;
		assert_true(umem_get_mb_from_offset(umm, ainfo->start_umoff) == id);
		prev_umoff = ainfo->start_umoff;
		ptr        = (umem_off_t *)umem_off2ptr(umm, prev_umoff);
		*ptr       = UMOFF_NULL;
	} else
		prev_umoff = ainfo->start_umoff;

	while (true) {
		ptr   = (umem_off_t *)umem_off2ptr(umm, prev_umoff);
		umoff = *ptr;
		if (UMOFF_IS_NULL(umoff))
			break;
		prev_umoff = umoff;
	}

	while (1) {
		umem_tx_begin(umm, NULL);
		umoff = umem_alloc_from_bucket(umm, alloc_size, id);

		if (UMOFF_IS_NULL(umoff) || (umem_get_mb_from_offset(umm, umoff) != id)) {
			umem_tx_abort(umm, 1);
			break;
		}
		umem_tx_add(umm, prev_umoff, sizeof(umem_off_t));
		ptr  = (umem_off_t *)umem_off2ptr(umm, prev_umoff);
		*ptr = umoff;
		ptr  = (umem_off_t *)umem_off2ptr(umm, umoff);
		*ptr = UMOFF_NULL;
		umem_tx_commit(umm);
		prev_umoff = umoff;
		if (((ainfo->num_allocs++ % CHECKPOINT_FREQ) == 0) && (chkpt_fn != NULL))
			chkpt_fn(arg);
	}
	if (chkpt_fn != NULL)
		chkpt_fn(arg);
	umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
	print_message("Bulk Alloc: Bucket %d, start off %lu num_allocation %d\n", ainfo->mb_id,
		      ainfo->start_umoff, ainfo->num_allocs);
}

static void
free_bucket_by_pct(struct umem_instance *umm, struct bucket_alloc_info *ainfo, int pct,
		   void (*chkpt_fn)(void *arg), void *arg)
{
	int                     num_free = (ainfo->num_allocs * pct) / 100;
	umem_off_t              umoff, *ptr, next_umoff;
	struct umem_pin_handle *p_hdl;
	struct umem_cache_range rg = {0};
	int                     i, rc;

	assert_true((pct >= 0) && (pct <= 100));

	if (UMOFF_IS_NULL(ainfo->start_umoff))
		return;
	print_message("Bulk Free BEFORE: Bucket %d, start off %lu num_allocation %d\n",
		      ainfo->mb_id, ainfo->start_umoff, ainfo->num_allocs);

	rg.cr_off  = umem_get_mb_base_offset(umm, ainfo->mb_id);
	rg.cr_size = 1;
	rc         = umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl);
	assert_true(rc == 0);

	umoff = ainfo->start_umoff;
	for (i = 0; i < num_free; i++) {
		assert_true(umem_get_mb_from_offset(umm, umoff) == ainfo->mb_id);
		ptr        = (umem_off_t *)umem_off2ptr(umm, umoff);
		next_umoff = *ptr;
		umem_atomic_free(umm, umoff);
		umoff = next_umoff;
		if (((ainfo->num_allocs-- % CHECKPOINT_FREQ) == 0) && (chkpt_fn != NULL))
			chkpt_fn(arg);
		if (UMOFF_IS_NULL(umoff))
			break;
	}
	ainfo->start_umoff = umoff;
	if (chkpt_fn != NULL)
		chkpt_fn(arg);
	umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
	print_message("Bulk Free AFTER: Bucket %d, start off %lu num_allocation %d\n", ainfo->mb_id,
		      ainfo->start_umoff, ainfo->num_allocs);
}

static void
wal_mb_utilization_tests(void **state)
{
	struct io_test_args     *arg = *state;
	struct vos_container    *cont;
	struct umem_instance    *umm;
	struct bucket_alloc_info ainfo[MDTEST_MB_CNT + 1];
	uint32_t                 id;
	int                      i, j;
	int                      mb_reuse = 0;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	assert_true(MDTEST_MAX_EMB_CNT >= 8);
	for (i = 0; i < MDTEST_MAX_EMB_CNT - 1; i++) {
		/* Create an MB and fill it with allocs */
		ainfo[i].mb_id       = umem_allot_mb_evictable(umm, 0);
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		ainfo[i].alloc_size  = 0;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i], checkpoint_fn, &arg->ctx.tc_po_hdl);
	}

	/* Free 5% of space for MB 2 */
	free_bucket_by_pct(umm, &ainfo[0], 5, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 90+ */
	/* Free 30% of space for MB 3 */
	free_bucket_by_pct(umm, &ainfo[1], 30, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 30-75 */
	/* Free 80% of space for MB 4 */
	free_bucket_by_pct(umm, &ainfo[2], 80, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 0-30 */
	/* Free 15% of space for MB 5 */
	free_bucket_by_pct(umm, &ainfo[3], 20, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 75-90 */
	/* Free 10% of space for MB 6 */
	free_bucket_by_pct(umm, &ainfo[4], 18, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 75-90 */
	/* Free 50% of space for MB 7 */
	free_bucket_by_pct(umm, &ainfo[5], 50, checkpoint_fn, &arg->ctx.tc_po_hdl); /* 30-75 */
	/* Free 90% of space for MB 8 */
	free_bucket_by_pct(umm, &ainfo[6], 90, NULL, NULL); /* 0-30 */

	wal_pool_refill(arg);
	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	/* Allocator should return mb with utilization 30%-75% */
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[1].mb_id);
	assert_true(id == ainfo[1].mb_id);
	alloc_bucket_to_full(umm, &ainfo[1], checkpoint_fn, &arg->ctx.tc_po_hdl);
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[5].mb_id);
	assert_true(id == ainfo[5].mb_id);
	alloc_bucket_to_full(umm, &ainfo[5], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/* Next preference should be 75%-90% */
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[3].mb_id);
	assert_true(id == ainfo[3].mb_id);
	alloc_bucket_to_full(umm, &ainfo[3], checkpoint_fn, &arg->ctx.tc_po_hdl);
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[4].mb_id);
	assert_true(id == ainfo[4].mb_id);
	alloc_bucket_to_full(umm, &ainfo[4], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/* Next preference should be 0%-30% */
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[2].mb_id);
	assert_true(id == ainfo[2].mb_id);
	alloc_bucket_to_full(umm, &ainfo[2], checkpoint_fn, &arg->ctx.tc_po_hdl);
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[6].mb_id);
	assert_true(id == ainfo[6].mb_id);
	alloc_bucket_to_full(umm, &ainfo[6], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/* Next is to create a new memory bucket. */
	id = umem_allot_mb_evictable(umm, 0);
	for (i = 0; i < MDTEST_MAX_EMB_CNT - 1; i++)
		assert_true(id != ainfo[i].mb_id);
	print_message("obtained id %d\n", id);

	/* If there are no more new evictable mb available it should return
	 * one with 90% or more utilization.
	 */
	for (i = MDTEST_MAX_EMB_CNT - 1; i < MDTEST_MB_CNT + 1; i++) {
		id = umem_allot_mb_evictable(umm, 0);
		for (j = 0; j < i; j++) {
			if (id == ainfo[j].mb_id) {
				print_message("reusing evictable mb %d\n", id);
				mb_reuse = 1;
				break;
			}
		}
		if (mb_reuse)
			break;
		ainfo[i].mb_id       = id;
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		ainfo[i].alloc_size  = 0;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i], checkpoint_fn, &arg->ctx.tc_po_hdl);
	}

	print_message("evictable memory buckets created = %d\n", i);
	assert_true(i == MDTEST_MAX_EMB_CNT);
}

#define ZONE_MAX_SIZE (16 * 1024 * 1024)

static void
wal_mb_emb_evicts_emb(void **state)
{
	struct io_test_args     *arg = *state;
	struct vos_container    *cont;
	struct umem_instance    *umm;
	int                      i, j, po;
	struct bucket_alloc_info ainfo[MDTEST_MB_CNT + 1];
	uint32_t                 id;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	/* Fill non-evictable buckets. */
	ainfo[0].mb_id       = 0;
	ainfo[0].num_allocs  = 0;
	ainfo[0].start_umoff = UMOFF_NULL;
	ainfo[0].alloc_size  = 0;
	alloc_bucket_to_full(umm, &ainfo[0], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/*
	 * validate whether non-evictable mbs have actually consumed MDTEST_MAX_NEMB_CNT
	 */
	print_message("allocations in non-evictable mbs = %u\n", ainfo[0].num_allocs);
	print_message("space used in non-evictable mbs = %u\n",
		      ainfo[0].num_allocs * ainfo[0].alloc_size);
	po = (ainfo[0].num_allocs * ainfo[0].alloc_size + ZONE_MAX_SIZE - 1) / ZONE_MAX_SIZE;
	assert_true(po == MDTEST_MAX_NEMB_CNT);

	/* Now free few allocation to support spill */
	free_bucket_by_pct(umm, &ainfo[0], 20, checkpoint_fn, &arg->ctx.tc_po_hdl);

	/* Create and fill MDTEST_MB_CNT evictable memory buckets. */
	for (i = 1; i < MDTEST_MB_CNT + 1; i++) {
		/* Create an MB and fill it with allocs */
		id = umem_allot_mb_evictable(umm, 0);
		for (j = 0; j < i; j++) {
			if (id == ainfo[j].mb_id) {
				print_message("evictable mb reused at iteration %d\n", id);
				goto out;
			}
		}
		ainfo[i].mb_id       = id;
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		ainfo[i].alloc_size  = 0;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i], checkpoint_fn, &arg->ctx.tc_po_hdl);
	}
out:
	assert_true(i == MDTEST_MAX_EMB_CNT + 1);

	/* Validate and free all allocations in evictable MBs */
	for (j = 0; j < i; j++)
		free_bucket_by_pct(umm, &ainfo[j], 100, checkpoint_fn, &arg->ctx.tc_po_hdl);
}

static void
wal_mb_nemb_evicts_emb(void **state)
{
	struct io_test_args     *arg = *state;
	struct vos_container    *cont;
	struct umem_instance    *umm;
	int                      i, j, po;
	struct bucket_alloc_info ainfo[MDTEST_MB_CNT + 1];
	uint32_t                 id;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	/* Create and fill evictable memory buckets. */
	for (i = 1; i < MDTEST_MB_CNT + 1; i++) {
		/* Create an MB and fill it with allocs */
		id = umem_allot_mb_evictable(umm, 0);
		for (j = 1; j < i; j++) {
			if (id == ainfo[j].mb_id) {
				print_message("evictable mb reused at iteration %d\n", id);
				goto out;
			}
		}
		ainfo[i].mb_id       = id;
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		ainfo[i].alloc_size  = 0;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i], checkpoint_fn, &arg->ctx.tc_po_hdl);
	}
out:
	assert_true(i == MDTEST_MAX_EMB_CNT + 1);

	/* Fill non-evictable buckets. */
	ainfo[0].mb_id       = 0;
	ainfo[0].num_allocs  = 0;
	ainfo[0].start_umoff = UMOFF_NULL;
	ainfo[0].alloc_size  = 0;
	alloc_bucket_to_full(umm, &ainfo[0], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/*
	 * validate whether non-evictable mbs have actually consumed MDTEST_MAX_NEMB_CNT buckets.
	 */
	print_message("allocations in non-evictable mbs = %u\n", ainfo[0].num_allocs);
	print_message("space used in non-evictable mbs = %u\n",
		      ainfo[0].num_allocs * ainfo[0].alloc_size);
	po = (ainfo[0].num_allocs * ainfo[0].alloc_size + ZONE_MAX_SIZE - 1) / ZONE_MAX_SIZE;
	assert_true(po == MDTEST_MAX_NEMB_CNT);

	/* Validate and free all allocations in evictable MBs */
	for (j = 0; j < i; j++)
		free_bucket_by_pct(umm, &ainfo[j], 100, checkpoint_fn, &arg->ctx.tc_po_hdl);
}

static int
umoff_in_freelist(umem_off_t *free_list, int cnt, umem_off_t umoff, bool clear)
{
	int i;

	for (i = 0; i < cnt; i++)
		if (umoff == free_list[i])
			break;

	if (i < cnt) {
		if (clear)
			free_list[i] = UMOFF_NULL;
		return 1;
	}
	return 0;
}

static void
wal_umempobj_block_reuse_internal(void **state, int restart)
{
	struct io_test_args     *arg = *state;
	struct vos_container    *cont;
	struct umem_instance    *umm;
	umem_off_t               umoff, next_umoff, nnext_umoff;
	umem_off_t              *ptr_cur, *ptr_next;
	umem_off_t              *free_list[MDTEST_MB_CNT + 1];
	umem_off_t              *free_list_bk[MDTEST_MB_CNT + 1];
	int                      free_num[MDTEST_MB_CNT + 1];
	struct bucket_alloc_info ainfo[MDTEST_MB_CNT + 1];
	int                      i, j, cnt, rc, num, total_frees;
	struct umem_pin_handle  *p_hdl;
	struct umem_cache_range  rg = {0};
	uint64_t                 space_used_before, space_used_after;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm  = vos_cont2umm(cont);

	/* Allocate from NE Buckets. It should use 80% 360M i.e, 16 buckets */
	ainfo[0].mb_id       = 0;
	ainfo[0].num_allocs  = 0;
	ainfo[0].start_umoff = UMOFF_NULL;
	ainfo[0].alloc_size  = 512;
	alloc_bucket_to_full(umm, &ainfo[0], checkpoint_fn, &arg->ctx.tc_po_hdl);

	/* Allocate from Evictable Buckets. */
	for (i = 1; i <= MDTEST_MAX_EMB_CNT; i++) {
		/* Create an MB and fill it with allocs */
		ainfo[i].mb_id       = umem_allot_mb_evictable(umm, 0);
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		ainfo[i].alloc_size  = 512;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i], checkpoint_fn, &arg->ctx.tc_po_hdl);
	}

	/* Free few allocations from each NE bucket */
	umem_tx_begin(umm, NULL);
	umoff       = ainfo[0].start_umoff;
	num         = ainfo[0].num_allocs;
	free_num[0] = num / 10000;
	cnt         = 0;
	D_ALLOC_ARRAY(free_list[0], free_num[0]);
	for (j = 1; j <= num; j++) {
		ptr_cur    = (umem_off_t *)umem_off2ptr(umm, umoff);
		next_umoff = *ptr_cur;
		if ((j % 10000) == 0) {
			if (UMOFF_IS_NULL(next_umoff))
				break;
			ptr_next    = (umem_off_t *)umem_off2ptr(umm, next_umoff);
			nnext_umoff = *ptr_next;
			umem_tx_add_ptr(umm, ptr_cur, sizeof(umoff));
			*ptr_cur = nnext_umoff;
			umem_free(umm, next_umoff);
			print_message("id=0:Freeing offset %lu\n", next_umoff);
			ainfo->num_allocs--;
			free_list[0][cnt++] = next_umoff;
			umoff               = nnext_umoff;
		} else
			umoff = next_umoff;
		if (UMOFF_IS_NULL(umoff))
			break;
	}
	umem_tx_commit(umm);
	assert_true(cnt == free_num[0]);
	print_message("id=0:Total frees %d\n", cnt);

	/* Free few allocations from each E bucket */
	for (i = 1; i <= MDTEST_MAX_EMB_CNT; i++) {
		rg.cr_off  = umem_get_mb_base_offset(umm, ainfo[i].mb_id);
		rg.cr_size = 1;
		rc         = umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl);
		assert_true(rc == 0);

		umem_tx_begin(umm, NULL);
		umoff       = ainfo[i].start_umoff;
		num         = ainfo[i].num_allocs;
		free_num[i] = num / 10000;
		cnt         = 0;
		D_ALLOC_ARRAY(free_list[i], free_num[i]);
		for (j = 1; j <= num; j++) {
			ptr_cur    = (umem_off_t *)umem_off2ptr(umm, umoff);
			next_umoff = *ptr_cur;
			if ((j % 10000) == 0) {
				if (UMOFF_IS_NULL(next_umoff))
					break;
				ptr_next    = (umem_off_t *)umem_off2ptr(umm, next_umoff);
				nnext_umoff = *ptr_next;
				umem_tx_add_ptr(umm, ptr_cur, sizeof(umoff));
				*ptr_cur = nnext_umoff;
				umem_free(umm, next_umoff);
				print_message("id=%d:Freeing offset %lu\n", i, next_umoff);
				ainfo->num_allocs--;
				free_list[i][cnt++] = next_umoff;
				umoff               = nnext_umoff;
			} else
				umoff = next_umoff;
			if (UMOFF_IS_NULL(umoff))
				break;
		}
		umem_tx_commit(umm);
		umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
		assert_true(cnt == free_num[i]);
		print_message("id=%d:Total frees %d\n", ainfo[i].mb_id, cnt);
	}

	/* restart with or without checkpoint */
	if (restart) {
		wal_pool_refill(arg);
		cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
		umm  = vos_cont2umm(cont);
	}

	for (i = 0; i < MDTEST_MAX_EMB_CNT + 1; i++) {
		D_ALLOC_ARRAY(free_list_bk[i], free_num[i]);
		memcpy(free_list_bk[i], free_list[i], free_num[i] * sizeof(umem_off_t));
	}

	/* Allocate from NE Buckets and it should reuse the previous freed blocks */
	for (j = 0; j < free_num[0]; j++) {
		umem_tx_begin(umm, NULL);
		umoff = umem_alloc(umm, ainfo[0].alloc_size);
		umem_tx_commit(umm);
		assert_true(!UMOFF_IS_NULL(umoff));
		assert_true(umoff_in_freelist(free_list[0], free_num[0], umoff, true));
	}

	/* New allocation should fail */
	umem_tx_begin(umm, NULL);
	umoff = umem_alloc(umm, ainfo[0].alloc_size);
	umem_tx_abort(umm, 1);
	assert_true(UMOFF_IS_NULL(umoff));

	/* Allocate from E Buckets and it should reuse the previous freed blocks */
	for (i = 1; i <= MDTEST_MAX_EMB_CNT; i++) {
		rg.cr_off  = umem_get_mb_base_offset(umm, ainfo[i].mb_id);
		rg.cr_size = 1;
		rc         = umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl);
		assert_true(rc == 0);

		for (j = 0; j < free_num[i]; j++) {
			umem_tx_begin(umm, NULL);
			umoff = umem_alloc_from_bucket(umm, ainfo[i].alloc_size, ainfo[i].mb_id);
			assert_true(!UMOFF_IS_NULL(umoff));
			umem_tx_commit(umm);
			assert_true(umoff_in_freelist(free_list[i], free_num[i], umoff, true));
		}
		umem_tx_begin(umm, NULL);
		/* New allocation should fail */
		umoff = umem_alloc(umm, ainfo[i].alloc_size);
		umem_tx_abort(umm, 1);
		assert_true(UMOFF_IS_NULL(umoff));
		print_message("Finished reallocating for id = %d\n", ainfo[i].mb_id);
		umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
	}

	/* Free the allocated memory to see whether they are properly accounted */
	rc = umempobj_get_heapusage(umm->umm_pool, &space_used_before);
	if (rc) {
		print_message("Failed to get heap usage\n");
		assert_true(rc == 0);
	}
	for (j = 0; j < free_num[0]; j++)
		umem_atomic_free(umm, free_list_bk[0][j]);
	D_FREE(free_list[0]);
	D_FREE(free_list_bk[0]);

	total_frees = free_num[0];

	for (i = 1; i <= MDTEST_MAX_EMB_CNT; i++) {
		rg.cr_off  = umem_get_mb_base_offset(umm, ainfo[i].mb_id);
		rg.cr_size = 1;
		rc         = umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl);
		assert_true(rc == 0);

		for (j = 0; j < free_num[i]; j++) {
			umoff = umem_atomic_free(umm, free_list_bk[i][j]);
		}
		umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
		total_frees += free_num[i];
		D_FREE(free_list[i]);
		D_FREE(free_list_bk[i]);
	}
	rc = umempobj_get_heapusage(umm->umm_pool, &space_used_after);
	if (rc) {
		print_message("Failed to get heap usage\n");
		assert_true(rc == 0);
	}
	print_message("Space usage: before free %lu, after free %lu, expected %lu\n",
		      space_used_before, space_used_after, (space_used_before - total_frees * 512));
	assert_true(space_used_after <= (space_used_before - total_frees * 512));
}

void
wal_umempobj_block_reuse(void **state)
{
	wal_umempobj_block_reuse_internal(state, 0);
}

void
wal_umempobj_replay_block_reuse(void **state)
{
	wal_umempobj_block_reuse_internal(state, 1);
}

void
wal_umempobj_chkpt_block_reuse(void **state)
{
	struct io_test_args *arg = *state;

	arg->checkpoint = true;
	arg->no_replay  = true;
	wal_umempobj_block_reuse_internal(state, 1);
	arg->checkpoint = false;
	arg->no_replay  = false;
}

static const struct CMUnitTest wal_tests[] = {
    {"WAL01: Basic pool/cont create/destroy test", wal_tst_pool_cont, NULL, NULL},
    {"WAL02: Basic pool/cont create/destroy test with checkpointing", wal_tst_pool_cont,
      wal02_setup, wal02_teardown},
};

static const struct CMUnitTest wal_kv_basic_tests[] = {
    {"WAL10: Basic SV/EV small/large update/fetch/verify", wal_kv_basic, NULL, NULL},
    {"WAL11: Basic SV/EV large TX update/fetch/verify", wal_kv_large, NULL, NULL},
    {"WAL12: Basic SV/EV small/large update/fetch/verify checkpoint", wal_kv_basic,
     wal12_setup, wal_kv_teardown},
    {"WAL13: Interrupt replay", wal_io_multiple_updates, wal13_setup, wal_kv_teardown},
    {"WAL13: Interrupt checkpoint", wal_io_multiple_updates, wal14_setup, wal_kv_teardown},
};

static const struct CMUnitTest wal_io_tests[] = {
    {"WAL20: Update/fetch/verify test", wal_io_multiple_refills, NULL, NULL},
    {"WAL21: 10K update/fetch/verify test", wal_io_multiple_updates, NULL, NULL},
    {"WAL22: Objects Update(overwrite)/fetch test", wal_io_multiple_objects_ovwr, NULL, NULL},
    {"WAL23: Objects Update/fetch test", wal_io_multiple_objects, NULL, NULL},
};

static const struct CMUnitTest wal_io_int_tests[] = {
    {"WAL24: Key query punch with subsequent update", wal_io_query_key_punch_update, NULL, NULL},
};

static const struct CMUnitTest wal_MB_tests[] = {
    {"WAL30: UMEM MB Basic Test", wal_mb_tests, setup_mb_io, teardown_mb_io},
    {"WAL31: UMEM MB EMB selection based on utilization Test", wal_mb_utilization_tests,
     setup_mb_io, teardown_mb_io},
    {"WAL32: UMEM MB EMB eviction by other EMBs Test", wal_mb_emb_evicts_emb, setup_mb_io,
     teardown_mb_io},
    {"WAL33: UMEM MB EMB eviction by NEMB expansion Test", wal_mb_nemb_evicts_emb, setup_mb_io,
     teardown_mb_io},
    {"WAL34: UMEM MB garbage collection", wal_umempobj_block_reuse, setup_mb_io, teardown_mb_io},
    {"WAL35: UMEM MB checkpoint restart garbage collection", wal_umempobj_chkpt_block_reuse,
     setup_mb_io, teardown_mb_io},
    {"WAL36: UMEM MB restart replay garbage collection", wal_umempobj_replay_block_reuse,
     setup_mb_io, teardown_mb_io},
};

int
run_wal_tests(const char *cfg)
{
	char		 test_name[DTS_CFG_MAX];
	const char	*akey = "hashed";
	const char	*dkey = "hashed";
	int		 i, rc;

	if (!bio_nvme_configured(SMD_DEV_TYPE_META)) {
		print_message("MD_ON_SSD mode isn't enabled, skip all tests.\n");
		return 0;
	}

	dts_create_config(test_name, "WAL Pool and container tests %s", cfg);
	D_PRINT("Running %s\n", test_name);
	rc = cmocka_run_group_tests_name(test_name, wal_tests, setup_wal_test,
					   teardown_wal_test);

	dts_create_config(test_name, "WAL Basic SV and EV IO tests %s", cfg);
	D_PRINT("Running %s\n", test_name);
	otype = 0;
	rc += cmocka_run_group_tests_name(test_name, wal_kv_basic_tests,
						  setup_wal_io, teardown_io);

	for (i = 0; i < (sizeof(type_list) / sizeof(int)); i++) {
		otype = type_list[i];
		if (is_daos_obj_type_set(otype, DAOS_OT_DKEY_UINT64))
			dkey = "uint";
		if (is_daos_obj_type_set(otype, DAOS_OT_DKEY_LEXICAL))
			dkey = "lex";
		if (is_daos_obj_type_set(otype, DAOS_OT_AKEY_UINT64))
			akey = "uint";
		if (is_daos_obj_type_set(otype, DAOS_OT_AKEY_LEXICAL))
			akey = "lex";
		dts_create_config(test_name, "WAL0 Basic IO tests dkey=%-6s akey=%s %s", dkey, akey,
				  cfg);
		test_name[3] = '1';
		D_PRINT("Running %s\n", test_name);
		rc += cmocka_run_group_tests_name(test_name, wal_io_tests,
						  setup_wal_io, teardown_io);
		if (otype == DAOS_OT_MULTI_UINT64) {
			test_name[3] = '2';
			D_PRINT("Running %s\n", test_name);
			rc += cmocka_run_group_tests_name(test_name, wal_io_int_tests,
							  setup_wal_io, teardown_io);
		}
	}

	if (umempobj_get_backend_type() == DAOS_MD_BMEM_V2) {
		dts_create_config(test_name, "Memory Bucket tests with WAL %s", cfg);
		D_PRINT("Running %s\n", test_name);
		rc += cmocka_run_group_tests_name(test_name, wal_MB_tests, NULL, NULL);
	}
	return rc;
}
