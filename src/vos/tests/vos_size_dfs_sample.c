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
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <daos/debug.h>
#include <daos.h>
#include <daos_fs.h>

#define DEFAULT_DFS_EXAMPLE_NAME "vos_dfs_sample.yaml"


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
		to_open = DEFAULT_DFS_EXAMPLE_NAME;

	fp = fopen(to_open, "w");
	if (fp == NULL)
		printf("Could not open %s: %s\n", to_open, strerror(errno));

	return fp;
}

void
print_usage(const char *name)
{
	printf("Usage: %s [OPTIONS]\n"
		"OPTIONS:\n"
		"--fname, -f <filename>		Output file (%s)\n"
		"-h				Print this help message\n",
		name, DEFAULT_DFS_EXAMPLE_NAME);
}

void
to_lower(char *buf, char *str, int size)
{
	int i;

	if (size >= DFS_MAX_PATH) {
		buf[0] = '\0';
		return;
	}

	for (i = 0; i < size; i++) {
		buf[i] = tolower(str[i]);
	}

	buf[i] = '\0';
}

void
print_list(FILE *fp, const char *key, char values[][DFS_MAX_PATH],
	int values_count)
{
	int i = 0;

	fprintf(fp, "  %s: [ ", key);

	while (i < values_count) {
		fprintf(fp, "%s", values[i]);

		if (i == (values_count - 1)) {
			break;
		}

		fprintf(fp, ", ");
		i++;
	}

	fprintf(fp, " ]\n");
}

void
print_dkey(FILE *fp, daos_key_t	*dkey, daos_iod_t *iods, int akey_count)
{
	int i = 0;
	char buf[DFS_MAX_PATH];
	char values[akey_count][DFS_MAX_PATH];

	/* print all the A-Key values */
	for (i = 0; i < akey_count; i++) {
		to_lower(buf, iods[i].iod_name.iov_buf,
			iods[i].iod_name.iov_len);

		fprintf(fp, "%s: &%s\n", buf, buf);
		sprintf(values[i], "*%s", buf);

		fprintf(fp, "  size: %zu\n", iods[i].iod_name.iov_len);
		fprintf(fp, "  overhead: meta\n");

		if (iods[i].iod_type == DAOS_IOD_SINGLE) {
			fprintf(fp, "  value_type: single_value\n");
		}
		if (iods[i].iod_type == DAOS_IOD_ARRAY) {
			fprintf(fp, "  value_type: array\n");
		}

		fprintf(fp, "  values: [{\"count\": %u, \"size\": %u}]\n",
			iods[i].iod_nr, (unsigned int)iods[i].iod_size);
		fprintf(fp, "\n");
	}

	/* print the D-Key value */
	to_lower(buf, dkey->iov_buf, dkey->iov_len);

	fprintf(fp, "%s: &%s\n", buf, buf);
	fprintf(fp, "  size: %zu\n", dkey->iov_buf_len);
	fprintf(fp, "  overhead: meta\n");
	print_list(fp, "akeys", values, akey_count);
	fprintf(fp, "\n");
}

/**
* TODO: add option to specify the number of files and its size
*/
void
print_dfs_example_remainder(FILE *fp, int dfs_inode_size)
{
	fprintf(fp,
	"dfs_inode: &dfs_inode\n"
	"  type: integer\n"
	"  overhead: meta\n"
	"  value_type: array\n"
	"  values: [{\"count\": 1, \"size\": %d}]\n"
	"\n"
	"# Assumes 16 bytes for file name\n"
	"dirent_key: &dirent\n"
	"  count: 1000000\n"
	"  size: 16\n"
	"  akeys: [*dfs_inode]\n"
	"\n"
	"dir_obj: &dir\n"
	"  dkeys: [*dirent]\n"
	"\n"
	"superblock: &sb\n"
	"  dkeys: [*dfs_sb_metadata]\n"
	"\n"
	"array_akey: &file_data\n"
	"  size: 1\n"
	"  overhead: meta\n"
	"  value_type: array\n"
	"  values: [{\"count\": 1, \"size\": 4096}]\n"
	"\n"
	"array_meta: &file_meta\n"
	"  size: 19\n"
	"  overhead: meta\n"
	"  value_type: single_value\n"
	"  values: [{\"size\": 24}]\n"
	"\n"
	"file_dkey_key0: &file_dkey0\n"
	"  count: 1\n"
	"  type: integer\n"
	"  akeys: [*file_data, *file_meta]\n"
	"\n"
	"file_dkey_key: &file_dkey\n"
	"  count: 1\n"
	"  type: integer\n"
	"  akeys: [*file_data]\n"
	"\n"
	"file_key: &file\n"
	"  count: 1000000\n"
	"  dkeys: [*file_dkey0, *file_dkey]\n"
	"\n"
	"posix_key: &posix\n"
	"  objects: [*sb, *file, *dir]\n"
	"\n"
	"containers: [*posix]\n", dfs_inode_size);
}

int
main(int argc, char **argv)
{
	FILE				*fp;
	char				*fname = NULL;
	daos_iod_t			*akey_sb;
	daos_key_t			dkey_sb;
	static struct option		 long_options[] = {
		{"fname",		required_argument, 0, 'f'},
		{"help",		no_argument, 0, 'h'},
	};

	int				 rc;
	int				 index = 0;
	int				 opt = 0;
	int					akey_count;
	int					dfs_inode_size;

	rc = daos_debug_init(NULL);
	if (rc) {
		printf("Error initializing debug system\n");
		return rc;
	}

	while ((opt = getopt_long(argc, argv, "a:f:h",
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

	rc = get_sb_layout(&dkey_sb, &akey_sb, &akey_count, &dfs_inode_size);
	if (rc) {
		goto exit_1;
	}

	fprintf(fp, "---\n"
		"# Sample conflig file DFS files and directories\n"
		"num_pools: 1000\n\n");

	print_dkey(fp, &dkey_sb, akey_sb, akey_count);
	print_dfs_example_remainder(fp, dfs_inode_size);

	fclose(fp);
	D_FREE(akey_sb);
exit_1:
	daos_debug_fini();
	return rc;
}
