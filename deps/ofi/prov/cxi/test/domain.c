/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(domain, .init = cxit_setup_domain, .fini = cxit_teardown_domain,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic domain creation */
Test(domain, simple)
{
	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	cxit_destroy_domain();
}

/* Test use of topology ops */
Test(domain, topology)
{
	unsigned int group_num, switch_num, port_num;
	int ret;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);
	ret = dom_ops->topology(&cxit_domain->fid, &group_num, &switch_num,
				&port_num);
	cr_assert_eq(ret, FI_SUCCESS, "topology failed: %d\n", ret);

	ret = dom_ops->topology(&cxit_domain->fid, NULL, &switch_num,
				&port_num);
	cr_assert_eq(ret, FI_SUCCESS, "null group topology failed: %d\n", ret);

	ret = dom_ops->topology(&cxit_domain->fid, &group_num, NULL,
				&port_num);
	cr_assert_eq(ret, FI_SUCCESS, "null switch topology failed: %d\n", ret);

	ret = dom_ops->topology(&cxit_domain->fid, &group_num, &switch_num,
				NULL);
	cr_assert_eq(ret, FI_SUCCESS, "null port topology failed: %d\n", ret);

	cxit_destroy_domain();
}

Test(domain, enable_hybrid_mr_desc)
{
	int ret;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	ret = dom_ops->enable_hybrid_mr_desc(&cxit_domain->fid, true);
	cr_assert_eq(ret, FI_SUCCESS, "enable_hybrid_mr_desc failed: %d\n",
		     ret);

	cxit_destroy_domain();
}

Test(domain, ep_get_unexp_msgs)
{
	size_t num_ux_ret;
	size_t num_ux;
	size_t addrlen = sizeof(cxit_ep_addr);
	int ret;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));

	num_ux_ret = dom_ops->ep_get_unexp_msgs(cxit_ep, NULL, 0,
						NULL, &num_ux);
	cr_assert_eq(num_ux_ret, 0, "ep_get_unexp_msgs bad return\n");
	cr_assert_eq(num_ux, 0, "ep_get_unexp_msgs ux_count not 0\n");

	/* Tear down RMA objects */
	cxit_destroy_ep();
	cxit_destroy_av();
	cxit_destroy_cntrs();
	cxit_destroy_cqs();
	cxit_destroy_domain();
}

Test(domain, get_dwq_depth)
{
	int ret;
	size_t depth;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	ret = dom_ops->get_dwq_depth(&cxit_domain->fid, &depth);
	cr_assert_eq(ret, FI_SUCCESS, "get_dwq_depth failed: %d\n",
		     ret);

	cr_assert(depth > 0);

	cxit_destroy_domain();
}

Test(domain, enable_mr_match_events)
{
	int ret;
	struct cxip_domain *cxip_dom;
	struct cxip_mr *cxip_mr;
	uint64_t key = 50;
	struct mem_region region;
	bool enable;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	cxip_dom = container_of(cxit_domain, struct cxip_domain,
				util_domain.domain_fid);
	cr_assert_eq(cxip_env.mr_match_events,
		     cxip_dom->mr_match_events, "Global setting failed");

	if (!cxip_env.mr_match_events) {
		enable = true;
		ret = fi_control(&cxit_domain->fid,
				 FI_OPT_CXI_SET_MR_MATCH_EVENTS, &enable);
		cr_assert_eq(ret, FI_SUCCESS,
			     "enable_mr_match_events failed: %d", ret);

		cr_assert_eq(cxip_dom->mr_match_events, true,
			     "domain mr_match_events not set");
	}

	/* MR type established, setup RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_create_cqs();
	cxit_bind_cqs();
	cxit_create_cntrs();
	cxit_bind_cntrs();
	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert_eq(ret, FI_SUCCESS, "EP enable failed %d", ret);

	ret = mr_create(8, FI_REMOTE_WRITE | FI_REMOTE_READ, 0, &key, &region);
	cr_assert_eq(ret, FI_SUCCESS, "MR create failed %d", ret);

	cxip_mr = container_of(region.mr, struct cxip_mr, mr_fid);
	cr_assert_eq(cxip_mr->count_events, true,
		     "MR match events not set");

	mr_destroy(&region);

	/* Tear down RMA objects */
	cxit_destroy_ep();
	cxit_destroy_av();
	cxit_destroy_cntrs();
	cxit_destroy_cqs();
	cxit_destroy_domain();
}

Test(domain, enable_optimized_mrs)
{
	int ret;
	struct cxip_domain *cxip_dom;
	bool optimized;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	cxip_dom = container_of(cxit_domain, struct cxip_domain,
				util_domain.domain_fid);
	cr_assert_eq(cxip_env.optimized_mrs,
		     cxip_dom->optimized_mrs, "Global setting failed");

	/* Disable optimized MRs for the domain */
	ret = dom_ops->enable_optimized_mrs(&cxit_domain->fid, false);
	optimized = false;
	ret = fi_control(&cxit_domain->fid,
			 FI_OPT_CXI_SET_OPTIMIZED_MRS, &optimized);
	if (cxip_dom->is_prov_key) {
		cr_assert_eq(ret, FI_SUCCESS, "Unexpected call failure");
		cr_assert_eq(cxip_dom->optimized_mrs, false, "Disable failed");
	} else {
		cr_assert_eq(ret, -FI_EINVAL, "Client key check failed");
		cr_assert_eq(cxip_dom->optimized_mrs, cxip_env.optimized_mrs,
			     "Client key altered domain specific setting");
	}

	/* Enable optimized MRs for the domain */
	optimized = true;
	ret = fi_control(&cxit_domain->fid,
			 FI_OPT_CXI_SET_OPTIMIZED_MRS, &optimized);
	if (cxip_dom->is_prov_key) {
		cr_assert_eq(ret, FI_SUCCESS, "Unexpected call failure");
		cr_assert_eq(cxip_dom->optimized_mrs, true, "Enable failed");
	} else {
		cr_assert_eq(ret, -FI_EINVAL, "Client key check failed");
		cr_assert_eq(cxip_dom->optimized_mrs, cxip_env.optimized_mrs,
			     "Client key altered domain specific setting");
	}

	cxit_destroy_domain();
}

Test(domain, disable_prov_key_cache)
{
	int ret;
	struct cxip_domain *cxip_dom;
	bool enable = false;

	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	cxip_dom = container_of(cxit_domain, struct cxip_domain,
				util_domain.domain_fid);
	cr_assert_eq(cxip_env.prov_key_cache,
		     cxip_dom->prov_key_cache, "Global setting failed");

	ret = fi_control(&cxit_domain->fid, FI_OPT_CXI_SET_PROV_KEY_CACHE,
			 &enable);

	if (cxip_dom->is_prov_key) {
		cr_assert_eq(ret, FI_SUCCESS, "Unexpected failure %d", ret);
		cr_assert_eq(cxip_dom->prov_key_cache, false, "Update failed");
	} else {
		cr_assert_eq(ret, -FI_EINVAL, "Unexpected success");
		cr_assert_eq(cxip_env.prov_key_cache,
			     cxip_dom->prov_key_cache, "Unexpected update");
	}

	cxit_destroy_domain();
}

static const char *_fi_coll_to_text(enum fi_collective_op coll)
{
	switch (coll) {
	case FI_BARRIER:	return "FI_BARRIER";
	case FI_BROADCAST:	return "FI_BROADCAST";
	case FI_ALLTOALL:	return "FI_ALLTOALL";
	case FI_ALLREDUCE:	return "FI_ALLREDUCE";
	case FI_ALLGATHER:	return "FI_ALLGATHER";
	case FI_REDUCE_SCATTER:	return "FI_REDUCE_SCATTER";
	case FI_REDUCE:		return "FI_REDUCE";
	case FI_SCATTER:	return "FI_SCATTER";
	case FI_GATHER:		return "FI_GATHER";
	default:		return "NOCOLL";
	}
}

static const char *_fi_op_to_text(enum fi_op op)
{
	switch ((int)op) {
	case FI_MIN:		return "FI_MIN";
	case FI_MAX:		return "FI_MAX";
	case FI_SUM:		return "FI_SUM";
	case FI_PROD:		return "FI_PROD";
	case FI_LOR:		return "FI_LOR";
	case FI_LAND:		return "FI_LAND";
	case FI_BOR:		return "FI_BOR";
	case FI_BAND:		return "FI_BAND";
	case FI_LXOR:		return "FI_LXOR";
	case FI_BXOR:		return "FI_BXOR";
	case FI_ATOMIC_READ:	return "FI_ATOMIC_READ";
	case FI_ATOMIC_WRITE:	return "FI_ATOMIC_WRITE";
	case FI_CSWAP:		return "FI_CSWAP";
	case FI_CSWAP_NE:	return "FI_CSWAP_NE";
	case FI_CSWAP_LE:	return "FI_CSWAP_LE";
	case FI_CSWAP_LT:	return "FI_CSWAP_LT";
	case FI_CSWAP_GE:	return "FI_CSWAP_GE";
	case FI_CSWAP_GT:	return "FI_CSWAP_GT";
	case FI_MSWAP:		return "FI_MSWAP";
	case FI_NOOP:		return "FI_NOOP";
	default:		return "NOOP";
	}
}

static const char *_fi_datatype_to_text(enum fi_datatype datatype)
{
	switch ((int)datatype) {
	case FI_INT8:			return "FI_INT8";
	case FI_UINT8:			return "FI_UINT8";
	case FI_INT16:			return "FI_INT16";
	case FI_UINT16:			return "FI_UINT16";
	case FI_INT32:			return "FI_INT32";
	case FI_UINT32:			return "FI_UINT32";
	case FI_INT64:			return "FI_INT64";
	case FI_UINT64:			return "FI_UINT64";
	case FI_FLOAT:			return "FI_FLOAT";
	case FI_DOUBLE:			return "FI_DOUBLE";
	case FI_FLOAT_COMPLEX:		return "FI_FLOAT_COMPLEX";
	case FI_DOUBLE_COMPLEX:		return "FI_DOUBLE_COMPLEX";
	case FI_LONG_DOUBLE:		return "FI_LONG_DOUBLE";
	case FI_LONG_DOUBLE_COMPLEX:	return "FI_LONG_DOUBLE_COMPLEX";
	case FI_VOID:			return "FI_VOID";
	default:			return "NOTYPE";
	}
}

static void _test_coll_info(enum fi_collective_op coll,
			    enum fi_op op,
			    enum fi_datatype dtyp,
			    size_t count, size_t size, int exp)
{
	struct fi_collective_attr attr, *attrp;
	const char *collname = _fi_coll_to_text(coll);
	const char *opname = _fi_op_to_text(op);
	const char *dtypname = _fi_datatype_to_text(dtyp);
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.op = op;
	attr.datatype = dtyp;
	attrp = (op == -1) ? NULL : &attr;
	ret = fi_query_collective(cxit_domain, coll, attrp, 0L);
	cr_assert_eq(ret, exp,
		     "query(%s attr.op=%s %s)=%s expect=%s\n",
		     collname, opname, dtypname,
		     fi_strerror(ret), fi_strerror(exp));
	if (!attrp || ret)
		return;

	cr_assert_eq(attr.datatype_attr.count, count,
		     "query(%s attr.op=%s %s)count=%ld expect=%ld\n",
		     collname, opname, dtypname,
		     attr.datatype_attr.count, count);
	cr_assert_eq(attr.datatype_attr.size, size,
		     "query(%s attr.op=%s %s)size=%ld expect=%ld\n",
		     collname, opname, dtypname,
		     attr.datatype_attr.size, size);
}

Test(domain, coll_info)
{
	cxit_create_domain();
	cr_assert(cxit_domain != NULL);

	_test_coll_info(FI_BARRIER, -1, -1, 0, 0, FI_SUCCESS);
	_test_coll_info(FI_BARRIER, FI_NOOP, FI_VOID, 0, 0, FI_SUCCESS);

	_test_coll_info(FI_BROADCAST, -1, FI_VOID, 0, 0, -FI_EINVAL);
	_test_coll_info(FI_BROADCAST, FI_SUM, FI_VOID, 0, 0, -FI_EOPNOTSUPP);
	_test_coll_info(FI_BROADCAST, FI_ATOMIC_WRITE, FI_UINT8, 32, 1,
			FI_SUCCESS);

	_test_coll_info(FI_REDUCE, FI_ATOMIC_WRITE, -1, 0, 0, -FI_EOPNOTSUPP);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_INT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_INT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_INT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_UINT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_UINT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_UINT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BOR, FI_UINT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_INT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_INT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_INT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_UINT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_UINT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_UINT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BAND, FI_UINT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_INT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_INT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_INT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_UINT8, 32, 1, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_UINT16, 16, 2, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_UINT32, 8, 4, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_BXOR, FI_UINT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_MIN, FI_UINT64, 0, 0, -FI_EOPNOTSUPP);
	_test_coll_info(FI_REDUCE, FI_MIN, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_MIN, FI_DOUBLE, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_MAX, FI_UINT64, 0, 0, -FI_EOPNOTSUPP);
	_test_coll_info(FI_REDUCE, FI_MAX, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_MAX, FI_DOUBLE, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_SUM, FI_UINT64, 0, 0, -FI_EOPNOTSUPP);
	_test_coll_info(FI_REDUCE, FI_SUM, FI_INT64, 4, 8, FI_SUCCESS);
	_test_coll_info(FI_REDUCE, FI_SUM, FI_DOUBLE, 4, 8, FI_SUCCESS);

	cxit_destroy_domain();
}

TestSuite(domain_cntrs, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic counter read */
Test(domain_cntrs, cntr_read)
{
	int ret;
	uint64_t value;
	struct timespec ts;

	ret = dom_ops->cntr_read(&cxit_domain->fid, C_CNTR_LPE_SUCCESS_CNTR,
				 &value, &ts);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	printf("LPE_SUCCESS_CNTR: %lu\n", value);
}
