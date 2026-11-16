/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../cart_bulk.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

struct crt_native_ep {
	unsigned                     registered, head, tail;
	struct crt_native_completion completions[16];
};
struct crt_native_mr {
	struct crt_native_ep *ep;
	uint8_t              *base;
};
static pthread_t owner_thread;
static int
reg(struct crt_native_ep *ep, void *data, size_t size, unsigned access, struct crt_native_mr **out,
    struct crt_native_remote_memory *remote)
{
	assert(pthread_equal(pthread_self(), owner_thread));
	assert(access != 0);
	*out = malloc(sizeof(**out));
	assert(*out != NULL);
	(*out)->ep   = ep;
	(*out)->base = data;
	ep->registered++;
	*remote = (struct crt_native_remote_memory){(uintptr_t)data, size, 17};
	return 0;
}
static int
dereg(struct crt_native_mr *mr)
{
	mr->ep->registered--;
	free(mr);
	return 0;
}
static int
no_op(struct crt_native_ep *ep)
{
	(void)ep;
	return 0;
}
static int
post(struct crt_native_ep *ep, enum crt_native_opcode opcode, struct crt_native_mr *mr,
     size_t offset, size_t size, const struct crt_native_remote_memory *remote, size_t within,
     void *cookie)
{
	assert(pthread_equal(pthread_self(), owner_thread));
	assert(ep->tail - ep->head < 16);
	if (opcode == CRT_NATIVE_READ)
		memcpy(mr->base + offset, (void *)(uintptr_t)(remote->address + within), size);
	else
		memcpy((void *)(uintptr_t)(remote->address + within), mr->base + offset, size);
	ep->completions[ep->tail++ % 16] = (struct crt_native_completion){cookie, opcode, 0, size};
	return 0;
}
static int
progress(struct crt_native_ep *ep, struct crt_native_completion *out, unsigned capacity,
	 unsigned *count)
{
	*count = 0;
	while (*count < capacity && ep->head != ep->tail)
		out[(*count)++] = ep->completions[ep->head++ % 16];
	return 0;
}
static const struct crt_native_ops ops = {.register_memory   = reg,
					  .deregister_memory = dereg,
					  .close_begin       = no_op,
					  .quiesce           = no_op,
					  .post              = post,
					  .progress          = progress};

/* Concrete CaRT wrapper test doubles; production equivalents live in
 * crt_bulk.c. Exercise the real codec/cache/session and deferred release path.
 */
struct handle {
	atomic_uint                   refs;
	d_sg_list_t                   sgl;
	bool                          remote, alias;
	crt_bulk_perm_t               permission;
	struct crt_native_cart_remote native;
};
int
crt_bulk_addref(crt_bulk_t handle)
{
	struct handle *h = handle;
	atomic_fetch_add(&h->refs, 1);
	return 0;
}
int
crt_bulk_free(crt_bulk_t handle)
{
	struct handle *h = handle;
	if (atomic_fetch_sub(&h->refs, 1) == 1) {
		assert(h->remote);
		crt_native_cart_remote_release(&h->native);
	}
	return 0;
}
int
crt_bulk_access(crt_bulk_t handle, d_sg_list_t *sgl)
{
	struct handle *h = handle;
	if (h->remote)
		return -DER_NOTSUPPORTED;
	if (h->alias)
		*sgl = h->sgl;
	else {
		assert(sgl->sg_nr >= h->sgl.sg_nr);
		for (unsigned i = 0; i < h->sgl.sg_nr; i++)
			sgl->sg_iovs[i] = h->sgl.sg_iovs[i];
		sgl->sg_nr_out = h->sgl.sg_nr;
	}
	return 0;
}
int
crt_native_cart_bulk_permission(crt_bulk_t handle, crt_bulk_perm_t *permission)
{
	struct handle *h = handle;
	*permission      = h->permission == 0 ? CRT_BULK_RO : h->permission;
	return 0;
}
int
crt_native_cart_wrap_remote(struct crt_native_cart_bulk    *binding,
			    struct crt_native_bulk_session *session, uint64_t id,
			    const struct crt_native_bulk_remote *remote, crt_bulk_t *out)
{
	assert(remote->lease_id == id);
	struct handle *h = calloc(1, sizeof(*h));
	assert(h != NULL);
	atomic_init(&h->refs, 1);
	h->remote = true;
	h->native = (struct crt_native_cart_remote){
	    .binding = binding, .session = session, .descriptor = remote, .handle = h};
	crt_native_cart_remote_acquire(&h->native);
	*out = h;
	return 0;
}
int
crt_native_cart_remote_info(crt_bulk_t handle, struct crt_native_cart_bulk **binding,
			    struct crt_native_bulk_session      **session,
			    const struct crt_native_bulk_remote **remote)
{
	struct handle *h = handle;
	if (!h->remote)
		return -DER_INVAL;
	if (binding)
		*binding = h->native.binding;
	if (session)
		*session = h->native.session;
	if (remote)
		*remote = h->native.descriptor;
	return 0;
}
void
crt_native_cart_destroy_remote(struct crt_native_cart_remote *remote)
{
	free(remote->handle);
}
static int
send_ack(void *arg, uint64_t id)
{
	return crt_native_bulk_session_ack(arg, id);
}
static int
ignore_ack(void *arg, uint64_t id)
{
	(void)arg;
	(void)id;
	return 0;
}
static int
field(crt_proc_t proc, void *data)
{
	crt_proc_op_t op;
	int           rc = crt_proc_get_op(proc, &op);
	return rc ? rc : crt_proc_crt_bulk_t(proc, op, data);
}
static void *
worker_free(void *arg)
{
	assert(crt_bulk_free(arg) == 0);
	return NULL;
}

static atomic_uint request_refs = 1;
static bool        expired;
bool
crt_native_cart_bulk_expired(struct crt_bulk_desc *desc)
{
	(void)desc;
	return expired;
}
void
crt_req_addref(crt_rpc_t *rpc)
{
	(void)rpc;
	request_refs++;
}
void
crt_req_decref(crt_rpc_t *rpc)
{
	(void)rpc;
	assert(request_refs > 1);
	request_refs--;
}
struct completion_result {
	unsigned count;
	int      rc;
};
static int
completion(const struct crt_bulk_cb_info *info)
{
	struct completion_result *r = info->bci_arg;
	r->count++;
	r->rc = info->bci_rc;
	assert(request_refs == 2 && info->bci_bulk_desc->bd_len == 32);
	return 0;
}
static int
verify(const struct crt_bulk_cb_info *info)
{
	return ((crt_bulk_cb_t)info->bci_complete_cb)(info);
}

struct submission {
	struct crt_native_cart_bulk *binding;
	struct crt_bulk_desc        *descriptor;
	struct completion_result    *completed;
	crt_bulk_opid_t              id;
};
static void *
submit_worker(void *arg)
{
	struct submission *s = arg;
	assert(crt_native_cart_bulk_transfer(s->binding, s->descriptor, verify, completion,
					     s->completed, &s->id) == 0);
	return NULL;
}

int
main(void)
{
	owner_thread                      = pthread_self();
	struct crt_native_ep            a = {0}, b = {0};
	struct crt_native_bulk_session *sa, *sb;
	struct crt_native_cart_bulk    *ca, *cb;
	assert(crt_native_bulk_session_create(&ops, &a, 8, ignore_ack, NULL, &sa) == 0);
	assert(crt_native_bulk_session_create(&ops, &b, 8, send_ack, sa, &sb) == 0);
	assert(crt_native_cart_bulk_create(&ops, &a, sa, 2, &ca) == 0);
	struct crt_native_cart_bulk *bad_binding;
	assert(crt_native_cart_bulk_create(&ops, &b, sa, 2, &bad_binding) == -DER_INVAL);
	assert(crt_native_cart_bulk_create(&ops, &b, sb, 2, &cb) == 0);
	uint8_t buffer[128];
	char    payload[32] = {0};
	d_iov_t iov         = {
		    .iov_buf = payload, .iov_len = sizeof(payload), .iov_buf_len = sizeof(payload)};
	struct handle h = {.sgl = {.sg_nr = 1, .sg_iovs = &iov}};
	atomic_init(&h.refs, 1);
	char          destination[32];
	d_iov_t       dst_iov = {.iov_buf = destination, .iov_len = 32, .iov_buf_len = 32};
	struct handle dst = {.permission = CRT_BULK_RW, .sgl = {.sg_nr = 1, .sg_iovs = &dst_iov}};
	atomic_init(&dst.refs, 1);
	crt_rpc_t request = {.cr_ctx = (void *)&b};
	memset(payload, 0x42, sizeof(payload));
	for (unsigned alias = 0; alias < 3; alias++) {
		h.alias                                = alias;
		struct crt_native_cart_bulk_txn encode = {.binding = ca}, decode = {.binding = cb};
		crt_bulk_t                      local = &h, remote = NULL;
		size_t                          size, used;
		assert(crt_native_cart_run(field, &local, CRT_PROC_ENCODE, buffer, sizeof(buffer),
					   crt_native_cart_bulk_proc, &encode, &size) == 0);
		assert(encode.count == 1);
		if (alias == 0) {
			/* Retry a failed/unqueued encoder transaction without publishing its key.
			 */
			assert(crt_native_cart_bulk_abort(&encode) == 0);
			assert(crt_native_cart_run(field, &local, CRT_PROC_ENCODE, buffer,
						   sizeof(buffer), crt_native_cart_bulk_proc,
						   &encode, &size) == 0);
		}
		assert(crt_native_cart_bulk_commit(&encode) == 0 && encode.count == 0);
		assert(crt_native_cart_run(field, &remote, CRT_PROC_DECODE, buffer, size,
					   crt_native_cart_bulk_proc, &decode, &used) == 0);
		const struct crt_native_bulk_remote *descriptor;
		assert(crt_native_cart_remote_info(remote, NULL, NULL, &descriptor) == 0);
		assert(descriptor->length == 32 &&
		       descriptor->segments[0].address == (uintptr_t)payload);
		assert(crt_bulk_addref(remote) == 0);
		crt_bulk_t               retained  = remote;
		struct completion_result completed = {0};
		struct crt_bulk_desc     transfer  = {.bd_rpc        = &request,
						      .bd_bulk_op    = CRT_BULK_GET,
						      .bd_remote_hdl = remote,
						      .bd_local_hdl  = &dst,
						      .bd_len        = sizeof(destination)};
		struct submission        submitted = {
			   .binding = cb, .descriptor = &transfer, .completed = &completed};
		pthread_t submit;
		assert(pthread_create(&submit, NULL, submit_worker, &submitted) == 0);
		assert(pthread_join(submit, NULL) == 0);
		crt_bulk_opid_t opid = submitted.id;
		assert(request_refs == 2);
		assert(crt_native_cart_run(field, &remote, CRT_PROC_FREE, NULL, 0,
					   crt_native_cart_bulk_proc, &decode, &used) == 0 &&
		       remote == NULL);
		/* Transfer/application reference delays the final import ACK. */
		expired = alias == 2;
		memset(destination, 0, sizeof(destination));
		assert(crt_native_cart_bulk_progress(cb) == 0);
		assert(completed.count == (expired ? 1 : 0));
		expired = false;
		if (alias == 1) {
			int cancel_rc;
			assert(crt_native_cart_bulk_try_abort(NULL, opid, &cancel_rc) &&
			       cancel_rc == -DER_INVAL);
			assert(crt_native_cart_bulk_try_abort(request.cr_ctx, opid, &cancel_rc) &&
			       cancel_rc == 0);
		}
		pthread_t worker;
		assert(pthread_create(&worker, NULL, worker_free, retained) == 0);
		for (unsigned i = 0; i < 32; i++)
			assert(crt_native_cart_bulk_progress(cb) == 0);
		assert(pthread_join(worker, NULL) == 0);
		assert(crt_native_cart_bulk_progress(cb) == 0);
		assert(completed.count == 1 &&
		       completed.rc == (alias == 2 ? -DER_TIMEDOUT
					: alias    ? -DER_CANCELED
						   : 0) &&
		       request_refs == 1);
		if (alias == 2)
			assert(destination[0] == 0); /* expired before posting */
		else
			assert(memcmp(destination, payload, sizeof(destination)) == 0);
		int abort_rc;
		assert(crt_native_cart_bulk_try_abort(request.cr_ctx, opid, &abort_rc) &&
		       abort_rc == -DER_NONEXIST);
		/* Reclaim the cache to exercise the alternate SGL ownership next round. */
		assert(crt_native_cart_bulk_destroy(ca) == 0 && a.registered == 0 &&
		       atomic_load(&h.refs) == 1);
		assert(crt_native_cart_bulk_create(&ops, &a, sa, 2, &ca) == 0);
	}
	assert(crt_native_cart_bulk_destroy(ca) == 0 && crt_native_cart_bulk_destroy(cb) == 0);
	assert(crt_native_bulk_session_destroy(sa) == 0 &&
	       crt_native_bulk_session_destroy(sb) == 0);
	puts("CaRT bulk binding: cached registrations, encoder rollback, import wrappers, retained "
	     "references and worker release passed");
	return 0;
}
