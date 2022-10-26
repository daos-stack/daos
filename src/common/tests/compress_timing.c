/*
 * (C) Copyright 2016-2022 Intel Corporation.
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

#include <daos/compression.h>
#include <gurt/common.h>

#define ONE_GB (1024 * 1024 * 1024L)
#define ONE_MB (1024 * 1024L)
#define ONE_KB 1024L
#define ONE_BYTE 1L

static bool verbose;

/** Calgary file in repo is compressed */
const char *calgary_file_path = "src/tests/input/calgary";

enum COMPRESS_DIR {
	DIR_COMPRESS	= 0,
	DIR_DECOMPRESS	= 1,
	DIR_UNKNOWN	= 2,
};

struct blk_info {
	uint8_t			*s_buf;		/** origin block data */
	uint8_t			*c_buf;		/** compressed buffer */
	uint8_t			*d_buf;		/** decompressed buffer */
	uint32_t		block_sz;	/** block size */
	uint32_t		comp_sz;	/** compressed data length */
	uint32_t		decomp_sz;	/** decompressed data length */
	uint32_t		complete;	/** async completion */
};

struct compress_timing_args {
	struct daos_compressor	*compressor;
	struct blk_info		**blk_array;	/** block array */
	uint32_t		blk_count;	/** total block count */
	uint8_t			dir;		/** compress or decompress */
	uint32_t		iterations;	/** iterations to run */
};

/** User-defined compression callback function, for async mode only */
static void
compress_callback_fn(void *user_cb_data, int produced, int status)
{
	struct blk_info *blk = (struct blk_info *)user_cb_data;

	blk->comp_sz = produced;
	blk->complete = 1;
}

/** User-defined decompression callback function, for async mode only */
static void
decompress_callback_fn(void *user_cb_data, int produced, int status)
{
	struct blk_info *blk = (struct blk_info *)user_cb_data;

	blk->decomp_sz = produced;
	blk->complete = 1;
}

static uint32_t
timebox(uint32_t (*cb)(void *), void *arg, uint64_t *nsec)
{
	struct timespec	start, end;
	uint32_t rc;

	d_gettime(&start);
	rc = cb(arg);
	d_gettime(&end);

	*nsec = d_timediff_ns(&start, &end);

	return rc;
}

/** Run compression/decompression test on sync mode */
static uint32_t
compress_timed_cb(void *arg)
{
	struct compress_timing_args	*timing_args = arg;
	struct blk_info			*blk;
	uint32_t			iter, blk_num;
	size_t				produced_total = 0;
	int				rc;

	for (iter = 0; iter < timing_args->iterations; iter++) {
		for (blk_num = 0; blk_num < timing_args->blk_count;
		     blk_num++) {
			blk = timing_args->blk_array[blk_num];
			if (timing_args->dir == DIR_COMPRESS) {
				rc = daos_compressor_compress(
					timing_args->compressor,
					blk->s_buf,
					blk->block_sz,
					blk->c_buf,
					blk->block_sz,
					(size_t *)&blk->comp_sz);

				if (rc)
					blk->comp_sz = 0; /* Incompressable */
			} else if (timing_args->dir == DIR_DECOMPRESS) {
				if (!blk->comp_sz) {
					/**
					 * Not a compressed block, just
					 * copy the origin buffer to the
					 * decompressed buffer.
					 */
					memcpy(blk->d_buf,
					       blk->s_buf,
					       blk->block_sz);
					blk->decomp_sz = blk->block_sz;
					continue;
				}

				rc = daos_compressor_decompress(
					timing_args->compressor,
					blk->c_buf,
					blk->comp_sz,
					blk->d_buf,
					blk->block_sz,
					(size_t *)&blk->decomp_sz);
				if (rc)
					printf("\tError decomp rc=%d\n", rc);

			}
		}
	}

	/** Calculate total bytes produced by compressor for all blocks */
	for (blk_num = 0; blk_num < timing_args->blk_count; blk_num++) {
		blk = timing_args->blk_array[blk_num];
		if (timing_args->dir == DIR_COMPRESS)
			produced_total += blk->comp_sz;
		else
			produced_total += blk->decomp_sz;

	}
	return produced_total;
}

/** Run compression/decompression test on async mode */
static uint32_t
compress_async_timed_cb(void *arg)
{
	struct compress_timing_args	*timing_args = arg;
	struct blk_info			*blk;
	uint32_t			iter, blk_num;
	int				rc = 0;
	uint32_t			all_complete;
	uint32_t			produced_total = 0;

	for (iter = 0; iter < timing_args->iterations; iter++) {
		for (blk_num = 0; blk_num < timing_args->blk_count;
		     blk_num++) {
			blk = timing_args->blk_array[blk_num];
			/** Reset completion flag */
			blk->complete = 0;
			if (timing_args->dir == DIR_COMPRESS) {
				rc = daos_compressor_compress_async(
					timing_args->compressor,
					blk->s_buf,
					blk->block_sz,
					blk->c_buf,
					blk->block_sz,
					(void *)compress_callback_fn,
					(void *)blk);
				if (rc)
					printf("\tError comp rc=%d\n", rc);
			} else if (timing_args->dir == DIR_DECOMPRESS) {
				if (!blk->comp_sz) {
					/**
					 * Not a compressed block, just
					 * copy the origin buffer to the
					 * decompressed buffer.
					 */
					memcpy(blk->d_buf,
					       blk->s_buf,
					       blk->block_sz);
					blk->complete = 1;
					blk->decomp_sz = blk->block_sz;
					continue;
				}
				rc = daos_compressor_decompress_async(
					timing_args->compressor,
					blk->c_buf,
					blk->comp_sz,
					blk->d_buf,
					blk->block_sz,
					(void *)decompress_callback_fn,
					(void *)blk);
				if (rc)
					printf("\tError decomp rc=%d\n", rc);
			}
		}

		/** Polling responses until all blocks are completed */
		do {
			all_complete = 1;
			for (blk_num = 0; blk_num < timing_args->blk_count;
			     blk_num++) {
				blk = timing_args->blk_array[blk_num];
				if (blk->complete == 0)
					all_complete = 0;
			}
			daos_compressor_poll_response(timing_args->compressor);
		} while (!all_complete);
	}

	/** Calculate total bytes produced by compressor for all blocks */
	for (blk_num = 0; blk_num < timing_args->blk_count; blk_num++) {
		blk = timing_args->blk_array[blk_num];
		if (timing_args->dir == DIR_COMPRESS)
			produced_total += blk->comp_sz;
		else
			produced_total += blk->decomp_sz;

	}

	return produced_total;
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
	int			i = 0;
	static const char	*const units[] = {"B", "KB", "MB", "GB", "TB"};

	while (bytes >= 1024) {
		bytes /= 1024;
		i++;
	}
	sprintf(buf, "%lu %s", bytes, units[i]);
}

/** Decompress calgary file using ISA-L */
static int
decompress_calgary_file(uint8_t *src, size_t src_len,
			uint8_t *dst, size_t dst_len)
{
	size_t produced;
	struct compress_ft *ft = daos_compress_type2algo(COMPRESS_TYPE_DEFLATE,
							 0);
	struct daos_compressor	*compressor;

	daos_compressor_init(&compressor, ft, 0);

	daos_compressor_decompress(compressor,
				   src,
				   src_len,
				   dst,
				   dst_len,
				   &produced);

	daos_compressor_destroy(&compressor);
	return produced;
}

/**
 * Test steps:
 * - Read and decompress the compressed calgary file to source buffer
 * - Divide the source buffer into multiple blocks
 * - Compress each block statelessly
 * - Decompress the compressed blocks
 * - Calculate performance number
 * - Verify the result by compare the decompressed buffer with source buffer
 */
static int
run_timings(struct compress_ft *fts[],
	    const int types_count, const size_t *sizes,
	    const int bs_count, uint32_t iterations)
{
	int	size_idx;
	int	type_idx;
	char	hr_str[20];
	char	sz_str[20];
	size_t	nsec;
	int	rc;
	FILE	*fp;
	long int lifile_size;
	size_t	file_sz, total_sz;
	int	offset;
	float	mbs, compr_ratio;
	int	compare;

	unsigned char *f_buf; /** File buffer contains compressed calgary */
	unsigned char *s_buf; /** Original calgary data buffer */
	unsigned char *c_buf; /** Compressed data buffer */
	unsigned char *d_buf; /** Decompressed data buffer */

	fp = fopen(calgary_file_path, "r");
	if (!fp) {
		printf("Open file %s failed.\n", calgary_file_path);
		return -1;
	}
	fseek(fp, 0L, SEEK_END);
	lifile_size = ftell(fp);
	D_ASSERT(lifile_size >= 0);
	file_sz = lifile_size;
	fseek(fp, 0L, SEEK_SET);

	D_ALLOC(f_buf, file_sz);


	/** Read the Calgary corpus data from file */
	rc = fread(f_buf, sizeof(char), file_sz, fp);

	if (!rc) {
		D_FREE(f_buf);
		printf("Read file %s failed.\n", calgary_file_path);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	/**
	 * Allocate buffer to contain the origin calgary data which is
	 * decompressed from the calgary_file_path, with 3 times
	 * of the file size, 1.2MB -> 3.2MB.
	 */
	D_ALLOC(s_buf, 3 * file_sz);

	/** Decompress and restore the calgary file */
	total_sz = decompress_calgary_file(f_buf, file_sz, s_buf, 3 * file_sz);
	bytes_hr(total_sz, sz_str);
	printf("Total size: \t%s\n", sz_str);

	/** Allocate buffer for compress/decompress test purpose */
	D_ALLOC(c_buf, total_sz);
	D_ALLOC(d_buf, total_sz);

	/** Test each block size 4KB -> 512KB */
	for (size_idx = 0; size_idx < bs_count; size_idx++) {
		struct blk_info			**blk_array, *blk;
		struct compress_timing_args	args;
		size_t				size = sizes[size_idx];
		size_t				block_sz;
		uint32_t			blk_count = 0;
		uint32_t			blk_index = 0;
		enum COMPRESS_DIR		dir = DIR_COMPRESS;

		/** Divide the calgary data (3.2MB) into multiple blocks */
		blk_count = (total_sz / size)
				+ (total_sz % size ? 1 : 0);

		/** Alloc block array, each block is compressed separately */
		D_ALLOC(blk_array, blk_count * sizeof(struct blk_info *));

		args.blk_array = blk_array;
		args.blk_count = blk_count;
		args.iterations = iterations;

		for (offset = 0; offset < total_sz; offset += size) {
			if (offset + size > total_sz) /* last block */
				block_sz = total_sz - offset;
			else
				block_sz = size;

			D_ALLOC(blk, sizeof(struct blk_info));

			/** Initialize block info */
			blk->s_buf = s_buf + offset;
			blk->c_buf = c_buf + offset;
			blk->d_buf = d_buf + offset;
			blk->block_sz = block_sz;
			blk->complete = 0;

			/** Add the block info to the block array */
			blk_array[blk_index++] = blk;
		}

		bytes_hr(sizes[size_idx], hr_str);
		printf("Block size: \t%s\n", hr_str);
		printf("Block count: \t%d\n", args.blk_count);

		/** Test each compress algorithm */
		for (type_idx = 0; type_idx < types_count; type_idx++) {
			struct compress_ft	*ft = fts[type_idx];
			struct daos_compressor	*compressor;

			rc = daos_compressor_init(&compressor, ft,
						  sizes[size_idx]);
			if (rc)
				continue;

			args.compressor = compressor;
			memset(c_buf, 0, total_sz);
			memset(d_buf, 0, total_sz);

			/** Test both compression and decompression */
			for (dir = DIR_COMPRESS; dir < DIR_UNKNOWN; dir++) {
				args.dir = dir;
				if (ft->cf_compress_async) /** async support */
					rc = timebox(compress_async_timed_cb,
						     &args,
						     &nsec);
				else
					rc = timebox(compress_timed_cb,
						     &args,
						     &nsec);

				if (!rc) {
					printf("\t%s: Error calculating\n",
					       ft->cf_name);
					continue;
				}

				/** Calculate and print test results */
				mbs = (float)(total_sz / ONE_MB) /
					(((double)nsec / 1e9) /
					args.iterations);

				nsec_hr((nsec / args.iterations / blk_count),
					hr_str);

				if (dir == DIR_COMPRESS) {
					/**
					 * Compress ratio - less is better
					 *  = (compressed size / origin size)
					 */
					D_ASSERT(total_sz != 0);
					compr_ratio = (float)rc / total_sz;
					printf("\t%s:      \t%s\t%s\t"
						"%.1f MB/s\t%.2f%%\n",
						ft->cf_name,
						"comp",
						hr_str,
						mbs,
						compr_ratio * 100);
				} else if (dir == DIR_DECOMPRESS) {
					/**
					 * Verify test result by comparing the
					 * origin data with decompressed data
					 */
					compare = memcmp(s_buf, d_buf,
							 total_sz);
					printf("\t%s:      \t%s\t%s\t"
						"%.1f MB/s\t%s\n",
						ft->cf_name,
						"decomp",
						hr_str,
						mbs,
						compare ? "Fail" : "Pass");
				}
			}
			daos_compressor_destroy(&compressor);
		}

		for (blk_index = 0; blk_index < blk_count; blk_index++)
			D_FREE(blk_array[blk_index]);
		D_FREE(blk_array);
	}

	D_FREE(f_buf);
	D_FREE(s_buf);
	D_FREE(c_buf);
	D_FREE(d_buf);
	return 0;
}

static void
print_usage(char *name)
{
	printf("usage: %s [OPTIONS] ...\n\n", name);
	printf("\t-b BYTES, --bs=BYTES\t\t"
		"Compression block size.\n\t\t\t\t\t"
		"Default: Block sizes will double starting "
		"with 4KB until 512KB\n");
	printf("\t-c COMP, --comp=COMP\t\t"
			"Compression algorithm (lz4, deflate, deflate1,"
			"deflate2, deflate3, deflate4)\n"
		"\t\t\t\t\tDefault: Run through all algorithms\n");
	printf("\t-i ITERATIONS, --iter=ITERATIONS\t\t"
			"How many test iterations to run\n"
		"\t\t\t\t\tDefault: 1000\n");
	printf("\t-q, --qat\t\t\t"
			"Enable QAT hardware accelerator\n"
		"\t\t\t\t\tDefault: disabled\n");
	printf("\t-v, --verbose \t\t\tPrint more info\n");
	printf("\t-h, --help\t\t\tShow this message\n");
}

const char *s_opts = "vhqb:c:i:";
static int idx;

static struct option l_opts[] = {
	{"bs",		required_argument,	NULL, 'b'},
	{"comp",	required_argument,	NULL, 'c'},
	{"iter",	required_argument,	NULL, 'i'},
	{"qat",		no_argument,		NULL, 'q'},
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

int
main(int argc, char *argv[])
{
	const int		MAX_TYPES = 64;
	const int		MAX_SIZES = 256;
	int			type_count = 0;
	int			bs_count = 0;
	struct compress_ft	*compress_fts[MAX_TYPES];
	enum DAOS_COMPRESS_TYPE	type = COMPRESS_TYPE_UNKNOWN;
	size_t			bs_sizes[MAX_SIZES];
	int			opt;
	uint32_t		iterations = 1000;
	bool			qat_prefered = false;
	int			rc = 0;

	if (show_help(argc, argv)) {
		print_usage(argv[0]);
		return 0;
	}

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case 'c': {
			type = daos_contprop2compresstype(
				daos_str2compresscontprop(optarg));

			if (type == COMPRESS_TYPE_UNKNOWN)
				printf("'%s' not a valid compression algo\n",
				       optarg);
			break;
		}
		case 'b': {
			size_t bs = (size_t)atoll(optarg);

			if (bs == 0)
				printf("'%s[%"PRIu64"]' is "
				"not a valid size.\n", optarg, bs);
			bs_sizes[bs_count++] = bs;
			break;
		}
		case 'i': {
			iterations = atoll(optarg);
			if (iterations == 0)
				printf("'%s[%d]' is "
				"not a valid iterations.\n",
				optarg, iterations);
			break;
		}
		case 'q': {
			qat_prefered = true;
			break;
		}
		case 'v':
			verbose = true;
			break;
		case 'h': /** already handled */
		default:
			break;
		}
	}

	if (type == COMPRESS_TYPE_UNKNOWN) {
		/** Setup all types */
		type = COMPRESS_TYPE_UNKNOWN + 1;
		for (; type < COMPRESS_TYPE_END; type++)
			compress_fts[type_count++] =
				daos_compress_type2algo(type, qat_prefered);
	} else {
		compress_fts[type_count++] =
			daos_compress_type2algo(type, qat_prefered);
	}

	if (bs_count == 0) {
		/** Sizes to time are start -> end, doubling each time */
		const uint64_t	start = 4 * ONE_KB * ONE_BYTE;
		const uint64_t	end = 512 * ONE_KB * ONE_BYTE;
		size_t		size;

		for (size = start; size <= end &&
		     bs_count < MAX_SIZES; size *= 2)
			bs_sizes[bs_count++] = size;
	}
	rc = run_timings(compress_fts, type_count,
			 bs_sizes, bs_count, iterations);
	if (rc != 0)
		printf("Error: " DF_RC "\n", DP_RC(rc));

	return -rc;
}
