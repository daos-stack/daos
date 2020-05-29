/**
 * (C) Copyright 2019 Intel Corporation.
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

#ifdef MCHECKSUM_SUPPORT
#include <mchecksum.h>
#include <mchecksum_error.h>
#endif

static int
timebox(int (*cb)(void *), void *arg, uint64_t *usec)
{
	struct timespec	start, end, result;
	uint64_t	delta_us;
	int		rc;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	rc = cb(arg);

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	result = d_timediff(start, end);

	delta_us = ((uint64_t)result.tv_sec * 1000000000 +
				(uint64_t)result.tv_nsec) / 1000;
	*usec = delta_us;

	return rc;
}

struct csum_timing_args {
	struct daos_csummer *csummer;
	uint8_t *buf;
	size_t len;
};

int
csum_timed_cb(void *arg)
{
	struct csum_timing_args *timing_args = arg;
	int rc =  daos_csummer_update(timing_args->csummer, timing_args->buf,
				      timing_args->len);

	if (rc)
		return rc;
	return daos_csummer_finish(timing_args->csummer);
}

int
run_timings(struct csum_ft *fts[], const int types_count,
		 const size_t *sizes, const int sizes_count)
{
	int	size_idx;
	int	type_idx;
	size_t	usec;
	int	rc;


	for (size_idx = 0; size_idx < sizes_count; size_idx++) {
		/** create data to checksum */
		size_t		 len = sizes[size_idx];
		unsigned char	*buf = malloc(len);

		if (!buf) {
			printf("Not enough Memory;");
			return 1;
		}
		memset(buf, 0xa, len);

		printf("Data Length: %"PRIu64"\n", len);

		for (type_idx = 0; type_idx < types_count; type_idx++) {
			struct csum_ft		*ft = fts[type_idx];
			struct daos_csummer	*csummer;
			struct csum_timing_args	 args;
			size_t			 csum_size;
			uint8_t			*csum_buf;

			rc = daos_csummer_init(&csummer, ft, 0, 0, 0, 0, 0);
			if (rc != 0) {
				free(buf);
				return rc;
			}

			args.csummer = csummer;
			args.buf = buf;
			args.len = len;

			csum_size = daos_csummer_get_csum_len(csummer);
			csum_buf = calloc(csum_size, 1);

			daos_csummer_set_buffer(csummer, csum_buf, csum_size);

			rc = timebox(csum_timed_cb, &args, &usec);

			if (rc == 0)
				printf("\t%s:\t%"PRIu64" usec\n",
				       daos_csummer_get_name(csummer), usec);
			else
				printf("\t%s: Error calculating\n",
				       daos_csummer_get_name(csummer));

			free(csum_buf);
			daos_csummer_destroy(&csummer);
		}
		free(buf);
	}

	return 0;
}

/** ----------------------------------------------------------------------- */
/** MCHECKSUM CRC64 */
#ifdef MCHECKSUM_SUPPORT

int
m_csum64_init(struct daos_csummer *obj)
{
	mchecksum_object_t checksum = NULL;
	int rc = mchecksum_init("crc64", &checksum);

	if (rc == MCHECKSUM_SUCCESS)
		rc = 0;

	obj->dcs_ctx = checksum;
	return rc;
}

int
m_csum64_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	int rc;
	mchecksum_object_t checksum = obj->dcs_ctx;

	rc = mchecksum_update(checksum, buf, buf_len);

	/** returns non-negative on success or negative on failure */
	if (rc > 0)
		rc = 0;

	return rc;
}

void
m_csum64_get(struct daos_csummer *obj)
{
	mchecksum_object_t checksum = obj->dcs_ctx;
	uint32_t *crc32 = (uint32_t *) obj->dcs_csum_buf;

	mchecksum_get(checksum, crc32, obj->dcs_csum_buf_size,
		      MCHECKSUM_FINALIZE);
}
size_t
m_csum64_get_size(struct daos_csummer *obj)
{
	mchecksum_object_t checksum = obj->dcs_ctx;

	return mchecksum_get_size(checksum);
}

void
m_csum64_destroy(struct daos_csummer *obj)
{
	mchecksum_object_t checksum = obj->dcs_ctx;

	mchecksum_destroy(checksum);
}

int
m_csum64_finish(struct daos_csummer *obj)
{
	mchecksum_object_t checksum = obj->dcs_ctx;
	uint64_t *crc64 = (uint64_t *) obj->dcs_csum_buf;
	int rc = mchecksum_get(checksum, crc64, obj->dcs_csum_buf_size,
			       MCHECKSUM_FINALIZE);
	if (rc == MCHECKSUM_SUCCESS)
		rc = 0;
	return rc;
}

struct csum_ft m_csum64_algo = {
		.update = m_csum64_update,
		.get = m_csum64_get,
		.init = m_csum64_init,
		.destroy = m_csum64_destroy,
		.finish = m_csum64_finish,
		.get_size = m_csum64_get_size,
		.name = "mcrc64"
	};
#endif

/** ----------------------------------------------------------------------- */

static void
print_usage(char *name)
{
	printf("usage: %s [OPTIONS] ...\n\n", name);
	printf("\t-s BYTES, --size=BYTES\t"
		"Size of data used to calculate checksum.\n"
		"\t\t\tDefault: Sizes will double starting with 128 until 4G");
	printf("\t-c CHECKSUM, --checksum=CHECKSUM\t"
			"Type of checksum (crc16, crc32, crc64, mcrc64)\n"
		"\t\t\tDefault: Run through all checksums");
	printf("\t-h, --help\t\tShow this message\n");
}
const char *s_opts = "hs:c:";
static int idx;

static struct option l_opts[] = {
	{"size",	required_argument,	NULL, 's'},
	{"checksum",	required_argument,	NULL, 'c'},
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

struct csum_ft *
strarg2ft(char *str)
{
	if (csum_str_match(str, "crc16"))
		return daos_csum_type2algo(CSUM_TYPE_ISAL_CRC16_T10DIF);
	if (csum_str_match(str, "crc32"))
		return daos_csum_type2algo(CSUM_TYPE_ISAL_CRC32_ISCSI);
	if (csum_str_match(str, "crc64"))
		return daos_csum_type2algo(CSUM_TYPE_ISAL_CRC64_REFL);

#ifdef MCHECKSUM_SUPPORT
	if (csum_str_match(str, "mcrc64"))
		return &m_csum64_algo;
#endif
	return NULL;
}

int
main(int argc, char *argv[])
{
	const int		 MAX_TYPES = 64;
	const int		 MAX_SIZES = 64;
	int			 type_count = 0;
	int			 sizes_count = 0;
	struct csum_ft		*csum_fts[MAX_TYPES];
	size_t			 sizes[MAX_SIZES];
	int			 opt;

	if (show_help(argc, argv)) {
		print_usage(argv[0]);
		return 0;
	}

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case 'c':
		{
			struct csum_ft *ft = strarg2ft(optarg);

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
		case 'h': /** already handled */
		default:
			break;
		}
	}

	if (type_count == 0) {
		/** Setup all types */
		enum DAOS_CSUM_TYPE type = CSUM_TYPE_UNKNOWN + 1;

		for (; type < CSUM_TYPE_END; type++)
			csum_fts[type_count++] = daos_csum_type2algo(type);
#ifdef MCHECKSUM_SUPPORT
		csum_fts[type_count++] = &m_csum64_algo;
#endif
	}

	if (sizes_count == 0) {
		/** Sizes to time are 128 -> 4 GB, doubling each time */
		const uint64_t	end = 1024*1024*1024*4L; /** 4 GB */
		const uint64_t	start = 128; /** 128 bytes */
		size_t		size;

		for (size = start; size < end; size *= 2)
			sizes[sizes_count++] = size;
	}

	run_timings(csum_fts, type_count, sizes, sizes_count);

	return 0;
}
