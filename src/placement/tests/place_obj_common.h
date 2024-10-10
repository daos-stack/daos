/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#ifndef __PL_MAP_COMMON_H__
#define __PL_MAP_COMMON_H__

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

extern bool g_verbose;

#define skip_msg(msg) do { print_message(__FILE__":" STR(__LINE__) \
			" Skipping > "msg"\n"); skip(); } \
			while (0)
#define is_true assert_true
#define is_false assert_false

#define PLT_LAYOUT_VERSION	1
extern bool fail_domain_node;
void
print_layout(struct pl_obj_layout *layout);

int
plt_obj_place(daos_obj_id_t oid, uint32_t pda, struct pl_obj_layout **layout,
		struct pl_map *pl_map, bool print_layout);

void
plt_obj_layout_check(struct pl_obj_layout *layout, uint32_t pool_size,
		int num_allowed_failures);

void
plt_obj_rebuild_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *down_tgts, int num_down, int num_spares_left,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_drain_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *draining_tgts, int num_draining, int num_spares,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_add_layout_check(struct pl_obj_layout *layout,
			 struct pl_obj_layout *org_layout, uint32_t pool_size,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_reint_layout_check(struct pl_obj_layout *layout,
			   struct pl_obj_layout *org_layout, uint32_t pool_size,
			   int *reint_tgts, int num_reint, int num_spares,
			   uint32_t num_spares_returned,
			   uint32_t *spare_tgt_ranks, uint32_t *shard_ids);

void
plt_obj_rebuild_unique_check(uint32_t *shard_ids, uint32_t num_shards,
		uint32_t pool_size);

bool
plt_obj_layout_match(struct pl_obj_layout *lo_1, struct pl_obj_layout *lo_2);

void
plt_set_domain_status(uint32_t id, int status, uint32_t *ver,
		      struct pool_map *po_map, bool pl_debug_msg,
		      enum pool_comp_type level);

void
plt_set_tgt_status(uint32_t id, int status, uint32_t *ver,
		struct pool_map *po_map, bool pl_debug_msg);

void
plt_drain_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_fail_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_fail_tgt_out(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_reint_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_reint_tgt_up(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_spare_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *spare_tgt_ranks, bool pl_debug_msg,
		   uint32_t *shard_ids, uint32_t *spare_cnt, uint32_t *po_ver,
		   pl_map_type_t map_type, uint32_t spare_max_nr,
		   struct pool_map *po_map, struct pl_map *pl_map);

void
gen_pool_and_placement_map(int num_pds, int fdoms_per_pd, int nodes_per_domain,
			   int vos_per_target, pl_map_type_t pl_type, int fdom_lvl,
			   struct pool_map **po_map_out,
			   struct pl_map **pl_map_out);

void
gen_pool_and_placement_map_non_standard(int num_domains,
					int *domain_targets,
					pl_map_type_t pl_type,
					struct pool_map **po_map_out,
					struct pl_map **pl_map_out);

void
free_pool_and_placement_map(struct pool_map *po_map_in,
			    struct pl_map *pl_map_in);

void
plt_reint_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *reint_tgts, int reint_cnt,
		   uint32_t *spare_tgt_ranks, uint32_t *shard_ids,
		   uint32_t *spare_cnt, pl_map_type_t map_type,
		   uint32_t spare_max_nr, struct pool_map *po_map,
		   struct pl_map *pl_map, uint32_t *po_ver, bool pl_debug_msg);

int
get_object_classes(daos_oclass_id_t **oclass_id_pp);

int
extend_test_pool_map(struct pool_map *map, uint32_t nnodes,
		     uint32_t ndomains, uint32_t *domains, bool *updated_p,
		     uint32_t *map_version_p, uint32_t dss_tgt_nr);

bool
is_max_class_obj(daos_oclass_id_t cid);

int
placement_tests_run(bool verbose);
int
pda_tests_run(bool verbose);
int
pda_layout_run(bool verbose);
int
dist_tests_run(bool verbose, uint32_t num_obj, int obj_class);

static inline void
verbose_msg(char *msg, ...)
{
	if (g_verbose) {
		va_list vargs;

		va_start(vargs, msg);
		vprint_message(msg, vargs);
		va_end(vargs);
	}
}

static inline void
gen_maps(int num_pds, int fdoms_per_pd, int nodes_per_domain, int vos_per_target,
	 struct pool_map **po_map, struct pl_map **pl_map)
{
	*po_map = NULL;
	*pl_map = NULL;
	gen_pool_and_placement_map(num_pds, fdoms_per_pd, nodes_per_domain, vos_per_target,
				   PL_TYPE_JUMP_MAP, PO_COMP_TP_RANK, po_map, pl_map);
	assert_non_null(*po_map);
	assert_non_null(*pl_map);
}

static inline void
gen_maps_adv(int num_pds, int fdoms_per_pd, int nodes_per_domain, int vos_per_target, int fdom_lvl,
	     struct pool_map **po_map, struct pl_map **pl_map)
{
	*po_map = NULL;
	*pl_map = NULL;
	gen_pool_and_placement_map(num_pds, fdoms_per_pd, nodes_per_domain, vos_per_target,
				   PL_TYPE_JUMP_MAP, fdom_lvl, po_map, pl_map);
	assert_non_null(*po_map);
	assert_non_null(*pl_map);
}

static inline void
gen_oid(daos_obj_id_t *oid, uint64_t lo, uint64_t hi, daos_oclass_id_t cid)
{
	int rc;

	oid->lo = lo;
	/* make sure top 32 bits are unset (DAOS only) */
	oid->hi = hi & 0xFFFFFFFF;
	rc = daos_obj_set_oid_by_class(oid, 0, cid, 0);
	assert_rc_equal(rc, cid == OC_UNKNOWN ? -DER_INVAL : 0);
}

#define assert_placement_success_print(pl_map, cid, pda)			\
	do {									\
		daos_obj_id_t __oid;						\
		struct pl_obj_layout *__layout = NULL;				\
		gen_oid(&__oid, 1, UINT64_MAX, cid);				\
		assert_success(plt_obj_place(__oid, pda, &__layout, pl_map,	\
				true));						\
		pl_obj_layout_free(__layout);					\
	} while (0)

#define assert_placement_success(pl_map, cid, pda)				\
	do {									\
		daos_obj_id_t __oid;						\
		struct pl_obj_layout *__layout = NULL;				\
		gen_oid(&__oid, 1, UINT64_MAX, cid);				\
		assert_success(plt_obj_place(__oid, pda, &__layout, pl_map,	\
				false));					\
		pl_obj_layout_free(__layout);					\
	} while (0)

#define assert_invalid_param(pl_map, cid, pda)					\
	do {									\
		daos_obj_id_t __oid;						\
		struct pl_obj_layout *__layout = NULL;				\
		int rc;								\
		gen_oid(&__oid, 1, UINT64_MAX, cid);				\
		rc = plt_obj_place(__oid, pda, &__layout,			\
				   pl_map, false);				\
		assert_rc_equal(rc, -DER_INVAL);				\
	} while (0)

#endif /*   PL_MAP_COMMON_H   */
