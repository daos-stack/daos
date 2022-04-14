/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC       DD_FAC(tests)

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_test.h>
#include <daos/dts.h>
#include <daos/dpar.h>
#include "perf_internal.h"

enum {
	TS_MODE_ECHO, /* pure network */
	TS_MODE_DAOS, /* full stack */
};

int	ts_mode = TS_MODE_DAOS;
int	ts_class = OC_SX;

static int
daos_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     struct io_credit *cred, daos_epoch_t epoch,
		     bool sync, double *duration)
{
	daos_event_t *evp = sync ? NULL : cred->tc_evp;
	uint64_t      start = 0;
	int	      rc;

	if (!dts_is_async(&ts_ctx))
		TS_TIME_START(duration, start);
	if (op_type == TS_DO_UPDATE) {
		rc = daos_obj_update(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				     &cred->tc_dkey, 1, &cred->tc_iod,
				     &cred->tc_sgl, evp);
	} else {
		rc = daos_obj_fetch(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				    &cred->tc_dkey, 1, &cred->tc_iod,
				    &cred->tc_sgl, NULL, evp);
	}

	if (!dts_is_async(&ts_ctx))
		TS_TIME_END(duration, start);

	return rc;
}

static int
objects_open(void)
{
	int	i;
	int	rc;

	perf_setup_keys();

	for (i = 0; i < ts_obj_p_cont; i++) {
		if (!ts_oid_init) {
			ts_oids[i] = daos_test_oid_gen(
				ts_ctx.tsc_coh, ts_class, 0, 0,
				ts_ctx.tsc_mpi_rank);
			if (ts_class == DAOS_OC_R2S_SPEC_RANK)
				ts_oids[i] = dts_oid_set_rank(ts_oids[i],
							      RANK_ZERO);
		}

		rc = daos_obj_open(ts_ctx.tsc_coh, ts_oids[i], DAOS_OO_RW,
				   &ts_ohs[i], NULL);
		if (rc) {
			fprintf(stderr, "object open failed\n");
			return -1;
		}
	}
	ts_oid_init = true;
	return 0;
}

static int
objects_close(void)
{
	int i;
	int rc = 0;

	if (!ts_oid_init)
		return 0; /* nothing to do */

	for (i = 0; i < ts_obj_p_cont; i++) {
		rc = daos_obj_close(ts_ohs[i], NULL);
		D_ASSERT(rc == 0);
	}
	return 0;
}

static int
pf_update(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	rc = objects_open();
	if (rc)
		return rc;

	rc = objects_update(param);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_fetch(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	rc = objects_open();
	if (rc)
		return rc;

	param->pa_rw.verify = false;
	rc = objects_fetch(param);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_verify(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	if (ts_single && ts_recx_p_akey > 1) {
		fprintf(stdout, "Verification is unsupported\n");
		return 0;
	}

	rc = objects_open();
	if (rc)
		return rc;

	param->pa_rw.verify = true;
	rc = objects_fetch(param);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_oit(struct pf_test *pf, struct pf_param *param)
{
	static const int OID_ARR_SIZE	= 8;
	daos_obj_id_t	oids[OID_ARR_SIZE];
	daos_anchor_t	anchor;
	daos_handle_t	toh;
	daos_epoch_t	epoch;
	uint32_t	oids_nr;
	int		total;
	int		i;
	int		rc;

	if (ts_mode != TS_MODE_DAOS)
		return 0; /* cannot support */

	rc = daos_cont_create_snap_opt(ts_ctx.tsc_coh, &epoch, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc)
		fprintf(stderr, "failed to create snapshot\n");

	rc = daos_oit_open(ts_ctx.tsc_coh, epoch, &toh, NULL);
	D_ASSERT(rc == 0);

	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		D_ASSERTF(rc == 0, "%d\n", rc);

		D_PRINT("returned %d oids\n", oids_nr);
		for (i = 0; i < oids_nr; i++) {
			if (param->pa_verbose) {
				D_PRINT("oid[%d] ="DF_OID"\n",
					total, DP_OID(oids[i]));
			}
			total++;
		}
		if (daos_anchor_is_eof(&anchor)) {
			D_PRINT("listed %d objects\n", total);
			break;
		}
	}
	rc = daos_oit_close(toh, NULL);
	D_ASSERT(rc == 0);
	return rc;
}

static int
pf_parse_oit(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, NULL, strp);
}

/* predefined test cases */
struct pf_test pf_tests[] = {
	{
		.ts_code	= 'U',
		.ts_name	= "UPDATE",
		.ts_parse	= pf_parse_rw,
		.ts_func	= pf_update,
	},
	{
		.ts_code	= 'F',
		.ts_name	= "FETCH",
		.ts_parse	= pf_parse_rw,
		.ts_func	= pf_fetch,
	},
	{
		.ts_code	= 'V',
		.ts_name	= "VERIFY",
		.ts_parse	= pf_parse_rw,
		.ts_func	= pf_verify,
	},
	{
		.ts_code	= 'O',
		.ts_name	= "OIT",
		.ts_parse	= pf_parse_oit,
		.ts_func	= pf_oit,
	},
	{
		.ts_code	= 0,
	},
};

static int
pf_name2class(char *name)
{
	if (!strcasecmp(name, "R4S")) {
		ts_class = OC_RP_4G1;
	} else if (!strcasecmp(name, "R3S")) {
		ts_class = OC_RP_3G1;
	} else if (!strcasecmp(name, "R2S")) {
		ts_class = OC_RP_2G1;
	} else if (!strcasecmp(name, "TINY")) {
		ts_class = OC_S1;
	} else if (!strcasecmp(name, "LARGE")) {
		ts_class = OC_SX;
	} else if (!strcasecmp(name, "EC2P1")) {
		ts_class = OC_EC_2P1G1;
	} else if (!strcasecmp(name, "EC2P")) {
		ts_class = OC_EC_2P2G1;
	} else if (!strcasecmp(name, "EC4P2")) {
		ts_class = OC_EC_4P2G1;
	} else if (!strcasecmp(name, "EC8P2")) {
		ts_class = OC_EC_8P2G1;
	} else {
		return -1;
	}
	return 0;
}

const char perf_daos_usage[] = "\n"
"-T daos|echo\n"
"	Type of test, it can be 'daos' or 'echo'.\n"
"	daos : I/O traffic goes through the full DAOS stack, including both\n"
"	       network and storage.\n"
"	echo : I/O traffic generated by the utility only goes through the\n"
"	       network stack and never lands to storage.\n"
"	The default value is 'daos'\n\n"
"-C number\n"
"	Credits for concurrently asynchronous I/O. It can be value between 1\n"
"	and 64. The utility runs in synchronous mode if credits is set to 0.\n\n"
"-c TINY|LARGE|R2S|R3S|R4S|EC2P1|EC2P2|EC4P2|EC8P2\n"
"	Object class for DAOS full stack test.\n\n"
"-g dmg_conf\n"
"	dmg configuration file.\n\n"
"Examples:\n"
"	$ daos_perf -C 16 -A -R 'U;p F;i=5;p V'\n";

static void
ts_print_usage(void)
{
	printf("daos_perf -- performance benchmark tool for DAOS\n\n");
	printf("Description:\n");
	printf("The daos_perf utility benchmarks point-to-point I/O "
	       "performance of different layers of the VOS stack.\n");
	printf("%s", perf_common_usage);
	printf("%s", perf_daos_usage);
}

const struct option perf_daos_opts[] = {
	{ "type",	required_argument,	NULL,	'T' },
	{ "credits",	required_argument,	NULL,	'C' },
	{ "class",	required_argument,	NULL,	'c' },
	{ "dmg_conf",	required_argument,	NULL,	'g' },
	{ NULL,		0,			NULL,	0   },
};

const char perf_daos_optstr[] = "T:C:c:g:";

int
main(int argc, char **argv)
{
	char		*cmds	  = NULL;
	char		*dmg_conf = NULL;
	char		uuid_buf[256];
	int		credits   = -1;	/* sync mode */
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	struct option	*ts_opts;
	char		*ts_optstr;
	int		rc, ret;

	ts_dkey_prefix = PF_DKEY_PREF;

	par_init(&argc, &argv);
	par_rank(PAR_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	par_size(PAR_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	rc = perf_alloc_opts(perf_daos_opts, ARRAY_SIZE(perf_daos_opts),
			     perf_daos_optstr, &ts_opts, &ts_optstr);
	if (rc)
		return rc;

	while ((rc = getopt_long(argc, argv, ts_optstr, ts_opts, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			ret = perf_parse_opts(rc, &cmds);
			if (ret) {
				perf_free_opts(ts_opts, ts_optstr);
				if (ret == 1 && ts_ctx.tsc_mpi_rank == 0) {
					ts_print_usage();
					return 0;
				}
				return ret;
			}
			break;
		case 'T':
			if (!strcasecmp(optarg, "echo")) {
				/* just network, no storage */
				ts_mode = TS_MODE_ECHO;

			} else if (!strcasecmp(optarg, "daos")) {
				/* full stack: network + storage */
				ts_mode = TS_MODE_DAOS;

			} else {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return -1;
			}
			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'c':
			rc = pf_name2class(optarg);
			if (rc) {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return rc;
			}
			break;
		case 'g':
			dmg_conf = optarg;
			break;
		}
	}

	if (!cmds) {
		D_PRINT("Please provide command string\n");
		ts_print_usage();
		return -1;
	}

	if (ts_seed == 0) {
		struct timeval	tv;

		gettimeofday(&tv, NULL);
		ts_seed = tv.tv_usec;
	}

	/* Convert object classes for echo mode.
	 * NB: we can also run in echo mode for arbitrary object class by
	 * setting DAOS_IO_BYPASS="target" while starting server.
	 */
	if (ts_mode == TS_MODE_ECHO) {
		if (ts_class == OC_RP_4G1)
			ts_class = DAOS_OC_ECHO_R4S_RW;
		else if (ts_class == OC_RP_3G1)
			ts_class = DAOS_OC_ECHO_R3S_RW;
		else if (ts_class == OC_RP_2G1)
			ts_class = DAOS_OC_ECHO_R2S_RW;
		else
			ts_class = DAOS_OC_ECHO_TINY_RW;

	}

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 || ts_recx_p_akey == 0) {
		fprintf(stderr, "Invalid arguments %d/%d/%d/\n",
			ts_dkey_p_obj, ts_akey_p_dkey, ts_recx_p_akey);
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	ts_ctx.tsc_cred_nr = credits;
	ts_ctx.tsc_svc.rl_nr = 1;
	ts_ctx.tsc_svc.rl_ranks  = &svc_rank;

	if (ts_stride < STRIDE_MIN)
		ts_stride = STRIDE_MIN;

	stride_buf_init(ts_stride);

	ts_ctx.tsc_cred_vsize	= ts_stride;
	ts_ctx.tsc_scm_size	= ts_scm_size;
	ts_ctx.tsc_nvme_size	= ts_nvme_size;
	ts_ctx.tsc_dmg_conf	= dmg_conf;

	/*
	 * For daos_perf, if pool/cont uuids are supplied as command line
	 * arguments it's assumed that the pool/cont were created. If only a
	 * cont uuid is supplied then a pool and container will be created and
	 * the cont uuid will be used during creation
	 */
	if (!uuid_is_null(ts_ctx.tsc_pool_uuid)) {
		ts_ctx.tsc_skip_pool_create = true;
		if (!uuid_is_null(ts_ctx.tsc_cont_uuid))
			ts_ctx.tsc_skip_cont_create = true;
	}

	if (ts_ctx.tsc_mpi_rank == 0) {
		if (!ts_ctx.tsc_skip_cont_create)
			uuid_generate(ts_ctx.tsc_cont_uuid);
		if (!ts_ctx.tsc_skip_pool_create)
			uuid_generate(ts_ctx.tsc_pool_uuid);
	}

	ts_update_or_fetch_fn = daos_update_or_fetch;

	rc = dts_ctx_init(&ts_ctx, NULL);
	if (rc)
		return -1;

	memset(uuid_buf, 0, sizeof(uuid_buf));
	if (ts_ctx.tsc_mpi_rank == 0)
		uuid_unparse(ts_ctx.tsc_pool_uuid, uuid_buf);

	if (ts_ctx.tsc_mpi_rank == 0) {
		fprintf(stdout,
			"Test :\n\t%s\n"
			"Pool :\n\t%s\n"
			"Parameters :\n"
			"\tpool size     : SCM: %u MB, NVMe: %u MB\n"
			"\tcredits       : %d (sync I/O for -ve)\n"
			"\tobj_per_cont  : %u x %d (procs)\n"
			"\tdkey_per_obj  : %u (%s)\n"
			"\takey_per_dkey : %u\n"
			"\trecx_per_akey : %u\n"
			"\tvalue type    : %s\n"
			"\tstride size   : %u\n",
			pf_class2name(ts_class), uuid_buf,
			(unsigned int)(ts_scm_size >> 20),
			(unsigned int)(ts_nvme_size >> 20),
			credits,
			ts_obj_p_cont,
			ts_ctx.tsc_mpi_size,
			ts_dkey_p_obj, ts_dkey_prefix == NULL ? "int" : "buf",
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			ts_stride);
	}

	rc = perf_alloc_keys();
	if (rc != 0) {
		fprintf(stderr, "failed to allocate %u open handles\n",
			ts_obj_p_cont);
		return -1;
	}

	par_barrier(PAR_COMM_WORLD);

	rc = run_commands(cmds, pf_tests);

	if (ts_indices)
		free(ts_indices);
	stride_buf_fini();
	dts_ctx_fini(&ts_ctx);

	par_fini();

	perf_free_keys();
	return 0;
}
