/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/* This generates a summary of struct sizes to be used by vos_size.py
 * to generate metadata overhead estimates
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <daos/debug.h>
#include <daos_srv/vos.h>

#define FOREACH_TYPE(ACTION)						\
	ACTION(container, VOS_TC_CONTAINER, 0)				\
	ACTION(object, VOS_TC_OBJECT, 0)				\
	ACTION(dkey, VOS_TC_DKEY, 0)					\
	ACTION(akey, VOS_TC_AKEY, 0)					\
	ACTION(integer_dkey, VOS_TC_DKEY, BTR_FEAT_DIRECT_KEY)		\
	ACTION(integer_akey, VOS_TC_AKEY, BTR_FEAT_DIRECT_KEY)		\
	ACTION(single_value, VOS_TC_SV, 0)				\
	ACTION(array, VOS_TC_ARRAY, 0)					\
	ACTION(vea, VOS_TC_VEA, 0)

#define DECLARE_TYPE(name, type, feats)	\
	struct daos_tree_overhead	name;

#define CHECK_CALL(name, type, feats)					\
	do {								\
		rc = vos_tree_get_overhead(alloc_overhead, type, feats,	\
					   &name);			\
		if (rc != 0) {						\
			printf(#name " lookup failed: rc = "DF_RC"\n",	\
				DP_RC(rc));				\
			goto exit_1;					\
		}							\
	} while (0);

#define PRINT_DYNAMIC(name, type, feats)				\
	print_dynamic(buf, #name, &name);

#define PRINT_RECORD(name, type, feats)					\
	print_record(buf, #name, &name);

static void
print_dynamic(struct d_string_buffer_t *buf, const char *name,
	      const struct daos_tree_overhead *ovhd)
{
	int	i;

	if (ovhd->to_dyn_count == 0)
		return;

	for (i = 0; i < ovhd->to_dyn_count; i++) {
		d_write_string_buffer(buf, "%s_%d_key: &%s_%d\n", name,
				      ovhd->to_dyn_overhead[i].no_order, name,
			ovhd->to_dyn_overhead[i].no_order);
		d_write_string_buffer(buf, "  order: %d\n",
				      ovhd->to_dyn_overhead[i].no_order);
		d_write_string_buffer(buf, "  size: %d\n",
				      D_ALIGNUP(ovhd->to_dyn_overhead[i].no_size, 32));
	}
}

static void
print_record(struct d_string_buffer_t *buf, const char *name,
	const struct daos_tree_overhead *ovhd)
{
	int	i;
	int	count = 0;

	d_write_string_buffer(buf, "  %s:\n", name);
	d_write_string_buffer(buf, "    order: %d\n",
			      ovhd->to_leaf_overhead.no_order);
	d_write_string_buffer(buf, "    leaf_node_size: %d\n",
			      D_ALIGNUP(ovhd->to_leaf_overhead.no_size, 32));
	d_write_string_buffer(buf, "    int_node_size: %d\n",
			      D_ALIGNUP(ovhd->to_int_node_size, 32));
	d_write_string_buffer(buf, "    record_msize: %d\n", D_ALIGNUP(ovhd->to_record_msize, 32));
	d_write_string_buffer(buf, "    node_rec_msize: %d\n",
			      D_ALIGNUP(ovhd->to_node_rec_msize, 32));
	d_write_string_buffer(buf, "    num_dynamic: %d\n", ovhd->to_dyn_count);
	if (ovhd->to_dyn_count == 0)
		return;

	d_write_string_buffer(buf, "    dynamic: [\n      ");
	for (i = 0; i < ovhd->to_dyn_count; i++) {
		count += d_write_string_buffer(buf, "*%s_%d", name,
				 ovhd->to_dyn_overhead[i].no_order);
		if (i == (ovhd->to_dyn_count - 1))
			continue;
		if (count > 40) {
			count = 0;
			d_write_string_buffer(buf, ",\n      ");
		} else {
			count += d_write_string_buffer(buf, ", ");
		}
	}
	d_write_string_buffer(buf, "\n    ]\n");
}

static int
get_daos_csummers(struct d_string_buffer_t *buf)
{
	enum DAOS_HASH_TYPE	 type;
	struct daos_csummer	*csummer = NULL;
	struct hash_ft *ft;
	int	rc, csum_size;

	d_write_string_buffer(buf, "csummers:\n");

	for (type = HASH_TYPE_UNKNOWN + 1; type < HASH_TYPE_END; type++) {
		ft = daos_mhash_type2algo(type);
		rc = daos_csummer_init(&csummer, ft, 128, 0);

		if (rc != 0) {
			return rc;
		}

		csum_size = daos_csummer_get_csum_len(csummer);

		d_write_string_buffer(buf, "    %s: %d\n",
				      ft->cf_name, csum_size);
		daos_csummer_destroy(&csummer);
	}

	return DER_SUCCESS;
}

int
get_vos_structure_sizes_yaml(int alloc_overhead, struct d_string_buffer_t *buf,
			     const char *vos_path)
{
	FOREACH_TYPE(DECLARE_TYPE)
	int rc;

	/* clean string buffer */
	d_free_string(buf);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		goto exit_0;
	}
	rc = vos_self_init(vos_path, false, BIO_STANDALONE_TGT_ID);
	if (rc) {
		goto exit_1;
	}

	FOREACH_TYPE(CHECK_CALL)

	d_write_string_buffer(buf, "---\n# VOS tree overheads\n");
	d_write_string_buffer(buf, "root: %d\n", D_ALIGNUP(vos_pool_get_msize(), 32));
	d_write_string_buffer(buf, "container: %d\n", D_ALIGNUP(vos_container_get_msize(), 32));
	d_write_string_buffer(buf, "scm_cutoff: %d\n",
			      vos_pool_get_scm_cutoff());

	FOREACH_TYPE(PRINT_DYNAMIC)
	d_write_string_buffer(buf, "trees:\n");
	FOREACH_TYPE(PRINT_RECORD)

	rc = get_daos_csummers(buf);
	if (rc) {
		goto exit_2;
	}

	if (buf->status != 0) {
		d_free_string(buf);
		rc = buf->status;
		goto exit_2;
	}

exit_2:
	vos_self_fini();
exit_1:
	daos_debug_fini();
exit_0:
	return rc;
}
