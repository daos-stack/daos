/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos, to generate the epoch io test.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

static struct option long_ops[] = {
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

void print_usage(void)
{
	fprintf(stdout, "-n|--dmg_config\n");
	fprintf(stdout, "daos_run_io_conf <io_conf_file>\n");
}

#define POOL_SIZE	(10ULL << 30)
int
main(int argc, char **argv)
{
	test_arg_t		*arg;
	struct epoch_io_args	*eio_arg;
	char			*fname = NULL;
	void			*state = NULL;
	int			rc;

	MPI_Init(&argc, &argv);
	rc = daos_init();
	if (rc) {
		fprintf(stderr, "daos init failed: rc %d\n", rc);
		goto out_fini;
	}

	while ((rc = getopt_long(argc, argv, "h:n:", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'h':
			print_usage();
			goto out_fini;
		case 'n':
			dmg_config_file = optarg;
			printf("dmg_config_file = %s\n", dmg_config_file);
			break;
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			print_usage();
			rc = -1;
			goto out_fini;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Bad parameters.\n");
		print_usage();
		rc = -1;
		goto out_fini;
	}

	fname = argv[optind];

	rc = obj_setup(&state);
	if (rc) {
		fprintf(stderr, "obj setup failed: rc %d\n", rc);
		goto out_fini;
	}

	arg = state;
	arg->dmg_config = dmg_config_file;
	eio_arg = &arg->eio_args;
	D_INIT_LIST_HEAD(&eio_arg->op_list);
	eio_arg->op_lvl = TEST_LVL_DAOS;
	eio_arg->op_iod_size = 1;
	eio_arg->op_oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	arg->eio_args.op_no_verify = 1;	/* No verification for now */

	MPI_Barrier(MPI_COMM_WORLD);

	rc = io_conf_run(arg, fname);
	if (rc)
		fprintf(stderr, "io_conf_run failed: rc %d\n", rc);

	test_teardown(&state);

	daos_fini();
	MPI_Barrier(MPI_COMM_WORLD);
	fprintf(stdout, "daos_run_io_conf completed successfully\n");
out_fini:
	MPI_Finalize();
	return rc;
}
