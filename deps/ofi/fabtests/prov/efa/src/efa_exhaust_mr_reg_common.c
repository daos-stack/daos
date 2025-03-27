/* Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_exhaust_mr_reg_common.h"

int ft_efa_open_ibv_device(struct ibv_context **ctx) {
	int num_dev = 0;
	struct ibv_device **dev_list;

	dev_list = ibv_get_device_list(&num_dev);
	if (num_dev < 1) {
		FT_ERR("No ibv devices found");
		ibv_free_device_list(dev_list);
		return -1;
	} else if (num_dev > 1) {
		FT_WARN("More than 1 ibv devices found! This test will only "
			"exhaust MRs on the first device");
	}
	*ctx = ibv_open_device(dev_list[0]);
	ibv_free_device_list(dev_list);
	return 0;
}

int ft_efa_close_ibv_device(struct ibv_context *ctx) {
	return ibv_close_device(ctx);
}

int ft_efa_get_max_mr(struct ibv_context *ctx) {
	int ret;
	struct ibv_device_attr dev_attr = {0};
	ret = ibv_query_device(ctx, &dev_attr);
	if (ret)
		FT_ERR("ibv_query_device failed with %d\n", ret);
	return dev_attr.max_mr;
}

int ft_efa_setup_ibv_pd(struct ibv_context *ctx, struct ibv_pd **pd)
{
	*pd = ibv_alloc_pd(ctx);
	if (!*pd) {
		FT_ERR("alloc_pd failed with error %d\n", errno);
		return EXIT_FAILURE;
	}
	return FI_SUCCESS;
}

void ft_efa_destroy_ibv_pd(struct ibv_pd *pd)
{
	int err;

	err = ibv_dealloc_pd(pd);
	if (err) {
		FT_ERR("deallloc_pd failed with error %d\n", errno);
	}
}

int ft_efa_register_mr_reg(struct ibv_pd *pd, void **buffers, size_t buf_size,
			   struct ibv_mr **mr_reg_vec, size_t count, size_t *registered)
{
	int i, err = 0;

	for (i = 0; i < count; i++) {
		mr_reg_vec[i] = ibv_reg_mr(pd, buffers[i], buf_size,
					   IBV_ACCESS_LOCAL_WRITE);
		if (!mr_reg_vec[i]) {
			FT_ERR("reg_mr index %d failed with errno %d\n", i,
			       errno);
			err = errno;
			goto out;
		}
		if (i % 50000 == 0) {
			printf("Registered %d MRs...\n", i+1);
		}
	}
out:
	*registered = i;
	printf("Registered %d MRs\n", i+1);
	return err;
}

int ft_efa_deregister_mr_reg(struct ibv_mr **mr_reg_vec, size_t count)
{
	int i, err = 0;

	for (i = 0; i < count; i++) {
		if (mr_reg_vec[i])
			err = ibv_dereg_mr(mr_reg_vec[i]);
		if (err) {
			FT_ERR("dereg_mr index %d failed with errno %d\n", i,
			       errno);
		}
		if (i % 50000 == 0) {
			printf("Deregistered %d MRs...\n", i+1);
		}
	}
	printf("Deregistered %ld MRs\n", count);
	return err;
}

int ft_efa_alloc_bufs(void **buffers, size_t buf_size, size_t count, size_t *alloced) {
	int i;
	int ret = FI_SUCCESS;

	for (i = 0; i < count; i++) {
		buffers[i] = malloc(buf_size);
		if (!buffers[i]) {
			FT_ERR("malloc failed!\n");
			ret = EXIT_FAILURE;
			goto out;
		}
	}

out:
	*alloced = i;
	return ret;
}

void ft_efa_free_bufs(void **buffers, size_t count) {
	int i;

	for (i = 0; i < count; i++)
		free(buffers[i]);
}

int ft_efa_unexpected_pingpong(void)
{
	int ret, i;

	opts.options |= FT_OPT_OOB_CTRL;

	ret = ft_sync();
	if (ret)
		return ret;

	for (i = 0; i < opts.iterations + opts.warmup_iterations; i++) {
		if (i == opts.warmup_iterations)
			ft_start();

		ret = ft_post_tx(ep, remote_fi_addr, opts.transfer_size, NO_CQ_DATA, &tx_ctx);
		if (ret)
			return ret;

		ft_sync();

		ret = ft_get_rx_comp(rx_seq);
		if (ret)
			return ret;

		ret = ft_post_rx(ep, rx_size, &rx_ctx);
		if (ret)
			return ret;

		ret = ft_get_tx_comp(tx_seq);
		if (ret)
			return ret;
	}

	ft_stop();

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end,
			     2, opts.argc, opts.argv);
	else
		show_perf(NULL, opts.transfer_size, opts.iterations, &start,
			  &end, 2);

	return 0;
}
