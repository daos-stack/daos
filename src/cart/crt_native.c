/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(hg)
#include "crt_internal.h"
#include "crt_native.h"
#include "native/cart_bulk.h"

#ifdef CRT_HAVE_NATIVE_RC
#include "native/device.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <infiniband/verbs.h>
#include <limits.h>

struct incoming {
	struct crt_native_event    event;
	struct crt_native_peer    *peer;
	struct crt_native_rpc     *engine;
	struct crt_native_token    token;
	struct crt_native_rx_lease rx;
	const void                *payload;
	size_t                     size;
	uint32_t                   opcode;
	unsigned                   index, next;
};
struct bridge_peer {
	struct bridge_peer           *next, *dispatch_next;
	bool                          dispatch_queued;
	struct crt_native_peer       *peer;
	struct crt_native_cart_bulk  *bulk;
	struct incoming              *incoming;
	unsigned                      capacity, free_head;
	unsigned                      requests, incoming_active;
	struct crt_native_rpc_state  *waiting;
	struct crt_native_rpc_state **waiting_tail;
};
struct crt_native_context {
	struct crt_context          *cart;
	pthread_mutex_t              mutex, progress_mutex;
	struct crt_native_net       *net;
	struct crt_native_rpc_state *requests;
	struct bridge_peer          *peers, *bulk_dispatch;
	struct crt_native_event     *events, *events_tail;
	char                        *device;
	struct crt_native_ep        *probe;
	unsigned                     references;
	atomic_uint                  bulks;
	bool                         stopping;
};

static int
from_errno(int rc)
{
	switch (rc) {
	case 0:
		return 0;
	case EAGAIN:
	case EBUSY:
	case ENOSPC:
		return -DER_AGAIN;
	case ENOMEM:
		return -DER_NOMEM;
	case EPROTO:
		return -DER_PROTO;
	case EINVAL:
		return -DER_INVAL;
	case ETIMEDOUT:
		return -DER_TIMEDOUT;
	case ECANCELED:
	case ESHUTDOWN:
		return -DER_CANCELED;
	case EMSGSIZE:
		return -DER_TRUNC;
	default:
		return -DER_UNREACH;
	}
}
static void
lock(struct crt_native_context *c)
{
	pthread_mutex_lock(&c->mutex);
}
static void
unlock(struct crt_native_context *c)
{
	pthread_mutex_unlock(&c->mutex);
}
static void
queue_event(struct crt_native_context *c, struct crt_native_event *event)
{
	event->next = NULL;
	if (c->events_tail)
		c->events_tail->next = event;
	else
		c->events = event;
	c->events_tail = event;
}
static struct bridge_peer *
peer_data(struct crt_native_context *c, struct crt_native_peer *peer)
{
	struct bridge_peer *existing = crt_native_peer_data(peer);
	if (existing)
		return existing;
	struct bridge_peer *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->peer         = peer;
	p->waiting_tail = &p->waiting;
	p->next         = c->peers;
	c->peers        = p;
	crt_native_peer_set_data(peer, p);
	return p;
}

static void
remove_pending(struct crt_native_rpc_state *s)
{
	if (!s->pending_previous)
		return;
	struct bridge_peer *p = crt_native_peer_data(s->peer);
	*s->pending_previous  = s->pending_next;
	if (s->pending_next)
		s->pending_next->pending_previous = s->pending_previous;
	else
		p->waiting_tail = s->pending_previous;
	s->pending_previous = NULL;
}

static void
add_request(struct crt_native_context *c, struct crt_native_rpc_state *s)
{
	struct bridge_peer *p = crt_native_peer_data(s->peer);
	D_ASSERT(p != NULL);
	p->requests++;
	s->next     = c->requests;
	s->previous = &c->requests;
	if (s->next)
		s->next->previous = &s->next;
	c->requests = s;
	c->references++;
}

static int
ready(void *arg, struct crt_native_peer *peer)
{
	struct crt_native_context *c = arg;
	struct bridge_peer        *p = peer_data(c, peer);
	if (p == NULL)
		return ENOMEM;
	p->capacity = crt_native_peer_capacity(peer) / 2;
	p->incoming = calloc(p->capacity, sizeof(*p->incoming));
	if (p->incoming == NULL)
		return ENOMEM;
	int rc = crt_native_cart_bulk_create(
	    &crt_native_verbs_ops, crt_native_peer_bulk_endpoint(peer),
	    crt_native_peer_session(peer), crt_native_peer_capacity(peer), &p->bulk);
	if (rc != 0)
		return ENOMEM;
	crt_native_cart_bulk_defer_callbacks(p->bulk);
	for (unsigned i = 0; i < p->capacity; i++) {
		p->incoming[i].index = i;
		p->incoming[i].next  = i + 1;
	}
	return 0;
}

static void
received(void *arg, struct crt_native_peer *peer, struct crt_native_rpc *engine,
	 struct crt_native_token token, uint32_t opcode, const void *payload, size_t size)
{
	struct crt_native_context *c = arg;
	struct bridge_peer        *p = crt_native_peer_data(peer);
	if (p == NULL || p->free_head == p->capacity) {
		crt_native_peer_fail(peer, ENOBUFS);
		return;
	}
	struct incoming *in = &p->incoming[p->free_head];
	p->free_head        = in->next;
	in->peer            = peer;
	in->engine          = engine;
	in->token           = token;
	in->opcode          = opcode;
	in->payload         = payload;
	in->size            = size;
	int rc = crt_native_channel_retain(crt_native_peer_channel(peer), payload, size, &in->rx);
	if (rc != 0) {
		in->next     = p->free_head;
		p->free_head = in->index;
		crt_native_peer_fail(peer, rc);
		return;
	}
	in->event.client = false;
	p->incoming_active++;
	queue_event(c, &in->event);
}

static void
completed(void *arg, int error, uint32_t status, const void *payload, size_t size)
{
	struct crt_native_rpc_state *s = arg;
	if (s->event_queued)
		return;
	s->event_queued = true;
	s->error        = error;
	s->status       = status;
	s->payload      = payload;
	s->size         = size;
	if (error == 0 && size) {
		int rc = crt_native_channel_retain(crt_native_peer_channel(s->peer), payload, size,
						   &s->rx);
		if (rc != 0) {
			s->copy = malloc(size);
			if (s->copy) {
				memcpy(s->copy, payload, size);
				s->payload = s->copy;
			} else {
				s->error   = ENOMEM;
				s->payload = NULL;
				s->size    = 0;
				crt_native_peer_fail(s->peer, ENOMEM);
			}
		}
	}
	s->event.client = true;
	queue_event(s->context, &s->event);
}

static void
failed(void *arg, struct crt_native_peer *peer, int error)
{
	struct crt_native_context *c = arg;
	if (error != ECANCELED && error != ESHUTDOWN)
		D_ERROR("doca+rc: context %d peer %s failed: errno=%d (%s)\n", c->cart->cc_idx,
			crt_native_peer_uri(peer), error, strerror(error));
	for (struct crt_native_rpc_state *s = c->requests; s != NULL; s = s->next)
		if (s->peer == peer && s->submitted && !s->sent && !s->event_queued)
			completed(s, error, 0, NULL, 0);
}

/* Registered serializers run under the transport mutex. User handlers and
 * completion callbacks never do: that would invert CaRT's RPC -> context lock
 * order against concurrent send/reply/cancel calls from application threads.
 */
static int
encode(void *arg, void *buffer, size_t capacity, size_t *used)
{
	struct crt_native_rpc_state    *s   = arg;
	struct bridge_peer             *p   = crt_native_peer_data(s->peer);
	struct crt_native_cart_bulk_txn txn = {.binding = p->bulk};
	int rc = crt_native_cart_payload(s->rpc, s->receiving, CRT_PROC_ENCODE, buffer, capacity,
					 crt_native_cart_bulk_proc, &txn, used);
	if (rc == 0)
		rc = crt_native_cart_bulk_commit(&txn);
	else
		crt_native_cart_bulk_abort(&txn);
	/* Native engine uses errno; CaRT body/encoding failures are explicit,
	 * never a Mercury fallback. Keep the exact encoding error for completion.
	 */
	if (rc != 0) {
		s->error = rc;
		return rc == -DER_AGAIN ? EAGAIN : EMSGSIZE;
	}
	return 0;
}

static int
prepare_header(struct crt_rpc_priv *rpc)
{
	struct crt_req_hdr *h = &rpc->crp_req_hdr;
	h->crh_flags          = rpc->crp_flags;
	int rc                = crt_req_dst_set(
            h, crt_grp_priv_get_primary_rank(rpc->crp_grp_priv, rpc->crp_pub.cr_ep.ep_rank),
            rpc->crp_pub.cr_ep.ep_tag);
	if (rc)
		return rc;
	h->crh_deadline_unix_sec = rpc->crp_deadline_sec;
	if (crt_is_service()) {
		h->crh_src_rank =
		    crt_grp_priv_get_primary_rank(rpc->crp_grp_priv, rpc->crp_grp_priv->gp_self);
		h->crh_common.chc_hlc = d_hlc_get();
	} else {
		h->crh_src_rank       = CRT_NO_RANK;
		h->crh_common.chc_hlc = d_hlct_get();
	}
	return 0;
}

static void
queue_bulk_callbacks(struct crt_native_context *c, struct bridge_peer *p)
{
	if (!p->dispatch_queued && crt_native_cart_bulk_has_callbacks(p->bulk)) {
		p->dispatch_next   = c->bulk_dispatch;
		c->bulk_dispatch   = p;
		p->dispatch_queued = true;
	}
}

static int
drive_peer(void *arg, struct crt_native_peer *peer)
{
	struct crt_native_context *c = arg;
	struct bridge_peer        *p = crt_native_peer_data(peer);
	if (p == NULL || p->bulk == NULL)
		return 0;
	int rc = crt_native_cart_bulk_progress(p->bulk);
	queue_bulk_callbacks(c, p);
	if (rc != 0 && rc != -DER_AGAIN && rc != -DER_CANCELED)
		return EIO;
	if (crt_native_peer_status(peer) != 0)
		return 0;
	unsigned budget = 32;
	while (p->waiting && budget) {
		struct crt_native_rpc_state *s = p->waiting;
		budget--;
		if (s->canceled || s->event_queued) {
			remove_pending(s);
			if (!s->event_queued)
				completed(s, ECANCELED, 0, NULL, 0);
			continue;
		}
		s->error = 0;
		rc       = crt_native_channel_call_encoded(crt_native_peer_channel(peer),
							   s->rpc->crp_pub.cr_opc, encode, s, completed,
							   s, &s->id);
		if (rc == EAGAIN)
			break;
		remove_pending(s);
		if (rc != 0) {
			int error = s->error;
			completed(s, rc, 0, NULL, 0);
			if (error)
				s->error = error;
			continue;
		}
		s->sent = true;
		/* CaRT itself refreshes NO_TIMEOUT calls while membership is valid. */
		if (s->rpc->crp_opc_info->coi_reset_timer)
			continue;
		/* Convert the CaRT wall-clock deadline into a local monotonic one. */
		struct timespec wall, mono;
		if (clock_gettime(CLOCK_REALTIME, &wall) || clock_gettime(CLOCK_MONOTONIC, &mono))
			return EIO;
		int64_t  seconds  = (int64_t)s->rpc->crp_deadline_sec - wall.tv_sec;
		uint64_t deadline = 1;
		if (seconds > 0)
			deadline = (uint64_t)mono.tv_sec * 1000000000 + mono.tv_nsec +
				   (uint64_t)seconds * 1000000000 - wall.tv_nsec;
		crt_native_channel_set_deadline(crt_native_peer_channel(peer), s->id, deadline);
	}
	return 0;
}

static int
destroy_peer(void *arg, struct crt_native_peer *peer)
{
	struct crt_native_context *c = arg;
	struct bridge_peer        *p = crt_native_peer_data(peer);
	if (p == NULL)
		return 0;
	if (p->requests || p->incoming_active || p->dispatch_queued)
		return EBUSY;
	int rc = p->bulk ? crt_native_cart_bulk_destroy(p->bulk) : 0;
	if (rc != 0)
		return EBUSY;
	struct bridge_peer **link = &c->peers;
	while (*link != p)
		link = &(*link)->next;
	*link = p->next;
	free(p->incoming);
	free(p);
	crt_native_peer_set_data(peer, NULL);
	return 0;
}

static int
setting(const char *name, long fallback, long minimum, long maximum, long *out)
{
	const char *value = getenv(name);
	if (value == NULL) {
		*out = fallback;
		return 0;
	}
	char *end;
	errno  = 0;
	long n = strtol(value, &end, 10);
	if (errno || *value == '\0' || *end || n < minimum || n > maximum)
		return -DER_INVAL;
	*out = n;
	return 0;
}

static int
interface_ip(const char *name, char ip[INET_ADDRSTRLEN])
{
	struct in_addr address;
	if (name == NULL)
		return -DER_INVAL;
	if (inet_pton(AF_INET, name, &address) == 1) {
		strcpy(ip, name);
		return 0;
	}
	struct ifaddrs *list;
	if (getifaddrs(&list))
		return -DER_INVAL;
	int rc = -DER_NONEXIST;
	for (struct ifaddrs *a = list; a; a = a->ifa_next)
		if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET &&
		    strcmp(a->ifa_name, name) == 0 &&
		    inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, ip,
			      INET_ADDRSTRLEN)) {
			rc = 0;
			break;
		}
	freeifaddrs(list);
	return rc;
}

int
crt_native_ctx_init(struct crt_hg_context *hg, int index, bool primary, int iface)
{
	struct crt_native_context *c = calloc(1, sizeof(*c));
	if (c == NULL) {
		D_ERROR("doca+rc: context %d allocation failed\n", index);
		return -DER_NOMEM;
	}
	c->cart = container_of(hg, struct crt_context, cc_hg_ctx);
	atomic_init(&c->bulks, 0);
	pthread_mutexattr_t attr;
	int                 err = pthread_mutexattr_init(&attr);
	if (err) {
		free(c);
		return -DER_MISC;
	}
	err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (!err)
		err = pthread_mutex_init(&c->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	if (err) {
		free(c);
		return -DER_MISC;
	}
	if (pthread_mutex_init(&c->progress_mutex, NULL)) {
		pthread_mutex_destroy(&c->mutex);
		free(c);
		return -DER_MISC;
	}
	hg->chc_native = c;
	char        ip[INET_ADDRSTRLEN];
	const char *iface_name = crt_provider_iface_str_get(primary, CRT_PROV_DOCA_RC, iface);
	const char *stage      = "configuration";
	int         rc         = interface_ip(iface_name, ip);
	const char *domain     = crt_provider_domain_str_get(primary, CRT_PROV_DOCA_RC, iface);
	long        port, gid, receives, frames, peers, read_depth, inline_size, stats, cq_batches,
	    signal_interval;
	if (!rc)
		rc = setting("CRT_NATIVE_PORT", 0, 0, 255, &port);
	if (!rc)
		rc = setting("CRT_NATIVE_GID_INDEX", -2, -2, 255, &gid);
	if (!rc) {
		stage                                      = "select local RDMA interface";
		struct crt_native_device_options   options = {.interface = iface_name,
							      .ipv4      = ip,
							      .domain    = domain,
							      .device = getenv("CRT_NATIVE_DEVICE"),
							      .port   = port,
							      .gid_index = gid};
		struct crt_native_device_selection selected;
		int selected_rc = crt_native_select_device(&options, &selected);
		if (selected_rc) {
			D_ERROR("doca+rc: %s for iface=%s IPv4=%s domain=%s device_override=%s "
				"port=%ld gid_index=%ld: %s\n",
				selected_rc == EADDRNOTAVAIL ? "ambiguous RDMA mapping"
							     : "no usable RDMA mapping",
				iface_name, ip, domain ? domain : "(auto)",
				options.device ? options.device : "(auto)", port, gid,
				strerror(selected_rc));
			rc = from_errno(selected_rc);
		} else {
			c->device = strdup(selected.device);
			rc        = c->device ? 0 : -DER_NOMEM;
			port      = selected.port;
			gid       = selected.gid_index;
			D_INFO("doca+rc: context %d selected iface=%s device=%s port=%ld "
			       "gid_index=%ld "
			       "RoCEv2=%d\n",
			       index, selected.interface, selected.device, port, gid,
			       selected.roce_v2);
		}
	}
	if (!rc)
		rc = setting("CRT_NATIVE_RECEIVES", 64, 2, 8192, &receives);
	if (!rc)
		rc = setting("CRT_NATIVE_FRAME_SIZE", 65536, 128, 1048576, &frames);
	if (!rc)
		rc = setting("CRT_NATIVE_MAX_PEERS", 128, 1, 4096, &peers);
	if (!rc)
		rc = setting("CRT_NATIVE_RDMA_READ_DEPTH", 16, 1, 255, &read_depth);
	if (!rc)
		rc = setting("CRT_NATIVE_INLINE_SIZE", 256, 0, 4096, &inline_size);
	if (!rc)
		rc = setting("CRT_NATIVE_STATS", 0, 0, 1, &stats);
	if (!rc)
		rc = setting("CRT_NATIVE_CQ_BATCHES", 4, 1, 16, &cq_batches);
	if (!rc)
		rc = setting("CRT_NATIVE_SIGNAL_INTERVAL", 8, 1, 32, &signal_interval);
	struct crt_na_config *na = crt_provider_get_na_config(primary, CRT_PROV_DOCA_RC);
	if (!rc && na->noc_auth_key && na->noc_auth_key[0]) {
		D_ERROR("doca+rc does not implement fabric auth keys\n");
		rc = -DER_NOTSUPPORTED;
	}
	int      base        = na->noc_port;
	unsigned listen_port = crt_is_service() ? (base > 0 ? base : 31416) + index : 0;
	if (!rc && listen_port > 65535)
		rc = -DER_INVAL;
	if (!rc) {
		struct crt_native_config config    = {.device          = c->device,
						      .port            = port,
						      .gid_index       = gid,
						      .depth           = receives * 4,
						      .rdma_read_depth = read_depth,
						      .inline_size     = inline_size,
						      .stats           = stats,
						      .cq_poll_batches = cq_batches,
						      .signal_interval = signal_interval};
		struct crt_native_limits limits    = {.frame_size = frames, .receives = receives};
		struct crt_native_net_callbacks cb = {.ready    = ready,
						      .request  = received,
						      .progress = drive_peer,
						      .failed   = failed,
						      .destroy  = destroy_peer};
		stage                              = "RDMA endpoint probe";
		rc = from_errno(crt_native_verbs_ops.open(&config, &c->probe));
		if (!rc) {
			stage = "close RDMA endpoint probe";
			rc    = from_errno(crt_native_verbs_ops.close(c->probe));
			if (!rc)
				c->probe = NULL;
		}
		if (!rc) {
			stage = "create TCP bootstrap listener";
			rc    = from_errno(crt_native_net_create(&crt_native_verbs_ops, &config,
								 &limits, ip, listen_port, peers,
								 10000, &cb, c, &c->net));
		}
	}
	if (rc) {
		D_ERROR("doca+rc: context %d initialization failed at %s: iface=%s device=%s " DF_RC
			"\n",
			index, stage, iface_name ? iface_name : "(unset)",
			c->device ? c->device : "(unset)", DP_RC(rc));
		crt_native_ctx_fini(hg);
		return rc;
	}
	struct crt_prov_gdata *data = crt_get_prov_gdata(primary, CRT_PROV_DOCA_RC);
	if (crt_is_service() && data->cpg_addr[0] == '\0')
		snprintf(data->cpg_addr, sizeof(data->cpg_addr), "%s", crt_native_net_uri(c->net));
	return 0;
}

int
crt_native_ctx_addr(struct crt_hg_context *hg, char *buffer, size_t *size)
{
	if (hg->chc_native == NULL || hg->chc_native->net == NULL || size == NULL)
		return -DER_INVAL;
	const char *uri  = crt_native_net_uri(hg->chc_native->net);
	size_t      need = strlen(uri) + 1;
	if (buffer == NULL) {
		*size = need;
		return 0;
	}
	if (*size < need) {
		*size = need;
		return -DER_TRUNC;
	}
	memcpy(buffer, uri, need);
	*size = need;
	return 0;
}

int
crt_native_addr_lookup(struct crt_hg_context *hg, const char *uri, void **out)
{
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL || out == NULL)
		return -DER_INVAL;
	lock(c);
	const char             *stage = "network lookup";
	struct crt_native_peer *peer;
	int rc = c->stopping ? ECANCELED : crt_native_net_lookup(c->net, uri, &peer);
	if (!rc) {
		int status = crt_native_peer_status(peer);
		if (status && status != EINPROGRESS) {
			stage = "peer setup";
			crt_native_peer_put(peer);
			rc = status;
		}
	}
	if (!rc && !peer_data(c, peer)) {
		stage = "allocate peer state";
		crt_native_peer_fail(peer, ENOMEM);
		crt_native_peer_put(peer);
		rc = ENOMEM;
	}
	if (!rc)
		*out = peer;
	else
		D_ERROR("doca+rc: context %d address lookup failed at %s: uri=%s "
			"errno=%d (%s)\n",
			c->cart->cc_idx, stage, uri ? uri : "(null)", rc, strerror(rc));
	unlock(c);
	return from_errno(rc);
}

void
crt_native_addr_free(void *addr)
{
	crt_native_peer_put(addr);
}

int
crt_native_req_create(struct crt_hg_context *hg, struct crt_rpc_priv *rpc)
{
	if (rpc->crp_native)
		return 0;
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL || rpc->crp_hg_addr == NULL)
		return -DER_INVAL;
	struct crt_native_rpc_state *s = &rpc->crp_native_storage;
	*s                             = (struct crt_native_rpc_state){
					.context = c, .rpc = rpc, .peer = (void *)rpc->crp_hg_addr};
	lock(c);
	int status = crt_native_peer_status(s->peer);
	if (status && status != EINPROGRESS) {
		unlock(c);
		return from_errno(status);
	}
	add_request(c, s);
	rpc->crp_native = s;
	unlock(c);
	return 0;
}
void
crt_native_req_send(struct crt_rpc_priv *rpc)
{
	struct crt_native_rpc_state *s         = rpc->crp_native;
	int                          header_rc = prepare_header(rpc);
	rpc->crp_state                         = RPC_STATE_REQ_SENT;
	RPC_ADDREF(rpc); /* queue owns this reference until native completion */
	lock(s->context);
	s->submitted = true;
	if (header_rc) {
		completed(s, header_rc, 0, NULL, 0);
		unlock(s->context);
		return;
	}
	struct bridge_peer *p = crt_native_peer_data(s->peer);
	s->pending_next       = NULL;
	s->pending_previous   = p->waiting_tail;
	*p->waiting_tail      = s;
	p->waiting_tail       = &s->pending_next;
	crt_native_peer_kick(s->peer);
	int error = crt_native_peer_status(s->peer);
	if (error != 0 && error != EINPROGRESS)
		completed(s, error, 0, NULL, 0);
	unlock(s->context);
}
int
crt_native_req_cancel(struct crt_rpc_priv *rpc)
{
	struct crt_native_rpc_state *s = rpc->crp_native;
	if (s == NULL)
		return -DER_INVAL;
	lock(s->context);
	s->canceled = true;
	int rc      = 0;
	if (s->sent && !s->event_queued)
		rc = crt_native_channel_cancel(crt_native_peer_channel(s->peer), s->id, ECANCELED);
	else if (s->submitted && !s->event_queued) {
		remove_pending(s);
		completed(s, ECANCELED, 0, NULL, 0);
	}
	unlock(s->context);
	return rc == ENOENT ? 0 : from_errno(rc);
}

int
crt_native_reply_send(struct crt_rpc_priv *rpc)
{
	struct crt_native_rpc_state *s = rpc->crp_native;
	if (s == NULL || !s->receiving)
		return -DER_INVAL;
	lock(s->context);
	int rc = crt_native_peer_status(s->peer);
	if (!rc && s->reply_queued)
		rc = EALREADY;
	if (!rc) {
		rpc->crp_reply_hdr.crh_common.chc_hlc = d_hlc_get();
		s->error                              = 0;
		rc = crt_native_rpc_reply_encoded(s->engine, s->token, 0, encode, s);
		if (!rc)
			s->reply_queued = true;
		crt_native_peer_kick(s->peer);
	}
	int result = s->error < 0 ? s->error : from_errno(rc);
	unlock(s->context);
	return result;
}
void
crt_native_reply_error(struct crt_rpc_priv *rpc, int error)
{
	rpc->crp_reply_hdr.crh_status = error;
	crt_native_reply_send(rpc);
	rpc->crp_reply_pending = 0;
}

static int
decode(struct crt_native_rpc_state *s, bool reply)
{
	struct bridge_peer             *p   = crt_native_peer_data(s->peer);
	struct crt_native_cart_bulk_txn txn = {.binding = p->bulk};
	size_t                          used;
	return crt_native_cart_payload(s->rpc, reply, CRT_PROC_DECODE, (void *)s->payload, s->size,
				       crt_native_cart_bulk_proc, &txn, &used);
}

static void
dispatch_incoming(struct crt_native_context *c, struct incoming *in)
{
	struct crt_rpc_priv *rpc;
	struct crt_req_hdr   header;
	int                  rc   = crt_native_header_peek(in->payload, in->size, &header);
	d_rank_t             self = crt_gdata.cg_grp->gg_primary_grp->gp_self;
	if (!rc && (header.crh_opc != in->opcode ||
		    crt_req_dst_tag(&header) != (uint32_t)c->cart->cc_idx ||
		    (self != CRT_NO_RANK && crt_req_dst_rank(&header) != self)))
		rc = -DER_BAD_TARGET;
	if (!rc) {
		struct crt_rpc_priv tmp_in = {.crp_req_hdr = header, .crp_flags = header.crh_flags},
				    tmp_out = {0};
		tmp_in.crp_pub.cr_ctx       = c->cart;
		rc                          = crt_hg_process_header(&tmp_in, &tmp_out);
		uint64_t offset;
		if (!rc && d_hlc_get_msg(header.crh_common.chc_hlc, NULL, &offset) &&
		    !crt_opc_is_swim(in->opcode))
			rc = -DER_HLC_SYNC;
	}
	lock(c);
	int peer_error = crt_native_peer_status(in->peer);
	unlock(c);
	if (!rc && peer_error)
		rc = from_errno(peer_error);
	if (!rc)
		rc = crt_rpc_priv_alloc(in->opcode, &rpc, false);
	if (rc != 0) {
		lock(c);
		crt_native_peer_fail(in->peer, EPROTO);
		crt_native_channel_release(&in->rx);
		unlock(c);
		goto recycle;
	}
	crt_rpc_priv_init(rpc, c->cart, true);
	struct crt_native_rpc_state *s = &rpc->crp_native_storage;
	*s                             = (struct crt_native_rpc_state){.context   = c,
								       .peer      = in->peer,
								       .rpc       = rpc,
								       .engine    = in->engine,
								       .token     = in->token,
								       .rx        = in->rx,
								       .payload   = in->payload,
								       .size      = in->size,
								       .receiving = true};
	in->rx                         = (struct crt_native_rx_lease){0};
	rpc->crp_native                = s;
	lock(c);
	add_request(c, s);
	rpc->crp_input_got = 1;
	rc                 = decode(s, false);
	if (rc == 0) {
		rpc->crp_flags = rpc->crp_req_hdr.crh_flags;

		rpc->crp_reply_hdr.crh_common.chc_rpcid = rpc->crp_req_hdr.crh_common.chc_rpcid;
		struct crt_rpc_priv tmp                 = {.crp_req_hdr   = rpc->crp_req_hdr,
							   .crp_flags     = rpc->crp_flags,
							   .crp_coreq_hdr = rpc->crp_coreq_hdr};
		tmp.crp_pub.cr_ctx                      = c->cart;
		rc                                      = crt_hg_process_header(&tmp, rpc);
		rpc->crp_pub.cr_ep.ep_rank              = crt_req_dst_rank(&rpc->crp_req_hdr);
		rpc->crp_pub.cr_ep.ep_tag               = crt_req_dst_tag(&rpc->crp_req_hdr);
	} else
		crt_native_peer_fail(in->peer, EPROTO);
	unlock(c);
	if (rc == 0 && rpc->crp_opc_info->coi_rpc_cb == NULL)
		rc = -DER_UNREG;
	if (rc == 0)
		rc = (rpc->crp_flags & CRT_RPC_FLAG_COLL) ? crt_corpc_common_hdlr(rpc)
							  : crt_rpc_common_hdlr(rpc);
	if (rc != 0) {
		crt_native_reply_error(rpc, rc);
		RPC_DECREF(rpc);
	}
recycle:
	lock(c);
	struct bridge_peer *p = crt_native_peer_data(in->peer);
	D_ASSERT(p->incoming_active > 0);
	p->incoming_active--;
	in->next     = p->free_head;
	p->free_head = in->index;
	unlock(c);
}

static void
dispatch_completion(struct crt_native_rpc_state *s)
{
	struct crt_rpc_priv       *rpc = s->rpc;
	struct crt_native_context *c   = s->context;
	crt_rpc_lock(rpc);
	D_SPIN_LOCK(&rpc->crp_lock);
	bool already = rpc->crp_completed;
	D_SPIN_UNLOCK(&rpc->crp_lock);
	if (already) {
		crt_rpc_unlock(rpc);
		RPC_DECREF(rpc);
		return;
	}
	int rc = s->error < 0 ? s->error : from_errno(s->error);
	if (!rc && s->status)
		rc = s->status <= INT_MAX ? -(int)s->status : -DER_PROTO;
	if (!rc && !rpc->crp_opc_info->coi_no_reply) {
		lock(c);
		rpc->crp_output_got = 1;
		rc                  = decode(s, true);
		if (!rc && rpc->crp_reply_hdr.crh_common.chc_rpcid !=
			       rpc->crp_req_hdr.crh_common.chc_rpcid)
			rc = -DER_PROTO;
		if (rc != 0)
			crt_native_peer_fail(s->peer, EPROTO);
		unlock(c);
		if (!rc) {
			if (crt_is_service()) {
				uint64_t offset;
				if (d_hlc_get_msg(rpc->crp_reply_hdr.crh_common.chc_hlc, NULL,
						  &offset) &&
				    !crt_opc_is_swim(rpc->crp_pub.cr_opc))
					rc = -DER_HLC_SYNC;
			} else
				d_hlct_sync(rpc->crp_reply_hdr.crh_common.chc_hlc);
		}
	}
	if (rc == -DER_CANCELED && crt_req_timedout(rpc))
		rc = -DER_TIMEDOUT;
	/* Untracking requires a terminal state and uses it to return endpoint
	 * credits. Complete-and-unlock sets the state too late for this step.
	 * Keep the RPC lock held across the transition and untracking.
	 */
	if (rc == -DER_CANCELED)
		rpc->crp_state = RPC_STATE_CANCELED;
	else if (rc == -DER_TIMEDOUT)
		rpc->crp_state = RPC_STATE_TIMEOUT;
	else if (rc == -DER_UNREACH)
		rpc->crp_state = RPC_STATE_FWD_UNREACH;
	else
		rpc->crp_state = RPC_STATE_COMPLETED;
	crt_context_req_untrack(rpc);
	crt_rpc_complete_and_unlock(rpc, rc);
	RPC_DECREF(rpc); /* native send queue's additional reference */
}

int
crt_native_ctx_progress(struct crt_hg_context *hg)
{
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL)
		return -DER_UNINIT;
	if (pthread_mutex_trylock(&c->progress_mutex))
		return -DER_TIMEDOUT;
	unsigned work;
	lock(c);
	int rc = c->net ? crt_native_net_progress(c->net, &work) : 0;
	unlock(c);
	unsigned callbacks = 0;
	for (unsigned budget = 0; budget < 32; budget++) {
		lock(c);
		struct crt_native_event *event = c->events;
		if (event) {
			c->events = event->next;
			if (!c->events)
				c->events_tail = NULL;
		}
		unlock(c);
		if (!event)
			break;
		if (event->client)
			dispatch_completion(
			    container_of(event, struct crt_native_rpc_state, event));
		else
			dispatch_incoming(c, container_of(event, struct incoming, event));
		callbacks++;
	}
	/* Dispatch only bindings with completed callbacks, outside the context
	 * lock. The queued flag pins their peer data until dispatch completes.
	 */
	for (unsigned budget = 0; budget < 32; budget++) {
		lock(c);
		struct bridge_peer *p = c->bulk_dispatch;
		if (p)
			c->bulk_dispatch = p->dispatch_next;
		unlock(c);
		if (!p)
			break;
		callbacks += crt_native_cart_bulk_dispatch(p->bulk);
		lock(c);
		p->dispatch_queued = false;
		queue_bulk_callbacks(c, p);
		unlock(c);
	}
	pthread_mutex_unlock(&c->progress_mutex);
	return rc ? from_errno(rc) : callbacks ? 0 : -DER_TIMEDOUT;
}

int
crt_native_ctx_unpost(struct crt_hg_context *hg)
{
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL)
		return 0;
	lock(c);
	c->stopping = true;
	if (c->net)
		crt_native_net_shutdown(c->net);
	unlock(c);
	return 0;
}

static int
ctx_fini_once(struct crt_hg_context *hg)
{
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL)
		return 0;
	if (pthread_mutex_trylock(&c->progress_mutex))
		return -DER_BUSY;
	lock(c);
	if (c->references || c->events) {
		unlock(c);
		pthread_mutex_unlock(&c->progress_mutex);
		return -DER_BUSY;
	}
	int rc = 0;
	if (c->net) {
		crt_native_net_shutdown(c->net);
		unsigned work;
		crt_native_net_progress(c->net, &work);
		rc = crt_native_net_destroy(c->net);
		if (!rc)
			c->net = NULL;
	}
	if (!rc && c->probe) {
		rc = crt_native_verbs_ops.close(c->probe);
		if (!rc)
			c->probe = NULL;
	}
	if (!rc && atomic_load(&c->bulks))
		rc = EBUSY;
	unlock(c);
	pthread_mutex_unlock(&c->progress_mutex);
	if (rc)
		return rc == EBUSY ? -DER_BUSY : from_errno(rc);
	pthread_mutex_destroy(&c->progress_mutex);
	pthread_mutex_destroy(&c->mutex);
	free(c->device);
	free(c);
	hg->chc_native = NULL;
	return 0;
}

int
crt_native_ctx_fini(struct crt_hg_context *hg)
{
	struct crt_native_context *c = hg->chc_native;
	if (c == NULL)
		return 0;
	/* A destroy called from a progress callback must not wait on itself. */
	if (pthread_mutex_trylock(&c->progress_mutex))
		return -DER_BUSY;
	pthread_mutex_unlock(&c->progress_mutex);
	struct timespec start, now;
	if (clock_gettime(CLOCK_MONOTONIC, &start))
		return -DER_MISC;
	crt_native_ctx_unpost(hg);
	for (;;) {
		int rc = ctx_fini_once(hg);
		if (rc != -DER_BUSY && rc != -DER_AGAIN)
			return rc;
		/* QP flushes can exceed one CQ batch or arrive asynchronously.
		 * Dispatch deferred RPC/bulk callbacks outside the transport lock
		 * before retrying destruction. No live storage is forcibly freed.
		 */
		int progress_rc = crt_native_ctx_progress(hg);
		if (progress_rc != 0 && progress_rc != -DER_TIMEDOUT)
			return progress_rc;
		if (clock_gettime(CLOCK_MONOTONIC, &now))
			return -DER_MISC;
		int64_t elapsed = (int64_t)(now.tv_sec - start.tv_sec) * INT64_C(1000000000) +
				  now.tv_nsec - start.tv_nsec;
		if (elapsed >= INT64_C(1000000000))
			return rc;
		const struct timespec pause = {.tv_nsec = 1000000};
		nanosleep(&pause, NULL);
	}
}

void
crt_native_req_destroy(struct crt_rpc_priv *rpc)
{
	struct crt_native_rpc_state *s = rpc->crp_native;
	struct crt_native_context   *c = s->context;
	lock(c);
	struct bridge_peer *p = crt_native_peer_data(s->peer);
	if (p) {
		struct crt_native_cart_bulk_txn txn = {.binding = p->bulk};
		size_t                          used;
		if (rpc->crp_input_got)
			crt_native_cart_payload(rpc, false, CRT_PROC_FREE, NULL, 0,
						crt_native_cart_bulk_proc, &txn, &used);
		if (rpc->crp_output_got)
			crt_native_cart_payload(rpc, true, CRT_PROC_FREE, NULL, 0,
						crt_native_cart_bulk_proc, &txn, &used);
	}
	if (s->receiving && !s->reply_queued && !crt_native_peer_status(s->peer)) {
		int rc = rpc->crp_opc_info->coi_no_reply
			     ? crt_native_rpc_reply(s->engine, s->token, 0, NULL, 0)
			     : EPROTO;
		/* Never leave an unreachable HANDLING token occupying the peer's
		 * reply window after the application's last RPC reference is gone.
		 */
		if (rc)
			crt_native_peer_fail(s->peer, rc);
	}
	if (s->rx.channel)
		crt_native_channel_release(&s->rx);
	free(s->copy);
	remove_pending(s);
	*s->previous = s->next;
	if (s->next)
		s->next->previous = s->previous;
	D_ASSERT(p != NULL && p->requests > 0);
	p->requests--;
	c->references--;
	unlock(c);
	crt_rpc_priv_fini(rpc);
	crt_rpc_priv_free(rpc);
}

int
crt_native_bulk_context_hold(void *context)
{
	struct crt_context        *ctx = context;
	struct crt_native_context *c   = ctx->cc_hg_ctx.chc_native;
	if (c == NULL)
		return -DER_UNINIT;
	lock(c);
	int rc = c->stopping ? -DER_CANCELED : 0;
	if (!rc)
		atomic_fetch_add(&c->bulks, 1);
	unlock(c);
	return rc;
}
void
crt_native_bulk_context_drop(void *context)
{
	struct crt_context *ctx = context;
	atomic_fetch_sub(&ctx->cc_hg_ctx.chc_native->bulks, 1);
}

char *
crt_native_origin(struct crt_rpc_priv *rpc)
{
	return (char *)crt_native_peer_uri(rpc->crp_native->peer);
}

int
crt_native_protocol_info(struct crt_protocol_info **out)
{
	if (out == NULL)
		return -DER_INVAL;
	*out = NULL;
	int                 count;
	struct ibv_device **devices = ibv_get_device_list(&count);
	if (devices == NULL)
		return 0;
	for (int i = 0; i < count; i++) {
		struct crt_protocol_info *info = calloc(1, sizeof(*info));
		if (!info) {
			ibv_free_device_list(devices);
			return -DER_NOMEM;
		}
		info->class_name    = strdup("doca");
		info->protocol_name = strdup("rc");
		info->device_name   = strdup(ibv_get_device_name(devices[i]));
		if (!info->class_name || !info->protocol_name || !info->device_name) {
			free(info->class_name);
			free(info->protocol_name);
			free(info->device_name);
			free(info);
			ibv_free_device_list(devices);
			return -DER_NOMEM;
		}
		info->next = *out;
		*out       = info;
	}
	ibv_free_device_list(devices);
	return 0;
}

#else /* explicit unsupported-build behavior, no silent Mercury fallback */
void
crt_native_addr_free(void *addr)
{
	(void)addr;
}
int
crt_native_ctx_init(struct crt_hg_context *h, int i, bool p, int f)
{
	(void)h;
	(void)i;
	(void)p;
	(void)f;
	D_ERROR("doca+rc requires CART_NATIVE_RC=yes and libibverbs\n");
	return -DER_NOTSUPPORTED;
}
int
crt_native_ctx_fini(struct crt_hg_context *h)
{
	(void)h;
	return 0;
}
int
crt_native_ctx_unpost(struct crt_hg_context *h)
{
	(void)h;
	return 0;
}
int
crt_native_ctx_progress(struct crt_hg_context *h)
{
	(void)h;
	return -DER_NOTSUPPORTED;
}
int
crt_native_ctx_addr(struct crt_hg_context *h, char *b, size_t *s)
{
	(void)h;
	(void)b;
	(void)s;
	return -DER_NOTSUPPORTED;
}
int
crt_native_addr_lookup(struct crt_hg_context *h, const char *u, void **o)
{
	(void)h;
	(void)u;
	(void)o;
	return -DER_NOTSUPPORTED;
}
int
crt_native_req_create(struct crt_hg_context *h, struct crt_rpc_priv *r)
{
	(void)h;
	(void)r;
	return -DER_NOTSUPPORTED;
}
void
crt_native_req_send(struct crt_rpc_priv *r)
{
	(void)r;
}
int
crt_native_req_cancel(struct crt_rpc_priv *r)
{
	(void)r;
	return -DER_NOTSUPPORTED;
}
int
crt_native_reply_send(struct crt_rpc_priv *r)
{
	(void)r;
	return -DER_NOTSUPPORTED;
}
void
crt_native_reply_error(struct crt_rpc_priv *r, int e)
{
	(void)r;
	(void)e;
}
void
crt_native_req_destroy(struct crt_rpc_priv *r)
{
	(void)r;
}
char *
crt_native_origin(struct crt_rpc_priv *r)
{
	(void)r;
	return "native-disabled";
}
int
crt_native_protocol_info(struct crt_protocol_info **o)
{
	*o = NULL;
	return -DER_NOTSUPPORTED;
}
int
crt_native_bulk_context_hold(void *c)
{
	(void)c;
	return -DER_NOTSUPPORTED;
}
void
crt_native_bulk_context_drop(void *c)
{
	(void)c;
}
#endif
