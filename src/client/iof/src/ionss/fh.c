/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "iof_common.h"
#include "ionss.h"
#include "log.h"

int ios_fh_alloc(struct ios_projection *projection,
		 struct ionss_file_handle **fhp)
{
	struct ios_base *base = projection->base;
	struct ionss_file_handle *fh;
	int rc;

	*fhp = NULL;

	fh = iof_pool_acquire(projection->fh_pool);
	if (!fh)
		return -DER_NOMEM;

	D_RWLOCK_WRLOCK(&base->gah_rwlock);

	rc = ios_gah_allocate(base->gs, &fh->gah, fh);
	if (rc) {
		IOF_LOG_ERROR("Failed to acquire GAH %d", rc);
		iof_pool_release(projection->fh_pool, fh);
		D_RWLOCK_UNLOCK(&base->gah_rwlock);
		return -DER_NOMEM;
	}

	IOF_TRACE_UP(fh, projection, "file_handle");

	*fhp = fh;

	D_RWLOCK_UNLOCK(&base->gah_rwlock);

	IOF_TRACE_INFO(fh, GAH_PRINT_FULL_STR, GAH_PRINT_FULL_VAL(fh->gah));

	return 0;
}

void ios_fh_decref(struct ionss_file_handle *fh, int count)
{
	struct ios_projection *projection = fh->projection;
	struct ios_base *base = base = projection->base;
	uint oldref;
	int rc;

	D_RWLOCK_WRLOCK(&base->gah_rwlock);

	oldref = atomic_fetch_sub(&fh->ref, count);

	D_ASSERTF(oldref != 0, "Unexpected fh refcount: %d\n", oldref);

	IOF_TRACE_DEBUG(fh, GAH_PRINT_STR " decref %d to %d",
			GAH_PRINT_VAL(fh->gah), count, oldref - count);

	if (oldref != count)
		D_GOTO(out, 0);

	IOF_TRACE_DEBUG(fh, "Closing %d", fh->fd);

	rc = close(fh->fd);
	if (rc != 0)
		IOF_TRACE_ERROR(fh, "Failed to close file %d", fh->fd);

	rc = ios_gah_deallocate(base->gs, &fh->gah);
	if (rc)
		IOF_TRACE_ERROR(fh, "Failed to deallocate GAH %d", rc);

	iof_pool_release(projection->fh_pool, fh);

out:
	D_RWLOCK_UNLOCK(&base->gah_rwlock);
}

struct ionss_file_handle *
ios_fh_find(struct ios_base *base, struct ios_gah *gah)
{
	struct ionss_file_handle *fh = NULL;
	uint oldref;
	int rc;

	D_RWLOCK_RDLOCK(&base->gah_rwlock);

	rc = ios_gah_get_info(base->gs, gah, (void **)&fh);
	if (rc || !fh) {
		IOF_TRACE_ERROR(&base,
				"Failed to load fh from " GAH_PRINT_FULL_STR " %d -%s",
				GAH_PRINT_FULL_VAL(*gah), rc, d_errstr(rc));
		D_GOTO(out, fh = NULL);
	}

	oldref = atomic_fetch_add(&fh->ref, 1);

	IOF_TRACE_DEBUG(fh, GAH_PRINT_STR " addref to %d",
			GAH_PRINT_VAL(fh->gah), oldref + 1);

out:
	D_RWLOCK_UNLOCK(&base->gah_rwlock);

	return fh;
}

struct ionss_dir_handle *
ios_dirh_find(struct ios_base *base, struct ios_gah *gah)
{
	struct ionss_dir_handle *dirh = NULL;
	int rc;

	D_RWLOCK_RDLOCK(&base->gah_rwlock);

	rc = ios_gah_get_info(base->gs, gah, (void **)&dirh);
	if (rc || !dirh) {
		IOF_TRACE_ERROR(&base,
				"Failed to load dirh from " GAH_PRINT_FULL_STR " %d -%s",
				GAH_PRINT_FULL_VAL(*gah), rc, d_errstr(rc));
		D_GOTO(out, dirh = NULL);
	}

	IOF_TRACE_DEBUG(dirh, GAH_PRINT_STR, GAH_PRINT_VAL(*gah));

out:
	D_RWLOCK_UNLOCK(&base->gah_rwlock);

	return dirh;
}
