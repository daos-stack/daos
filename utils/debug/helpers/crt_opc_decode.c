/**
 * (C) Copyright 2017-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include <daos/common.h>
#include <daos/debug.h>

#include <crt_internal.h>

static void
usage(char *name, FILE *out)
{
	fprintf(out,
		"Usage:\n"
		"\t%s -h\n"
		"\t%s op_id\n",
		name, name);
}

int
main(int argc, char **argv)
{
	crt_opcode_t        opc_id;
	char               *module_name;
	char               *opc_name;
	int                 opt;
	const char         *opt_cfg        = "h";
	const struct option long_opt_cfg[] = {{"help", no_argument, NULL, 'h'},
					      {0, 0, 0, 0}};
	int                 rc;

	while ((opt = getopt_long(argc, argv, opt_cfg, long_opt_cfg, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0], stdout);
			exit(EXIT_SUCCESS);
			break;
		default:
			usage(argv[0], stderr);
			exit(EXIT_FAILURE);
			break;
		}
	}

	rc = daos_debug_init_ex("/dev/stdout", DLOG_INFO);
	D_ASSERT(rc == 0);

	opc_id = atoi(argv[1]);
	crt_opc_decode(opc_id, &module_name, &opc_name);
	printf("cart operation id: %#x (%" PRIu32 ")\n"
	       "module name:       %s\n"
	       "operation name:    %s\n",
	       opc_id, opc_id, module_name, opc_name);

	exit(EXIT_SUCCESS);
}
