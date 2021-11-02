/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_obj.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include "daos_iotest.h"
#include "../../object/obj_ec.h"
#include <daos_types.h>
#include <daos/checksum.h>
#include <daos/placement.h>
#include <daos_metrics.h>

static int mdts_obj_class = OC_RP_2G1;
static int metrics_disabled = 1;

static pthread_barrier_t	bar;
static int			total_nodes;

/** Metrics */
daos_metrics_ucntrs_t   *cal_pool_cntrs;
daos_metrics_ucntrs_t   *cal_cont_cntrs;
daos_metrics_ucntrs_t   *cal_obj_cntrs;
daos_metrics_ustats_t   *cal_obj_up_stat;
daos_metrics_ustats_t   *cal_obj_fh_stat;
daos_metrics_udists_t   *cal_obj_dist_iosz;
daos_metrics_udists_t   *cal_obj_dist_uprp;
daos_metrics_udists_t   *cal_obj_dist_upec;

daos_metrics_ucntrs_t   *act_pool_cntrs;
daos_metrics_ucntrs_t   *act_cont_cntrs;
daos_metrics_ucntrs_t   *act_obj_cntrs;
daos_metrics_ustats_t   *act_obj_up_stat;
daos_metrics_ustats_t   *act_obj_fh_stat;
daos_metrics_udists_t   *act_obj_dist_iosz;
daos_metrics_udists_t   *act_obj_dist_uprp;
daos_metrics_udists_t   *act_obj_dist_upec;

struct prot_info {
	int oclass;
	int mclass;
	int num_nodes;
	int parity_info;
};

struct prot_info prot_rp[] = {
	{ OC_SX, DAOS_METRICS_DIST_NORP, 1, 0 },
	{ OC_RP_2GX, DAOS_METRICS_DIST_RP2, 2, 0 },
	{ OC_RP_3GX, DAOS_METRICS_DIST_RP3, 3, 0 },
	{ OC_RP_4GX, DAOS_METRICS_DIST_RP4, 4, 0 },
	{ OC_RP_6GX, DAOS_METRICS_DIST_RP6, 6, 0 },
	{ OC_RP_8GX, DAOS_METRICS_DIST_RP8, 8, 0 },
	{ OC_RP_12G1, DAOS_METRICS_DIST_RP12, 12, 0 },
	{ OC_RP_16G1, DAOS_METRICS_DIST_RP16, 16, 0 },
	{ OC_RP_24G1, DAOS_METRICS_DIST_RP24, 24, 0 },
	{ OC_RP_32G1, DAOS_METRICS_DIST_RP32, 32, 0 },
	{ OC_RP_48G1, DAOS_METRICS_DIST_RP48, 48, 0 },
	{ OC_RP_64G1, DAOS_METRICS_DIST_RP64, 64, 0 },
	{ OC_RP_128G1, DAOS_METRICS_DIST_RP128, 128 }
};
#define N_RP		(sizeof(prot_rp)/sizeof(struct prot_info))

struct prot_info prot_ec[] = {
	{ OC_EC_2P1GX, DAOS_METRICS_DIST_EC2P1, 3, 1 },
	{ OC_EC_2P2GX, DAOS_METRICS_DIST_EC2P2, 4, 2 },
	{ OC_EC_4P1GX, DAOS_METRICS_DIST_EC4P1, 5, 1 },
	{ OC_EC_4P2GX, DAOS_METRICS_DIST_EC4P2, 6, 2 },
	{ OC_EC_8P1GX, DAOS_METRICS_DIST_EC8P1, 9, 1 },
	{ OC_EC_8P2GX, DAOS_METRICS_DIST_EC8P2, 10, 2 },
	{ OC_EC_16P1GX, DAOS_METRICS_DIST_EC16P1, 17, 1 },
	{ OC_EC_16P2GX, DAOS_METRICS_DIST_EC16P2, 18, 2 }
};
#define N_EC	(sizeof(prot_ec)/sizeof(struct prot_info))

static int
is_metrics_enabled()
{
	int rc;
	int major, minor;

	rc = daos_metrics_get_version(&major, &minor);
	if (rc == 1) {
		print_message("Client DAOS metrics is not enabled\n");
		print_message("All tests will be skipped\n");
		return 1;
	}
	assert_rc_equal(rc, 0);
	if (major != DAOS_METRICS_MAJOR_VERSION) {
		print_message("Metrics version mismatch\n");
		return 1;
	} else if (minor < DAOS_METRICS_MINOR_VERSION) {
		print_message("Metrics version mismatch\n");
		return 1;
	}
	return rc;
}

static int
test_metrics_init()
{
	int rc;

	metrics_disabled = is_metrics_enabled();

	if (metrics_disabled)
		return 1;

	rc = daos_metrics_alloc_cntrsbuf(&cal_pool_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_cntrsbuf(&act_pool_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_cntrsbuf(&cal_cont_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_cntrsbuf(&act_cont_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_cntrsbuf(&cal_obj_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_cntrsbuf(&act_obj_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_statsbuf(&cal_obj_up_stat);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_statsbuf(&act_obj_up_stat);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_statsbuf(&cal_obj_fh_stat);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_statsbuf(&act_obj_fh_stat);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&cal_obj_dist_iosz);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&act_obj_dist_iosz);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&cal_obj_dist_uprp);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&act_obj_dist_uprp);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&cal_obj_dist_upec);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_alloc_distbuf(&act_obj_dist_upec);
	assert_rc_equal(rc, 0);
	return 0;
}

static void
test_metrics_fini()
{
	if (metrics_disabled)
		return;
	daos_metrics_dump(stdout);
	daos_metrics_free_cntrsbuf(cal_pool_cntrs);
	daos_metrics_free_cntrsbuf(act_pool_cntrs);
	daos_metrics_free_cntrsbuf(cal_cont_cntrs);
	daos_metrics_free_cntrsbuf(act_cont_cntrs);
	daos_metrics_free_cntrsbuf(cal_obj_cntrs);
	daos_metrics_free_cntrsbuf(act_obj_cntrs);
	daos_metrics_free_statsbuf(cal_obj_up_stat);
	daos_metrics_free_statsbuf(act_obj_up_stat);
	daos_metrics_free_statsbuf(cal_obj_fh_stat);
	daos_metrics_free_statsbuf(act_obj_fh_stat);
	daos_metrics_free_distbuf(cal_obj_dist_iosz);
	daos_metrics_free_distbuf(act_obj_dist_iosz);
	daos_metrics_free_distbuf(cal_obj_dist_uprp);
	daos_metrics_free_distbuf(act_obj_dist_uprp);
	daos_metrics_free_distbuf(cal_obj_dist_upec);
	daos_metrics_free_distbuf(act_obj_dist_upec);
}

static void
test_metrics_snapshot()
{
	int rc;

	rc = daos_metrics_get_cntrs(DAOS_METRICS_POOL_RPC_CNTR, cal_pool_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_get_cntrs(DAOS_METRICS_CONT_RPC_CNTR, cal_cont_cntrs);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_get_cntrs(DAOS_METRICS_OBJ_RPC_CNTR, cal_obj_cntrs);
	assert_rc_equal(rc, 0);

	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_UPDATE_STATS, cal_obj_up_stat);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_FETCH_STATS, cal_obj_fh_stat);
	assert_rc_equal(rc, 0);

	rc = daos_metrics_get_dist(DAOS_METRICS_IO_DIST_SZ, cal_obj_dist_iosz);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_RP, cal_obj_dist_uprp);
	assert_rc_equal(rc, 0);
	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_EC, cal_obj_dist_upec);
	assert_rc_equal(rc, 0);
}

#define COMPARE_COUNTER(act, cal, cname) {					\
	if (((act)->cname.mc_inflight != (cal)->cname.mc_inflight) ||		\
	    ((act)->cname.mc_success != (cal)->cname.mc_success)   ||		\
	    /** Retriable failures cannot be calculated */			\
	    ((act)->cname.mc_failure < (cal)->cname.mc_failure)) {		\
		print_message("cntr compare %s failed\n", #cname);		\
		return 1;							\
	}									\
}

static int
compare_pool_counters()
{
	int rc;

	print_message("validating the pool counters\n");
	daos_metrics_pool_rpc_cntrs_t *pool_act = &act_pool_cntrs->u.arc_pool_cntrs;
	daos_metrics_pool_rpc_cntrs_t *pool_cal = &cal_pool_cntrs->u.arc_pool_cntrs;

	rc = daos_metrics_get_cntrs(DAOS_METRICS_POOL_RPC_CNTR, act_pool_cntrs);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_pool_cntrs->mc_grp, DAOS_METRICS_POOL_RPC_CNTR);
	COMPARE_COUNTER(pool_act, pool_cal, prc_connect_cnt);
	COMPARE_COUNTER(pool_act, pool_cal, prc_disconnect_cnt);
	COMPARE_COUNTER(pool_act, pool_cal, prc_attr_cnt);
	COMPARE_COUNTER(pool_act, pool_cal, prc_query_cnt);
	return 0;
}

static int
compare_cont_counters()
{
	int rc;

	print_message("validating the container counters\n");
	daos_metrics_cont_rpc_cntrs_t *cont_act = &act_cont_cntrs->u.arc_cont_cntrs;
	daos_metrics_cont_rpc_cntrs_t *cont_cal = &cal_cont_cntrs->u.arc_cont_cntrs;

	rc = daos_metrics_get_cntrs(DAOS_METRICS_CONT_RPC_CNTR, act_cont_cntrs);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_cont_cntrs->mc_grp, DAOS_METRICS_CONT_RPC_CNTR);
	COMPARE_COUNTER(cont_act, cont_cal, crc_create_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_destroy_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_open_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_close_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_snapshot_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_snaplist_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_snapdel_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_attr_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_acl_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_prop_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_query_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_oidalloc_cnt);
	COMPARE_COUNTER(cont_act, cont_cal, crc_aggregate_cnt);
	return 0;
}

static int
compare_obj_counters()
{
	int rc;

	print_message("validating the object counters\n");
	daos_metrics_obj_rpc_cntrs_t *obj_act = &act_obj_cntrs->u.arc_obj_cntrs;
	daos_metrics_obj_rpc_cntrs_t *obj_cal = &cal_obj_cntrs->u.arc_obj_cntrs;

	rc = daos_metrics_get_cntrs(DAOS_METRICS_OBJ_RPC_CNTR, act_obj_cntrs);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_cntrs->mc_grp, DAOS_METRICS_OBJ_RPC_CNTR);
	COMPARE_COUNTER(obj_act, obj_cal, orc_update_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_fetch_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_obj_punch_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_dkey_punch_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_akey_punch_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_obj_enum_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_dkey_enum_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_akey_enum_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_akey_enum_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_sync_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_querykey_cnt);
	COMPARE_COUNTER(obj_act, obj_cal, orc_cpd_cnt);

	return 0;
}

static int
compare_stats(daos_metrics_stat_t *first, daos_metrics_stat_t *second)
{
	if ((first->st_value != second->st_value) ||
	    (first->st_min != second->st_min) ||
	    (first->st_max != second->st_max) ||
	    (first->st_sum != second->st_sum) ||
	    (first->st_sum_of_squares != second->st_sum_of_squares)) {
		print_message("stats metrics does not match\n");
		return 1;
	}
	return 0;
}

static int
compare_obj_stats()
{
	int rc;

	print_message("validating the io stats metrics\n");
	daos_metrics_stat_t *obj_up_act = &act_obj_up_stat->u.st_obj_update;
	daos_metrics_stat_t *obj_fh_act = &act_obj_fh_stat->u.st_obj_fetch;
	daos_metrics_stat_t *obj_up_cal = &cal_obj_up_stat->u.st_obj_update;
	daos_metrics_stat_t *obj_fh_cal = &cal_obj_fh_stat->u.st_obj_fetch;

	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_UPDATE_STATS, act_obj_up_stat);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_up_stat->ms_grp, DAOS_METRICS_OBJ_UPDATE_STATS);
	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_FETCH_STATS, act_obj_fh_stat);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_fh_stat->ms_grp, DAOS_METRICS_OBJ_FETCH_STATS);

	rc = compare_stats(obj_up_act, obj_up_cal);
	assert_rc_equal(rc, 0);
	rc = compare_stats(obj_fh_act, obj_fh_cal);
	assert_rc_equal(rc, 0);

	return 0;
}

static int
get_io_bktbsz(size_t size)
{
	if (size < 1024)
		return DAOS_METRICS_DIST_IO_0_1K;
	else if (size < 2*1024)
		return DAOS_METRICS_DIST_IO_1K_2K;
	else if (size < 4*1024)
		return DAOS_METRICS_DIST_IO_2K_4K;
	else if (size < 8*1024)
		return DAOS_METRICS_DIST_IO_4K_8K;
	else if (size < 16*1024)
		return DAOS_METRICS_DIST_IO_8K_16K;
	else if (size < 32*1024)
		return DAOS_METRICS_DIST_IO_16K_32K;
	else if (size < 64*1024)
		return DAOS_METRICS_DIST_IO_32K_64K;
	else if (size < 128*1024)
		return DAOS_METRICS_DIST_IO_64K_128K;
	else if (size < 256*1024)
		return DAOS_METRICS_DIST_IO_128K_256K;
	else if (size < 512*1024)
		return DAOS_METRICS_DIST_IO_256K_512K;
	else if (size < 1024*1024)
		return DAOS_METRICS_DIST_IO_512K_1M;
	else if (size < 1024*1024*2)
		return DAOS_METRICS_DIST_IO_1M_2M;
	else if (size < 1024*1024*4)
		return DAOS_METRICS_DIST_IO_2M_4M;
	else
		return DAOS_METRICS_DIST_IO_4M_INF;
}

static int
compare_obj_iodist()
{
	int rc, i;

	print_message("validating the io distribution metrics\n");
	daos_metrics_iodist_sz_t *iodsz_act = act_obj_dist_iosz->u.md_iosz;
	daos_metrics_iodist_sz_t *iodsz_cal = cal_obj_dist_iosz->u.md_iosz;
	daos_metrics_updist_rp_t *updrp_act = act_obj_dist_uprp->u.md_uprp;
	daos_metrics_updist_rp_t *updrp_cal = cal_obj_dist_uprp->u.md_uprp;
	daos_metrics_updist_ec_t *updec_act = act_obj_dist_upec->u.md_upec;
	daos_metrics_updist_ec_t *updec_cal = cal_obj_dist_upec->u.md_upec;

	rc = daos_metrics_get_dist(DAOS_METRICS_IO_DIST_SZ, act_obj_dist_iosz);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_dist_iosz->md_grp, DAOS_METRICS_IO_DIST_SZ);

	for (i = 0; i < DAOS_METRICS_DIST_IO_BKT_COUNT; i++) {
		assert_int_equal(iodsz_act[i].ids_updatecnt, iodsz_cal[i].ids_updatecnt);
		assert_int_equal(iodsz_act[i].ids_fetchcnt, iodsz_cal[i].ids_fetchcnt);
	}

	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_RP, act_obj_dist_uprp);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_dist_uprp->md_grp, DAOS_METRICS_UP_DIST_RP);
	for (i = 0; i < DAOS_METRICS_DIST_RP_BKT_COUNT; i++) {
		assert_int_equal(updrp_act[i].udrp_updatecnt, updrp_cal[i].udrp_updatecnt);
		assert_int_equal(updrp_act[i].udrp_updatesz, updrp_cal[i].udrp_updatesz);
	}

	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_EC, act_obj_dist_upec);
	assert_rc_equal(rc, 0);
	assert_int_equal(act_obj_dist_upec->md_grp, DAOS_METRICS_UP_DIST_EC);
	for (i = 0; i < DAOS_METRICS_DIST_EC_BKT_COUNT; i++) {
		assert_int_equal(updec_act[i].udec_full_updatecnt,
					updec_cal[i].udec_full_updatecnt);
		assert_int_equal(updec_act[i].udec_part_updatecnt,
					updec_cal[i].udec_part_updatecnt);
		assert_int_equal(updec_act[i].udec_full_updatesz,
					updec_cal[i].udec_full_updatesz);
		assert_int_equal(updec_act[i].udec_part_updatesz,
					updec_cal[i].udec_part_updatesz);
	}
	return 0;
}

static void
test_metrics_compare()
{
	int rc;

	print_message("Comparing the metrics values\n");
	rc = compare_pool_counters();
	assert_rc_equal(rc, 0);
	rc = compare_cont_counters();
	assert_rc_equal(rc, 0);
	rc = compare_obj_counters();
	assert_rc_equal(rc, 0);
	rc = compare_obj_stats();
	assert_rc_equal(rc, 0);
	rc = compare_obj_iodist();
	assert_rc_equal(rc, 0);
}

static int
get_rp_factor(int factor)
{
	assert_int_equal(factor == DAOS_METRICS_DIST_RPU, 0);
	switch (factor) {

	case DAOS_METRICS_DIST_RP2:
		return 2;
	case DAOS_METRICS_DIST_RP3:
		return 3;
	case DAOS_METRICS_DIST_RP4:
		return 4;
	case DAOS_METRICS_DIST_RP6:
		return 6;
	case DAOS_METRICS_DIST_RP8:
		return 8;
	case DAOS_METRICS_DIST_RP12:
		return 12;
	case DAOS_METRICS_DIST_RP16:
		return 16;
	case DAOS_METRICS_DIST_RP24:
		return 24;
	case DAOS_METRICS_DIST_RP32:
		return 32;
	case DAOS_METRICS_DIST_RP48:
		return 48;
	case DAOS_METRICS_DIST_RP64:
		return 64;
	case DAOS_METRICS_DIST_RP128:
		return 128;
	default:
		break;

	}
	return 1;
}

static inline daos_size_t
get_ec_singlevalue_size(int size, int k , int p)
{
	if (size <= OBJ_EC_SINGV_EVENDIST_SZ(k))
		return (p + 1) * size;
	else
		return ((size * (p + k)) / k);
};

static daos_size_t
get_ec_factored_size(daos_size_t size, int factor)
{
	assert_int_equal(factor == DAOS_METRICS_DIST_ECU, 0);
	switch (factor) {
		case DAOS_METRICS_DIST_EC2P1:
			return get_ec_singlevalue_size(size, 2, 1);
		case DAOS_METRICS_DIST_EC2P2:
			return get_ec_singlevalue_size(size, 2, 2);
		case DAOS_METRICS_DIST_EC4P1:
			return get_ec_singlevalue_size(size, 4, 1);
		case DAOS_METRICS_DIST_EC4P2:
			return get_ec_singlevalue_size(size, 4, 2);
		case DAOS_METRICS_DIST_EC8P1:
			return get_ec_singlevalue_size(size, 8, 1);
		case DAOS_METRICS_DIST_EC8P2:
			return get_ec_singlevalue_size(size, 8, 2);
		case DAOS_METRICS_DIST_EC16P1:
			return get_ec_singlevalue_size(size, 16, 1);
		case DAOS_METRICS_DIST_EC16P2:
			return get_ec_singlevalue_size(size, 16, 2);
		default:
			assert_int_equal(factor == DAOS_METRICS_DIST_ECU, 1);
			break;
	}
	/* won't reach here */
	return 0;

}

static void
acct_obj_update(int cnt, daos_size_t size, int ptype, int factor, int is_part)
{
	int bkt;

	if (ptype == 0)
		size *= get_rp_factor(factor);
	else if (ptype == 1) /* EC Single Value */
		size = get_ec_factored_size(size, factor);

	cal_obj_cntrs->u.arc_obj_cntrs.orc_update_cnt.mc_success += cnt;

	cal_obj_up_stat->u.st_obj_update.st_value += cnt;
	if (cal_obj_up_stat->u.st_obj_update.st_min > size)
		cal_obj_up_stat->u.st_obj_update.st_min = size;
	else if (cal_obj_up_stat->u.st_obj_update.st_value == 1)
		cal_obj_up_stat->u.st_obj_update.st_min = size;
	if (cal_obj_up_stat->u.st_obj_update.st_max < size)
		cal_obj_up_stat->u.st_obj_update.st_max = size*cnt;
	cal_obj_up_stat->u.st_obj_update.st_sum += size*cnt;
	cal_obj_up_stat->u.st_obj_update.st_sum_of_squares += size*size*cnt*cnt;

	bkt = get_io_bktbsz(size);
	cal_obj_dist_iosz->u.md_iosz[bkt].ids_updatecnt += cnt;

	if (ptype == 0) { /* RP */
		cal_obj_dist_uprp->u.md_uprp[factor].udrp_updatecnt += cnt;
		cal_obj_dist_uprp->u.md_uprp[factor].udrp_updatesz += size*cnt;
	} else { /* EC ptype == 1 => single value, ptype == 2 => array*/
		if (is_part) {
			cal_obj_dist_upec->u.md_upec[factor].udec_part_updatecnt += cnt;
			cal_obj_dist_upec->u.md_upec[factor].udec_part_updatesz += size*cnt;
		} else {
			cal_obj_dist_upec->u.md_upec[factor].udec_full_updatecnt += cnt;
			cal_obj_dist_upec->u.md_upec[factor].udec_full_updatesz += size*cnt;
		}
	}
}

static void
acct_obj_fetch(int cnt, daos_size_t size, int ptype)
{
	int bkt;

	cal_obj_cntrs->u.arc_obj_cntrs.orc_fetch_cnt.mc_success += cnt;

	cal_obj_fh_stat->u.st_obj_fetch.st_value += cnt;
	if (cal_obj_fh_stat->u.st_obj_fetch.st_min > size)
		cal_obj_fh_stat->u.st_obj_fetch.st_min = size;
	else if (cal_obj_fh_stat->u.st_obj_fetch.st_value == 1)
		cal_obj_fh_stat->u.st_obj_fetch.st_min = size;
	if (cal_obj_fh_stat->u.st_obj_fetch.st_max < size)
		cal_obj_fh_stat->u.st_obj_fetch.st_max = size*cnt;
	cal_obj_fh_stat->u.st_obj_fetch.st_sum += size*cnt;
	cal_obj_fh_stat->u.st_obj_fetch.st_sum_of_squares += size*size*cnt;

	bkt = get_io_bktbsz(size);
	cal_obj_dist_iosz->u.md_iosz[bkt].ids_fetchcnt += cnt;
}

/** Pool Tests */
/** connect/disconnect to/from a valid pool */
static void
pool_connect(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_pool_info_t info = {0};
	int		 rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_success += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_disconnect_cnt.mc_success += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_query_cnt.mc_success += 1;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	/** connect to pool */
	print_message("rank 0 connecting to pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
		       DAOS_PC_RW, &poh, &info,
		       arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_memory_equal(info.pi_uuid, arg->pool.pool_uuid,
		    sizeof(info.pi_uuid));
	/** TODO: assert_int_equal(info.pi_ntargets, arg->...); */
	assert_int_equal(info.pi_ndisabled, 0);
	print_message("success\n");

	print_message("rank 0 querying pool info... ");
	memset(&info, 'D', sizeof(info));
	info.pi_bits = DPI_ALL;
	rc = daos_pool_query(poh, NULL /* tgts */, &info, NULL,
			     arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(info.pi_ndisabled, 0);
	print_message("success\n");

	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
		/* disable the async after testing done */
		arg->async = false;
	}
	print_message("rank %d success\n", arg->myrank);
	test_metrics_compare();
}

/** connect exclusively to a pool */
static void
pool_connect_exclusively(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_handle_t	 poh_ex;
	int		 rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_success += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_failure += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_disconnect_cnt.mc_success += 1;

	print_message("SUBTEST 1: other connections already exist; shall get "
		      "%d\n", -DER_BUSY);
	print_message("establishing a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("trying to establish an exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, -DER_BUSY);
	print_message("disconnecting the non-exclusive connection\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	test_metrics_compare();
}

#define BUFSIZE 10

static void
pool_attribute(void **state)
{
	test_arg_t *arg = *state;
	daos_event_t	 ev;
	daos_handle_t	 poh;
	int		 rc;

	/** STRDUP to handle a bug in verbs */
	char const *const names[] = { strdup("AVeryLongName"), strdup("Name") };
	char const *const names_get[] = { strdup("AVeryLongName"), strdup("Wrong"),
					  strdup("Name") };
	size_t const name_sizes[] = {
				strlen(names[0]) + 1,
				strlen(names[1]) + 1,
	};
	void const *const in_values[] = {
				strdup("value"),
				strdup("this is a long value")
	};
	size_t const in_sizes[] = {
				strlen(in_values[0]),
				strlen(in_values[1])
	};
	int			 n = (int) ARRAY_SIZE(names);
	int			 m = (int) ARRAY_SIZE(names_get);
	char			 out_buf[10 * BUFSIZE] = { 0 };
	void			*out_values[] = {
						  &out_buf[0 * BUFSIZE],
						  &out_buf[1 * BUFSIZE],
						  &out_buf[2 * BUFSIZE]
						};
	size_t			 out_sizes[] =	{ BUFSIZE, BUFSIZE, BUFSIZE };
	size_t			 total_size;

	if (metrics_disabled) {
		free((char *)names[0]); free((char *)names[1]);
		free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
		free((char *)in_values[0]); free((char *)in_values[1]);
		skip();
	}
	if (arg->myrank != 0) {
		free((char *)names[0]); free((char *)names[1]);
		free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
		free((char *)in_values[0]); free((char *)in_values[1]);
		return;
	}

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_success += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_disconnect_cnt.mc_success += 1;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_attr_cnt.mc_success += 8;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	print_message("connecting to pool\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, DAOS_PC_RW, &poh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("setting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_pool_set_attr(poh, n, names, in_values, in_sizes, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_pool_list_attr(poh, NULL, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_pool_list_attr(poh, out_buf, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10*BUFSIZE;
	rc = daos_pool_list_attr(poh, out_buf, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);
	assert_string_equal(out_buf + name_sizes[1], names[0]);

	print_message("getting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_pool_get_attr(poh, m, names_get, out_values, out_sizes, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying Name-Value (A)..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_memory_equal(out_values[0], in_values[0], in_sizes[0]);

	print_message("Verifying Name-Value (B)..\n");
	assert_int_equal(out_sizes[1], 0);

	print_message("Verifying Name-Value (C)..\n");
	assert_true(in_sizes[1] > BUFSIZE);
	assert_int_equal(out_sizes[2], in_sizes[1]);
	assert_memory_equal(out_values[2], in_values[1], BUFSIZE);

	rc = daos_pool_get_attr(poh, m, names_get, NULL, out_sizes, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying with NULL buffer..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_int_equal(out_sizes[1], 0);
	assert_int_equal(out_sizes[2], in_sizes[1]);

	print_message("Deleting all attributes\n");
	rc = daos_pool_del_attr(poh, m, names_get, arg->async ? &ev : NULL);
	/** should work even if "Wrong" do not exist */
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying all attributes deletion\n");
	total_size = 0;
	rc = daos_pool_list_attr(poh, NULL, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(total_size, 0);

	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	test_metrics_compare();
	free((char *)names[0]); free((char *)names[1]);
	free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
	free((char *)in_values[0]); free((char *)in_values[1]);
}

/** Run Query and container list operations.
  */
static void
pool_query_list(void **state)
{
	test_arg_t		*arg = *state;
	int			 rc;
	daos_prop_t		*prop_query;
	daos_size_t		 nconts;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_query_cnt.mc_success += 2;

	/***** Test: retrieve number of containers in pool *****/
	nconts =  0xDEF0; /* Junk value (e.g., uninitialized) */
	rc = daos_pool_list_cont(arg->pool.poh, &nconts, NULL /* conts */,
			NULL /* ev */);
	print_message("daos_pool_list_cont returned rc=%d\n", rc);
	assert_rc_equal(rc, 0);

	prop_query = daos_prop_alloc(0);
	rc = daos_pool_query(arg->pool.poh, NULL, NULL, prop_query, NULL);
	assert_rc_equal(rc, 0);

	print_message("success\n");
	test_metrics_compare();
}

static void
expect_pool_connect_access(test_arg_t *arg0, uint64_t perms,
			   uint64_t flags, int exp_result)
{
	test_arg_t	*arg = NULL;
	daos_prop_t	*prop;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	arg->pool.pool_connect_flags = flags;
	prop = get_daos_prop_with_owner_acl_perms(perms,
						  DAOS_PROP_PO_ACL);

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);

	/** Make sure we actually got to pool connect */
	assert_int_equal(arg->setup_state, SETUP_POOL_CONNECT);
	assert_rc_equal(rc, exp_result);

	daos_prop_free(prop);
	test_teardown((void **)&arg);
}

static void
pool_connect_access(void **state)
{
	test_arg_t	*arg0 = *state;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_success += 3;
	/*failure is +2 more from test_teardown on rank0 */
	cal_pool_cntrs->u.arc_pool_cntrs.prc_connect_cnt.mc_failure += 4;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_disconnect_cnt.mc_success += 3;
	cal_pool_cntrs->u.arc_pool_cntrs.prc_query_cnt.mc_success += 6;

	print_message("pool ACL gives the owner no permissions\n");
	expect_pool_connect_access(arg0, 0, DAOS_PC_RO, -DER_NO_PERM);

	print_message("pool ACL gives the owner RO, they want RW\n");
	expect_pool_connect_access(arg0, DAOS_ACL_PERM_READ, DAOS_PC_RW,
				   -DER_NO_PERM);

	print_message("pool ACL gives the owner RO, they want RO\n");
	expect_pool_connect_access(arg0, DAOS_ACL_PERM_READ, DAOS_PC_RO,
				   0);

	print_message("pool ACL gives the owner RW, they want RO\n");
	expect_pool_connect_access(arg0,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_PC_RO,
				   0);

	print_message("pool ACL gives the owner RW, they want RW\n");
	expect_pool_connect_access(arg0,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_PC_RW,
				   0);
	test_metrics_compare();
}


/** create/destroy container */
static void
co_create(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	daos_event_t	 ev;
	int		 rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 1;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	/** container uuid */
	uuid_generate(uuid);

	/** create container */
	print_message("creating container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_create(arg->pool.poh, uuid, NULL,
			      arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container created\n");

	print_message("opening container %ssynchronously\n",
		      arg->async ? "a" : "");
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh,
			    &info, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("contained opened\n");

	print_message("closing container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container closed\n");

	/** destroy container */
	/* XXX check if this is a real leak or out-of-sync close */
	sleep(5);
	print_message("destroying container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_destroy(arg->pool.poh, uuid, 1 /* force */,
			    arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	print_message("container destroyed\n");

	test_metrics_compare();
}

#define BUFSIZE 10

static void
co_attribute(void **state)
{
	test_arg_t *arg = *state;
	daos_event_t	 ev;
	int		 rc;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;

	char const *const names[] = { strdup("AVeryLongName"), strdup("Name") };
	char const *const names_get[] = { strdup("AVeryLongName"), strdup("Wrong"),
					  strdup("Name") };
	size_t const name_sizes[] = {
				strlen(names[0]) + 1,
				strlen(names[1]) + 1,
	};
	void const *const in_values[] = {
				strdup("value"),
				strdup("this is a long value"),
	};
	size_t const in_sizes[] = {
				strlen(in_values[0]),
				strlen(in_values[1]),
	};
	int			 n = (int)ARRAY_SIZE(names);
	int			 m = (int)ARRAY_SIZE(names_get);
	char			 out_buf[10 * BUFSIZE] = { 0 };
	void			*out_values[] = {
						  &out_buf[0 * BUFSIZE],
						  &out_buf[1 * BUFSIZE],
						  &out_buf[2 * BUFSIZE],
						};
	size_t			 out_sizes[] =	{ BUFSIZE, BUFSIZE, BUFSIZE };
	size_t			 total_size;

	if (metrics_disabled) {
		free((char *)names[0]); free((char *)names[1]);
		free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
		free((char *)in_values[0]); free((char *)in_values[1]);
		skip();
	}
	if (arg->myrank != 0) {
		free((char *)names[0]); free((char *)names[1]);
		free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
		free((char *)in_values[0]); free((char *)in_values[1]);
		return;
	}

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_attr_cnt.mc_success += 8;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	/** container uuid */
	uuid_generate(uuid);

	/** create container */
	print_message("creating container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_create(arg->pool.poh, uuid, NULL,
			      arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container created\n");

	print_message("opening container %ssynchronously\n",
		      arg->async ? "a" : "");
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh,
			    &info, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("contained opened\n");

	print_message("setting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_set_attr(coh, n, names, in_values, in_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_cont_list_attr(coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_cont_list_attr(coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10 * BUFSIZE;
	rc = daos_cont_list_attr(coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);
	assert_string_equal(out_buf + name_sizes[1], names[0]);

	print_message("getting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_cont_get_attr(coh, m, names_get, out_values, out_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying Name-Value (A)..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_memory_equal(out_values[0], in_values[0], in_sizes[0]);

	print_message("Verifying Name-Value (B)..\n");
	assert_int_equal(out_sizes[1], 0);

	print_message("Verifying Name-Value (C)..\n");
	assert_true(in_sizes[1] > BUFSIZE);
	assert_int_equal(out_sizes[2], in_sizes[1]);
	assert_memory_equal(out_values[2], in_values[1], BUFSIZE);

	rc = daos_cont_get_attr(coh, m, names_get, NULL, out_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying with NULL buffer..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_int_equal(out_sizes[1], 0);
	assert_int_equal(out_sizes[2], in_sizes[1]);

	rc = daos_cont_del_attr(coh, m, names_get,
				arg->async ? &ev : NULL);
	/* should work even if "Wrong" do not exist */
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying all attributes deletion\n");
	total_size = 0;
	rc = daos_cont_list_attr(coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(total_size, 0);

	print_message("closing container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container closed\n");

	/** destroy container */
	/* XXX check if this is a real leak or out-of-sync close */
	sleep(5);
	print_message("destroying container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_destroy(arg->pool.poh, uuid, 1 /* force */,
			    arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container destroyed\n");

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	test_metrics_compare();
	free((char *)names[0]); free((char *)names[1]);
	free((char *)names_get[0]); free((char *)names_get[1]); free((char *)names_get[2]);
	free((char *)in_values[0]); free((char *)in_values[1]);
}

static bool
ace_has_permissions(struct daos_ace *ace, uint64_t perms)
{
	if (ace->dae_access_types != DAOS_ACL_ACCESS_ALLOW) {
		print_message("Expected access type allow for ACE\n");
		daos_ace_dump(ace, 0);
		return false;
	}

	if (ace->dae_allow_perms != perms) {
		print_message("Expected allow perms %#lx for ACE\n", perms);
		daos_ace_dump(ace, 0);
		return false;
	}

	return true;
}

static bool
is_cont_acl_prop_default(struct daos_acl *prop)
{
	struct daos_ace *ace;
	ssize_t		acl_expected_len = 0;

	if (daos_acl_validate(prop) != 0) {
		print_message("ACL property not valid\n");
		daos_acl_dump(prop);
		return false;
	}

	if (daos_acl_get_ace_for_principal(prop, DAOS_ACL_OWNER,
					   NULL, &ace) != 0) {
		print_message("Owner ACE not found\n");
		return false;
	}

	acl_expected_len += daos_ace_get_size(ace);

	/* Owner should have full control of the container by default */
	if (!ace_has_permissions(ace, DAOS_ACL_PERM_CONT_ALL)) {
		print_message("Owner ACE was wrong\n");
		return false;
	}

	if (daos_acl_get_ace_for_principal(prop, DAOS_ACL_OWNER_GROUP,
					   NULL, &ace) != 0) {
		print_message("Owner Group ACE not found\n");
		return false;
	}

	acl_expected_len += daos_ace_get_size(ace);

	/* Owner-group should have basic access */
	if (!ace_has_permissions(ace,
				 DAOS_ACL_PERM_READ |
				 DAOS_ACL_PERM_WRITE |
				 DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_SET_PROP)) {
		print_message("Owner Group ACE was wrong\n");
		return false;
	}

	if (prop->dal_len != acl_expected_len) {
		print_message("More ACEs in list than expected, expected len = "
			      "%ld, actual len = %u\n", acl_expected_len,
			      prop->dal_len);
		return false;
	}

	print_message("ACL prop matches expected defaults\n");
	return true;
}

static daos_prop_t *
get_query_prop_all(void)
{
	daos_prop_t	*prop;
	const int	prop_count = DAOS_PROP_CO_NUM;
	int		i;

	prop = daos_prop_alloc(prop_count);
	assert_non_null(prop);

	for (i = 0; i < prop_count; i++) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_MIN + 1 + i;
		assert_true(prop->dpp_entries[i].dpe_type < DAOS_PROP_CO_MAX);
	}

	return prop;
}

static void
co_properties(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	char			*label = "test_cont_properties";
	char			*label2 = "test_cont_prop_label2";
	char			*foo_label = "foo";
	char			*label2_v2 = "test_cont_prop_label2_version2";
	uuid_t			 cuuid2;
	daos_handle_t		 coh2;
	uuid_t			 cuuid3;
	daos_handle_t		 coh3;
	uuid_t			 cuuid4;
	uint64_t		 snapshot_max = 128;
	daos_prop_t		*prop;
	daos_prop_t		*prop_query;
	struct daos_prop_entry	*entry;
	daos_pool_info_t	 info = {0};
	int			 rc;
	char			*exp_owner;
	char			*exp_owner_grp;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	prop = daos_prop_alloc(2);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	prop->dpp_entries[0].dpe_str = strdup(label);
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop->dpp_entries[1].dpe_val = snapshot_max;
	D_STRNDUP(arg->cont_label, label, DAOS_PROP_LABEL_MAX_LEN);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_pool_cntrs->u.arc_pool_cntrs.prc_query_cnt.mc_success += 1;

	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 3;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_failure += 4;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_prop_cnt.mc_success += 3;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_query_cnt.mc_success += 1;


	rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_debug_set_params(arg->group, info.pi_leader,
		DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
	assert_rc_equal(rc, 0);

	prop_query = get_query_prop_all();
	rc = daos_cont_query(arg->coh, NULL, prop_query, NULL);
	assert_rc_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, DAOS_PROP_CO_NUM);
	/* set properties should get the value user set */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LABEL);
	if (entry == NULL || strcmp(entry->dpe_str, label) != 0) {
		print_message("label verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_SNAPSHOT_MAX);
	if (entry == NULL || entry->dpe_val != snapshot_max) {
		print_message("snapshot_max verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_OFF) {
		print_message("csum verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	if (entry == NULL || entry->dpe_val != 32 * 1024) {
		print_message("csum chunk size verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query,
				    DAOS_PROP_CO_CSUM_SERVER_VERIFY);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_SV_OFF) {
		print_message("csum server verify verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ENCRYPT);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_ENCRYPT_OFF) {
		print_message("encrypt verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL ||
	    !is_cont_acl_prop_default((struct daos_acl *)entry->dpe_val_ptr)) {
		print_message("ACL prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	/* default owner */
	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &exp_owner), 0);
	print_message("Checking owner set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner, DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	D_FREE(exp_owner);

	/* default owner-group */
	assert_int_equal(daos_acl_gid_to_principal(getegid(), &exp_owner_grp),
			 0);
	print_message("Checking owner-group set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner_grp,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner-group prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	D_FREE(exp_owner_grp);

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
			     0, NULL);

	/* Create container: different UUID, same label - fail */
	print_message("Checking create: different UUID same label "
		      "(will fail)\n");
	uuid_generate(cuuid2);
	rc = daos_cont_create(arg->pool.poh, cuuid2, prop, NULL);
	assert_rc_equal(rc, -DER_EXIST);

	/* Create container: same UUID, different label - fail */
	print_message("Checking create: same UUID, different label "
		      "(will fail)\n");
	free(prop->dpp_entries[0].dpe_str);
	prop->dpp_entries[0].dpe_str = strdup(label2);
	rc = daos_cont_create(arg->pool.poh, arg->co_uuid, prop, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* Create container: same UUID, no label - pass (idempotent) */
	print_message("Checking create: same UUID, no label\n");
	rc = daos_cont_create(arg->pool.poh, arg->co_uuid, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* Create container C2: no UUID specified, new label - pass */
	print_message("Checking create: different UUID and label\n");
	rc = daos_cont_create_with_label(arg->pool.poh, label2, NULL,
					 NULL, NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("created container C2: %s\n", label2);
	/* Open by label, and immediately close */
	rc = daos_cont_open(arg->pool.poh, label2, DAOS_COO_RW, &coh2,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh2, NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("opened and closed container %s\n", label2);

	/* Create container: C1 UUID, different label - fail
	 * uuid matches first container, label matches second container
	 */
	print_message("Checking create: same UUID, different label "
		      "(will fail)\n");
	rc = daos_cont_create(arg->pool.poh, arg->co_uuid, prop, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* destroy the container C2 (will re-create it next) */
	rc = daos_cont_destroy(arg->pool.poh, label2, 0 /* force */,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("destroyed container C2: %s\n", label2);

	/* Create C3 with an initial label, rename to old C2 label2
	 * Create container with label2  - fail.
	 */
	print_message("Checking set-prop and create label conflict "
		      "(will fail)\n");
	rc = daos_cont_create_with_label(arg->pool.poh, foo_label,
					 NULL /* prop */, &cuuid3,
					 NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("step1: created container C3: %s : "
		      "UUID:"DF_UUIDF"\n", foo_label, DP_UUID(cuuid3));
	rc = daos_cont_open(arg->pool.poh, foo_label, DAOS_COO_RW,
			    &coh3, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("step2: C3 set-prop, rename %s -> %s\n",
		      foo_label, prop->dpp_entries[0].dpe_str);
	rc = daos_cont_set_prop(coh3, prop, NULL);
	assert_rc_equal(rc, 0);
	uuid_generate(cuuid4);
	print_message("step3: create cont with label: %s (will fail)\n",
		      prop->dpp_entries[0].dpe_str);
	rc = daos_cont_create(arg->pool.poh, cuuid4, prop, NULL);
	assert_rc_equal(rc, -DER_EXIST);

	/* Container 3 set-prop label2_v2,
	 * container 1 set-prop label2 - pass
	 */
	print_message("Checking label rename and reuse\n");
	free(prop->dpp_entries[0].dpe_str);
	prop->dpp_entries[0].dpe_str = strdup(label2_v2);
	print_message("step: C3 set-prop change FROM %s TO %s\n",
		      label2, label2_v2);
	rc = daos_cont_set_prop(coh3, prop, NULL);
	assert_rc_equal(rc, 0);
	free(prop->dpp_entries[0].dpe_str);
	prop->dpp_entries[0].dpe_str = strdup(label2);
	print_message("step: C1 set-prop change FROM %s TO %s\n",
		      label, label2);
	rc = daos_cont_set_prop(arg->coh, prop, NULL);
	assert_rc_equal(rc, 0);

	/* destroy container C3 */
	rc = daos_cont_close(coh3, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, label2_v2, 0 /* force */,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("destroyed container C3: %s : "
		      "UUID:"DF_UUIDF"\n", label2_v2, DP_UUID(cuuid3));

	test_metrics_compare();
	daos_prop_free(prop);
	daos_prop_free(prop_query);
	test_teardown((void **)&arg);
}

static void
co_destroy_access_denied(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*pool_prop;
	daos_prop_t	*cont_prop;
	int		 rc;
	struct daos_acl	*cont_acl = NULL;
	struct daos_ace	*update_ace;
	daos_handle_t	coh;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	/*
	 * Pool doesn't give the owner delete cont privs. For the pool, write
	 * is an alias for create+del container.
	 */
	pool_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_POOL_ALL &
						       ~DAOS_ACL_PERM_DEL_CONT &
						       ~DAOS_ACL_PERM_WRITE,
						       DAOS_PROP_PO_ACL);

	/* container doesn't give delete privs to the owner */
	cont_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_CONT_ALL &
						       ~DAOS_ACL_PERM_DEL_CONT,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CREATE)
		rc = test_setup_next_step((void **)&arg, NULL, pool_prop,
					  cont_prop);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_acl_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_failure += 1;

	print_message("Try to delete container where pool and cont "
		      "deny access\n");
	rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Delete with privs from container ACL only\n");

	cont_acl = daos_acl_dup(cont_prop->dpp_entries[0].dpe_val_ptr);
	assert_non_null(cont_acl);
	rc = daos_acl_get_ace_for_principal(cont_acl, DAOS_ACL_OWNER,
					    NULL,
					    &update_ace);
	assert_rc_equal(rc, 0);
	update_ace->dae_allow_perms = DAOS_ACL_PERM_CONT_ALL;

	print_message("- getting container handle\n");
	rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
			    &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("- updating cont ACL to restore delete privs\n");
	rc = daos_cont_update_acl(coh, cont_acl, NULL);
	assert_rc_equal(rc, 0);

	print_message("- closing container\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("Deleting container now should succeed\n");
	rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
	assert_rc_equal(rc, 0);

	/* Clear cont uuid since we already deleted it */
	uuid_clear(arg->co_uuid);

	test_metrics_compare();

	daos_acl_free(cont_acl);
	daos_prop_free(pool_prop);
	daos_prop_free(cont_prop);
	test_teardown((void **)&arg);
}


static void
expect_cont_open_access(test_arg_t *arg, uint64_t perms, uint64_t flags,
			int exp_result)
{
	daos_prop_t	*prop;
	int		 rc = 0;

	arg->cont_open_flags = flags;
	prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);

	if (arg->myrank == 0) {
		/* Make sure we actually got to the container open step */
		assert_int_equal(arg->setup_state, SETUP_CONT_CONNECT);
		assert_int_equal(rc, exp_result);
	}

	/* Cleanup */
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
	daos_prop_free(prop);
}

static void
co_open_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		rc;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT /*SETUP_EQ*/, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 5;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 5;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 3;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_failure += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 3;

	print_message("cont ACL gives the user no permissions\n");
	expect_cont_open_access(arg, 0, DAOS_COO_RO, -DER_NO_PERM);

	print_message("cont ACL gives the user RO, they want RW\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ, DAOS_COO_RW,
				-DER_NO_PERM);

	print_message("cont ACL gives the user RO, they want RO\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ, DAOS_COO_RO,
				0);

	print_message("cont ACL gives the user RW, they want RO\n");
	expect_cont_open_access(arg,
				DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				DAOS_COO_RO,
				0);

	print_message("cont ACL gives the user RW, they want RW\n");
	expect_cont_open_access(arg,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_COO_RW,
				   0);

	test_metrics_compare();
	test_teardown((void **)&arg);
}

static void
expect_co_query_access(test_arg_t *arg, daos_prop_t *query_prop,
		       uint64_t perms, int exp_result)
{
	daos_prop_t		*cont_prop;
	daos_cont_info_t	 info;
	int			 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_query(arg->coh, &info, query_prop, NULL);
		assert_rc_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static daos_prop_t *
get_single_query_prop(uint32_t type)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = type;

	return prop;
}

static void
co_query_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*prop;
	int		rc;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT/*SETUP_EQ*/, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 17;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 17;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 17;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 17;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_query_cnt.mc_success += 9;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_query_cnt.mc_failure += 8;

	print_message("Not asking for any props\n");
	expect_co_query_access(arg, NULL,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~DAOS_ACL_PERM_GET_PROP &
			       ~DAOS_ACL_PERM_GET_ACL,
			       -0);

	print_message("Empty prop object (all props), but no get-prop\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Empty prop object (all props), but no get-ACL\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Empty prop object (all props), with access\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("All props with no get-prop access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("All props with no get-ACL access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("All props with only prop and ACL access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	/*
	 * ACL props can only be accessed by users with get-ACL permission
	 */
	print_message("ACL prop with no get-ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_ACL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("ACL prop with only get-ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_ACL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	/*
	 * Props unrelated to access/ACLs can only be accessed by users with
	 * the get-prop permission
	 */
	print_message("Non-access-related prop with no get-prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_LABEL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Non-access-related prop with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_LABEL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	/*
	 * Ownership props can be accessed by users with either get-prop or
	 * get-acl access
	 */
	print_message("Owner with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	print_message("Owner with only ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("Owner with neither get-prop nor get-acl access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~(DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_GET_ACL),
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Owner-group with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	print_message("Owner-group with only ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("Owner-group with no get-prop or get-acl access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~(DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_GET_ACL),
			       -DER_NO_PERM);

	daos_prop_free(prop);

	test_metrics_compare();
	test_teardown((void **)&arg);
}

static void
expect_co_get_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t		*cont_prop;
	daos_prop_t		*acl_prop;
	int			 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
		assert_rc_equal(rc, exp_result);

		if (rc == 0)
			daos_prop_free(acl_prop);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_get_acl_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		rc;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 2;
	/* ACL get is cont query */
	cal_cont_cntrs->u.arc_cont_cntrs.crc_query_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_query_cnt.mc_failure += 1;

	print_message("No get-ACL permissions\n");
	expect_co_get_acl_access(arg,
				 DAOS_ACL_PERM_CONT_ALL &
				 ~DAOS_ACL_PERM_GET_ACL,
				 -DER_NO_PERM);

	print_message("Only get-ACL permissions\n");
	expect_co_get_acl_access(arg, DAOS_ACL_PERM_GET_ACL, 0);

	test_metrics_compare();

	test_teardown((void **)&arg);
}

static void
expect_co_overwrite_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_overwrite_acl(arg->coh, acl, NULL);
		assert_rc_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_update_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_update_acl(arg->coh, acl, NULL);
		assert_rc_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_delete_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_delete_acl(arg->coh, DAOS_ACL_OWNER, NULL, NULL);
		assert_rc_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_modify_acl_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		 rc;
	uint64_t	 no_set_acl_perm = DAOS_ACL_PERM_CONT_ALL &
					   ~DAOS_ACL_PERM_SET_ACL;
	uint64_t	 min_set_acl_perm = DAOS_ACL_PERM_READ |
					    DAOS_ACL_PERM_SET_ACL;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	test_metrics_snapshot();
	cal_cont_cntrs->u.arc_cont_cntrs.crc_create_cnt.mc_success += 6;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_destroy_cnt.mc_success += 6;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_open_cnt.mc_success += 6;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_close_cnt.mc_success += 6;
	/** Overwriting ACL is nothing but setting property. */
	cal_cont_cntrs->u.arc_cont_cntrs.crc_prop_cnt.mc_success += 1;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_prop_cnt.mc_failure += 1;
	/** Update ACL */
	cal_cont_cntrs->u.arc_cont_cntrs.crc_acl_cnt.mc_success += 2;
	cal_cont_cntrs->u.arc_cont_cntrs.crc_acl_cnt.mc_failure += 2;

	print_message("Overwrite ACL denied with no set-ACL perm\n");
	expect_co_overwrite_acl_access(arg, no_set_acl_perm,
				       -DER_NO_PERM);

	print_message("Overwrite ACL allowed with set-ACL perm\n");
	expect_co_overwrite_acl_access(arg, min_set_acl_perm,
				       0);

	print_message("Update ACL denied with no set-ACL perm\n");
	expect_co_update_acl_access(arg,
				    DAOS_ACL_PERM_CONT_ALL &
				    ~DAOS_ACL_PERM_SET_ACL,
				    -DER_NO_PERM);

	print_message("Update ACL allowed with set-ACL perm\n");
	expect_co_update_acl_access(arg,
				    DAOS_ACL_PERM_READ |
				    DAOS_ACL_PERM_SET_ACL,
				    0);

	print_message("Delete ACL denied with no set-ACL perm\n");
	expect_co_delete_acl_access(arg,
				    DAOS_ACL_PERM_CONT_ALL &
				    ~DAOS_ACL_PERM_SET_ACL,
				    -DER_NO_PERM);

	print_message("Delete ACL allowed with set-ACL perm\n");
	expect_co_delete_acl_access(arg,
				    DAOS_ACL_PERM_READ |
				    DAOS_ACL_PERM_SET_ACL,
				    0);

	test_metrics_compare();

	test_teardown((void **)&arg);
}

static void
co_snapshot(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	struct ioreq	 req;
	int		 rc, i, snap_cnt;
	uint64_t	noid;
	daos_obj_id_t	oid;
	daos_epoch_t epoch_in[5], epoch_out[5];
	daos_epoch_range_t epr;
	daos_anchor_t anchor;

	if (metrics_disabled)
		skip();
	if (arg0->myrank != 0)
		return;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	test_metrics_snapshot();
	for (i = 0; i < 5; i++) {
		printf("Creating snap %d\n", i);
		insert_single("dkey1", "akey1", 0, "data",
		       strlen("data") + 1, DAOS_TX_NONE, &req);
		acct_obj_update(1, strlen("data")+1, 0, DAOS_METRICS_DIST_RP2, 0);
		if (i & 0x1) {
			rc = daos_cont_create_snap(arg->coh, &epoch_in[i], NULL, NULL);
			assert_rc_equal(rc, 0);
		} else {
			rc = daos_cont_create_snap_opt(arg->coh, &epoch_in[i], NULL,
							DAOS_SNAP_OPT_CR, NULL);
			assert_rc_equal(rc, 0);
		}
		cal_cont_cntrs->u.arc_cont_cntrs.crc_snapshot_cnt.mc_success += 1;
		sleep(1);
	}
	insert_single("dkey1", "akey1", 0, "data",
	       strlen("DATA") + 1, DAOS_TX_NONE, &req);
	acct_obj_update(1, strlen("data")+1, 0, DAOS_METRICS_DIST_RP2, 0);

	epr.epr_lo = epoch_in[2];
	epr.epr_hi = epoch_in[2];
	rc = daos_cont_destroy_snap(arg->coh,  epr, NULL);
	cal_cont_cntrs->u.arc_cont_cntrs.crc_snapdel_cnt.mc_success += 1;
	assert_rc_equal(rc, 0);
	memset(epoch_out, 0xAA, 5 * sizeof(daos_epoch_t));
	memset(&anchor, 0, sizeof(anchor));
	snap_cnt = 5;

	rc = daos_cont_list_snap(arg->coh, &snap_cnt, epoch_out, NULL, &anchor, NULL);
	cal_cont_cntrs->u.arc_cont_cntrs.crc_snaplist_cnt.mc_success += 1;

	assert_rc_equal(rc, 0);
	assert_int_equal(snap_cnt, 4);

	for (i = 0; i < 5; i++) {
		printf("Destroying snap %d\n", i);
		epr.epr_lo = epoch_in[i];
		epr.epr_hi = epoch_in[i];
		rc = daos_cont_destroy_snap(arg->coh,  epr, NULL);
		if (i != 2) {
			assert_rc_equal(rc, 0);
			cal_cont_cntrs->u.arc_cont_cntrs.crc_snapdel_cnt.mc_success += 1;
		} else {
			/** Already destroyed */
			cal_cont_cntrs->u.arc_cont_cntrs.crc_snapdel_cnt.mc_failure += 1;
		}
	}

	daos_cont_aggregate(arg->coh, epoch_in[4], NULL);
	assert_rc_equal(rc, 0);
	cal_cont_cntrs->u.arc_cont_cntrs.crc_aggregate_cnt.mc_success += 1;

	rc = daos_cont_alloc_oids(arg->coh, 1, &noid, NULL);
	assert_rc_equal(rc, 0);
	printf("oid returned by daos_cont_alloc_oids - %lu\n", noid);
	cal_cont_cntrs->u.arc_cont_cntrs.crc_oidalloc_cnt.mc_success += 1;

	test_metrics_compare();

	ioreq_fini(&req);
	test_teardown((void **)&arg);
}

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	test_metrics_snapshot();

	for (offset = (UINT64_MAX >> 1); offset > 0; offset >>= 8) {
		char buf[10];

		print_message("idx offset: %lu\n", offset);

		/** Insert */
		insert_single("var_idx_off_d", "var_idx_off_a", offset, "data",
		       strlen("data") + 1, DAOS_TX_NONE, &req);
		acct_obj_update(1, strlen("data")+1, 0, DAOS_METRICS_DIST_RP2, 0);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_idx_off_d", "var_idx_off_a", offset,
			      buf, 10, DAOS_TX_NONE, &req);
		acct_obj_fetch(1, strlen("data")+1, DAOS_METRICS_DIST_RP2);
		assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");

	}

	test_metrics_compare();

	ioreq_fini(&req);
}

/**
 * Test I/O and data verification with variable unaligned record sizes for both
 * NVMe and SCM.
 */
static void
io_var_rec_size(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	 dkey_num;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1U << 24;
	char		*fetch_buf;
	char		*update_buf;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	dkey_num = rand();

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	test_metrics_snapshot();

	D_ALLOC(fetch_buf, max_size);
	assert_non_null(fetch_buf);

	D_ALLOC(update_buf, max_size);
	assert_non_null(update_buf);

	dts_buf_render(update_buf, max_size);

	for (size = 1; size <= max_size; size <<= 1, dkey_num++) {
		char dkey[30];

		/**
		 * Adjust size to be unaligned, always include 1 byte test
		 * (minimal supported size).
		 */
		size += (size == 1) ? 0 : (rand() % 10);
		print_message("Record size: %lu val: \'%c\' dkey: %lu\n",
			      size, update_buf[0], dkey_num);

		/** Insert */
		sprintf(dkey, DF_U64, dkey_num);
		insert_single(dkey, "var_rec_size_a", 0, update_buf,
			      size, DAOS_TX_NONE, &req);

		acct_obj_update(1, size, 0, DAOS_METRICS_DIST_RP2, 0);

		/** Lookup */
		memset(fetch_buf, 0, max_size);
		lookup_single(dkey, "var_rec_size_a", 0, fetch_buf,
			      max_size, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);

		acct_obj_fetch(1, size, DAOS_METRICS_DIST_RP2);
	}

	D_FREE(update_buf);
	D_FREE(fetch_buf);
	test_metrics_compare();

	ioreq_fini(&req);
}

/**
 * Test update/fetch with data verification of varying size and IOD type.
 * Size is either small I/O to SCM or larger (>=4k) I/O to NVMe, and IOD
 * type is either array or single value.
 */
static void
mio_simple_internal(void **state, daos_obj_id_t oid, unsigned int size,
		   daos_iod_type_t iod_type, const char dkey[],
		   const char akey[])
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*fetch_buf;
	char		*update_buf;

	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);
	D_ALLOC(update_buf, size);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, size);

	/** Insert */
	insert_single(dkey, akey, 0, update_buf, size, DAOS_TX_NONE, &req);

	/** Lookup */
	memset(fetch_buf, 0, size);
	lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE, &req);

	/** Verify data consistency */
	if (!daos_obj_is_echo(oid)) {
		assert_int_equal(req.iod[0].iod_size, size);
		assert_memory_equal(update_buf, fetch_buf, size);
	}
	punch_dkey(dkey, DAOS_TX_NONE, &req);

	D_FREE(update_buf);
	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

static void
mio_simple_internal_acct(unsigned int size)
{
	int factor = DAOS_METRICS_DIST_RP2;

	if (mdts_obj_class == OC_S1)
		factor = DAOS_METRICS_DIST_NORP;
	acct_obj_update(1, size, 0, factor, 0);
	acct_obj_fetch(1, size, factor);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_punch_cnt.mc_success += 1;
}

/**
 * Very basic update/fetch with data verification with varying record size and
 * IOD type.
 */
static void
io_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");

	/** Test first for SCM, then on NVMe with record size > 4k */
	print_message("DAOS_IOD_ARRAY:SCM\n");
	mio_simple_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
			   "io_simple_scm_array dkey",
			   "io_simple_scm_array akey");
	mio_simple_internal_acct(IO_SIZE_SCM);
	print_message("DAOS_IOD_ARRAY:NVMe\n");
	mio_simple_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
			   "io_simple_nvme_array dkey",
			   "io_simple_nvme_array akey");
	mio_simple_internal_acct(IO_SIZE_NVME);
	print_message("DAOS_IOD_SINGLE:SCM\n");
	mio_simple_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
			   "io_simple_scm_single dkey",
			   "io_simple_scm_single akey");
	mio_simple_internal_acct(IO_SIZE_SCM);
	print_message("DAOS_IOD_SINGLE:NVMe\n");
	mio_simple_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
			   "io_simple_nvme_single dkey",
			   "io_simple_nvme_single akey");
	mio_simple_internal_acct(IO_SIZE_NVME);
	print_message("Comparing Metrics values\n");
	test_metrics_compare();
}

#define ENUM_KEY_BUF		32 /* size of each dkey/akey */
#define ENUM_LARGE_KEY_BUF	(512 * 1024) /* 512k large key */
#define ENUM_KEY_REC_NR		10 /* number of keys/records to insert */
#define ENUM_PRINT		100 /* print every 100th key/record */
#define ENUM_DESC_NR		5 /* number of keys/records returned by enum */
#define ENUM_DESC_BUF		512 /* all keys/records returned by enum */
#define ENUM_IOD_SIZE		1024 /* used for mixed record enumeration */
#define ENUM_NR_NVME		5 /* consecutive rec exts in an NVMe extent */
#define ENUM_NR_SCM		2 /* consecutive rec exts in an SCM extent */

static void
insert_records(daos_obj_id_t oid, struct ioreq *req, char *data_buf,
	       uint64_t start_idx)
{
	uint64_t	 idx;
	int		 num_rec_exts;
	int		 i;

	print_message("Insert %d records from index "DF_U64
		      " under the same key (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      start_idx, DP_OID(oid));
	idx = start_idx; /* record extent index */
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		/* insert alternating SCM (2k) and NVMe (5k) records */
		if (i % 2 == 0)
			num_rec_exts = ENUM_NR_SCM; /* rx_nr=2 for SCM test */
		else
			num_rec_exts = ENUM_NR_NVME; /* rx_nr=5 for NVMe test */
		insert_single_with_rxnr("d_key", "a_rec", idx, data_buf,
					ENUM_IOD_SIZE, num_rec_exts,
					DAOS_TX_NONE, req);
		acct_obj_update(1, ENUM_IOD_SIZE*num_rec_exts, 0, DAOS_METRICS_DIST_RP2, 0);
		idx += num_rec_exts;
		/* Prevent records coalescing on aggregation */
		idx += 1;
	}
}

static int
iterate_records(struct ioreq *req, char *dkey, char *akey, int iod_size)
{
	daos_anchor_t	anchor;
	int		key_nr;
	int		i;
	uint32_t	number;

	/** Enumerate all mixed NVMe and SCM records */
	key_nr = 0;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		daos_epoch_range_t	eprs[5];
		daos_recx_t		recxs[5];
		daos_size_t		size;

		number = 5;
		enumerate_rec(DAOS_TX_NONE, dkey, akey, &size,
			      &number, recxs, eprs, &anchor, true, req);
		cal_obj_cntrs->u.arc_obj_cntrs.orc_recx_enum_cnt.mc_success += 1;
		if (number == 0)
			continue;

		for (i = 0; i < (number - 1); i++) {
			assert_true(size == iod_size);
			/* Print a subset of enumerated records */
			if ((i + key_nr) % ENUM_PRINT != 0)
				continue;
			print_message("i:%d iod_size:%d rx_nr:%d, rx_idx:%d\n",
				      i + key_nr, (int)size,
				      (int)recxs[i].rx_nr,
				      (int)recxs[i].rx_idx);
			i++; /* print the next record to see both rec sizes */
			print_message("i:%d iod_size:%d rx_nr:%d, rx_idx:%d\n",
				      i + key_nr, (int)size,
				      (int)recxs[i].rx_nr,
				      (int)recxs[i].rx_idx);

		}
		key_nr += number;
	}

	return key_nr;
}

#define ENUM_BUF_SIZE (128 * 1024)
/** very basic enumerate */
static void
enumerate_simple(void **state)
{
	test_arg_t	*arg = *state;
	char		*small_buf;
	char		*buf;
	daos_size_t	 buf_len;
	char		*ptr;
	char		 key[ENUM_KEY_BUF];
	char		*large_key = NULL;
	char		*large_buf = NULL;
	char		*data_buf;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t	 anchor;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	 number;
	int		 key_nr;
	int		 i;
	int		 rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	test_metrics_snapshot();

	D_ALLOC(small_buf, ENUM_DESC_BUF);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	memset(large_key, 'L', ENUM_LARGE_KEY_BUF);
	large_key[ENUM_LARGE_KEY_BUF - 1] = '\0';
	D_ALLOC(large_buf, ENUM_LARGE_KEY_BUF * 2);

	D_ALLOC(data_buf, ENUM_BUF_SIZE);
	assert_non_null(data_buf);
	dts_buf_render(data_buf, ENUM_BUF_SIZE);

	/**
	 * Insert 1000 dkey records, all with the same key value and the same
	 * akey.
	 */
	print_message("Insert %d dkeys (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      DP_OID(oid));
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		sprintf(key, "%d", i);
		if (i == ENUM_KEY_REC_NR/3) {
			/* Insert one large dkey (512K "L's") */
			print_message("Insert (i=%d) dkey=LARGE_KEY\n", i);
			insert_single(large_key, "a_key", 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
			acct_obj_update(1, strlen("data") + 1, 0, DAOS_METRICS_DIST_RP2, 0);
		} else {
			/* Insert dkeys 0-999 */
			insert_single(key, "a_key", 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
			acct_obj_update(1, strlen("data") + 1, 0, DAOS_METRICS_DIST_RP2, 0);
		}
	}

	/* Enumerate all dkeys */
	print_message("Enumerate dkeys\n");
	memset(&anchor, 0, sizeof(anchor));
	for (number = ENUM_DESC_NR, key_nr = 0;
	     !daos_anchor_is_eof(&anchor);
	     number = ENUM_DESC_NR) {
		buf = small_buf;
		buf_len = ENUM_DESC_BUF;
		memset(buf, 0, buf_len);
		/**
		 * Return an array of "number" dkeys to buf, using "kds" for
		 * index to get the dkey.
		 */
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
				    buf_len, &req);
		if (rc == 0)
			cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_enum_cnt.mc_success += 1;
		else
			cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_enum_cnt.mc_failure += 1;
		if (rc == -DER_KEY2BIG) {
			/**
			 * Retry dkey enumeration with a larger buffer since
			 * one of the returned key descriptors is the large key.
			 */
			print_message("Ret:-DER_KEY2BIG, len:"DF_U64"\n",
				      kds[0].kd_key_len);
			assert_int_equal((int)kds[0].kd_key_len,
					 ENUM_LARGE_KEY_BUF - 1);
			buf = large_buf;
			buf_len = ENUM_LARGE_KEY_BUF * 2;
			rc = enumerate_dkey(DAOS_TX_NONE, &number, kds,
					    &anchor, buf, buf_len, &req);
			if (rc == 0)
				cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_enum_cnt.mc_success += 1;
			else
				cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_enum_cnt.mc_failure += 1;
		}
		assert_rc_equal(rc, 0);

		if (number == 0)
			continue; /* loop should break for EOF */

		for (ptr = buf, i = 0; i < number; i++) {
			if (kds[i].kd_key_len > ENUM_KEY_BUF) {
				print_message("dkey:'%c...' len:%d\n", ptr[0],
					      (int)kds[i].kd_key_len);
			} else if ((i + key_nr) % ENUM_PRINT == 0) {
				/* Print a subset of enumerated dkeys */
				snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
				print_message("i:%d dkey:%s len:%d\n",
					      i + key_nr, key,
					      (int)kds[i].kd_key_len);
			}
			ptr += kds[i].kd_key_len;
		}
		key_nr += number;
	}
	/* Confirm the number of dkeys enumerated equal the number inserted */
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert 1000 akey records, all with the same key value and the same
	 * dkey.
	 */
	print_message("Insert %d akeys (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      DP_OID(oid));
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		sprintf(key, "%d", i);
		if (i == ENUM_KEY_REC_NR/7) {
			/* Insert one large akey (512K "L's") */
			print_message("Insert (i=%d) akey=LARGE_KEY\n", i);
			insert_single("d_key", large_key, 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
			acct_obj_update(1, strlen("data") + 1, 0, DAOS_METRICS_DIST_RP2, 0);
		} else {
			/* Insert akeys 0-999 */
			insert_single("d_key", key, 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
			acct_obj_update(1, strlen("data") + 1, 0, DAOS_METRICS_DIST_RP2, 0);
		}
	}

	/* Enumerate all akeys */
	print_message("Enumerate akeys\n");
	memset(&anchor, 0, sizeof(anchor));
	for (number = ENUM_DESC_NR, key_nr = 0;
	     !daos_anchor_is_eof(&anchor);
	     number = ENUM_DESC_NR) {
		buf = small_buf;
		buf_len = ENUM_DESC_BUF;
		memset(buf, 0, buf_len);
		/**
		 * Return an array of "number" akeys to buf, using "kds" for
		 * index to get the akey.
		 */
		rc = enumerate_akey(DAOS_TX_NONE, "d_key", &number, kds,
				    &anchor, buf, buf_len, &req);
		if (rc == 0)
			cal_obj_cntrs->u.arc_obj_cntrs.orc_akey_enum_cnt.mc_success += 1;
		else
			cal_obj_cntrs->u.arc_obj_cntrs.orc_akey_enum_cnt.mc_failure += 1;
		if (rc == -DER_KEY2BIG) {
			/**
			 * Retry akey enumeration with a larger buffer since one
			 * of the returned key descriptors is the large key.
			 */
			print_message("Ret:-DER_KEY2BIG, len:"DF_U64"\n",
				      kds[0].kd_key_len);
			assert_int_equal((int)kds[0].kd_key_len,
					 ENUM_LARGE_KEY_BUF - 1);
			buf = large_buf;
			buf_len = ENUM_LARGE_KEY_BUF * 2;
			rc = enumerate_akey(DAOS_TX_NONE, "d_key", &number,
					    kds, &anchor, buf, buf_len, &req);
			if (rc == 0)
				cal_obj_cntrs->u.arc_obj_cntrs.orc_akey_enum_cnt.mc_success += 1;
			else
				cal_obj_cntrs->u.arc_obj_cntrs.orc_akey_enum_cnt.mc_failure += 1;
		}
		assert_rc_equal(rc, 0);

		if (number == 0)
			break; /* loop should break for EOF */

		for (ptr = buf, i = 0; i < number; i++) {
			if (kds[i].kd_key_len > ENUM_KEY_BUF) {
				print_message("akey:'%c...' len:%d\n", ptr[0],
					      (int)kds[i].kd_key_len);
			} else if ((i + key_nr) % ENUM_PRINT == 0) {
				/* Print a subset of enumerated akeys */
				snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
				print_message("i:%d akey:%s len:%d\n",
					      i + key_nr, key,
					     (int)kds[i].kd_key_len);
			}
			ptr += kds[i].kd_key_len;
		}
		key_nr += number;
	}
	/* Confirm the number of akeys enumerated equal the number inserted */
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert N mixed NVMe and SCM records, all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 0);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert N mixed NVMe and SCM records starting at offset 1,
	 * all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 1);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	/** Records could be merged with previous updates by aggregation */
	print_message("key_nr = %d\n", key_nr);

	/**
	 * Insert N mixed NVMe and SCM records starting at offset 2,
	 * all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 2);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	/** Records could be merged with previous updates by aggregation */
	print_message("key_nr = %d\n", key_nr);

	for (i = 0; i < 10; i++) {
		insert_single_with_rxnr("d_key", "a_lrec", i * 128 * 1024,
					data_buf, 1, 128 * 1024, DAOS_TX_NONE,
					&req);
		acct_obj_update(1, 128 * 1024*1, 0, DAOS_METRICS_DIST_RP2, 0);
	}
	key_nr = iterate_records(&req, "d_key", "a_lrec", 1);
	print_message("key_nr = %d\n", key_nr);
	D_FREE(small_buf);
	D_FREE(large_buf);
	D_FREE(large_key);
	D_FREE(data_buf);
	/** XXX Verify kds */
	test_metrics_compare();
	ioreq_fini(&req);
}

#define PUNCH_NUM_KEYS 5
#define PUNCH_IOD_SIZE 1024
#define PUNCH_SCM_NUM_EXTS 2 /* SCM 2k record */
#define PUNCH_NVME_NUM_EXTS 5 /* NVMe 5k record */
#define PUNCH_ENUM_NUM 2
/**
 * Test akey punch, dkey punch, record punch and object punch with mixed large
 * NVMe and small SCM record sizes. Verify punched keys with key
 * enumeration. Record enumeration is still under development, so for now verify
 * punched records with lookup only.
 */
static void
punch_simple_internal(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*buf;
	char		*dkeys[PUNCH_NUM_KEYS*2];
	char		*data_buf;
	int		 num_rec_exts = 0;
	int		 i;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	test_metrics_snapshot();

	D_ALLOC(data_buf, IO_SIZE_NVME);
	dts_buf_render(data_buf, IO_SIZE_NVME);
	D_ALLOC(buf, 512);

	/**
	 * Insert 1 record per akey at different dkeys. Record sizes are
	 * alternating SCM (2 consecutive extents = 2k), and NVME (5 consecutive
	 * record extents = 5k).
	 */
	print_message("Inserting records.\n");
	for (i = 0; i < PUNCH_NUM_KEYS*2; i++) {
		if (i % 2 == 0)
			num_rec_exts = PUNCH_SCM_NUM_EXTS;
		else
			num_rec_exts = PUNCH_NVME_NUM_EXTS;
		D_ASPRINTF(dkeys[i], "punch_simple_dkey%d", i);
		print_message("\tinsert dkey:%s, akey:'akey', rx_nr:%d\n",
			      dkeys[i], num_rec_exts);
		insert_single_with_rxnr(dkeys[i], "akey",/*idx*/ 0, data_buf,
					PUNCH_IOD_SIZE, num_rec_exts,
					DAOS_TX_NONE, &req);
		acct_obj_update(1, num_rec_exts*PUNCH_IOD_SIZE, 0, DAOS_METRICS_DIST_RP2, 0);
	}
	/* Insert a few more unique akeys at the first dkey */
	num_rec_exts = PUNCH_NVME_NUM_EXTS;
	print_message("\tinsert dkey:%s, akey:'akey0', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey0",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);
	acct_obj_update(1, num_rec_exts*PUNCH_IOD_SIZE, 0, DAOS_METRICS_DIST_RP2, 0);
	print_message("\tinsert dkey:%s, akey:'akey1', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey1",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);
	acct_obj_update(1, num_rec_exts*PUNCH_IOD_SIZE, 0, DAOS_METRICS_DIST_RP2, 0);

	/**
	 * Punch records.
	 */
	print_message("Punch a few records:\n");
	num_rec_exts = PUNCH_NVME_NUM_EXTS;
	print_message("\tpunch dkey:%s, akey:'akey0', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	punch_rec_with_rxnr(dkeys[0], "akey0", /*idx*/0, num_rec_exts,
			    DAOS_TX_NONE, &req);
	acct_obj_update(1, 0, 0, DAOS_METRICS_DIST_RP2, 0);
	print_message("\tpunch dkey:%s, akey:'akey1', rx_nr:%d\n",
			dkeys[0], num_rec_exts);
	punch_rec_with_rxnr(dkeys[0], "akey1", /*idx*/0, num_rec_exts,
			    DAOS_TX_NONE, &req);
	acct_obj_update(1, 0, 0, DAOS_METRICS_DIST_RP2, 0);

	/**
	 * Punch akeys (along with 50% of records) from object.
	 */
	print_message("Punch all akeys\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		punch_akey(dkeys[i], "akey", DAOS_TX_NONE, &req);
	punch_akey(dkeys[0], "akey0", DAOS_TX_NONE, &req);
	punch_akey(dkeys[0], "akey1", DAOS_TX_NONE, &req);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_akey_punch_cnt.mc_success += PUNCH_NUM_KEYS + 2;

	/**
	 * Punch 50% of dkeys (along with all akeys) from object.
	 */
	print_message("Punch all dkeys\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		punch_dkey(dkeys[i], DAOS_TX_NONE, &req);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_dkey_punch_cnt.mc_success += PUNCH_NUM_KEYS;

	/**
	 * Object punch (punch all keys associated with object).
	 */
	print_message("Punch entire object\n");
	punch_obj(DAOS_TX_NONE, &req);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_obj_punch_cnt.mc_success += 1;
	D_FREE(buf);
	D_FREE(data_buf);
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		D_FREE(dkeys[i]);

	test_metrics_compare();

	ioreq_fini(&req);
}

#define MANYREC_NUMRECS	5
/**
 * Basic test for dkey/akey punch and full object punch.
 */
static void
punch_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	punch_simple_internal(state, oid);

}

/**
 * Test update/fetch with data verification of multiple records of varying size
 * and IOD type. Size is either small I/O to SCM or larger (>=4k) I/O to NVMe,
 * and IOD type is either array or single value.
 */
static void
io_manyrec_internal(void **state, daos_obj_id_t oid, unsigned int size,
		    daos_iod_type_t iod_type, const char dkey[],
		    const char akey[])
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*akeys[MANYREC_NUMRECS];
	char		*rec[MANYREC_NUMRECS];
	daos_size_t	rec_size[MANYREC_NUMRECS];
	int		rx_nr[MANYREC_NUMRECS];
	daos_off_t	offset[MANYREC_NUMRECS];
	char		*val[MANYREC_NUMRECS];
	daos_size_t	val_size[MANYREC_NUMRECS];
	int		i;
	daos_size_t	tsize = 0;

	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	test_metrics_snapshot();
	for (i = 0; i < MANYREC_NUMRECS; i++) {
		akeys[i] = calloc(30, 1);
		assert_non_null(akeys[i]);
		snprintf(akeys[i], 30, "%s%d", akey, i);
		D_ALLOC(rec[i], size);
		assert_non_null(rec[i]);
		dts_buf_render(rec[i], size);
		rec_size[i] = size;
		rx_nr[i] = 1;
		offset[i] = i * size;
		val[i] = calloc(size, 1);
		assert_non_null(val[i]);
		val_size[i] = size;
		tsize += size;
	}

	/** Insert */
	insert(dkey, MANYREC_NUMRECS, (const char **)akeys,
	       rec_size, rx_nr, offset, (void **)rec, DAOS_TX_NONE, &req, 0);
	acct_obj_update(1, tsize, 0, DAOS_METRICS_DIST_RP2, 0);

	/** Lookup */
	lookup(dkey, MANYREC_NUMRECS, (const char **)akeys, offset, rec_size,
	       (void **)val, val_size, DAOS_TX_NONE, &req, false);
	acct_obj_fetch(1, tsize, DAOS_METRICS_DIST_RP2);

	/** Verify data consistency */
	for (i = 0; i < MANYREC_NUMRECS; i++) {
		print_message("\tsize = %lu\n", req.iod[i].iod_size);
		assert_int_equal(req.iod[i].iod_size, rec_size[i]);
		assert_memory_equal(val[i], rec[i], rec_size[i]);
		D_FREE(val[i]);
		D_FREE(akeys[i]);
		D_FREE(rec[i]);
	}
	test_metrics_compare();
	ioreq_fini(&req);
}

/**
 * Very basic update/fetch with data verification of multiple records, with
 * varying record size and IOD type.
 */
static void
io_manyrec(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	print_message("Insert(e=0)/lookup(e=0)/verify complex kv records:\n");

	print_message("DAOS_IOD_ARRAY:SCM\n");
	io_manyrec_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
			    "io_manyrec_scm_array dkey",
			    "io_manyrec_scm_array akey");

	print_message("DAOS_IOD_ARRAY:NVME\n");
	io_manyrec_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
			    "io_manyrec_nvme_array dkey",
			    "io_manyrec_array akey");

	print_message("DAOS_IOD_SINGLE:SCM\n");
	io_manyrec_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
			    "io_manyrec_scm_single dkey",
			    "io_manyrec_scm_single akey");

	print_message("DAOS_IOD_SINGLE:NVME\n");
	io_manyrec_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
			    "io_manyrec_nvme_single dkey",
			    "io_manyrec_nvme_single akey");

}

/** very basic key query test */
static void
io_obj_key_query(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_iod_t	iod = {0};
	d_sg_list_t	sgl = {0};
	uint32_t	update_var = 0xdeadbeef;
	d_iov_t		val_iov;
	d_iov_t		dkey;
	d_iov_t		akey;
	uint64_t	dkey_val, akey_val;
	daos_recx_t	recx;
	uint32_t	flags;
	daos_handle_t	th;
	int		rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, OC_S1,
				DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_UINT64,
				0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey, akey */
	dkey_val = 5;
	akey_val = 10;
	d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
	d_iov_set(&akey, &akey_val, sizeof(uint64_t));

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;
	iod.iod_size = sizeof(update_var);

	d_iov_set(&val_iov, &update_var, sizeof(update_var));
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	recx.rx_idx = 5;
	recx.rx_nr = 1;

	test_metrics_snapshot();
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	acct_obj_update(1, sizeof(update_var), 0, DAOS_METRICS_DIST_NORP, 0);

	dkey_val = 10;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	acct_obj_update(1, sizeof(update_var), 0, DAOS_METRICS_DIST_NORP, 0);

	recx.rx_idx = 50;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	acct_obj_update(1, sizeof(update_var), 0, DAOS_METRICS_DIST_NORP, 0);

	/*
	 * Not essential to this test, opening a TX helps us exercise
	 * dc_tx_get_epoch through the daos_obj_query_key fanout.
	 */
	rc = daos_tx_open(arg->coh, &th, 0, NULL);
	assert_rc_equal(rc, 0);

	flags = 0;
	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, th, flags, &dkey, &akey, &recx, NULL);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_querykey_cnt.mc_success += 1;
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 10);
	assert_int_equal(*(uint64_t *)akey.iov_buf, 10);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	rc = daos_tx_close(th, NULL);
	assert_rc_equal(rc, 0);

	test_metrics_compare();

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
io_obj_sync(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	oid = daos_test_oid_gen(arg->coh, OC_S1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	test_metrics_snapshot();

	insert_single("dkey1", "akey1", 0, "data",
	       strlen("data") + 1, DAOS_TX_NONE, &req);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);
	cal_obj_cntrs->u.arc_obj_cntrs.orc_sync_cnt.mc_success += 1;
	/**
	 * daos_obj_verify() does more rpc calls than just obj sync.
	 * Hence just check whether the obj sync call is made or not.
	 */
	rc = daos_metrics_get_cntrs(DAOS_METRICS_OBJ_RPC_CNTR, act_obj_cntrs);
	assert_rc_equal(rc, 0);
	assert_int_equal(cal_obj_cntrs->u.arc_obj_cntrs.orc_sync_cnt.mc_success,
	    act_obj_cntrs->u.arc_obj_cntrs.orc_sync_cnt.mc_success);
	ioreq_fini(&req);
}

static void *
io_thrd(void *state)
{
	test_arg_t	*arg = state;
	daos_obj_id_t	 oid;

	oid = daos_test_oid_gen(arg->coh, mdts_obj_class, 0, 0, arg->myrank);
	pthread_barrier_wait(&bar);
	/** Test first for SCM, then on NVMe with record size > 4k */
	print_message("DAOS_IOD_ARRAY:SCM\n");
	mio_simple_internal(&state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
			   "io_simple_scm_array dkey",
			   "io_simple_scm_array akey");
	print_message("DAOS_IOD_ARRAY:NVMe\n");
	mio_simple_internal(&state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
			   "io_simple_nvme_array dkey",
			   "io_simple_nvme_array akey");
	print_message("DAOS_IOD_SINGLE:SCM\n");
	mio_simple_internal(&state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
			   "io_simple_scm_single dkey",
			   "io_simple_scm_single akey");
	print_message("DAOS_IOD_SINGLE:NVMe\n");
	mio_simple_internal(&state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
			   "io_simple_nvme_single dkey",
			   "io_simple_nvme_single akey");
	print_message("Comparing Metrics values\n");
	pthread_barrier_wait(&bar);
	pthread_barrier_wait(&bar);
	pthread_exit(NULL);
}

#define NUM_THRDS	5
static void
io_obj_mt(void **state)
{
	test_arg_t		*arg = *state;
	int			 i, rc;
	pthread_t		 tid[NUM_THRDS];
	daos_metrics_ucntrs_t    pool_cntrs;
	daos_metrics_ucntrs_t    cont_cntrs;
	daos_metrics_ucntrs_t    obj_cntrs;
	daos_metrics_ustats_t    obj_up_stat;
	daos_metrics_ustats_t    obj_fh_stat;
	daos_metrics_udists_t    obj_dist_iosz;
	daos_metrics_udists_t    obj_dist_uprp;
	daos_metrics_udists_t    obj_dist_upec;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	test_metrics_snapshot();
	pthread_barrier_init(&bar, NULL, NUM_THRDS+1);
	print_message("Creating threads\n");
	for (i = 0; i < NUM_THRDS ; i++) {
		rc = pthread_create(&tid[i], NULL, io_thrd, (void *)*state);
		assert_rc_equal(rc, 0);
		/** Add the accounting information */
		mio_simple_internal_acct(IO_SIZE_SCM);
		mio_simple_internal_acct(IO_SIZE_NVME);
		mio_simple_internal_acct(IO_SIZE_SCM);
		mio_simple_internal_acct(IO_SIZE_NVME);
	}
	pthread_barrier_wait(&bar);
	pthread_barrier_wait(&bar);
	print_message("Snapshot the metrics data while threads are active\n");
	memcpy(&pool_cntrs, cal_pool_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(&cont_cntrs, cal_cont_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(&obj_cntrs, cal_obj_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(&obj_up_stat, cal_obj_up_stat, sizeof(daos_metrics_ustats_t));
	memcpy(&obj_fh_stat, cal_obj_fh_stat, sizeof(daos_metrics_ustats_t));
	memcpy(&obj_dist_iosz, cal_obj_dist_iosz, sizeof(daos_metrics_udists_t));
	memcpy(&obj_dist_uprp, cal_obj_dist_uprp, sizeof(daos_metrics_udists_t));
	memcpy(&obj_dist_upec, cal_obj_dist_upec, sizeof(daos_metrics_udists_t));
	test_metrics_snapshot();
	pthread_barrier_wait(&bar);
	print_message("Waiting for threads to exit\n");
	for (i = 0; i < NUM_THRDS ; i++) {
		rc = pthread_join(tid[i], NULL);
		assert_rc_equal(rc, 0);
	}
	print_message("Comparing the metrics\n");
	/** Check whether metrics data is preserved across thread exit */
	test_metrics_compare();
	/** Check whether the metrics data matches the calculated data */
	memcpy(cal_pool_cntrs, &pool_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(cal_cont_cntrs, &cont_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(cal_obj_cntrs, &obj_cntrs, sizeof(daos_metrics_ucntrs_t));
	memcpy(cal_obj_up_stat, &obj_up_stat, sizeof(daos_metrics_ustats_t));
	memcpy(cal_obj_fh_stat, &obj_fh_stat, sizeof(daos_metrics_ustats_t));
	memcpy(cal_obj_dist_iosz, &obj_dist_iosz, sizeof(daos_metrics_udists_t));
	memcpy(cal_obj_dist_uprp, &obj_dist_uprp, sizeof(daos_metrics_udists_t));
	memcpy(cal_obj_dist_upec, &obj_dist_upec, sizeof(daos_metrics_udists_t));
	test_metrics_compare();
}

static void
io_obj_rp(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	int		 i;
	char		*fetch_buf;
	char		*update_buf;
	char		*akey = "akey";
	char		*dkey = "dkey";
	daos_size_t	 size;
	daos_obj_id_t	 oid;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	for (i = 0; i < N_RP; i++) {
		test_metrics_snapshot();
		if (prot_rp[i].num_nodes > total_nodes)
			break;
		print_message("Testing io (single value single target) with RP nodes set to %d\n",
				prot_rp[i].num_nodes);
		size = IO_SIZE_NVME + random()%IO_SIZE_NVME;
		oid = daos_test_oid_gen(arg->coh, prot_rp[i].oclass, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

		D_ALLOC(fetch_buf, size);
		assert_non_null(fetch_buf);
		D_ALLOC(update_buf, size);
		assert_non_null(update_buf);
		dts_buf_render(update_buf, size);

		/** Insert */
		insert_single(dkey, akey, 0, update_buf, size, DAOS_TX_NONE, &req);
		acct_obj_update(1, size, 0, prot_rp[i].mclass, 0);

		/** Lookup */
		memset(fetch_buf, 0, size);
		lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE, &req);
		acct_obj_fetch(1, size, prot_rp[i].mclass);

		/** Verify data consistency */
		if (!daos_obj_is_echo(oid)) {
			assert_int_equal(req.iod[0].iod_size, size);
			assert_memory_equal(update_buf, fetch_buf, size);
		}

		D_FREE(update_buf);
		D_FREE(fetch_buf);
		ioreq_fini(&req);
		test_metrics_compare();
	}
}

static void
io_obj_ec_single(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	int		 i;
	char		*fetch_buf;
	char		*update_buf;
	char		*akey = "akey";
	char		*dkey = "dkey";
	daos_size_t	 size;
	daos_obj_id_t	 oid;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	for (i = 0; i < N_EC; i++) {
		test_metrics_snapshot();
		if (prot_ec[i].num_nodes > total_nodes)
			break;
		print_message("Testing io (single value) with EC nodes set to %d\n",
				prot_ec[i].num_nodes);
		/** Testing small size */
		size = 32;
		oid = daos_test_oid_gen(arg->coh, prot_ec[i].oclass, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

		D_ALLOC(fetch_buf, size);
		assert_non_null(fetch_buf);
		D_ALLOC(update_buf, size);
		assert_non_null(update_buf);
		dts_buf_render(update_buf, size);

		/** Insert */
		insert_single(dkey, akey, 0, update_buf, size, DAOS_TX_NONE, &req);
		acct_obj_update(1, size, 1, prot_ec[i].mclass, 0);

		/** Lookup */
		memset(fetch_buf, 0, size);
		lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE, &req);
		acct_obj_fetch(1, size, prot_ec[i].mclass);

		/** Verify data consistency */
		if (!daos_obj_is_echo(oid)) {
			assert_int_equal(req.iod[0].iod_size, size);
			assert_memory_equal(update_buf, fetch_buf, size);
		}

		D_FREE(update_buf);
		D_FREE(fetch_buf);
		ioreq_fini(&req);
		test_metrics_compare();
	}

}

static void
io_obj_ec_array(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	int		 i;
	char		*fetch_buf;
	char		*update_buf;
	char		*akey = "akey";
	char		*dkey = "dkey";
	daos_size_t	 size;
	daos_obj_id_t	 oid;
	daos_prop_t	*props;
	daos_recx_t	 recxs;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	if (metrics_disabled)
		skip();
	if (arg->myrank != 0)
		return;

	/* Set the container property for ec cell to 4K */
	props = daos_prop_alloc(1);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	props->dpp_entries[0].dpe_val = (4 << 10);

	/** container uuid */
        uuid_generate(uuid);
	rc = daos_cont_create(arg->pool.poh, uuid, props, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh, &info, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < N_EC; i++) {
		test_metrics_snapshot();
		if (prot_ec[i].num_nodes > total_nodes)
			break;
		print_message("Testing array partial stripe update with EC set to %d + %d \n",
				prot_ec[i].num_nodes - prot_ec[i].parity_info,
				prot_ec[i].parity_info);
		/** Testing Partial stripe */
		size = (4 << 10);
		recxs.rx_idx = 0;
		recxs.rx_nr = size;
		oid = daos_test_oid_gen(coh, prot_ec[i].oclass, 0, 0, arg->myrank);
		ioreq_init(&req, coh, oid, DAOS_IOD_ARRAY, arg);

		D_ALLOC(fetch_buf, size);
		assert_non_null(fetch_buf);
		D_ALLOC(update_buf, size);
		assert_non_null(update_buf);
		dts_buf_render(update_buf, size);

		/** Insert */
		insert_single_with_rxnr(dkey, akey, 0, update_buf, 1, size, DAOS_TX_NONE, &req);
		acct_obj_update(1, size * (1 + prot_ec[i].parity_info), 2, prot_ec[i].mclass, 1);

		/** Lookup */
		memset(fetch_buf, 0, size);
		lookup_recxs(dkey, akey, 1, DAOS_TX_NONE, &recxs, 1, fetch_buf, size, &req);
		acct_obj_fetch(1, size, prot_ec[i].mclass);

		/** Verify data consistency */
		if (!daos_obj_is_echo(oid)) {
			/*assert_int_equal(req.iod[0].iod_size, size); */
			assert_memory_equal(update_buf, fetch_buf, size);
		}

		D_FREE(update_buf);
		D_FREE(fetch_buf);
		ioreq_fini(&req);
		test_metrics_compare();
	}

	for (i = 0; i < N_EC; i++) {
		test_metrics_snapshot();
		if (prot_ec[i].num_nodes > total_nodes)
			break;
		print_message("Testing array full stripe update with EC set to %d + %d \n",
				prot_ec[i].num_nodes - prot_ec[i].parity_info,
				prot_ec[i].parity_info);
		/** Testing Full stripe */
		size = (4ul << 10) * (prot_ec[i].num_nodes - prot_ec[i].parity_info);
		recxs.rx_idx = 0;
		recxs.rx_nr = size;
		oid = daos_test_oid_gen(coh, prot_ec[i].oclass, 0, 0, arg->myrank);
		ioreq_init(&req, coh, oid, DAOS_IOD_ARRAY, arg);

		D_ALLOC(fetch_buf, size);
		assert_non_null(fetch_buf);
		D_ALLOC(update_buf, size);
		assert_non_null(update_buf);
		dts_buf_render(update_buf, size);

		/** Insert */
		insert_single_with_rxnr(dkey, akey, 0, update_buf, 1, size, DAOS_TX_NONE, &req);
		acct_obj_update(1, (4ul << 10) * prot_ec[i].num_nodes, 2, prot_ec[i].mclass, 0);

		/** Lookup */
		memset(fetch_buf, 0, size);
		lookup_recxs(dkey, akey, 1, DAOS_TX_NONE, &recxs, 1, fetch_buf, size, &req);
		/** Lookup fetches from all data nodes */
		acct_obj_fetch((prot_ec[i].num_nodes - prot_ec[i].parity_info), (4ul << 10),
				prot_ec[i].mclass);

		/** Verify data consistency */
		if (!daos_obj_is_echo(oid)) {
			/*assert_int_equal(req.iod[0].iod_size, size); */
			assert_memory_equal(update_buf, fetch_buf, size);
		}

		D_FREE(update_buf);
		D_FREE(fetch_buf);
		ioreq_fini(&req);
		test_metrics_compare();
	}
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, uuid, 1, NULL);
	assert_rc_equal(rc, 0);

}

static const struct CMUnitTest cm_tests[] = {
	{ "M_POOL1: connect/disconnect to pool (async)",
	  pool_connect, async_enable, test_case_teardown},
	{ "M_POOL2: exclusive connection",
	  pool_connect_exclusively, NULL, test_case_teardown},
	{ "M_POOL3: set/get/list user-defined pool attributes (sync)",
	  pool_attribute, NULL, test_case_teardown},
	{ "M_POOL4: pool query/list containers",
	  pool_query_list, NULL, test_case_teardown},
	{ "M_POOL5: pool connect access based on ACL",
	  pool_connect_access, NULL, test_case_teardown},
	{ "M_CONT1: create/open/close/destroy container (async)",
	  co_create, async_enable, test_case_teardown},
	{ "M_CONT2: set/get/list user-defined container attributes (sync)",
	  co_attribute, async_disable, test_case_teardown},
	{ "M_CONT3: create container with properties and query",
	  co_properties, NULL, test_case_teardown},
	{ "M_CONT4: container destroy access denied",
	  co_destroy_access_denied, NULL, test_case_teardown},
	{ "M_CONT5: container open access by ACL",
	  co_open_access, NULL, test_case_teardown},
	{ "M_CONT6: container query access by ACL",
	  co_query_access, NULL, test_case_teardown},
	{ "M_CONT7: container get-acl access by ACL",
	  co_get_acl_access, NULL, test_case_teardown},
	{ "M_CONT8: container overwrite/update/delete ACL access by ACL",
	  co_modify_acl_access, NULL, test_case_teardown},
	{ "M_CONT9: container snapshot",
	  co_snapshot, NULL, test_case_teardown},
	{ "M_IO1: simple update/fetch/verify",
	  io_simple, async_disable, test_case_teardown},
	{ "M_IO2: i/o with variable rec size(async)",
	  io_var_rec_size, async_enable, test_case_teardown},
	{ "M_IO3: i/o with variable index",
	  io_var_idx_offset, async_enable, test_case_teardown},
	{ "M_IO4: simple enumerate",
	  enumerate_simple, async_disable, test_case_teardown},
	{ "M_IO5: simple punch",
	  punch_simple, async_disable, test_case_teardown},
	{ "M_IO6: multiple record update/fetch/verify",
	  io_manyrec, async_disable, test_case_teardown},
	{ "M_IO7: basic object key query testing",
	  io_obj_key_query, async_disable, test_case_teardown},
	{ "M_IO8: testing object sync ",
	  io_obj_sync, async_disable, test_case_teardown},
	{ "M_IO9: testing io multithreaded ",
	  io_obj_mt, async_disable, test_case_teardown},
	{ "M_IO10: testing io stats with rp",
	  io_obj_rp, async_disable, test_case_teardown},
	{ "M_IO11: testing io stats single obj with ec",
	  io_obj_ec_single, async_disable, test_case_teardown},
	{ "M_IO12: testing io stats array obj with ec",
	  io_obj_ec_array, async_disable, test_case_teardown}
};

static int
setup_internal(void **state)
{
	test_arg_t	*arg;

	arg = *state;

	if (arg->pool.pool_info.pi_nnodes < 2)
		mdts_obj_class = OC_S1;
	/* REVISIT: Right now OC_S1 and OC_RP_2G1 tested.
	 * else if (arg->obj_class != OC_UNKNOWN)
	 *   mdts_obj_class = arg->obj_class;
	 */
	total_nodes = arg->pool.pool_info.pi_nnodes;

	return 0;
}

static int
setup(void **state)
{
	int	rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);
	if (rc != 0)
		return rc;

	return setup_internal(state);
}

int
run_daos_client_metrics_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;
	char oclass[16] = {0};
	char buf[32];

	MPI_Barrier(MPI_COMM_WORLD);
	test_metrics_init();
	daos_metrics_reset();
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(cm_tests);
		sub_tests = NULL;
	}

	if (dt_obj_class != OC_UNKNOWN) {
		oclass[0] = '_';
		daos_oclass_id2name(dt_obj_class, &oclass[1]);
	}
	snprintf(buf, sizeof(buf), "DAOS_IO%s", oclass);
	buf[sizeof(buf) - 1] = 0;

	rc = run_daos_sub_tests(buf, cm_tests,
				ARRAY_SIZE(cm_tests), sub_tests, sub_tests_size,
				setup, test_teardown);

	test_metrics_fini();
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
