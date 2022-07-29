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
#include "perf_internal.h"

daos_size_t	ts_scm_size  = (2ULL << 30);	/* default pool SCM size */
daos_size_t	ts_nvme_size;			/* default pool NVMe size */

bool		ts_const_akey;
char		*ts_dkey_prefix;
unsigned int	ts_obj_p_cont	= 1;	/* # objects per container */
unsigned int	ts_dkey_p_obj	= 256;	/* # dkeys per object */
unsigned int	ts_akey_p_dkey	= 16;	/* # akeys per dkey */
unsigned int	ts_recx_p_akey	= 16;	/* # recxs per akey */
unsigned int	ts_stride	= 64;	/* default extent size */
unsigned int	ts_seed;
bool		ts_single	= true;	/* value type: single or array */
bool		ts_random;		/* random write (array value only) */
bool		ts_pause;

bool		ts_oid_init;

typedef char	key_str_t[DTS_KEY_LEN];
key_str_t	*ts_dkey_vals;
key_str_t	*ts_akey_vals;
daos_key_t	*ts_dkeys;
daos_key_t	*ts_akeys;

daos_handle_t	*ts_ohs;		/* all opened objects */
daos_obj_id_t	*ts_oids;		/* object IDs */
uint64_t	*ts_indices;

struct credit_context	ts_ctx;
pf_update_or_fetch_fn_t	ts_update_or_fetch_fn;

/* buffer for data verification */
struct pf_stride_buf {
	char		*sb_buf;
	char		 sb_mark;
	unsigned int	 sb_size;
};

struct pf_stride_buf	stride_buf;

/* mark 16 bytes within each 4K for verification */
static int stride_marks[] = {
	0,	3,	7,	13,
	23,	56,	105,	158,
	231,	400,	712,	1291,
	1788,	2371,	3116,	3968,
};

#define STRIDE_PAGE	(1 << 12)

enum {
	/* set a few some bytes in stride_buf */
	STRIDE_BUF_SET,
	/* load marked bytes from stride_buf for write */
	STRIDE_BUF_LOAD,
	/* check if read buffer can match with stride buffer */
	STRIDE_BUF_VERIFY,
};

void
stride_buf_init(int size)
{
	stride_buf.sb_mark	= 'A';
	stride_buf.sb_size	= size;
	stride_buf.sb_buf	= calloc(1, size);
	D_ASSERT(stride_buf.sb_buf);
}

void
stride_buf_fini(void)
{
	if (stride_buf.sb_buf)
		free(stride_buf.sb_buf);
}

static int
stride_buf_op(int opc, char *buf, unsigned offset, int size)
{
	unsigned int	i;
	unsigned int	j;
	char		mark = stride_buf.sb_mark;

	if (opc == STRIDE_BUF_SET) {
		stride_buf.sb_mark++;
		if (stride_buf.sb_mark > 'Z')
			stride_buf.sb_mark = 'A';
	}

	for (i = (offset & ~(STRIDE_PAGE - 1));
	     i < stride_buf.sb_size; i += STRIDE_PAGE) {
		for (j = 0; j < ARRAY_SIZE(stride_marks); j++) {
			int	pos;

			pos = i + stride_marks[j];
			if (pos < offset)
				continue;
			/* possible for the last page */
			if (pos >= stride_buf.sb_size)
				break;

			if (pos >= offset + size) {
				/* NB: for single value, unset marks because
				 * old version will be fully overwritten
				 */
				if (ts_single && opc == STRIDE_BUF_SET) {
					stride_buf.sb_buf[pos] = 0;
					continue;
				}
				return 0;
			}

			switch (opc) {
			case STRIDE_BUF_SET:
				stride_buf.sb_buf[pos] = mark;
				break;
			case STRIDE_BUF_VERIFY:
				D_ASSERT(buf);
				if (stride_buf.sb_buf[pos] != buf[pos - offset])
					return -1; /* mismatch */
				break;
			case STRIDE_BUF_LOAD:
				D_ASSERT(buf);
				buf[pos - offset] = stride_buf.sb_buf[pos];
				break;
			}
		}
	}
	return 0;
}

static void
stride_buf_set(unsigned offset, int size)
{
	stride_buf_op(STRIDE_BUF_SET, NULL, offset, size);
}

static void
stride_buf_load(char *buf, unsigned offset, int size)
{
	stride_buf_op(STRIDE_BUF_LOAD, buf, offset, size);
}

static int
stride_buf_verify(char *buf, unsigned offset, int size)
{
	return stride_buf_op(STRIDE_BUF_VERIFY, buf, offset, size);
}


static int
akey_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     daos_key_t *dkey, daos_key_t *akey, daos_epoch_t *epoch,
		     int idx, struct pf_param *param)
{
	struct io_credit *cred;
	daos_iod_t	     *iod;
	d_sg_list_t	     *sgl;
	daos_recx_t	     *recx;
	int		      rc = 0;

	if (param->pa_verbose)
		D_PRINT("%s dkey="DF_KEY" akey="DF_KEY"\n",
			op_type == TS_DO_UPDATE ? "Update" : "Fetch ",
			DP_KEY(dkey), DP_KEY(akey));

	cred = credit_take(&ts_ctx);
	if (!cred) {
		fprintf(stderr, "credit cannot be NULL for IO\n");
		rc = -1;
		return rc;
	}

	iod  = &cred->tc_iod;
	sgl  = &cred->tc_sgl;
	recx = &cred->tc_recx;

	d_iov_set(&cred->tc_dkey, dkey->iov_buf, dkey->iov_len);

	/* setup I/O descriptor */
	d_iov_set(&iod->iod_name, akey->iov_buf, akey->iov_len);
	if (ts_single) {
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = param->pa_rw.size;
		recx->rx_nr   = 1;
		recx->rx_idx  = 0;
	} else {
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		recx->rx_nr   = param->pa_rw.size;
		recx->rx_idx  = ts_indices[idx] * ts_stride +
				param->pa_rw.offset;
	}

	iod->iod_nr    = 1;
	iod->iod_recxs = recx;
	iod->iod_flags = 0;

	if (op_type == TS_DO_UPDATE) {
		/* initialize value buffer and setup sgl */
		stride_buf_load(cred->tc_vbuf, param->pa_rw.offset,
				param->pa_rw.size);
	} else {
		if (param->pa_rw.verify) /* Clear the buffer for verify */
			memset(cred->tc_vbuf, 0, param->pa_rw.size);
	}

	d_iov_set(&cred->tc_val, cred->tc_vbuf, param->pa_rw.size);
	sgl->sg_iovs = &cred->tc_val;
	sgl->sg_nr = 1;
	sgl->sg_nr_out = 0;

	D_ASSERT(ts_update_or_fetch_fn != NULL);
	rc = ts_update_or_fetch_fn(obj_idx, op_type, cred, *epoch,
				   !!param->pa_rw.verify, &param->pa_duration);
	if (rc != 0) {
		fprintf(stderr, "%s failed. rc=%d, epoch=%"PRIu64"\n",
			op_type == TS_DO_FETCH ? "Fetch" : "Update",
			rc, *epoch);
		if (param->pa_rw.verify)
			credit_return(&ts_ctx, cred);
		return rc;
	}

	(*epoch)++;
	if (param->pa_rw.verify) {
		rc = stride_buf_verify(cred->tc_vbuf, param->pa_rw.offset,
				       param->pa_rw.size);
		credit_return(&ts_ctx, cred);
		return rc;
	}
	return 0;
}

static int
dkey_update_or_fetch(enum ts_op_type op_type, daos_key_t *dkey, daos_epoch_t *epoch,
		     struct pf_param *param)
{
	int		 i;
	int		 j;
	int		 k;
	int		 rc = 0;
	int		 akey_idx;

	if (!ts_indices) {
		ts_indices = dts_rand_iarr_alloc_set(ts_recx_p_akey, 0,
						     ts_random);
		D_ASSERT(ts_indices != NULL);
	}

	for (i = 0; i < param->pa_akey_nr; i++) {
		akey_idx = i;
		if (ts_const_akey)
			akey_idx = 0;
		for (j = 0; j < param->pa_recx_nr; j++) {
			for (k = 0; k < param->pa_obj_nr; k++) {
				rc = akey_update_or_fetch(k, op_type, dkey,
							  &ts_akeys[akey_idx], epoch, j,
							  param);
				if (rc)
					break;
			}
		}
	}
	return rc;
}

void
perf_setup_keys(void)
{
	int	i, len;

	for (i = 0; i < ts_dkey_p_obj; i++) {
		dts_key_gen(ts_dkey_vals[i], DTS_KEY_LEN, ts_dkey_prefix);

		if (ts_dkey_prefix == NULL)
			len = sizeof(uint64_t);
		else
			len = min(strlen(ts_dkey_vals[i]), DTS_KEY_LEN);
		d_iov_set(&ts_dkeys[i], ts_dkey_vals[i], len);
	}

	for (i = 0; i < ts_akey_p_dkey; i++) {
		dts_key_gen(ts_akey_vals[i], DTS_KEY_LEN, "akey-");
		len = min(strlen(ts_akey_vals[i]), DTS_KEY_LEN);
		d_iov_set(&ts_akeys[i], ts_akey_vals[i], len);
	}
}

int
objects_update(struct pf_param *param)
{
	daos_epoch_t epoch = crt_hlc_get();
	int		i;
	int		rc = 0;
	int		rc_drain;
	uint64_t	start = 0;

	stride_buf_set(param->pa_rw.offset, param->pa_rw.size);
	++epoch;

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(&param->pa_duration, start);

	for (i = 0; i < param->pa_dkey_nr; i++) {
		rc = dkey_update_or_fetch(TS_DO_UPDATE, &ts_dkeys[i], &epoch,
					  param);
		if (rc)
			break;
	}
	rc_drain = credit_drain(&ts_ctx);
	if (rc == 0)
		rc = rc_drain;

	if (dts_is_async(&ts_ctx))
		TS_TIME_END(&param->pa_duration, start);

	return rc;
}

int
objects_fetch(struct pf_param *param)
{
	int		i;
	int		rc = 0;
	int		rc_drain;
	uint64_t	start = 0;
	daos_epoch_t	epoch = crt_hlc_get();

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(&param->pa_duration, start);

	for (i = 0; i < param->pa_dkey_nr; i++) {
		rc = dkey_update_or_fetch(TS_DO_FETCH, &ts_dkeys[i], &epoch,
					  param);
		if (rc != 0)
			break;
	}
	rc_drain = credit_drain(&ts_ctx);
	if (rc == 0)
		rc = rc_drain;

	if (dts_is_async(&ts_ctx))
		TS_TIME_END(&param->pa_duration, start);
	return rc;
}

/* Test command Format: "C;p=x;q D;a;b"
 *
 * The upper-case character is command, e.g. U=update, F=fetch, anything after
 * semicolon is parameter of the command. Space or tab is the separator between
 * commands.
 */

#define PARAM_SEP	';'
#define PARAM_ASSIGN	'='

int
pf_parse_common(char *str, struct pf_param *param, pf_parse_cb_t parse_cb,
		char **strp)
{
	bool skip = false;
	int  rc;

	/* parse parameters and execute the function. */
	while (1) {
		if (isspace(*str) || *str == 0)
			break; /* end of a test command + parameters */

		if (*str == PARAM_SEP) { /* test command has parameters */
			skip = false;
			str++;
			continue;
		}
		if (skip) { /* skip the current test command */
			str++;
			continue;
		}

		switch (*str) {
		default:
			if (parse_cb) {
				rc = parse_cb(str, param, &str);
				if (rc)
					return rc;
			} else {
				str++;
			}
			break;
		case 'k':
			param->pa_no_reset = true;
			str++;
			break;
		case 'p':
			param->pa_perf = true;
			str++;
			break;
		case 'i':
			str++;
			if (*str != PARAM_ASSIGN)
				return -1;

			param->pa_iteration = strtol(&str[1], &str, 0);
			break;
		case 'v':
			str++;
			param->pa_verbose = true;
			break;
		}
		skip = *str != PARAM_SEP;
	}
	*strp = str;
	return 0;
}

static int
pf_parse_rw_cb(char *str, struct pf_param *param, char **strp)
{
	char	c = *str;
	int	val;

	switch (c) {
	default:
		str++;
		break;
	case 'O':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;
		param->pa_obj_nr = strtol(&str[1], &str, 0);
		if (param->pa_obj_nr > ts_obj_p_cont)
			return -1;
		break;
	case 'D':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;
		param->pa_dkey_nr = strtol(&str[1], &str, 0);
		if (param->pa_dkey_nr > ts_dkey_p_obj)
			return -1;
		break;
	case 'a':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;
		param->pa_akey_nr = strtol(&str[1], &str, 0);
		if (param->pa_akey_nr > ts_akey_p_dkey)
			return -1;
		break;
	case 'n':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;
		param->pa_recx_nr = strtol(&str[1], &str, 0);
		if (param->pa_recx_nr > ts_recx_p_akey)
			return -1;
		break;
	case 'd':
		param->pa_rw.dkey_flag = true;
		str++;
		break;
	case 'o':
	case 's':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;

		val = strtol(&str[1], &str, 0);
		if (val_has_unit(*str)) {
			val = val_unit(val, *str);
			str++;
		}
		if (c == 'o')
			param->pa_rw.offset = val;
		else
			param->pa_rw.size = val;
		break;
	}
	*strp = str;
	return 0;
}

int
pf_parse_rw(char *str, struct pf_param *param, char **strp)
{
	int	rc;

	rc = pf_parse_common(str, param, pf_parse_rw_cb, strp);
	if (rc)
		return rc;

	if (param->pa_rw.size == 0) /* full stride write */
		param->pa_rw.size = ts_stride;

	if (ts_single)
		param->pa_rw.offset = 0;

	if (param->pa_rw.offset + param->pa_rw.size > ts_stride) {
		D_PRINT("offset + size crossed the stride boundary: %d/%d/%d\n",
			param->pa_rw.offset, param->pa_rw.size, ts_stride);
		return -1;
	}
	return 0;
}

static struct pf_test *
find_test(char code, struct pf_test pf_tests[])
{
	struct pf_test	*ts;
	int		 i;

	for (i = 0;; i++) {
		ts = &pf_tests[i];
		if (ts->ts_code == 0)
			break;

		if (ts->ts_code == code)
			return ts;
	}
	fprintf(stderr, "unknown test code %c\n", code);
	return NULL;
}

static void
pause_test(char *name)
{
	int	c;

	while (ts_ctx.tsc_mpi_rank == 0) {
		D_PRINT("Type 'y|Y' to run test=%s: ", name);
		c = getc(stdin);
		if (c == 'y' || c == 'Y')
			break;
	}
	if (ts_ctx.tsc_mpi_size > 1)
		par_barrier(PAR_COMM_WORLD);
}

static int
run_one(struct pf_test *ts, struct pf_param *param)
{
	double	start;
	double	end;
	int	i;
	int	rc;

	/* guarantee the each test can generate the same OIDs/keys */
	srand(ts_seed);
	if (param->pa_iteration == 0)
		param->pa_iteration = 1;

	fprintf(stdout, "Running %s test (iteration=%d",
		ts->ts_name, param->pa_iteration);
	if (param->pa_obj_nr != ts_obj_p_cont)
		fprintf(stdout, ", objects=%d", param->pa_obj_nr);
	if (param->pa_dkey_nr != ts_dkey_p_obj)
		fprintf(stdout, ", dkeys=%d", param->pa_dkey_nr);
	if (param->pa_akey_nr != ts_akey_p_dkey)
		fprintf(stdout, ", akeys=%d", param->pa_akey_nr);
	if (param->pa_recx_nr != ts_recx_p_akey)
		fprintf(stdout, ", recx=%d", param->pa_recx_nr);
	fprintf(stdout, ")\n");

	start = daos_get_ntime();

	for (i = 0; i < param->pa_iteration; i++) {
		if (!param->pa_no_reset)
			dts_reset_key();

		rc = ts->ts_func(ts, param);
		if (rc)
			break;
	}

	end = daos_get_ntime();
	if (ts_ctx.tsc_mpi_size > 1) {
		int	rc_g = 0;

		par_allreduce(PAR_COMM_WORLD, &rc, &rc_g, 1, PAR_INT, PAR_MIN);
		rc = rc_g;
	}

	if (rc != 0) {
		fprintf(stderr, "Failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (param->pa_perf)
		show_result(param, start, end, ts->ts_name);

	return 0;
}

int
run_commands(char *cmds, struct pf_test pf_tests[])
{
	struct pf_test	*ts = NULL;
	bool		 skip = false;
	int		 rc;

	while (1) {
		struct pf_param param;
		char		code;

		if (ts) {
			char	*tmp = cmds;

			if (ts_pause)
				pause_test(ts->ts_name);
			else
				D_PRINT("Running test=%s\n", ts->ts_name);

			memset(&param, 0, sizeof(param));

			param.pa_obj_nr = ts_obj_p_cont;
			param.pa_dkey_nr = ts_dkey_p_obj;
			param.pa_akey_nr = ts_akey_p_dkey;
			param.pa_recx_nr = ts_recx_p_akey;

			/* parse private parameters of the test */
			rc = ts->ts_parse(cmds, &param, &cmds);
			if (rc) {
				D_PRINT("Invalid test parameters: %s\n", tmp);
				return rc;
			}

			/* run the test */
			rc = run_one(ts, &param);
			if (rc) {
				D_PRINT("%s failed\n", ts->ts_name);
				return rc;
			}
			D_PRINT("Completed test=%s\n", ts->ts_name);
			ts = NULL; /* reset */
			continue;
		}

		code = *cmds;
		cmds++;
		if (code == 0) /* finished all the tests */
			return 0;

		if (isspace(code)) { /* move to a new command */
			skip = false;
			continue;
		}

		if (skip) /* unknown test code, skip all parameters */
			continue;

		ts = find_test(code, pf_tests);
		if (!ts) {
			fprintf(stdout, "Unknown test code=%c\n", code);
			skip = true;
			continue;
		}
	}
}

void
show_result(struct pf_param *param, uint64_t start, uint64_t end,
	    char *test_name)
{
	double		agg_duration;
	uint64_t	first_start;
	uint64_t	last_end;
	double		duration_max;
	double		duration_min;
	double		duration_sum;

	if (ts_ctx.tsc_mpi_size > 1) {
		par_reduce(PAR_COMM_WORLD, &start, &first_start, 1, PAR_UINT64, PAR_MIN, 0);
		par_reduce(PAR_COMM_WORLD, &end, &last_end, 1, PAR_UINT64, PAR_MAX, 0);
		agg_duration = (last_end - first_start) /
			       (1000.0 * 1000 * 1000);
	} else {
		agg_duration = param->pa_duration / (1000.0 * 1000);
	}

	/* nano sec to sec */

	if (ts_ctx.tsc_mpi_size > 1) {
		par_reduce(PAR_COMM_WORLD, &param->pa_duration, &duration_max, 1, PAR_DOUBLE,
			   PAR_MAX, 0);
		par_reduce(PAR_COMM_WORLD, &param->pa_duration, &duration_min, 1, PAR_DOUBLE,
			   PAR_MIN, 0);
		par_reduce(PAR_COMM_WORLD, &param->pa_duration, &duration_sum, 1, PAR_DOUBLE,
			   PAR_SUM, 0);
	} else {
		duration_max = duration_min =
		duration_sum = param->pa_duration;
	}

	if (ts_ctx.tsc_mpi_rank == 0) {
		unsigned long	total;
		bool		show_bw = false;
		double		bandwidth;
		double		latency;
		double		rate;

		if (strcmp(test_name, "QUERY") == 0) {
			total = ts_ctx.tsc_mpi_size * param->pa_iteration *
				param->pa_obj_nr;
		} else if (strcmp(test_name, "AGGREGATE") == 0 ||
			   strcmp(test_name, "DISCARD") == 0 ||
			   strcmp(test_name, "GARBAGE COLLECTION") == 0) {
			total = ts_ctx.tsc_mpi_size * param->pa_iteration;
		} else if (strcmp(test_name, "PUNCH") == 0) {
			total = ts_ctx.tsc_mpi_size * param->pa_iteration * param->pa_obj_nr;
			if (param->pa_rw.dkey_flag)
				total *= param->pa_dkey_nr;
		} else {
			show_bw = true;
			total = ts_ctx.tsc_mpi_size * param->pa_iteration *
				param->pa_obj_nr * param->pa_dkey_nr *
				param->pa_akey_nr * param->pa_recx_nr;
		}

		rate = total / agg_duration;
		latency = duration_max / total;

		fprintf(stdout, "%s successfully completed:\n"
			"\tduration : %-10.6f sec\n", test_name, agg_duration);
		if (show_bw) {
			bandwidth = (rate * param->pa_rw.size) / (1024 * 1024);
			fprintf(stdout, "\tbandwith : %-10.3f MB/sec\n", bandwidth);
		}
		fprintf(stdout, "\trate     : %-10.2f IO/sec\n"
			"\tlatency  : %-10.3f us "
			"(nonsense if credits > 1)\n", rate, latency);

		fprintf(stdout, "Duration across processes:\n");
		fprintf(stdout, "\tMAX duration : %-10.6f sec\n",
			duration_max/(1000 * 1000));
		fprintf(stdout, "\tMIN duration : %-10.6f sec\n",
			duration_min/(1000 * 1000));
		fprintf(stdout, "\tAverage duration : %-10.6f sec\n",
			duration_sum / ((ts_ctx.tsc_mpi_size) * 1000 * 1000));
	}
}

const char perf_common_usage[] = "\n"
"The options are as follows:\n"
"-h	Print this help message.\n\n"
"-P number\n"
"	Pool SCM partition size, which can have M(megatbytes) or\n"
"	G(gigabytes) as postfix of number. E.g. -P 512M, -P 8G.\n\n"
"-N number\n"
"	Pool NVMe partition size.\n\n"
"-o number\n"
"	Number of objects are used by the utility.\n\n"
"-d number\n"
"	Number of dkeys per object. The number can have 'k' or 'm' as postfix\n"
"	which stands for kilo or million.\n\n"
"-a number\n"
"	Number of akeys per dkey. The number can have 'k' or 'm' as postfix\n"
"	which stands for kilo or million.\n\n"
"-n number\n"
"	Number of strides per akey. The number can have 'k' or 'm' as postfix\n"
"	which stands for kilo or million.\n\n"
"-s number\n"
"	Value size. The number can have 'K' or 'M' as postfix which stands for\n"
"	kilobyte or megabytes.\n\n"
"-A [R]\n"
"	Use array value of akey, single value is selected by default.\n"
"	optional parameter 'R' indicates random writes\n\n"
"-R commands\n"
"	Execute a series of test commands:\n"
"	'U'    : Update test\n"
"	'F'    : Fetch test\n"
"	'V'    : Verify data consistency\n"
"	'O'    : OID table test (daos_perf only)\n"
"	'Q'    : Query test (vos_perf only)\n"
"	'I'    : VOS iteration test (vos_perf only)\n"
"	'P'    : Punch test (vos_perf only)\n"
"	'p'    : Output performance numbers\n"
"	'i=$N' : Iterate test $N times\n"
"	'k'    : Don't reset key for each iteration\n"
"	'o=$N' : Offset for update or fetch\n"
"	's=$N' : IO size for update or fetch\n"
"	'd'    : Dkey punch (for Punch test)\n"
"	'v'    : Verbose mode\n\n"
"	Test commands are in format of: \"C;p=x;q D;a;b\" The upper-case\n"
"	character is command, e.g. U=update, F=fetch, anything after\n"
"	semicolon is parameter of the command. Space or tab is the separator\n"
"	between commands.\n\n"
"-w	Pause after initialization for attaching debugger or analysis tool\n\n"
"-G seed\n"
"	Random seed\n\n"
"-u pool_uuid\n"
"	Specify an existing pool uuid\n\n"
"-X cont_uuid\n"
"	Specify an existing cont uuid\n";

const struct option perf_common_opts[] = {
	{ "help",	no_argument,		NULL,	'h' },
	{ "pool_scm",	required_argument,	NULL,	'P' },
	{ "pool_nvme",	required_argument,	NULL,	'N' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "num",	required_argument,	NULL,	'n' },
	{ "size",	required_argument,	NULL,	's' },
	{ "array",	optional_argument,	NULL,	'A' },
	{ "run",	required_argument,	NULL,	'R' },
	{ "wait",	no_argument,		NULL,	'w' },
	{ "seed",	required_argument,	NULL,	'G' },
	{ "pool",	required_argument,	NULL,	'u' },
	{ "cont",	required_argument,	NULL,	'X' },
};

const char perf_common_optstr[] = "hP:N:o:d:a:n:s:A::R:wG:u:X:";

int
perf_parse_opts(int rc, char **cmds)
{
	char	*endp;

	switch (rc) {
	case 'h':
		return 1;
	case 'P':
		ts_scm_size = strtoul(optarg, &endp, 0);
		ts_scm_size = val_unit(ts_scm_size, *endp);
		break;
	case 'N':
		ts_nvme_size = strtoul(optarg, &endp, 0);
		ts_nvme_size = val_unit(ts_nvme_size, *endp);
		break;
	case 'o':
		ts_obj_p_cont = strtoul(optarg, &endp, 0);
		ts_obj_p_cont = val_unit(ts_obj_p_cont, *endp);
		break;
	case 'd':
		ts_dkey_p_obj = strtoul(optarg, &endp, 0);
		ts_dkey_p_obj = val_unit(ts_dkey_p_obj, *endp);
		break;
	case 'a':
		ts_akey_p_dkey = strtoul(optarg, &endp, 0);
		ts_akey_p_dkey = val_unit(ts_akey_p_dkey, *endp);
		break;
	case 'n':
		ts_recx_p_akey = strtoul(optarg, &endp, 0);
		ts_recx_p_akey = val_unit(ts_recx_p_akey, *endp);
		break;
	case 's':
		ts_stride = strtoul(optarg, &endp, 0);
		ts_stride = val_unit(ts_stride, *endp);
		break;
	case 'A':
		ts_single = false;
		if (optarg && (optarg[0] == 'r' || optarg[0] == 'R'))
			ts_random = true;
		break;
	case 'R':
		*cmds = optarg;
		break;
	case 'w':
		ts_pause = true;
		break;
	case 'G':
		ts_seed = atoi(optarg);
		break;
	case 'u':
		rc = uuid_parse(optarg, ts_ctx.tsc_pool_uuid);
		printf("Using pool:"DF_UUID"\n", DP_UUID(ts_ctx.tsc_pool_uuid));
		if (rc)
			return rc;
		break;
	case 'X':
		rc = uuid_parse(optarg, ts_ctx.tsc_cont_uuid);
		printf("Using cont:"DF_UUID"\n", DP_UUID(ts_ctx.tsc_cont_uuid));
		if (rc)
			return rc;
		break;
	default:
		fprintf(stderr, "Unknown option %c\n", rc);
		return -1;
	}
	return 0;
}

void
perf_free_opts(struct option *opts, char *optstr)
{
	D_FREE(opts);
	D_FREE(optstr);
}

int
perf_alloc_opts(const struct option opts_in[], int opt_len,
		const char optstr_in[], struct option **opts_out,
		char **optstr_out)
{
	int		c_opt_len;
	int		c_optstr_len = strlen(perf_common_optstr) + 1;
	int		optstr_len = strlen(optstr_in) + 1;
	struct option	*opts;
	char		*optstr;
	int		i, j;

	c_opt_len = ARRAY_SIZE(perf_common_opts);
	opt_len += c_opt_len;

	D_ALLOC_ARRAY(opts, opt_len);
	if (opts == NULL)
		return -1;

	for (i = 0, j = 0; i < opt_len; i++) {
		if (i < c_opt_len)
			opts[i] = perf_common_opts[i];
		else
			opts[i] = opts_in[j++];
	}

	D_ALLOC(optstr, c_optstr_len + optstr_len);
	if (optstr == NULL) {
		D_FREE(opts);
		return -1;
	}

	strncpy(optstr, perf_common_optstr, c_optstr_len);
	strncat(optstr, optstr_in, optstr_len);

	*opts_out = opts;
	*optstr_out = optstr;

	return 0;
}

void
perf_free_keys(void)
{
	if (ts_oids)
		free(ts_oids);
	if (ts_ohs)
		free(ts_ohs);
	if (ts_dkeys)
		free(ts_dkeys);
	if (ts_akeys)
		free(ts_akeys);
	if (ts_dkey_vals)
		free(ts_dkey_vals);
	if (ts_akey_vals)
		free(ts_akey_vals);
}

int
perf_alloc_keys(void)
{
	ts_ohs = calloc(ts_obj_p_cont, sizeof(*ts_ohs));
	if (!ts_ohs)
		return -DER_NOMEM;

	ts_oids = calloc(ts_obj_p_cont, sizeof(*ts_oids));
	if (!ts_oids)
		goto error;

	ts_dkeys = calloc(ts_dkey_p_obj, sizeof(*ts_dkeys));
	if (!ts_dkeys)
		goto error;

	ts_akeys = calloc(ts_akey_p_dkey, sizeof(*ts_akeys));
	if (!ts_akeys)
		goto error;

	ts_dkey_vals = calloc(ts_dkey_p_obj, sizeof(*ts_dkey_vals));
	if (!ts_dkey_vals)
		goto error;

	ts_akey_vals = calloc(ts_akey_p_dkey, sizeof(*ts_akey_vals));
	if (!ts_akey_vals)
		goto error;

	return 0;
error:
	perf_free_keys();
	return -DER_NOMEM;
}
