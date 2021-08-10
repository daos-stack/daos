/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(st)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "configini.h"
#include "crt_utils.h"
#include "daos_errno.h"

/* Configini Section names */
#define DEFAULT_SCALE_NAME			"threshold"
#define DEFAULT_VALUE_NAME			"default_values"
#define RAW_DATA_EXTENSION			"raw"
#define RESULT_EXTENSION			"results"

#define MASTER_VALUE_SIZE			64
#define INVALID_SCALING				-1.0
#define MAX_NUMBER_KEYS				200
#define CRT_SELF_TEST_AUTO_BULK_THRESH		(1 << 20)
#define CRT_SELF_TEST_GROUP_NAME		("crt_self_test")

/* User input maximum values */
#define SELF_TEST_MAX_REPETITIONS		(0x40000000)
#define SELF_TEST_MAX_INFLIGHT			(0x40000000)
#define SELF_TEST_MAX_LIST_STR_LEN		(1 << 16)
#define SELF_TEST_MAX_NUM_ENDPOINTS		(UINT32_MAX)
#define SELF_TEST_MAX_RAW_DATA_OUTPUT		(0x00000400)

struct st_size_params {
	uint32_t send_size;
	uint32_t reply_size;
	union {
		struct {
			enum crt_st_msg_type send_type: 2;
			enum crt_st_msg_type reply_type: 2;
		};
		uint32_t flags;
	};
};

struct st_endpoint {
	uint32_t rank;
	uint32_t tag;
};

struct st_master_endpt {
	crt_endpoint_t endpt;
	struct crt_st_status_req_out reply;
	int32_t test_failed;
	int32_t test_completed;
};

const struct {
	char identifier;
	char *short_name;
	char *long_name;
	enum crt_st_msg_type type;
} transfer_type_map[] = {{'e', "E", "EMPTY", CRT_SELF_TEST_MSG_TYPE_EMPTY},
			 {'i', "I", "IOV", CRT_SELF_TEST_MSG_TYPE_IOV},
			 {'b', "Bp", "BULK_PUT",
			  CRT_SELF_TEST_MSG_TYPE_BULK_PUT},
			 {'r', "Bg", "BULK_GET",
			  CRT_SELF_TEST_MSG_TYPE_BULK_GET} };

#define MSG_TYPE_STR(str, id)					\
	{							\
	int _i;							\
	for (_i = 0; _i < 4; _i++) {				\
		if (id == transfer_type_map[_i].type) {		\
			str =  transfer_type_map[_i].long_name;	\
			break;					\
		}						\
	}	\
	}

#define	TST_HIGH	0x01	/* test in higher direction */
#define	TST_LOW		0x02	/* test in lower direction */
#define	TST_OUTPUT	0x10	/* print results */
struct status_feature {
	char	*name;
	int	 value;
	float	 scale;
	int	 flag;
	char	*description;
};

static struct status_feature status[] = {
	{"bw", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Bandwidth"},
	{"tp", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Throughput"},
	{"av", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Averages"},
	{"sd", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Standard Deviations"},
	{"min", 0, 0, TST_LOW | TST_OUTPUT, "Minimum"},
	{"med25", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Medium 25"},
	{"med50", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Medium 50"},
	{"med75", 0, 0, TST_HIGH | TST_LOW | TST_OUTPUT, "Medium 75"},
	{"max", 0, 0, TST_HIGH | TST_OUTPUT, "Maximum"},
};

/* Global shutdown flag, used to terminate the progress thread */
struct global_params {
	int			g_shutdown_flag;
	const int		g_default_rep_count;
	bool			g_randomize_endpoints;
	char			*g_dest_name;
	struct st_endpoint	*g_endpts;
	struct st_endpoint	*g_ms_endpts;
	uint32_t		 g_num_endpts;
	uint32_t		 g_num_ms_endpts;
	char			*g_msg_sizes_str;
	int			 g_rep_count;
	int			 g_max_inflight;
	int16_t			 g_buf_alignment;
	float			 g_scale_factor;
	int			 g_output_megabits;
	int			 g_raw_data;
	char			*g_attach_info_path;
	char			*g_expected_outfile;
	char			*g_expected_infile;
	char			*g_expected_results;
	char			*g_config_append;

	const int		 g_default_max_inflight;
	bool			 alloc_g_dest_name;
	bool			 alloc_g_msg_sizes_str;
	bool			 alloc_g_attach_info_path;
	bool			 alloc_g_expected_outfile;
	bool			 alloc_g_expected_infile;
	bool			 alloc_g_expected_results;
	bool			 alloc_g_config_append;
	Config			*cfg_output;
};

struct global_params gbl = {
	0,			/*  g_shutdown_flag; */
	10000,			/*  g_default_rep_count; */
	false,			/*  g_randomize_endpoints; */
	NULL,			/* *g_dest_name; */
	NULL,			/* *g_endpts; */
	NULL,			/* *g_ms_endpts; */
	0,			/*  g_num_endpts; */
	0,			/*  g_num_ms_endpts; */
	NULL,			/* *g_msg_sizes_str; */
	0,			/*  g_rep_count; */
	0,			/*  g_max_inflight; */
	CRT_ST_BUF_ALIGN_DEFAULT,/*	g_buf_alignment;  */
	INVALID_SCALING,	/* g_scale_factor; */
	0,			/* g_output_megabits; */
	0,			/*  g_raw_data; */
	NULL,			/* *g_attach_info_path; */
	NULL,			/* *g_expected_outfile; */
	NULL,			/* *g_expected_infile; */
	NULL,			/* *g_expected_results; */
	NULL,			/* *g_config_append; */
	1000,			/*  g_default_max_inflight; */
	true,			/*  alloc_g_dest_name; */
	false,			/*  alloc_g_msg_sizes_str; */
	true,			/*  alloc_g_attach_info_path; */
	false,			/*  alloc_g_expected_outfile; */
	false,			/*  alloc_g_expected_infile; */
	false,			/*  alloc_g_expected_results; */
	false,			/*  alloc_g_config_append; */
	NULL,			/* *cfg_output; */
};

static struct option long_options[] = {
	{"file-name", required_argument, 0, 'f'},
	{"config", required_argument, 0, 'c'},
	{"config-append", required_argument, 0, 'o'},
	{"display", required_argument, 0, 'd'},

	{"group-name", required_argument, 0, 'g'},
	{"master-endpoint", required_argument, 0, 'm'},
	{"endpoint", required_argument, 0, 'e'},
	{"message-sizes", required_argument, 0, 's'},
	{"repetitions-per-size", required_argument, 0, 'r'},
	{"max-inflight-rpcs", required_argument, 0, 'i'},
	{"align", required_argument, 0, 'a'},
	{"Mbits", no_argument, 0, 'b'},
	{"randomize-endpoints", no_argument, 0, 'q'},
	{"path", required_argument, 0, 'p'},
	{"help", no_argument, 0, 'h'},
	{"raw_data", required_argument, 0, 'v'},
	{"expected-threshold", required_argument, 0, 'w'},
	{"expected-results", required_argument, 0, 'x'},
	{"expected-input", required_argument, 0, 'y'},
	{"expected-output", required_argument, 0, 'z'},
#define INCLUDE_OBSOLETE
#ifdef INCLUDE_OBSOLETE
	{"nopmix", no_argument, 0, 'n'},
	{"singleton", no_argument, 0, 't'},
#endif
	{0, 0, 0, 0}
};
static int g_shutdown_flag;
static bool g_randomize_endpoints;
static bool g_group_inited;

#ifdef INCLUDE_OBSOLETE
#define ARGV_PARAMETERS "a:bc:d:e:f:g:hi:m:no:p:qr:s:tv:w:x:y:z:"
#else
#define ARGV_PARAMETERS "a:bc:d:e:f:g:hi:m:o:p:qr:s:v:w:x:y:z:"
#endif

/* Default parameters */
char	default_msg_sizes_str[] =
	"b200000,b200000 0,0 b200000,b200000 i1000,i1000 b200000,"
	"i1000,i1000 0,0 i1000,0";

/* Forward declarations */
static char *config_section_name_create(char *section_name,
					struct crt_st_start_params
					*test_params);

/* ********************************************* */
static void *
progress_fn(void *arg)
{
	int		ret;
	crt_context_t	*crt_ctx = NULL;

	crt_ctx = (crt_context_t *)arg;
	D_ASSERT(crt_ctx != NULL);
	D_ASSERT(*crt_ctx != NULL);

	while (!gbl.g_shutdown_flag) {
		ret = crt_progress(*crt_ctx, 1);
		if (ret != 0 && ret != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed; ret = %d\n", ret);
			break;
		}
	};

	pthread_exit(NULL);
}

static int
self_test_init(char *dest_name, crt_context_t *crt_ctx,
	       crt_group_t **srv_grp, pthread_t *tid,
	       char *attach_info_path, bool listen)
{
	uint32_t	 init_flags = 0;
	uint32_t	 grp_size;
	d_rank_list_t	*rank_list = NULL;
	int		 attach_retries = 40;
	int		 i;
	d_rank_t	 max_rank = 0;
	int		 ret;

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, attach_retries, false, false);

	if (listen)
		init_flags |= CRT_FLAG_BIT_SERVER;
	ret = crt_init(CRT_SELF_TEST_GROUP_NAME, init_flags);
	if (ret != 0) {
		D_ERROR("crt_init failed; ret = %d\n", ret);
		return ret;
	}

	if (attach_info_path) {
		ret = crt_group_config_path_set(attach_info_path);
		D_ASSERTF(ret == 0,
			  "crt_group_config_path_set failed, ret = %d\n", ret);
	}

	ret = crt_context_create(crt_ctx);
	if (ret != 0) {
		D_ERROR("crt_context_create failed; ret = %d\n", ret);
		return ret;
	}

	while (attach_retries-- > 0) {
		ret = crt_group_attach(dest_name, srv_grp);
		if (ret == 0)
			break;
		sleep(1);
	}

	if (ret != 0) {
		D_ERROR("crt_group_attach failed; ret = %d\n", ret);
		return ret;
	}

	g_group_inited = true;

	D_ASSERTF(*srv_grp != NULL,
		  "crt_group_attach succeeded but returned group is NULL\n");

	DBG_PRINT("Attached %s\n", dest_name);

	gbl.g_shutdown_flag = 0;

	ret = pthread_create(tid, NULL, progress_fn, crt_ctx);
	if (ret != 0) {
		D_ERROR("failed to create progress thread: %s\n",
			strerror(errno));
		return -DER_MISC;
	}

	ret = crt_group_size(*srv_grp, &grp_size);
	D_ASSERTF(ret == 0, "crt_group_size() failed; rc=%d\n", ret);

	ret = crt_group_ranks_get(*srv_grp, &rank_list);
	D_ASSERTF(ret == 0,
		  "crt_group_ranks_get() failed; rc=%d\n", ret);

	D_ASSERTF(rank_list != NULL, "Rank list is NULL\n");

	D_ASSERTF(rank_list->rl_nr == grp_size,
		  "rank_list differs in size. expected %d got %d\n",
		  grp_size, rank_list->rl_nr);

	ret = crt_group_psr_set(*srv_grp, rank_list->rl_ranks[0]);
	D_ASSERTF(ret == 0, "crt_group_psr_set() failed; rc=%d\n", ret);

	/* waiting to sync with the following parameters
	 * 0 - tag 0
	 * 1 - total ctx
	 * 5 - ping timeout
	 * 150 - total timeout
	 */
	ret = crtu_wait_for_ranks(*crt_ctx, *srv_grp, rank_list,
				  0, 1, 5, 150);
	D_ASSERTF(ret == 0, "wait_for_ranks() failed; ret=%d\n", ret);

	max_rank = rank_list->rl_ranks[0];
	for (i = 1; i < rank_list->rl_nr; i++) {
		if (rank_list->rl_ranks[i] > max_rank)
			max_rank = rank_list->rl_ranks[i];
	}

	d_rank_list_free(rank_list);

	ret = crt_rank_self_set(max_rank + 1);
	if (ret != 0) {
		D_ERROR("crt_rank_self_set failed; ret = %d\n", ret);
		return ret;
	}

	return 0;
}

/* Sort by rank and then by tag */
static int
st_compare_endpts(const void *a_in, const void *b_in)
{
	struct st_endpoint *a = (struct st_endpoint *)a_in;
	struct st_endpoint *b = (struct st_endpoint *)b_in;

	if (a->rank != b->rank)
		return a->rank > b->rank;
	return a->tag > b->tag;
}

/* Sort by latencies (stored in val element */
static int
st_compare_latencies_by_vals(const void *a_in, const void *b_in)
{
	struct st_latency *a = (struct st_latency *)a_in;
	struct st_latency *b = (struct st_latency *)b_in;

	if (a->val != b->val)
		return a->val > b->val;
	return a->cci_rc > b->cci_rc;
}

/* Sort by rank, then by tag, and then by latencies */
static int
st_compare_latencies_by_ranks(const void *a_in, const void *b_in)
{
	struct st_latency *a = (struct st_latency *)a_in;
	struct st_latency *b = (struct st_latency *)b_in;

	if (a->rank != b->rank)
		return a->rank > b->rank;
	if (a->tag != b->tag)
		return a->tag > b->tag;
	if (a->val != b->val)
		return a->val > b->val;
	return a->cci_rc > b->cci_rc;
}

static void
start_test_cb(const struct crt_cb_info *cb_info)
{
	/* Result returned to main thread */
	int32_t *return_status = cb_info->cci_arg;

	/* Status retrieved from the RPC result payload */
	int32_t *reply_status;

	/* Check the status of the RPC transport itself */
	if (cb_info->cci_rc != 0) {
		*return_status = cb_info->cci_rc;
		return;
	}

	/* Get the status from the payload */
	reply_status = (int32_t *)crt_reply_get(cb_info->cci_rpc);
	D_ASSERT(reply_status != NULL);

	/* Return whatever result we got to the main thread */
	*return_status = *reply_status;
}

static void
status_req_cb(const struct crt_cb_info *cb_info)
{
	/* Result returned to main thread */
	struct crt_st_status_req_out   *return_status = cb_info->cci_arg;

	/* Status retrieved from the RPC result payload */
	struct crt_st_status_req_out   *reply_status;

	/* Check the status of the RPC transport itself */
	if (cb_info->cci_rc != 0) {
		return_status->status = cb_info->cci_rc;
		return;
	}

	/* Get the status from the payload */
	reply_status = crt_reply_get(cb_info->cci_rpc);
	D_ASSERT(reply_status != NULL);

	/*
	 * Return whatever result we got to the main thread
	 *
	 * Write these in specific order so we can avoid locking
	 * TODO: This assumes int32 writes are atomic
	 *   (they are on x86 if 4-byte aligned)
	 */
	return_status->test_duration_ns = reply_status->test_duration_ns;
	return_status->num_remaining = reply_status->num_remaining;
	return_status->status = reply_status->status;
}

/*
 * Iterates over a list of failing latency measurements and prints out the
 * count of each type of failure, along with the error string and code
 *
 * The input latencies must be sorted by cci_rc to group all same cci_rc values
 * together into contiguous blocks (-1 -1 -1, -2 -2 -2, etc.)
 */
static void
print_fail_counts(struct st_latency *latencies,
		  uint32_t num_latencies,
		  const char *prefix)
{
	uint32_t last_err_idx = 0;
	uint32_t local_rep = 0;

	/* Function called but no errors to print */
	if (latencies[0].cci_rc == 0)
		return;

	while (1) {
		/*
		 * Detect when the error code has changed and print the count
		 * of the last error code block
		 */
		if (local_rep >= num_latencies ||
		    latencies[local_rep].cci_rc == 0 ||
		    latencies[last_err_idx].cci_rc !=
		    latencies[local_rep].cci_rc) {
			printf("%s%u: -%s (%d)\n", prefix,
			       local_rep - last_err_idx,
			       d_errstr(-latencies[last_err_idx].cci_rc),
			       latencies[last_err_idx].cci_rc);
			last_err_idx = local_rep;
		}

		/* Abort upon reaching the end of the list or a non-failure */
		if (local_rep >= num_latencies ||
		    latencies[local_rep].cci_rc == 0)
			break;

		local_rep++;
	}
}

/* Calculates all statics. Returns the number of valid points */
static int
calculate_stats(struct st_latency *latencies, int count,
		int64_t *av, double *sd, int64_t *min,
		int64_t *max, int64_t *med25, int64_t *med50,
		int64_t *med75, int64_t *total)
{
	uint32_t	i;
	uint32_t	num_failed = 0;
	uint32_t	num_passed = 0;
	double		latency_std_dev = 0;
	int64_t		latency_avg = 0;
	int64_t		value;
	int64_t		lmin = 0;
	int64_t		lmax = 0;
	int		idx;

	/* Find initial value for max and min */
	for (i = 0; i < count; i++) {
		if (latencies[i].cci_rc == 0) {
			lmax = latencies[i].val;
			lmin = latencies[i].val;
			break;
		}
	}

	/* Sum total for average.  Find max/min */
	for (i = 0; i < count; i++) {
		if (latencies[i].cci_rc < 0) {
			num_failed++;
			continue;
		}
		num_passed += 1;
		value = latencies[i].val;
		latency_avg += value;
		lmin = ((lmin < value) ? lmin : value);
		lmax = ((lmax > value) ? lmax : value);
	}
	*total = latency_avg;
	latency_avg /= num_passed;
	*av = latency_avg;
	*min = lmin;
	*max = lmax;

	/* Find sum square from average (variance) and standard deviation */
	for (i = 0; i < count; i++) {
		if (latencies[i].cci_rc < 0) {
			/* avoid checkpatch warning */
			continue;
		}
		value = latencies[i].val - latency_avg;
		latency_std_dev += value * value;
	}
	latency_std_dev /= num_passed;
	latency_std_dev = sqrt(latency_std_dev);
	*sd = latency_std_dev;

	/* Find mediam values.  Works for sorted input only. */
	idx = (count - num_failed - 1) / 4;
	*med25 = latencies[idx].val;
	idx = (count - num_failed - 1) / 2;
	*med50 = latencies[idx].val;
	idx = ((count - num_failed - 1) * 3) / 4;
	*med75 = latencies[idx].val;

	/* return number of points evaluated */
	return num_passed;
}

static void
print_results(struct st_latency *latencies,
	      struct crt_st_start_params *test_params,
	      int64_t test_duration_ns, int output_megabits,
	      Config *cfg, char *section_name,
	      char *section_name_raw)
{
	ConfigRet	 ret;
	Config		*Ocfg = gbl.cfg_output;
	uint32_t	 local_rep;
	uint32_t	 num_failed = 0;
	uint32_t	 num_passed = 0;
	double		 latency_std_dev;
	int64_t		 latency_avg;
	int64_t		 latency_min;
	int64_t		 latency_max;
	int64_t		 latency_med25;
	int64_t		 latency_med50;
	int64_t		 latency_med75;
	int64_t		 latency_total;
	double		 throughput;
	double		 bandwidth;
	char		 kname[3 * MASTER_VALUE_SIZE];
	char		 new_key_name[2 * MASTER_VALUE_SIZE];
	char		 master[MASTER_VALUE_SIZE];
	int64_t		 size_per_request;

	/* Check for bugs */
	D_ASSERT(latencies != NULL);
	D_ASSERT(test_params != NULL);
	D_ASSERT(test_params->rep_count != 0);
	D_ASSERT(test_duration_ns > 0);

	/* Read master-endpoint string */
	ConfigReadString(cfg, section_name, "master_endpoint",
			 master, sizeof(master), "ME");

	/* Compute the throughput in RPCs/sec */
	throughput = test_params->rep_count /
		(test_duration_ns / 1000000000.0F);

	/* Compute bandwidth in bytes */
	size_per_request = test_params->send_size + test_params->reply_size;
	bandwidth = throughput * size_per_request;

	/* Print the results for this size */
	if (output_megabits)
		printf("\tRPC Bandwidth (Mbits/sec): %.2f\n",
		       bandwidth * 8.0F / 1000000.0F);
	else
		printf("\tRPC Bandwidth (MB/sec): %.2f\n",
		       bandwidth / (1024.0F * 1024.0F));

	printf("\tRPC Throughput (RPCs/sec): %.0f\n", throughput);

	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-bw", master);
	ConfigAddInt(Ocfg, section_name, new_key_name,
		     bandwidth / (1024.0F * 1024.0F));
	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-tp", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, throughput);

	D_DEBUG(DB_TEST,
		" Grp: %s, rep %d, sendSize %d, replySize %d\n",
		test_params->srv_grp, test_params->rep_count,
		test_params->send_size, test_params->reply_size);

	/* Figure out how many repetitions were errors */
	num_failed = 0;
	for (local_rep = 0; local_rep < test_params->rep_count; local_rep++) {
#ifdef PRINT_ALL
		printf("    rank:tag %d:%d  iteration %3d: latenct %10ld\n",
		       latencies[local_rep].rank, latencies[local_rep].tag,
		       local_rep, latencies[local_rep].val);
#endif
		/* Place raw data into output configuration */
		if ((section_name_raw != NULL)  &&
		    (local_rep <= gbl.g_raw_data) &&
		    (latencies[local_rep].cci_rc >= 0)) {
			snprintf(new_key_name, sizeof(new_key_name),
				 "%s-%d:%d-:%05d", master,
				 latencies[local_rep].rank,
				 latencies[local_rep].tag,
				 local_rep);
			ret = ConfigAddInt(Ocfg, section_name_raw,
					   new_key_name,
					   latencies[local_rep].val);
			if (ret != CONFIG_OK) {
				/* avoid checkpatch warning */
				D_INFO(" Could not place raw data into %s\n",
				       section_name_raw);
			}
		}

		if (latencies[local_rep].cci_rc < 0) {
			num_failed++;

			/* Since this RPC failed, overwrite its latency
			 * with -1 so it will sort before any passing
			 * RPCs. This segments the latencies into two
			 * sections - from [0:num_failed] will be -1,
			 * and from [num_failed:] will be successful RPC
			 * latencies
			 */
			latencies[local_rep].val = -1;
		}
	}

	/*
	 * Compute number successful and exit early if none worked to
	 * guard against overflow and divide by zero later
	 */
	num_passed = test_params->rep_count - num_failed;
	if (num_passed == 0) {
		printf("\tAll RPCs for this message size failed\n");
		return;
	}

	/*
	 * Sort the latencies by: (in descending order of precedence)
	 * - val
	 * - cci_rc
	 *
	 * Note that errors have a val = -1, so they get grouped together
	 */
	qsort(latencies, test_params->rep_count,
	      sizeof(latencies[0]), st_compare_latencies_by_vals);

	calculate_stats(latencies, test_params->rep_count,
			&latency_avg, &latency_std_dev,
			&latency_min, &latency_max,
			&latency_med25, &latency_med50, &latency_med75,
			&latency_total);

	/* Print latency summary results */
	printf("\tRPC Latencies from master to all endpoints (us):\n"
	       "\t\tMin    : %6ld\n"
	       "\t\t25th  %%: %6ld\n"
	       "\t\tMedian : %6ld\n"
	       "\t\t75th  %%: %6ld\n"
	       "\t\tMax    : %6ld\n"
	       "\t\tAverage: %6ld\n"
	       "\t\tStd Dev: %8.2f\n",
	       latency_min / 1000,
	       latency_med25 / 1000,
	       latency_med50 / 1000,
	       latency_med75 / 1000,
	       latency_max / 1000,
	       latency_avg / 1000, latency_std_dev / 1000);

	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-min", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_min / 1000);
	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-med25", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_med25 / 1000);

	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-med50", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_med50 / 1000);
	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-med75", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_med75 / 1000);

	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-max", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_max / 1000);
	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-av", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_avg / 1000);

	snprintf(new_key_name, sizeof(new_key_name), "%s-@:*-sd", master);
	ConfigAddInt(Ocfg, section_name, new_key_name, latency_std_dev / 1000);

	/* Print error summary results */
	printf("\tRPC Failures: %u\n", num_failed);
	if (num_failed > 0)
		print_fail_counts(&latencies[0], num_failed, "\t\t");

	/*
	 * Sort the latencies by: (in descending order of precedence)
	 * - rank
	 * - tag
	 * - val
	 * - cci_rc
	 *
	 * Note that errors have a val = -1, so they get grouped together
	 */
	qsort(latencies, test_params->rep_count,
	      sizeof(latencies[0]), st_compare_latencies_by_ranks);

#ifdef PRINT_ALL
	printf("\n NEW Sorted value\n");
	for (local_rep = num_failed; local_rep < test_params->rep_count;
	     local_rep++){
		printf("   Iteration: %d, EP %d, latencies %ld\n",
		       local_rep,  latencies[local_rep].rank,
		       latencies[local_rep].val);
	}
#endif

	/* Iterate over each rank / tag pair */
	local_rep = 0;
	do {
		uint32_t rank = latencies[local_rep].rank;
		uint32_t tag = latencies[local_rep].tag;
		uint32_t num_used;
		uint32_t count;
		uint32_t begin;

		/* Compute start, last, and num_failed for this rank/tag */
		num_failed = 0;
		count = 0;
		begin = local_rep;
		while (1) {
			if (latencies[local_rep].rank != rank ||
			    latencies[local_rep].tag != tag)
				break;

			count++;
			local_rep++;
			if (local_rep >= test_params->rep_count)
				break;
		}
		D_ASSERT(count > 0);
		/* Find stats for this endpoint */
		num_used = calculate_stats(&latencies[begin],
					   count,
					   &latency_avg, &latency_std_dev,
					   &latency_min, &latency_max,
					   &latency_med25, &latency_med50,
					   &latency_med75, &latency_total);
		D_ASSERT(num_used > 0);

		num_failed = count - num_used;
		if (num_failed > 0) {
			printf("\n");
			printf("\t\t\tFailures: %u\n", num_failed);
			print_fail_counts(&latencies[begin], num_failed,
					  "\t\t\t");
		}

		/*
		 * Throughput and bandwidth calculations.
		 * scale = 1000000000;
		 * total_time = (num_used * average);
		 * tp = (num_used / ( total_time/ scale)
		 *    = (num_used / (( num_used * average) / scale))
		 *    = scale / average;
		 * bw = tp * size_per_request
		 * WARNING: this evaluation of tp was a factor of 10
		 *	  to low.  It has been increased via scaling
		 *	  factor.  Need to understand why.
		 */
		throughput = 10000000000.0F / latency_avg;
		bandwidth = throughput * size_per_request /
			    (1024.0 * 1024.0);

		snprintf(new_key_name, sizeof(new_key_name), "%s-%d:%d-",
			 master, latencies[begin].rank,
			 latencies[begin].tag);

		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "tp");
		ConfigAddInt(Ocfg, section_name, kname, throughput);
		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "bw");
		ConfigAddInt(Ocfg, section_name, kname, bandwidth);

		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "min");
		ConfigAddInt(Ocfg, section_name, kname, latency_min / 1000);
		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "med25");
		ConfigAddInt(Ocfg, section_name, kname, latency_med25 / 1000);

		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "med50");
		ConfigAddInt(Ocfg, section_name, kname, latency_med50 / 1000);
		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "med75");
		ConfigAddInt(Ocfg, section_name, kname, latency_med75 / 1000);

		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "max");
		ConfigAddInt(Ocfg, section_name, kname, latency_max / 1000);
		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "av");
		ConfigAddInt(Ocfg, section_name, kname, latency_avg / 1000);

		snprintf(kname, sizeof(kname), "%s%s", new_key_name, "sd");
		ConfigAddInt(Ocfg, section_name, kname, latency_std_dev / 1000);
	} while (local_rep < test_params->rep_count);
}

static char *
config_section_name_add(char *section_name, char *name_to_add)
{
	int	 len;
	char	*section_name_new;

	len = strlen(section_name) + strlen(name_to_add) + 2;
	section_name_new = malloc(len);
	if (section_name_new != NULL) {
		/* avoid checkpatch warning */
		snprintf(section_name_new, len, "%s_%s",
			 section_name, name_to_add);
	}
	return section_name_new;
}

static int
config_create_section(Config *cfg, char *section_name, bool remove)
{
	int		ret = 0;
	ConfigRet	config_ret;

	if (section_name == NULL)
		return ret;

	if (remove && ConfigHasSection(cfg, section_name)) {
		/* avoid checkpatch warning */
		ConfigRemoveSection(cfg, section_name);
	}

	if (!ConfigHasSection(cfg, section_name)) {
		config_ret = ConfigAddSection(cfg, section_name);
		if (config_ret != CONFIG_OK)
			ret = ENOENT;
	}

	return ret;
}

static int
config_create_output_config(char *section_name, bool remove)
{
	int		 ret_value = 0;
	ConfigRet	 config_ret;
	char		*new_section_name = NULL;

	/*
	 * Read result file if specified and exists.
	 * Not requirement that file exist so don't exit if cannot be read.
	 * May be called more than once so verify that gbl.cfg_output == null
	 * Else, open new configuration to use.
	 */
	if ((gbl.g_expected_outfile != NULL) && (gbl.cfg_output == NULL)) {
		config_ret = ConfigReadFile(gbl.g_expected_outfile,
					    &gbl.cfg_output);
		if (config_ret != CONFIG_OK) {
			D_WARN("Output file does not exist: %s\n",
			       gbl.g_expected_outfile);
		}
	}
	/* Output config does not exist or couldn't be open, create one */
	if (gbl.cfg_output == NULL) {
		gbl.cfg_output = ConfigNew();
		if (gbl.cfg_output == NULL) {
			/* avoid checkpatch warning */
			D_GOTO(cleanup, ret_value = -ENOMEM);
		}
	}

	/*
	 * Create section if name is specified.
	 * If section already exist then remove it.
	 */
	new_section_name = config_section_name_create(section_name, NULL);
	if (new_section_name == NULL) {
		/* avoid checkpatch warning */
		D_GOTO(cleanup, ret_value = -ENOMEM);
	}
	ret_value = config_create_section(gbl.cfg_output, new_section_name,
					  remove);

	/* Create section for raw data */
	if (gbl.g_raw_data != 0) {
		char	*section_name_raw;

		section_name_raw = config_section_name_add(new_section_name,
							   RAW_DATA_EXTENSION);
		config_create_section(gbl.cfg_output, section_name_raw,
				      remove);
		free(section_name_raw);
	}

cleanup:
	if (new_section_name != NULL)
		free(new_section_name);
	return ret_value;
}

/*
 * From section in configuration, find closest value that matches the key.
 * If nothing matches, then return the default value.
 */
static int
get_config_value(Config *cfg, char *sec_name, char *key,
		 int *ret_value, int default_value)
{
	ConfigRet	 config_ret;
	int		 value = default_value;
	int		 ret = 0;
	int		 ivalue;
	char		*working;
	char		*tkey;
	char		*key_names[MAX_NUMBER_KEYS];
	char		*key_name;
	int		 number_keys;
	int		 total_number_keys;
	int		 max_keys = MAX_NUMBER_KEYS;
	char		*c_master = NULL;
	char		*c_endpoint = NULL;
	char		*c_remaining = NULL;
	char		*c_tag = NULL;
	char		*end_ptr;
	int		 i;
	int		 status_size = sizeof(status) /
				       sizeof(struct status_feature);
	int		 master = -1;
	int		 endpoint = -1;
	int		 endpoint_tag = -1;
	int		 master_tag = -1;
	int		 itemp;
	int		 itemp2;
	char		 id[MASTER_VALUE_SIZE];
	int		 ini_value;

	/* Make sure config is valid */
	if (cfg == NULL) {
		/* avoid checkpatch warning */
		D_GOTO(exit_code_ret, value);
	}

	/* See if there is an exact match, if so,then return value */
	config_ret = ConfigReadInt(cfg, sec_name, key, &ivalue, -1);
	if ((config_ret == CONFIG_OK) && (ivalue != -1)) {
		/* avoid checkpatch warning */
		D_GOTO(exit_code_ret, value = ivalue);
	}

	/* Determine which stats we are looking at */
	for (i = 0; i < status_size; i++) {
		tkey = strstr(key, status[i].name);
		if (tkey != NULL) {
			/* avoid checkpatch warning */
			break;
		}
	}
	if (i >= status_size) {
		/* avoid checkpatch warning */
		D_GOTO(exit_code, ret = -DER_NONEXIST);
	}
	key_name = status[i].name;

	/* Allocate working area for key.  strtok modifies working string */
	D_STRNDUP(working, key, (size_t)strlen(key));

	/*
	 * Parse string to find master and its tag.
	 * Strip off master M:T and then the Master
	 * Tag not necessary specified, or as *
	 */
	c_tag = NULL;
	c_master = strtok_r(working, "-", &c_endpoint);
	c_master = strtok_r(c_master, ":", &c_tag);

	if ((c_master != NULL) && isdigit(*c_master)) {
		/* avoid checkpatch warning */
		master = strtol(c_master, &end_ptr, 10);
		if (end_ptr == c_master) {
			/* Error reading in master value */
			D_GOTO(exit_code_free, value);
		}
	}
	if ((c_tag != NULL) && isdigit(*c_tag)) {
		/* avoid checkpatch warning */
		master_tag = strtol(c_tag, &end_ptr, 10);
		if (end_ptr == c_tag) {
			/* Error reading in tag value */
			D_GOTO(exit_code_free, value);
		}
	}

	/*
	 * Parse string to find endpoint and its tag.
	 * Strip off endpoit EP:T and then the tag.
	 * Not neccsary to specified endpoint.
	 * Tag not necessary specified, or as *
	 */
	c_endpoint = strtok_r(c_endpoint, "-", &c_remaining);
	c_endpoint = strtok_r(c_endpoint, ":", &c_tag);
	if ((c_endpoint != NULL) && isdigit(*c_endpoint)) {
		/* avoid check patch warning */
		endpoint = strtol(c_endpoint, &end_ptr, 10);
		if (end_ptr == c_endpoint) {
			/* Error reading in endpoint */
			D_GOTO(exit_code_free, value);
		}
	}
	if ((c_tag != NULL) && isdigit(*c_tag)) {
		endpoint_tag = strtol(c_tag, &end_ptr, 10);
		if (end_ptr == c_tag) {
			/* Error reading in tag */
			D_GOTO(exit_code_free, value);
		}
	}

	D_DEBUG(DB_TEST, "Key to Match: %d:%d - %d:%d\n",
		master, master_tag, endpoint, endpoint_tag);
	/*
	 * Priority search: match master and endpoint exactly.
	 * Search through all keys searching for a match with
	 * master and endpoint.  First make sure this is a valid
	 * search
	 */
	if ((master == -1) || (endpoint == -1))
		D_GOTO(code_search_master, value);
	number_keys = 0;
	number_keys = ConfigGetKeys(cfg, sec_name,
				    key_names, max_keys, number_keys);
	total_number_keys = number_keys;
	/* Loop through all keys */
	while (number_keys != 0) {
		for (i = 0; i < number_keys; i++) {
			/* Make sure key is for this stats.*/
			if (strstr(key_names[i], key_name) == NULL) {
				/* avoid check patch warning */
				continue;
			}

			strncpy(id, key_names[i], sizeof(id) - 1);
			id[sizeof(id) - 1] = '\0';
			config_ret = ConfigReadInt(cfg, sec_name,
						   key_names[i],
						   &ini_value, 0.0);

			/* search master key and compare */
			itemp = -1;
			c_master = strtok_r(id, "-", &c_endpoint);
			c_master = strtok_r(c_master, ":", &c_tag);
			if ((c_master != NULL) && isdigit(*c_master)) {
				itemp = strtol(c_master, &end_ptr, 10);
				if (end_ptr == c_master)
					D_GOTO(exit_code_free, value);
			}
			/* Match with master, check endpoint */
			if (itemp == master) {
				/* search endpoint key and compare */
				itemp2 = -1;
				c_endpoint = strtok_r(c_endpoint, "-",
						      &c_remaining);
				c_endpoint = strtok_r(c_endpoint, ":", &c_tag);
				if ((c_endpoint != NULL) &&
				    isdigit(*c_endpoint)) {
					itemp2 = strtol(c_endpoint, &end_ptr,
							10);
					if (end_ptr == c_endpoint)
						D_GOTO(exit_code_free, value);
				}
				if (itemp2 == endpoint) {
					/* Have a match */
					D_GOTO(exit_code_free,
					       value = ini_value);
				} /* End of endpoint match */
			} /* end of master match */
		} /* End of for loop */

		/* setup array of keys for next loop */
		number_keys = ConfigGetKeys(cfg, sec_name,
					    key_names, max_keys,
					    total_number_keys);
		total_number_keys += number_keys;
	}

code_search_master:
	/*
	 * Priority search: match master and wild card endpoint.
	 * Search through all keys searching for a match with master.
	 * Assumes that the endpoint not specified.
	 */
	if (master == -1)
		D_GOTO(code_search_endpoint, value);
	number_keys = 0;
	number_keys = ConfigGetKeys(cfg, sec_name,
				    key_names, max_keys, number_keys);
	total_number_keys = number_keys;
	/* Loop through all keys */
	while (number_keys != 0) {
		for (i = 0; i < number_keys; i++) {
			/* Make sure key is for this stats.*/
			if (strstr(key_names[i], key_name) == NULL) {
				/* avoid checkpatch warning */
				continue;
			}

			config_ret = ConfigReadInt(cfg, sec_name,
						   key_names[i],
						   &ini_value, 0.0);
			strncpy(id, key_names[i], sizeof(id) - 1);
			id[sizeof(id) - 1] = '\0';

			/* search master key and compare */
			c_master = strtok_r(id, "-", &c_endpoint);
			c_master = strtok_r(c_master, ":", &c_tag);
			itemp = -1;
			if ((c_master != NULL) && isdigit(*c_master)) {
				itemp = strtol(c_master, NULL, 10);
				if (end_ptr == c_master)
					D_GOTO(exit_code_free, value);
			}
			if (itemp == master) {
				/* endpoint must be a '*' */
				c_endpoint = strtok_r(c_endpoint, "-",
						      &c_remaining);
				c_endpoint = strtok_r(c_endpoint, ":", &c_tag);
				if ((c_endpoint != NULL) &&
				    strcmp(c_endpoint, "*") == 0) {
					/* Have a match, read value and exit */
					D_GOTO(exit_code_free,
					       value = ini_value);
				}
			} /* End of master compare */
		} /* End of for loop */

		/* setup array of keys for next loop */
		number_keys = ConfigGetKeys(cfg, sec_name,
					    key_names, max_keys,
					    total_number_keys);
		total_number_keys += number_keys;
	}

code_search_endpoint:
	/*
	 * Priority search: wild card master and match endpoint.
	 * Search through all keys searching for a match with endpoint.
	 * Assumes that the master specified with '*'.
	 */
	if (endpoint == -1)
		D_GOTO(exit_code_free, value);
	number_keys = 0;
	number_keys = ConfigGetKeys(cfg, sec_name,
				    key_names, max_keys, number_keys);
	total_number_keys = number_keys;
	/* Loop through all keys */
	while (number_keys != 0) {
		for (i = 0; i < number_keys; i++) {
			/* Make sure key is for this stats.*/
			if (strstr(key_names[i], key_name) == NULL) {
				/* avoid checkpatch warning */
				continue;
			}

			config_ret = ConfigReadInt(cfg, sec_name,
						   key_names[i],
						   &ini_value, 0.0);
			strncpy(id, key_names[i], sizeof(id) - 1);
			id[sizeof(id) - 1] = '\0';

			/* search master key and compare */
			c_master = strtok_r(id, "-", &c_endpoint);
			c_master = strtok_r(c_master, ":", &c_tag);
			if ((c_master != NULL) && strcmp(c_master, "*") == 0) {
				itemp = -1;
				/* endpoint must be a value */
				c_endpoint = strtok_r(c_endpoint, "-",
						      &c_remaining);
				c_endpoint = strtok_r(c_endpoint, ":", &c_tag);

				if ((c_endpoint != NULL) &&
				    isdigit(*c_endpoint)) {
					itemp = strtol(c_endpoint, &end_ptr,
						       10);
					if (end_ptr == c_endpoint)
						D_GOTO(exit_code_free, value);
				}

				/* Have a match, read value and exit */
				if (endpoint == itemp) {
					D_GOTO(exit_code_free,
					       value = ini_value);
				}
			} /* End of master compare */
		} /* End of for loop */

		/* setup array of keys for next loop */
		number_keys = ConfigGetKeys(cfg, sec_name,
					    key_names, max_keys,
					    total_number_keys);
		total_number_keys += number_keys;
	}

exit_code_free:
	free(working);
exit_code_ret:
	*ret_value = value;
exit_code:
	return ret;
}

static int
compare_print_results(char *section_name, char *input_section_name,
		      char *result_section_name)
{
	Config		*cfg_expected = NULL;
	ConfigRet	 config_ret;
	char		*sec_name;
	int		 ret_value = 0;
	int		 i;
	int		 status_size = sizeof(status) /
				       sizeof(struct status_feature);

	/*
	 * input_section_name  - user supplied section name (if specified).
	 * section_name        - created or user supplied section name.
	 * result_section_name - section name with "result" extension.
	 *			 section for stored results
	 */

	D_INFO(" Section name: %s\n", section_name);
	D_INFO(" Input Section name: %s\n", input_section_name);
	D_INFO(" Result Section name: %s\n", result_section_name);

	/* Read in expected file if specified */
	if (gbl.g_expected_infile != NULL) {
		D_INFO(" Expected File: %s\n", gbl.g_expected_infile);
		config_ret = ConfigReadFile(gbl.g_expected_infile,
					    &cfg_expected);
		if (config_ret != CONFIG_OK) {
			printf("Cannot open expected file: %s\n",
			       gbl.g_expected_infile);
			D_ERROR("Cannot open expected file: %s\n",
				gbl.g_expected_infile);
			D_GOTO(cleanup, ret_value = -ENOENT);
		}
	} else {
		/* avoid checkpatch warning */
		D_INFO(" No Expected File Specified\n");
	}

	/* Read in default stat value in default sector if defined. */
	sec_name = DEFAULT_VALUE_NAME;
	if (ConfigHasSection(cfg_expected, DEFAULT_VALUE_NAME)) {
		int ivalue;

		D_INFO(" File %s has default sector: %s\n",
		       gbl.g_expected_infile, DEFAULT_VALUE_NAME);
		for (i = 0; i < status_size; i++) {
			config_ret = ConfigReadInt(cfg_expected, sec_name,
						   status[i].name,
						   &ivalue, 0.0);
			if (config_ret == CONFIG_OK) {
				/* avoid checkpatch warning */
				status[i].value = ivalue;
			}
		}
	} else {
		/* avoid checkpatch warning */
		D_INFO(" File %s does Not have default sector: %s\n",
		       gbl.g_expected_infile, DEFAULT_VALUE_NAME);
	}

	/*
	 * Read in specified stat values for requested sector.
	 * Over rides default settings if specified.
	 */
	if (ConfigHasSection(cfg_expected, input_section_name)) {
		int ivalue;

		for (i = 0; i < status_size; i++) {
			config_ret = ConfigReadInt(cfg_expected,
						   input_section_name,
						   status[i].name,
						   &ivalue, 0.0);
			if (config_ret == CONFIG_OK) {
				/* avoid checkpatch warning */
				status[i].value = ivalue;
			}
		}
	}

	/*
	 * -----------------------
	 * Read in scaling factors
	 *   Order of precedent: least to higher
	 *     expected-threshold parameter - applied to all thresholds
	 *     all    - threshold sector    - apply to all thresholds
	 *     <stat> - threshold sector    - apply to all <stat> thresholds
	 *     expected-results parameter   - apply to listed <stat> with =
	 * -----------------------
	 */
	/* Use global scaling factor if specified */
	if (gbl.g_scale_factor != INVALID_SCALING) {
		/* avoid checkpatch warning */
		for (i = 0; i < status_size; i++) {
			/* avoid checkpatch warning */
			status[i].scale = gbl.g_scale_factor;
		}
	}

	/* Check if scaling factor defined in default sector */
	sec_name = DEFAULT_SCALE_NAME;
	if (ConfigHasSection(cfg_expected, sec_name)) {
		float scale = 0.0;

		/* First, see if set all to one global value */
		config_ret = ConfigReadFloat(cfg_expected, sec_name,
					     "all", &scale, 0.0);
		if (config_ret == CONFIG_OK) {
			for (i = 0; i < status_size; i++) {
				/* avoid checkpatch warning */
				status[i].scale = scale;
			}
		}

		/* Now see if individual scaling factors are set in file*/
		for (i = 0; i < status_size; i++) {
			config_ret = ConfigReadFloat(cfg_expected, sec_name,
						     status[i].name,
						     &scale, 0.0);
			if (config_ret == CONFIG_OK) {
				/* avoid checkpatch warning */
				status[i].scale = scale;
			}
		}
	}

	/*
	 * Read in specific scaling factor for specified sector.
	 * If this is non zero, then only the keys listed here are printed.
	 * If a "=value" is part of the string (i.e. av=12), then
	 * that value is used as the scaling factor.
	 * Note: g_expected_results may be pointing to an argv parameter
	 * which are constant.  Therefore, allocate a region for string
	 * manipulation. (strtok modifies string it is working on).
	 */
	if (gbl.g_expected_results != NULL) {
		char	*str;
		char	*save_str;
		char	*save_str_outer;
		char	*ptr;
		char	*tptr;
		float	 scale;
		int	 num;
		int	 j;

		/* alloc temporary string storage and copy to it */
		str = (char *)malloc(strlen(gbl.g_expected_results));
		if (str == NULL) {
			D_ERROR(" Memory Not allocated\n");
			D_GOTO(cleanup, ret_value = -ENOENT);
		}
		strcpy(str, gbl.g_expected_results);
		tptr = str;

		/* first, mark status not to print */
		for (i = 0; i < status_size; i++) {
			/* avoid checkpatch warning */
			status[i].flag &= ~(TST_OUTPUT);
		}

		/*
		 * Parse string for first token.
		 * ptr will point to something like "av" or "av=10"
		 * Then loop around for all entries.
		 */
		ptr = strtok_r(tptr, ",", &save_str_outer);
		while (ptr != NULL) {
			/*
			 * Note: This will eliminate "=:" from prt.
			 * Also, save_str will point to the next character
			 *   in the string (i.e. the scale value),
			 */
			strtok_r(ptr, "=:", &save_str);

			/* Search results held in status structure */
			for (j = 0; j < status_size; j++) {
				/* avoid checkpatch warning */
				if (strcmp(ptr, status[j].name) == 0) {
					/* avoid checkpatch warning */
					break;
				}
			}

			/* make sure we had a valid input */
			if (j >= status_size) {
				D_WARN("Illegal Input\n");
				D_GOTO(next_arg, ret_value = -EINVAL);
			}

			status[j].flag |= TST_OUTPUT;

			/* read scale factor */
			if (save_str != NULL) {
				num = sscanf(save_str, "%f", &scale);
				if (num == 1) {
					/* avoid checkpatch warning */
					status[j].scale = scale;
				}
			}
next_arg:
			ptr = strtok_r(NULL, ",", &save_str_outer);
		}
		if (str != NULL)
			free(str);
	}

	/*
	 * Do comparisons
	 * Everything above this was setting the default expected
	 * and scaling factors.  If specific factors are
	 * specified (i.e. M:t-EP:t=...) then they must be handle
	 * during the comparison.  They are specific to that one/group
	 * of comparison(s) and cannot be applied as a general default
	 * value.
	 */
#define	RANGE_SIZE	128
	for (i = 0; i < status_size; i++) {
		float	 upper = status[i].value;
		float	 lower = status[i].value;
		float	 percent_diff;
		float	 value = 0.0;
		float	 scale;
		int	 ivalue;
		int	 ret;

		bool	 passed;
		bool	 firstpass;
		char	*key_names[MAX_NUMBER_KEYS];
		int	 number_keys;
		int	 total_number_keys;
		int	 max = MAX_NUMBER_KEYS;
		char	*key;
		char	*tkey;
		int	 j;
		char	 range[RANGE_SIZE];
		char	 compar_result[2 * RANGE_SIZE];
		char	*results;
		Config	*Ecfg = cfg_expected;

		/* See if we are to output this stats */
		if (status[i].flag & TST_OUTPUT) {
			/* Set ranges for comparison */
			firstpass = true;

			/* Get number of keys in section */
			number_keys = 0;
			number_keys = ConfigGetKeys(gbl.cfg_output,
						    section_name,
						    key_names, max,
						    number_keys);
			total_number_keys = number_keys;
			if (number_keys == 0) {
				D_INFO(" NO keys found\n");
				break;
			}

			/*
			 * Results were store in the output file.
			 * Loop through all results/keys to compare.
			 */
			j = 0;
			while (number_keys != 0) {
				tkey = strstr(key_names[j],
					      status[i].name);
				if (tkey == NULL) {
					/* avoid checkpatch warning */
					D_GOTO(increment_code, ret_value);
				}

				ConfigReadInt(gbl.cfg_output, section_name,
					      key_names[j], &ivalue, 0);
				value = (float)ivalue;
				key = key_names[j];

				/* Find scale and value for key */
				sec_name = DEFAULT_SCALE_NAME;
				ivalue = (int)status[i].scale;
				ret = get_config_value(Ecfg, sec_name, key,
						       &ivalue, ivalue);
				if (ret < 0) {
					/* avoid checkpatch warning */
					D_GOTO(cleanup, ret_value = ret);
				}
				scale = (float)ivalue;

				sec_name = input_section_name;
				ivalue = (int)status[i].value;
				ret = get_config_value(Ecfg, sec_name, key,
						       &ivalue, ivalue);
				if (ret < 0) {
					/* avoid checkpatch warning */
					D_GOTO(cleanup, ret_value = ret);
				}

				/* Find range for testing */
				percent_diff = 0.;
				if (ivalue != 0)
					percent_diff = (value - ivalue) /
							ivalue;
				percent_diff *= 100.0;
				upper = ivalue;
				lower = ivalue;

				if (status[i].flag & TST_LOW) {
					/* avoid checkpatch warning */
					lower = ivalue * (1.0 - scale / 100.);
				}
				if (status[i].flag & TST_HIGH) {
					/* avoid checkpatch warning */
					upper = ivalue * (1.0 + scale / 100.);
				}

				/* Test for range */
				passed = true;
				results = "Passed:";
				if (Ecfg == NULL) {
					snprintf(range, RANGE_SIZE, " ");
					results = " ";
				} else if ((status[i].flag &
					    (TST_HIGH | TST_LOW)) ==
					    (TST_HIGH | TST_LOW)) {
					snprintf(range, RANGE_SIZE,
						 " Range (%6d -- %6d)"
						 " %3d%% %5.1f%%",
						 (int)lower, (int)upper,
						 (int)scale, percent_diff);
					if (!((lower <= value) &&
					      (value <= upper))) {
						passed = false;
						results = "Failed:";
						/* ret_value = -1; */
					}
				} else if (status[i].flag & TST_HIGH) {
					snprintf(range, RANGE_SIZE,
						 " Range (..    -- %6d)"
						 "  %3d%% %5.1f%%",
						 (int)upper, (int)scale,
						 percent_diff);
					if (value >= upper) {
						passed = false;
						results = "Failed:";
						/* ret_value = -1; */
					}
				} else if (status[i].flag & TST_LOW) {
					snprintf(range, RANGE_SIZE,
						 " Range (%6d --   "
						 "  ..)   %3d%%"
						 " %5.1f%%",
						 (int)lower, (int)scale,
						 percent_diff);
					if (value <= lower) {
						passed = false;
						results = "Failed:";
						/* ret_value = -1; */
					}
				}

				/* print results */
				if (firstpass)
					printf("\n Endpoint Result (%s)\n",
					       status[i].description);
				firstpass = false;
				printf("   %s : %8d  %s %s\n",
				       key, (int)value, results, range);
				snprintf(compar_result, sizeof(compar_result),
					 "%s %8d %s",
					 results, (int)value, range);

				/* Log error message */
				if (!passed) {
					/* avoid checkpatch warning */
					D_INFO("%s %s range check\n",
					       key, results);
				}

				/* Place results into result section */
				ConfigAddString(gbl.cfg_output,
						result_section_name,
						key, compar_result);
increment_code:
				/* loop control:*/
				j++;
				if (j == number_keys) {
					char	*sn = section_name;
					int	 tnk = total_number_keys;
					Config	*cout = gbl.cfg_output;

					j = 0;
					number_keys = ConfigGetKeys(cout,
								    sn,
								    key_names,
								    max, tnk);
					total_number_keys += number_keys;
				}
			}
		} /* end of valid test loop */
	} /* end of "for" loop */
cleanup:
	if (cfg_expected != NULL)
		ConfigFree(cfg_expected);
	return ret_value;
}

/* Place output results into output file, tagging with master key */
static int
combine_results(Config *cfg_results, char *section_name)
{
	ConfigRet	 ret;
	int		 ret_value = 0;
	int		 temp;
	int		 temp2;
	int		 i;
	int		 dfault = -1;
	const char	*key_name;
	size_t		 size = MASTER_VALUE_SIZE;
	char		 new_key_name[2 * MASTER_VALUE_SIZE];
	char		 master[MASTER_VALUE_SIZE];
	int		 status_size = sizeof(status) /
				       sizeof(struct status_feature);

	/* Read master-endpoint string */
	ret = ConfigReadString(cfg_results, section_name, "master_endpoint",
			       master, size, "ME");
	/*
	 * Copy results into output.
	 * If the output already has it, then remove it.
	 */
	for (i = 0; i < status_size; i++) {
		key_name = status[i].name;
		ret = ConfigReadInt(cfg_results, section_name, key_name,
				    &temp, dfault);
		/* Tag master endpoint to key name */
		snprintf(new_key_name, sizeof(new_key_name),
			 "%s-%s", master, key_name);

		/* Check to see if key occurres in results */
		if (ret == CONFIG_OK) {
			/* Check to see if key already exist in output */
			ret = ConfigReadInt(gbl.cfg_output, section_name,
					    key_name, &temp2, dfault);
			if (ret == CONFIG_OK) {
				ConfigRemoveKey(gbl.cfg_output, section_name,
						key_name);
			}
			ConfigAddInt(gbl.cfg_output, section_name,
				     new_key_name, temp);
		}
	}

#ifdef PRINT_IMMEDIATE_RESULTS
#define PRINT_IMMEDIATE_RESULTS
	printf(" Print input results\n");
	ConfigPrintSection(cfg_results, stdout, section_name);

	printf("\n Output file results\n");
	ConfigPrintSection(gbl.cfg_output, stdout, section_name);
#endif

	/* print out results if file specified */
	if (gbl.g_expected_outfile != NULL) {
		/* avoid checkpatch warning */
		ret = ConfigPrintToFile(gbl.cfg_output,
					gbl.g_expected_outfile);
		if (ret != CONFIG_OK) {
			D_ERROR("Fail to write to output file: %s\n",
				gbl.g_expected_outfile);
		ret_value = -ENOENT;
		}
	}
	return ret_value;
}

static int
file_name_create(char **gpath_name, bool *name_allocated, char *env)
{
	char		*new_name = NULL;
	char		*env_name;
	char		*file_name;
	char		*dup_name;
	char		*temp_name = NULL;
	int		 t_length;
	int		 ret;
	int		 ret_value = 0;
	char		*path_name = *gpath_name;

	/* get env to append, if not defined, then leave name as it is */
	env_name = getenv(env);

	D_EMIT(" Path name: %s\n", path_name);
	D_EMIT(" env:       %s\n", env_name);

	if (env_name == NULL) {
		D_INFO(" Environment %s not set\n", env);
		D_GOTO(cleanup, ret_value);
	}

	/* If no name given, then just return */
	if (path_name == NULL) {
		D_INFO(" Path Name not set\n");
		D_GOTO(cleanup, ret_value);
	}

	/*
	 * Extract file name.
	 *  The path_name may be constant (argv) so we cannot modify it.
	 */
	dup_name = strdup(path_name);
	file_name = strrchr(dup_name, '/');
	if (file_name == NULL) {
		/* avoid ckeckpatch warning */
		file_name = dup_name;
		}
	file_name = strtok_r(file_name, "/", &temp_name);

	/* Create new path/file name */
	t_length = strlen(file_name) + strlen(env_name) + 2;
	new_name = (char *)malloc(t_length);
	ret = snprintf(new_name, t_length, "%s/%s",
		       env_name, file_name);
	if (ret != 0) {
		if (name_allocated) {
			/* avoid ckeckpatch warning */
			free(*gpath_name);
		}
		*gpath_name = new_name;
		*name_allocated = true;
	} else {
		D_WARN("Could not create Path/File_name: %s/%s\n",
		       env_name, file_name);
		free(new_name);
		ret_value = -1;
	}

	/* Free up space allocated. */
	 free(dup_name);
cleanup:
	D_EMIT(" New Path name: %s\n", *gpath_name);
	return ret_value;
}

static char *
config_section_name_create(char *section_name,
			   struct crt_st_start_params
			   *test_params)
{
	int	len = SELF_TEST_MAX_SECTION_NAME_SIZE;
	char	*name_str;
	int	ids;
	int	idr;

	/* Calculate length and allocate region */
	if (section_name != NULL) {
		/* avoid checkpatch warning */
		len = strlen(section_name) + 2;
	}
	if (gbl.g_config_append != NULL) {
		/* avoid checkpatch warning */
		len += strlen(gbl.g_config_append);
	}
	name_str = (char *)malloc(len + 1);
	if (name_str == NULL) {
		/* avoid checkpatch warning */
		D_GOTO(exit_code, name_str = NULL);
	}

	/*
	 * If section name was specified in calling sequnce, then use it.
	 * Otherwise, create a name based on test parameters.
	 */
	if (section_name != NULL) {
		if (gbl.g_config_append == NULL) {
			snprintf(name_str, len, "%s", section_name);
		} else {
			snprintf(name_str, len, "%s_%s", section_name,
				 gbl.g_config_append);
		}
	} else {
		/* Verify we have test parameters passed. */
		if (test_params == (struct crt_st_start_params *)NULL) {
			free(name_str);
			D_GOTO(exit_code, name_str = NULL);
		}

		/* Add alignment parameter */
		if (test_params->buf_alignment >= 10) {
			len = snprintf(name_str,
				       SELF_TEST_MAX_SECTION_NAME_SIZE,
				       "align_%dK_",
				       1 << (test_params->buf_alignment
					     - 10));
		} else {
			len = snprintf(name_str,
				       SELF_TEST_MAX_SECTION_NAME_SIZE,
				       "align_%dB_",
				       1 << test_params->buf_alignment);
		}

		/* Add inflight parameter */
		len += snprintf(&name_str[len],
				SELF_TEST_MAX_SECTION_NAME_SIZE - len,
				"inFlight_%d_", test_params->max_inflight);

		/* Add size parameter */
		if (test_params->send_size >= 0x00100000) {
			len += snprintf(&name_str[len],
					SELF_TEST_MAX_SECTION_NAME_SIZE - len,
					"size_%dM_",
					test_params->send_size >> 20);
		} else if (test_params->send_size > 0x00000400) {
			len += snprintf(&name_str[len],
					SELF_TEST_MAX_SECTION_NAME_SIZE - len,
					"size_%dK_",
					test_params->send_size >> 12);
		} else {
			len += snprintf(&name_str[len],
					SELF_TEST_MAX_SECTION_NAME_SIZE - len,
					"size_%dB_",
					test_params->send_size);
		}
		/* Add transfer send/receive type */
		ids = test_params->send_type;
		idr = test_params->reply_type;
		len += snprintf(&name_str[len],
				SELF_TEST_MAX_SECTION_NAME_SIZE - len,
				"%s%s_", transfer_type_map[ids].short_name,
				 transfer_type_map[idr].short_name);
	}
exit_code:
	return name_str;
}

static int
test_msg_size(crt_context_t crt_ctx,
	      struct st_master_endpt *ms_endpts,
	      uint32_t num_ms_endpts,
	      struct crt_st_start_params *test_params,
	      struct st_latency **latencies,
	      crt_bulk_t *latencies_bulk_hdl, int output_megabits,
	      char *input_section_name)
{
	int				 ret;
	int				 done;
	uint32_t			 failed_count;
	uint32_t			 complete_count;
	crt_rpc_t			*new_rpc;
	struct crt_st_start_params	*start_args;
	uint32_t			 m_idx;
	Config				*cfg = NULL;
	char				*section_name = NULL;
	char				*section_name_raw = NULL;
	char				*section_name_result = NULL;
	char				*str_send = NULL;
	char				*str_put = NULL;
	int				 ret_value = 0;

	/*
	 * Launch self-test 1:many sessions on each master endpoint
	 * as simultaneously as possible (don't wait for acknowledgment)
	 */
	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
		crt_endpoint_t *endpt = &ms_endpts[m_idx].endpt;

		/* Create and send a new RPC starting the test */
		ret = crt_req_create(crt_ctx, endpt, CRT_OPC_SELF_TEST_START,
				     &new_rpc);
		if (ret != 0) {
			D_ERROR("Creating start RPC failed to endpoint"
				" %u:%u; ret = %d\n", endpt->ep_rank,
				endpt->ep_tag, ret);
			ms_endpts[m_idx].test_failed = 1;
			ms_endpts[m_idx].test_completed = 1;
			continue;
		}

		start_args = (struct crt_st_start_params *)
			crt_req_get(new_rpc);
		D_ASSERTF(start_args != NULL,
			  "crt_req_get returned NULL\n");
		memcpy(start_args, test_params, sizeof(*test_params));
		start_args->srv_grp = test_params->srv_grp;

		/* Set the launch status to a known impossible value */
		ms_endpts[m_idx].reply.status = INT32_MAX;

		ret = crt_req_send(new_rpc, start_test_cb,
				   &ms_endpts[m_idx].reply.status);
		if (ret != 0) {
			D_ERROR("Failed to send start RPC to endpoint %u:%u; "
				"ret = %d\n", endpt->ep_rank, endpt->ep_tag,
				ret);
			ms_endpts[m_idx].test_failed = 1;
			ms_endpts[m_idx].test_completed = 1;
			continue;
		}
	}

	/*
	 * Wait for each node to report whether or not the test launched
	 * successfully
	 */
	do {
		/* Flag indicating all test launches have returned a status */
		done = 1;

		/* Wait a bit for tests to finish launching */
		sched_yield();

		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (ms_endpts[m_idx].reply.status == INT32_MAX) {
				/* No response yet... */
				done = 0;
				break;
			}
	} while (done != 1);

	/* Print a warning for any 1:many sessions that failed to launch */
	failed_count = 0;
	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
		if (ms_endpts[m_idx].reply.status != 0) {
			D_ERROR("Failed to launch self-test 1:many session on"
				" %u:%u; ret = %d\n",
				ms_endpts[m_idx].endpt.ep_rank,
				ms_endpts[m_idx].endpt.ep_tag,
				ms_endpts[m_idx].reply.status);
			ms_endpts[m_idx].test_failed = 1;
			ms_endpts[m_idx].test_completed = 1;
			failed_count++;
		} else if (ms_endpts[m_idx].test_failed != 0) {
			ms_endpts[m_idx].test_failed = 1;
			ms_endpts[m_idx].test_completed = 1;
			failed_count++;
		} else {
			ms_endpts[m_idx].test_failed = 0;
			ms_endpts[m_idx].test_completed = 0;
		}

	/* Check to make sure that at least one 1:many session was started */
	if (failed_count >= num_ms_endpts) {
		D_ERROR("Failed to launch any 1:many test sessions\n");
		return ms_endpts[0].reply.status;
	}

	/*
	 * Poll the master nodes until all tests complete
	 *   (either successfully or by returning an error)
	 */
	do {
		/* Wait a small amount of time for tests to progress */
		sleep(1);

		/* Send status requests to every non-finished node */
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
			/* Skip endpoints that have finished */
			if (ms_endpts[m_idx].test_completed != 0)
				continue;

			/* Set result status to impossible guard value */
			ms_endpts[m_idx].reply.status = INT32_MAX;

			/* Create a new RPC to check the status */
			ret = crt_req_create(crt_ctx, &ms_endpts[m_idx].endpt,
					     CRT_OPC_SELF_TEST_STATUS_REQ,
					     &new_rpc);
			if (ret != 0) {
				D_ERROR("Creating status request RPC to"
					" endpoint %u:%u; ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag,
					ret);
				ms_endpts[m_idx].test_failed = 1;
				ms_endpts[m_idx].test_completed = 1;
				continue;
			}

			/*
			 * Sent data is the bulk handle where results should
			 * be written
			 */
			*((crt_bulk_t *)crt_req_get(new_rpc)) =
				latencies_bulk_hdl[m_idx];

			/* Send the status request */
			ret = crt_req_send(new_rpc, status_req_cb,
					   &ms_endpts[m_idx].reply);
			if (ret != 0) {
				D_ERROR("Failed to send status RPC to endpoint"
					" %u:%u; ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag, ret);
				ms_endpts[m_idx].test_failed = 1;
				ms_endpts[m_idx].test_completed = 1;
				continue;
			}
		}

		/* Wait for all status request results to come back */
		do {
			/* Flag indicating all status requests have returned */
			done = 1;

			/* Wait a bit for status requests to be handled */
			sched_yield();

			for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
				if (ms_endpts[m_idx].reply.status ==
				    INT32_MAX &&
				    ms_endpts[m_idx].test_completed == 0) {
					/* No response yet... */
					done = 0;
					break;
				}
		} while (done != 1);

		complete_count = 0;
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
			/* Skip endpoints that have already finished */
			if (ms_endpts[m_idx].test_completed != 0) {
				complete_count++;
				continue;
			}

			switch (ms_endpts[m_idx].reply.status) {
			case CRT_ST_STATUS_TEST_IN_PROGRESS:
				D_DEBUG(DB_TEST, "Test still processing on "
					"%u:%u - # RPCs remaining: %u\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag,
					ms_endpts[m_idx].reply.num_remaining);
				break;
			case CRT_ST_STATUS_TEST_COMPLETE:
				ms_endpts[m_idx].test_completed = 1;
				break;
			default:
				D_ERROR("Detected test failure on %u:%u -"
					" ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag,
					ms_endpts[m_idx].reply.status);
				ms_endpts[m_idx].test_failed = 1;
				ms_endpts[m_idx].test_completed = 1;
				complete_count++;
			}
		}
	} while (complete_count < num_ms_endpts);

	/*
	 * Create section name and section in global output config
	 * Dont remove section if it already exist.
	 */
	section_name = config_section_name_create(input_section_name,
						  test_params);
	if (section_name == NULL) {
		D_ERROR("No memory allocated for sector name");
		D_GOTO(exit_code, ret_value = -ENOMEM);
	}
	config_create_section(gbl.cfg_output, section_name, false);

	/* Create section for raw data */
	if (gbl.g_raw_data != 0) {
		section_name_raw = config_section_name_add(section_name,
							   RAW_DATA_EXTENSION);
		if (section_name_raw != NULL) {
			config_create_section(gbl.cfg_output, section_name_raw,
					      false);
		}
	}

	/*
	 * Create section for storing comparison results.
	 * Remove previous results section.
	 */
	section_name_result = config_section_name_add(section_name,
						      RESULT_EXTENSION);
	if (section_name_result != NULL) {
		/* avoid checkpatch warning */
		config_create_section(gbl.cfg_output, section_name_result,
				      true);
	}

	/* Create temporary configuration structure to store results */
	cfg = ConfigNew();

	/*
	 * Print the results for this size.
	 * Compare results.
	 */
	MSG_TYPE_STR(str_send, test_params->send_type);
	MSG_TYPE_STR(str_put, test_params->reply_type);
	printf("##################################################\n");
	printf("Results for message size (%d-%s %d-%s)\n"
	       "     (max_inflight_rpcs = %d):\n\n",
	       test_params->send_size,
	       str_send,
	       test_params->reply_size,
	       str_put,
	       test_params->max_inflight);

	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
		int		print_count;
		size_t		size = MASTER_VALUE_SIZE;
		char		master_value[MASTER_VALUE_SIZE];

		/* Skip endpoints that failed */
		if (ms_endpts[m_idx].test_failed != 0)
			continue;

		/* Create section name and Master key */
		ConfigAddSection(cfg, section_name);
		snprintf(master_value, size, "%u:%u",
			 ms_endpts[m_idx].endpt.ep_rank,
			 ms_endpts[m_idx].endpt.ep_tag);
		ConfigAddString(cfg, section_name, "master_endpoint",
				master_value);

		/* Print a header for this endpoint and store number of chars */
		printf("Master Endpoint %u:%u%n\n",
		       ms_endpts[m_idx].endpt.ep_rank,
		       ms_endpts[m_idx].endpt.ep_tag,
		       &print_count);

		/* Print a nice line under the header of the right length */
		for (; print_count > 0; print_count--)
			printf("-");
		printf("\n");

		/* print results and add info to configuration section */
		print_results(latencies[m_idx], test_params,
			      ms_endpts[m_idx].reply.test_duration_ns,
			      output_megabits,
			      cfg, section_name, section_name_raw);

		/* Transfer results from working config to output config */
		combine_results(cfg, section_name);

		/* Cleanup configuration structure for next loop */
		ConfigRemoveSection(cfg, section_name);
	}

	/* compare and output results */
	ret_value = compare_print_results(section_name, input_section_name,
					  section_name_result);

	/* Free up temporary configuration structure and others */
	if (cfg != NULL) {
		/* avoid checkpatch warning */
		ConfigFree(cfg);
	}
	if (section_name != NULL) {
		/* avoid checkpatch warning */
		free(section_name);
	}
	if (section_name_raw != NULL) {
		/* avoid checkpatch warning */
		free(section_name_raw);
	}
	if (section_name_result != NULL) {
		/* avoid checkpatch warning */
		free(section_name_result);
	}
exit_code:
	return ret_value;
}

static void
randomize_endpts(struct st_endpoint *endpts, uint32_t num_endpts)
{
	struct st_endpoint	tmp;
	int			r_index;
	int			i;
	int			k;

	srand(time(NULL));

	printf("Randomizing order of endpoints\n");
	/* Shuffle endpoints few times */

	for (k = 0; k < 10; k++)
	for (i = 0; i < num_endpts; i++) {
		r_index = rand() % num_endpts;

		tmp = endpts[i];
		endpts[i] = endpts[r_index];
		endpts[r_index] = tmp;
	}

	printf("New order:\n");
	for (i = 0; i < num_endpts; i++) {
		/* Avoid checkpatch warning */
		printf("%d:%d ", endpts[i].rank, endpts[i].tag);
	}
	printf("\n");
}

static int
run_self_test(struct st_size_params all_params[],
	      int num_msg_sizes, int rep_count, int max_inflight,
	      char *dest_name, struct st_endpoint *ms_endpts_in,
	      uint32_t num_ms_endpts_in,
	      struct st_endpoint *endpts, uint32_t num_endpts,
	      int output_megabits, int16_t buf_alignment,
	      char *attach_info_path, char *section_name)
{
	crt_context_t		  crt_ctx;
	crt_group_t		 *srv_grp;
	pthread_t		  tid;

	int			  size_idx;
	uint32_t		  m_idx;
	int			  ret;
	int			  cleanup_ret;
	char			  *str_send = NULL;
	char			  *str_put = NULL;

	struct st_master_endpt	 *ms_endpts = NULL;
	uint32_t		  num_ms_endpts = 0;

	struct st_latency	**latencies = NULL;
	d_iov_t			 *latencies_iov = NULL;
	d_sg_list_t		 *latencies_sg_list = NULL;
	crt_bulk_t		 *latencies_bulk_hdl = CRT_BULK_NULL;
	bool			  listen = false;
	crt_endpoint_t		  self_endpt;

	/* Sanity checks that would indicate bugs */
	D_ASSERT(endpts != NULL && num_endpts > 0);
	D_ASSERT((ms_endpts_in == NULL && num_ms_endpts_in == 0) ||
		 (ms_endpts_in != NULL && num_ms_endpts_in > 0));

	/* will send TEST_START RPC to self, so listen for incoming requests */
	if (ms_endpts_in == NULL)
		listen = true;

	/* Initialize CART */
	ret = self_test_init(dest_name, &crt_ctx, &srv_grp, &tid,
			     attach_info_path, listen /* run as server */);
	if (ret != 0) {
		D_ERROR("self_test_init failed: grp_name %s, path %s, rc %d\n",
			dest_name, attach_info_path, ret);
		printf("self_test_init failed: grp_name %s,  path %s\n",
		       dest_name, attach_info_path);
		D_GOTO(cleanup_nothread, ret);
	}

	/* Get the group/rank/tag for this application (self_endpt) */
	ret = crt_group_rank(NULL, &self_endpt.ep_rank);
	if (ret != 0) {
		D_ERROR("crt_group_rank failed; ret = %d\n", ret);
		D_GOTO(cleanup, ret);
	}
	self_endpt.ep_grp = crt_group_lookup(CRT_SELF_TEST_GROUP_NAME);
	if (self_endpt.ep_grp == NULL) {
		D_ERROR("crt_group_lookup failed for group %s\n",
			CRT_SELF_TEST_GROUP_NAME);
		D_GOTO(cleanup, ret = -DER_NONEXIST);
	}
	self_endpt.ep_tag = 0;

	/*
	 * Allocate a new list of unique master endpoints, each with a
	 * crt_endpoint_t and additional metadata
	 */
	if (ms_endpts_in == NULL) {
		/*
		 * If no master endpoints were specified, allocate just one and
		 * set it to self_endpt
		 */
		num_ms_endpts = 1;
		D_ALLOC_PTR(ms_endpts);
		if (ms_endpts == NULL)
			D_GOTO(cleanup, ret = -ENOMEM);
		ms_endpts[0].endpt.ep_rank = self_endpt.ep_rank;
		ms_endpts[0].endpt.ep_tag = self_endpt.ep_tag;
		ms_endpts[0].endpt.ep_grp = self_endpt.ep_grp;
	} else {
		/*
		 * If master endpoints were specified, initially allocate enough
		 * space to hold all of them, but only unique master endpoints
		 * to the new list
		 */
		D_ALLOC_ARRAY(ms_endpts, num_ms_endpts_in);
		if (ms_endpts == NULL)
			D_GOTO(cleanup, ret = -ENOMEM);

		/*
		 * Sort the supplied endpoints to make it faster to identify
		 * duplicates
		 */
		qsort(ms_endpts_in, num_ms_endpts_in,
		      sizeof(ms_endpts_in[0]), st_compare_endpts);

		/* Add the first element to the new list */
		ms_endpts[0].endpt.ep_rank = ms_endpts_in[0].rank;
		ms_endpts[0].endpt.ep_tag = ms_endpts_in[0].tag;
		/*
		 * TODO: This isn't right - it should be self_endpt.ep_grp.
		 * However, this requires changes elsewhere - this is tracked
		 * by CART-187.
		 *
		 * As implemented here, rank 0 tag 0 in the client group will
		 * be used as the master endpoint by default
		 */
		ms_endpts[0].endpt.ep_grp = srv_grp;
		num_ms_endpts = 1;

		/*
		 * Add unique elements to the new list
		 */
		for (m_idx = 1; m_idx < num_ms_endpts_in; m_idx++)
			if ((ms_endpts_in[m_idx].rank !=
			     ms_endpts[num_ms_endpts - 1].endpt.ep_rank) ||
			    (ms_endpts_in[m_idx].tag !=
			     ms_endpts[num_ms_endpts - 1].endpt.ep_tag)) {
				ms_endpts[num_ms_endpts].endpt.ep_rank =
					ms_endpts_in[m_idx].rank;
				ms_endpts[num_ms_endpts].endpt.ep_tag =
					ms_endpts_in[m_idx].tag;
				ms_endpts[num_ms_endpts].endpt.ep_grp =
					srv_grp;
				num_ms_endpts++;
			}

		/*
		 * If the counts don't match up, some were duplicates - resize
		 * the resulting smaller array which contains only unique
		 * entries
		 */
		if (num_ms_endpts != num_ms_endpts_in) {
			struct st_master_endpt *realloc_ptr;

			D_REALLOC_ARRAY(realloc_ptr, ms_endpts,
					num_ms_endpts_in, num_ms_endpts);
			if (realloc_ptr == NULL)
				D_GOTO(cleanup, ret = -ENOMEM);
			ms_endpts = realloc_ptr;
		}
	}

	/* Allocate latency lists for each 1:many session */
	D_ALLOC_ARRAY(latencies, num_ms_endpts);
	if (latencies == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);
	D_ALLOC_ARRAY(latencies_iov, num_ms_endpts);
	if (latencies_iov == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);
	D_ALLOC_ARRAY(latencies_sg_list, num_ms_endpts);
	if (latencies_sg_list == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);
	D_ALLOC_ARRAY(latencies_bulk_hdl, num_ms_endpts);
	if (latencies_bulk_hdl == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);

	/*
	 * For each 1:many session, allocate an array for latency results.
	 * Map that array to an IOV, and create a bulk handle that will be used
	 * to transfer latency results back into that buffer
	 */
	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
		D_ALLOC_ARRAY(latencies[m_idx], rep_count);
		if (latencies[m_idx] == NULL)
			D_GOTO(cleanup, ret = -DER_NOMEM);
		d_iov_set(&latencies_iov[m_idx], latencies[m_idx],
			  rep_count * sizeof(**latencies));
		latencies_sg_list[m_idx].sg_iovs =
			&latencies_iov[m_idx];
		latencies_sg_list[m_idx].sg_nr = 1;

		ret = crt_bulk_create(crt_ctx, &latencies_sg_list[m_idx],
				      CRT_BULK_RW,
				      &latencies_bulk_hdl[m_idx]);
		if (ret != 0) {
			D_ERROR("Failed to allocate latencies bulk handle;"
				" ret = %d\n", ret);
			D_GOTO(cleanup, ret);
		}
		D_ASSERT(latencies_bulk_hdl != CRT_BULK_NULL);
	}

	if (gbl.g_randomize_endpoints) {
		/* Avoid checkpatch warning */
		randomize_endpts(endpts, num_endpts);
	}

	for (size_idx = 0; size_idx < num_msg_sizes; size_idx++) {
		struct crt_st_start_params	 test_params = { 0 };

		/* Set test parameters to send to the test node */
		d_iov_set(&test_params.endpts, endpts,
			  num_endpts * sizeof(*endpts));
		test_params.rep_count = rep_count;
		test_params.max_inflight = max_inflight;
		test_params.send_size = all_params[size_idx].send_size;
		test_params.reply_size = all_params[size_idx].reply_size;
		test_params.send_type = all_params[size_idx].send_type;
		test_params.reply_type = all_params[size_idx].reply_type;
		test_params.buf_alignment = buf_alignment;
		test_params.srv_grp = dest_name;

		ret = test_msg_size(crt_ctx, ms_endpts, num_ms_endpts,
				    &test_params, latencies, latencies_bulk_hdl,
				    output_megabits, section_name);

		if (ret != 0) {
			MSG_TYPE_STR(str_send, test_params.send_type);
			MSG_TYPE_STR(str_put, test_params.reply_type);
			D_ERROR("Testing message size (%d-%s %d-%s) failed;"
				" ret = %d\n",
				test_params.send_size,
				str_send,
				test_params.reply_size,
				str_put,
				ret);
			D_GOTO(cleanup, ret);
		}
	}

cleanup:
	/* Tell the progress thread to abort and exit */
	gbl.g_shutdown_flag = 1;

	cleanup_ret = pthread_join(tid, NULL);
	if (cleanup_ret)
		D_ERROR("Could not join progress thread");
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

cleanup_nothread:
	if (latencies_bulk_hdl != NULL) {
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (latencies_bulk_hdl[m_idx] != CRT_BULK_NULL)
				crt_bulk_free(latencies_bulk_hdl[m_idx]);
		D_FREE(latencies_bulk_hdl);
	}
	if (latencies_sg_list != NULL)
		D_FREE(latencies_sg_list);
	if (latencies_iov != NULL)
		D_FREE(latencies_iov);
	if (ms_endpts != NULL)
		D_FREE(ms_endpts);
	if (latencies != NULL) {
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (latencies[m_idx] != NULL)
				D_FREE(latencies[m_idx]);
		D_FREE(latencies);
	}

	if (srv_grp != NULL && g_group_inited) {
		cleanup_ret = crt_group_detach(srv_grp);
		if (cleanup_ret != 0)
			D_ERROR("crt_group_detach failed; ret = %d\n",
				cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

	cleanup_ret = crt_context_destroy(crt_ctx, 0);
	if (cleanup_ret != 0)
		D_ERROR("crt_context_destroy failed; ret = %d\n", cleanup_ret);
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

	cleanup_ret = crt_finalize();
	if (cleanup_ret != 0)
		D_ERROR("crt_finalize failed; ret = %d\n", cleanup_ret);
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);
	return ret;
}

static void
print_usage(const char *prog_name, const char *msg_sizes_str,
	    int rep_count,
	    int max_inflight)
{
	/* TODO --verbose */

	printf("\n*********************************\n"
	       " self_test does timing analysis for data transfer between\n"
	       " multiple endpoints.\n"
	       " Input parameters may be specified in either an input file\n"
	       " and/or command line options.  If both are used, then file\n"
	       " input is read first and command line options second,\n"
	       " allowing command line options to take precedent.\n\n");

	printf(" Timing results may be compared to expected results\n"
	       " with the results stored into an optional file.\n"
	       " The input, the expected and the result files use the\n"
	       " configini format, where the name of a sector (test name) is\n"
	       " enclosed in brackets follow by a series of key:value pairs.\n"
	       " For the input file, the keys are the long format of the\n"
	       " command line options follow by a separator and the key value\n"
	       " The separator may be either the : or the =.\n"
	       " There may be multiple sector/test-name in each file.\n"
	       " Self_test will operate on sector specified with the 'config'\n"
	       " option. The sector names MUST be consistent between all\n"
	       " files. If the sector name already exists in the result file,\n"
	       " then the previous results will be replaced.\n\n");

	printf(" The expected and results files uses predefined keys, each\n"
	       " representing a differ statistics.\n"
	       "    all  - apply to all statistics (scaling only)\n"
	       "     bw  - bandwidth\n"
	       "     tp  - through put\n"
	       "     av  - average\n"
	       "     sd  - standard deviation\n"
	       "    min  - minimum\n"
	       "    max  - maximum\n"
	       "  med25  - the 25 percentile from the bottom\n"
	       "  med50  - the 50 percentile from the bottom\n"
	       "  med75  - the 75 percentile from the bottom\n"
	       " The expected file supports 2 pre-defined sectors: 'scale'\n"
	       " and 'default_values'.  These 2 sectors (if specified) are\n"
	       " applied first, allowing sector specific values to over ride\n"
	       " these default settings.\n\n");

	printf(" Comparisons is performed and reported on any statistic\n"
	       " that has both an expected and scale factor defined.\n"
	       " In addition, a list of statistics comparison may be listed\n"
	       " using the 'expected_result' option.  This is a comma\n"
	       " separated list of the desired statistics.  The scale factor\n"
	       " for a statistics can be specified/modified in the list by\n"
	       " setting its value after the statistics key word, separated\n"
	       " by either a : or = sign (ie 'bw,av=12,sd=9,min,max').\n"
	       " All scale factors represents the percentag deviation\n"
	       " from the expected value\n\n");

	printf("*** Usage using file options only ****\n"
	       " %s --file-name <file_name> --config <test> --display <value>\n"
	       "\n"
	       "  --file-name <file_name>\n"
	       "      Short version: -f\n"
	       "      The name of file with list of parameters and arguments\n"
	       "\n"
	       "  --config <test_group>\n"
	       "      Short version: -c\n"
	       "      Name of sector/group to obtain information\n"
	       "\n"
	       "  --display <value>\n"
	       "      Short version: -d\n"
	       "      Display the configuration file setup\n"
	       "      Negative values of these will displays only\n"
	       "	'0' - no display shown\n"
	       "	'1' - show info on specified sector/group\n"
	       "	'2' - show all sector/group headings\n"
	       "	'3' - show all sector/group info specified in file\n"
	       "\n",
	       prog_name
	);

	printf("*** Usage using any command line options ***\n"
	       " %s --group-name <name> --endpoint"
	       " <ranks:tags> [optional arguments]\n"
	       "\n"
	       "Required Arguments\n"
	       "  --group-name <group_name>\n"
	       "      Short version: -g\n"
	       "      The name of the process set to test against\n"
	       "\n"
	       "  --endpoint <ranks:tags>\n"
	       "      Short version: -e\n"
	       "      Describes an endpoint (or range of endpoints)to connect to\n"
	       "	Note: Can be specified multiple times\n"
	       "\n"
	       "      ranks and tags are comma-separated lists to connect to\n"
	       "	Supports both ranges and lists - for example, \"1-5,3,8\"\n"
	       "\n"
	       "      Example: --endpoint 1-3,2:0-1\n"
	       "	This would create these endpoints:\n"
	       "	  1:0\n"
	       "	  1:1\n"
	       "	  2:0\n"
	       "	  2:1\n"
	       "	  3:0\n"
	       "	  3:1\n"
	       "	  2:0\n"
	       "	  2:1\n"
	       "\n"
	       "	By default, self-test will send test messages to these\n"
	       "	endpoints in the order listed above.See --randomize-endpoints\n"
	       "	for more information\n"
	       "\n"
	       "Optional Arguments\n"
	       "  --help\n"
	       "      Short version: -h\n"
	       "      Display this usage output\n"
	       "\n"
	       "  --file-name <file_name>\n"
	       "      Short version: -f\n"
	       "      The name of file with list of parameters and arguments\n"
	       "\n"
	       "  --expected-input <file_name>\n"
	       "      Short version: -y\n"
	       "      The name of file with list of expected values\n"
	       "\n"
	       "  --expected-output <file_name>\n"
	       "      Short version: -z\n"
	       "      The name of file for output results\n"
	       "\n"
	       "  --expected-results <string>\n"
	       "      Short version: -x\n"
	       "      comma separarted string of statistics key word\n"
	       "      to output with optional scaling factor.\n"
	       "      i.e. av=23,sd,bw=50,tp,med25,med50,med75\n"
	       "\n"
	       "  --expected-threshold <value>\n"
	       "      Short version: -w\n"
	       "      Global scaling factor to apply to all statistics\n"
	       "\n"
	       "  --config <test_group>\n"
	       "      Short version: -c\n"
	       "      Name of sector/group to obtain information\n"
	       "\n"
	       "  --config-append <test_group_append>\n"
	       "      Short version: -o\n"
	       "      String to append to sector name in the result file\n"
	       "\n"
	       "  --display <value>\n"
	       "      Short version: -d\n"
	       "      Display the configuration file setup\n"
	       "      Negative values of these will displays only\n"
	       "	'0' - no display shown\n"
	       "	'1' - show info on specified sector/group\n"
	       "	'2' - show all sector/group headings\n"
	       "	'3' - show all sector/group info specified in file\n"
	       "\n"
	       "  --raw_data <value>\n"
	       "      Short version: -b\n"
	       "      For each endpoint:tag, number of raw data latencies\n"
	       "      output into results file.\n"
	       "      Section name is <section_name>_raw\n"
	       "\n"
	       "  --message-sizes <(a b),(c d),...>\n"
	       "      Short version: -s\n"
	       "      List of size tuples (in bytes) to use for the self test.\n"
	       "\n"
	       "      Note that the ( ) are not strictly necessary\n"
	       "      Providing a single size (a) is interpreted as an alias for (a a)\n"
	       "\n"
	       "      For each tuple, the first value is the sent size\n"
	       "      and the second value is the reply size\n"
	       "      Valid sizes are [0-%d]\n"
	       "      Performance results will be reported individually for each tuple.\n"
	       "\n"
	       "      Each size integer can be prepended with a single character to specify\n"
	       "      the underlying transport mechanism. Available types are:\n"
	       "	'e' - Empty (no payload)\n"
	       "	'i' - I/O vector (IOV)\n"
	       "	'b' - Bulk transfer\n"
	       "      For example, (b1000) would transfer 1000 bytes via bulk in both directions\n"
	       "      Similarly, (i100 b1000) would use IOV to send and bulk to reply\n"
	       "      Only reasonable combinations are permitted (i.e. e1000 is not allowed)\n"
	       "      If no type specifier is specified, one will be chosen automatically.\n"
	       "	The simple heuristic is that bulk will be used if a specified\n"
	       "	size is >= %u\n"
	       "      BULK_GET will be used on the service side to 'send' data from client\n"
	       "	to service, and BULK_PUT will be used on the service side to 'reply'\n"
	       "	(assuming bulk transfers specified)\n"
	       "\n"
	       "      Note that different messages are sent internally via different structures.\n"
	       "      These are enumerated as follows, with x,y > 0:\n"
	       "	(0  0)  - Empty payload sent in both directions\n"
	       "	(ix 0)  - 8-byte session_id + x-byte iov sent, empty reply\n"
	       "	(0  iy) - 8-byte session_id sent, y-byte iov reply\n"
	       "	(ix iy) - 8-byte session_id + x-byte iov sent, y-byte iov reply\n"
	       "	(0  by) - 8-byte session_id + 8-byte bulk handle sent\n"
	       "		  y-byte BULK_PUT, empty reply\n"
	       "	(bx 0)  - 8-byte session_id + 8-byte bulk_handle sent\n"
	       "		  x-byte BULK_GET, empty reply\n"
	       "	(ix by) - 8-byte session_id + x-byte iov + 8-byte bulk_handle sent\n"
	       "		  y-byte BULK_PUT, empty reply\n"
	       "	(bx iy) - 8-byte session_id + 8-byte bulk_handle sent\n"
	       "		  x-byte BULK_GET, y-byte iov reply\n"
	       "	(bx by) - 8-byte session_id + 8-byte bulk_handle sent\n"
	       "		  x-byte BULK_GET, y-byte BULK_PUT, empty reply\n"
	       "\n"
	       "      Note also that any message size other than (0 0) will use test sessions.\n"
	       "	A self-test session will be negotiated with the service before sending\n"
	       "	any traffic, and the session will be closed after testing this\n"
	       "	size completes.\n"
	       "	The time to create and tear down these sessions is NOT measured.\n"
	       "\n"
	       "      Default: \"%s\"\n"
	       "\n"
	       "  --master-endpoint <ranks:tags>\n"
	       "      Short version: -m\n"
	       "      Describes an endpoint (or range of endpoints) that will each run a\n"
	       "	1:many self-test against the list of endpoints given via the\n"
	       "	--endpoint argument.\n"
	       "\n"
	       "      Specifying multiple --master-endpoint ranks/tags sets up a many:many\n"
	       "	self-test - the first 'many' is the list of master endpoints, each\n"
	       "	which executes a separate concurrent test against the second\n"
	       "	'many' (the list of test endpoints)\n"
	       "\n"
	       "      The argument syntax for this option is identical to that for\n"
	       "	--endpoint. Also, like --endpoint, --master-endpoint can be\n"
	       "	specified multiple times\n"
	       "\n"
	       "      Unlike --endpoint, the list of master endpoints is sorted and\n"
	       "	any duplicate entries are removed automatically. This is because\n"
	       "	each instance of self-test can only manage one 1:many test at\n"
	       "	a time\n"
	       "\n"
	       "      If not specified, the default value is to use this command-line\n"
	       "	application itself to run a 1:many test against the test endpoints\n"
	       "\n"
	       "      This client application sends all of the self-test parameters to\n"
	       "	this master node and instructs it to run a self-test session against\n"
	       "	the other endpoints specified by the --endpoint argument\n"
	       "\n"
	       "      This allows self-test to be run between any arbitrary CART-enabled\n"
	       "	applications without having to make them self-test aware. These\n"
	       "	other applications can be busy doing something else entirely and\n"
	       "	self-test will have no impact on that workload beyond consuming\n"
	       "	additional network and compute resources\n"
	       "\n"
	       "  --repetitions-per-size <N>\n"
	       "      Short version: -r\n"
	       "      Number of samples per message size per endpt.\n"
	       "      RPCs for each particular size will be repeated this many times per endpt.\n"
	       "      Default: %d\n"
	       "\n"
	       "  --max-inflight-rpcs <N>\n"
	       "      Short version: -i\n"
	       "      Maximum number of RPCs allowed to be executing concurrently.\n"
	       "\n"
	       "      Note that at the beginning of each test run, a buffer of size send_size\n"
	       "	is allocated for each inflight RPC (total max_inflight * send_size).\n"
	       "	This could be a lot of memory. Also, if the reply uses bulk, the\n"
	       "	size increases to (max_inflight * max(send_size, reply_size))\n"
	       "\n"
	       "      Default: %d\n"
	       "\n"
	       "  --align <alignment>\n"
	       "      Short version: -a\n"
	       "\n"
	       "      Forces all test buffers to be aligned (or misaligned) as specified.\n"
	       "\n"
	       "      The argument specifies what the least-significant byte of all test buffer\n"
	       "	addresses should be forced to be. For example, if --align 0 is specified,\n"
	       "	all test buffer addresses will end in 0x00 (thus aligned to 256 bytes).\n"
	       "	To force misalignment, use something like --align 3. For 64-bit (8-byte)\n"
	       "	alignment, use something like --align 8 or --align 24 (0x08 and 0x18)\n"
	       "\n"
	       "      Alignment should be specified as a decimal value in the range [%d:%d]\n"
	       "\n"
	       "      If specified, buffers will be allocated with an extra 256 bytes of\n"
	       "	alignment padding and the buffer to transfer will start at the point which\n"
	       "	the least - significant byte of the address matches the requested alignment.\n"
	       "\n"
	       "      Default is no alignment - whatever is returned by the allocator is used\n"
	       "\n"
	       "  --Mbits\n"
	       "      Short version: -b\n"
	       "      By default, self-test outputs performance results in MB (#Bytes/1024^2)\n"
	       "      Specifying --Mbits switches the output to megabits (#bits/1000000)\n"
	       "  --singleton\n"
	       "      Short version: -t\n"
	       "      If specified, self_test will launch as a singleton process (with no orterun).\n"
	       "  --path  /path/to/attach_info_file/directory\n"
	       "      Short version: -p  prefix\n"
	       "      This option implies --singleton is set.\n"
	       "	If specified, self_test will use the address information in:\n"
	       "	/tmp/group_name.attach_info_tmp, if prefix is specified, self_test will\n"
	       "	use the address information in: prefix/group_name.attach_info_tmp.\n"
	       "	Note the = sign in the option.\n",
	       prog_name, UINT32_MAX,
	       CRT_SELF_TEST_AUTO_BULK_THRESH, msg_sizes_str, rep_count,
	       max_inflight, CRT_ST_BUF_ALIGN_MIN, CRT_ST_BUF_ALIGN_MIN);
}

#define ST_ENDPT_RANK_IDX 0
#define ST_ENDPT_TAG_IDX 1
static int
st_validate_range_str(const char *str)
{
	const char *start = str;

	while (*str != '\0') {
		if ((*str < '0' || *str > '9') &&
		    (*str != '-') && (*str != ',')) {
			return -EINVAL;
		}

		str++;

		/* Make sure the range string isn't ridiculously large */
		if (str - start > SELF_TEST_MAX_LIST_STR_LEN)
			return -EINVAL;
	}
	return 0;
}

static void
st_parse_range_str(char *const str, char *const validated_str,
		   uint32_t *num_elements)
{
	char *pch;
	char *pch_sub;
	char *saveptr_comma = NULL;
	char *saveptr_hyphen = NULL;
	char *validated_cur_ptr = validated_str;

	/* Split into tokens based on commas */
	pch = strtok_r(str, ",", &saveptr_comma);
	while (pch != NULL) {
		/* Number of valid hyphen-delimited values encountered so far */
		int		hyphen_count = 0;
		/* Start/stop values */
		uint32_t	val[2] = {0, 0};
		/* Flag indicating if values were filled, 0=no 1=yes */
		int		val_valid[2] = {0, 0};

		/* Number of characters left to write to before overflowing */
		int		num_avail = SELF_TEST_MAX_LIST_STR_LEN -
					(validated_cur_ptr - validated_str);
		int		num_written = 0;

		/*
		 * Split again on hyphens, using only the first two non-empty
		 * values
		 */
		pch_sub = strtok_r(pch, "-", &saveptr_hyphen);
		while (pch_sub != NULL && hyphen_count < 2) {
			if (*pch_sub != '\0') {
				/*
				 * Seems like we have a valid number.
				 * If anything goes wrong, skip over this
				 * comma-separated range/value.
				 */
				if (sscanf(pch_sub, "%u",
					   &val[hyphen_count]) != 1) {
					val_valid[0] = 0;
					val_valid[1] = 0;
					break;
				}

				val_valid[hyphen_count] = 1;

				hyphen_count++;
			}

			pch_sub = strtok_r(NULL, "-", &saveptr_hyphen);
		}

		if (val_valid[0] == 1 && val_valid[1] == 1) {
			/* This was a valid range */
			uint32_t min = val[0] < val[1] ? val[0] : val[1];
			uint32_t max = val[0] > val[1] ? val[0] : val[1];

			*num_elements += max - min + 1;
			num_written = snprintf(validated_cur_ptr, num_avail,
					       "%u-%u,", min, max);
		} else if (val_valid[0] == 1) {
			/* Only one valid value */
			*num_elements += 1;
			num_written = snprintf(validated_cur_ptr, num_avail,
					       "%u,", val[0]);
		}

		/*
		 * It should not be possible to provide input that gets larger
		 * after sanition
		 */
		D_ASSERT(num_written <= num_avail);

		validated_cur_ptr += num_written;

		pch = strtok_r(NULL, ",", &saveptr_comma);
	}

	/* Trim off the trailing , */
	if (validated_cur_ptr > validated_str)
		*(validated_cur_ptr - 1) = '\0';
}

int
parse_endpoint_string(char *const opt_arg,
		      struct st_endpoint **const endpts,
		      uint32_t *const num_endpts)
{
	char			*token_ptrs[2] = {NULL, NULL};
	int			 separator_count = 0;
	int			 ret = 0;
	char			*pch = NULL;
	char			*rank_valid_str = NULL;
	uint32_t		 num_ranks = 0;
	char			*tag_valid_str = NULL;
	uint32_t		 num_tags = 0;
	struct st_endpoint	*realloced_mem;
	struct st_endpoint	*next_endpoint;
	char			*save_ptr = NULL;

	/*
	 * strtok replaces separators with \0 characters
	 * Use this to divide up the input argument into three strings
	 *
	 * Use the first three ; delimited strings - ignore the rest
	 */
	pch = strtok_r(opt_arg, ":", &save_ptr);
	while (pch != NULL && separator_count < 2) {
		token_ptrs[separator_count] = pch;

		separator_count++;

		pch = strtok_r(NULL, ":", &save_ptr);
	}

	/* Validate the input strings */
	if (token_ptrs[ST_ENDPT_RANK_IDX] == NULL ||
	    token_ptrs[ST_ENDPT_TAG_IDX] == NULL ||
	    *token_ptrs[ST_ENDPT_RANK_IDX] == '\0' ||
	    *token_ptrs[ST_ENDPT_TAG_IDX] == '\0') {
		printf("endpoint must contain non-empty rank:tag\n");
		return -EINVAL;
	}
	/* Both group and tag can only contain [0-9\-,] */
	if (st_validate_range_str(token_ptrs[ST_ENDPT_RANK_IDX]) != 0) {
		printf("endpoint rank contains invalid characters\n");
		return -EINVAL;
	}
	if (st_validate_range_str(token_ptrs[ST_ENDPT_TAG_IDX]) != 0) {
		printf("endpoint tag contains invalid characters\n");
		return -EINVAL;
	}

	/*
	 * Now that strings have been sanity checked, allocate some space for a
	 * fully-validated copy of the rank and tag list. This works as follows:
	 * - The input string is tokenized and parsed
	 * - Each value (or range) is checked for validity
	 * - If valid, that range is written to the _valid_ string and the
	 *   number of elements in that range is added to the counter
	 *
	 * After both ranks and tags have been validated, the endpoint array can
	 * be resized to accommodate the new entries and the validated string
	 * can be re-parsed (without error checking) to add elements to the
	 * array.
	 */
	D_ALLOC(rank_valid_str, SELF_TEST_MAX_LIST_STR_LEN);
	if (rank_valid_str == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);

	D_ALLOC(tag_valid_str, SELF_TEST_MAX_LIST_STR_LEN);
	if (tag_valid_str == NULL)
		D_GOTO(cleanup, ret = -ENOMEM);

	st_parse_range_str(token_ptrs[ST_ENDPT_RANK_IDX], rank_valid_str,
			   &num_ranks);

	st_parse_range_str(token_ptrs[ST_ENDPT_TAG_IDX], tag_valid_str,
			   &num_tags);

	/* Validate num_ranks and num_tags */
	if ((((uint64_t)num_ranks * (uint64_t)num_tags)
	     > (uint64_t)SELF_TEST_MAX_NUM_ENDPOINTS) ||
	    (((uint64_t)*num_endpts + (uint64_t)num_ranks * (uint64_t)num_tags)
	     > (uint64_t)SELF_TEST_MAX_NUM_ENDPOINTS)) {
		D_ERROR("Too many endpoints - current=%u, "
			"additional requested=%lu, max=%u\n",
			*num_endpts,
			(uint64_t)num_ranks * (uint64_t)num_tags,
			SELF_TEST_MAX_NUM_ENDPOINTS);
		D_GOTO(cleanup, ret = -EINVAL);
	}

	printf("Adding endpoints:\n");
	printf("  ranks: %s (# ranks = %u)\n", rank_valid_str, num_ranks);
	printf("  tags: %s (# tags = %u)\n", tag_valid_str, num_tags);

	/* Reallocate/expand the endpoints array */
	D_REALLOC_ARRAY(realloced_mem, *endpts, *num_endpts,
			*num_endpts + num_ranks * num_tags);
	if (realloced_mem == NULL)
		D_GOTO(cleanup, ret = -DER_NOMEM);
	*num_endpts += num_ranks * num_tags;
	*endpts = (struct st_endpoint *)realloced_mem;

	/* Populate the newly expanded values in the endpoints array */
	next_endpoint = *endpts + *num_endpts - (num_ranks * num_tags);

	/*
	 * This block uses simpler tokenization logic (not strtok) because it
	 * has already been pre-validated
	 */

	/* Iterate over all rank tokens */
	pch = rank_valid_str;
	while (*pch != '\0') {
		uint32_t	rank;
		uint32_t	rank_max;
		int		num_scanned;

		num_scanned = sscanf(pch, "%u-%u", &rank, &rank_max);
		D_ASSERT(num_scanned == 1 || num_scanned == 2);
		if (num_scanned == 1)
			rank_max = rank;

		/* For this rank token, iterate over its range */
		do {
			uint32_t	 tag;
			uint32_t	 tag_max;
			char		*pch_tag;

			pch_tag = tag_valid_str;

			/*
			 * For this particular rank, iterate over all tag tokens
			 */
			while (*pch_tag != '\0') {
				num_scanned = sscanf(pch_tag, "%u-%u", &tag,
						     &tag_max);
				D_ASSERT(num_scanned == 1 || num_scanned == 2);
				if (num_scanned == 1)
					tag_max = tag;

				/*
				 * For this rank and chosen tag token, iterate
				 * over the range of tags in this tag token
				 */
				do {
					next_endpoint->rank = rank;
					next_endpoint->tag = tag;
					next_endpoint++;
					tag++;
				} while (tag <= tag_max);

				/* Advance the pointer just past the next , */
				do {
					pch_tag++;
				} while (*pch_tag != '\0' && *pch_tag != ',');
				if (*pch_tag == ',')
					pch_tag++;
			}

			rank++;
		} while (rank <= rank_max);

		/* Advance the pointer just past the next , */
		do {
			pch++;
		} while (*pch != '\0' && *pch != ',');
		if (*pch == ',')
			pch++;
	}

	/* Make sure all the allocated space got filled with real endpoints */
	D_ASSERT(next_endpoint == *endpts + *num_endpts);

	ret = 0;

cleanup:
	if (rank_valid_str != NULL)
		D_FREE(rank_valid_str);
	if (tag_valid_str != NULL)
		D_FREE(tag_valid_str);

	return ret;
}

/**
 * Parse a message size tuple from the user. The input format for this is
 * described in the usage text - basically one or two unsigned integer sizes,
 * each optionally prefixed by a character that specifies what underlying IO
 * type should be used to transfer a payload of that size (empty, iov, bulk).
 *
 * \return	0 on successfully filling *test_params, nonzero otherwise
 */
int
parse_message_sizes_string(const char *pch,
			   struct st_size_params *test_params)
{
	/*
	 * Note whether a type is specified or not. If no type is
	 * specified by the user, it will be automatically selected
	 */
	int send_type_specified = 0;
	int reply_type_specified = 0;

	/*
	 * A simple map between identifier ('e') and type (...EMPTY)
	 *
	 * Note that BULK_PUT (for send) or BULK_GET (for reply) are
	 * not yet implemented. For this reason, only 'b' for bulk is
	 * accepted as a type here, which will automatically choose
	 * PUT or GET depending on the direction.
	 *
	 * If send/PUT or reply/GET are ever implemented, the map can be
	 * easily changed to support this.
	 */
	/* Number of types recognized */
	const int num_types = ARRAY_SIZE(transfer_type_map);

	int ret;

	/*
	 * Advance pch to the next numerical character in the token
	 * If along the way pch happens to be one of the type
	 * characters, note down that type and continue hunting for a
	 * number. In this way, only the last type specifier before the
	 * number is stored.
	 */
	while (*pch != '\0' && (*pch < '0' || *pch > '9')) {
		int i;

		for (i = 0; i < num_types; i++)
			if (*pch == transfer_type_map[i].identifier) {
				send_type_specified = 1;
				test_params->send_type =
					transfer_type_map[i].type;
			}
		pch++;
	}
	if (*pch == '\0')
		return -1;

	/* Read the first size */
	ret = sscanf(pch, "%u", &test_params->send_size);
	if (ret != 1)
		return -1;

	/* Advance pch to the next non-numeric character */
	while (*pch != '\0' && *pch >= '0' && *pch <= '9')
		pch++;

	/*
	 * Advance pch to the next numerical character in the token
	 * If along the way pch happens to be one of the type
	 * characters, note down that type and continue hunting for a
	 * number. In this way, only the last type specifier before the
	 * number is stored.
	 */
	while (*pch != '\0' && (*pch < '0' || *pch > '9')) {
		int i;

		for (i = 0; i < num_types; i++)
			if (*pch == transfer_type_map[i].identifier) {
				reply_type_specified = 1;
				test_params->reply_type =
					transfer_type_map[i].type;
			}
		pch++;
	}
	if (*pch != '\0') {
		/* Read the second size */
		ret = sscanf(pch, "%u", &test_params->reply_size);
		if (ret != 1)
			return -1;
	} else {
		/* Only one numerical value - that's perfectly valid */
		test_params->reply_size = test_params->send_size;
		test_params->reply_type = test_params->send_type;
		reply_type_specified = send_type_specified;
	}

	/* If we got here, the send_size and reply_size are valid */

	/***** Automatically assign types if they were not specified *****/
	if (send_type_specified == 0) {
		if (test_params->send_size == 0)
			test_params->send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
		else if (test_params->send_size <
			  CRT_SELF_TEST_AUTO_BULK_THRESH)
			test_params->send_type = CRT_SELF_TEST_MSG_TYPE_IOV;
		else
			test_params->send_type =
				CRT_SELF_TEST_MSG_TYPE_BULK_GET;
	}
	if (reply_type_specified == 0) {
		if (test_params->reply_size == 0)
			test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
		else if (test_params->reply_size <
			  CRT_SELF_TEST_AUTO_BULK_THRESH)
			test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_IOV;
		else
			test_params->reply_type =
				CRT_SELF_TEST_MSG_TYPE_BULK_PUT;
	}

	/***** Silently / automatically correct invalid types *****/
	/* Empty messages always have empty type */
	if (test_params->send_size == 0)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
	if (test_params->reply_size == 0)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;

	/* All other empty requests with nonzero payload convert to iov */
	if (test_params->send_size != 0 &&
	    test_params->send_type == CRT_SELF_TEST_MSG_TYPE_EMPTY)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_IOV;
	if (test_params->reply_size != 0 &&
	    test_params->reply_type == CRT_SELF_TEST_MSG_TYPE_EMPTY)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_IOV;

	/* Bulk requests convert to the type allowed by send/reply */
	if (test_params->send_type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_BULK_GET;
	if (test_params->reply_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_BULK_PUT;

	return 0;
}

/*
 *********************************
 * START: Add libconfigini macros
 *********************************
 */
#define LOG_ERR(fmt, ...)	\
	fprintf(stderr, "[ERROR] <%s:%d> : " fmt "\n",\
		__func__, __LINE__, __VA_ARGS__)
/*
 ******************************
 * END: Add libconfigini macros
 ******************************
 */
/*
 * Read Config file and interpret;
 */
#define STRING_MAX_SIZE 256
static int
config_file_setup(char *file_name, char *section_name,
		  char *display)
{
	Config *cfg = NULL;
	int ret = 0;
	char *end_ptr;
	ConfigRet config_ret;
	char string[STRING_MAX_SIZE];
	int len;
	int temp;

	/* Read and parse configuration file */
	config_ret = ConfigReadFile(file_name, &cfg);
	if (config_ret != CONFIG_OK) {
		printf("ConfigOpenFile failed for: %s\n", file_name);
		D_EMIT("ConfigOpenFile failed for: %s\n", file_name);
		return -ENOENT;
	}

	/************/
	config_ret = ConfigReadString(cfg, section_name, "display",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (display || (config_ret == CONFIG_OK)) {
		printf("Configuration file %s\n", file_name);
		/*
		 * display option set via command line "display"
		 * or by display option in file "string"
		 */
		if (display)
			temp = strtol(display, NULL, 10);
		else
			temp = strtol(&string[0], NULL, 10);

		if ((temp == 1) || (temp == -1)) {
			/* Avoid checkpatch warning */
			ConfigPrintSection(cfg, stdout, section_name);
		} else if ((temp == 2) || (temp == -2)) {
			/* Avoid checkpatch warning */
			ConfigPrintSectionNames(cfg, stdout);
		} else if ((temp == 3) || (temp == -3)) {
			/* Avoid checkpatch warning */
			ConfigPrint(cfg, stdout);
		}
		if (temp < 0) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -1);
		}
	}

	/* Parse of configuration file */
	/********/
	config_ret = ConfigReadString(cfg, section_name, "help",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		/* Avoid checkpatch warning */
		D_GOTO(cleanup, ret = 1);
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "group-name",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_dest_name = (char *)malloc(len);
		if (gbl.g_dest_name == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		gbl.alloc_g_dest_name = true;
		memcpy(gbl.g_dest_name, string, len);
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "master-endpoint",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		/* Avoid checkpatch warning */
		parse_endpoint_string(&string[0], &gbl.g_ms_endpts,
				      &gbl.g_num_ms_endpts);
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "endpoint",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		/* Avoid checkpatch warning */
		parse_endpoint_string(&string[0], &gbl.g_endpts,
				      &gbl.g_num_endpts);
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "message-sizes",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_msg_sizes_str = (char *)malloc(len);
		if (gbl.g_msg_sizes_str == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		gbl.alloc_g_msg_sizes_str = true;
		memcpy(gbl.g_msg_sizes_str, string, len);
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "repetitions-per-size",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		gbl.g_rep_count = strtol(&string[0], &end_ptr, 10);
		if (end_ptr == &string[0]) {
			gbl.g_rep_count = gbl.g_default_rep_count;
			printf("Warning: Invalid repetitions-per-size\n"
			       "  Using default value %d instead\n",
			       gbl.g_rep_count);
		}
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "max-inflight-rpcs",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		gbl.g_max_inflight = strtol(&string[0], &end_ptr, 10);
		if (end_ptr == &string[0]) {
			gbl.g_max_inflight = gbl.g_default_max_inflight;
			printf("Warning: Invalid max-inflight-rpcs\n"
			"  Using default value %d instead\n",
			gbl.g_max_inflight);
		}
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "align",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		gbl.g_buf_alignment = strtol(&string[0], &end_ptr, 10);
		if (end_ptr == &string[0] ||
		    gbl.g_buf_alignment < CRT_ST_BUF_ALIGN_MIN ||
		    gbl.g_buf_alignment > CRT_ST_BUF_ALIGN_MAX) {
			printf("Warning: Invalid align value %d;"
			       " Expected value in range [%d:%d]\n",
			       gbl.g_buf_alignment, CRT_ST_BUF_ALIGN_MIN,
			       CRT_ST_BUF_ALIGN_MAX);
			gbl.g_buf_alignment = CRT_ST_BUF_ALIGN_DEFAULT;
		}
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "MBits",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		temp = strtol(&string[0], NULL, 10);
		if (temp == 0)
			gbl.g_output_megabits = 0;
		else
			gbl.g_output_megabits = 1;
	}

#ifdef INCLUDE_OBSOLETE
	/********/
	config_ret = ConfigReadString(cfg, section_name, "singleton",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
#endif

	/********/
	config_ret = ConfigReadString(cfg, section_name, "randomize-endpoints",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		temp = strtol(&string[0], NULL, 10);
		if (temp == 0)
			gbl.g_randomize_endpoints = false;
		else
			gbl.g_randomize_endpoints = true;
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name, "path",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_attach_info_path = (char *)malloc(len);
		if (gbl.g_attach_info_path == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		memcpy(gbl.g_attach_info_path, string, len);
	}

	/**********/
	config_ret = ConfigReadString(cfg, section_name,
				      "expected-threshold",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		/* Avoid checkpatch warning */
		gbl.g_scale_factor = strtol(&string[0], NULL, 10);
	}

	/**********/
	config_ret = ConfigReadString(cfg, section_name,
				      "raw_data",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		gbl.g_raw_data = strtol(&string[0], NULL, 10);
		gbl.g_raw_data = gbl.g_raw_data <
				 SELF_TEST_MAX_RAW_DATA_OUTPUT ?
				 gbl.g_raw_data : SELF_TEST_MAX_RAW_DATA_OUTPUT;
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "expected-results",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_expected_results = (char *)malloc(len);
		if (gbl.g_expected_results == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		memcpy(gbl.g_expected_results, string, len);
		gbl.alloc_g_expected_results = true;
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "expected-output",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_expected_outfile = (char *)malloc(len);
		if (gbl.g_expected_outfile == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		memcpy(gbl.g_expected_outfile, string, len);
		gbl.alloc_g_expected_outfile = true;
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "config-append",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_config_append = (char *)malloc(len);
		if (gbl.g_config_append == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		memcpy(gbl.g_config_append, string, len);
		gbl.alloc_g_config_append = true;
	}

	/********/
	config_ret = ConfigReadString(cfg, section_name,
				      "expected-input",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
	if (config_ret == CONFIG_OK) {
		len = strlen(string) + 1;
		gbl.g_expected_infile = (char *)malloc(len);
		if (gbl.g_expected_infile == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(cleanup, ret = -ENOMEM);
		}
		memcpy(gbl.g_expected_infile, string, len);
		gbl.alloc_g_expected_infile = true;
	}

	/********/
#ifdef INCLUDE_OBSOLETE
	config_ret = ConfigReadString(cfg, section_name, "nopmix",
				      &string[0], STRING_MAX_SIZE,
				      (char *)NULL);
#endif

	/* Free up structure and return */
cleanup:
	ConfigFree(cfg);
	return ret;
}

int
parse_command_options(int argc, char *argv[])
{
	int	c;
	int	ret = 0;
	char	*end_ptr = NULL;

	while (1) {
		c = getopt_long(argc, argv, ARGV_PARAMETERS,
				long_options, NULL);
		/* Break out of while loop */
		if (c == -1)
			break;

		switch (c) {
		/* 2 parameters for file specification */
		case 'f':
		case 'c':
		case 'd':
			break;

		/* Non file parameters. May be used to override file. */
		case 'g':
			if (gbl.g_dest_name != NULL) {
				/* Avoid checkpatch warning */
				free(gbl.g_dest_name);
			}
			gbl.alloc_g_dest_name = false;
			gbl.g_dest_name = optarg;
			break;
		case 'm':
			parse_endpoint_string(optarg, &gbl.g_ms_endpts,
					      &gbl.g_num_ms_endpts);
			break;
		case 'e':
			parse_endpoint_string(optarg, &gbl.g_endpts,
					      &gbl.g_num_endpts);
			break;
		case 's':
			if (gbl.alloc_g_msg_sizes_str) {
				/* Avoid checkpatch warning */
				free(gbl.g_msg_sizes_str);
			}
			gbl.alloc_g_msg_sizes_str = false;
			gbl.g_msg_sizes_str = optarg;
			break;
		case 'r':
			gbl.g_rep_count = strtol(optarg, &end_ptr, 10);
			if (end_ptr == optarg) {
				gbl.g_rep_count = gbl.g_default_rep_count;
				printf("Warning: Invalid repetitions-per-size\n"
				       "  Using default value %d instead\n",
				       gbl.g_rep_count);
			}
			break;
		case 'i':
			gbl.g_max_inflight = strtol(optarg, &end_ptr, 10);
			if (end_ptr == optarg) {
				gbl.g_max_inflight = gbl.g_default_max_inflight;
				printf("Warning: Invalid max-inflight-rpcs\n"
				       "  Using default value %d instead\n",
				       gbl.g_max_inflight);
			}
			break;
		case 'a':
			gbl.g_buf_alignment = strtol(optarg, &end_ptr, 10);
			if (end_ptr == optarg ||
			    gbl.g_buf_alignment < CRT_ST_BUF_ALIGN_MIN ||
			    gbl.g_buf_alignment > CRT_ST_BUF_ALIGN_MAX) {
				printf("Warning: Invalid align value %d;"
				       " Expected value in range [%d:%d]\n",
				       gbl.g_buf_alignment,
				       CRT_ST_BUF_ALIGN_MIN,
				       CRT_ST_BUF_ALIGN_MAX);
				gbl.g_buf_alignment = CRT_ST_BUF_ALIGN_DEFAULT;
			}
			break;
		case 'b':
			gbl.g_output_megabits = 1;
			break;
		case 'p':
			if (gbl.g_attach_info_path != NULL) {
				/* Avoid checkpatch warning */
				free(gbl.g_attach_info_path);
			}
			gbl.alloc_g_attach_info_path = false;
			gbl.g_attach_info_path = optarg;
			break;
		case 'q':
			gbl.g_randomize_endpoints = true;
			break;
		case 'v':  /* *** */
			gbl.g_raw_data = strtol(optarg, NULL, 10);
			gbl.g_raw_data = gbl.g_raw_data <
				     SELF_TEST_MAX_RAW_DATA_OUTPUT ?
				     gbl.g_raw_data :
				     SELF_TEST_MAX_RAW_DATA_OUTPUT;
			break;
		case 'w':  /* *** */
			gbl.g_scale_factor = strtof(optarg, NULL);
			break;
		case 'x':
			if (gbl.alloc_g_expected_results) {
				/* Avoid checkpatch warning */
				free(gbl.g_expected_results);
			}
			gbl.alloc_g_expected_results = false;
			gbl.g_expected_results = optarg;
			break;
		case 'y':
			if (gbl.alloc_g_expected_infile) {
				/* Avoid checkpatch warning */
				free(gbl.g_expected_infile);
			}
			gbl.alloc_g_expected_infile = false;
			gbl.g_expected_infile = optarg;
			break;
		case 'z':
			if (gbl.alloc_g_expected_outfile) {
				/* Avoid checkpatch warning */
				free(gbl.g_expected_outfile);
			}
			gbl.alloc_g_expected_outfile = false;
			gbl.g_expected_outfile = optarg;
			break;
		case 'o':
			if (gbl.alloc_g_config_append) {
				/* Avoid checkpatch warning */
				free(gbl.g_config_append);
			}
			gbl.alloc_g_config_append = false;
			gbl.g_config_append = optarg;
			break;
#ifdef INCLUDE_OBSOLETE
		/* 't' and 'n' options are deprecated */
		case 't':
			printf("Warning: 't' argument is deprecated\n");
			break;
		case 'n':
			printf("Warning: 'n' argument is deprecated\n");
			break;
#endif
		case 'h':
		case '?':
		default:
			print_usage(argv[0], default_msg_sizes_str,
				    gbl.g_default_rep_count,
				    gbl.g_default_max_inflight);
			if (c == 'h')
				D_GOTO(cleanup, ret = 1);
			else
				D_GOTO(cleanup, ret = -EINVAL);
		}
	}
	return 0;
cleanup:
	return ret;
}

int
main(int argc, char *argv[])
{
	char				*file_name = NULL;
	char				*section_name = NULL;
	const char			 tuple_tokens[] = "(),";
	struct st_size_params		*all_params = NULL;
	char				*sizes_ptr = NULL;
	char				*pch = NULL;
	int				 num_msg_sizes;
	int				 num_tokens;
	int				 c;
	int				 j;
	int				 ret = 0;

	char				*display = NULL;
	char				*str_send = NULL;
	char				*str_put = NULL;
	char				*save_ptr;

	gbl.g_msg_sizes_str = default_msg_sizes_str;
	gbl.g_rep_count = gbl.g_default_rep_count;
	gbl.g_max_inflight = gbl.g_default_max_inflight;

	ret = d_log_init();
	if (ret != 0) {
		fprintf(stderr, "crt_log_init() failed. rc: %d\n", ret);
		return ret;
	}

	/****************** First Parse user file arguments *************/
	/* File specified via -f file argument */
	while (1) {
		c = getopt_long(argc, argv, ARGV_PARAMETERS,
				long_options, NULL);
		/* break out of while loop */
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			printf("\n file name %s\n", optarg);
			file_name = optarg;
			break;
		case 'c':
			section_name = optarg;
			break;
		case 'd':
			display = optarg;
			break;
		case 'h':
			print_usage(argv[0], default_msg_sizes_str,
				    gbl.g_default_rep_count,
				    gbl.g_default_max_inflight);
			D_GOTO(cleanup, ret = 0);
			break;
		default:
			break;
		}
	}

	/* Configuration file specified.  Read it's context */
	if (file_name != NULL) {
		ret = config_file_setup(file_name, section_name, display);
		if (ret == 1) {
			print_usage(argv[0], default_msg_sizes_str,
				    gbl.g_default_rep_count,
				    gbl.g_default_max_inflight);
			D_GOTO(cleanup, ret = 0);
		}
		if (ret < 0) {
			/* avoid checkpatch warning */
			D_GOTO(cleanup, ret);
		}
	}

	/**************** Second Parse of user arguments ***************/
	/*
	* Command line arguments takes precedent.
	* Overwrite default and/or file input arguments
	* Restart the scanning.
	*/
	optind = 1;
	ret = parse_command_options(argc, argv);
	if (ret != 0)
		D_GOTO(cleanup, ret);

	/******* Parse message sizes argument ***************/
	/*
	 * Count the number of tuple tokens (',') in the user-specified string
	 * This gives an upper limit on the number of arguments the user passed
	 */
	num_tokens = 0;
	sizes_ptr = gbl.g_msg_sizes_str;
	while (1) {
		const char *token_ptr = tuple_tokens;

		/* Break upon reaching the end of the argument */
		if (*sizes_ptr == '\0')
			break;

		/*
		 * For each valid token, check if this character
		 * is that token
		 */
		while (1) {
			if (*token_ptr == '\0')
				break;
			if (*token_ptr == *sizes_ptr)
				num_tokens++;
			token_ptr++;
		}

		sizes_ptr++;
	}

	/*
	 * Allocate a large enough buffer to hold the message
	 * sizes list
	 */
	D_ALLOC_ARRAY(all_params, num_tokens + 1);
	if (all_params == NULL)
		D_GOTO(cleanup, ret = ENOMEM);

	/* Iterate over the user's message sizes and parse / validate them */
	num_msg_sizes = 0;
	pch = strtok_r(gbl.g_msg_sizes_str, tuple_tokens, &save_ptr);
	while (pch != NULL) {
		D_ASSERTF(num_msg_sizes <= num_tokens, "Token counting err\n");

		ret = parse_message_sizes_string(pch,
						 &all_params[num_msg_sizes]);
		if (ret == 0)
			num_msg_sizes++;
		else
			printf("Warning: Invalid message sizes tuple\n"
			       "  Expected values in range [0:%u], got '%s'\n",
			       UINT32_MAX,
			       pch);

		pch = strtok_r(NULL, tuple_tokens, &save_ptr);
	}

	if (num_msg_sizes <= 0) {
		printf("No valid message sizes given\n");
		D_GOTO(cleanup, ret = -EINVAL);
	}

	/* Shrink the buffer if some of the user's tokens weren't kept */
	if (num_msg_sizes < num_tokens + 1) {
		struct st_size_params *realloced_mem;

		/* This should always succeed since the buffer is shrinking.. */
		D_REALLOC_ARRAY(realloced_mem, all_params, num_tokens + 1,
				num_msg_sizes);
		if (realloced_mem == NULL)
			D_GOTO(cleanup, ret = -ENOMEM);
		all_params = (struct st_size_params *)realloced_mem;
	}

	/******************** Validate arguments ********************/
	if (gbl.g_dest_name == NULL ||
	    crt_validate_grpid(gbl.g_dest_name) != 0) {
		printf("--group-name argument not specified or is invalid\n");
		D_GOTO(cleanup, ret = -EINVAL);
	}
	if (gbl.g_ms_endpts == NULL)
		printf("Warning: No --master-endpoint specified; using this\n"
		       " command line application as the master endpoint\n");
	if (gbl.g_endpts == NULL || gbl.g_num_endpts == 0) {
		printf("No endpoints specified\n");
		D_GOTO(cleanup, ret = -EINVAL);
	}
	if ((gbl.g_rep_count <= 0) ||
	    (gbl.g_rep_count > SELF_TEST_MAX_REPETITIONS)) {
		printf("Invalid --repetitions-per-size argument\n"
		       "  Expected value in range (0:%d], got %d\n",
		       SELF_TEST_MAX_REPETITIONS, gbl.g_rep_count);
		D_GOTO(cleanup, ret = -EINVAL);
	}
	if ((gbl.g_max_inflight <= 0) ||
	    (gbl.g_max_inflight > SELF_TEST_MAX_INFLIGHT)) {
		printf("Invalid --max-inflight-rpcs argument\n"
		       "  Expected value in range (0:%d], got %d\n",
		       SELF_TEST_MAX_INFLIGHT, gbl.g_max_inflight);
		D_GOTO(cleanup, ret = -EINVAL);
	}

	/*
	 * No reason to have max_inflight bigger than the total number of RPCs
	 * each session
	 */
	gbl.g_max_inflight = gbl.g_max_inflight > gbl.g_rep_count ?
			 gbl.g_rep_count : gbl.g_max_inflight;

	/********************* Print out parameters *********************/
	printf("Self Test Parameters:\n"
	       "  Group name to test against: %s\n"
	       "  # endpoints:	%u\n"
	       "  Message sizes: [", gbl.g_dest_name, gbl.g_num_endpts);
	for (j = 0; j < num_msg_sizes; j++) {
		if (j > 0)
			printf(", ");

		MSG_TYPE_STR(str_send, all_params[j].send_type);
		MSG_TYPE_STR(str_put, all_params[j].reply_type);
		printf("(%d-%s %d-%s)", all_params[j].send_size,
		       str_send,
		       all_params[j].reply_size,
		       str_put);
	}
	printf("]\n");
	if (gbl.g_buf_alignment == CRT_ST_BUF_ALIGN_DEFAULT)
		printf("  Buffer addresses end with:  <Default>\n");
	else
		printf("  Buffer addresses end with:  %3d\n",
		       gbl.g_buf_alignment);
	printf("  Repetitions per size:	      %3d\n"
	       "  Max inflight RPCs:	      %3d\n\n",
	       gbl.g_rep_count, gbl.g_max_inflight);

	/* Evaluate name of results file */
	ret = file_name_create(&gbl.g_expected_outfile,
			       &gbl.alloc_g_expected_outfile,
			       "DAOS_TEST_LOG_DIR");
	if (ret != 0) {
		D_WARN("Error creating output name\n");
		D_GOTO(cleanup, ret);
	} else {
		D_WARN("Selftest Results File: %s\n", gbl.g_expected_outfile);
		printf("Selftest Results File:\n\t %s\n",
		       gbl.g_expected_outfile);
	}

	/****** Open global configuration for output results *****/
	/*
	 * If no section name specified, then will be created later on.
	 */
	ret = config_create_output_config(section_name, true);

	/********************* Run the self test *********************/
	ret = run_self_test(all_params, num_msg_sizes, gbl.g_rep_count,
			    gbl.g_max_inflight, gbl.g_dest_name,
			    gbl.g_ms_endpts, gbl.g_num_ms_endpts,
			    gbl.g_endpts, gbl.g_num_endpts,
			    gbl.g_output_megabits, gbl.g_buf_alignment,
			    gbl.g_attach_info_path, section_name);

	/* Write output results and free output configuration */
	if (gbl.g_expected_outfile != NULL) {
		ConfigRet config_ret;

		printf(" Selftest Results File:\n\t %s\n",
		       gbl.g_expected_outfile);
		config_ret = ConfigPrintToFile(gbl.cfg_output,
					       gbl.g_expected_outfile);
		if (config_ret != CONFIG_OK) {
			D_ERROR("Fail to write to output file: %s\n",
				gbl.g_expected_outfile);
		ret = -ENOENT;
		}
	} else {
		printf(" Selftest Results File not specified:"
		       " no results written\n");
		D_INFO(" Selftest Results File not specified:"
		       " no results written\n");
	}
	ConfigFree(gbl.cfg_output);

	/********************* Clean up *********************/
cleanup:
	if (gbl.alloc_g_dest_name && (gbl.g_dest_name != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_dest_name);
	}
	if (gbl.alloc_g_msg_sizes_str && (gbl.g_msg_sizes_str != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_msg_sizes_str);
	}
	if (gbl.alloc_g_attach_info_path && (gbl.g_attach_info_path != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_attach_info_path);
	}
	if (gbl.alloc_g_expected_results && (gbl.g_expected_results != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_expected_results);
	}
	if (gbl.alloc_g_expected_infile && (gbl.g_expected_infile != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_expected_infile);
	}
	if (gbl.alloc_g_expected_outfile && (gbl.g_expected_outfile != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_expected_outfile);
	}
	if (gbl.alloc_g_config_append && (gbl.g_config_append != NULL)) {
		/* Avoid checkpatch warning */
		D_FREE(gbl.g_config_append);
	}
	if (gbl.g_ms_endpts != NULL) {
		D_FREE(gbl.g_ms_endpts);
		gbl.g_ms_endpts = NULL;
	}
	if (gbl.g_endpts != NULL) {
		D_FREE(gbl.g_endpts);
		gbl.g_endpts = NULL;
	}
	if (all_params != NULL)
		D_FREE(all_params);
	d_log_fini();

	return ret;
}
