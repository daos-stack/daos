/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018,2020-2022 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <criterion/criterion.h>

#include "cxip_test_common.h"

struct fi_info *cxit_fi_hints;
struct fi_info *cxit_fi;
struct fid_fabric *cxit_fabric;
struct fid_domain *cxit_domain;
struct fi_cxi_dom_ops *dom_ops;
struct fid_ep *cxit_ep;
struct fid_ep *cxit_tx_alias_ep;
struct cxip_addr cxit_ep_addr;
fi_addr_t cxit_ep_fi_addr;
struct fi_eq_attr cxit_eq_attr = {};
struct fid_eq *cxit_eq;
struct fi_cq_attr cxit_tx_cq_attr = {
	.format = FI_CQ_FORMAT_TAGGED,
	.size = 16384
};
struct fi_cq_attr cxit_rx_cq_attr = { .format = FI_CQ_FORMAT_TAGGED };
uint64_t cxit_eq_bind_flags = 0;
uint64_t cxit_tx_cq_bind_flags = FI_TRANSMIT;
uint64_t cxit_rx_cq_bind_flags = FI_RECV;
struct fid_cq *cxit_tx_cq, *cxit_rx_cq;
struct fi_cntr_attr cxit_cntr_attr = {};
struct fid_cntr *cxit_send_cntr, *cxit_recv_cntr;
struct fid_cntr *cxit_read_cntr, *cxit_write_cntr;
struct fid_cntr *cxit_rem_cntr;
struct fi_av_attr cxit_av_attr;
struct fid_av *cxit_av;
struct cxit_coll_mc_list cxit_coll_mc_list = { .count = 5 };
char *cxit_node, *cxit_service;
uint64_t cxit_flags;
int cxit_n_ifs;
struct fid_av_set *cxit_av_set;
struct fid_mc *cxit_mc;
bool cxit_prov_key;
int s_page_size;
bool enable_cxi_hmem_ops = 1;

/* Get _SC_PAGESIZE */
static void cxit_set_page_size(void)
{
	if (!s_page_size)
		s_page_size = sysconf(_SC_PAGESIZE);
}

int cxit_dom_read_cntr(unsigned int cntr, uint64_t *value,
		       struct timespec *ts, bool sync)
{
	int ret;
	struct timespec start;
	struct timespec delta;

	/* Map counters if not already mapped */
	ret = dom_ops->cntr_read(&cxit_domain->fid, cntr, value, &start);
	if (ret || !sync)
		goto done;

	/* Wait for an update to occur to read latest counts */
	do {
		usleep(100);
		ret = dom_ops->cntr_read(&cxit_domain->fid, cntr, value,
					 &delta);
	} while (!ret && delta.tv_sec == start.tv_sec &&
		 delta.tv_nsec == start.tv_nsec);

done:
	if (ts && !ret)
		*ts = sync ? delta : start;

	return ret;
}

static ssize_t copy_from_hmem_iov(void *dest, size_t size,
				 enum fi_hmem_iface iface, uint64_t device,
				 const struct iovec *hmem_iov,
				 size_t hmem_iov_count,
				 uint64_t hmem_iov_offset)
{
	size_t cpy_size = MIN(size, hmem_iov->iov_len);

	assert(iface == FI_HMEM_SYSTEM);
	assert(hmem_iov_count == 1);
	assert(hmem_iov_offset == 0);

	memcpy(dest, hmem_iov->iov_base, cpy_size);

	return cpy_size;
}

static ssize_t copy_to_hmem_iov(enum fi_hmem_iface iface, uint64_t device,
				const struct iovec *hmem_iov,
				size_t hmem_iov_count,
				uint64_t hmem_iov_offset, const void *src,
				size_t size)
{
	size_t cpy_size = MIN(size, hmem_iov->iov_len);

	assert(iface == FI_HMEM_SYSTEM);
	assert(hmem_iov_count == 1);
	assert(hmem_iov_offset == 0);

	memcpy(hmem_iov->iov_base, src, cpy_size);

	return cpy_size;
}

struct fi_hmem_override_ops cxi_hmem_ops = {
	.copy_from_hmem_iov = copy_from_hmem_iov,
	.copy_to_hmem_iov = copy_to_hmem_iov,
};

void cxit_create_fabric_info(void)
{
	int ret;

	if (cxit_fi)
		return;

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &cxit_fi);
	cr_assert(ret == FI_SUCCESS, "fi_getinfo");
	cxit_fi->ep_attr->tx_ctx_cnt = cxit_fi->domain_attr->tx_ctx_cnt;
	cxit_fi->ep_attr->rx_ctx_cnt = cxit_fi->domain_attr->rx_ctx_cnt;

	/* Add in FI_SOURCE and FI_SOURCE_ERR to include all capabilities */
	cxit_fi->caps |= FI_SOURCE | FI_SOURCE_ERR;
	cxit_fi->rx_attr->caps |= FI_SOURCE | FI_SOURCE_ERR;
}

void cxit_destroy_fabric_info(void)
{
	fi_freeinfo(cxit_fi);
	cxit_fi = NULL;
}

void cxit_create_fabric(void)
{
	int ret;

	if (cxit_fabric)
		return;

	ret = fi_fabric(cxit_fi->fabric_attr, &cxit_fabric, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_fabric");
}

void cxit_destroy_fabric(void)
{
	int ret;

	ret = fi_close(&cxit_fabric->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close fabric");
	cxit_fabric = NULL;
}

void cxit_create_domain(void)
{
	int ret;

	if (cxit_domain)
		return;

	ret = fi_domain(cxit_fabric, cxit_fi, &cxit_domain, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_domain");

	/* Should be able to open either v1 - v6 */
	ret = fi_open_ops(&cxit_domain->fid, FI_CXI_DOM_OPS_1, 0,
			  (void **)&dom_ops, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_open_ops v1");
	cr_assert(dom_ops->cntr_read != NULL, "v1 function returned");

	ret = fi_open_ops(&cxit_domain->fid, FI_CXI_DOM_OPS_2, 0,
			  (void **)&dom_ops, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_open_ops v2");
	cr_assert(dom_ops->cntr_read != NULL &&
		  dom_ops->topology != NULL, "V2 functions returned");

	ret = fi_open_ops(&cxit_domain->fid, FI_CXI_DOM_OPS_3, 0,
			  (void **)&dom_ops, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_open_ops v3");
	cr_assert(dom_ops->cntr_read != NULL &&
		  dom_ops->topology != NULL &&
		  dom_ops->enable_hybrid_mr_desc != NULL,
		  "V3 functions returned");

	ret = fi_open_ops(&cxit_domain->fid, FI_CXI_DOM_OPS_6, 0,
			  (void **)&dom_ops, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_open_ops v6");
	cr_assert(dom_ops->cntr_read != NULL &&
		  dom_ops->topology != NULL &&
		  dom_ops->enable_hybrid_mr_desc != NULL &&
		  dom_ops->ep_get_unexp_msgs != NULL &&
		  dom_ops->get_dwq_depth != NULL &&
		  dom_ops->enable_mr_match_events != NULL,
		  "V3 functions returned");

	if (enable_cxi_hmem_ops) {
		ret = fi_set_ops(&cxit_domain->fid, FI_SET_OPS_HMEM_OVERRIDE, 0,
				 &cxi_hmem_ops, NULL);
		cr_assert(ret == FI_SUCCESS, "fi_set_ops");
	}
}

void cxit_destroy_domain(void)
{
	int ret;

	ret = fi_close(&cxit_domain->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close domain. %d", ret);
	cxit_domain = NULL;
}

void cxit_create_ep(void)
{
	int ret;

	ret = fi_endpoint(cxit_domain, cxit_fi, &cxit_ep, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_endpoint");
	cr_assert_not_null(cxit_ep);
}

void cxit_destroy_ep(void)
{
	int ret;

	if (cxit_ep != NULL) {
		ret = fi_close(&cxit_ep->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close endpoint = %d", ret);
		cxit_ep = NULL;
	}
}

void cxit_create_eq(void)
{
	struct fi_eq_attr attr = {
		.size = 32,
		.flags = FI_WRITE,
		.wait_obj = FI_WAIT_NONE
	};
	int ret;

	ret = fi_eq_open(cxit_fabric, &attr, &cxit_eq, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_eq_open failed %d", ret);
	cr_assert_not_null(cxit_eq, "fi_eq_open returned NULL eq");
}

void cxit_destroy_eq(void)
{
	int ret;

	ret = fi_close(&cxit_eq->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close EQ failed %d", ret);
	cxit_eq = NULL;
}

void cxit_bind_eq(void)
{
	int ret;

	/* NOTE: ofi implementation does not allow any flags */
	ret = fi_ep_bind(cxit_ep, &cxit_eq->fid, cxit_eq_bind_flags);
	cr_assert(!ret, "fi_ep_bind EQ");
}

void cxit_create_cqs(void)
{
	int ret;

	ret = fi_cq_open(cxit_domain, &cxit_tx_cq_attr, &cxit_tx_cq, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cq_open (TX)");

	ret = fi_cq_open(cxit_domain, &cxit_rx_cq_attr, &cxit_rx_cq, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cq_open (RX)");
}

void cxit_destroy_cqs(void)
{
	int ret;

	ret = fi_close(&cxit_rx_cq->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close RX CQ");
	cxit_rx_cq = NULL;

	ret = fi_close(&cxit_tx_cq->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close TX CQ");
	cxit_tx_cq = NULL;
}

void cxit_bind_cqs(void)
{
	int ret;

	ret = fi_ep_bind(cxit_ep, &cxit_tx_cq->fid, cxit_tx_cq_bind_flags);
	cr_assert(!ret, "fi_ep_bind TX CQ");

	ret = fi_ep_bind(cxit_ep, &cxit_rx_cq->fid, cxit_rx_cq_bind_flags);
	cr_assert(!ret, "fi_ep_bind RX CQ");
}

void cxit_create_rem_cntrs(void)
{
	int ret;

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_rem_cntr, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (rem)");
}

void cxit_create_local_cntrs(void)
{
	int ret;

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_send_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (send)");

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_recv_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (recv)");

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_read_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (read)");

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_write_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (write)");
}

void cxit_create_local_byte_cntrs(void)
{
	struct fi_cntr_attr attr = {
		.events = FI_CXI_CNTR_EVENTS_BYTES,
		.wait_obj = FI_WAIT_YIELD,
	};
	int ret;

	ret = fi_cntr_open(cxit_domain, &attr, &cxit_send_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (send)");

	ret = fi_cntr_open(cxit_domain, &attr, &cxit_recv_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (recv)");

	/* For now have read/write still use event counting */
	ret = fi_cntr_open(cxit_domain, NULL, &cxit_read_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (read)");

	ret = fi_cntr_open(cxit_domain, NULL, &cxit_write_cntr,
			   NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (write)");
}

void cxit_create_cntrs(void)
{
	cxit_create_local_cntrs();
	cxit_create_rem_cntrs();
}

void cxit_destroy_cntrs(void)
{
	int ret;

	if (cxit_send_cntr) {
		ret = fi_close(&cxit_send_cntr->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close send_cntr");
		cxit_send_cntr = NULL;
	}

	if (cxit_recv_cntr) {
		ret = fi_close(&cxit_recv_cntr->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close recv_cntr");
		cxit_recv_cntr = NULL;
	}

	if (cxit_read_cntr) {
		ret = fi_close(&cxit_read_cntr->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close read_cntr");
		cxit_read_cntr = NULL;
	}

	if (cxit_write_cntr) {
		ret = fi_close(&cxit_write_cntr->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close write_cntr");
		cxit_write_cntr = NULL;
	}

	if (cxit_rem_cntr) {
		ret = fi_close(&cxit_rem_cntr->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close rem_cntr");
		cxit_rem_cntr = NULL;
	}
}

void cxit_bind_cntrs(void)
{
	int ret;

	if (cxit_send_cntr) {
		ret = fi_ep_bind(cxit_ep, &cxit_send_cntr->fid, FI_SEND);
		cr_assert(!ret, "fi_ep_bind send_cntr");
	}

	if (cxit_recv_cntr) {
		ret = fi_ep_bind(cxit_ep, &cxit_recv_cntr->fid, FI_RECV);
		cr_assert(!ret, "fi_ep_bind recv_cntr");
	}

	if (cxit_read_cntr) {
		ret = fi_ep_bind(cxit_ep, &cxit_read_cntr->fid, FI_READ);
		cr_assert(!ret, "fi_ep_bind read_cntr");
	}

	if (cxit_write_cntr) {
		ret = fi_ep_bind(cxit_ep, &cxit_write_cntr->fid, FI_WRITE);
		cr_assert(!ret, "fi_ep_bind write_cntr");
	}
}

void cxit_create_av(void)
{
	int ret;

	ret = fi_av_open(cxit_domain, &cxit_av_attr, &cxit_av, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_av_open");
}

void cxit_destroy_av(void)
{
	int ret;

	ret = fi_close(&cxit_av->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close AV. %d", ret);
	cxit_av = NULL;
}

void cxit_bind_av(void)
{
	int ret;

	ret = fi_ep_bind(cxit_ep, &cxit_av->fid, 0);
	cr_assert(!ret, "fi_ep_bind AV");
}

void cxit_init(void)
{
	struct slist_entry *entry, *prev __attribute__((unused));
	int ret;
	struct fi_info *hints = cxit_allocinfo();
	struct fi_info *info;

	setlinebuf(stdout);
	cxit_set_page_size();

	/* Force provider init */
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	slist_foreach(&cxip_if_list, entry, prev) {
		cxit_n_ifs++;
	}

	fi_freeinfo(info);
	fi_freeinfo(hints);
}

struct fi_info *cxit_allocinfo_common(uint32_t proto)
{
	struct fi_info *info;
	char *odp_env;
	char *prov_key_env;

	info = fi_allocinfo();
	cr_assert(info, "fi_allocinfo");

	/* Always select CXI */
	info->fabric_attr->prov_name = strdup(cxip_prov_name);

	info->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;

	/* Test with provider generated keys instead of client */
	prov_key_env = getenv("CXIP_TEST_PROV_KEY");
	if (prov_key_env && strtol(prov_key_env, NULL, 10)) {
		cxit_prov_key = 1;
		info->domain_attr->mr_mode |= FI_MR_PROV_KEY;
	} else {
		cxit_prov_key = 0;
	}

	/* If remote ODP is enabled then test with ODP */
	odp_env = getenv("FI_CXI_ODP");
	if (odp_env && strtol(odp_env, NULL, 10))
		info->domain_attr->mr_mode &= ~FI_MR_ALLOCATED;

	/* If a EP protocol was specified indicate to use it */
	if (proto)
		info->ep_attr->protocol = proto;

	return info;
}

struct fi_info *cxit_allocinfo(void)
{
	return cxit_allocinfo_common(0);
}

struct fi_info *cxit_allocinfo_proto(uint32_t proto)
{
	return cxit_allocinfo_common(proto);
}

void cxit_setup_getinfo(void)
{
	cxit_init();

	if (!cxit_fi_hints)
		cxit_fi_hints = cxit_allocinfo();
}

void cxit_setup_getinfo_proto(uint32_t proto)
{
	cxit_init();

	if (!cxit_fi_hints)
		cxit_fi_hints = cxit_allocinfo_proto(proto);
}

void cxit_teardown_getinfo(void)
{
	fi_freeinfo(cxit_fi_hints);
	cxit_fi_hints = NULL;
}

void cxit_setup_fabric(void)
{
	cxit_setup_getinfo();
	cxit_create_fabric_info();
}

void cxit_teardown_fabric(void)
{
	cxit_destroy_fabric_info();
	cxit_teardown_getinfo();
}

void cxit_setup_domain(void)
{
	cxit_setup_fabric();
	cxit_create_fabric();
}

void cxit_teardown_domain(void)
{
	cxit_destroy_fabric();
	cxit_teardown_fabric();
}

void cxit_setup_ep(void)
{
	cxit_setup_domain();
	cxit_create_domain();
}

void cxit_teardown_ep(void)
{
	cxit_destroy_domain();
	cxit_teardown_domain();
}

void cxit_setup_enabled_rnr_msg_ep(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	/* Indicate we want to use the CS protocol */
	cxit_fi_hints->ep_attr->protocol = FI_PROTO_CXI_RNR;

	cxit_setup_ep();

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs();

	/* No FI_RMA_EVENT, don't create/bind remote counters */
	cxit_create_local_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_enabled_ep_disable_fi_rma_event(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	cxit_setup_ep();

	cxit_fi->caps &= ~FI_RMA_EVENT;
	cxit_fi->domain_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->tx_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->rx_attr->caps &= ~FI_RMA_EVENT;

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs();

	/* No FI_RMA_EVENT, don't create/bind remote counters */
	cxit_create_local_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_enabled_ep_mr_events(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	cxit_setup_ep();

	/* Enable FI_CXI_MR_MATCH_EVENTS via domain */
	ret = dom_ops->enable_mr_match_events(&cxit_domain->fid,
					      true);
	cr_assert_eq(ret, FI_SUCCESS);

	/* Disable RMA events to make sure MATCH_EVENTS on its own is
	 * sufficient to disallow atomic with FI_DELIVERY_COMPLETE.
	 */
	cxit_fi->caps &= ~FI_RMA_EVENT;
	cxit_fi->domain_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->tx_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->rx_attr->caps &= ~FI_RMA_EVENT;

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs();

	/* No FI_RMA_EVENT, so only create local counters */
	cxit_create_local_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_enabled_ep(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	cxit_fi_hints->tx_attr->size = 512;

	cxit_setup_ep();

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
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
}

void cxit_setup_enabled_ep_fd(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_rx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_tx_cq_attr.wait_obj = FI_WAIT_FD;
	cxit_rx_cq_attr.wait_obj = FI_WAIT_FD;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	cxit_setup_ep();

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
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
}

void cxit_setup_rma_disable_fi_rma_event(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_ep_disable_fi_rma_event();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

void cxit_setup_rma_mr_events(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};
	bool disable = false;

	cxit_setup_enabled_ep_mr_events();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);

	/* Ensure if FI_MR_PROV_KEY cache will not be used */
	fi_control(&cxit_domain->fid, FI_OPT_CXI_SET_PROV_KEY_CACHE, &disable);
}

void cxit_setup_rnr_msg_ep(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_rnr_msg_ep();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			  0, NULL);
	cr_assert(ret == 1);
}

void cxit_bind_cqs_hybrid_mr_desc(void)
{
	int ret;

	ret = fi_ep_bind(cxit_ep, &cxit_tx_cq->fid,
			 cxit_tx_cq_bind_flags | FI_SELECTIVE_COMPLETION);
	cr_assert(!ret, "fi_ep_bind TX CQ");

	ret = fi_ep_bind(cxit_ep, &cxit_rx_cq->fid,
			 cxit_rx_cq_bind_flags | FI_SELECTIVE_COMPLETION);
	cr_assert(!ret, "fi_ep_bind RX CQ");
}

void cxit_create_domain_hybrid_mr_desc(void)
{
	int ret;

	if (cxit_domain)
		return;

	ret = fi_domain(cxit_fabric, cxit_fi, &cxit_domain, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_domain");

	ret = fi_open_ops(&cxit_domain->fid, FI_CXI_DOM_OPS_3, 0,
			  (void **)&dom_ops, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_open_ops v2");
	cr_assert(dom_ops->cntr_read != NULL &&
		  dom_ops->topology != NULL &&
		  dom_ops->enable_hybrid_mr_desc != NULL,
		  "V3 functions returned");

	if (enable_cxi_hmem_ops) {
		ret = fi_set_ops(&cxit_domain->fid, FI_SET_OPS_HMEM_OVERRIDE, 0,
				 &cxi_hmem_ops, NULL);
		cr_assert(ret == FI_SUCCESS, "fi_set_ops");
	}

	ret = dom_ops->enable_hybrid_mr_desc(&cxit_domain->fid, true);
	cr_assert(ret == FI_SUCCESS, "enable_hybrid_mr_desc failed");
}

void cxit_setup_ep_hybrid_mr_desc(void)
{
	cxit_setup_domain();
	cxit_create_domain_hybrid_mr_desc();
}

void cxit_setup_enabled_ep_hybrid_mr_desc(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	cxit_setup_ep_hybrid_mr_desc();

	cxit_fi->caps &= ~FI_RMA_EVENT;
	cxit_fi->domain_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->tx_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->rx_attr->caps &= ~FI_RMA_EVENT;

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs_hybrid_mr_desc();

	/* No FI_RMA_EVENT, don't create/bind remote counters */
	cxit_create_local_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_rma_hybrid_mr_desc(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_ep_hybrid_mr_desc();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

void cxit_setup_enabled_rnr_ep_hybrid_mr_desc(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	/* Indicate we want to use the CS protocol */
	cxit_fi_hints->ep_attr->protocol = FI_PROTO_CXI_RNR;
	cxit_fi_hints->domain_attr->mr_mode = FI_MR_PROV_KEY | FI_MR_ALLOCATED |
					      FI_MR_ENDPOINT;

	cxit_setup_ep_hybrid_mr_desc();

	cxit_fi->caps &= ~FI_RMA_EVENT;
	cxit_fi->domain_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->tx_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->rx_attr->caps &= ~FI_RMA_EVENT;

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs_hybrid_mr_desc();

	/* No FI_RMA_EVENT, don't create/bind remote counters */
	cxit_create_local_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_enabled_rnr_ep_hybrid_mr_desc_byte_cntr(void)
{
	int ret;
	size_t addrlen = sizeof(cxit_ep_addr);

	cxit_setup_getinfo();

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_av_attr.type = FI_AV_TABLE;

	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	cxit_fi_hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;

	/* Indicate we want to use the CS protocol */
	cxit_fi_hints->ep_attr->protocol = FI_PROTO_CXI_RNR;
	cxit_fi_hints->domain_attr->mr_mode = FI_MR_PROV_KEY | FI_MR_ALLOCATED |
					      FI_MR_ENDPOINT;

	cxit_setup_ep_hybrid_mr_desc();

	cxit_fi->caps &= ~FI_RMA_EVENT;
	cxit_fi->domain_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->tx_attr->caps &= ~FI_RMA_EVENT;
	cxit_fi->rx_attr->caps &= ~FI_RMA_EVENT;

	/* Set up RMA objects */
	cxit_create_ep();
	cxit_create_eq();
	cxit_bind_eq();
	cxit_create_cqs();
	cxit_bind_cqs_hybrid_mr_desc();

	/* No FI_RMA_EVENT, don't create/bind remote counters */
	cxit_create_local_byte_cntrs();
	cxit_bind_cntrs();

	cxit_create_av();
	cxit_bind_av();

	ret = fi_enable(cxit_ep);
	cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

	/* Find assigned Endpoint address. Address is assigned during enable. */
	ret = fi_getname(&cxit_ep->fid, &cxit_ep_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "ret is %d\n", ret);
	cr_assert(addrlen == sizeof(cxit_ep_addr));
}

void cxit_setup_rma_rnr_hybrid_mr_desc(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_rnr_ep_hybrid_mr_desc();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

void cxit_setup_rma_rnr_hybrid_mr_desc_byte_cntr(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_rnr_ep_hybrid_mr_desc_byte_cntr();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

void cxit_setup_rma(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxip_coll_trace_append = true;
	cxip_coll_trace_muted = false;
	cxit_setup_enabled_ep();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

void cxit_teardown_rma(void)
{
	/* Tear down RMA objects */
	cxit_destroy_ep(); /* EP must be destroyed before bound objects */

	cxit_destroy_av();
	cxit_destroy_cntrs();
	cxit_destroy_cqs();
	cxit_destroy_eq();
	cxit_teardown_ep();
}

/* Use FI_WAIT_FD CQ wait object */
void cxit_setup_rma_fd(void)
{
	int ret;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_ep_fd();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);
}

#define CXI0_AMO_REMAP \
	"/sys/class/cxi/cxi0/device/properties/amo_remap_to_pcie_fadd"

void set_amo_remap_to_pcie_fadd(int amo_remap_to_pcie_fadd)
{
	FILE *fd;
	int ret;

	/* Assume open a single CXI device is present. */
	fd = fopen(CXI0_AMO_REMAP, "w");
	cr_assert(fd != NULL, "Failed to open %s: %d\n", CXI0_AMO_REMAP,
		  -errno);

	ret = fprintf(fd, "%d", amo_remap_to_pcie_fadd);
	cr_assert(ret >= 0,
		  "Failed to write AMO remap value: errno=%d\n", -errno);

	fclose(fd);
}

void reset_amo_remap_to_pcie_fadd(void)
{
	set_amo_remap_to_pcie_fadd(-1);
}

static void cxit_setup_tx_alias_rma_impl(bool delivery_complete)
{
	int ret;
	struct cxip_ep *cxi_ep;
	struct cxip_ep *cxi_alias_ep = NULL;
	uint64_t op_flags;
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};

	cxit_setup_enabled_ep();

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	/* Insert local address into AV to prepare to send to self */
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   0, NULL);
	cr_assert(ret == 1);

	/* Create TX alias EP */
	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);
	cr_assert(!(cxi_ep->tx_attr.op_flags & FI_RECV), "Bad op flags");

	op_flags = cxi_ep->tx_attr.op_flags | FI_TRANSMIT;
	if (delivery_complete)
		op_flags |= FI_DELIVERY_COMPLETE;
	ret = fi_ep_alias(cxit_ep, &cxit_tx_alias_ep, op_flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_alias");

	cxi_alias_ep = container_of(&cxit_tx_alias_ep->fid,
				    struct cxip_ep, ep.fid);
	cr_assert_not_null(cxi_alias_ep->ep_obj);
}

void cxit_setup_tx_alias_rma(void)
{
	cxit_setup_tx_alias_rma_impl(false);
}

void cxit_setup_tx_alias_rma_dc(void)
{
	cxit_setup_tx_alias_rma_impl(true);
}

void cxit_teardown_tx_alias_rma(void)
{
	struct cxip_ep *cxi_ep;
	int ret;

	cxi_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	ret = fi_close(&cxit_tx_alias_ep->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close alias endpoint");
	cr_assert_eq(ofi_atomic_get32(&cxi_ep->ep_obj->ref), 0,
		     "EP reference count");

	/* Tear down RMA objects */
	cxit_destroy_ep(); /* EP must be destroyed before bound objects */

	cxit_destroy_av();
	cxit_destroy_cntrs();
	cxit_destroy_cqs();
	cxit_destroy_eq();
	cxit_teardown_ep();
}

/* Everyone needs to wait sometime */
int cxit_await_completion(struct fid_cq *cq, struct fi_cq_tagged_entry *cqe)
{
	int ret;

	do {
		ret = fi_cq_read(cq, cqe, 1);
	} while (ret == -FI_EAGAIN);

	return ret;
}

void validate_tx_event(struct fi_cq_tagged_entry *cqe, uint64_t flags,
		       void *context)
{
	cr_assert(cqe->op_context == context, "TX CQE Context mismatch");
	cr_assert(cqe->flags == flags, "TX CQE flags mismatch");
	cr_assert(cqe->len == 0, "Invalid TX CQE length");
	cr_assert(cqe->buf == 0, "Invalid TX CQE address");
	cr_assert(cqe->data == 0, "Invalid TX CQE data");
	cr_assert(cqe->tag == 0, "Invalid TX CQE tag");
}

void validate_rx_event(struct fi_cq_tagged_entry *cqe, void *context,
		       size_t len, uint64_t flags, void *buf, uint64_t data,
		       uint64_t tag)
{
	cr_assert(cqe->op_context == context, "CQE Context mismatch");
	cr_assert(cqe->len == len, "Invalid CQE length");
	cr_assert(cqe->flags == flags, "CQE flags mismatch");
	cr_assert(cqe->buf == buf, "Invalid CQE address (%p %p)",
		  cqe->buf, buf);
	cr_assert(cqe->data == data, "Invalid CQE data");
	cr_assert(cqe->tag == tag, "Invalid CQE tag");
}

void validate_rx_event_mask(struct fi_cq_tagged_entry *cqe, void *context,
			    size_t len, uint64_t flags, void *buf,
			    uint64_t data, uint64_t tag, uint64_t ignore)
{
	cr_assert(cqe->op_context == context, "CQE Context mismatch");
	cr_assert(cqe->len == len, "Invalid CQE length: (%lu %lu)",
		  cqe->len, len);
	cr_assert(cqe->flags == flags, "CQE flags mismatch");
	cr_assert(cqe->buf == buf, "Invalid CQE address (%p %p)",
		  cqe->buf, buf);
	cr_assert(cqe->data == data, "Invalid CQE data");
	cr_assert(((cqe->tag & ~ignore) == (tag & ~ignore)), "Invalid CQE tag");
}

void validate_multi_recv_rx_event(struct fi_cq_tagged_entry *cqe, void
				  *context, size_t len, uint64_t flags,
				  uint64_t data, uint64_t tag)
{
	cr_assert(cqe->op_context == context, "CQE Context mismatch");
	cr_assert(cqe->len == len, "Invalid CQE length");
	cr_assert((cqe->flags & ~FI_MULTI_RECV) == flags,
		  "CQE flags mismatch (%#llx %#lx)",
		  (cqe->flags & ~FI_MULTI_RECV), flags);
	cr_assert(cqe->data == data, "Invalid CQE data");
	cr_assert(cqe->tag == tag, "Invalid CQE tag %#lx %#lx", cqe->tag, tag);
}

int mr_create_ext(size_t len, uint64_t access, uint8_t seed, uint64_t *key,
		  struct fid_cntr *cntr, struct mem_region *mr)
{
	int ret;

	cr_assert_not_null(mr);

	if (len) {
		mr->mem = calloc(1, len);
		cr_assert_not_null(mr->mem, "Error allocating memory window");
	} else {
		mr->mem = 0;
	}

	for (size_t i = 0; i < len; i++)
		mr->mem[i] = i + seed;

	ret = fi_mr_reg(cxit_domain, mr->mem, len, access, 0, *key, 0, &mr->mr,
			NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed %d", ret);
	ret = fi_mr_bind(mr->mr, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind(ep) failed %d", ret);

	if (cxit_fi->caps & FI_RMA_EVENT && cntr) {
		ret = fi_mr_bind(mr->mr, &cntr->fid, FI_REMOTE_WRITE);
		cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind(cntr) failed %d",
			     ret);
	}

	ret = fi_mr_enable(mr->mr);
	if (!ret)
		*key = fi_mr_key(mr->mr);

	return ret;
}

int mr_create(size_t len, uint64_t access, uint8_t seed, uint64_t *key,
	      struct mem_region *mr)
{
	return mr_create_ext(len, access, seed, key, cxit_rem_cntr, mr);
}

void mr_destroy(struct mem_region *mr)
{
	fi_close(&mr->mr->fid);
	free(mr->mem);
}
