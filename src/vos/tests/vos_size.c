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
#include <errno.h>
#include <getopt.h>
#include <daos/debug.h>
#include <daos_srv/vos.h>

static int alloc_overhead = 16;

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
	print_dynamic(fp, #name, &name);

#define PRINT_RECORD(name, type, feats)					\
	print_record(fp, #name, &name);

static void
print_dynamic(FILE *fp, const char *name, const struct daos_tree_overhead *ovhd)
{
	int	i;

	if (ovhd->to_dyn_count == 0)
		return;

	for (i = 0; i < ovhd->to_dyn_count; i++) {
		fprintf(fp, "%s_%d_key: &%s_%d\n", name,
			ovhd->to_dyn_overhead[i].no_order, name,
			ovhd->to_dyn_overhead[i].no_order);
		fprintf(fp, "  order: %d\n", ovhd->to_dyn_overhead[i].no_order);
		fprintf(fp, "  size: %d\n", ovhd->to_dyn_overhead[i].no_size);
	}
}

static void
print_record(FILE *fp, const char *name, const struct daos_tree_overhead *ovhd)
{
	int	i;
	int	count = 0;

	fprintf(fp, "  %s:\n", name);
	fprintf(fp, "    order: %d\n", ovhd->to_leaf_overhead.no_order);
	fprintf(fp, "    leaf_node_size: %d\n", ovhd->to_leaf_overhead.no_size);
	fprintf(fp, "    int_node_size: %d\n", ovhd->to_int_node_size);
	fprintf(fp, "    record_msize: %d\n", ovhd->to_record_msize);
	fprintf(fp, "    node_rec_msize: %d\n", ovhd->to_node_rec_msize);
	fprintf(fp, "    num_dynamic: %d\n", ovhd->to_dyn_count);
	if (ovhd->to_dyn_count == 0)
		return;

	fprintf(fp, "    dynamic: [\n      ");
	for (i = 0; i < ovhd->to_dyn_count; i++) {
		count += fprintf(fp, "*%s_%d", name,
				 ovhd->to_dyn_overhead[i].no_order);
		if (i == (ovhd->to_dyn_count - 1))
			continue;
		if (count > 40) {
			count = 0;
			fprintf(fp, ",\n      ");
		} else {
			count += fprintf(fp, ", ");
		}
	}
	fprintf(fp, "\n    ]\n");
}


char *
alloc_fname(const char *requested)
{
	char *suffix;
	char *fname;

	suffix = strstr(requested, ".yaml");

	if (!suffix || strcmp(suffix, ".yaml") != 0)
		D_ASPRINTF(fname, "%s.yaml", requested);
	else
		D_STRNDUP(fname, requested, PATH_MAX);

	if (fname == NULL)
		printf("Could not allocate memory to save %s\n", requested);

	return fname;
}

void
free_fname(char *fname)
{
	D_FREE(fname);
}

FILE *
open_file(const char *fname)
{
	FILE		*fp;
	const char	*to_open = fname;

	if (fname == NULL)
		to_open = "vos_size.yaml";

	fp = fopen(to_open, "w");
	if (fp == NULL)
		printf("Could not open %s: %s\n", to_open, strerror(errno));

	return fp;
}

void
print_usage(const char *name)
{
	printf("Usage: %s [OPTIONS]\n\
	OPTIONS:\n\
		--alloc_overhead, -a <bytes>	Overhead bytes per alloc (16)\n\
		--fname, -f <filename>		Output file (vos_size.yaml)\n\
		-h				Print this help message\n",
		name);
}

int
main(int argc, char **argv)
{
	FILE				*fp;
	char				*fname = NULL;
	static struct option		 long_options[] = {
		{"fname",		required_argument, 0, 'f'},
		{"help",		no_argument, 0, 'h'},
		{"alloc_overhead",	required_argument, 0, 'a'},
	};
	FOREACH_TYPE(DECLARE_TYPE)
	int				 rc;
	int				 index = 0;
	int				 opt = 0;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		printf("Error initializing debug system\n");
		return rc;
	}

	rc = vos_init();
	if (rc) {
		printf("Error initializing VOS instance\n");
		goto exit_0;
	}

	while ((opt = getopt_long(argc, argv, "a:f:h",
				  long_options, &index)) != -1) {
		switch (opt) {
		case 'a':
			alloc_overhead = atoi(optarg);
			break;
		case 'f':
			fname = alloc_fname(optarg);
			if (fname == NULL)
				goto exit_1;
			break;
		case 'h':
			print_usage(argv[0]);
			goto exit_1;
		default:
			printf("Unknown option\n");
			print_usage(argv[0]);
			goto exit_1;
		}
	}

	FOREACH_TYPE(CHECK_CALL)

	fp = open_file(fname);
	free_fname(fname);

	if (fp == NULL)
		goto exit_1;

	fprintf(fp, "---\n# VOS tree overheads\n"
		"root: %d\nscm_cutoff: %d\n", vos_pool_get_msize(),
		vos_pool_get_scm_cutoff());

	FOREACH_TYPE(PRINT_DYNAMIC)
	fprintf(fp, "trees:\n");
	FOREACH_TYPE(PRINT_RECORD)

	fclose(fp);
exit_1:
	vos_fini();
exit_0:
	daos_debug_fini();
	return 0;
}


