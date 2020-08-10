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
#include <stdarg.h>
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
	ACTION(array, VOS_TC_ARRAY, 0)

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

struct str_buffer {
	unsigned int status;
	size_t str_size;
	size_t buf_size;
	char *str;
};

static int
wr_str(struct str_buffer *buf, const char *fmt, ...) {
	int n;
	int size = 64;
	char *new_buf;
	va_list ap;

	if (buf == NULL || buf->status != 0) {
		return -DER_NO_PERM;
	}

	if (buf->str == NULL) {
		D_ALLOC(buf->str, size);
		if (buf->str == NULL) {
			buf->status = -DER_NOMEM;
			return -DER_NOMEM;
		}
		buf->str_size = 0;
		buf->buf_size = size;
	}

	while (1) {
		va_start(ap, fmt);
		size = buf->buf_size - buf->str_size;
		n = vsnprintf(buf->str + buf->str_size, size, fmt, ap);
		va_end(ap);

		if (n < 0) {
			buf->status = -DER_TRUNC;
			return -DER_TRUNC;
		}

		if ((buf->str_size + n) < buf->buf_size) {
			buf->str_size += n;
			return n;
		}

		size = buf->buf_size * 2;
		D_REALLOC(new_buf, buf->str, size);
		if (new_buf == NULL) {
			buf->status = -DER_NOMEM;
			return -DER_NOMEM;
		}

		buf->str = new_buf;
		buf->buf_size = size;
	}
}

static void
print_dynamic(struct str_buffer *buf, const char *name,
	      const struct daos_tree_overhead *ovhd)
{
	int	i;

	if (ovhd->to_dyn_count == 0)
		return;

	for (i = 0; i < ovhd->to_dyn_count; i++) {
		wr_str(buf, "%s_%d_key: &%s_%d\n", name,
			ovhd->to_dyn_overhead[i].no_order, name,
			ovhd->to_dyn_overhead[i].no_order);
		wr_str(buf, "  order: %d\n", ovhd->to_dyn_overhead[i].no_order);
		wr_str(buf, "  size: %d\n", ovhd->to_dyn_overhead[i].no_size);
	}
}

static void
print_record(struct str_buffer *buf, const char *name,
	const struct daos_tree_overhead *ovhd)
{
	int	i;
	int	count = 0;

	wr_str(buf, "  %s:\n", name);
	wr_str(buf, "    order: %d\n", ovhd->to_leaf_overhead.no_order);
	wr_str(buf, "    leaf_node_size: %d\n", ovhd->to_leaf_overhead.no_size);
	wr_str(buf, "    int_node_size: %d\n", ovhd->to_int_node_size);
	wr_str(buf, "    record_msize: %d\n", ovhd->to_record_msize);
	wr_str(buf, "    node_rec_msize: %d\n", ovhd->to_node_rec_msize);
	wr_str(buf, "    num_dynamic: %d\n", ovhd->to_dyn_count);
	if (ovhd->to_dyn_count == 0)
		return;

	wr_str(buf, "    dynamic: [\n      ");
	for (i = 0; i < ovhd->to_dyn_count; i++) {
		count += wr_str(buf, "*%s_%d", name,
				 ovhd->to_dyn_overhead[i].no_order);
		if (i == (ovhd->to_dyn_count - 1))
			continue;
		if (count > 40) {
			count = 0;
			wr_str(buf, ",\n      ");
		} else {
			count += wr_str(buf, ", ");
		}
	}
	wr_str(buf, "\n    ]\n");
}

void
free_string(struct str_buffer *buf)
{
	if (buf->str != NULL) {
		D_FREE(buf->str);
		buf->status = 0;
		buf->str_size = 0;
		buf->buf_size = 0;
	}
}

int
get_vos_structure_sizes_yaml(int alloc_overhead, struct str_buffer *buf)
{
	FOREACH_TYPE(DECLARE_TYPE)
	int rc;

	/* clean string buffer */
	free_string(buf);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		goto exit_0;
	}

	rc = vos_init();
	if (rc) {
		goto exit_1;
	}

	FOREACH_TYPE(CHECK_CALL)

	wr_str(buf, "---\n# VOS tree overheads\n"
		"root: %d\nscm_cutoff: %d\n", vos_pool_get_msize(),
		vos_pool_get_scm_cutoff());

	FOREACH_TYPE(PRINT_DYNAMIC)
	wr_str(buf, "trees:\n");
	FOREACH_TYPE(PRINT_RECORD)

	if (buf->status != 0) {
		free_string(buf);
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
