/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos_transport. It implements the dtp context related
 * APIs.
 */
#include <abt.h>
#include <dtp_internal.h>

int
dtp_context_create(void *arg, dtp_context_t *dtp_ctx)
{
	struct dtp_context	*ctx = NULL;
	int			rc = 0;

	if (dtp_ctx == NULL) {
		D_ERROR("invalid parameter of NULL dtp_ctx.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);

	rc = dtp_hg_ctx_init(&ctx->dc_hg_ctx, dtp_gdata.dg_ctx_num);
	if (rc != 0) {
		D_ERROR("dtp_hg_ctx_init failed rc: %d.\n", rc);
		D_FREE_PTR(ctx);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc);
	}

	DAOS_INIT_LIST_HEAD(&ctx->dc_link);
	ctx->dc_idx = dtp_gdata.dg_ctx_num;
	daos_list_add_tail(&ctx->dc_link, &dtp_gdata.dg_ctx_list);
	dtp_gdata.dg_ctx_num++;

	ctx->dc_pool = arg;
	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	*dtp_ctx = (dtp_context_t)ctx;

out:
	return rc;
}

int
dtp_context_destroy(dtp_context_t dtp_ctx, int force)
{
	struct dtp_context	*ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* TODO: check force */

	ctx = (struct dtp_context *)dtp_ctx;
	rc = dtp_hg_ctx_fini(&ctx->dc_hg_ctx);
	if (rc == 0) {
		pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
		dtp_gdata.dg_ctx_num--;
		daos_list_del_init(&ctx->dc_link);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_FREE_PTR(ctx);
	} else {
		D_ERROR("dtp_hg_ctx_fini failed rc: %d.\n", rc);
	}

out:
	return rc;
}

int
dtp_context_idx(dtp_context_t dtp_ctx, int *ctx_idx)
{
	struct dtp_context	*ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || ctx_idx == NULL) {
		D_ERROR("invalid parameter, dtp_ctx: %p, ctx_idx: %p.\n",
			dtp_ctx, ctx_idx);
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx = (struct dtp_context *)dtp_ctx;
	*ctx_idx = ctx->dc_idx;

out:
	return rc;
}

int
dtp_context_num(int *ctx_num)
{
	if (ctx_num == NULL) {
		D_ERROR("invalid parameter of NULL ctx_num.\n");
		return -DER_INVAL;
	}

	*ctx_num = dtp_gdata.dg_ctx_num;
	return 0;
}

bool
dtp_context_empty(int locked)
{
	bool rc = false;

	if (locked == 0)
		pthread_rwlock_rdlock(&dtp_gdata.dg_rwlock);

	rc = daos_list_empty(&dtp_gdata.dg_ctx_list);

	if (locked == 0)
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	return rc;
}

int
dtp_progress(dtp_context_t dtp_ctx, int64_t timeout,
	     dtp_progress_cond_cb_t cond_cb, void *arg)
{
	struct dtp_context	*ctx;
	struct timeval		 tv;
	int64_t			 hg_timeout;
	uint64_t		 now;
	uint64_t		 end = 0;
	int			 rc = 0;

	/** validate input parameters */
	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/**
	 * Invoke the callback once first, in case the condition is met before
	 * calling progress
	 */
	if (cond_cb) {
		/** execute callback */
		rc = cond_cb(arg);
		if (rc > 0)
			/** exit as per the callback request */
			D_GOTO(out, rc = 0);
		if (rc < 0)
			/**
			 * something wrong happened during the callback
			 * execution
			 */
			D_GOTO(out, rc);
	}

	ctx = (struct dtp_context *)dtp_ctx;
	if (timeout == 0 || cond_cb == NULL) {
		/** fast path */
		rc = dtp_hg_progress(&ctx->dc_hg_ctx, timeout);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_hg_progress failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}

		if (cond_cb) {
			int ret;

			/**
			 * Don't clobber rc which might be set to
			 * -DER_TIMEDOUT
			 */
			ret = cond_cb(arg);
			/** be careful with return code */
			if (ret > 0)
				D_GOTO(out, rc = 0);
			if (ret < 0)
				D_GOTO(out, rc = ret);
		}

		D_GOTO(out, rc);
	}

	/** Progress with callback and non-null timeout */
	if (timeout <= 0) {
		D_ASSERT(timeout < 0);
		/**
		 * For infinite timeout, use a mercury timeout of 1s to avoid
		 * being blocked indefinitely if another thread has called
		 * dtp_hg_progress() behind our back
		 */
		hg_timeout = 0;
	} else  {
		rc = gettimeofday(&tv, NULL);
		if (rc != 0)
			D_GOTO(out, rc);
		now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		end = now + timeout;
		/** similiarly, probe more frequently if timeout is large */
		if (timeout > 1000 * 1000)
			hg_timeout = 1000 * 1000;
		else
			hg_timeout = timeout;
	}

	while (true) {
		rc = dtp_hg_progress(&ctx->dc_hg_ctx, hg_timeout);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_hg_progress failed with %d\n", rc);
			D_GOTO(out, rc = 0);
		}

		/** execute callback */
		rc = cond_cb(arg);
		if (rc > 0)
			D_GOTO(out, rc = 0);
		if (rc < 0)
			D_GOTO(out, rc);

		/** check for timeout, if not infinite */
		if (timeout > 0) {
			rc = gettimeofday(&tv, NULL);
			if (rc != 0)
				D_GOTO(out, rc);
			now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
			if (now >= end) {
				rc = -DER_TIMEDOUT;
				break;
			}
			if (end - now > 1000 * 1000)
				hg_timeout = 1000 * 1000;
			else
				hg_timeout = end - now;
		}
	};
out:
	return rc;
}
