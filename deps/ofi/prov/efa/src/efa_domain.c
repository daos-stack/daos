/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2013-2015 Intel Corporation, Inc.  All rights reserved. */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include <assert.h>
#include <ofi_util.h>

#include "config.h"
#include "efa.h"
#include "efa_av.h"
#include "efa_cntr.h"
#include "rdm/efa_rdm_cq.h"
#include "rdm/efa_rdm_atomic.h"
#include "dgram/efa_dgram_ep.h"
#include "dgram/efa_dgram_cq.h"


struct dlist_entry g_efa_domain_list;

static int efa_domain_close(fid_t fid);

static int efa_domain_ops_open(struct fid *fid, const char *ops_name,
				uint64_t flags, void **ops, void *context);

static struct fi_ops efa_ops_domain_fid = {
	.size = sizeof(struct fi_ops),
	.close = efa_domain_close,
	.bind = fi_no_bind,
	.control = fi_no_control,
	.ops_open = efa_domain_ops_open,
};

static struct fi_ops_domain efa_ops_domain_dgram = {
	.size = sizeof(struct fi_ops_domain),
	.av_open = efa_av_open,
	.cq_open = efa_dgram_cq_open,
	.endpoint = efa_dgram_ep_open,
	.scalable_ep = fi_no_scalable_ep,
	.cntr_open = efa_cntr_open,
	.poll_open = fi_no_poll_open,
	.stx_ctx = fi_no_stx_context,
	.srx_ctx = fi_no_srx_context,
	.query_atomic = fi_no_query_atomic,
	.query_collective = fi_no_query_collective,
};

static struct fi_ops_domain efa_ops_domain_rdm = {
	.size = sizeof(struct fi_ops_domain),
	.av_open = efa_av_open,
	.cq_open = efa_rdm_cq_open,
	.endpoint = efa_rdm_ep_open,
	.scalable_ep = fi_no_scalable_ep,
	.cntr_open = efa_cntr_open,
	.poll_open = fi_poll_create,
	.stx_ctx = fi_no_stx_context,
	.srx_ctx = fi_no_srx_context,
	.query_atomic = efa_rdm_atomic_query,
	.query_collective = fi_no_query_collective,
};

/**
 * @brief init the device and ibv_pd field in efa_domain
 *
 * @param efa_domain[in,out]	efa domain to be set.
 * @param domain_name		domain name
 * @param ep_type		endpoint type
 * @return 0 if efa_domain->device and efa_domain->ibv_pd has been set successfully
 *         negative error code if err is encountered
 */
static int efa_domain_init_device_and_pd(struct efa_domain *efa_domain,
                                         const char *domain_name,
                                         enum fi_ep_type ep_type)
{
	int i;
	char *device_name = NULL;
	const char *domain_name_suffix = efa_domain_name_suffix(ep_type);

	if (!domain_name)
		return -FI_EINVAL;

	for (i = 0; i < g_device_cnt; i++) {
		device_name = g_device_list[i].ibv_ctx->device->name;
		if (strstr(domain_name, device_name) == domain_name &&
		    strlen(domain_name) - strlen(device_name) ==
		            strlen(domain_name_suffix) &&
		    strcmp((const char *) (domain_name + strlen(device_name)),
		           domain_name_suffix) == 0) {
			efa_domain->device = &g_device_list[i];
			break;
		}
	}

	if (i == g_device_cnt)
		return -FI_ENODEV;

	EFA_INFO(FI_LOG_DOMAIN, "Domain %s selected device %s\n", domain_name, device_name);
	efa_domain->ibv_pd = efa_domain->device->ibv_pd;
	return 0;
}

static int efa_domain_init_qp_table(struct efa_domain *efa_domain)
{
	size_t qp_table_size;

	qp_table_size = roundup_power_of_two(efa_domain->device->ibv_attr.max_qp);
	efa_domain->qp_table_sz_m1 = qp_table_size - 1;
	efa_domain->qp_table = calloc(qp_table_size, sizeof(*efa_domain->qp_table));
	if (!efa_domain->qp_table)
		return -FI_ENOMEM;

	return 0;
}

static int efa_domain_init_rdm(struct efa_domain *efa_domain, struct fi_info *info)
{
	int err;
	bool enable_shm = efa_env.enable_shm_transfer;

	/* App provided hints supercede environmental variables.
	 *
	 * Using the shm provider comes with some overheads, so avoid
	 * initializing the provider if the app provides a hint that it does not
	 * require node-local communication. We can still loopback over the EFA
	 * device in cases where the app violates the hint and continues
	 * communicating with node-local peers.
	 *
	 */
	if ((info->caps & FI_REMOTE_COMM)
	    /* but not local communication */
	    && !(info->caps & FI_LOCAL_COMM)) {
		enable_shm = false;
	}

	efa_domain->shm_info = NULL;
	if (enable_shm)
		efa_shm_info_create(info, &efa_domain->shm_info);
	else
		EFA_INFO(FI_LOG_CORE, "EFA will not use SHM for intranode communication because FI_EFA_ENABLE_SHM_TRANSFER=0\n");

	if (efa_domain->shm_info) {
		err = fi_fabric(efa_domain->shm_info->fabric_attr,
				&efa_domain->fabric->shm_fabric,
				efa_domain->fabric->util_fabric.fabric_fid.fid.context);
		if (err)
			return err;
	} else {
		efa_domain->fabric->shm_fabric = NULL;
	}

	if (efa_domain->fabric->shm_fabric) {
		err = fi_domain(efa_domain->fabric->shm_fabric, efa_domain->shm_info,
				&efa_domain->shm_domain, NULL);
		if (err)
			return err;
	}

	efa_domain->rdm_mode = info->mode;
	efa_domain->mtu_size = efa_domain->device->ibv_port_attr.max_msg_sz;
	efa_domain->addrlen = (info->src_addr) ? info->src_addrlen : info->dest_addrlen;
	efa_domain->rdm_cq_size = MAX(info->rx_attr->size + info->tx_attr->size,
				  efa_env.cq_size);
	efa_domain->num_read_msg_in_flight = 0;

	dlist_init(&efa_domain->ope_queued_list);
	dlist_init(&efa_domain->ope_longcts_send_list);
	dlist_init(&efa_domain->peer_backoff_list);
	dlist_init(&efa_domain->handshake_queued_peer_list);
	return 0;
}

/* @brief Allocate a domain, open the device, and set it up based on the hints.
 *
 * This function creates a domain and uses the info struct to configure the
 * domain based on what capabilities are set. Fork support is checked here and
 * the MR cache is also set up here.
 *
 * @param fabric_fid fabric that the domain should be tied to
 * @param info info struct that was validated and returned by fi_getinfo
 * @param domain_fid pointer where newly domain fid should be stored
 * @param context void pointer stored with the domain fid
 * @return 0 on success, fi_errno on error
 */
int efa_domain_open(struct fid_fabric *fabric_fid, struct fi_info *info,
		    struct fid_domain **domain_fid, void *context)
{
	struct efa_domain *efa_domain;
	int ret = 0, err;

	efa_domain = calloc(1, sizeof(struct efa_domain));
	if (!efa_domain)
		return -FI_ENOMEM;

	dlist_init(&efa_domain->list_entry);
	efa_domain->fabric = container_of(fabric_fid, struct efa_fabric,
					  util_fabric.fabric_fid);

	err = ofi_domain_init(fabric_fid, info, &efa_domain->util_domain,
			      context, OFI_LOCK_MUTEX);
	if (err) {
		ret = err;
		goto err_free;
	}

	efa_domain->ibv_mr_reg_ct = 0;
	efa_domain->ibv_mr_reg_sz = 0;

	err = ofi_genlock_init(&efa_domain->srx_lock, efa_domain->util_domain.threading != FI_THREAD_SAFE ?
			       OFI_LOCK_NOOP : OFI_LOCK_MUTEX);
	if (err) {
		EFA_WARN(FI_LOG_DOMAIN, "srx lock init failed! err: %d\n", err);
		ret = err;
		goto err_free;
	}

	efa_domain->util_domain.av_type = FI_AV_TABLE;
	efa_domain->util_domain.mr_map.mode |= FI_MR_VIRT_ADDR;
	/*
	 * FI_MR_PROV_KEY means provider will generate a key for MR,
	 * which EFA provider does by using key generated by EFA device.
	 *
	 * util_domain.mr_map.mode is same as info->mode, which has
	 * the bit FI_MR_PROV_KEY on. When the bit is on, util_domain.mr_map
	 * will generate a key for MR, which is not what we want
	 * (we want to use the key generated by device). Therefore unset
	 * the FI_MR_PROV_KEY bit of mr_map.
	 */
	efa_domain->util_domain.mr_map.mode &= ~FI_MR_PROV_KEY;

	if (!info->ep_attr || info->ep_attr->type == FI_EP_UNSPEC) {
		EFA_WARN(FI_LOG_DOMAIN, "ep type not specified when creating domain\n");
		return -FI_EINVAL;
	}

	efa_domain->mr_local = ofi_mr_local(info);
	if (EFA_EP_TYPE_IS_DGRAM(info) && !efa_domain->mr_local) {
		EFA_WARN(FI_LOG_EP_DATA, "dgram require FI_MR_LOCAL, but application does not support it\n");
		ret = -FI_ENODATA;
		goto err_free;
	}

	err = efa_domain_init_device_and_pd(efa_domain, info->domain_attr->name, info->ep_attr->type);
	if (err) {
		ret = err;
		goto err_free;
	}

	efa_domain->info = fi_dupinfo(EFA_EP_TYPE_IS_RDM(info) ? efa_domain->device->rdm_info : efa_domain->device->dgram_info);
	if (!efa_domain->info) {
		ret = -FI_ENOMEM;
		goto err_free;
	}

	*domain_fid = &efa_domain->util_domain.domain_fid;

	err = efa_domain_init_qp_table(efa_domain);
	if (err) {
		ret = err;
		EFA_WARN(FI_LOG_DOMAIN, "Failed to init qp table. err: %d\n", ret);
		goto err_free;
	}

	/*
	 * FI_MR_LOCAL means application will handle memory registration by itself.
	 * Therefore when FI_MR_LOCAL is on, MR cache is not necessary.
	 */
	if (!efa_domain->mr_local && efa_mr_cache_enable) {
		err = efa_mr_cache_open(&efa_domain->cache, efa_domain);
		if (err) {
			ret = err;
			goto err_free;
		}

		efa_domain->util_domain.domain_fid.mr = &efa_domain_mr_cache_ops;
	} else {
		efa_domain->util_domain.domain_fid.mr = &efa_domain_mr_ops;
	}

	efa_domain->util_domain.domain_fid.fid.ops = &efa_ops_domain_fid;
	if (EFA_EP_TYPE_IS_RDM(info)) {
		err = efa_domain_init_rdm(efa_domain, info);
		if (err) {
			EFA_WARN(FI_LOG_DOMAIN,
				 "efa_domain_init_rdm failed. err: %d\n",
				 -err);
			goto err_free;
		}
		efa_domain->util_domain.domain_fid.ops = &efa_ops_domain_rdm;
	} else {
		assert(EFA_EP_TYPE_IS_DGRAM(info));
		efa_domain->util_domain.domain_fid.ops = &efa_ops_domain_dgram;
	}

	err = efa_fork_support_enable_if_requested(*domain_fid);
	if (err) {
		ret = err;
		EFA_WARN(FI_LOG_DOMAIN, "Failed to initialize fork support. err: %d\n", ret);
		goto err_free;
	}

	err = efa_domain_hmem_info_init_all(efa_domain);
	if (err) {
		ret = err;
		EFA_WARN(FI_LOG_DOMAIN, "Failed to check hmem support status. err: %d\n", ret);
		goto err_free;
	}

	dlist_insert_tail(&efa_domain->list_entry, &g_efa_domain_list);
	return 0;

err_free:
	assert(efa_domain);

	err = efa_domain_close(&efa_domain->util_domain.domain_fid.fid);
	if (err) {
		EFA_WARN(FI_LOG_DOMAIN, "When handling error (%d), domain resource was being released."
			 "During the release process, an addtional error (%d) was encoutered\n",
			 -ret, -err);
	}

	efa_domain = NULL;
	*domain_fid = NULL;
	return ret;
}

static int efa_domain_close(fid_t fid)
{
	struct efa_domain *efa_domain;
	int ret;

	efa_domain = container_of(fid, struct efa_domain,
				  util_domain.domain_fid.fid);

	dlist_remove(&efa_domain->list_entry);

	if (efa_domain->cache) {
		ofi_mr_cache_cleanup(efa_domain->cache);
		free(efa_domain->cache);
		efa_domain->cache = NULL;
	}

	if (efa_domain->ibv_pd) {
		efa_domain->ibv_pd = NULL;
	}

	ret = ofi_domain_close(&efa_domain->util_domain);
	if (ret)
		return ret;

	if (efa_domain->shm_domain) {
		ret = fi_close(&efa_domain->shm_domain->fid);
		if (ret)
			return ret;
	}

	if (efa_domain->shm_info)
		fi_freeinfo(efa_domain->shm_info);

	if (efa_domain->info)
		fi_freeinfo(efa_domain->info);

	ofi_genlock_destroy(&efa_domain->srx_lock);
	free(efa_domain->qp_table);
	free(efa_domain);
	return 0;
}

/**
 * @brief Query EFA specific Memory Region attributes
 *
 * @param mr ptr to fid_mr
 * @param mr_attr  ptr to fi_efa_mr_attr
 * @return int 0 on success, negative integer on failure
 */
#if HAVE_EFADV_QUERY_MR

static int
efa_domain_query_mr(struct fid_mr *mr_fid, struct fi_efa_mr_attr *mr_attr)
{
	struct efadv_mr_attr attr = {0};
	struct efa_mr *efa_mr;
	int ret;

	memset(mr_attr, 0, sizeof(*mr_attr));

	efa_mr = container_of(mr_fid, struct efa_mr, mr_fid);
	ret = efadv_query_mr(efa_mr->ibv_mr, &attr, sizeof(attr));
	if (ret) {
		EFA_WARN(FI_LOG_DOMAIN, "efadv_query_mr failed. err: %d\n", ret);
		return ret;
	}

	/* Translate the validity masks and bus_id from efadv_mr_attr to fi_efa_mr_attr */
	if (attr.ic_id_validity & EFADV_MR_ATTR_VALIDITY_RECV_IC_ID) {
		mr_attr->recv_ic_id = attr.recv_ic_id;
		mr_attr->ic_id_validity |= FI_EFA_MR_ATTR_RECV_IC_ID;
	}

	if (attr.ic_id_validity & EFADV_MR_ATTR_VALIDITY_RDMA_READ_IC_ID) {
		mr_attr->rdma_read_ic_id = attr.rdma_read_ic_id;
		mr_attr->ic_id_validity |= FI_EFA_MR_ATTR_RDMA_READ_IC_ID;
	}

	if (attr.ic_id_validity & EFADV_MR_ATTR_VALIDITY_RDMA_RECV_IC_ID) {
		mr_attr->rdma_recv_ic_id = attr.rdma_recv_ic_id;
		mr_attr->ic_id_validity |= FI_EFA_MR_ATTR_RDMA_RECV_IC_ID;
	}

	return FI_SUCCESS;
}

#else

static int
efa_domain_query_mr(struct fid_mr *mr, struct fi_efa_mr_attr *mr_attr)
{
	return -FI_ENOSYS;
}

#endif /* HAVE_EFADV_QUERY_MR */

static struct fi_efa_ops_domain efa_ops_domain = {
	.query_mr = efa_domain_query_mr,
};

static int
efa_domain_ops_open(struct fid *fid, const char *ops_name, uint64_t flags,
		     void **ops, void *context)
{
	int ret = FI_SUCCESS;

	if (strcmp(ops_name, FI_EFA_DOMAIN_OPS) == 0) {
		*ops = &efa_ops_domain;
	} else {
		EFA_WARN(FI_LOG_DOMAIN,
			"Unknown ops name: %s\n", ops_name);
		ret = -FI_EINVAL;
	}

	return ret;
}

void efa_domain_progress_rdm_peers_and_queues(struct efa_domain *domain)
{
	struct efa_rdm_peer *peer;
	struct dlist_entry *tmp;
	struct efa_rdm_ope *ope;
	int ret;

	assert(domain->info->ep_attr->type == FI_EP_RDM);

	/* Update timers for peers that are in backoff list*/
	dlist_foreach_container_safe(&domain->peer_backoff_list, struct efa_rdm_peer,
				     peer, rnr_backoff_entry, tmp) {
		if (ofi_gettime_us() >= peer->rnr_backoff_begin_ts +
					peer->rnr_backoff_wait_time) {
			peer->flags &= ~EFA_RDM_PEER_IN_BACKOFF;
			dlist_remove(&peer->rnr_backoff_entry);
		}
	}

	/*
	 * Resend handshake packet for any peers where the first
	 * handshake send failed.
	 */
	dlist_foreach_container_safe(&domain->handshake_queued_peer_list,
				     struct efa_rdm_peer, peer,
				     handshake_queued_entry, tmp) {
		if (peer->flags & EFA_RDM_PEER_IN_BACKOFF)
			continue;

		ret = efa_rdm_ep_post_handshake(peer->ep, peer);
		if (ret == -FI_EAGAIN)
			continue;

		if (OFI_UNLIKELY(ret)) {
			EFA_WARN(FI_LOG_EP_CTRL,
				"Failed to post HANDSHAKE to peer %ld: %s\n",
				peer->efa_fiaddr, fi_strerror(-ret));
			efa_base_ep_write_eq_error(&peer->ep->base_ep, -ret, FI_EFA_ERR_PEER_HANDSHAKE);
			continue;
		}

		dlist_remove(&peer->handshake_queued_entry);
		peer->flags &= ~EFA_RDM_PEER_HANDSHAKE_QUEUED;
		peer->flags |= EFA_RDM_PEER_HANDSHAKE_SENT;
	}

	/*
	 * Repost pkts for all queued op entries
	 */
	dlist_foreach_container_safe(&domain->ope_queued_list,
				     struct efa_rdm_ope,
				     ope, queued_entry, tmp) {
		peer = efa_rdm_ep_get_peer(ope->ep, ope->addr);

		if (peer && (peer->flags & EFA_RDM_PEER_IN_BACKOFF))
			continue;

		if (ope->internal_flags & EFA_RDM_OPE_QUEUED_BEFORE_HANDSHAKE) {
			ret = efa_rdm_ope_repost_ope_queued_before_handshake(ope);
			if (ret == -FI_EAGAIN)
				continue;

			if (OFI_UNLIKELY(ret)) {
				assert(ope->type == EFA_RDM_TXE);
				/* efa_rdm_txe_handle_error will remove ope from the queued_list */
				ope->ep->ope_queued_before_handshake_cnt--;
				efa_rdm_txe_handle_error(ope, -ret, FI_EFA_ERR_PKT_POST);
				continue;
			}

			dlist_remove(&ope->queued_entry);
			ope->internal_flags &= ~EFA_RDM_OPE_QUEUED_BEFORE_HANDSHAKE;
			ope->ep->ope_queued_before_handshake_cnt--;
		}

		if (ope->internal_flags & EFA_RDM_OPE_QUEUED_RNR) {
			assert(!dlist_empty(&ope->queued_pkts));
			ret = efa_rdm_ep_post_queued_pkts(ope->ep, &ope->queued_pkts);

			if (ret == -FI_EAGAIN)
				continue;

			if (OFI_UNLIKELY(ret)) {
				assert(ope->type == EFA_RDM_RXE || ope->type == EFA_RDM_TXE);
				if (ope->type == EFA_RDM_RXE)
					efa_rdm_rxe_handle_error(ope, -ret, FI_EFA_ERR_PKT_SEND);
				else
					efa_rdm_txe_handle_error(ope, -ret, FI_EFA_ERR_PKT_SEND);
				continue;
			}

			dlist_remove(&ope->queued_entry);
			ope->internal_flags &= ~EFA_RDM_OPE_QUEUED_RNR;
		}

		if (ope->internal_flags & EFA_RDM_OPE_QUEUED_CTRL) {
			ret = efa_rdm_ope_post_send(ope, ope->queued_ctrl_type);
			if (ret == -FI_EAGAIN)
				continue;

			if (OFI_UNLIKELY(ret)) {
				assert(ope->type == EFA_RDM_TXE || ope->type == EFA_RDM_RXE);
				if (ope->type == EFA_RDM_TXE)
					efa_rdm_txe_handle_error(ope, -ret, FI_EFA_ERR_PKT_POST);
				else
					efa_rdm_rxe_handle_error(ope, -ret, FI_EFA_ERR_PKT_POST);
				continue;
			}

			/* it can happen that efa_rdm_ope_post_send() released ope
			 * (if the ope is rxe and packet type is EOR and inject is used). In
			 * that case rxe's state has been set to EFA_RDM_OPE_FREE and
			 * it has been removed from ep->op_queued_entry_list, so nothing
			 * is left to do.
			 */
			if (ope->state == EFA_RDM_OPE_FREE)
				continue;

			ope->internal_flags &= ~EFA_RDM_OPE_QUEUED_CTRL;
			dlist_remove(&ope->queued_entry);
		}

		if (ope->internal_flags & EFA_RDM_OPE_QUEUED_READ) {
			ret = efa_rdm_ope_post_read(ope);
			if (ret == -FI_EAGAIN)
				continue;

			if (OFI_UNLIKELY(ret)) {
				assert(ope->type == EFA_RDM_TXE || ope->type == EFA_RDM_RXE);
				if (ope->type == EFA_RDM_TXE)
					efa_rdm_txe_handle_error(ope, -ret, FI_EFA_ERR_READ_POST);
				else
					efa_rdm_rxe_handle_error(ope, -ret, FI_EFA_ERR_READ_POST);
				continue;
			}

			ope->internal_flags &= ~EFA_RDM_OPE_QUEUED_READ;
			dlist_remove(&ope->queued_entry);
		}
	}
	/*
	 * Send data packets until window or data queue is exhausted.
	 */
	dlist_foreach_container(&domain->ope_longcts_send_list, struct efa_rdm_ope,
				ope, entry) {
		peer = efa_rdm_ep_get_peer(ope->ep, ope->addr);
		assert(peer);
		if (peer->flags & EFA_RDM_PEER_IN_BACKOFF)
			continue;

		/*
		 * Do not send DATA packet until we received HANDSHAKE packet from the peer,
		 * this is because endpoint does not know whether peer need connid in header
		 * until it get the HANDSHAKE packet.
		 *
		 * We only do this for DATA packet because other types of packets always
		 * has connid in there packet header. If peer does not make use of the connid,
		 * the connid can be safely ignored.
		 *
		 * DATA packet is different because for DATA packet connid is an optional
		 * header inserted between the mandatory header and the application data.
		 * Therefore if peer does not use/understand connid, it will take connid
		 * as application data thus cause data corruption.
		 *
		 * This will not cause deadlock because peer will send a HANDSHAKE packet
		 * back upon receiving 1st packet from the endpoint, and in all 3 sub0protocols
		 * (long-CTS message, emulated long-CTS write and emulated long-CTS read)
		 * where DATA packet is used, endpoint will send other types of packet to
		 * peer before sending DATA packets. The workflow of the 3 sub-protocol
		 * can be found in protocol v4 document chapter 3.
		 */
		if (!(peer->flags & EFA_RDM_PEER_HANDSHAKE_RECEIVED))
			continue;

		if (ope->window > 0) {
			ret = efa_rdm_ope_post_send(ope, EFA_RDM_CTSDATA_PKT);
			if (OFI_UNLIKELY(ret)) {
				if (ret == -FI_EAGAIN)
					continue;

				efa_rdm_txe_handle_error(ope, -ret, FI_EFA_ERR_PKT_POST);
				continue;
			}
		}
	}
}
