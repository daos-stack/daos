/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "transport.h"

#include <errno.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
#include <string.h>

struct crt_native_mr {
	struct crt_native_ep *owner;
	struct ibv_mr        *mr;
	unsigned              references;
};

struct operation {
	struct crt_native_mr  *mr;
	void                  *cookie;
	enum crt_native_opcode opcode;
	size_t                 length;
	unsigned               next_free;
	bool                   active;
};

struct crt_native_ep {
	struct ibv_context       *context;
	struct ibv_pd            *pd;
	struct ibv_cq            *cq;
	struct ibv_qp            *qp;
	struct crt_native_config  config;
	struct crt_native_address local;
	struct operation         *operations;
	unsigned                  outstanding;
	unsigned                  free_head;
	unsigned                  registrations;
	bool                      connected;
	bool                      closing;
};

static int
verbs_close(struct crt_native_ep *ep)
{
	int rc;

	if (ep == NULL)
		return EINVAL;
	if (ep->outstanding != 0 || ep->registrations != 0)
		return EBUSY;
	/* Keep remaining resources reachable if a destroy operation fails. */
	if (ep->qp != NULL) {
		rc = ibv_destroy_qp(ep->qp);
		if (rc != 0)
			return rc;
		ep->qp = NULL;
	}
	if (ep->cq != NULL) {
		rc = ibv_destroy_cq(ep->cq);
		if (rc != 0)
			return rc;
		ep->cq = NULL;
	}
	if (ep->pd != NULL) {
		rc = ibv_dealloc_pd(ep->pd);
		if (rc != 0)
			return rc;
		ep->pd = NULL;
	}
	if (ep->context != NULL) {
		rc = ibv_close_device(ep->context);
		if (rc != 0)
			return rc;
	}
	free(ep->operations);
	free(ep);
	return 0;
}

static int
verbs_open(const struct crt_native_config *cfg, struct crt_native_ep **output)
{
	struct ibv_device     **devices;
	struct ibv_port_attr    port;
	struct ibv_qp_init_attr init = {0};
	struct ibv_qp_attr      attr = {0};
	struct crt_native_ep   *ep;
	int                     count, rc = ENODEV;

	if (output == NULL)
		return EINVAL;
	*output = NULL;
	if (cfg == NULL || cfg->device == NULL || cfg->port == 0 || cfg->depth == 0 ||
	    cfg->depth > 32768 || cfg->gid_index < -1 || cfg->gid_index > 255)
		return EINVAL;
	ep = calloc(1, sizeof(*ep));
	if (ep == NULL)
		return ENOMEM;
	ep->config     = *cfg;
	ep->operations = calloc(cfg->depth, sizeof(*ep->operations));
	if (ep->operations == NULL) {
		rc = ENOMEM;
		goto fail;
	}
	for (unsigned i = 0; i < cfg->depth; i++)
		ep->operations[i].next_free = i + 1;
	devices = ibv_get_device_list(&count);
	if (devices == NULL) {
		rc = errno != 0 ? errno : ENODEV;
		goto fail;
	}
	for (int i = 0; i < count; i++) {
		if (strcmp(cfg->device, ibv_get_device_name(devices[i])) == 0) {
			ep->context = ibv_open_device(devices[i]);
			if (ep->context == NULL)
				rc = errno != 0 ? errno : EIO;
			break;
		}
	}
	ibv_free_device_list(devices);
	if (ep->context == NULL)
		goto fail;
	rc = ibv_query_port(ep->context, cfg->port, &port);
	if (rc != 0)
		goto fail;
	if (port.state != IBV_PORT_ACTIVE) {
		rc = ENETDOWN;
		goto fail;
	}
	ep->local.lid    = port.lid;
	ep->local.mtu    = port.active_mtu;
	ep->local.global = cfg->gid_index >= 0;
	if (port.link_layer == IBV_LINK_LAYER_ETHERNET && !ep->local.global) {
		rc = EINVAL;
		goto fail;
	}
	if (ep->local.global) {
		union ibv_gid gid;

		rc = ibv_query_gid(ep->context, cfg->port, cfg->gid_index, &gid);
		if (rc != 0)
			goto fail;
		memcpy(ep->local.gid, gid.raw, sizeof(ep->local.gid));
	}
	ep->pd = ibv_alloc_pd(ep->context);
	if (ep->pd == NULL) {
		rc = errno != 0 ? errno : ENOMEM;
		goto fail;
	}
	ep->cq = ibv_create_cq(ep->context, (int)cfg->depth, NULL, NULL, 0);
	if (ep->cq == NULL) {
		rc = errno != 0 ? errno : ENOMEM;
		goto fail;
	}
	init.send_cq          = ep->cq;
	init.recv_cq          = ep->cq;
	init.qp_type          = IBV_QPT_RC;
	init.cap.max_send_wr  = cfg->depth;
	init.cap.max_recv_wr  = cfg->depth;
	init.cap.max_send_sge = 1;
	init.cap.max_recv_sge = 1;
	ep->qp                = ibv_create_qp(ep->pd, &init);
	if (ep->qp == NULL) {
		rc = errno != 0 ? errno : ENOMEM;
		goto fail;
	}
	ep->local.qpn = ep->qp->qp_num;
	/* PSNs need not be secret. They must match the peer's receive PSN. */
	ep->local.psn        = ep->local.qpn & 0xffffff;
	attr.qp_state        = IBV_QPS_INIT;
	attr.port_num        = cfg->port;
	attr.pkey_index      = 0;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	rc                   = ibv_modify_qp(ep->qp, &attr,
					     IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS);
	if (rc != 0)
		goto fail;
	*output = ep;
	return 0;
fail:
	/* No work has been posted, so cleanup cannot abandon accepted operations. */
	verbs_close(ep);
	return rc;
}

static int
verbs_address(struct crt_native_ep *ep, struct crt_native_address *address)
{
	if (ep == NULL || address == NULL || ep->closing)
		return EINVAL;
	*address = ep->local;
	return 0;
}

static int
verbs_connect(struct crt_native_ep *ep, const struct crt_native_address *remote)
{
	struct ibv_qp_attr attr = {0};
	int                rc;

	if (ep == NULL || remote == NULL || ep->closing || ep->connected || remote->qpn == 0 ||
	    remote->qpn > 0xffffff || remote->psn > 0xffffff || remote->mtu < IBV_MTU_256 ||
	    remote->mtu > IBV_MTU_4096 || remote->global != ep->local.global)
		return EINVAL;
	attr.qp_state           = IBV_QPS_RTR;
	attr.path_mtu           = remote->mtu < ep->local.mtu ? remote->mtu : ep->local.mtu;
	attr.dest_qp_num        = remote->qpn;
	attr.rq_psn             = remote->psn;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer      = 12;
	attr.ah_attr.port_num   = ep->config.port;
	attr.ah_attr.dlid       = remote->lid;
	attr.ah_attr.is_global  = remote->global;
	if (remote->global) {
		memcpy(attr.ah_attr.grh.dgid.raw, remote->gid, sizeof(remote->gid));
		attr.ah_attr.grh.sgid_index = ep->config.gid_index;
		attr.ah_attr.grh.hop_limit  = 64;
	}
	rc = ibv_modify_qp(ep->qp, &attr,
			   IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			       IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
	if (rc != 0)
		return rc;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state      = IBV_QPS_RTS;
	attr.sq_psn        = ep->local.psn;
	attr.timeout       = 14;
	attr.retry_cnt     = 7;
	attr.rnr_retry     = 7;
	attr.max_rd_atomic = 1;
	rc                 = ibv_modify_qp(ep->qp, &attr,
					   IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
					       IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
	if (rc == 0)
		ep->connected = true;
	else
		ep->closing = true;
	return rc;
}

static int
verbs_register(struct crt_native_ep *ep, void *buffer, size_t length, unsigned remote_access,
	       struct crt_native_mr **output, struct crt_native_remote_memory *remote)
{
	struct crt_native_mr *mr;
	int                   access = IBV_ACCESS_LOCAL_WRITE;

	if (output == NULL)
		return EINVAL;
	*output = NULL;
	if (ep == NULL || buffer == NULL || length == 0 || ep->closing || remote == NULL ||
	    (remote_access & ~(CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE)) != 0)
		return EINVAL;
	if (remote_access & CRT_NATIVE_REMOTE_READ)
		access |= IBV_ACCESS_REMOTE_READ;
	if (remote_access & CRT_NATIVE_REMOTE_WRITE)
		access |= IBV_ACCESS_REMOTE_WRITE;
	mr = calloc(1, sizeof(*mr));
	if (mr == NULL)
		return ENOMEM;
	mr->mr = ibv_reg_mr(ep->pd, buffer, length, access);
	if (mr->mr == NULL) {
		int rc = errno != 0 ? errno : ENOMEM;

		free(mr);
		return rc;
	}
	mr->owner = ep;
	ep->registrations++;
	*remote = (struct crt_native_remote_memory){
	    .address = (uintptr_t)buffer,
	    .length  = length,
	    .key     = remote_access ? mr->mr->rkey : 0,
	};
	*output = mr;
	return 0;
}

static int
verbs_deregister(struct crt_native_mr *mr)
{
	int rc;

	if (mr == NULL)
		return EINVAL;
	if (mr->references != 0)
		return EBUSY;
	rc = ibv_dereg_mr(mr->mr);
	if (rc == 0) {
		mr->owner->registrations--;
		free(mr);
	}
	return rc;
}

static int
verbs_post(struct crt_native_ep *ep, enum crt_native_opcode opcode, struct crt_native_mr *mr,
	   size_t offset, size_t length, const struct crt_native_remote_memory *remote,
	   size_t remote_offset, void *cookie)
{
	struct ibv_sge     sge;
	struct ibv_send_wr send = {0}, *bad_send;
	struct ibv_recv_wr recv = {0}, *bad_recv;
	unsigned           slot;
	int                rc;

	if (ep == NULL || mr == NULL || mr->owner != ep || opcode < CRT_NATIVE_SEND ||
	    opcode > CRT_NATIVE_WRITE || length == 0 || length > UINT32_MAX ||
	    offset > mr->mr->length || length > mr->mr->length - offset)
		return EINVAL;
	if (ep->closing)
		return ESHUTDOWN;
	if (!ep->connected && opcode != CRT_NATIVE_RECV)
		return ENOTCONN;
	if (opcode == CRT_NATIVE_READ || opcode == CRT_NATIVE_WRITE) {
		if (remote == NULL || remote_offset > remote->length ||
		    length > remote->length - remote_offset ||
		    remote_offset > UINT64_MAX - remote->address ||
		    length > UINT64_MAX - (remote->address + remote_offset))
			return EINVAL;
	}
	slot = ep->free_head;
	if (slot == ep->config.depth)
		return EAGAIN;
	sge = (struct ibv_sge){.addr   = (uintptr_t)mr->mr->addr + offset,
			       .length = (uint32_t)length,
			       .lkey   = mr->mr->lkey};
	if (opcode == CRT_NATIVE_RECV) {
		recv.wr_id   = slot;
		recv.sg_list = &sge;
		recv.num_sge = 1;
		rc           = ibv_post_recv(ep->qp, &recv, &bad_recv);
	} else {
		send.wr_id      = slot;
		send.sg_list    = &sge;
		send.num_sge    = 1;
		send.send_flags = IBV_SEND_SIGNALED;
		send.opcode     = opcode == CRT_NATIVE_SEND   ? IBV_WR_SEND
				  : opcode == CRT_NATIVE_READ ? IBV_WR_RDMA_READ
							      : IBV_WR_RDMA_WRITE;
		if (opcode != CRT_NATIVE_SEND) {
			send.wr.rdma.remote_addr = remote->address + remote_offset;
			send.wr.rdma.rkey        = remote->key;
		}
		rc = ibv_post_send(ep->qp, &send, &bad_send);
	}
	if (rc == 0) {
		ep->free_head        = ep->operations[slot].next_free;
		ep->operations[slot] = (struct operation){
		    .mr = mr, .cookie = cookie, .opcode = opcode, .length = length, .active = true};
		ep->outstanding++;
		mr->references++;
	}
	return rc;
}

static int
verbs_close_begin(struct crt_native_ep *ep)
{
	struct ibv_qp_attr attr = {.qp_state = IBV_QPS_ERR};

	if (ep == NULL)
		return EINVAL;
	if (ep->qp == NULL)
		return ep->closing ? 0 : EINVAL;
	ep->closing = true;
	return ibv_modify_qp(ep->qp, &attr, IBV_QP_STATE);
}

static int
verbs_quiesce(struct crt_native_ep *ep)
{
	int rc;
	if (ep == NULL || !ep->closing)
		return EINVAL;
	if (ep->outstanding != 0)
		return EBUSY;
	if (ep->qp == NULL)
		return 0;
	/* There is exactly one QP in this endpoint's private PD. Destroying it
	 * removes every remote path to that PD before exported MRs are released.
	 */
	rc = ibv_destroy_qp(ep->qp);
	if (rc == 0) {
		ep->qp        = NULL;
		ep->connected = false;
	}
	return rc;
}

static int
verbs_progress(struct crt_native_ep *ep, struct crt_native_completion *output, unsigned capacity,
	       unsigned *count)
{
	struct ibv_wc     batch[32];
	struct operation *op;
	int               rc, error;

	if (count == NULL)
		return EINVAL;
	*count = 0;
	if (ep == NULL || output == NULL || capacity == 0)
		return EINVAL;
	while (*count < capacity) {
		unsigned budget = capacity - *count;

		if (budget > sizeof(batch) / sizeof(batch[0]))
			budget = sizeof(batch) / sizeof(batch[0]);
		rc = ibv_poll_cq(ep->cq, (int)budget, batch);
		if (rc < 0) {
			verbs_close_begin(ep);
			return EIO;
		}
		if (rc == 0)
			break;
		error = 0;
		/* A poll removes the whole batch from the CQ. Even if one entry is
		 * corrupt, account for every other entry before returning an error.
		 */
		for (int i = 0; i < rc; i++) {
			struct ibv_wc wc = batch[i];

			if (wc.wr_id >= ep->config.depth || !ep->operations[wc.wr_id].active) {
				error = EPROTO;
				continue;
			}
			op             = &ep->operations[wc.wr_id];
			output[*count] = (struct crt_native_completion){
			    .cookie = op->cookie,
			    .opcode = op->opcode,
			    .error  = wc.status == IBV_WC_SUCCESS        ? 0
				      : wc.status == IBV_WC_WR_FLUSH_ERR ? ECANCELED
									 : EIO,
			    .bytes  = wc.status != IBV_WC_SUCCESS     ? 0
				      : op->opcode == CRT_NATIVE_RECV ? wc.byte_len
								      : op->length,
			};
			op->mr->references--;
			op->active    = false;
			op->next_free = ep->free_head;
			ep->free_head = (unsigned)wc.wr_id;
			ep->outstanding--;
			(*count)++;
		}
		if (error != 0) {
			verbs_close_begin(ep);
			return error;
		}
	}
	return 0;
}

static unsigned
verbs_capacity(struct crt_native_ep *ep)
{
	return ep == NULL ? 0 : ep->config.depth;
}

const struct crt_native_ops crt_native_verbs_ops = {
    .name              = "verbs",
    .capacity          = verbs_capacity,
    .open              = verbs_open,
    .address           = verbs_address,
    .connect           = verbs_connect,
    .register_memory   = verbs_register,
    .deregister_memory = verbs_deregister,
    .post              = verbs_post,
    .progress          = verbs_progress,
    .close_begin       = verbs_close_begin,
    .quiesce           = verbs_quiesce,
    .close             = verbs_close,
};
