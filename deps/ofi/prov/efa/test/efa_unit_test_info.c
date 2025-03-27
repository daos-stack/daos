/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

/**
 * @brief test that when a wrong fi_info was used to open resource, the error is handled
 * gracefully
 */
void test_info_open_ep_with_wrong_info()
{
	struct fi_info *hints, *info;
	struct fid_fabric *fabric = NULL;
	struct fid_domain *domain = NULL;
	struct fid_ep *ep = NULL;
	int err;

	hints = efa_unit_test_alloc_hints(FI_EP_DGRAM);

	err = fi_getinfo(FI_VERSION(1, 14), NULL, NULL, 0ULL, hints, &info);
	assert_int_equal(err, 0);

	/* dgram endpoint require FI_MSG_PREFIX */
	assert_int_equal(info->mode, FI_MSG_PREFIX);

	/* make the info wrong by setting the mode to 0 */
	info->mode = 0;

	err = fi_fabric(info->fabric_attr, &fabric, NULL);
	assert_int_equal(err, 0);

	err = fi_domain(fabric, info, &domain, NULL);
	assert_int_equal(err, 0);

	/* because of the error in the info object, fi_endpoint() should fail with -FI_ENODATA */
	err = fi_endpoint(domain, info, &ep, NULL);
	assert_int_equal(err, -FI_ENODATA);
	assert_null(ep);

	err = fi_close(&domain->fid);
	assert_int_equal(err, 0);

	err = fi_close(&fabric->fid);
	assert_int_equal(err, 0);
}

/**
 * @brief test that we support older version of libfabric API version 1.1
 */
void test_info_open_ep_with_api_1_1_info()
{
	struct fi_info *hints, *info;
	struct fid_fabric *fabric = NULL;
	struct fid_domain *domain = NULL;
	struct fid_ep *ep = NULL;
	int err;

	hints = calloc(sizeof(struct fi_info), 1);
	assert_non_null(hints);

	hints->domain_attr = calloc(sizeof(struct fi_domain_attr), 1);
	assert_non_null(hints->domain_attr);

	hints->fabric_attr = calloc(sizeof(struct fi_fabric_attr), 1);
	assert_non_null(hints->fabric_attr);

	hints->ep_attr = calloc(sizeof(struct fi_ep_attr), 1);
	assert_non_null(hints->ep_attr);

	hints->fabric_attr->prov_name = "efa";
	hints->ep_attr->type = FI_EP_RDM;

	/* in libfabric API < 1.5, domain_attr->mr_mode is an enum with
	 * two options: FI_MR_BASIC or FI_MR_SCALABLE, (EFA does not support FI_MR_SCALABLE).
	 *
	 * Additional information about memory registration is specified as bits in
	 * "mode". For example, the requirement of local memory registration
	 * is specified as FI_LOCAL_MR.
	 */
	hints->mode = FI_LOCAL_MR;
	hints->domain_attr->mr_mode = FI_MR_BASIC;

	err = fi_getinfo(FI_VERSION(1, 1), NULL, NULL, 0ULL, hints, &info);
	assert_int_equal(err, 0);

	err = fi_fabric(info->fabric_attr, &fabric, NULL);
	assert_int_equal(err, 0);

	err = fi_domain(fabric, info, &domain, NULL);
	assert_int_equal(err, 0);

	err = fi_endpoint(domain, info, &ep, NULL);
	assert_int_equal(err, 0);

	err = fi_close(&ep->fid);
	assert_int_equal(err, 0);

	err = fi_close(&domain->fid);
	assert_int_equal(err, 0);

	err = fi_close(&fabric->fid);
	assert_int_equal(err, 0);
}

/**
 * @brief Verify info->tx/rx_attr->msg_order is set according to hints.
 *
 */
static void
test_info_tx_rx_msg_order_from_hints(struct fi_info *hints, int expected_ret)
{
	struct fi_info *info;
	int err;

	err = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL, NULL, 0ULL, hints, &info);

	assert_int_equal(err, expected_ret);

	if (expected_ret == FI_SUCCESS) {
		assert_true(hints->tx_attr->msg_order == info->tx_attr->msg_order);
		assert_true(hints->rx_attr->msg_order == info->rx_attr->msg_order);
	}

	fi_freeinfo(info);
}

/**
 * @brief Verify info->tx/rx_attr->op_flags is set according to hints.
 *
 */
static void
test_info_tx_rx_op_flags_from_hints(struct fi_info *hints, int expected_ret)
{
	struct fi_info *info;
	int err;

	err = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL, NULL, 0ULL, hints, &info);

	assert_int_equal(err, expected_ret);

	if (expected_ret == FI_SUCCESS) {
		assert_true(hints->tx_attr->op_flags == info->tx_attr->op_flags);
		assert_true(hints->rx_attr->op_flags == info->rx_attr->op_flags);
	}

	fi_freeinfo(info);
}

/**
 * @brief Verify info->tx/rx_attr->size is set according to hints.
 *
 */
static void test_info_tx_rx_size_from_hints(struct fi_info *hints, int expected_ret)
{
	struct fi_info *info;
	int err;

	err = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL, NULL, 0ULL, hints, &info);

	assert_int_equal(err, expected_ret);

	if (expected_ret == FI_SUCCESS) {
		assert_true(hints->tx_attr->size == info->tx_attr->size);
		assert_true(hints->rx_attr->size == info->rx_attr->size);
	}

	fi_freeinfo(info);
}

void test_info_tx_rx_msg_order_rdm_order_none(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->rx_attr->msg_order = FI_ORDER_NONE;
	test_info_tx_rx_msg_order_from_hints(resource->hints, 0);
}

void test_info_tx_rx_msg_order_rdm_order_sas(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_SAS;
	resource->hints->rx_attr->msg_order = FI_ORDER_SAS;
	test_info_tx_rx_msg_order_from_hints(resource->hints, 0);
}

void test_info_tx_rx_msg_order_dgram_order_none(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_DGRAM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->rx_attr->msg_order = FI_ORDER_NONE;
	test_info_tx_rx_msg_order_from_hints(resource->hints, 0);
}

/**
 * @brief dgram endpoint doesn't support any ordering, so fi_getinfo
 * should return -FI_ENODATA if hints requests sas
 */
void test_info_tx_rx_msg_order_dgram_order_sas(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_DGRAM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_SAS;
	resource->hints->rx_attr->msg_order = FI_ORDER_SAS;
	test_info_tx_rx_msg_order_from_hints(resource->hints, -FI_ENODATA);
}

void test_info_tx_rx_op_flags_rdm(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->op_flags = FI_DELIVERY_COMPLETE;
	resource->hints->rx_attr->op_flags = FI_COMPLETION;
	test_info_tx_rx_op_flags_from_hints(resource->hints, 0);
}

void test_info_tx_rx_size_rdm(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->size = 16;
	resource->hints->rx_attr->size = 16;
	test_info_tx_rx_size_from_hints(resource->hints, 0);
}

static void test_info_check_shm_info_from_hints(struct fi_info *hints)
{
	struct fi_info *info;
	struct fid_fabric *fabric = NULL;
	struct fid_domain *domain = NULL;
	int err;
	struct efa_domain *efa_domain;

	err = fi_getinfo(FI_VERSION(1, 14), NULL, NULL, 0ULL, hints, &info);
	/* Do nothing if the current setup does not support FI_HMEM */
	if (err && hints->caps & FI_HMEM) {
		return;
	}
	assert_int_equal(err, 0);

	err = fi_fabric(info->fabric_attr, &fabric, NULL);
	assert_int_equal(err, 0);

	err = fi_domain(fabric, info, &domain, NULL);
	assert_int_equal(err, 0);

	efa_domain = container_of(domain, struct efa_domain, util_domain.domain_fid);
	if (efa_domain->shm_info) {
		if (hints->caps & FI_HMEM)
			assert_true(efa_domain->shm_info->caps & FI_HMEM);
		else
			assert_false(efa_domain->shm_info->caps & FI_HMEM);

		assert_true(efa_domain->shm_info->tx_attr->op_flags == info->tx_attr->op_flags);

		assert_true(efa_domain->shm_info->rx_attr->op_flags == info->rx_attr->op_flags);

		if (hints->domain_attr->threading) {
			assert_true(hints->domain_attr->threading == info->domain_attr->threading);
			assert_true(hints->domain_attr->threading == efa_domain->shm_info->domain_attr->threading);
		}
	}

	fi_close(&domain->fid);

	fi_close(&fabric->fid);

	fi_freeinfo(info);
}

/**
 * @brief Check shm info created by efa_domain() has correct caps.
 *
 */
void test_info_check_shm_info_hmem()
{
	struct fi_info *hints;

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	hints->caps |= FI_HMEM;
	test_info_check_shm_info_from_hints(hints);

	hints->caps &= ~FI_HMEM;
	test_info_check_shm_info_from_hints(hints);
}

void test_info_check_shm_info_op_flags()
{
	struct fi_info *hints;

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	hints->tx_attr->op_flags |= FI_COMPLETION;
	hints->rx_attr->op_flags |= FI_COMPLETION;
	test_info_check_shm_info_from_hints(hints);

	hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;
	hints->rx_attr->op_flags |= FI_MULTI_RECV;
	test_info_check_shm_info_from_hints(hints);
}

void test_info_check_shm_info_threading()
{
	struct fi_info *hints;

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	hints->domain_attr->threading = FI_THREAD_DOMAIN;
	test_info_check_shm_info_from_hints(hints);
}

/**
 * @brief Check that case when a user requested FI_HMEM support
 *        using libfabric API < 1.18,
 */
void test_info_check_hmem_cuda_support_on_api_lt_1_18()
{
	struct fi_info *hints, *info = NULL;
	int err;

	if (!hmem_ops[FI_HMEM_CUDA].initialized)
		skip();

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	hints->caps |= FI_HMEM;
	hints->domain_attr->mr_mode |= FI_MR_HMEM;

	/* For libfabric API < 1.18,
	 * on a system that support GPUDirect RDMA read,
	 * HMEM cuda is available when GPUDirect RDMA is available,
	 * and environment variable FI_EFA_USE_DEVICE_RDMA set to 1/on/true
	 * otherwise it is not available.
	 */
	setenv("FI_EFA_USE_DEVICE_RDMA", "1", true /* overwrite */);
	err = fi_getinfo(FI_VERSION(1, 6), NULL, NULL, 0, hints, &info);
	if (efa_device_support_rdma_read()) {
		assert_int_equal(err, 0);
		fi_freeinfo(info);
	} else {
		assert_int_equal(err, -FI_ENODATA);
	}

	setenv("FI_EFA_USE_DEVICE_RDMA", "0", true /* overwrite */);
	err = fi_getinfo(FI_VERSION(1, 14), NULL, NULL, 0, hints, &info);
	assert_int_equal(err, -FI_ENODATA);

	unsetenv("FI_EFA_USE_DEVICE_RDMA");
}

/**
 * @brief Check that case when a user requested FI_HMEM support
 *        using libfabric API >= 1.18,
 */
void test_info_check_hmem_cuda_support_on_api_ge_1_18()
{
	struct fi_info *hints, *info = NULL;
	int err;

	if (!hmem_ops[FI_HMEM_CUDA].initialized)
		skip();

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	hints->caps |= FI_HMEM;
	hints->domain_attr->mr_mode |= FI_MR_HMEM;

	/* Piror to version 1.18, libfabric EFA provider support CUDA
	 * memory only when GPUDirect RDMA is available.
	 * In version 1.18, libfabric EFA provider implemented
	 * universal CUDA support through CUDA library.
	 * However, this feature (universal CUDA support) can cause some
	 * middleware to deadlock, thus it is only available
	 * when a user is using 1.18 API.
	 */
	err = fi_getinfo(FI_VERSION(1, 18), NULL, NULL, 0, hints, &info);
	assert_int_equal(err, 0);
	fi_freeinfo(info);
}

/**
 * @brief Check that EFA does not claim support of FI_HMEM when
 *        it is not requested
 */
void test_info_check_no_hmem_support_when_not_requested()
{
	struct fi_info *hints, *info = NULL;
	int err;

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	err = fi_getinfo(FI_VERSION(1,6), NULL, NULL, 0, hints, &info);
	assert_int_equal(err, 0);
	assert_non_null(info);
	assert_false(info->caps & FI_HMEM);
	fi_freeinfo(info);
}

/**
 * @brief core test function for use_device_rdma
 *
 * @param env_val 0/1/-1: set FI_EFA_USE_DEVICE_RDMA to 0 or 1, or leave it unset (-1)
 * @param setopt_val 0/1/-1: set use_device_rdma using fi_setopt to 0 or 1, or leave it unset (-1)
 * @param expected_val expected result of ep->use_device_rdma
 * @param api_version API version to use.
 */
void test_use_device_rdma( const int env_val,
			   const int setopt_val,
			   const int expected_val,
			   const uint32_t api_version )
{
	int ret = 0;
	struct fi_info *hints, *info;
	struct fid_fabric *fabric = NULL;
	struct fid_domain *domain = NULL;
	struct fid_ep *ep = NULL;
	struct efa_rdm_ep *efa_rdm_ep;
	bool rdma_capable_hw;
	char env_str[16];

	if (env_val >= 0) {
		snprintf(env_str, 15, "%d", env_val);
		setenv("FI_EFA_USE_DEVICE_RDMA", env_str, 1);
	} else {
		unsetenv("FI_EFA_USE_DEVICE_RDMA");
	}

	hints = efa_unit_test_alloc_hints(FI_EP_RDM);

	ret = fi_getinfo(api_version, NULL, NULL, 0ULL, hints, &info);
	assert_int_equal(ret, 0);

	rdma_capable_hw = efa_device_support_rdma_read();

	if (expected_val && !rdma_capable_hw) {
		/* cannot test USE_DEVICE_RDMA=1, hardware
		   doesn't support it, and will abort() */
		fi_freeinfo(info);
		skip();
		return;
	}

	ret = fi_fabric(info->fabric_attr, &fabric, NULL);
	assert_int_equal(ret, 0);

	ret = fi_domain(fabric, info, &domain, NULL);
	assert_int_equal(ret, 0);

	fi_endpoint(domain, info, &ep, NULL);
	assert_int_equal(ret, 0);

	if (setopt_val >= 0) {
		bool b_val = (bool) setopt_val;
		int ret_setopt;
		ret_setopt = fi_setopt(&ep->fid, FI_OPT_ENDPOINT,
			FI_OPT_EFA_USE_DEVICE_RDMA, &b_val, sizeof(bool));
		if (FI_VERSION_LT(api_version, FI_VERSION(1,18))) {
			assert_int_not_equal(ret_setopt, 0);
		}
		else if (expected_val != setopt_val) {
			assert_int_not_equal(ret_setopt, 0);
		}
		else {
			assert_int_equal(ret_setopt, 0);
		}
	}

	efa_rdm_ep = container_of(ep, struct efa_rdm_ep,
			base_ep.util_ep.ep_fid.fid);
	assert_int_equal( efa_rdm_ep->use_device_rdma, expected_val );

	assert_int_equal(fi_close(&ep->fid), 0);
	assert_int_equal(fi_close(&domain->fid), 0);
	assert_int_equal(fi_close(&fabric->fid), 0);
	fi_freeinfo(info);

	return;
}

/* indicates the test shouldn't set the setopt or environment
   variable during setup. */
const int VALUE_NOT_SET = -1;

/* settings agree: on */
void test_efa_use_device_rdma_env1_opt1() {
	test_use_device_rdma(1, 1, 1, FI_VERSION(1,18));
}
/* settings agree: off */
void test_efa_use_device_rdma_env0_opt0() {
	test_use_device_rdma(0, 0, 0, FI_VERSION(1,18));
}
/* settings conflict, env on */
void test_efa_use_device_rdma_env1_opt0() {
	test_use_device_rdma(1, 0, 1, FI_VERSION(1,18));
}
/* settings conflict, env off */
void test_efa_use_device_rdma_env0_opt1() {
	test_use_device_rdma(0, 1, 0, FI_VERSION(1,18));
}
/* setopt only on */
void test_efa_use_device_rdma_opt1() {
	test_use_device_rdma(VALUE_NOT_SET, 1, 1, FI_VERSION(1,18));
}
/* setopt only off */
void test_efa_use_device_rdma_opt0() {
	test_use_device_rdma(VALUE_NOT_SET, 0, 0, FI_VERSION(1,18));
}
/* environment only on */
void test_efa_use_device_rdma_env1() {
	test_use_device_rdma(1, VALUE_NOT_SET, 1, FI_VERSION(1,18));
}
/* environment only off */
void test_efa_use_device_rdma_env0() {
	test_use_device_rdma(0, VALUE_NOT_SET, 0, FI_VERSION(1,18));
}
/* setopt rejected in 1,17 */
void test_efa_use_device_rdma_opt_old() {
	test_use_device_rdma(1, 1, 1, FI_VERSION(1,17));
	test_use_device_rdma(0, 0, 0, FI_VERSION(1,17));
}
