/*
 * (C) 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos.h>
#include <gurt/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <time.h>

/****************/
/* Local Macros */
/****************/
#define BENCHMARK_NAME "array I/O rate"

#ifndef DAOS_VERSION
#define DAOS_VERSION "0.0.0"
#endif

#define NDIGITS                2
#define NWIDTH                 27

#define DAOS_PERF_BUF_SIZE_MAX (1 << 24)
#define DAOS_PERF_SKIP_SMALL   100
#define DAOS_PERF_SKIP_LARGE   10
#define DAOS_PERF_LARGE_SIZE   8192

/* Helper macros for error checking */
#define DAOS_PERF_CHECK_D_ERROR(label, rc, ...)                                                    \
	do {                                                                                       \
		if (unlikely(rc != 0)) {                                                           \
			DL_ERROR(rc, __VA_ARGS__);                                                 \
			goto label;                                                                \
		}                                                                                  \
	} while (0)

#define DAOS_PERF_CHECK_ERROR(cond, label, rc, err_val, ...)                                       \
	do {                                                                                       \
		if (unlikely(cond)) {                                                              \
			rc = err_val;                                                              \
			DL_ERROR(rc, __VA_ARGS__);                                                 \
			goto label;                                                                \
		}                                                                                  \
	} while (0)

#define DAOS_PERF_OPTS_DEFAULTS                                                                    \
	((struct daos_perf_opts){.pool_str     = NULL,                                             \
				 .cont_str     = NULL,                                             \
				 .svc          = NULL,                                             \
				 .oclass       = OC_S1,                                            \
				 .buf_size_min = 0,                                                \
				 .buf_size_max = DAOS_PERF_BUF_SIZE_MAX,                           \
				 .chunk_size   = 0,                                                \
				 .loop         = 1,                                                \
				 .verify       = false,                                            \
				 .read         = false,                                            \
				 .write        = false})

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct daos_perf_opts {
	char            *pool_str;
	char            *cont_str;
	char            *svc;
	daos_oclass_id_t oclass;
	size_t           buf_size_min;
	size_t           buf_size_max;
	size_t           chunk_size;
	int              loop;
	bool             verify;
	bool             read;
	bool             write;
};

struct daos_perf_info {
	struct daos_perf_opts opts;
	daos_handle_t         poh;
	daos_handle_t         coh;
	void                 *buf;
	size_t                buf_size;
	int                   rank;
};

/********************/
/* Local Prototypes */
/********************/

static void
daos_perf_parse_options(int argc, char *argv[], struct daos_perf_opts *opts);

static void
daos_perf_free_options(struct daos_perf_opts *opts);

static size_t
daos_perf_parse_size(const char *str);

static void
daos_perf_usage(const char *execname);

static void
daos_perf_init_data(void *buf, size_t buf_size);

static int
daos_perf_verify_data(const void *buf, size_t buf_size);

static void
daos_perf_print_header(const struct daos_perf_info *info);

static void
daos_perf_print_result(const struct daos_perf_info *info, size_t buf_size, struct timespec t_w,
		       struct timespec t_r);

static int
daos_perf_run(const struct daos_perf_info *info, size_t buf_size, size_t skip);

/*******************/
/* Local Variables */
/*******************/

static const char   *daos_perf_short_options = "hP:C:S:O:y:z:c:l:rw:v";

static struct option daos_perf_long_options[] = {{"help", no_argument, NULL, 'h'},
						 {"pool", required_argument, NULL, 'P'},
						 {"cont", required_argument, NULL, 'C'},
						 {"svc", required_argument, NULL, 'S'},
						 {"oclass", required_argument, NULL, 'O'},
						 {"buf_size_min", required_argument, NULL, 'y'},
						 {"buf_size_max", required_argument, NULL, 'z'},
						 {"chunk_size", required_argument, NULL, 'c'},
						 {"loop", required_argument, NULL, 'l'},
						 {"read", no_argument, NULL, 'r'},
						 {"write", no_argument, NULL, 'w'},
						 {"verify", no_argument, NULL, 'v'},
						 {NULL, 0, NULL, 0}};

/*****************************/
/* Local Function Definition */
/*****************************/

static void
daos_perf_usage(const char *execname)
{
	printf("Usage: %s [options]\n", execname);
	printf("Options:\n");
	printf("  -h, --help                Show this help message\n");
	printf("  -P, --pool POOL_STR       Pool string (required)\n");
	printf("  -C, --cont CONT_STR       Container label (required)\n");
	printf("  -S, --svc SVC_RANKS       Service ranks (optional)\n");
	printf("  -O, --oclass CLASS_ID     Object class (optional)\n");
	printf("  -y, --buf_size_min SIZE   Minimum buffer size (default: 0)\n");
	printf("  -z, --buf_size_max SIZE   Maximum buffer size (default: 16MB)\n");
	printf("  -c, --chunk_size SIZE     DAOS chunk size (optional)\n");
	printf("  -l, --loop LOOP           Loop count (default: 1)\n");
	printf("  -r, --read                Measure read (default: false)\n");
	printf("  -w, --write               Measure write (default: true)\n");
	printf("  -v, --verify              Verify data (default: false)\n");
}

static size_t
daos_perf_parse_size(const char *str)
{
	char  *endptr;
	size_t size;

	if (str == NULL)
		return 0;

	size = strtoull(str, &endptr, 0);

	if (endptr == str)
		return 0;

	switch (*endptr) {
	case 'k':
	case 'K':
		size *= 1024;
		break;
	case 'm':
	case 'M':
		size *= 1024 * 1024;
		break;
	case 'g':
	case 'G':
		size *= 1024 * 1024 * 1024;
		break;
	}

	return size;
}

static void
daos_perf_parse_options(int argc, char *argv[], struct daos_perf_opts *opts)
{
	int c;

	while ((c = getopt_long(argc, argv, daos_perf_short_options, daos_perf_long_options,
				NULL)) != -1) {
		switch (c) {
		case 'h':
			daos_perf_usage(argv[0]);
			exit(EXIT_SUCCESS);
		case 'P':
			opts->pool_str = strdup(optarg);
			break;
		case 'C':
			opts->cont_str = strdup(optarg);
			break;
		case 'S':
			opts->svc = strdup(optarg);
			break;
		case 'O':
			opts->oclass = daos_oclass_name2id(optarg);
			break;
		case 'y':
			opts->buf_size_min = daos_perf_parse_size(optarg);
			break;
		case 'z':
			opts->buf_size_max = daos_perf_parse_size(optarg);
			break;
		case 'c':
			opts->chunk_size = daos_perf_parse_size(optarg);
			break;
		case 'l':
			opts->loop = atoi(optarg);
			break;
		case 'v':
			opts->verify = true;
			break;
		default:
			fprintf(stderr, "Unknown option -%c\n", c);
			daos_perf_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	/* Validate required options */
	if (opts->pool_str == NULL || opts->cont_str == NULL) {
		fprintf(stderr, "Error: --pool and --cont are required\n");
		daos_perf_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Set defaults */
	if (opts->buf_size_max == 0)
		opts->buf_size_max = DAOS_PERF_BUF_SIZE_MAX;
	if (opts->loop <= 0)
		opts->loop = 1;
}

static void
daos_perf_free_options(struct daos_perf_opts *opts)
{
	free(opts->pool_str);
	free(opts->cont_str);
	free(opts->svc);
}

static void
daos_perf_init_data(void *buf, size_t buf_size)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t   i;

	for (i = 0; i < buf_size; i++)
		ptr[i] = (uint8_t)(i & 0xFF);
}

static int
daos_perf_verify_data(const void *buf, size_t buf_size)
{
	const uint8_t *ptr = (const uint8_t *)buf;
	size_t         i;

	for (i = 0; i < buf_size; i++) {
		if (ptr[i] != (uint8_t)(i & 0xFF)) {
			fprintf(stderr,
				"Data verification failed at offset %zu: expected %u, got %u\n", i,
				(uint8_t)(i & 0xFF), ptr[i]);
			return -1;
		}
	}
	return 0;
}

static void
daos_perf_print_header(const struct daos_perf_info *info)
{
	const struct daos_perf_opts *opts = &info->opts;

	printf("# DAOS %s v" DAOS_VERSION "\n", BENCHMARK_NAME);
	printf("# %d client process(es)\n", 1);
	printf("# Loop %d times from size %zu to %zu byte(s)\n", opts->loop, opts->buf_size_min,
	       opts->buf_size_max);
	if (opts->verify)
		printf("# WARNING verifying data, output will be slower\n");
	if (opts->oclass) {
		char oclass_name[64];
		daos_oclass_id2name(opts->oclass, oclass_name);
		printf("# Using object class %s\n", oclass_name);
	}
	printf("%-*s%*s%*s%*s%*s\n", 10, "# Size", NWIDTH, "\"Write Rate (ops/s)\"", NWIDTH,
	       "\"Write Time (us)\"", NWIDTH, "\"Read Rate (ops/s)\"", NWIDTH,
	       "\"Read Time (us)\"");
	fflush(stdout);
}

static void
daos_perf_print_result(const struct daos_perf_info *info, size_t buf_size, struct timespec t_w,
		       struct timespec t_r)
{
	const struct daos_perf_opts *opts = &info->opts;
	double                       w_time, r_time;

	w_time = d_time2s(t_w) * 1e6 / (double)opts->loop;
	r_time = d_time2s(t_r) * 1e6 / (double)opts->loop;

	printf("%-*zu%*lu%*.*f%*lu%*.*f\n", 10, buf_size, NWIDTH, (long unsigned int)(1e6 / w_time),
	       NWIDTH, NDIGITS, w_time, NWIDTH, (long unsigned int)(1e6 / r_time), NWIDTH, NDIGITS,
	       r_time);
}

static int
daos_perf_run(const struct daos_perf_info *info, size_t buf_size, size_t skip)
{
	const struct daos_perf_opts *opts = &info->opts;
	daos_obj_id_t                oid  = {0, 0};
	daos_handle_t                oh;
	size_t                       chunk_size = opts->chunk_size ? opts->chunk_size : buf_size;
	size_t                       i;
	struct timespec              t1, t2, t3, t4;
	int                          rc;
	bool                         create = false;

	rc = daos_cont_alloc_oids(info->coh, 1, &oid.lo, NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not allocate obj id");

	rc = daos_obj_generate_oid(info->coh, &oid, DAOS_OT_ARRAY_BYTE, opts->oclass, 0, 0);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not generate oid");

	rc = daos_array_open_with_attr(info->coh, oid, DAOS_TX_NONE, DAOS_OO_RW, 1, chunk_size, &oh,
				       NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not create array");
	create = true;

	/* Warm up for RPC */
	for (i = 0; i < skip + (size_t)opts->loop; i++) {
		d_iov_t iov = {.iov_buf = info->buf, .iov_buf_len = buf_size, .iov_len = buf_size};
		daos_range_t     rg  = {.rg_idx = i * buf_size, .rg_len = buf_size};
		daos_array_iod_t iod = {.arr_nr = 1, .arr_rgs = &rg};
		d_sg_list_t      sgl = {.sg_nr = 1, .sg_iovs = &iov};

		if (i == skip)
			d_gettime(&t1);

		rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
		DAOS_PERF_CHECK_D_ERROR(error, rc, "could not write to array");

		if (opts->verify) {
			rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
			DAOS_PERF_CHECK_D_ERROR(error, rc, "could not read from array");
			DAOS_PERF_CHECK_ERROR(iod.arr_nr_read != buf_size, error, rc, -DER_INVAL,
					      "read %zu bytes, expected %zu", iod.arr_nr_read,
					      buf_size);

			rc = daos_perf_verify_data(info->buf, buf_size);
			DAOS_PERF_CHECK_ERROR(rc != 0, error, rc, -DER_INVAL,
					      "data verification failed");
		}
	}
	d_gettime(&t2);

	for (i = 0; i < skip + (size_t)opts->loop; i++) {
		d_iov_t iov = {.iov_buf = info->buf, .iov_buf_len = buf_size, .iov_len = buf_size};
		daos_range_t     rg  = {.rg_idx = i * buf_size, .rg_len = buf_size};
		daos_array_iod_t iod = {.arr_nr = 1, .arr_rgs = &rg};
		d_sg_list_t      sgl = {.sg_nr = 1, .sg_iovs = &iov};

		if (i == skip)
			d_gettime(&t3);

		rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
		DAOS_PERF_CHECK_D_ERROR(error, rc, "could not read from array");

		if (opts->verify) {
			DAOS_PERF_CHECK_ERROR(iod.arr_nr_read != buf_size, error, rc, -DER_INVAL,
					      "read %zu bytes, expected %zu", iod.arr_nr_read,
					      buf_size);

			rc = daos_perf_verify_data(info->buf, buf_size);
			DAOS_PERF_CHECK_ERROR(rc != 0, error, rc, -DER_INVAL,
					      "data verification failed");
		}
	}
	d_gettime(&t4);

	daos_perf_print_result(info, buf_size, d_timediff(&t1, &t2), d_timediff(&t3, &t4));

	/* Release and remove array */
	rc = daos_array_destroy(oh, DAOS_TX_NONE, NULL);
	if (rc != 0)
		DL_WARN(rc, "could not remove array");

	rc = daos_array_close(oh, NULL);
	if (rc != 0)
		DL_WARN(rc, "could not close obj");

	return 0;

error:
	if (!create)
		return rc;

	rc = daos_array_destroy(oh, DAOS_TX_NONE, NULL);
	if (rc != 0)
		DL_WARN(rc, "could not remove array");

	rc = daos_array_close(oh, NULL);
	if (rc != 0)
		DL_WARN(rc, "could not close obj");

	return rc;
}

/***************************/
/* Main Function           */
/***************************/

int
main(int argc, char *argv[])
{
	struct daos_perf_info info = {0};
	struct daos_perf_opts opts = {
	    .pool_str     = NULL,
	    .cont_str     = NULL,
	    .svc          = NULL,
	    .buf_size_min = 0,
	    .buf_size_max = DAOS_PERF_BUF_SIZE_MAX,
	    .loop         = 1,
	    .verify       = false,
	};
	size_t size;
	int    rc;

	/* Parse options */
	daos_perf_parse_options(argc, argv, &opts);

	info.opts     = opts;
	info.buf_size = opts.buf_size_max;
	info.rank     = 0; /* Single rank for now */

	/* Initialize DAOS */
	rc = daos_init();
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not initialize DAOS");

	/* Connect to pool */
	rc = daos_pool_connect(opts.pool_str, NULL, DAOS_PC_RW, &info.poh, NULL, NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not connect to pool");

	/* Open container */
	rc = daos_cont_open(info.poh, opts.cont_str, O_RDWR, &info.coh, NULL, NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not open container");

	/* Allocate buffer */
	D_ALLOC(info.buf, info.buf_size);
	DAOS_PERF_CHECK_ERROR(info.buf == NULL, error, rc, ENOMEM, "could not allocate buffer");
	daos_perf_init_data(info.buf, info.buf_size);

	/* Print header */
	daos_perf_print_header(&info);

	/* Write with different sizes */
	for (size = MAX(1, opts.buf_size_min); size <= opts.buf_size_max; size *= 2) {
		rc = daos_perf_run(&info, size,
				   size > DAOS_PERF_LARGE_SIZE ? DAOS_PERF_SKIP_LARGE
							       : DAOS_PERF_SKIP_SMALL);
		DAOS_PERF_CHECK_D_ERROR(error, rc, "could not measure perf for size %zu", size);
	}

	/* Cleanup */
	D_FREE(info.buf);
	rc = daos_cont_close(info.coh, NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not close container");
	rc = daos_pool_disconnect(info.poh, NULL);
	DAOS_PERF_CHECK_D_ERROR(error, rc, "could not disconnect from pool");

	daos_fini();

	daos_perf_free_options(&opts);

	return EXIT_SUCCESS;

error:
	/* Cleanup */
	D_FREE(info.buf);
	daos_cont_close(info.coh, NULL);
	daos_pool_disconnect(info.poh, NULL);
	daos_fini();
	daos_perf_free_options(&opts);

	return EXIT_FAILURE;
}
