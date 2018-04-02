/**
 * (C) Copyright 2018 Intel Corporation.
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
#include <mpi.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include "dts_common.h"

/* unused object class to identify VOS (storage only) test mode */
#define DAOS_OC_RAW	 0xBEEF

/* Test class, can be:
 *	vos  : pure storage
 *	ehco : pure network
 *	daos : full stack
 */
int			 ts_class = DAOS_OC_RAW;

char			 ts_pmem_file[PATH_MAX];

unsigned int		 ts_obj_p_cont	= 1;	/* # objects per container */
unsigned int		 ts_dkey_p_obj	= 1;	/* # dkeys per object */
unsigned int		 ts_akey_p_dkey	= 100;	/* # akeys per dkey */
unsigned int		 ts_recx_p_akey	= 1000;	/* # recxs per akey */
/* value type: single or array */
bool			 ts_single	= true;
/* always overwrite value of an akey */
bool			 ts_overwrite;
/* use zero-copy API for VOS, ignored for "echo" or "daos" */
bool			 ts_zero_copy;

uuid_t			 ts_cookie;		/* update cookie for VOS */
daos_handle_t		 ts_oh;			/* object open handle */
daos_obj_id_t		 ts_oid;		/* object ID */
daos_unit_oid_t		 ts_uoid;		/* object shard ID (for VOS) */

struct dts_context	 ts_ctx;

static int
ts_vos_update(struct dts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	if (!ts_zero_copy) {
		rc = vos_obj_update(ts_ctx.tsc_coh, ts_uoid, epoch,
				    ts_cookie, 0, &cred->tc_dkey, 1,
				    &cred->tc_iod, &cred->tc_sgl);
		if (rc)
			return -1;

	} else { /* zero-copy */
		daos_sg_list_t	*sgl;
		daos_handle_t	 ioh;

		rc = vos_obj_zc_update_begin(ts_ctx.tsc_coh, ts_uoid, epoch,
					     &cred->tc_dkey, 1,
					     &cred->tc_iod, &ioh);
		if (rc)
			return rc;

		rc = vos_obj_zc_sgl_at(ioh, 0, &sgl);
		if (rc)
			return rc;

		D__ASSERT(cred->tc_sgl.sg_nr == 1);
		D__ASSERT(sgl->sg_nr_out == 1);

		memcpy(sgl->sg_iovs[0].iov_buf,
		       cred->tc_sgl.sg_iovs[0].iov_buf,
		       cred->tc_sgl.sg_iovs[0].iov_len);

		rc = vos_obj_zc_update_end(ioh, ts_cookie, 0,
					   &cred->tc_dkey, 1,
					   &cred->tc_iod, 0);
		if (rc)
			return rc;
	}
	return 0;
}

static int
ts_daos_update(struct dts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	rc = daos_obj_update(ts_oh, epoch, &cred->tc_dkey, 1,
			     &cred->tc_iod, &cred->tc_sgl, cred->tc_evp);
	return rc;
}

static int
ts_key_insert(void)
{
	int		*indices;
	char		 dkey_buf[DTS_KEY_LEN];
	char		 akey_buf[DTS_KEY_LEN];
	int		 vsize = ts_ctx.tsc_cred_vsize;
	int		 i;
	int		 j;
	int		 rc = 0;
	daos_epoch_t	 epoch = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D__ASSERT(indices != NULL);

	dts_key_gen(dkey_buf, DTS_KEY_LEN, "blade");

	for (i = 0; i < ts_akey_p_dkey; i++) {

		dts_key_gen(akey_buf, DTS_KEY_LEN, "walker");

		for (j = 0; j < ts_recx_p_akey; j++) {
			struct dts_io_credit *cred;
			daos_iod_t	     *iod;
			daos_sg_list_t	     *sgl;
			daos_recx_t	     *recx;

			cred = dts_credit_take(&ts_ctx);
			if (!cred) {
				fprintf(stderr, "test failed\n");
				return -1;
			}

			iod  = &cred->tc_iod;
			sgl  = &cred->tc_sgl;
			recx = &cred->tc_recx;

			memset(iod, 0, sizeof(*iod));
			memset(sgl, 0, sizeof(*sgl));
			memset(recx, 0, sizeof(*recx));

			/* setup dkey */
			memcpy(cred->tc_dbuf, dkey_buf, strlen(dkey_buf));
			daos_iov_set(&cred->tc_dkey, cred->tc_dbuf,
				     strlen(cred->tc_dbuf));

			/* setup I/O descriptor */
			memcpy(cred->tc_abuf, akey_buf, strlen(akey_buf));
			daos_iov_set(&iod->iod_name, cred->tc_abuf,
				     strlen(cred->tc_abuf));
			if (ts_single) {
				iod->iod_type = DAOS_IOD_SINGLE;
				iod->iod_size = vsize;
			} else {
				iod->iod_type = DAOS_IOD_ARRAY;
				iod->iod_size = 1;
			}
			if (ts_single) {
				recx->rx_nr = 1;
			} else {
				recx->rx_nr  = vsize;
				recx->rx_idx = ts_overwrite ?
					       0 : indices[j] * vsize;
			}
			iod->iod_nr    = 1;
			iod->iod_recxs = recx;

			/* initialize value buffer and setup sgl */
			cred->tc_vbuf[0] = 'A' + j % 26;
			cred->tc_vbuf[1] = 'a' + j % 26;
			cred->tc_vbuf[2] = cred->tc_vbuf[vsize - 1] = 0;

			daos_iov_set(&cred->tc_val, cred->tc_vbuf, vsize);
			sgl->sg_iovs = &cred->tc_val;
			sgl->sg_nr = 1;

			/* overwrite can replace orignal data and reduce space
			 * consumption.
			 */
			if (!ts_overwrite)
				epoch++;

			if (ts_class == DAOS_OC_RAW)
				rc = ts_vos_update(cred, epoch);
			else
				rc = ts_daos_update(cred, epoch);

			if (rc != 0) {
				fprintf(stderr, "Update failed: %d\n", rc);
				D__GOTO(failed, rc);
			}
		}
	}
failed:
	free(indices);
	return rc;
}

static int
ts_write_perf(void)
{
	int	i;
	int	j;
	int	rc;

	for (i = 0; i < ts_obj_p_cont; i++) {
		ts_oid = dts_oid_gen(ts_class, ts_ctx.tsc_mpi_rank);

		for (j = 0; j < ts_dkey_p_obj; j++) {
			if (ts_class != DAOS_OC_RAW) {
				rc = daos_obj_open(ts_ctx.tsc_coh, ts_oid, 1,
						   DAOS_OO_RW, &ts_oh, NULL);
				if (rc) {
					fprintf(stderr, "object open failed\n");
					return -1;
				}
			} else {
				memset(&ts_uoid, 0, sizeof(ts_uoid));
				ts_uoid.id_pub = ts_oid;
			}

			rc = ts_key_insert();
			if (rc)
				return rc;

			if (ts_class != DAOS_OC_RAW)
				daos_obj_close(ts_oh, NULL);
		}
	}

	rc = dts_credit_drain(&ts_ctx);
	return rc;
}

static uint64_t
ts_val_factor(uint64_t val, char factor)
{
	switch (factor) {
	default:
		return val;
	case 'k':
		val *= 1000;
		return val;
	case 'm':
		val *= 1000 * 1000;
		return val;
	case 'g':
		val *= 1000 * 1000 * 1000;
		return val;
	case 'K':
		val *= 1024;
		return val;
	case 'M':
		val *= 1024 * 1024;
		return val;
	case 'G':
		val *= 1024 * 1024 * 1024;
		return val;
	}
}

static const char *
ts_class_name(void)
{
	switch (ts_class) {
	default:
		return "unknown";
	case DAOS_OC_RAW:
		return "VOS (storage only)";
	case DAOS_OC_ECHO_RW:
		return "ECHO (network only)";
	case DAOS_OC_TINY_RW:
		return "DAOS (full stack)";
	}
}

static const char *
ts_val_type(void)
{
	return ts_single ? "single" : "array";
}

static const char *
ts_yes_or_no(bool value)
{
	return value ? "yes" : "no";
}

static void
ts_print_usage(void)
{
	printf("daos_perf -- performance benchmark tool for DAOS\n\
\n\
Description:\n\
	The daos_perf utility benchmarks point-to-point I/O performance of\n\
	different layers of the DAOS stack.\n\
\n\
The options are as follows:\n\
-P number\n\
	Pool size, which can have M (megatbytes)or G (gigabytes) as postfix\n\
	of number. E.g. -P 512M, -P 8G.\n\
\n\
-T vos|echo|daos\n\
	Tyes of test, it can be 'vos', 'echo' and 'daos'.\n\
	vos  : run directly on top of Versioning Object Store (VOS).\n\
	echo : I/O traffic generated by the utility only goes through the\n\
	       network stack and never lands to storage.\n\
	daos : I/O traffic goes through the full DAOS stack, including both\n\
	       network and storage.\n\
	The default value is 'vos'\n\
\n\
-C number\n\
	Credits for concurrently asynchronous I/O. It can be value between 1\n\
	and 64. The utility runs in synchronous mode if credits is set to 0.\n\
	This option is ignored for mode 'vos'.\n\
\n\
-o number\n\
	Number of objects are used by the utility.\n\
\n\
-d number\n\
	Number of dkeys per object. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-a number\n\
	Number of akeys per dkey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-r number\n\
	Number of records per akey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-A	Use array value of akey, single value is selected by default.\n\
\n\
-s number\n\
	Size of single value, or extent size of array value. The number can\n\
	have 'K' or 'M' as postfix which stands for kilobyte or megabytes.\n\
\n\
-z	Use zero copy API, this option is only valid for 'vos'\n\
\n\
-t	Instead of using different indices and epochs, all I/Os land to the\n\
	same extent in the same epoch. This option can reduce usage of\n\
	storage space.\n\
\n\
-f pathname\n\
	Full path name of the VOS file.\n");
}

static struct option ts_ops[] = {
	{ "pool",	required_argument,	NULL,	'P' },
	{ "type",	required_argument,	NULL,	'T' },
	{ "credits",	required_argument,	NULL,	'C' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "recx",	required_argument,	NULL,	'r' },
	{ "array",	no_argument,		NULL,	'A' },
	{ "size",	required_argument,	NULL,	's' },
	{ "zcopy",	no_argument,		NULL,	'z' },
	{ "overwrite",	no_argument,		NULL,	't' },
	{ "file",	required_argument,	NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

int
main(int argc, char **argv)
{
	daos_size_t	pool_size = (2ULL << 30); /* default pool size */
	int		credits   = -1;	/* sync mode */
	int		vsize	   = 32;	/* default value size */
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	double		then;
	double		now;
	int		rc;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	memset(ts_pmem_file, 0, sizeof(ts_pmem_file));
	while ((rc = getopt_long(argc, argv, "P:T:C:o:d:a:r:As:ztf:h",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 'T':
			if (!strcasecmp(optarg, "echo")) {
				/* just network, no storage */
				ts_class = DAOS_OC_ECHO_RW;

			} else if (!strcasecmp(optarg, "daos")) {
				/* full stack: network + storage */
				ts_class = DAOS_OC_TINY_RW;

			} else if (!strcasecmp(optarg, "vos")) {
				/* pure storage */
				ts_class = DAOS_OC_RAW;

			} else {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return -1;
			}
			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'P':
			pool_size = strtoul(optarg, &endp, 0);
			pool_size = ts_val_factor(pool_size, *endp);
			break;
		case 'o':
			ts_obj_p_cont = strtoul(optarg, &endp, 0);
			ts_obj_p_cont = ts_val_factor(ts_obj_p_cont, *endp);
			break;
		case 'd':
			ts_dkey_p_obj = strtoul(optarg, &endp, 0);
			ts_dkey_p_obj = ts_val_factor(ts_dkey_p_obj, *endp);
			break;
		case 'a':
			ts_akey_p_dkey = strtoul(optarg, &endp, 0);
			ts_akey_p_dkey = ts_val_factor(ts_akey_p_dkey, *endp);
			break;
		case 'r':
			ts_recx_p_akey = strtoul(optarg, &endp, 0);
			ts_recx_p_akey = ts_val_factor(ts_recx_p_akey, *endp);
			break;
		case 'A':
			ts_single = false;
			break;
		case 's':
			vsize = strtoul(optarg, &endp, 0);
			vsize = ts_val_factor(vsize, *endp);
			break;
		case 't':
			ts_overwrite = true;
			break;
		case 'z':
			ts_zero_copy = true;
			break;
		case 'f':
			strncpy(ts_pmem_file, optarg, PATH_MAX - 1);
			break;
		case 'h':
			if (ts_ctx.tsc_mpi_rank == 0)
				ts_print_usage();
			return 0;
		}
	}

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 ||
	    ts_recx_p_akey == 0) {
		fprintf(stderr, "Invalid arguments %d/%d/%d/\n",
			ts_akey_p_dkey, ts_recx_p_akey,
			ts_recx_p_akey);
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	if (vsize <= sizeof(int))
		vsize = sizeof(int);

	if (ts_ctx.tsc_mpi_rank == 0 || ts_class == DAOS_OC_RAW) {
		uuid_generate(ts_ctx.tsc_pool_uuid);
		uuid_generate(ts_ctx.tsc_cont_uuid);
	}

	if (ts_class == DAOS_OC_RAW) {
		uuid_generate(ts_cookie);
		ts_ctx.tsc_cred_nr = -1; /* VOS can only support sync mode */
		if (strlen(ts_pmem_file) == 0)
			strcpy(ts_pmem_file, "/mnt/daos/vos_perf.pmem");

		ts_ctx.tsc_pmem_file = ts_pmem_file;
	} else {
		ts_ctx.tsc_cred_nr = credits;
		ts_ctx.tsc_svc.rl_nr = 1;
		ts_ctx.tsc_svc.rl_ranks  = &svc_rank;
	}
	ts_ctx.tsc_cred_vsize	= vsize;
	ts_ctx.tsc_pool_size	= pool_size;

	if (ts_ctx.tsc_mpi_rank == 0) {
		fprintf(stdout,
			"Test :\n\t%s\n"
			"Parameters :\n"
			"\tpool size     : %u MB\n"
			"\tcredits       : %d (sync I/O for -ve)\n"
			"\tobj_per_cont  : %u x %d (procs)\n"
			"\tdkey_per_obj  : %u\n"
			"\takey_per_dkey : %u\n"
			"\trecx_per_akey : %u\n"
			"\tvalue type    : %s\n"
			"\tvalue size    : %u\n"
			"\tzero copy     : %s\n"
			"\toverwrite     : %s\n"
			"\tVOS file      : %s\n",
			ts_class_name(),
			(unsigned int)(pool_size >> 20),
			credits,
			ts_obj_p_cont,
			ts_ctx.tsc_mpi_size,
			ts_dkey_p_obj,
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			vsize,
			ts_yes_or_no(ts_zero_copy),
			ts_yes_or_no(ts_overwrite),
			ts_class == DAOS_OC_RAW ? ts_pmem_file : "<NULL>");
	}

	rc = dts_ctx_init(&ts_ctx);
	if (rc)
		return -1;

	if (ts_ctx.tsc_mpi_rank == 0)
		fprintf(stdout, "Started...\n");
	MPI_Barrier(MPI_COMM_WORLD);

	then = dts_time_now();
	/* TODO: add fetch performance test */
	rc = ts_write_perf();
	now = dts_time_now();

	if (ts_ctx.tsc_mpi_size > 1) {
		int rc_g;

		MPI_Allreduce(&rc, &rc_g, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
		rc = rc_g;
	}

	if (rc) {
		fprintf(stderr, "Failed: %d\n", rc);
	} else {
		double		duration, agg_duration;
		double		first_start;
		double		last_end;
		double		duration_max;
		double		duration_min;
		double		duration_sum;

		duration = now - then;

		if (ts_ctx.tsc_mpi_size > 1) {
			MPI_Reduce(&then, &first_start, 1, MPI_DOUBLE,
				   MPI_MIN, 0, MPI_COMM_WORLD);
			MPI_Reduce(&now, &last_end, 1, MPI_DOUBLE,
				   MPI_MAX, 0, MPI_COMM_WORLD);
		} else {
			first_start = then;
			last_end = now;
		}

		agg_duration = last_end - first_start;

		if (ts_ctx.tsc_mpi_size > 1) {
			MPI_Reduce(&duration, &duration_max, 1, MPI_DOUBLE,
				   MPI_MAX, 0, MPI_COMM_WORLD);
			MPI_Reduce(&duration, &duration_min, 1, MPI_DOUBLE,
				   MPI_MIN, 0, MPI_COMM_WORLD);
			MPI_Reduce(&duration, &duration_sum, 1, MPI_DOUBLE,
				   MPI_SUM, 0, MPI_COMM_WORLD);
		} else {
			duration_max = duration_min = duration_sum = duration;
		}

		if (ts_ctx.tsc_mpi_rank == 0) {
			unsigned long	total;
			double		bandwidth;
			double		latency;
			double		rate;

			total = ts_ctx.tsc_mpi_size *
				ts_obj_p_cont * ts_dkey_p_obj *
				ts_akey_p_dkey * ts_recx_p_akey;

			rate = total / agg_duration;
			latency = (agg_duration * 1000 * 1000) / total;
			bandwidth = (rate * vsize) / (1024 * 1024);

			fprintf(stdout, "Successfully completed:\n"
				"\tduration : %-10.6f sec\n"
				"\tbandwith : %-10.3f MB/sec\n"
				"\trate     : %-10.2f IO/sec\n"
				"\tlatency  : %-10.3f us "
				"(nonsense if credits > 1)\n",
				agg_duration, bandwidth, rate, latency);

			fprintf(stdout, "Duration across processes:\n");
			fprintf(stdout, "MAX duration : %-10.6f sec\n",
				duration_max);
			fprintf(stdout, "MIN duration : %-10.6f sec\n",
				duration_min);
			fprintf(stdout, "Average duration : %-10.6f sec\n",
				duration_sum / ts_ctx.tsc_mpi_size);
		}
	}

	dts_ctx_fini(&ts_ctx);
	MPI_Finalize();

	return 0;
}
