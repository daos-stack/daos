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

static char		db_path[100];
struct bio_ut_args	ut_args;

void
ut_fini(struct bio_ut_args *args)
{
	bio_xsctxt_free(args->bua_xs_ctxt);
	smd_fini();
	lmm_db_fini();
	bio_nvme_fini();
	ABT_finalize();
	daos_debug_fini();
}

#define BIO_UT_NUMA_NODE	-1
#define BIO_UT_MEM_SIZE		1024	/* MB */
#define BIO_UT_HUGEPAGE_SZ	2	/* MB */
#define BIO_UT_TARGET_NR	1

int
ut_init(struct bio_ut_args *args)
{
	struct sys_db	*db;
	char		 nvme_conf[200] = { 0 };
	int		 fd, rc;

	snprintf(nvme_conf, sizeof(nvme_conf), "%s/daos_nvme.conf", db_path);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = ABT_init(0, NULL);
	if (rc != 0)
		goto out_debug;

	fd = open(nvme_conf, O_RDONLY, 0600);
	if (fd < 0) {
		D_ERROR("Failed to open %s. %s\n", nvme_conf, strerror(errno));
		rc = daos_errno2der(errno);
		goto out_abt;
	}
	close(fd);

	rc = bio_nvme_init(nvme_conf, BIO_UT_NUMA_NODE, BIO_UT_MEM_SIZE, BIO_UT_HUGEPAGE_SZ,
			   BIO_UT_TARGET_NR, true);
	if (rc) {
		D_ERROR("NVMe init failed. "DF_RC"\n", DP_RC(rc));
		goto out_abt;
	}

	rc = lmm_db_init_ex(db_path, "self_db", true, true);
	if (rc) {
		D_ERROR("lmm DB init failed. "DF_RC"\n", DP_RC(rc));
		goto out_nvme;
	}
	db = lmm_db_get();

	rc = smd_init(db);
	D_ASSERT(rc == 0);

	rc = bio_xsctxt_alloc(&args->bua_xs_ctxt, BIO_STANDALONE_TGT_ID, true);
	if (rc) {
		D_ERROR("Allocate Per-xstream NVMe context failed. "DF_RC"\n", DP_RC(rc));
		goto out_smd;
	}

	return 0;
out_smd:
	smd_fini();
	lmm_db_fini();
out_nvme:
	bio_nvme_fini();
out_abt:
	ABT_finalize();
out_debug:
	daos_debug_fini();
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
