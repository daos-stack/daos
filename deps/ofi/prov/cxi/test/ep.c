/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(ep, .init = cxit_setup_ep, .fini = cxit_teardown_ep,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic EP creation */
Test(ep, simple)
{
	cxit_create_ep();

	cxit_destroy_ep();
}

/* Test NULL parameter passed with EP creation */
Test(ep, ep_null_info)
{
	int ret;

	ret = fi_endpoint(cxit_domain, NULL, &cxit_ep, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Failure with NULL info. %d", ret);
}

/* Test NULL parameter passed with EP creation */
Test(ep, ep_null_ep)
{
	int ret;

	ret = fi_endpoint(cxit_domain, cxit_fi, NULL, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Failure with NULL ep. %d", ret);
}

struct ep_test_params {
	void *context;
	enum fi_ep_type type;
	int retval;
};

static struct ep_test_params ep_ep_params[] = {
	{.type = FI_EP_RDM,
		.retval = FI_SUCCESS},
	{.type = FI_EP_UNSPEC,
		.retval = FI_SUCCESS},
	{.type = FI_EP_MSG,
		.retval = -FI_EINVAL},
	{.type = FI_EP_DGRAM,
		.retval = -FI_EINVAL},
	{.type = FI_EP_RDM,
		.context = (void *)0xabcdef,
		.retval = FI_SUCCESS},
};

ParameterizedTestParameters(ep, fi_ep_types)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(ep_ep_params);
	return cr_make_param_array(struct ep_test_params, ep_ep_params,
				   param_sz);
}

ParameterizedTest(struct ep_test_params *param, ep, fi_ep_types)
{
	int ret;
	struct cxip_ep *cep;

	cxit_fi->ep_attr->type = param->type;
	cxit_ep = NULL;
	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, param->context);
	cr_assert_eq(ret, param->retval,
		     "fi_endpoint() error for type %d. %d != %d",
		     param->type, ret, param->retval);

	if (ret != FI_SUCCESS)
		return;

	cr_assert_not_null(cxit_ep);
	cr_expect_eq(cxit_ep->fid.fclass, FI_CLASS_EP);
	cr_expect_eq(cxit_ep->fid.context, param->context);
	cep = container_of(cxit_ep, struct cxip_ep, ep);
	cr_expect_not_null(cep->ep_obj);

	cxit_destroy_ep();
}

/* Test Passive EP creation is not supported */
Test(ep, passive_ep)
{
	int ret;
	struct fid_pep *pep = NULL;

	ret = fi_passive_ep(cxit_fabric, cxit_fi, &pep, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "Failure with fi_passive_ep. %d", ret);
	cr_assert_null(pep);
}

Test(ep, ep_bind_null_bind_obj)
{
	int ret;

	cxit_create_ep();

	ret = fi_ep_bind(cxit_ep, NULL, 0);
	cr_assert_eq(ret, -FI_EINVAL);

	cxit_destroy_ep();
}

Test(ep, ep_bind_invalid_fclass)
{
	int ret;

	cxit_create_ep();
	cxit_create_av();

	/* try to bind an unsupported class type */
	cxit_ep->fid.fclass = FI_CLASS_PEP;
	ret = fi_ep_bind(cxit_ep, &cxit_av->fid, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	cxit_ep->fid.fclass = FI_CLASS_EP;

	cxit_destroy_av();
	cxit_destroy_ep();
}

Test(ep, ep_bind_av)
{
	struct cxip_ep *ep;
	struct cxip_av *av;

	cxit_create_ep();
	cxit_create_av();

	cxit_bind_av();

	av = container_of(cxit_av, struct cxip_av, av_fid.fid);
	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);

	cr_assert_not_null(ep->ep_obj);
	cr_assert_eq(ep->ep_obj->av, av);

	cxit_destroy_ep();
	cxit_destroy_av();
}

Test(ep, ep_bind_eq)
{
	int ret;

	/* order is not important */
	cxit_create_eq();
	cxit_create_ep();

	ret = fi_ep_bind(cxit_ep, &cxit_eq->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_eq_bind EQ failed %d", ret);

	/* order is important */
	cxit_destroy_ep();
	cxit_destroy_eq();
}

Test(ep, ep_bind_mr)
{
	int ret;

	/*
	 * At the time of implementing this test MRs were not supported by the
	 * CXI provider. Fake attempting to register a MR with a EP using an AV
	 */
	cxit_create_ep();
	cxit_create_av();

	cxit_av->fid.fclass = FI_CLASS_MR;
	ret = fi_ep_bind(cxit_ep, &cxit_av->fid, 0);
	cr_assert_eq(ret, -FI_EINVAL, "Bind (fake) MR to EP. %d", ret);
	cxit_av->fid.fclass = FI_CLASS_AV;

	cxit_destroy_ep();
	cxit_destroy_av();
}

Test(ep, ep_bind_cq)
{
	struct cxip_ep *ep;
	struct cxip_cq *rx_cq, *tx_cq;

	cxit_create_ep();
	cxit_create_cqs();
	cr_assert_not_null(cxit_tx_cq);
	cr_assert_not_null(cxit_rx_cq);

	cxit_bind_cqs();

	rx_cq = container_of(cxit_rx_cq, struct cxip_cq, util_cq.cq_fid.fid);
	tx_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid.fid);
	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);

	cr_assert_not_null(ep->ep_obj);
	cr_assert_eq(ep->ep.fid.fclass, FI_CLASS_EP);
	cr_assert_eq(ep->ep_obj->txc->send_cq, tx_cq);
	cr_assert_eq(ep->ep_obj->rxc->recv_cq, rx_cq);

	cxit_destroy_ep();
	cxit_destroy_cqs();
}

Test(ep, ep_bind_cq_eps)
{
	struct fid_ep *fid_ep2;
	struct cxip_ep *ep;
	struct cxip_ep *ep2;
	int ret;

	cxit_create_ep();
	cxit_create_cqs();
	cr_assert_not_null(cxit_tx_cq);
	cr_assert_not_null(cxit_rx_cq);

	cxit_bind_cqs();

	/* Create second EP */
	ret = fi_endpoint(cxit_domain, cxit_fi, &fid_ep2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_endpoint");
	cr_assert_not_null(fid_ep2);

	/* Bind same CQs to second EP */
	ret = fi_ep_bind(fid_ep2, &cxit_tx_cq->fid, cxit_tx_cq_bind_flags);
	cr_assert(!ret, "fi_ep_bind TX CQ to 2nd EP");

	ret = fi_ep_bind(fid_ep2, &cxit_rx_cq->fid, cxit_rx_cq_bind_flags);
	cr_assert(!ret, "fi_ep_bind RX CQ to 2nd EP");

	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	cr_assert_not_null(ep->ep_obj);
	ep2 = container_of(fid_ep2, struct cxip_ep, ep.fid);
	cr_assert_not_null(ep2->ep_obj);

	cr_assert_eq(ep->ep_obj->txc->send_cq, ep2->ep_obj->txc->send_cq,
		     "Send CQ mismatch");
	cr_assert_eq(ep->ep_obj->rxc->recv_cq, ep2->ep_obj->rxc->recv_cq,
		     "Receive CQ mismatch");

	ret = fi_close(&fid_ep2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close endpoint");

	cxit_destroy_ep();
	cxit_destroy_cqs();
}

Test(ep, ep_bind_cntr)
{
	int ret;

	cxit_create_ep();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_av();
	cxit_bind_av();

	cxit_create_cntrs();
	cxit_bind_cntrs();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS);

	cxit_destroy_ep();
	cxit_destroy_cntrs();
	cxit_destroy_av();
	cxit_destroy_cqs();
}

Test(ep, ep_bind_stx_ctx)
{
	int ret;
	struct fi_tx_attr *attr = NULL;
	void *context = NULL;

	ret = fi_stx_context(cxit_domain, attr, NULL, context);
	cr_assert_eq(ret, -FI_ENOSYS,
		     "TODO Add test for STX CTXs binding to the endpoint when implemented");
}

Test(ep, ep_bind_srx_ctx)
{
	int ret;
	struct fi_rx_attr *attr = NULL;
	void *context = NULL;

	ret = fi_srx_context(cxit_domain, attr, NULL, context);
	cr_assert_eq(ret, -FI_ENOSYS,
		     "TODO Add test for SRX CTXs binding to the endpoint when implemented");
}

Test(ep, ep_bind_unhandled)
{
	int ret;

	cxit_create_ep();
	cxit_create_av();

	/* Emulate a different type of object type */
	cxit_av->fid.fclass = -1;
	ret = fi_ep_bind(cxit_ep, &cxit_av->fid, 0);
	cr_assert_eq(ret, -FI_EINVAL, "fi_ep_bind unhandled object. %d", ret);
	cxit_av->fid.fclass = FI_CLASS_AV;

	cxit_destroy_ep();
	cxit_destroy_av();
}

Test(ep, cancel_ep)
{
	int ret;

	cxit_create_ep();

	ret = fi_cancel(&cxit_ep->fid, NULL);
	cr_assert_eq(ret, -FI_EOPBADSTATE);

	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_cancel(&cxit_ep->fid, NULL);
	cr_assert_eq(ret, -FI_ENOENT);

	ret = fi_cancel(&cxit_ep->fid, (void *)1);
	cr_assert_eq(ret, -FI_ENOENT);

	cxit_destroy_ep();
	cxit_destroy_av();
	cxit_destroy_cqs();
}

Test(ep, cancel_unhandled)
{
	int ret;

	cxit_create_ep();

	/* Emulate a different type of object type */
	cxit_ep->fid.fclass = FI_CLASS_PEP;
	ret = fi_cancel(&cxit_ep->fid, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	cxit_ep->fid.fclass = FI_CLASS_EP;

	cxit_destroy_ep();
}

Test(ep, control_unhandled_obj)
{
	int ret;

	cxit_create_ep();

	/* Emulate a different type of object type */
	cxit_ep->fid.fclass = FI_CLASS_PEP;
	ret = fi_control(&cxit_ep->fid, -1, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	cxit_ep->fid.fclass = FI_CLASS_EP;

	cxit_destroy_ep();
}

Test(ep, control_unhandled_cmd)
{
	int ret;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, -1, NULL);
	cr_assert_eq(ret, -FI_EINVAL);

	cxit_destroy_ep();
}

Test(ep, control_null_fid_alias)
{
	int ret;
	struct fi_alias alias = {0};

	cxit_create_ep();

	/* A null alias.fid causes -FI_EINVAL */
	ret = fi_control(&cxit_ep->fid, FI_ALIAS, &alias);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_ALIAS. %d", ret);

	cxit_destroy_ep();
}

Test(ep, control_empty_alias)
{
	int ret;
	struct fi_alias alias = {0};
	struct fid *alias_fid;

	cxit_create_ep();

	/* Empty alias.flags causes -FI_EINVAL */
	alias.fid = &alias_fid;
	ret = fi_control(&cxit_ep->fid, FI_ALIAS, &alias);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_ALIAS. %d", ret);

	cxit_destroy_ep();
}

Test(ep, control_bad_flags_alias)
{
	int ret;
	struct fi_alias alias = {0};

	cxit_create_ep();

	/* Both Tx and Rx flags causes -FI_EINVAL */
	alias.flags = FI_TRANSMIT | FI_RECV;
	ret = fi_control(&cxit_ep->fid, FI_ALIAS, &alias);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_ALIAS. %d", ret);

	cxit_destroy_ep();
}

Test(ep, control_tx_flags_alias)
{
	int ret;
	struct fi_alias alias = {0};
	struct fid *alias_fid = NULL;
	struct cxip_ep *cxi_ep, *alias_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	alias.fid = &alias_fid;
	alias.flags = FI_TRANSMIT;
	ret = fi_control(&cxit_ep->fid, FI_ALIAS, &alias);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_ALIAS. %d", ret);
	cr_assert_not_null(alias_fid);

	/* verify alias vs cxit_ep */
	alias_ep = container_of(alias_fid, struct cxip_ep, ep.fid);
	cr_assert_eq(alias_ep->ep_obj, cxi_ep->ep_obj, "EP Attr");
	cr_assert_eq(alias_ep->is_alias, 1, "EP is_alias");
	cr_assert_eq(ofi_atomic_get32(&cxi_ep->ep_obj->ref), 1, "EP refs 1");

	/* close alias */
	ret = fi_close(alias_fid);
	cr_assert(ret == FI_SUCCESS, "fi_close endpoint");
	alias_fid = NULL;
	cr_assert_eq(ofi_atomic_get32(&cxi_ep->ep_obj->ref), 0, "EP refs 0");

	cxit_destroy_ep();
}

Test(ep, control_rx_flags_alias)
{
	int ret;
	struct fi_alias alias = {0};
	struct fid *alias_fid = NULL;
	struct cxip_ep *cxi_ep, *alias_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	alias.fid = &alias_fid;
	alias.flags = FI_RECV;
	ret = fi_control(&cxit_ep->fid, FI_ALIAS, &alias);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_ALIAS. %d", ret);
	cr_assert_not_null(alias_fid);

	alias_ep = container_of(alias_fid, struct cxip_ep, ep.fid);
	cr_assert_eq(alias_ep->ep_obj, cxi_ep->ep_obj, "EP Attr");
	cr_assert_eq(alias_ep->is_alias, 1, "EP is_alias");
	cr_assert_not_null(cxi_ep->ep_obj, "EP attr NULL");
	cr_assert_eq(ofi_atomic_get32(&cxi_ep->ep_obj->ref), 1, "EP refs 1");

	/* close alias */
	ret = fi_close(alias_fid);
	cr_assert(ret == FI_SUCCESS, "fi_close endpoint");
	alias_fid = NULL;
	cr_assert_eq(ofi_atomic_get32(&cxi_ep->ep_obj->ref), 0, "EP refs 0");

	cxit_destroy_ep();
}

Test(ep, control_getopsflag_both_tx_rx)
{
	int ret;
	uint64_t flags = FI_TRANSMIT | FI_RECV;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_GETOPSFLAG TX/RX. %d",
		     ret);

	cxit_destroy_ep();
}

Test(ep, control_getopsflag_no_flags)
{
	int ret;
	uint64_t flags = FI_TRANSMIT | FI_RECV;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_GETOPSFLAG 0. %d", ret);

	cxit_destroy_ep();
}

Test(ep, control_getopsflag_tx)
{
	int ret;
	uint64_t flags = FI_TRANSMIT;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_GETOPSFLAG TX. %d", ret);
	cr_assert_eq(cxi_ep->tx_attr.op_flags, flags,
		     "fi_control FI_GETOPSFLAG Flag mismatch. %" PRIx64 " != %"
		     PRIx64 " ", cxi_ep->tx_attr.op_flags, flags);

	cxit_destroy_ep();
}

Test(ep, control_getopsflag_rx)
{
	int ret;
	uint64_t flags = FI_RECV;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_GETOPSFLAG RX. %d", ret);
	cr_assert_eq(cxi_ep->rx_attr.op_flags, flags,
		     "fi_control FI_GETOPSFLAG Flag mismatch. %" PRIx64 " != %"
		     PRIx64 " ", cxi_ep->rx_attr.op_flags, flags);

	cxit_destroy_ep();
}

Test(ep, control_setopsflag_both_tx_rx)
{
	int ret;
	uint64_t flags = FI_TRANSMIT | FI_RECV;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_SETOPSFLAG TX/RX. %d",
		     ret);

	cxit_destroy_ep();
}

Test(ep, control_setopsflag_no_flags)
{
	int ret;
	uint64_t flags = FI_TRANSMIT | FI_RECV;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, -FI_EINVAL, "fi_control FI_SETOPSFLAG 0. %d", ret);

	cxit_destroy_ep();
}

Test(ep, control_setopsflag_tx)
{
	int ret;
	uint64_t flags = (FI_TRANSMIT | FI_MSG | FI_TRIGGER |
			  FI_DELIVERY_COMPLETE);
	uint64_t tx_flags;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_SETOPSFLAG TX. %d", ret);
	flags &= ~FI_TRANSMIT;
	tx_flags = cxi_ep->tx_attr.op_flags;
	cr_assert_eq(tx_flags, flags,
		     "fi_control FI_SETOPSFLAG TX Flag mismatch. %" PRIx64
		     " != %" PRIx64, tx_flags, flags);

	cxit_destroy_ep();
}

Test(ep, control_setopsflag_tx_complete)
{
	int ret;
	uint64_t flags = FI_TRANSMIT | FI_MSG | FI_TRIGGER | FI_AFFINITY;
	uint64_t tx_flags;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_SETOPSFLAG TX. %d", ret);
	flags &= ~FI_TRANSMIT;
	flags |= FI_TRANSMIT_COMPLETE;
	tx_flags = cxi_ep->tx_attr.op_flags;
	cr_assert_eq(tx_flags, flags,
		     "fi_control FI_SETOPSFLAG TXcomp Flag mismatch. %" PRIx64
		     " != %" PRIx64, tx_flags, flags);

	cxit_destroy_ep();
}

Test(ep, control_setopsflag_rx)
{
	int ret;
	uint64_t flags = FI_RECV | FI_TAGGED | FI_NUMERICHOST | FI_EVENT;
	uint64_t rx_flags;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_SETOPSFLAG RX. %d", ret);
	flags &= ~FI_RECV;
	rx_flags = cxi_ep->rx_attr.op_flags;
	cr_assert_eq(rx_flags, flags,
		     "fi_control FI_SETOPSFLAG RX Flag mismatch. %" PRIx64
		     " != %" PRIx64, rx_flags, flags);

	cxit_destroy_ep();
}

Test(ep, control_enable_nocq)
{
	int ret;

	cxit_create_ep();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert_eq(ret, -FI_ENOCQ, "fi_enable. %d", ret);

	cxit_destroy_ep();
	cxit_destroy_av();
}

Test(ep, control_enable_noav)
{
	int ret;

	cxit_create_ep();
	cxit_create_cqs();
	cxit_bind_cqs();

	ret = fi_enable(cxit_ep);
	cr_assert_eq(ret, -FI_ENOAV, "fi_enable. %d", ret);

	cxit_destroy_ep();
	cxit_destroy_cqs();
}

Test(ep, control_enable)
{
	int ret;

	cxit_create_ep();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert_eq(ret, FI_SUCCESS, "fi_enable. %d", ret);

	cxit_destroy_ep();
	cxit_destroy_av();
	cxit_destroy_cqs();
}

struct ep_ctrl_null_params {
	int command;
	int retval;
};

ParameterizedTestParameters(ep, ctrl_null_arg)
{
	size_t param_sz;

	static struct ep_ctrl_null_params ep_null_params[] = {
		{.command = -1,
		 .retval = -FI_EINVAL},
		{.command = FI_SETOPSFLAG,
		 .retval = -FI_EINVAL},
		{.command = FI_ENABLE,
		 .retval = -FI_ENOAV},
	};

	param_sz = ARRAY_SIZE(ep_null_params);
	return cr_make_param_array(struct ep_ctrl_null_params, ep_null_params,
				   param_sz);
}

ParameterizedTest(struct ep_ctrl_null_params *param, ep, ctrl_null_arg)
{
	int ret;

	cxit_create_ep();

	ret = fi_control(&cxit_ep->fid, param->command, NULL);
	cr_assert_eq(ret, param->retval, "fi_control type %d. %d != %d",
		     param->command, ret, param->retval);

	cxit_destroy_ep();
}

struct ep_getopt_args {
	int level;
	int optname;
	size_t *optval;
	size_t *optlen;
	int retval;
};

static size_t optvalue;
static size_t optlength = sizeof(size_t);
static struct ep_getopt_args ep_null_params[] = {
	{.level = -1,
	 .retval = -FI_ENOPROTOOPT},
	{.level = FI_OPT_ENDPOINT,
	 .optname = FI_OPT_CM_DATA_SIZE,
	 .retval = -FI_ENOPROTOOPT},
	{.level = FI_OPT_ENDPOINT,
	 .optname = -1,
	 .retval = -FI_ENOPROTOOPT},
	{.level = FI_OPT_ENDPOINT,
	 .optname = FI_OPT_MIN_MULTI_RECV,
	 .optval = NULL,
	 .optlen = NULL,
	 .retval = -FI_EINVAL},
	{.level = FI_OPT_ENDPOINT,
	 .optname = FI_OPT_MIN_MULTI_RECV,
	 .optval = &optvalue,
	 .optlen = NULL,
	 .retval = -FI_EINVAL},
	{.level = FI_OPT_ENDPOINT,
	 .optname = FI_OPT_MIN_MULTI_RECV,
	 .optval = &optvalue,
	 .optlen = &optlength,
	 .retval = FI_SUCCESS},
};

ParameterizedTestParameters(ep, getopt_args)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(ep_null_params);
	return cr_make_param_array(struct ep_getopt_args, ep_null_params,
				   param_sz);
}

ParameterizedTest(struct ep_getopt_args *param, ep, getopt_args)
{
	int ret;
	struct cxip_ep *cxi_ep;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_getopt(&cxit_ep->fid, param->level, param->optname,
			(void *)param->optval, param->optlen);
	cr_assert_eq(ret, param->retval,
		     "fi_getopt lvl %d name %d val %p len %p. %d != %d",
		     param->level, param->optname, param->optval,
		     param->optlen, ret, param->retval);

	if (ret == FI_SUCCESS) {
		cr_assert_not_null(cxi_ep->ep_obj);
		cr_assert_eq(*param->optval,
			     cxi_ep->ep_obj->rxc->min_multi_recv,
			     "fi_getopt val mismatch. %zd != %zd",
			     *param->optval,
			     cxi_ep->ep_obj->rxc->min_multi_recv);
		cr_assert_eq(*param->optlen, sizeof(size_t),
			     "fi_getopt len mismatch. %zd != %zd",
			     *param->optlen, sizeof(size_t));
	}

	cxit_destroy_ep();
}

struct ep_setopt_args {
	int level;
	int optname;
	size_t optval;
	size_t optlen;
	int retval;
};

ParameterizedTestParameters(ep, setopt_args)
{
	size_t param_sz;

	static struct ep_setopt_args ep_null_params[] = {
		{.level = -1,
		.retval = -FI_ENOPROTOOPT},
		{.level = FI_OPT_ENDPOINT,
		.optname = FI_OPT_CM_DATA_SIZE,
		.retval = -FI_ENOPROTOOPT},
		{.level = FI_OPT_ENDPOINT,
		.optname = -1,
		.retval = -FI_ENOPROTOOPT},
		{.level = FI_OPT_ENDPOINT,
		.optname = FI_OPT_MIN_MULTI_RECV,
		.optval = 0,
		.retval = -FI_EINVAL},
		{.level = FI_OPT_ENDPOINT,
		.optname = FI_OPT_MIN_MULTI_RECV,
		.optval = 26,
		.retval = FI_SUCCESS},
		{.level = FI_OPT_ENDPOINT,
		.optname = FI_OPT_MIN_MULTI_RECV,
		.optval = 90001,
		.retval = FI_SUCCESS},
		{.level = FI_OPT_ENDPOINT,
		.optname = FI_OPT_MIN_MULTI_RECV,
		.optval = 1<<24,
		.retval = -FI_EINVAL},
	};

	param_sz = ARRAY_SIZE(ep_null_params);
	return cr_make_param_array(struct ep_setopt_args, ep_null_params,
				   param_sz);
}

ParameterizedTest(struct ep_setopt_args *param, ep, setopt_args)
{
	int ret;
	struct cxip_ep *cxi_ep;
	void *val = NULL;

	if (param->optval != 0)
		val = &param->optval;

	cxit_create_ep();

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_setopt(&cxit_ep->fid, param->level, param->optname,
			val, param->optlen);
	cr_assert_eq(ret, param->retval,
		     "fi_setopt lvl %d name %d val %zd. %d != %d",
		     param->level, param->optname, param->optval,
		     ret, param->retval);

	if (ret == FI_SUCCESS) {
		cr_assert_not_null(cxi_ep->ep_obj);
		cr_assert_eq(param->optval,
			     cxi_ep->ep_obj->rxc->min_multi_recv,
			     "fi_setopt val mismatch. %zd != %zd",
			     param->optval,
			     cxi_ep->ep_obj->rxc->min_multi_recv);
	}

	cxit_destroy_ep();
}

Test(ep, rx_ctx_ep)
{
	int ret;

	cxit_create_ep();

	/* RX context doesn't work with anything but scalable eps */
	ret = fi_rx_context(cxit_ep, 0, NULL, NULL, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "fi_rx_context bad ep. %d", ret);

	cxit_destroy_ep();
}

Test(ep, tx_ctx_ep)
{
	int ret;

	cxit_create_ep();

	/* RX context doesn't work with anything but scalable eps */
	ret = fi_tx_context(cxit_ep, 0, NULL, NULL, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "fi_tx_context bad ep. %d", ret);

	cxit_destroy_ep();
}

Test(ep, stx_ctx_null_stx)
{
	int ret;
	struct fi_tx_attr *attr = NULL;
	void *context = NULL;

	ret = fi_stx_context(cxit_domain, attr, NULL, context);
	/* TODO Fix when fi_stx_context is implemented, should be -FI_EINVAL */
	cr_assert_eq(ret, -FI_ENOSYS, "fi_stx_context null stx. %d", ret);
}

Test(ep, stx_ctx)
{
	int ret;
	struct fi_tx_attr *attr = NULL;
	struct fid_stx *stx;
	struct cxip_ep *ep;
	void *context = &ret;
	struct cxip_domain *dom;
	struct cxip_txc *txc;
	int refs;

	dom = container_of(cxit_domain, struct cxip_domain,
			   util_domain.domain_fid);
	refs = ofi_atomic_get32(&dom->ref);

	ret = fi_stx_context(cxit_domain, attr, &stx, context);

	/* TODO Fix when fi_stx_context is implemented, should be FI_SUCCESS */
	cr_assert_eq(ret, -FI_ENOSYS, "fi_stx_context failed. %d", ret);
	if (ret == -FI_ENOSYS)
		return;

	ep = container_of(stx, struct cxip_ep, ep);
	txc = ep->ep_obj->txc;

	/* Validate stx */
	cr_assert_eq(txc->domain, dom);
	cr_assert_eq(ofi_atomic_inc32(&dom->ref), refs + 1);
	cr_assert_eq(ep->ep.fid.fclass, FI_CLASS_TX_CTX);
	cr_assert_eq(ep->ep.fid.context, context);

	ret = fi_close(&stx->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close stx_ep. %d", ret);
}

Test(ep, srx_ctx_null_srx)
{
	int ret;
	struct fi_rx_attr *attr = NULL;
	void *context = NULL;

	ret = fi_srx_context(cxit_domain, attr, NULL, context);
	/* TODO Fix when fi_srx_context is implemented, should be -FI_EINVAL */
	cr_assert_eq(ret, -FI_ENOSYS, "fi_srx_context null srx. %d", ret);
}

Test(ep, srx_ctx)
{
	int ret;
	struct fi_rx_attr *attr = NULL;
	struct fid_ep *srx;
	struct cxip_ep *srx_ep;
	void *context = &ret;
	struct cxip_domain *dom;
	struct cxip_rxc *rxc;
	int refs;

	dom = container_of(cxit_domain, struct cxip_domain,
			   util_domain.domain_fid);
	refs = ofi_atomic_get32(&dom->ref);

	ret = fi_srx_context(cxit_domain, attr, &srx, context);
	/* TODO Fix when fi_srx_context is implemented, should be FI_SUCCESS */
	cr_assert_eq(ret, -FI_ENOSYS, "fi_stx_context failed. %d", ret);
	if (ret == -FI_ENOSYS)
		return;

	srx_ep = container_of(srx, struct cxip_ep, ep);
	rxc = srx_ep->ep_obj->rxc;

	/* Validate stx */
	cr_assert_eq(rxc->domain, dom);
	cr_assert_eq(ofi_atomic_inc32(&dom->ref), refs + 1);
	cr_assert_eq(srx_ep->ep.fid.fclass, FI_CLASS_RX_CTX);
	cr_assert_eq(srx_ep->ep.fid.context, context);
	cr_assert_eq(rxc->state, RXC_ENABLED);
	cr_assert_eq(rxc->min_multi_recv, CXIP_EP_MIN_MULTI_RECV);

	ret = fi_close(&srx->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close srx_ep. %d", ret);
}

TestSuite(ep_init, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(ep_init, auth_key)
{
	int ret;
	struct cxi_auth_key auth_key = {
		.svc_id = CXI_DEFAULT_SVC_ID,
		.vni = 1,
	};

	/* Create fabric */
	cxit_setup_domain();

	/* Try invalid auth key */
	cxit_fi->domain_attr->auth_key_size = 12345;
	ret = fi_domain(cxit_fabric, cxit_fi, &cxit_domain, NULL);
	cr_assert_eq(ret, -FI_EINVAL);

	/* Set custom auth key in Domain */
	cxit_fi->domain_attr->auth_key = mem_dup(&auth_key, sizeof(auth_key));
	cxit_fi->domain_attr->auth_key_size = sizeof(auth_key);

	/* Create enabled Domain/EP */
	cxit_setup_rma();

	cxit_teardown_rma();

	/*---*/

	cxit_setup_domain();
	cxit_create_domain();

	/* Try invalid auth key */
	cxit_fi->ep_attr->auth_key_size = 12345;

	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, NULL);
	cr_assert_eq(ret, -FI_EINVAL); /* inconsistent error */

	/* Set custom auth key in EP */
	auth_key.vni = 200;

	free(cxit_fi->ep_attr->auth_key);
	cxit_fi->ep_attr->auth_key = mem_dup(&auth_key, sizeof(auth_key));
	cxit_fi->ep_attr->auth_key_size = sizeof(auth_key);

	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, NULL);
	cr_assert_eq(ret, -FI_EINVAL);

	/* Try mis-matched svc_id */
	auth_key.svc_id = 10;
	auth_key.vni = 301;

	free(cxit_fi->ep_attr->auth_key);
	cxit_fi->ep_attr->auth_key = mem_dup(&auth_key, sizeof(auth_key));
	cxit_fi->ep_attr->auth_key_size = sizeof(auth_key);

	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, NULL);
	cr_assert_eq(ret, -FI_EINVAL);

	cxit_destroy_domain();
	cxit_teardown_domain();
}

Test(ep_init, tclass)
{
	int ret;

	/* Create fabric */
	cxit_setup_domain();

	/* Try invalid auth key */
	cxit_fi->domain_attr->tclass = FI_TC_DSCP;

	ret = fi_domain(cxit_fabric, cxit_fi, &cxit_domain, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "ret is: %d\n", ret);

	/* Set custom TC in Domain */
	cxit_fi->domain_attr->tclass = FI_TC_LOW_LATENCY;

	/* Create enabled Domain/EP */
	cxit_setup_rma();

	cxit_teardown_rma();

	/*---*/

	cxit_setup_domain();
	cxit_create_domain();

	/* Try invalid auth key */
	cxit_fi->tx_attr->tclass = FI_TC_DSCP;

	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "ret is: %d\n", ret);

	/* Set custom TC in EP */
	cxit_fi->tx_attr->tclass = FI_TC_DEDICATED_ACCESS;

	/* Create enabled Domain/EP */
	cxit_setup_rma();

	cxit_teardown_rma();
}

Test(ep, invalid_tx_attr_size)
{
	struct fid_ep *tmp_ep;
	int ret;

	/* Invalid TX attr size. */
	cxit_fi->tx_attr->size = 1234567;

	ret = fi_endpoint(cxit_domain, cxit_fi, &tmp_ep, NULL);
	cr_assert(ret != FI_SUCCESS, "fi_endpoint");
}

Test(ep, valid_tx_attr_size)
{
	struct fid_ep *tmp_ep;
	int ret;

	/* Invalid TX attr size. */
	cxit_fi->tx_attr->size = 16384;

	ret = fi_endpoint(cxit_domain, cxit_fi, &tmp_ep, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_endpoint");

	ret = fi_close(&tmp_ep->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close EP");
}

Test(ep, valid_tx_attr_size_hints)
{
	struct fi_info *hints;
	struct fi_info *info;
	int ret;
	unsigned int tx_size = 1024;

	hints = fi_allocinfo();
	cr_assert(hints != NULL, "fi_allocinfo");

	hints->tx_attr->size = tx_size;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	assert(info->tx_attr->size == tx_size);

	fi_freeinfo(info);
	fi_freeinfo(hints);
}

TestSuite(ep_tclass, .init = cxit_setup_tx_alias_rma,
	  .fini = cxit_teardown_tx_alias_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

/* Add control test for setting of EP tclass.
 *
 * Test same for alias EP.
 *
 * Parameterized for all TCLASS values and bad values.
 */
struct ep_tclass_params {
	int tclass;
	int retval;
};

static struct ep_tclass_params tclass_params[] = {
	{.tclass = 0,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_UNSPEC,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_DSCP,
	 .retval = -FI_EINVAL},
	{.tclass = FI_TC_LABEL,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_BEST_EFFORT,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_LOW_LATENCY,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_DEDICATED_ACCESS,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_BULK_DATA,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_SCAVENGER,
	 .retval = FI_SUCCESS},
	{.tclass = FI_TC_NETWORK_CTRL,		/* Not supported */
	 .retval = -FI_EINVAL},
	{.tclass = FI_TC_NETWORK_CTRL + 1,	/* Illegal */
	 .retval = -FI_EINVAL},
};

int set_ep_tclass(struct cxip_ep *ep, uint32_t tclass)
{
	int ret;

	ret = fi_set_val(&ep->ep.fid, FI_OPT_CXI_SET_TCLASS,
			 (void *)&tclass);
	if (ret == FI_SUCCESS) {
		if (tclass != FI_TC_UNSPEC)
			cr_assert_eq(tclass, ep->tx_attr.tclass,
				     "update tclass mismatch. %d != %d",
				     tclass, ep->tx_attr.tclass);
		else
			cr_assert_neq(tclass, ep->tx_attr.tclass,
				      "FI_TC_UNSPEC tclass not updated");
	}

	return ret;
}

ParameterizedTestParameters(ep_tclass, alias_set_tclass)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(tclass_params);
	return cr_make_param_array(struct ep_tclass_params,
				   tclass_params, param_sz);
}

/* Modify EP alias traffic class */
ParameterizedTest(struct ep_tclass_params *param, ep_tclass,
		  alias_set_tclass)
{
	int ret;
	struct cxip_ep *cxi_ep;
	struct cxip_ep *alias_ep = NULL;
	uint32_t orig_ep_tclass;

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);
	orig_ep_tclass = cxi_ep->tx_attr.tclass;

	alias_ep = container_of(&cxit_tx_alias_ep->fid, struct cxip_ep, ep.fid);
	cr_assert_not_null(alias_ep->ep_obj);

	ret = set_ep_tclass(alias_ep, param->tclass);
	cr_assert_eq(ret, param->retval,
		     "fi_set_val for TCLASS %d", param->tclass);

	/* make sure only the alias EP tclass changed */
	cr_assert_eq(orig_ep_tclass, cxi_ep->tx_attr.tclass,
		     "Original EP tclass changed");
}

ParameterizedTestParameters(ep_tclass, set_tclass)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(tclass_params);
	return cr_make_param_array(struct ep_tclass_params,
				   tclass_params, param_sz);
}

/* Modify standard EP traffic class parameters */
ParameterizedTest(struct ep_tclass_params *param, ep_tclass, set_tclass)
{
	int ret;
	struct cxip_ep *cxi_ep;

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = set_ep_tclass(cxi_ep, param->tclass);
	cr_assert_eq(ret, param->retval,
		     "fi_set_val for TCLASS %d", param->tclass);
}

TestSuite(ep_caps, .timeout = CXIT_DEFAULT_TIMEOUT);

void verify_ep_msg_cap(uint64_t flags)
{
	struct cxip_ep *ep;
	struct cxip_rxc_hpc *rxc_hpc = NULL;
	int ret;

	cxit_setup_ep();

	/* Set info TX/RX attribute appropriately */
	if (!(flags & FI_SEND))
		cxit_fi->tx_attr->caps &= ~(FI_SEND | FI_SEND);
	if (!(flags & FI_RECV))
		cxit_fi->rx_attr->caps &= ~(FI_MSG | FI_RECV);
	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "EP enable");

	ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	if (ep->ep_obj->rxc->protocol == FI_PROTO_CXI)
		rxc_hpc = container_of(ep->ep_obj->rxc, struct cxip_rxc_hpc,
				       base);

	/* Requires knowledge of implementation */
	if (flags & FI_SEND) {
		cr_assert(ep->ep_obj->txc->enabled, "TX Enabled");
		cr_assert(ep->ep_obj->txc->send_cq != NULL, "Send CQ");
	}

	if (flags & FI_RECV) {
		cr_assert(ep->ep_obj->rxc->state == RXC_ENABLED ||
			  ep->ep_obj->rxc->state == RXC_ENABLED_SOFTWARE,
			  "RX Enabled");
		cr_assert(ep->ep_obj->rxc->recv_cq != NULL, "Receive CQ");
		cr_assert(ep->ep_obj->rxc->rx_evtq.eq != NULL, "RX H/W EQ");
		cr_assert(ep->ep_obj->rxc->rx_cmdq != NULL, "RX TGT CMDQ");
		if (rxc_hpc)
			cr_assert(rxc_hpc->tx_cmdq != NULL, "RX TX CMDQ");
	} else {
		cr_assert(ep->ep_obj->rxc->state == RXC_ENABLED, "R/X enabled");
		cr_assert(ep->ep_obj->rxc->rx_evtq.eq == NULL, "RX H/W EQ");
		cr_assert(ep->ep_obj->rxc->rx_cmdq == NULL, "RX TGT CMDQ");
		if (rxc_hpc)
			cr_assert(rxc_hpc->tx_cmdq == NULL, "RX TX CMDQ");
	}

	cxit_teardown_rma();
}

static void verify_ep_msg_ops(uint64_t flags)
{
	bool recv;
	bool send;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	int recv_len = 512;
	int send_len = 512;
	struct iovec riovec;
	struct iovec siovec;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	int ret;

	recv = !!(flags & FI_RECV);
	send = !!(flags & FI_SEND);

	cxit_setup_ep();

	/* Set info TX/RX attribute appropriately */
	if (!send)
		cxit_fi->tx_attr->caps &= ~(FI_MSG | FI_SEND);
	if (!recv)
		cxit_fi->rx_attr->caps &= ~(FI_MSG | FI_RECV);
	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert_not_null(recv_buf);
	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert_not_null(send_buf);

	/* Verify can not call API functions */
	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_recv");

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	ret = fi_recvv(cxit_ep, &riovec, NULL, 1, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_recvv");

	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;
	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_recvmsg");

	ret = fi_send(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_send");

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	ret = fi_sendv(cxit_ep, &siovec, NULL, 1, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_sendv");

	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;
	ret = fi_sendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_sendmsg");

	ret = fi_inject(cxit_ep, send_buf, 8, cxit_ep_fi_addr);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_inject");

	ret = fi_senddata(cxit_ep, send_buf, send_len, NULL, 0xa5a5,
			  cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_senddata");

	ret = fi_injectdata(cxit_ep, send_buf, 8, 0xa5a5, cxit_ep_fi_addr);
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_injectdata");

	/* Enable EP */
	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "EP enable");

	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, recv ? FI_SUCCESS : -FI_ENOSYS,
		     "EP enabled fi_recv");

	ret = fi_recvv(cxit_ep, &riovec, NULL, 1, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, recv ? FI_SUCCESS : -FI_ENOSYS,
		     "EP enabled fi_recvv");

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, recv ? FI_SUCCESS : -FI_ENOSYS,
		     "EP enabled fi_recvmsg");

	ret = fi_send(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, send ? FI_SUCCESS : -FI_ENOSYS,
		     "EP enabled fi_send");

	ret = fi_sendv(cxit_ep, &siovec, NULL, 1, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, send ? FI_SUCCESS : -FI_ENOSYS,
		     "EP enabled fi_sendv");
	cr_assert_eq(ret, -FI_ENOSYS, "EP not enabled fi_sendv");

	cxit_teardown_rma();
}

Test(ep_caps, msg_tx_rx)
{
	struct fi_info *info;
	int ret;

	/* No hints */
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, NULL, &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, FI_SEND, "FI_SEND returned");
	cr_assert_eq(info->caps & FI_RECV, FI_RECV, "FI_RECV returned");
	cr_assert_eq(info->tx_attr->caps & FI_MSG, FI_MSG,
		     "FI_MSG TX returned");
	cr_assert_eq(info->tx_attr->caps & FI_SEND, FI_SEND,
		     "FI_SEND TX returned");
	cr_assert_eq(info->rx_attr->caps & FI_MSG, FI_MSG,
		     "FI_MSG RX returned");
	cr_assert_eq(info->rx_attr->caps & FI_RECV, FI_RECV,
		     "FI_RECV RX returned");
	verify_ep_msg_cap(FI_SEND | FI_RECV);
	fi_freeinfo(info);

	/* hints->caps set to 0 */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = 0;
	cxit_fi_hints->tx_attr->caps = 0;
	cxit_fi_hints->rx_attr->caps = 0;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, FI_SEND, "FI_SEND returned");
	cr_assert_eq(info->caps & FI_RECV, FI_RECV, "FI_RECV returned");
	cr_assert_eq(info->tx_attr->caps & FI_MSG, FI_MSG,
		     "FI_MSG TX returned");
	cr_assert_eq(info->tx_attr->caps & FI_SEND, FI_SEND,
		     "FI_SEND TX returned");
	cr_assert_eq(info->rx_attr->caps & FI_MSG, FI_MSG,
		     "FI_MSG RX returned");
	cr_assert_eq(info->rx_attr->caps & FI_RECV, FI_RECV,
		     "FI_RECV RX returned");
	verify_ep_msg_cap(FI_SEND | FI_RECV);
	fi_freeinfo(info);
	cxit_teardown_getinfo();

	/* hints->caps set to FI_MSG | FI_SEND | FI_RECV */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_SEND | FI_RECV;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, FI_SEND, "FI_SEND returned");
	cr_assert_eq(info->caps & FI_RECV, FI_RECV, "FI_RECV returned");
	verify_ep_msg_cap(FI_SEND | FI_RECV);
	fi_freeinfo(info);
	cxit_teardown_getinfo();

	/* hints->caps set to FI_MSG implies FI_SEND and FI_RECV */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, FI_SEND, "FI_SEND returned");
	cr_assert_eq(info->caps & FI_RECV, FI_RECV, "FI_RECV returned");
	verify_ep_msg_cap(FI_SEND | FI_RECV);
	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, msg_tx)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to FI_MSG | FI_SEND is TX message only EP */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_SEND;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, FI_SEND, "FI_SEND returned");
	cr_assert_eq(info->caps & FI_RECV, 0, "FI_RECV not returned");
	verify_ep_msg_cap(FI_SEND);
	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, msg_rx)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to FI_MSG | FI_RECV is RX message only EP */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_RECV;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_MSG, FI_MSG, "FI_MSG returned");
	cr_assert_eq(info->caps & FI_SEND, 0, "FI_SEND not returned");
	cr_assert_eq(info->caps & FI_RECV, FI_RECV, "FI_RECV returned");
	verify_ep_msg_cap(FI_RECV);
	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, msg_rx_only_ops)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to FI_MSG | FI_RECV is RX message only EP */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_RECV;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	verify_ep_msg_ops(FI_RECV);
	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

/* Verify FI_RMA API ops set */
extern struct fi_ops_rma cxip_ep_rma_ops;
extern struct fi_ops_rma cxip_ep_rma_no_ops;

static void verify_ep_rma_ops(uint64_t caps)
{
	int ret;

	cxit_setup_ep();

	cxit_fi->caps = caps;
	cxit_fi->tx_attr->caps = caps;

	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	/* Enable EP */
	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "EP enable");

	/* Verify correct function table is set */
	if (caps & FI_RMA && ofi_rma_initiate_allowed(caps))
		cr_assert_eq(cxit_ep->rma, &cxip_ep_rma_ops,
			     "FI_RMA ops not set");
	else
		cr_assert_eq(cxit_ep->rma, &cxip_ep_rma_no_ops,
			     "FI_RMA ops set");

	cxit_teardown_rma();
}

/* Verify FI_ATOMIC API ops enable/disable */
extern struct fi_ops_atomic cxip_ep_atomic_ops;
extern struct fi_ops_atomic cxip_ep_atomic_no_ops;

static void verify_ep_amo_ops(uint64_t caps)
{
	int ret;

	cxit_setup_ep();

	cxit_fi->caps = caps;
	cxit_fi->tx_attr->caps = caps;

	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	/* Enable EP */
	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "EP enable");

	/* Verify correct function table is set */
	if (caps & FI_ATOMIC && ofi_rma_initiate_allowed(caps))
		cr_assert_eq(cxit_ep->atomic, &cxip_ep_atomic_ops,
			     "FI_ATOMIC ops not set");
	else
		cr_assert_eq(cxit_ep->atomic, &cxip_ep_atomic_no_ops,
			     "FI_ATOMIC ops set");

	cxit_teardown_rma();
}

/* test_cap is the caps that should be set */
static void verify_caps_only(struct fi_info *info,
			     uint64_t test_cap)
{
	if (!(test_cap & FI_TAGGED))
		cr_assert_eq(info->caps & FI_TAGGED, 0, "FI_TAGGED set");
	if (!(test_cap & FI_ATOMIC))
		cr_assert_eq(info->caps & FI_ATOMIC, 0, "FI_ATOMIC set");
	if (!(test_cap & FI_RMA))
		cr_assert_eq(info->caps & FI_RMA, 0, "FI_RMA set");
	if (!(test_cap & FI_COLLECTIVE))
		cr_assert_eq(info->caps & FI_COLLECTIVE, 0,
			     "FI_COLLECTIVE set");
}

Test(ep_caps, msg_only)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to for only FI_MSG, don't enable other API */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	verify_caps_only(info, FI_MSG);

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, tagged_only)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to for only FI_TAGGED, don't enable other API */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_TAGGED;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	verify_caps_only(info, FI_TAGGED);

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, rma_only)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to for only FI_RMA, don't enable other API */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_RMA;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	verify_caps_only(info, FI_RMA);

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, atomic_only)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to for only FI_ATOMIC, don't enable other API */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_ATOMIC;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	verify_caps_only(info, FI_ATOMIC);

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, coll_only)
{
	struct fi_info *info;
	int ret;

	/* hints->caps set to for only FI_COLLECTIVE enables only FI_MSG */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_COLLECTIVE;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	verify_caps_only(info, FI_COLLECTIVE | FI_MSG);

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(ep_caps, rma_initiator)
{
	verify_ep_rma_ops(FI_RMA | FI_READ | FI_WRITE);
}

Test(ep_caps, rma_target_only)
{
	verify_ep_rma_ops(FI_RMA | FI_REMOTE_READ | FI_REMOTE_WRITE);
}

Test(ep_caps, rma_amo_only)
{
	verify_ep_rma_ops(FI_ATOMIC | FI_READ | FI_WRITE);
}

Test(ep_caps, rma_none)
{
	verify_ep_rma_ops(FI_MSG | FI_TAGGED);
}

Test(ep_caps, amo_initiator)
{
	verify_ep_amo_ops(FI_ATOMIC | FI_READ | FI_WRITE);
}

Test(ep_caps, amo_target_only)
{
	verify_ep_amo_ops(FI_ATOMIC | FI_REMOTE_READ | FI_REMOTE_WRITE);
}

Test(ep_caps, amo_rma_only)
{
	verify_ep_amo_ops(FI_RMA | FI_READ | FI_WRITE);
}

Test(ep_caps, amo_none)
{
	verify_ep_amo_ops(FI_MSG | FI_TAGGED);
}

TestSuite(ep_locking, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(ep_locking, domain)
{
	struct cxip_domain *dom;
	struct cxip_ep *ep;
	struct cxip_cq *cq;

	cxit_setup_getinfo();

	cxit_fi_hints->domain_attr->threading = FI_THREAD_DOMAIN;
	cxit_setup_rma();

	cr_assert_eq(cxit_fi->domain_attr->threading, FI_THREAD_DOMAIN,
		     "Threading");

	dom = container_of(cxit_domain, struct cxip_domain,
			   util_domain.domain_fid);
	cr_assert_eq(dom->trig_cmdq_lock.lock_type, OFI_LOCK_NONE,
		     "Domain trigger command lock");

	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	cr_assert_eq(ep->ep_obj->lock.lock_type, OFI_LOCK_NONE,
		     "EP object lock");

	cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_NONE,
		     "TX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_NOOP,
		     "TX CQ entry lock");

	cq = container_of(cxit_rx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_NONE,
		     "RX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_NOOP,
		     "RX CQ entry lock");

	cxit_teardown_rma();
}

Test(ep_locking, completion)
{
	struct cxip_domain *dom;
	struct cxip_ep *ep;
	struct cxip_cq *cq;

	cxit_setup_getinfo();

	cxit_fi_hints->domain_attr->threading = FI_THREAD_COMPLETION;
	cxit_setup_rma();

	cr_assert_eq(cxit_fi->domain_attr->threading, FI_THREAD_COMPLETION,
		     "Threading");

	dom = container_of(cxit_domain, struct cxip_domain,
			   util_domain.domain_fid);
	cr_assert_eq(dom->trig_cmdq_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "Domain trigger command lock");

	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	cr_assert_eq(ep->ep_obj->lock.lock_type, OFI_LOCK_NONE,
		     "EP object lock");

	cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_NONE,
		     "TX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_NOOP,
		     "TX CQ entry lock");

	cq = container_of(cxit_rx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_NONE,
		     "RX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_NOOP,
		     "RX CQ entry lock");

	cxit_teardown_rma();
}

Test(ep_locking, safe)
{
	struct cxip_domain *dom;
	struct cxip_ep *ep;
	struct cxip_cq *cq;

	cxit_setup_getinfo();

	cxit_fi_hints->domain_attr->threading = FI_THREAD_SAFE;
	cxit_setup_rma();

	cr_assert_eq(cxit_fi->domain_attr->threading, FI_THREAD_SAFE,
		     "Threading");

	dom = container_of(cxit_domain, struct cxip_domain,
			   util_domain.domain_fid);
	cr_assert_eq(dom->trig_cmdq_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "Domain trigger command lock");

	ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	cr_assert_eq(ep->ep_obj->lock.lock_type, OFI_LOCK_SPINLOCK,
		     "EP object lock");

	cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "TX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "TX CQ entry lock");

	cq = container_of(cxit_rx_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cq->ep_list_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "RX CQ EP list lock");
	cr_assert_eq(cq->util_cq.cq_lock.lock_type, OFI_LOCK_SPINLOCK,
		     "RX CQ entry lock");

	cxit_teardown_rma();
}
