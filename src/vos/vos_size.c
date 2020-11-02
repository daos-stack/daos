/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
				      ovhd->to_dyn_overhead[i].no_size);
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
			      ovhd->to_leaf_overhead.no_size);
	d_write_string_buffer(buf, "    int_node_size: %d\n",
			      ovhd->to_int_node_size);
	d_write_string_buffer(buf, "    record_msize: %d\n",
			      ovhd->to_record_msize);
	d_write_string_buffer(buf, "    node_rec_msize: %d\n",
			      ovhd->to_node_rec_msize);
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
get_vos_structure_sizes_yaml(int alloc_overhead, struct d_string_buffer_t *buf)
{
	FOREACH_TYPE(DECLARE_TYPE)
	int rc;

	/* clean string buffer */
	d_free_string(buf);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		goto exit_0;
	}

	rc = vos_init();
	if (rc) {
		goto exit_1;
	}

	FOREACH_TYPE(CHECK_CALL)

	d_write_string_buffer(buf, "---\n# VOS tree overheads\n"
		"root: %d\nscm_cutoff: %d\n", vos_pool_get_msize(),
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
	vos_fini();
exit_1:
	daos_debug_fini();
exit_0:
	return rc;
}
