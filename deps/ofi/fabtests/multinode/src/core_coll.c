/*
 * Copyright (c) 2017-2019 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHWARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. const NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER const AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS const THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>

#include <rdma/fi_errno.h>
#include <rdma/fi_domain.h>
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_trigger.h>
#include <rdma/fi_collective.h>

#include <core.h>
#include <coll_test.h>
#include <shared.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>

struct fid_av_set *av_set;
fi_addr_t world_addr;
fi_addr_t coll_addr;
struct fid_mc *coll_mc;

// For the verification
struct fi_av_set_attr av_set_attr;


static bool is_my_rank_participating(void)
{
	size_t rank = pm_job.my_rank;

	if (rank < av_set_attr.start_addr)
		return false;
	if (rank > av_set_attr.end_addr)
		return false;
	if ((rank - av_set_attr.start_addr) % av_set_attr.stride != 0)
		return false;
	return true;
}

static int wait_for_event(uint32_t event, const void *context)
{
	uint32_t ev;
	int err;
	struct fi_cq_err_entry comp = { 0 };
	struct fi_eq_entry entry;

	do {
		err = fi_eq_read(eq, &ev, &entry, sizeof(entry), 0);
		if (err >= 0) {
			FT_DEBUG("found eq entry %d\n", ev);
			if (ev == event) {
				if (!context || (entry.context == context))
					return FI_SUCCESS;
				else if (context)
					return -FI_EOTHER;
			}
		} else if (err != -EAGAIN) {
			return err;
		}

		err = fi_cq_read(rxcq, &comp, 1);
		if (err < 0 && err != -FI_EAGAIN)
			return err;

		err = fi_cq_read(txcq, &comp, 1);
		if (err < 0 && err != -FI_EAGAIN)
			return err;

	} while (err == -FI_EAGAIN);

	return err;
}

static int wait_for_comp(void *ctx)
{
	struct fi_cq_err_entry comp = { 0 };
	int err;

	do {
		err = fi_cq_read(rxcq, &comp, 1);
		if (err < 0 && err != -EAGAIN)
			return err;

		if (comp.op_context && comp.op_context == ctx)
			return FI_SUCCESS;

		err = fi_cq_read(txcq, &comp, 1);
		if (err < 0 && err != -EAGAIN)
			return err;

		if (comp.op_context && comp.op_context == ctx)
			return FI_SUCCESS;

	} while (err == -FI_EAGAIN);

	return err;
}

static int coll_setup_w_start_addr_stride(int start_addr, int stride)
{
	uint64_t done_flag;
	int err;

	av_set_attr.count = 0;
	av_set_attr.start_addr = start_addr;
	av_set_attr.end_addr = pm_job.num_ranks - 1;
	av_set_attr.stride = stride;

	if (!is_my_rank_participating())
		return FI_SUCCESS;

	err = fi_av_set(av, &av_set_attr, &av_set, NULL);
	if (err)
		FT_PRINTERR("fi_av_set", err);

	err = fi_av_set_addr(av_set, &world_addr);
	if (err) {
		FT_PRINTERR("failed to get collective addr - fi_av_set_addr", err);
		return err;
	}

	err = fi_join_collective(ep, world_addr, av_set, 0, &coll_mc, &done_flag);
	if (err) {
		FT_PRINTERR("fi_join_collective", err);
		return err;
	}

	return wait_for_event(FI_JOIN_COMPLETE, &done_flag);
}

static int coll_setup(void)
{
	return coll_setup_w_start_addr_stride(0, 1);
}

static int coll_setup_w_stride(void)
{
	return coll_setup_w_start_addr_stride(1, 2);
}

static int coll_teardown(void)
{
	int ret;
	if (!is_my_rank_participating())
		return FI_SUCCESS;

	ret = fi_close(&coll_mc->fid);
	if (ret)
		FT_CLOSE_FID(av_set);
	else
		ret = fi_close(&av_set->fid);
	return ret;
}

static int join_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	return FI_SUCCESS;
}

static int test_query(enum fi_collective_op coll_op, enum fi_op op,
	enum fi_datatype datatype)
{
	struct fi_collective_attr attr;

	attr.op = op;
	attr.datatype = datatype;
	attr.mode = 0;

	return fi_query_collective(domain, coll_op, &attr, 0);
}

static int barrier_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	uint64_t done_flag;
	int err;

	assert(coll_op == FI_BARRIER);

	coll_addr = fi_mc_addr(coll_mc);
	err = fi_barrier(ep, coll_addr, &done_flag);
	if (err) {
		FT_PRINTERR("collective barrier failed - fi_barrier", err);
		return err;
	}

	return wait_for_comp(&done_flag);
}

static int sum_all_reduce_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	uint64_t done_flag;
	uint64_t result = 0;
	uint64_t expect_result = 0;
	uint64_t data;
	const uint64_t base_data_value = 1234; /* any arbitrary value != 0 */
	size_t count = 1;
	uint64_t i;
	int err;

	assert(coll_op == FI_ALLREDUCE);
	assert(op == FI_SUM);
	assert(datatype == FI_UINT64);

	if (!is_my_rank_participating())
		return FI_SUCCESS;

	// Set to rank + base_data_value to make the participation of rank 0
	// verifiable
	data = base_data_value + pm_job.my_rank;

	for (i = av_set_attr.start_addr;
	     i <= av_set_attr.end_addr;
	     i += av_set_attr.stride) {
		expect_result += base_data_value + i;
	}

	coll_addr = fi_mc_addr(coll_mc);
	err = fi_allreduce(ep, &data, count, NULL, &result, NULL, coll_addr,
		FI_UINT64, FI_SUM, 0, &done_flag);
	if (err) {
		FT_PRINTERR("collective allreduce failed - fi_allreduce", err);
		return err;
	}

	err = wait_for_comp(&done_flag);
	if (err)
		return err;

	if (result == expect_result)
		return FI_SUCCESS;

	FT_DEBUG("allreduce failed; expect: %ld, actual: %ld",
		 expect_result, result);
	return -FI_ENOEQ;
}

static int all_gather_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	uint64_t done_flag;
	uint64_t *result;
	uint64_t *expect_result;
	uint64_t data = pm_job.my_rank;
	size_t count = 1;
	uint64_t i;
	int ret;

	assert(coll_op == FI_ALLGATHER);
	assert(datatype == FI_UINT64);

	result = malloc(pm_job.num_ranks * sizeof(*expect_result));
	if (!result)
		return -FI_ENOMEM;

	expect_result = malloc(pm_job.num_ranks * sizeof(*expect_result));
	if (!expect_result) {
		free(result);
		return -FI_ENOMEM;
	}

	for (i = 0; i < pm_job.num_ranks; i++)
		expect_result[i] = i;

	coll_addr = fi_mc_addr(coll_mc);
	ret = fi_allgather(ep, &data, count, NULL, result, NULL, coll_addr,
			   FI_UINT64, 0, &done_flag);
	if (ret) {
		FT_PRINTERR("collective allreduce failed:", ret);
		goto out;
	}

	ret = wait_for_comp(&done_flag);
	if (ret)
		goto out;

	for (i = 0; i < pm_job.num_ranks; i++) {
		if ((expect_result[i]) != result[i]) {
			FT_DEBUG("allgather failed; expect[%ld]: %ld, "
				 "actual[%ld]: %ld\n", i, expect_result[i],
				 i, result[i]);
			ret = -1;
			goto out;
		}
	}

	ret = FI_SUCCESS;

out:
	free(expect_result);
	free(result);
	return ret;
}

static int scatter_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	uint64_t done_flag;
	uint64_t result;
	uint64_t *data;
	uint64_t i;
	fi_addr_t root = 0;
	size_t data_size = pm_job.num_ranks * sizeof(*data);
	int err;

	assert(coll_op == FI_SCATTER);
	assert(datatype == FI_UINT64);

	data = malloc(data_size);
	if (!data)
		return -FI_ENOMEM;

	for (i = 0; i < pm_job.num_ranks; i++) {
		data[i] = i;
	}

	coll_addr = fi_mc_addr(coll_mc);
	if (pm_job.my_rank == root) {
		err = fi_scatter(ep, data, 1, NULL, &result, NULL, coll_addr,
				 root, FI_UINT64, 0, &done_flag);
	} else {
		err = fi_scatter(ep, NULL, 1, NULL, &result, NULL, coll_addr,
				 root, FI_UINT64, 0, &done_flag);
	}
	if (err) {
		FT_PRINTERR("collective scatter failed - fi_scatter", err);
		goto out;
	}

	err = wait_for_comp(&done_flag);
	if (err)
		goto out;

	if (data[pm_job.my_rank] != result) {
		FT_DEBUG("scatter failed; expect: %ld, actual: %ld",
			 data[pm_job.my_rank], result);
		err = -1;
		goto out;
	}

	err = FI_SUCCESS;

out:
	free(data);
	return err;
}

static int broadcast_test_run(enum fi_collective_op coll_op, enum fi_op op,
		enum fi_datatype datatype)
{
	uint64_t done_flag;
	uint64_t *result, *data;
	uint64_t i;
	fi_addr_t root = 0;
	size_t data_cnt = pm_job.num_ranks;
	int err;

	assert(coll_op == FI_BROADCAST);
	assert(datatype == FI_UINT64);

	result = malloc(data_cnt * sizeof(*result));
	if (!result)
		return -FI_ENOMEM;

	data = malloc(data_cnt * sizeof(*data));
	if (!data) {
		free(result);
		return -FI_ENOMEM;
	}

	for (i = 0; i < pm_job.num_ranks; ++i)
		data[i] = pm_job.num_ranks - 1 - i;

	coll_addr = fi_mc_addr(coll_mc);
	if (pm_job.my_rank == root) {
		err = fi_broadcast(ep, data, data_cnt, NULL, coll_addr,
				   root, FI_UINT64, 0, &done_flag);
	} else {
		err = fi_broadcast(ep, result, data_cnt, NULL, coll_addr,
				   root, FI_UINT64, 0, &done_flag);
	}
	if (err) {
		FT_PRINTERR("broadcast scatter failed - fi_broadcast", err);
		goto out;
	}

	err = wait_for_comp(&done_flag);
	if (err)
		goto out;

	if (pm_job.my_rank == root) {
		err = FI_SUCCESS;
		goto out;
	}

	for (i = 0; i < data_cnt; i++) {
		if (result[i] != data[i]) {
			FT_DEBUG("broadcast failed; expect: %ld, "
				 "actual: %ld\n", data[i], result[i]);
			err = -1;
			goto out;
		}
	}
	err = FI_SUCCESS;

out:
	free(data);
	free(result);
	return err;
}

struct coll_test tests[] = {
	{
		.name = "join_test",
		.setup = coll_setup,
		.run = join_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_BARRIER,
		.op = FI_NOOP,
		.datatype = FI_VOID,
	},
	{
		.name = "barrier_test",
		.setup = coll_setup,
		.run = barrier_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_BARRIER,
		.op = FI_NOOP,
		.datatype = FI_VOID,
	},
	{
		.name = "sum_all_reduce_test",
		.setup = coll_setup,
		.run = sum_all_reduce_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_ALLREDUCE,
		.op = FI_SUM,
		.datatype = FI_UINT64,
	},
	{
		.name = "sum_all_reduce_w_stride_test",
		.setup = coll_setup_w_stride,
		.run = sum_all_reduce_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_ALLREDUCE,
		.op = FI_SUM,
		.datatype = FI_UINT64,
	},
	{
		.name = "all_gather_test",
		.setup = coll_setup,
		.run = all_gather_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_ALLGATHER,
		.op = FI_NOOP,
		.datatype = FI_UINT64,
	},
	{
		.name = "scatter_test",
		.setup = coll_setup,
		.run = scatter_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_SCATTER,
		.op = FI_NOOP,
		.datatype = FI_UINT64
	},
	{
		.name = "broadcast_test",
		.setup = coll_setup,
		.run = broadcast_test_run,
		.teardown = coll_teardown,
		.coll_op = FI_BROADCAST,
		.op = FI_NOOP,
		.datatype = FI_UINT64
	},
	{
		.name = "empty_test_to_stop_the_sequence_of_execution",
		.run = NULL,
	},
};

static inline void setup_hints(void)
{
	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG | FI_COLLECTIVE;
	hints->mode = FI_CONTEXT;
	hints->domain_attr->control_progress = FI_PROGRESS_MANUAL;
	hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
}

static int multinode_setup_fabric(int argc, char **argv)
{
	char my_name[FT_MAX_CTRL_MSG];
	size_t len;
	int err;

	setup_hints();

	err = ft_getinfo(hints, &fi);
	if (err)
		return err;

	err = ft_open_fabric_res();
	if (err)
		return err;

	opts.av_size = pm_job.num_ranks;

	av_attr.type = FI_AV_TABLE;
	err = ft_alloc_active_res(fi);
	if (err)
		return err;

	err = ft_enable_ep(ep, eq, av, txcq, rxcq, txcntr, rxcntr, rma_cntr);
	if (err)
		return err;

	len = FT_MAX_CTRL_MSG;
	err = fi_getname(&ep->fid, (void *) my_name, &len);
	if (err) {
		FT_PRINTERR("error determining local endpoint name", err);
		goto errout;
	}

	pm_job.name_len = len;
	pm_job.names = malloc(len * pm_job.num_ranks);
	if (!pm_job.names) {
		FT_ERR("error allocating memory for address exchange\n");
		err = -FI_ENOMEM;
		goto errout;
	}

	err = pm_allgather(my_name, pm_job.names, pm_job.name_len);
	if (err) {
		FT_PRINTERR("error exchanging addresses", err);
		goto errout;
	}

	pm_job.fi_addrs = calloc(pm_job.num_ranks, sizeof(*pm_job.fi_addrs));
	if (!pm_job.fi_addrs) {
		FT_ERR("error allocating memory for av fi addrs\n");
		err = -FI_ENOMEM;
		goto errout;
	}

	err = fi_av_insert(av, pm_job.names, pm_job.num_ranks,
			   pm_job.fi_addrs, 0, NULL);
	if (err != pm_job.num_ranks) {
		FT_ERR("unable to insert all addresses into AV table: %d (%s)\n",
		       err, fi_strerror(err));
		err = -1;
		goto errout;
	}
	return 0;

errout:
	ft_free_res();
	return ft_exit_code(err);
}

static void pm_job_free_res(void)
{
	free(pm_job.names);
	free(pm_job.fi_addrs);
}

int multinode_run_tests(int argc, char **argv)
{
	struct coll_test *test;
	int ret = FI_SUCCESS;

	ret = multinode_setup_fabric(argc, argv);
	if (ret)
		return ret;

	for (test = tests; test->run && !ret; test++) {
		FT_DEBUG("Running Test: %s", test->name);
		ret = test_query(test->coll_op, test->op, test->datatype);
		if (ret) {
			FT_DEBUG("Test skipped: operation %s not supported.",
				test->name);
			ret = FI_SUCCESS;
			continue;
		}

		ret = test->setup();
		if (ret) {
			FT_DEBUG("Setup Failed...");
			goto out;
		}
		FT_DEBUG("Setup Complete...");

		ret = test->run(test->coll_op, test->op, test->datatype);

		if (ret) {
			FT_DEBUG("Test Failed: %s",  test->name);
			goto out;
		}

		pm_barrier();
		ret = test->teardown();
		if (ret) {
			FT_DEBUG("Teardown Failed...");
			goto out;
		}
		FT_DEBUG("Run Complete...");
		FT_DEBUG("Test Complete: %s", tests->name);
	}

out:
	if (ret)
		printf("failed\n");
	else
		printf("passed\n");

	pm_job_free_res();
	ft_free_res();
	return ft_exit_code(ret);
}
