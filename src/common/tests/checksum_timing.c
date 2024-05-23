/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include <getopt.h>
#include <daos/checksum.h>
#include <gurt/common.h>

static bool verbose;

static int
timebox(int (*cb)(void *), void *arg, uint64_t *nsec)
{
	struct timespec	start, end;
	int		rc;

	d_gettime(&start);
	rc = cb(arg);
	d_gettime(&end);

	*nsec = d_timediff_ns(&start, &end);

	return rc;
}

struct csum_timing_args {
	struct daos_csummer	*csummer;
	uint8_t			*buf;
	size_t			 len;
	uint32_t		 iterations;
};

static int
csum_timed_cb(void *arg)
{
	struct csum_timing_args	*timing_args = arg;
	int			 i;
	int			 rc = 0;

	for (i = 0; i < timing_args->iterations; i++) {
		rc = daos_csummer_update(timing_args->csummer,
					 timing_args->buf,
					 timing_args->len);
		if (rc)
			return rc;
	}

	rc = daos_csummer_finish(timing_args->csummer);
	return rc;
}

/** Convert nanosec to human readable time */
static void
nsec_hr(double nsec, char *buf)
{
	int			 i = 0;
	static const char	*const units[] = {"nsec", "usec", "sec",
						    "min", "hr"};
	uint32_t divisor[] = {
		1e3 /** nsec->usec */,
		1e6 /** usec->sec */,
		60 /** sec->min */,
		60 /** min->hr */};

	while (nsec >= divisor[i]) {
		nsec /= divisor[i];
		i++;
	}
	sprintf(buf, "%.*f %s", i, nsec, units[i]);
}

/** Convert num of bytes to human readable */
static void
bytes_hr(uint64_t bytes, char *buf)
{
	int			 i = 0;
	static const char	*const units[] = {"B", "KB", "MB", "GB", "TB"};

	while (bytes >= 1024) {
		bytes /= 1024;
		i++;
	}
	sprintf(buf, "%lu %s", bytes, units[i]);
}

static void
print_csum(uint8_t *csum_buf, uint32_t csum_buf_len)
{
	uint32_t i;

	printf("0x");
	for (i = 0; i < csum_buf_len; i++)
		printf("%x", csum_buf[i]);
	printf("\n");
}

static int
run_timings(struct hash_ft *fts[], const int types_count, const size_t *sizes,
	    const int sizes_count, uint32_t iterations)
{
	int	size_idx;
	int	type_idx;
	char	hr_str[20];
	size_t	nsec;
	int	rc;

	for (size_idx = 0; size_idx < sizes_count; size_idx++) {
		/** create data to checksum */
		size_t		 len = sizes[size_idx];
		unsigned char	*buf;

		D_ALLOC(buf, len);
		if (!buf) {
			printf("Not enough Memory;");
			return -DER_NOMEM;
		}
		memset(buf, 0xa, len);
		bytes_hr(len, hr_str);
		printf("Data Length: %s\n", hr_str);

		for (type_idx = 0; type_idx < types_count; type_idx++) {
			struct hash_ft		*ft = fts[type_idx];
			struct daos_csummer	*csummer;
			struct csum_timing_args	 args;
			size_t			 csum_size;
			uint8_t			*csum_buf;

			rc = daos_csummer_init(&csummer, ft, 0, 0);
			if (rc != 0) {
				D_FREE(buf);
				return rc;
			}

			args.csummer = csummer;
			args.buf = buf;
			args.len = len;

			csum_size = daos_csummer_get_csum_len(csummer);
			D_ALLOC(csum_buf, csum_size);
			if (csum_buf == NULL)
				return -DER_NOMEM;

			daos_csummer_set_buffer(csummer, csum_buf, csum_size);

			args.iterations = iterations;
			rc = timebox(csum_timed_cb, &args, &nsec);

			if (rc == 0) {
				nsec_hr(nsec / args.iterations, hr_str);
				printf("\t%s\t[%dB]:\t%s\n",
				       daos_csummer_get_name(csummer),
				       daos_csummer_get_csum_len(csummer),
				       hr_str);
				if (verbose)
					print_csum(csum_buf, csum_size);
			} else {
				printf("\t%s: Error calculating\n",
				       daos_csummer_get_name(csummer));
			}

			D_FREE(csum_buf);
			daos_csummer_destroy(&csummer);
		}
		D_FREE(buf);
	}

	return 0;
}

static int
murmur64_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint64_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
murmur64_reset(void *daos_mhash_ctx)
{
	memset(daos_mhash_ctx, 0, sizeof(uint64_t));
	return 0;
}

static void
murmur64_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
murmur64_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint64_t *val = (uint64_t *)daos_mhash_ctx;

	*((uint64_t *)buf) = *val;

	return 0;
}

static int
murmur64_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint64_t *value = (uint64_t *)daos_mhash_ctx;

	*value = d_hash_murmur64(buf, buf_len, *value);
	return 0;
}

struct hash_ft murmur64_algo = {
	.cf_init	= murmur64_init,
	.cf_reset	= murmur64_reset,
	.cf_destroy	= murmur64_destroy,
	.cf_finish	= murmur64_finish,
	.cf_update	= murmur64_update,
	.cf_hash_len	= sizeof(uint64_t),
	.cf_name	= "murm64"
};

static int
string32_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint64_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
string32_reset(void *daos_mhash_ctx)
{
	memset(daos_mhash_ctx, 0, sizeof(uint64_t));
	return 0;
}

static void
string32_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
string32_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint64_t *val = (uint64_t *)daos_mhash_ctx;

	*((uint64_t *)buf) = *val;

	return 0;
}

static int
string32_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint32_t *value = (uint32_t *)daos_mhash_ctx;

	*value = d_hash_string_u32((char *)buf, buf_len);
	return 0;
}

struct hash_ft string32_algo = {
	.cf_init	= string32_init,
	.cf_reset	= string32_reset,
	.cf_destroy	= string32_destroy,
	.cf_finish	= string32_finish,
	.cf_update	= string32_update,
	.cf_hash_len	= sizeof(uint32_t),
	.cf_name	= "str32"
};

/** ----------------------------------------------------------------------- */

static void
print_usage(char *name)
{
	printf("usage: %s [OPTIONS] ...\n\n", name);
	printf("\t-s BYTES, --size=BYTES\t\t"
		"Size of data used to calculate checksum.\n\t\t\t\t\t"
		"Default: Sizes will double starting with 128 until 4G\n");
	printf("\t-c CHECKSUM, --csum=CSUM\t"
			"Type of checksum (crc16, crc32, crc64, mcrc64)\n"
		"\t\t\t\t\tDefault: Run through all checksums\n");
	printf("\t-v, --verbose \t\t\tPrint more info\n");
	printf("\t-h, --help\t\t\tShow this message\n");
}

const char *s_opts = "vhs:c:";
static int idx;

static struct option l_opts[] = {
	{"size",	required_argument,	NULL, 's'},
	{"checksum",	required_argument,	NULL, 'c'},
	{"verbose",	no_argument,		NULL, 'v'},
	{"help",	no_argument,		NULL, 'h'}
};

bool
show_help(int argc, char *argv[])
{
	bool result = false;
	int t_optind = optind;
	int opt;

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case '?': /** If invalid option, show help */
		case 'h':
			result = true;
			break;
		default:
			break;
		}
	}
	optind = t_optind;

	return result;
}

#define	csum_str_match(str, csum_str) \
	(strncmp((str), (csum_str), sizeof((csum_str))) == 0)

struct hash_ft *
strarg2ft(char *str)
{
	if (csum_str_match(str, "crc16"))
		return daos_mhash_type2algo(HASH_TYPE_CRC16);
	if (csum_str_match(str, "crc32"))
		return daos_mhash_type2algo(HASH_TYPE_CRC32);
	if (csum_str_match(str, "adler32"))
		return daos_mhash_type2algo(HASH_TYPE_ADLER32);
	if (csum_str_match(str, "crc64"))
		return daos_mhash_type2algo(HASH_TYPE_CRC64);
	if (csum_str_match(str, "sha1"))
		return daos_mhash_type2algo(HASH_TYPE_SHA1);
	if (csum_str_match(str, "sha256"))
		return daos_mhash_type2algo(HASH_TYPE_SHA256);
	if (csum_str_match(str, "sha512"))
		return daos_mhash_type2algo(HASH_TYPE_SHA512);

	return NULL;
}

#define ONE_GB (1024 * 1024 * 1024L)
#define ONE_MB (1024 * 1024L)
#define ONE_KB 1024L
#define ONE_BYTE 1L

int
main(int argc, char *argv[])
{
	const int		 MAX_TYPES = 64;
	const int		 MAX_SIZES = 256;
	int			 type_count = 0;
	int			 sizes_count = 0;
	struct hash_ft		*csum_fts[MAX_TYPES];
	size_t			 sizes[MAX_SIZES];
	int			 opt;
	int			 rc = 0;

	if (show_help(argc, argv)) {
		print_usage(argv[0]);
		return 0;
	}

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case 'c':
		{
			struct hash_ft *ft = strarg2ft(optarg);

			if (ft == NULL)
				printf("'%s' not a valid csum type\n",
				       optarg);
			else {
				csum_fts[type_count++] = ft;
			}
			break;
		}
		case 's': {
			size_t size = (size_t)atoll(optarg);

			if (size == 0)
				printf("'%s[%"PRIu64"]' is "
				"not a valid size.\n", optarg, size);
			sizes[sizes_count++] = size;
		}
			break;
		case 'v':
			verbose = true;
			break;
		case 'h': /** already handled */
		default:
			break;
		}
	}

	if (type_count == 0) {
		/** Setup all types */
		enum DAOS_HASH_TYPE type = HASH_TYPE_UNKNOWN + 1;

		for (; type < HASH_TYPE_END; type++)
			csum_fts[type_count++] = daos_mhash_type2algo(type);
		csum_fts[type_count++] = &murmur64_algo;
		csum_fts[type_count++] = &string32_algo;
	}

	if (sizes_count == 0) {
		/** Sizes to time are start -> end, doubling each time */
		const uint64_t	start = 1 * ONE_BYTE;
		const uint64_t	end = 1 * ONE_MB;
		size_t		size;

		for (size = start; size <= end &&
		     sizes_count < MAX_SIZES; size *= 2)
			sizes[sizes_count++] = size;
	}
	rc = run_timings(csum_fts, type_count, sizes, sizes_count, 1000);
	if (rc != 0)
		printf("Error: "DF_RC"\n", DP_RC(rc));

	return -rc;
}
