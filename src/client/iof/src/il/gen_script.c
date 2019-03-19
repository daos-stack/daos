/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include "intercept.h"

void print_usage(const char *prog)
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
	fprintf(fp, "iof_" #name " ");

int main(int argc, char **argv)
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
