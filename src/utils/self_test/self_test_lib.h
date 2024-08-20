/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __SELF_TEST_LIB_H__
#define __SELF_TEST_LIB_H__

#include <stdint.h>

#include "crt_utils.h"

#define CRT_SELF_TEST_AUTO_BULK_THRESH (1 << 20)
#define CRT_SELF_TEST_GROUP_NAME       ("crt_self_test")

struct st_size_params {
	uint32_t send_size;
	uint32_t reply_size;
	union {
		struct {
			enum crt_st_msg_type send_type  : 2;
			enum crt_st_msg_type reply_type : 2;
		};
		uint32_t flags;
	};
};

struct st_endpoint {
	uint32_t rank;
	uint32_t tag;
};

struct st_master_endpt {
	crt_endpoint_t               endpt;
	struct crt_st_status_req_out reply;
	int32_t                      test_failed;
	int32_t                      test_completed;
};

static const char *const crt_st_msg_type_str[] = {"EMPTY", "IOV", "BULK_PUT", "BULK_GET"};

void
randomize_endpoints(struct st_endpoint *endpts, uint32_t num_endpts);
int
run_self_test(struct st_size_params all_params[], int num_msg_sizes, int rep_count,
	      int max_inflight, char *dest_name, struct st_endpoint *ms_endpts_in,
	      uint32_t num_ms_endpts_in, struct st_endpoint *endpts, uint32_t num_endpts,
	      struct st_master_endpt **ms_endpts_out, uint32_t *num_ms_endpts_out,
	      struct st_latency ****size_latencies, int16_t buf_alignment, char *attach_info_path,
	      bool use_agent, bool no_sync);
int
st_compare_endpts(const void *a_in, const void *b_in);
int
st_compare_latencies_by_vals(const void *a_in, const void *b_in);
int
st_compare_latencies_by_ranks(const void *a_in, const void *b_in);
void
free_size_latencies(struct st_latency ***latencies, uint32_t num_msg_sizes, uint32_t num_ms_endpts);

#endif /* __SELF_TEST_LIB_H__ */