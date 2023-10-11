/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include "bio_ut.h"
#include "../../bio/bio_wal.h"

static void
ut_mc_fini(struct bio_ut_args *args)
{
	int	rc;

	rc = bio_mc_close(args->bua_mc);
	if (rc)
		D_ERROR("UT MC close failed. "DF_RC"\n", DP_RC(rc));

	rc = bio_mc_destroy(args->bua_xs_ctxt, args->bua_pool_id, 0);
	if (rc)
		D_ERROR("UT MC destroy failed. "DF_RC"\n", DP_RC(rc));
}

static int
ut_mc_init(struct bio_ut_args *args, uint64_t meta_sz, uint64_t wal_sz, uint64_t data_sz)
{
	int	rc, ret;

	uuid_generate(args->bua_pool_id);
	rc = bio_mc_create(args->bua_xs_ctxt, args->bua_pool_id, meta_sz, wal_sz, data_sz, 0);
	if (rc) {
		D_ERROR("UT MC create failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	if (rc) {
		D_ERROR("UT MC open failed. "DF_RC"\n", DP_RC(rc));
		ret = bio_mc_destroy(args->bua_xs_ctxt, args->bua_pool_id, 0);
		if (ret)
			D_ERROR("UT MC destroy failed. "DF_RC"\n", DP_RC(ret));
	}

	return rc;
}

struct ut_fake_tx {
	uint32_t		ft_act_max;
	uint32_t		ft_buf_sz;
	uint32_t		ft_act_nr;
	uint32_t		ft_payload_sz;
	uint32_t		ft_act_idx;
	uint32_t		ft_copy_ptr_sz;
	struct umem_action	*ft_acts;
	char			*ft_buffer;
	unsigned int		ft_ckp_done:1;
};

static uint32_t
fake_act_nr(struct umem_wal_tx *tx)
{
	struct ut_fake_tx	*fake_tx = (struct ut_fake_tx *)&tx->utx_private;

	return fake_tx->ft_act_nr;
}

static uint32_t
fake_payload_sz(struct umem_wal_tx *tx)
{
	struct ut_fake_tx	*fake_tx = (struct ut_fake_tx *)&tx->utx_private;

	return fake_tx->ft_payload_sz;
}

static struct umem_action *
fake_act_first(struct umem_wal_tx *tx)
{
	struct ut_fake_tx	*fake_tx = (struct ut_fake_tx *)&tx->utx_private;

	fake_tx->ft_act_idx = 0;
	return &fake_tx->ft_acts[fake_tx->ft_act_idx];
}

static struct umem_action *
fake_act_next(struct umem_wal_tx *tx)
{
	struct ut_fake_tx	*fake_tx = (struct ut_fake_tx *)&tx->utx_private;

	fake_tx->ft_act_idx++;
	if (fake_tx->ft_act_idx >= fake_tx->ft_act_nr)
		return NULL;
	return &fake_tx->ft_acts[fake_tx->ft_act_idx];
}

static struct umem_wal_tx_ops ut_fake_wal_tx_ops = {
	.wtx_act_nr	= fake_act_nr,
	.wtx_payload_sz	= fake_payload_sz,
	.wtx_act_first	= fake_act_first,
	.wtx_act_next	= fake_act_next,
};

static void
ut_tx_free(struct umem_wal_tx *fake_wal_tx)
{
	struct ut_fake_tx	*fake_tx;

	fake_tx = (struct ut_fake_tx *)&fake_wal_tx->utx_private;
	D_FREE(fake_tx->ft_acts);
	D_FREE(fake_tx->ft_buffer);

	D_FREE(fake_wal_tx);
}

static struct umem_wal_tx *
ut_tx_alloc(uint32_t act_nr, uint32_t buf_sz)
{
	struct umem_wal_tx	*fake_wal_tx;
	struct ut_fake_tx	*fake_tx;

	D_ALLOC_PTR(fake_wal_tx);
	assert_non_null(fake_wal_tx);
	fake_wal_tx->utx_ops = &ut_fake_wal_tx_ops;

	fake_tx = (struct ut_fake_tx *)&fake_wal_tx->utx_private;

	D_ALLOC_ARRAY(fake_tx->ft_acts, act_nr);
	assert_non_null(fake_tx->ft_acts);
	fake_tx->ft_act_max = act_nr;

	if (buf_sz) {
		D_ALLOC(fake_tx->ft_buffer, buf_sz);
		assert_non_null(fake_tx->ft_buffer);
		fake_tx->ft_buf_sz = buf_sz;
		dts_buf_render(fake_tx->ft_buffer, buf_sz);
	}

	return fake_wal_tx;
}

static inline uint64_t
rand_addr()
{
	uint64_t addr  = rand_r(&ut_args.bua_seed);

	return (addr << 32) + rand_r(&ut_args.bua_seed);
}

static inline uint32_t
rand_int()
{
	return rand_r(&ut_args.bua_seed);
}

static void
ut_tx_add_action(struct umem_wal_tx *tx, unsigned int act_opc)
{
	struct ut_fake_tx	*fake_tx;
	struct umem_action	*act;
	uint64_t		 payload;
	int			 num;

	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	act = &fake_tx->ft_acts[fake_tx->ft_act_idx];
	fake_tx->ft_act_idx++;
	fake_tx->ft_act_nr++;
	D_ASSERT(fake_tx->ft_act_nr <= fake_tx->ft_act_max);

	act->ac_opc = act_opc;
	switch (act_opc) {
	case UMEM_ACT_COPY:
		act->ac_copy.addr = rand_addr();
		act->ac_copy.size = sizeof(uint64_t);
		payload = rand_addr();
		memcpy(act->ac_copy.payload, &payload, sizeof(uint64_t));
		fake_tx->ft_payload_sz += act->ac_copy.size;
		break;
	case UMEM_ACT_COPY_PTR:
		act->ac_copy_ptr.addr = rand_addr();
		if (fake_tx->ft_copy_ptr_sz == 0)
			act->ac_copy_ptr.size = (rand_int() % fake_tx->ft_buf_sz) + 1;
		else
			act->ac_copy_ptr.size = fake_tx->ft_copy_ptr_sz;
		D_ASSERT(act->ac_copy_ptr.size <= fake_tx->ft_buf_sz);
		act->ac_copy_ptr.ptr = (uint64_t)fake_tx->ft_buffer;
		fake_tx->ft_payload_sz += act->ac_copy_ptr.size;
		break;
	case UMEM_ACT_ASSIGN:
		num = rand_int() % 3;
		if (num == 0)
			act->ac_assign.size = 1;
		else if (num == 2)
			act->ac_assign.size = 2;
		else
			act->ac_assign.size = 4;
		act->ac_assign.val = rand_int();
		act->ac_assign.addr = rand_addr();
		break;
	case UMEM_ACT_MOVE:
		act->ac_move.size = rand_int();
		act->ac_move.src = rand_addr();
		act->ac_move.dst = rand_addr();
		fake_tx->ft_payload_sz += sizeof(uint64_t);
		break;
	case UMEM_ACT_SET:
		act->ac_set.val = rand_int() % 256;
		act->ac_set.size = rand_int();
		act->ac_set.addr = rand_addr();
		break;
	case UMEM_ACT_SET_BITS:
	case UMEM_ACT_CLR_BITS:
		act->ac_op_bits.pos = rand_int() % 64;
		act->ac_op_bits.num = 64 - act->ac_op_bits.pos;
		act->ac_op_bits.addr = rand_addr();
		break;
	case UMEM_ACT_CSUM:
		/*
		 * It doesn't make sense to test data CSUM when we always do self-polling
		 * in bio_iod_post_async(), since the NVMe update must have done before TX
		 * commit, and the completed data IOD will be ignored in bio_wal_commit().
		 *
		 * TODO: Introduce NVMe polling ULT for BIO unit tests.
		 */
		D_ASSERTF(0, "Data csum isn't supported before polling ULT implemented.\n");
		break;
	default:
		D_ASSERTF(0, "Invalid opc %u\n", act->ac_opc);
		break;
	}
}

static int
ut_replay_one(uint64_t tx_id, struct umem_action *act, void *arg)
{
	struct umem_wal_tx	*tx = arg;
	struct ut_fake_tx	*fake_tx;
	struct umem_action	*orig_act;

	assert_int_equal(tx_id, tx->utx_id);

	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	D_ASSERT(fake_tx->ft_act_idx < fake_tx->ft_act_nr);
	orig_act = &fake_tx->ft_acts[fake_tx->ft_act_idx];
	fake_tx->ft_act_idx++;

	switch (orig_act->ac_opc) {
	case UMEM_ACT_COPY:
		assert_int_equal(act->ac_opc, UMEM_ACT_COPY);
		assert_int_equal(act->ac_copy.addr, orig_act->ac_copy.addr);
		assert_int_equal(act->ac_copy.size, orig_act->ac_copy.size);
		assert_memory_equal(act->ac_copy.payload, orig_act->ac_copy.payload,
				    act->ac_copy.size);
		break;
	case UMEM_ACT_COPY_PTR:
		assert_int_equal(act->ac_opc, UMEM_ACT_COPY);
		assert_int_equal(act->ac_copy.addr, orig_act->ac_copy_ptr.addr);
		assert_int_equal(act->ac_copy.size, orig_act->ac_copy_ptr.size);
		assert_memory_equal(act->ac_copy.payload, orig_act->ac_copy_ptr.ptr,
				    act->ac_copy.size);
		break;
	case UMEM_ACT_ASSIGN:
		assert_int_equal(act->ac_opc, UMEM_ACT_ASSIGN);
		assert_int_equal(act->ac_assign.size, orig_act->ac_assign.size);
		assert_int_equal(act->ac_assign.val, orig_act->ac_assign.val);
		assert_int_equal(act->ac_assign.addr, orig_act->ac_assign.addr);
		break;
	case UMEM_ACT_MOVE:
		assert_int_equal(act->ac_opc, UMEM_ACT_MOVE);
		assert_int_equal(act->ac_move.size, orig_act->ac_move.size);
		assert_int_equal(act->ac_move.src, orig_act->ac_move.src);
		assert_int_equal(act->ac_move.dst, orig_act->ac_move.dst);
		break;
	case UMEM_ACT_SET:
		assert_int_equal(act->ac_opc, UMEM_ACT_SET);
		assert_int_equal(act->ac_set.val, orig_act->ac_set.val);
		assert_int_equal(act->ac_set.size, orig_act->ac_set.size);
		assert_int_equal(act->ac_set.addr, orig_act->ac_set.addr);
		break;
	case UMEM_ACT_SET_BITS:
	case UMEM_ACT_CLR_BITS:
		assert_int_equal(act->ac_opc, orig_act->ac_opc);
		assert_int_equal(act->ac_op_bits.num, orig_act->ac_op_bits.num);
		assert_int_equal(act->ac_op_bits.pos, orig_act->ac_op_bits.pos);
		assert_int_equal(act->ac_op_bits.addr, orig_act->ac_op_bits.addr);
		break;
	case UMEM_ACT_CSUM:
		D_ASSERTF(0, "Data CSUM shouldn't be replayed\n");
		break;
	default:
		D_ASSERTF(0, "Invalid opc %u\n", orig_act->ac_opc);
		break;
	}

	return 0;
}

static void
wal_ut_single(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	tx = ut_tx_alloc(7, (128UL << 10));
	assert_non_null(tx);

	ut_tx_add_action(tx, UMEM_ACT_COPY);
	ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
	ut_tx_add_action(tx, UMEM_ACT_ASSIGN);
	ut_tx_add_action(tx, UMEM_ACT_MOVE);
	ut_tx_add_action(tx, UMEM_ACT_SET);
	ut_tx_add_action(tx, UMEM_ACT_SET_BITS);
	ut_tx_add_action(tx, UMEM_ACT_CLR_BITS);

	rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
	assert_rc_equal(rc, 0);

	rc = bio_wal_commit(args->bua_mc, tx, NULL);
	assert_rc_equal(rc, 0);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Reset act index before replay */
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	fake_tx->ft_act_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_one, tx);
	assert_rc_equal(rc, 0);
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_tx_free(tx);

	ut_mc_fini(args);
}

static void
wal_ut_many_acts(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	unsigned int		 hdr_sz = sizeof(struct wal_trans_head);
	unsigned int		 entry_sz = sizeof(struct wal_trans_entry);
	unsigned int		 blk_bytes;
	int			 i, act_nr, rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	/* Generate many actions to fill 2 and half WAL blocks */
	blk_bytes = args->bua_mc->mc_wal_info.si_header.wh_blk_bytes;
	act_nr = (blk_bytes - hdr_sz) / entry_sz;
	act_nr = act_nr * 2 + act_nr / 2;

	tx = ut_tx_alloc(act_nr, 0);
	assert_non_null(tx);

	for (i = 0; i < act_nr; i++)
		ut_tx_add_action(tx, UMEM_ACT_COPY);

	rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
	assert_rc_equal(rc, 0);

	rc = bio_wal_commit(args->bua_mc, tx, NULL);
	assert_rc_equal(rc, 0);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Reset act index before replay */
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	fake_tx->ft_act_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_one, tx);
	assert_rc_equal(rc, 0);
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_tx_free(tx);

	ut_mc_fini(args);
}

static void
wal_ut_large_payload(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	tx = ut_tx_alloc(7, (1UL << 20));
	assert_non_null(tx);

	/* Specify large copy ptr size */
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	fake_tx->ft_copy_ptr_sz = (1UL << 20);

	ut_tx_add_action(tx, UMEM_ACT_ASSIGN);
	ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
	ut_tx_add_action(tx, UMEM_ACT_COPY);
	ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
	ut_tx_add_action(tx, UMEM_ACT_SET);

	rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
	assert_rc_equal(rc, 0);

	rc = bio_wal_commit(args->bua_mc, tx, NULL);
	assert_rc_equal(rc, 0);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Reset act index before replay */
	fake_tx->ft_act_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_one, tx);
	assert_rc_equal(rc, 0);
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_tx_free(tx);

	ut_mc_fini(args);
}

struct ut_tx_array {
	struct umem_wal_tx	**ta_tx_ptrs;
	uint64_t		  ta_replay_tx;	/* Current replay tx */
	uint32_t		  ta_tx_nr;
	uint32_t		  ta_tx_idx;
	uint32_t		  ta_replay_nr;
	uint32_t		  ta_replayed_nr;
};

static void
ut_txa_free(struct ut_tx_array *txa)
{
	int	i;

	for (i = 0; i < txa->ta_tx_nr; i++)
		ut_tx_free(txa->ta_tx_ptrs[i]);

	D_FREE(txa->ta_tx_ptrs);
	D_FREE(txa);
}

#define UT_MAX_BUF_SZ	(800UL << 10)	/* 800k */

static struct ut_tx_array *
ut_txa_alloc(unsigned int tx_nr)
{
	struct ut_tx_array	*txa;
	int			 i;

	D_ALLOC_PTR(txa);
	assert_non_null(txa);

	D_ALLOC_ARRAY(txa->ta_tx_ptrs, tx_nr);
	assert_non_null(txa->ta_tx_ptrs);

	for (i = 0; i < tx_nr; i++) {
		txa->ta_tx_ptrs[i] = ut_tx_alloc(7, UT_MAX_BUF_SZ);
		assert_non_null(txa->ta_tx_ptrs[i]);
	}
	txa->ta_tx_nr = tx_nr;
	txa->ta_replay_tx = UINT64_MAX;

	return txa;
}

static int
ut_replay_multi(uint64_t tx_id, struct umem_action *act, void *arg)
{
	struct ut_tx_array	*txa = arg;
	struct umem_wal_tx	*tx, *prev_tx;
	struct ut_fake_tx	*fake_tx;
	int			 rc;

	if (tx_id != txa->ta_replay_tx) {
		/* No need to advance index for first tx */
		if (txa->ta_replay_tx != UINT64_MAX)
			txa->ta_tx_idx++;

		D_ASSERT(txa->ta_tx_idx < txa->ta_tx_nr);
		tx = txa->ta_tx_ptrs[txa->ta_tx_idx];

		/* Reset act index before replay */
		fake_tx = (struct ut_fake_tx *)&tx->utx_private;
		fake_tx->ft_act_idx = 0;

		/* Verify act nr of the prev replayed tx */
		if (txa->ta_replay_tx != UINT64_MAX) {
			D_ASSERT(txa->ta_tx_idx > 0);
			prev_tx = txa->ta_tx_ptrs[txa->ta_tx_idx - 1];
			fake_tx = (struct ut_fake_tx *)&prev_tx->utx_private;
			assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);
		}

		txa->ta_replay_tx = tx_id;
		txa->ta_replayed_nr++;
	} else {
		D_ASSERT(txa->ta_tx_idx < txa->ta_tx_nr);
		tx = txa->ta_tx_ptrs[txa->ta_tx_idx];
	}

	rc = ut_replay_one(tx_id, act, (void *)tx);
	assert_rc_equal(rc, 0);

	return 0;
}

static void
wal_ut_multi(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 i, tx_nr = 10, rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	txa = ut_txa_alloc(tx_nr);
	assert_non_null(txa);

	for (i = 0; i < tx_nr; i++) {
		tx = txa->ta_tx_ptrs[i];

		ut_tx_add_action(tx, UMEM_ACT_COPY);
		ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
		ut_tx_add_action(tx, UMEM_ACT_ASSIGN);
		ut_tx_add_action(tx, UMEM_ACT_MOVE);
		ut_tx_add_action(tx, UMEM_ACT_SET);
		ut_tx_add_action(tx, UMEM_ACT_SET_BITS);
		ut_tx_add_action(tx, UMEM_ACT_CLR_BITS);

		rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
		assert_rc_equal(rc, 0);

		rc = bio_wal_commit(args->bua_mc, tx, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Set replay start position */
	txa->ta_replay_nr = txa->ta_tx_nr;
	txa->ta_tx_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Verify the last tx */
	tx = txa->ta_tx_ptrs[txa->ta_tx_nr - 1];
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_txa_free(txa);

	ut_mc_fini(args);
}

static void
wal_ut_checkpoint(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 i, tx_nr = 20, ckp_idx, rc;
	uint64_t		 purge_size = 0;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	txa = ut_txa_alloc(tx_nr);
	assert_non_null(txa);

	for (i = 0; i < tx_nr; i++) {
		tx = txa->ta_tx_ptrs[i];

		ut_tx_add_action(tx, UMEM_ACT_SET_BITS);
		ut_tx_add_action(tx, UMEM_ACT_CLR_BITS);
		ut_tx_add_action(tx, UMEM_ACT_COPY);
		ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
		ut_tx_add_action(tx, UMEM_ACT_ASSIGN);
		ut_tx_add_action(tx, UMEM_ACT_MOVE);
		ut_tx_add_action(tx, UMEM_ACT_SET);

		rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
		assert_rc_equal(rc, 0);

		rc = bio_wal_commit(args->bua_mc, tx, NULL);
		assert_rc_equal(rc, 0);
	}

	ckp_idx = tx_nr / 2;
	tx = txa->ta_tx_ptrs[ckp_idx];
	rc = bio_wal_checkpoint(args->bua_mc, tx->utx_id, &purge_size);
	assert_rc_equal(rc, 0);
	assert_int_not_equal(purge_size, 0);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Set replay start position */
	txa->ta_replay_nr = tx_nr - ckp_idx - 1;
	txa->ta_tx_idx = ckp_idx + 1;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Verify the last tx */
	tx = txa->ta_tx_ptrs[txa->ta_tx_nr - 1];
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_txa_free(txa);

	ut_mc_fini(args);
}

static void
ut_fill_wal(struct bio_ut_args *args, int tx_nr, struct ut_tx_array **txa_ptr)
{
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 i, rc;
	uint64_t		 purge_size = 0;

	txa = ut_txa_alloc(tx_nr);
	assert_non_null(txa);

	/* Make the compiler happy */
	D_ASSERT(tx_nr > 0);
	tx = txa->ta_tx_ptrs[0];

	/*
	 * Each tx is roughly 800k, 22 txs will consume 17600k, which is more than
	 * half of 32MB WAL size.
	 */
	for (i = 0; i < tx_nr; i++) {
		tx = txa->ta_tx_ptrs[i];

		/* Specify 800k copy ptr size */
		fake_tx = (struct ut_fake_tx *)&tx->utx_private;
		fake_tx->ft_copy_ptr_sz = UT_MAX_BUF_SZ;

		ut_tx_add_action(tx, UMEM_ACT_COPY_PTR);
		ut_tx_add_action(tx, UMEM_ACT_ASSIGN);
		ut_tx_add_action(tx, UMEM_ACT_SET);

		rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
		assert_rc_equal(rc, 0);

		rc = bio_wal_commit(args->bua_mc, tx, NULL);
		assert_rc_equal(rc, 0);
	}

	if (txa_ptr == NULL) {
		rc = bio_wal_checkpoint(args->bua_mc, tx->utx_id, &purge_size);
		assert_rc_equal(rc, 0);
		assert_int_not_equal(purge_size, 0);

		ut_txa_free(txa);
	} else {
		*txa_ptr = txa;
	}
}

static void
wal_ut_wrap(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (32ULL << 20);	/* 32 MB */
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 tx_nr = 22, rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	ut_fill_wal(args, tx_nr, NULL);
	ut_fill_wal(args, tx_nr, &txa);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Set replay start position */
	txa->ta_replay_nr = tx_nr;
	txa->ta_tx_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Verify the last tx */
	tx = txa->ta_tx_ptrs[txa->ta_tx_nr - 1];
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_txa_free(txa);

	ut_mc_fini(args);
}

static void
wal_ut_wrap_many(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (32ULL << 20);	/* 32 MB */
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 tx_nr = 22, rc;

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	ut_fill_wal(args, tx_nr, NULL);
	ut_fill_wal(args, tx_nr, NULL);
	ut_fill_wal(args, tx_nr, NULL);
	ut_fill_wal(args, tx_nr, NULL);
	ut_fill_wal(args, tx_nr, &txa);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Set replay start position */
	txa->ta_replay_nr = tx_nr;
	txa->ta_tx_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Verify the last tx */
	tx = txa->ta_tx_ptrs[txa->ta_tx_nr - 1];
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_txa_free(txa);

	ut_mc_fini(args);
}

static void
wal_ut_holes(void **state)
{
	struct bio_ut_args	*args = *state;
	uint64_t		 meta_sz = (128ULL << 20);	/* 128 MB */
	struct ut_tx_array	*txa;
	struct umem_wal_tx	*tx;
	struct ut_fake_tx	*fake_tx;
	int			 i, tx_nr = 2, rc;

	FAULT_INJECTION_REQUIRED();

	rc = ut_mc_init(args, meta_sz, meta_sz, meta_sz);
	assert_rc_equal(rc, 0);

	if (!ioc2d_bdev(args->bua_mc->mc_wal)->bb_unmap_supported) {
		print_message("Device doesn't support unmap, skipping...\n");
		ut_mc_fini(args);
		return;
	}

	txa = ut_txa_alloc(tx_nr);
	assert_non_null(txa);

	/* Commit T1 & T3, drop the T1 to generate a hole in WAL */
	daos_fail_loc_set(DAOS_NVME_WAL_TX_LOST | DAOS_FAIL_ONCE);
	for (i = 0; i < tx_nr; i++) {
		tx = txa->ta_tx_ptrs[i];

		ut_tx_add_action(tx, UMEM_ACT_COPY);

		rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
		assert_rc_equal(rc, 0);

		rc = bio_wal_commit(args->bua_mc, tx, NULL);
		assert_rc_equal(rc, 0);
	}
	daos_fail_loc_set(0);

	/* Ensure committed_id in WAL header not being bumped */
	daos_fail_loc_set(DAOS_NVME_WAL_TX_LOST | DAOS_FAIL_ONCE);
	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);
	daos_fail_loc_set(0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* No TX should be replayed */
	txa->ta_replay_nr = 0;
	txa->ta_tx_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Commit T3 to fill the hole generated by T1 */
	tx = txa->ta_tx_ptrs[0];
	rc = bio_wal_reserve(args->bua_mc, &tx->utx_id);
	assert_rc_equal(rc, 0);

	rc = bio_wal_commit(args->bua_mc, tx, NULL);
	assert_rc_equal(rc, 0);

	rc = bio_mc_close(args->bua_mc);
	assert_rc_equal(rc, 0);

	rc = bio_mc_open(args->bua_xs_ctxt, args->bua_pool_id, 0, &args->bua_mc);
	assert_rc_equal(rc, 0);

	/* Only T3 should be replayed */
	txa->ta_replay_nr = 1;
	txa->ta_tx_idx = 0;

	rc = bio_wal_replay(args->bua_mc, NULL, ut_replay_multi, txa);
	assert_rc_equal(rc, 0);
	assert_int_equal(txa->ta_replayed_nr, txa->ta_replay_nr);

	/* Verify the last replayed tx */
	tx = txa->ta_tx_ptrs[0];
	fake_tx = (struct ut_fake_tx *)&tx->utx_private;
	assert_int_equal(fake_tx->ft_act_nr, fake_tx->ft_act_idx);

	ut_txa_free(txa);
	ut_mc_fini(args);
}

static const struct CMUnitTest wal_uts[] = {
	{ "single tx commit/replay", wal_ut_single, NULL, NULL},
	{ "single tx with many acts", wal_ut_many_acts, NULL, NULL},
	{ "single tx with large payload", wal_ut_large_payload, NULL, NULL},
	{ "multiple tx commit/replay", wal_ut_multi, NULL, NULL},
	{ "replay after checkpoint", wal_ut_checkpoint, NULL, NULL},
	{ "wal log wraps once", wal_ut_wrap, NULL, NULL},
	{ "wal log wraps many", wal_ut_wrap_many, NULL, NULL},
	{ "holes on replay", wal_ut_holes, NULL, NULL},
};

static int
wal_ut_teardown(void **state)
{
	struct bio_ut_args	*args = *state;

	ut_fini(args);
	return 0;
}

static int
wal_ut_setup(void **state)
{
	int	rc;

	rc = ut_init(&ut_args);
	if (rc) {
		D_ERROR("UT init failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	*state = &ut_args;
	return 0;
}

int
run_wal_tests(void)
{
	return cmocka_run_group_tests_name("WAL unit tests", wal_uts,
					   wal_ut_setup, wal_ut_teardown);
}
