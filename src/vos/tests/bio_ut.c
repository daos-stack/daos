/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include "bio_ut.h"
#include "../vos_tls.h"
#include <daos_srv/vos.h>

static char		db_path[100];
struct bio_ut_args	ut_args;

void
ut_fini(struct bio_ut_args *args)
{
	vos_self_fini();
	daos_debug_fini();
}

#define BIO_UT_NUMA_NODE	-1
#define BIO_UT_MEM_SIZE		1024	/* MB */
#define BIO_UT_HUGEPAGE_SZ	2	/* MB */
#define BIO_UT_TARGET_NR	1

int
ut_init(struct bio_ut_args *args)
{
	int rc;

	daos_debug_init(DAOS_LOG_DEFAULT);

	rc = vos_self_init(db_path, false, BIO_STANDALONE_TGT_ID);
	if (rc)
		daos_debug_fini();
	else
		args->bua_xs_ctxt = vos_xsctxt_get();

	return rc;
}

static inline void
print_usage(void)
{
	fprintf(stdout, "bio_ut [-d <db_path>] [-s rand_seed]\n");
}

int main(int argc, char **argv)
{
	static struct option long_ops[] = {
		{ "db_path",	required_argument,	NULL,	'd' },
		{ "seed",	required_argument,	NULL,	's' },
		{ "help",	no_argument,		NULL,	'h' },
		{ NULL,		0,			NULL,	0   },
	};
	int rc;

	d_register_alt_assert(mock_assert);

	ut_args.bua_seed = (unsigned int)(time(NULL) & 0xFFFFFFFFUL);
	while ((rc = getopt_long(argc, argv, "d:s:h", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'd':
			memset(db_path, 0, sizeof(db_path));
			strncpy(db_path, optarg, sizeof(db_path) - 1);
			break;
		case 's':
			ut_args.bua_seed = atol(optarg);
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			fprintf(stderr, "unknown option %c\n", rc);
			print_usage();
			return -1;
		}
	}

	if (strlen(db_path) == 0)
		strncpy(db_path, "/mnt/daos", sizeof(db_path) - 1);

	fprintf(stdout, "Run all BIO unit tests with rand seed:%u\n", ut_args.bua_seed);
	rc = run_wal_tests();

	return rc;
}
