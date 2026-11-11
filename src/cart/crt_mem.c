/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements registered memory regions: a large buffer, typically
 * a GPU pool, is registered once per context, so that bulk handles on blocks inside it are
 * served by the transport MR cache.
 */
#define D_LOGFAC DD_FAC(bulk)

#include "crt_internal.h"

/* All live handles, so that a context can drop its registrations before it is destroyed. */
static D_LIST_HEAD(crt_mem_list);
static pthread_mutex_t crt_mem_lock = PTHREAD_MUTEX_INITIALIZER;

static void
mem_handle_free(struct crt_mem_handle *mh)
{
	uint32_t i;

	for (i = 0; i < mh->ctx_nr; i++) {
		if (mh->ctx_bulks[i].bulk != CRT_BULK_NULL)
			crt_bulk_free(mh->ctx_bulks[i].bulk);
	}
	D_FREE(mh->ctx_bulks);
	D_RWLOCK_DESTROY(&mh->lock);
	D_FREE(mh);
}

int
crt_mem_register(void *buf, size_t len, const d_mem_dev_t *mem_dev, crt_mem_handle_t *mh)
{
	struct crt_mem_handle *h;
	int                    rc;

	if (buf == NULL || len == 0 || mh == NULL || len > UINTPTR_MAX - (uintptr_t)buf) {
		D_ERROR("invalid parameter, buf: %p, len: %zu, mh: %p\n", buf, len, mh);
		return -DER_INVAL;
	}

	if (crt_initialized() && crt_is_service()) {
		/* engines have secondary contexts, with MR cache settings of their own */
		D_ERROR("registered memory regions are for clients only\n");
		return -DER_NOSYS;
	}

	if (mem_dev != NULL && mem_dev->md_type != D_MEM_TYPE_HOST) {
		if (mem_dev->md_type < D_MEM_TYPE_HOST || mem_dev->md_type > D_MEM_TYPE_ZE) {
			D_ERROR("invalid memory type %d\n", mem_dev->md_type);
			return -DER_INVAL;
		}
		rc = crt_mem_device_check();
		if (rc == -DER_NOTSUPPORTED)
			D_ERROR("device memory needs memory device support, see D_MEM_DEVICE\n");
		if (rc != 0)
			return rc;
	}

	rc = crt_mr_cache_check();
	if (rc == -DER_NOTSUPPORTED) {
		D_ERROR("the transport MR cache is disabled (see CRT_MRC_ENABLE), a registered "
			"region would not be reused\n");
		/* not -DER_NOTSUPPORTED: bulk handles without a region still work */
		rc = -DER_NOSYS;
	}
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(h);
	if (h == NULL)
		return -DER_NOMEM;

	rc = D_RWLOCK_INIT(&h->lock, NULL);
	if (rc != 0) {
		D_FREE(h);
		return rc;
	}
	h->buf = buf;
	h->len = len;
	if (mem_dev != NULL)
		h->dev = *mem_dev;
	atomic_init(&h->refs, 1);

	D_MUTEX_LOCK(&crt_mem_lock);
	d_list_add_tail(&h->link, &crt_mem_list);
	D_MUTEX_UNLOCK(&crt_mem_lock);

	*mh = h;
	return 0;
}

int
crt_mem_deregister(crt_mem_handle_t mh)
{
	uint32_t expected = 1;
	bool     busy;

	if (mh == NULL)
		return -DER_INVAL;

	/* Only the registration reference may be left. */
	D_MUTEX_LOCK(&crt_mem_lock);
	busy = !atomic_compare_exchange_strong(&mh->refs, &expected, 0);
	if (!busy)
		d_list_del_init(&mh->link);
	D_MUTEX_UNLOCK(&crt_mem_lock);

	/* the handle is not ours while it is busy, do not look at it again */
	if (busy) {
		D_ERROR("handle %p is used by %u in-flight I/O\n", mh, expected - 1);
		return -DER_BUSY;
	}

	mem_handle_free(mh);
	return 0;
}

void
crt_mem_addref(crt_mem_handle_t mh)
{
	/* the caller must hold the registration: no reference after crt_mem_deregister() */
	D_ASSERT(atomic_load(&mh->refs) > 0);
	atomic_fetch_add(&mh->refs, 1);
}

void
crt_mem_decref(crt_mem_handle_t mh)
{
	uint32_t old = atomic_fetch_sub(&mh->refs, 1);

	/* the registration reference is only dropped by crt_mem_deregister() */
	D_ASSERTF(old > 1, "handle %p: unbalanced decref, refs %u\n", mh, old);
}

int
crt_mem_query(crt_mem_handle_t mh, void **buf, size_t *len, d_mem_dev_t *mem_dev)
{
	if (mh == NULL)
		return -DER_INVAL;

	if (buf != NULL)
		*buf = mh->buf;
	if (len != NULL)
		*len = mh->len;
	if (mem_dev != NULL)
		*mem_dev = mh->dev;
	return 0;
}

static struct crt_mem_ctx_bulk *
mem_handle_ctx_lookup(struct crt_mem_handle *mh, crt_context_t ctx)
{
	uint32_t i;

	for (i = 0; i < mh->ctx_nr; i++) {
		if (mh->ctx_bulks[i].ctx == ctx)
			return &mh->ctx_bulks[i];
	}
	return NULL;
}

int
crt_mem_bind(crt_mem_handle_t mh, crt_context_t crt_ctx)
{
	struct crt_mem_ctx_bulk *mcb;
	struct crt_mem_ctx_bulk *bulks;
	crt_bulk_t               bulk = CRT_BULK_NULL;
	d_iov_t                  iov;
	d_sg_list_t              sgl;
	int                      rc = 0;

	if (mh == NULL || crt_ctx == NULL)
		return -DER_INVAL;

	D_RWLOCK_RDLOCK(&mh->lock);
	mcb = mem_handle_ctx_lookup(mh, crt_ctx);
	if (mcb != NULL)
		rc = mcb->rc;
	D_RWLOCK_UNLOCK(&mh->lock);
	if (mcb != NULL)
		return rc;

	/*
	 * Register outside the lock, it can take milliseconds for a large region. The context
	 * must not be destroyed meanwhile.
	 */
	d_iov_set(&iov, mh->buf, mh->len);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	rc            = crt_bulk_create_with_mdev(crt_ctx, &sgl, CRT_BULK_RW, &mh->dev, &bulk);
	if (rc == 0 && ((struct crt_bulk *)bulk)->deferred) {
		/*
		 * Over the bulk quota: the handle is deferred and nothing is registered. Only
		 * host memory can get here, a device memory handle is never deferred.
		 */
		D_ASSERTF(mh->dev.md_type == D_MEM_TYPE_HOST,
			  "deferred bulk handle for device memory, type %d\n", mh->dev.md_type);
		crt_bulk_free(bulk);
		bulk = CRT_BULK_NULL;
		rc   = -DER_AGAIN;
	}
	if (rc == -DER_NOTSUPPORTED)
		/* no memory device support, a bulk handle on a block would fail as well */
		return rc;
	if (rc == -DER_AGAIN) {
		/* transient and not remembered, a later call tries again */
		D_DEBUG(DB_IO, "cannot register %p, len %zu on context %p yet: " DF_RC "\n",
			mh->buf, mh->len, crt_ctx, DP_RC(rc));
		return rc;
	}
	if (rc != 0) {
		/*
		 * Remember the failure so that later calls on this context do not retry a
		 * region-sized registration. This includes -DER_NOMEM: a region too large for the
		 * GPU BAR or the NIC is reported as out of memory by the transport.
		 */
		D_WARN("failed to register %p, len %zu on context %p, bulk handles inside it are "
		       "registered one by one: " DF_RC "\n",
		       mh->buf, mh->len, crt_ctx, DP_RC(rc));
		bulk = CRT_BULK_NULL;
	}

	D_RWLOCK_WRLOCK(&mh->lock);
	mcb = mem_handle_ctx_lookup(mh, crt_ctx);
	if (mcb != NULL)
		/* lost the race with another thread */
		D_GOTO(out, rc = mcb->rc);

	D_REALLOC_ARRAY(bulks, mh->ctx_bulks, mh->ctx_nr, mh->ctx_nr + 1);
	if (bulks == NULL)
		/* cannot keep the registration, and this is not remembered either */
		D_GOTO(out, rc = -DER_NOMEM);

	mh->ctx_bulks          = bulks;
	bulks[mh->ctx_nr].ctx  = crt_ctx;
	bulks[mh->ctx_nr].bulk = bulk;
	bulks[mh->ctx_nr].rc   = rc;
	mh->ctx_nr++;
	bulk = CRT_BULK_NULL;
out:
	D_RWLOCK_UNLOCK(&mh->lock);
	if (bulk != CRT_BULK_NULL)
		crt_bulk_free(bulk);
	return rc;
}

/*
 * Called before a context is destroyed. Required, not only tidy: entries are keyed by the context
 * pointer, the held bulk belongs to the Mercury class being destroyed, and a new context may get
 * the same address and would find a stale entry. The handles stay valid and register again on
 * their next context.
 */
void
crt_mem_ctx_fini(crt_context_t ctx)
{
	struct crt_mem_handle *mh;
	uint32_t               i;

	D_MUTEX_LOCK(&crt_mem_lock);
	d_list_for_each_entry(mh, &crt_mem_list, link) {
		D_RWLOCK_WRLOCK(&mh->lock);
		for (i = 0; i < mh->ctx_nr; i++) {
			if (mh->ctx_bulks[i].ctx != ctx)
				continue;
			if (mh->ctx_bulks[i].bulk != CRT_BULK_NULL)
				crt_bulk_free(mh->ctx_bulks[i].bulk);
			mh->ctx_bulks[i] = mh->ctx_bulks[--mh->ctx_nr];
			break;
		}
		D_RWLOCK_UNLOCK(&mh->lock);
	}
	D_MUTEX_UNLOCK(&crt_mem_lock);
}
