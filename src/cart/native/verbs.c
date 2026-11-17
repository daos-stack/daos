/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "transport.h"

#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <infiniband/verbs.h>
#include <stdio.h>
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
	bool                   ready;
	unsigned               chain_head, chain_next;
	int                    completion_error;
	size_t                 completion_bytes;
};

struct crt_native_group {
	struct ibv_context    *context;
	struct ibv_cq         *cq;
	struct crt_native_ep **endpoints;
	unsigned               capacity, count, next_slot, depth;
	char                  *device;
	unsigned               poll_batches;
	uint64_t               polls, completions;
	bool                   stats;
	int                    error;
};

struct post_scratch {
	struct ibv_send_wr sends[CRT_NATIVE_BATCH];
	struct ibv_recv_wr receives[CRT_NATIVE_BATCH];
	struct ibv_sge     sges[CRT_NATIVE_BATCH];
	unsigned           slots[CRT_NATIVE_BATCH];
};

struct crt_native_ep {
	struct ibv_context       *context;
	struct ibv_pd            *pd;
	struct ibv_cq            *cq;
	struct ibv_qp            *qp;
	struct crt_native_config  config;
	struct crt_native_address local;
	struct operation         *operations;
	struct post_scratch      *batch;
	unsigned                  outstanding;
	unsigned                  read_limit, reads, inline_limit;
	struct crt_native_stats   stats;
	struct ibv_wc            *pending;
	unsigned                  pending_head, pending_count, group_slot;
	unsigned                 *ready;
	unsigned                  ready_head, ready_count;
	unsigned                  free_head;
	unsigned                  registrations;
	bool                      connected;
	bool                      closing;
};

static void
report_error(const struct crt_native_config *cfg, const char *operation, int error)
{
	fprintf(stderr,
		"doca+rc: %s failed: device=%s port=%u gid_index=%d depth=%u "
		"errno=%d (%s)\n",
		operation, cfg->device, cfg->port, cfg->gid_index, cfg->depth, error,
		strerror(error));
}

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
	if (ep->cq != NULL && ep->config.group == NULL) {
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
	if (ep->context != NULL && ep->config.group == NULL) {
		rc = ibv_close_device(ep->context);
		if (rc != 0)
			return rc;
	}
	if (ep->config.group && ep->config.group->endpoints[ep->group_slot] == ep) {
		ep->config.group->endpoints[ep->group_slot] = NULL;
		ep->config.group->count--;
	}
	if (ep->config.stats)
		fprintf(stderr,
			"doca+rc stats: send=%" PRIu64 " recv=%" PRIu64 " read=%" PRIu64
			" write=%" PRIu64 " inline=%" PRIu64 " signaled=%" PRIu64 " cqes=%" PRIu64
			" logical=%" PRIu64 " send_batches=%" PRIu64 " recv_batches=%" PRIu64
			" read_limit=%u inline_requested=%u inline_limit=%u"
			" single_posts=%" PRIu64 " batch_posts=%" PRIu64
			" direct_completions=%" PRIu64 " queued_completions=%" PRIu64
			" send_bytes=%" PRIu64 " send_min=%" PRIu64 " send_max=%" PRIu64
			" send_le64=%" PRIu64 " send_le128=%" PRIu64 " send_le256=%" PRIu64
			" send_le512=%" PRIu64 " send_le1024=%" PRIu64 " send_le4096=%" PRIu64
			" send_gt4096=%" PRIu64 "\n",
			ep->stats.send_posts, ep->stats.recv_posts, ep->stats.read_posts,
			ep->stats.write_posts, ep->stats.inline_sends, ep->stats.signaled_sends,
			ep->stats.cqes, ep->stats.logical_completions, ep->stats.send_batches,
			ep->stats.recv_batches, ep->read_limit, ep->config.inline_size,
			ep->inline_limit, ep->stats.single_posts, ep->stats.batch_posts,
			ep->stats.direct_completions, ep->stats.queued_completions,
			ep->stats.send_bytes, ep->stats.send_min, ep->stats.send_max,
			ep->stats.send_le64, ep->stats.send_le128, ep->stats.send_le256,
			ep->stats.send_le512, ep->stats.send_le1024, ep->stats.send_le4096,
			ep->stats.send_gt4096);
	free(ep->pending);
	free(ep->ready);
	free(ep->batch);
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
	const char             *stage = "allocate endpoint";

	if (output == NULL)
		return EINVAL;
	*output = NULL;
	if (cfg == NULL || cfg->device == NULL || cfg->port == 0 || cfg->depth == 0 ||
	    cfg->depth > 32768 || cfg->gid_index < -1 || cfg->gid_index > 255)
		return EINVAL;
	ep = calloc(1, sizeof(*ep));
	if (ep == NULL) {
		report_error(cfg, stage, ENOMEM);
		return ENOMEM;
	}
	ep->config     = *cfg;
	stage          = "allocate operation records";
	ep->pending    = calloc(cfg->depth, sizeof(*ep->pending));
	ep->ready      = calloc(cfg->depth, sizeof(*ep->ready));
	ep->batch      = calloc(1, sizeof(*ep->batch));
	ep->operations = calloc(cfg->depth, sizeof(*ep->operations));
	if (ep->operations == NULL || ep->batch == NULL || ep->ready == NULL ||
	    ep->pending == NULL) {
		rc = ENOMEM;
		goto fail;
	}
	for (unsigned i = 0; i < cfg->depth; i++)
		ep->operations[i].next_free = i + 1;
	if (cfg->group) {
		struct crt_native_group *g = cfg->group;
		stage                      = "join shared CQ group";
		if (cfg->depth != g->depth || strcmp(cfg->device, g->device) != 0) {
			rc = EINVAL;
			goto fail;
		}
		if (g->error || g->count == g->capacity) {
			rc = g->error ? g->error : ENOSPC;
			goto fail;
		}
		while (g->endpoints[g->next_slot])
			g->next_slot = (g->next_slot + 1) % g->capacity;
		ep->group_slot               = g->next_slot;
		g->endpoints[ep->group_slot] = ep;
		g->count++;
		ep->context = g->context;
		ep->cq      = g->cq;
	} else {
		stage   = "ibv_get_device_list";
		errno   = 0;
		devices = ibv_get_device_list(&count);
		if (devices == NULL) {
			rc = errno != 0 ? errno : ENODEV;
			goto fail;
		}
		stage = "find device";
		for (int i = 0; i < count; i++) {
			if (strcmp(cfg->device, ibv_get_device_name(devices[i])) == 0) {
				stage       = "ibv_open_device";
				errno       = 0;
				ep->context = ibv_open_device(devices[i]);
				if (ep->context == NULL)
					rc = errno != 0 ? errno : EIO;
				break;
			}
		}
		ibv_free_device_list(devices);
		if (ep->context == NULL)
			goto fail;
	}
	struct ibv_device_attr caps;
	stage = "ibv_query_device";
	rc    = ibv_query_device(ep->context, &caps);
	if (rc)
		goto fail;
	unsigned requested = cfg->rdma_read_depth ? cfg->rdma_read_depth : 16;
	if (requested > 255)
		requested = 255;
	unsigned initiator = caps.max_qp_init_rd_atom < 255 ? caps.max_qp_init_rd_atom : 255;
	unsigned responder = caps.max_qp_rd_atom < 255 ? caps.max_qp_rd_atom : 255;
	if (!initiator || !responder) {
		rc = ENOTSUP;
		goto fail;
	}
	ep->local.read_out = requested < initiator ? requested : initiator;
	ep->local.read_in  = requested < responder ? requested : responder;
	stage              = "ibv_query_port";
	rc                 = ibv_query_port(ep->context, cfg->port, &port);
	if (rc != 0)
		goto fail;
	if (port.state != IBV_PORT_ACTIVE) {
		stage = "require active port";
		rc    = ENETDOWN;
		goto fail;
	}
	ep->local.lid    = port.lid;
	ep->local.mtu    = port.active_mtu;
	ep->local.global = cfg->gid_index >= 0;
	if (port.link_layer == IBV_LINK_LAYER_ETHERNET && !ep->local.global) {
		stage = "require explicit RoCE GID index";
		rc    = EINVAL;
		goto fail;
	}
	if (ep->local.global) {
		union ibv_gid gid;

		stage = "ibv_query_gid";
		rc    = ibv_query_gid(ep->context, cfg->port, cfg->gid_index, &gid);
		if (rc != 0)
			goto fail;
		memcpy(ep->local.gid, gid.raw, sizeof(ep->local.gid));
	}
	stage  = "ibv_alloc_pd";
	errno  = 0;
	ep->pd = ibv_alloc_pd(ep->context);
	if (ep->pd == NULL) {
		rc = errno != 0 ? errno : ENOMEM;
		goto fail;
	}
	if (!cfg->group) {
		stage  = "ibv_create_cq";
		errno  = 0;
		ep->cq = ibv_create_cq(ep->context, (int)cfg->depth, NULL, NULL, 0);
		if (ep->cq == NULL) {
			rc = errno != 0 ? errno : ENOMEM;
			goto fail;
		}
	}
	init.send_cq             = ep->cq;
	init.recv_cq             = ep->cq;
	init.qp_type             = IBV_QPT_RC;
	init.cap.max_send_wr     = cfg->depth;
	init.cap.max_recv_wr     = cfg->depth;
	init.cap.max_send_sge    = 1;
	init.cap.max_recv_sge    = 1;
	init.cap.max_inline_data = cfg->inline_size;
	stage                    = "ibv_create_qp";
	errno                    = 0;
	ep->qp                   = ibv_create_qp(ep->pd, &init);
	while (!ep->qp && init.cap.max_inline_data && (errno == EINVAL || errno == ENOTSUP)) {
		init.cap.max_inline_data /= 2;
		ep->qp = ibv_create_qp(ep->pd, &init);
	}
	ep->inline_limit = init.cap.max_inline_data;
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
	stage                = "ibv_modify_qp(INIT)";
	rc                   = ibv_modify_qp(ep->qp, &attr,
					     IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS);
	if (rc != 0)
		goto fail;
	*output = ep;
	return 0;
fail:
	if (rc < 0)
		rc = errno ? errno : EIO;
	report_error(cfg, stage, rc);
	/* No work has been posted, so cleanup cannot abandon accepted operations. */
	if (verbs_close(ep) != 0)
		*output = ep;
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
	attr.max_dest_rd_atomic = ep->local.read_in;
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
	attr.qp_state         = IBV_QPS_RTS;
	attr.sq_psn           = ep->local.psn;
	attr.timeout          = 14;
	attr.retry_cnt        = 7;
	attr.rnr_retry        = 7;
	unsigned peer_credits = remote->read_in ? remote->read_in : 1;
	ep->read_limit     = ep->local.read_out < peer_credits ? ep->local.read_out : peer_credits;
	attr.max_rd_atomic = ep->read_limit;
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
	errno  = 0;
	mr->mr = ibv_reg_mr(ep->pd, buffer, length, access);
	if (mr->mr == NULL) {
		int rc = errno != 0 ? errno : ENOMEM;

		report_error(&ep->config, "ibv_reg_mr", rc);
		fprintf(stderr, "doca+rc: registration bytes=%zu access=%u\n", length,
			remote_access);
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
verbs_close_begin(struct crt_native_ep *);

static int
validate_post(struct crt_native_ep *ep, const struct crt_native_post *p)
{
	struct crt_native_mr *mr = p->mr;
	if (!mr || mr->owner != ep || p->opcode < CRT_NATIVE_SEND || p->opcode > CRT_NATIVE_WRITE ||
	    !p->length || p->length > UINT32_MAX || p->offset > mr->mr->length ||
	    p->length > mr->mr->length - p->offset)
		return EINVAL;
	if (!ep->connected && p->opcode != CRT_NATIVE_RECV)
		return ENOTCONN;
	if (p->opcode == CRT_NATIVE_READ || p->opcode == CRT_NATIVE_WRITE) {
		const struct crt_native_remote_memory *r = p->remote;
		if (!r || p->remote_offset > r->length ||
		    p->length > r->length - p->remote_offset ||
		    p->remote_offset > UINT64_MAX - r->address ||
		    p->length > UINT64_MAX - (r->address + p->remote_offset))
			return EINVAL;
	}
	return 0;
}

static void
record_send_size(struct crt_native_ep *ep, size_t size)
{
	if (!ep->config.stats)
		return;
	struct crt_native_stats *s = &ep->stats;
	s->send_bytes += size;
	if (!s->send_min || size < s->send_min)
		s->send_min = size;
	if (size > s->send_max)
		s->send_max = size;
	if (size <= 64)
		s->send_le64++;
	else if (size <= 128)
		s->send_le128++;
	else if (size <= 256)
		s->send_le256++;
	else if (size <= 512)
		s->send_le512++;
	else if (size <= 1024)
		s->send_le1024++;
	else if (size <= 4096)
		s->send_le4096++;
	else
		s->send_gt4096++;
}

/* The dominant one-WR case does not touch linked-list posting scratch or the
 * selective-signaling ledger. It still owns every accepted buffer until CQE.
 */
static int
verbs_post_one(struct crt_native_ep *ep, const struct crt_native_post *p)
{
	if (!ep || !p)
		return EINVAL;
	if (ep->closing)
		return ESHUTDOWN;
	int rc = validate_post(ep, p);
	if (rc)
		return rc;
	unsigned slot = ep->free_head;
	if (slot == ep->config.depth ||
	    (p->opcode == CRT_NATIVE_READ && ep->reads == ep->read_limit)) {
		if (ep->config.wake)
			ep->config.wake(ep->config.wake_arg);
		return EAGAIN;
	}
	struct ibv_sge sge   = {.addr   = (uintptr_t)p->mr->mr->addr + p->offset,
				.length = p->length,
				.lkey   = p->mr->mr->lkey};
	uint64_t       id    = (uint64_t)ep->group_slot << 32 | slot;
	unsigned       flags = IBV_SEND_SIGNALED;
	if (p->opcode == CRT_NATIVE_RECV) {
		struct ibv_recv_wr wr = {.wr_id = id, .sg_list = &sge, .num_sge = 1}, *bad = &wr;
		ep->stats.recv_batches++;
		rc = ibv_post_recv(ep->qp, &wr, &bad);
	} else {
		struct ibv_send_wr wr   = {.wr_id      = id,
					   .sg_list    = &sge,
					   .num_sge    = 1,
					   .send_flags = IBV_SEND_SIGNALED,
					   .opcode     = p->opcode == CRT_NATIVE_SEND ? IBV_WR_SEND
							 : p->opcode == CRT_NATIVE_READ
							     ? IBV_WR_RDMA_READ
							     : IBV_WR_RDMA_WRITE},
				   *bad = &wr;
		if (p->opcode == CRT_NATIVE_SEND && p->length <= ep->inline_limit)
			wr.send_flags |= IBV_SEND_INLINE;
		if (p->opcode != CRT_NATIVE_SEND) {
			wr.wr.rdma.remote_addr = p->remote->address + p->remote_offset;
			wr.wr.rdma.rkey        = p->remote->key;
		}
		flags = wr.send_flags;
		ep->stats.send_batches++;
		rc = ibv_post_send(ep->qp, &wr, &bad);
	}
	if (rc) {
		if (rc == EAGAIN && ep->config.wake)
			ep->config.wake(ep->config.wake_arg);
		return rc;
	}
	ep->free_head        = ep->operations[slot].next_free;
	ep->operations[slot] = (struct operation){.mr         = p->mr,
						  .cookie     = p->cookie,
						  .opcode     = p->opcode,
						  .length     = p->length,
						  .active     = true,
						  .chain_head = ep->config.depth,
						  .chain_next = ep->config.depth};
	p->mr->references++;
	ep->outstanding++;
	ep->stats.single_posts++;
	if (p->opcode == CRT_NATIVE_SEND) {
		ep->stats.send_posts++;
		ep->stats.signaled_sends++;
		if (flags & IBV_SEND_INLINE)
			ep->stats.inline_sends++;
		record_send_size(ep, p->length);
	} else if (p->opcode == CRT_NATIVE_RECV)
		ep->stats.recv_posts++;
	else if (p->opcode == CRT_NATIVE_READ) {
		ep->stats.read_posts++;
		ep->reads++;
	} else
		ep->stats.write_posts++;
	return 0;
}

static int
verbs_post_batch(struct crt_native_ep *ep, const struct crt_native_post *posts, unsigned count,
		 unsigned *accepted)
{
	if (!accepted)
		return EINVAL;
	*accepted = 0;
	if (!ep || !posts || !count || count > CRT_NATIVE_BATCH)
		return EINVAL;
	if (ep->closing)
		return ESHUTDOWN;
	if (count == 1) {
		int rc    = verbs_post_one(ep, posts);
		*accepted = rc ? 0 : 1;
		return rc;
	}
	for (unsigned i = 0; i < count; i++) {
		int rc = validate_post(ep, &posts[i]);
		if (rc)
			return rc;
		if (posts[i].opcode != posts[0].opcode)
			return EINVAL;
	}
	unsigned available = ep->config.depth - ep->outstanding;
	if (posts[0].opcode == CRT_NATIVE_READ && ep->read_limit - ep->reads < available)
		available = ep->read_limit - ep->reads;
	if (available == 1) {
		int rc    = verbs_post_one(ep, posts);
		*accepted = rc ? 0 : 1;
		return rc ? rc : EAGAIN;
	}
	struct post_scratch *b    = ep->batch;
	unsigned             slot = ep->free_head, prepared = 0;
	for (; prepared < count && slot < ep->config.depth; prepared++) {
		const struct crt_native_post *p = &posts[prepared];
		if (p->opcode == CRT_NATIVE_READ && ep->reads + prepared >= ep->read_limit)
			break;
		b->slots[prepared] = slot;
		slot               = ep->operations[slot].next_free;
		b->sges[prepared] = (struct ibv_sge){.addr = (uintptr_t)p->mr->mr->addr + p->offset,
						     .length = p->length,
						     .lkey   = p->mr->mr->lkey};
		uint64_t id       = (uint64_t)ep->group_slot << 32 | b->slots[prepared];
		if (p->opcode == CRT_NATIVE_RECV)
			b->receives[prepared] = (struct ibv_recv_wr){
			    .wr_id = id, .sg_list = &b->sges[prepared], .num_sge = 1};
		else {
			struct ibv_send_wr *wr = &b->sends[prepared];
			*wr                    = (struct ibv_send_wr){
					       .wr_id      = id,
					       .sg_list    = &b->sges[prepared],
					       .num_sge    = 1,
					       .send_flags = IBV_SEND_SIGNALED,
					       .opcode     = p->opcode == CRT_NATIVE_SEND ? IBV_WR_SEND
							     : p->opcode == CRT_NATIVE_READ ? IBV_WR_RDMA_READ
											    : IBV_WR_RDMA_WRITE};
			if (p->opcode == CRT_NATIVE_SEND && p->length <= ep->inline_limit)
				wr->send_flags |= IBV_SEND_INLINE;
			if (p->opcode != CRT_NATIVE_SEND) {
				wr->wr.rdma.remote_addr = p->remote->address + p->remote_offset;
				wr->wr.rdma.rkey        = p->remote->key;
			}
		}
	}
	if (!prepared) {
		if (ep->config.wake)
			ep->config.wake(ep->config.wake_arg);
		return EAGAIN;
	}
	unsigned interval = ep->config.signal_interval ? ep->config.signal_interval : 8;
	if (posts[0].opcode == CRT_NATIVE_SEND)
		for (unsigned i = 0; i < prepared; i++)
			if ((i + 1) % interval && i + 1 < prepared)
				b->sends[i].send_flags &= ~IBV_SEND_SIGNALED;
	int      rc;
	unsigned posted = prepared;
	if (posts[0].opcode == CRT_NATIVE_RECV) {
		for (unsigned i = 0; i + 1 < prepared; i++)
			b->receives[i].next = &b->receives[i + 1];
		struct ibv_recv_wr *bad = &b->receives[0];
		ep->stats.recv_batches++;
		rc = ibv_post_recv(ep->qp, b->receives, &bad);
		if (rc)
			for (posted = 0; posted < prepared && bad != &b->receives[posted]; posted++)
				;
	} else {
		for (unsigned i = 0; i + 1 < prepared; i++)
			b->sends[i].next = &b->sends[i + 1];
		struct ibv_send_wr *bad = &b->sends[0];
		ep->stats.send_batches++;
		rc = ibv_post_send(ep->qp, b->sends, &bad);
		if (rc)
			for (posted = 0; posted < prepared && bad != &b->sends[posted]; posted++)
				;
	}
	for (unsigned i = 0; i < posted; i++) {
		const struct crt_native_post *p     = &posts[i];
		unsigned                      index = b->slots[i];
		ep->free_head                       = ep->operations[index].next_free;
		ep->operations[index]               = (struct operation){.mr         = p->mr,
									 .cookie     = p->cookie,
									 .opcode     = p->opcode,
									 .length     = p->length,
									 .active     = true,
									 .chain_head = ep->config.depth,
									 .chain_next = ep->config.depth};
		p->mr->references++;
		ep->outstanding++;
		ep->stats.batch_posts++;
		if (p->opcode == CRT_NATIVE_SEND) {
			ep->stats.send_posts++;
			record_send_size(ep, p->length);
			if (b->sends[i].send_flags & IBV_SEND_SIGNALED)
				ep->stats.signaled_sends++;
			if (b->sends[i].send_flags & IBV_SEND_INLINE)
				ep->stats.inline_sends++;
		} else if (p->opcode == CRT_NATIVE_RECV)
			ep->stats.recv_posts++;
		else if (p->opcode == CRT_NATIVE_READ) {
			ep->stats.read_posts++;
			ep->reads++;
		} else
			ep->stats.write_posts++;
	}
	if (posts[0].opcode == CRT_NATIVE_SEND) {
		unsigned first = 0;
		for (unsigned i = 0; i < posted; i++)
			if (b->sends[i].send_flags & IBV_SEND_SIGNALED) {
				ep->operations[b->slots[i]].chain_head = b->slots[first];
				for (unsigned j = first; j < i; j++)
					ep->operations[b->slots[j]].chain_next = b->slots[j + 1];
				first = i + 1;
			}
		if (posted && !(b->sends[posted - 1].send_flags & IBV_SEND_SIGNALED)) {
			/* A rejected marker cannot strand accepted unsignaled buffers.
			 * Error the QP and consume its flush completions before release.
			 */
			verbs_close_begin(ep);
			rc = EIO;
		}
	}
	*accepted = posted;
	if ((rc || posted < count) && ep->config.wake)
		ep->config.wake(ep->config.wake_arg);
	return rc ? rc : posted < count ? EAGAIN : 0;
}

static int
verbs_post(struct crt_native_ep *ep, enum crt_native_opcode opcode, struct crt_native_mr *mr,
	   size_t offset, size_t length, const struct crt_native_remote_memory *remote,
	   size_t remote_offset, void *cookie)
{
	struct crt_native_post p = {.opcode        = opcode,
				    .mr            = mr,
				    .offset        = offset,
				    .length        = length,
				    .remote        = remote,
				    .remote_offset = remote_offset,
				    .cookie        = cookie};
	return verbs_post_one(ep, &p);
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
queue_completion(struct crt_native_ep *ep, struct ibv_wc wc)
{
	if (wc.wr_id >= ep->config.depth)
		return EPROTO;
	struct operation *marker = &ep->operations[wc.wr_id];
	if (!marker->active || marker->ready)
		return ep->closing && wc.status == IBV_WC_WR_FLUSH_ERR ? 0 : EPROTO;
	if (wc.status != IBV_WC_SUCCESS)
		verbs_close_begin(ep);
	unsigned slot =
	    marker->chain_head < ep->config.depth ? marker->chain_head : (unsigned)wc.wr_id;
	for (unsigned budget = 0; slot < ep->config.depth && budget < ep->config.depth; budget++) {
		struct operation *op   = &ep->operations[slot];
		unsigned          next = op->chain_next;
		if (op->active && !op->ready) {
			if (ep->ready_count == ep->config.depth)
				return EPROTO;
			op->ready            = true;
			op->completion_error = wc.status == IBV_WC_SUCCESS        ? 0
					       : wc.status == IBV_WC_WR_FLUSH_ERR ? ECANCELED
										  : EIO;
			op->completion_bytes = wc.status != IBV_WC_SUCCESS     ? 0
					       : op->opcode == CRT_NATIVE_RECV ? wc.byte_len
									       : op->length;
			ep->ready[(ep->ready_head + ep->ready_count++) % ep->config.depth] = slot;
		}
		if (slot == wc.wr_id)
			break;
		slot = next;
	}
	return 0;
}

static void
retire_completion(struct crt_native_ep *ep, unsigned slot, int error, size_t bytes,
		  struct crt_native_completion *out)
{
	struct operation *op = &ep->operations[slot];
	*out                 = (struct crt_native_completion){
			    .cookie = op->cookie, .opcode = op->opcode, .error = error, .bytes = bytes};
	if (op->opcode == CRT_NATIVE_READ)
		ep->reads--;
	op->mr->references--;
	op->active    = false;
	op->next_free = ep->free_head;
	ep->free_head = slot;
	ep->outstanding--;
	ep->stats.logical_completions++;
}

static int
verbs_progress(struct crt_native_ep *ep, struct crt_native_completion *output, unsigned capacity,
	       unsigned *count)
{
	if (!count)
		return EINVAL;
	*count = 0;
	if (!ep || !output || !capacity)
		return EINVAL;
	int error = 0;
	while (*count < capacity) {
		if (ep->ready_count) {
			unsigned slot  = ep->ready[ep->ready_head];
			ep->ready_head = (ep->ready_head + 1) % ep->config.depth;
			ep->ready_count--;
			struct operation *op = &ep->operations[slot];
			retire_completion(ep, slot, op->completion_error, op->completion_bytes,
					  &output[(*count)++]);
			ep->stats.queued_completions++;
			continue;
		}
		if (!ep->pending_count) {
			if (ep->config.group)
				break;
			unsigned budget = ep->config.depth < 32 ? ep->config.depth : 32;
			ep->stats.cq_polls++;
			int received = ibv_poll_cq(ep->cq, (int)budget, ep->pending);
			if (received < 0) {
				verbs_close_begin(ep);
				error = EIO;
				break;
			}
			if (!received)
				break;
			ep->stats.cqes += received;
			ep->pending_head  = 0;
			ep->pending_count = received;
		}
		struct ibv_wc wc = ep->pending[ep->pending_head];
		ep->pending_head = (ep->pending_head + 1) % ep->config.depth;
		ep->pending_count--;
		if (wc.wr_id < ep->config.depth) {
			struct operation *op = &ep->operations[wc.wr_id];
			if (wc.status == IBV_WC_SUCCESS && op->active && !op->ready &&
			    (op->chain_head == ep->config.depth || op->chain_head == wc.wr_id)) {
				size_t bytes =
				    op->opcode == CRT_NATIVE_RECV ? wc.byte_len : op->length;
				retire_completion(ep, (unsigned)wc.wr_id, 0, bytes,
						  &output[(*count)++]);
				ep->stats.direct_completions++;
				continue;
			}
		}
		int rc = queue_completion(ep, wc);
		if (rc) {
			error = rc;
			verbs_close_begin(ep);
		}
	}
	if ((ep->pending_count || ep->ready_count) && ep->config.wake)
		ep->config.wake(ep->config.wake_arg);
	return error ? error : ep->config.group ? ep->config.group->error : 0;
}

static unsigned
verbs_capacity(struct crt_native_ep *ep)
{
	return ep == NULL ? 0 : ep->config.depth;
}

static void
verbs_wake(struct crt_native_ep *ep)
{
	if (ep->config.wake)
		ep->config.wake(ep->config.wake_arg);
}

static int
verbs_group_destroy(struct crt_native_group *g)
{
	int rc;
	if (!g)
		return EINVAL;
	if (g->count)
		return EBUSY;
	if (g->cq) {
		rc = ibv_destroy_cq(g->cq);
		if (rc)
			return rc;
		g->cq = NULL;
	}
	if (g->context) {
		rc = ibv_close_device(g->context);
		if (rc)
			return rc;
	}
	if (g->stats)
		fprintf(stderr, "doca+rc CQ stats: polls=%" PRIu64 " cqes=%" PRIu64 "\n", g->polls,
			g->completions);
	free(g->endpoints);
	free(g->device);
	free(g);
	return 0;
}

static int
verbs_group_create(const struct crt_native_config *cfg, unsigned capacity,
		   struct crt_native_group **out)
{
	if (!cfg || !cfg->device || !out || !capacity || !cfg->depth ||
	    capacity > INT_MAX / cfg->depth)
		return EINVAL;
	*out                       = NULL;
	struct crt_native_group *g = calloc(1, sizeof(*g));
	if (!g)
		return ENOMEM;
	g->poll_batches = cfg->cq_poll_batches ? cfg->cq_poll_batches : 4;
	if (g->poll_batches > 16)
		g->poll_batches = 16;
	g->stats     = cfg->stats;
	g->capacity  = capacity;
	g->depth     = cfg->depth;
	g->device    = malloc(strlen(cfg->device) + 1);
	g->endpoints = calloc(capacity, sizeof(*g->endpoints));
	int rc       = ENOMEM;
	if (!g->endpoints || !g->device)
		goto fail;
	strcpy(g->device, cfg->device);
	int                 count;
	struct ibv_device **devices = ibv_get_device_list(&count);
	if (!devices) {
		rc = errno ? errno : ENODEV;
		goto fail;
	}
	rc = ENODEV;
	for (int i = 0; i < count; i++)
		if (strcmp(cfg->device, ibv_get_device_name(devices[i])) == 0) {
			g->context = ibv_open_device(devices[i]);
			rc         = g->context ? 0 : (errno ? errno : EIO);
			break;
		}
	ibv_free_device_list(devices);
	if (rc)
		goto fail;
	g->cq = ibv_create_cq(g->context, (int)(capacity * cfg->depth), NULL, NULL, 0);
	if (!g->cq) {
		rc = errno ? errno : ENOMEM;
		goto fail;
	}
	*out = g;
	return 0;
fail:
	report_error(cfg, "create shared CQ group", rc);
	if (verbs_group_destroy(g))
		*out = g;
	return rc;
}

static int
verbs_group_progress(struct crt_native_group *g)
{
	struct ibv_wc batch[32];
	if (!g)
		return EINVAL;
	/* Keep draining flush CQEs after an error; registrations stay pinned. */
	/* Drain bounded CQ bursts without starving CaRT handler dispatch. */
	for (unsigned round = 0; round < g->poll_batches; round++) {
		g->polls++;
		int count = ibv_poll_cq(g->cq, 32, batch);
		if (count < 0)
			return g->error = EIO;
		g->completions += count;
		for (int i = 0; i < count; i++) {
			unsigned              slot  = batch[i].wr_id >> 32;
			unsigned              index = (uint32_t)batch[i].wr_id;
			struct crt_native_ep *ep = slot < g->capacity ? g->endpoints[slot] : NULL;
			if (ep && index < ep->config.depth && !ep->operations[index].active &&
			    ep->closing && batch[i].status == IBV_WC_WR_FLUSH_ERR)
				continue;
			if (!ep || index >= ep->config.depth || !ep->operations[index].active ||
			    ep->pending_count == ep->config.depth) {
				g->error = EPROTO;
				continue;
			}
			ep->stats.cqes++;
			bool empty     = ep->pending_count == 0;
			batch[i].wr_id = index;
			ep->pending[(ep->pending_head + ep->pending_count++) % ep->config.depth] =
			    batch[i];
			if (empty)
				verbs_wake(ep);
		}
		if (count < 32)
			break;
	}
	return g->error;
}

static void
verbs_get_stats(struct crt_native_ep *ep, struct crt_native_stats *out)
{
	if (ep && out) {
		*out                  = ep->stats;
		out->inline_requested = ep->config.inline_size;
		out->inline_limit     = ep->inline_limit;
	}
}

const struct crt_native_ops crt_native_verbs_ops = {
    .name              = "verbs",
    .group_create      = verbs_group_create,
    .group_progress    = verbs_group_progress,
    .group_destroy     = verbs_group_destroy,
    .wake              = verbs_wake,
    .get_stats         = verbs_get_stats,
    .capacity          = verbs_capacity,
    .open              = verbs_open,
    .address           = verbs_address,
    .connect           = verbs_connect,
    .register_memory   = verbs_register,
    .deregister_memory = verbs_deregister,
    .post              = verbs_post,
    .post_batch        = verbs_post_batch,
    .progress          = verbs_progress,
    .close_begin       = verbs_close_begin,
    .quiesce           = verbs_quiesce,
    .close             = verbs_close,
};
