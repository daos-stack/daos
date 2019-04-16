/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

#include <unistd.h>
#include "intercept.h"

void
print_usage(const char *prog)
{
	printf("Usage: %s <-l|-s> <file_to_generate>\n", prog);
	printf("\nOptions:\n");
	printf("\t-l <path>\tGenerate a linker script\n");
	printf("\t-s <path>\tGenerate script for symbol checking\n");
}

#define LINK_SCRIPT_GEN(type, name, params) \
	fprintf(fp, "--wrap=" #name "\n");

#define LINK_SCRIPT_GEN64(type, name, params) \
	fprintf(fp, "--wrap=" #name "64\n");

#define SYMBOL_GEN(type, name, params) \
	fprintf(fp, #name " ");

#define SYMBOL_GEN64(type, name, params) \
	fprintf(fp, #name "64 ");

#define SYMBOL_GEN_IOF(type, name, params) \
	fprintf(fp, "dfuse_" #name " ");

int
main(int argc, char **argv)
{
	FILE *fp;
	const char *path = NULL;
	bool linker_script = false;
	int opt;

	for (;;) {
		opt = getopt(argc, argv, "l:s:");
		if (opt == -1)
			break;
		switch (opt) {
		case 'l':
			linker_script = true;
			path = optarg;
			break;
		case 's':
			linker_script = false;
			path = optarg;
			break;
		default:
			printf("Unknown option %c\n", opt);
			print_usage(argv[0]);
			return -1;
		}
	}

	if (path == NULL) {
		printf("No option specified\n");
		print_usage(argv[0]);
		return -1;
	}

	fp = fopen(path, "w");
	if (fp == NULL) {
		printf("Could not open %s for writing\n", argv[1]);
		return -1;
	}

	if (linker_script) {
		FOREACH_INTERCEPT(LINK_SCRIPT_GEN)
		FOREACH_ALIASED_INTERCEPT(LINK_SCRIPT_GEN64)
	} else {
		fprintf(fp, "syms=\"");
		FOREACH_INTERCEPT(SYMBOL_GEN)
		FOREACH_ALIASED_INTERCEPT(SYMBOL_GEN64)
		FOREACH_INTERCEPT(SYMBOL_GEN_IOF)
		fprintf(fp, "\"\nweak=\"");
		FOREACH_INTERCEPT(SYMBOL_GEN)
		FOREACH_ALIASED_INTERCEPT(SYMBOL_GEN64)
		fprintf(fp, "\"\n");
	}

	fclose(fp);

	return 0;
}
