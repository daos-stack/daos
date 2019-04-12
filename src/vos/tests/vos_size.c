/**
 * (C) Copyright 2019 Intel Corporation.
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
/* This generates a summary of struct sizes to be used by vos_estimate.py
 * to generate metadata overhead estimates
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <daos/debug.h>
#include <daos_srv/vos.h>

#define FOREACH_TYPE(ACTION)					\
	ACTION(container, VOS_TC_CONTAINER, 0)			\
	ACTION(object, VOS_TC_OBJECT, 0)			\
	ACTION(dkey, VOS_TC_DKEY, 0)				\
	ACTION(akey, VOS_TC_AKEY, 0)				\
	ACTION(integer_dkey, VOS_TC_DKEY, DAOS_OF_DKEY_UINT64)	\
	ACTION(integer_akey, VOS_TC_AKEY, DAOS_OF_AKEY_UINT64)	\
	ACTION(single_value, VOS_TC_SV, 0)			\
	ACTION(array, VOS_TC_ARRAY, 0)

#define DECLARE_TYPE(name, type, feats)	\
	struct daos_tree_overhead	name;

#define CHECK_CALL(name, type, feats)					\
	do {								\
		rc = vos_tree_get_overhead(16, type, 0, &name);		\
		if (rc != 0) {						\
			printf(#name " lookup failed: rc = %d\n", rc);	\
			goto exit_1;					\
		}							\
	} while (0);

#define TREE_FMT(name, type, feats)		\
"  " #name ":\n"				\
"    node_size: %d\n"				\
"    record_msize: %d\n"			\
"    single_size: %d\n"				\
"    order: %d\n"

#define TREE_PRINT(name, type, feats)			\
	name.to_node_size, name.to_record_msize,	\
	name.to_single_size, name.to_order,

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
		--fname, -f <filename>	Yaml file to create\n\
		-h			Print this help message\n", name);
}

int
main(int argc, char **argv)
{
	FILE				*fp;
	char				*fname = NULL;
	static struct option		 long_options[] = {
		{"fname",		required_argument, 0, 'f'},
		{"help",		no_argument, 0, 'h'},
	};
	FOREACH_TYPE(DECLARE_TYPE)
	int				 rc;
	int				 index = 0;
	int				 opt = 0;

	rc = daos_debug_init(NULL);
	if (rc) {
		printf("Error initializing debug system\n");
		return rc;
	}

	rc = vos_init();
	if (rc) {
		printf("Error initializing VOS instance\n");
		goto exit_0;
	}

	FOREACH_TYPE(CHECK_CALL)

	while ((opt = getopt_long(argc, argv, "f:h",
			  long_options, &index)) != -1) {
		switch (opt) {
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

	fp = open_file(fname);
	free_fname(fname);

	if (fp == NULL)
		goto exit_1;

	fprintf(fp, "---\n# VOS tree overheads\ntrees:\n"
		FOREACH_TYPE(TREE_FMT) "root: %d\nscm_cutoff: %d\n",
		FOREACH_TYPE(TREE_PRINT) vos_pool_get_msize(),
		vos_pool_get_scm_cutoff());

	fclose(fp);
exit_1:
	vos_fini();
exit_0:
	daos_debug_fini();
	return 0;
}


