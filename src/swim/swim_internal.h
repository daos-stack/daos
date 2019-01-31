/* Copyright (c) 2016 UChicago Argonne, LLC
 * Copyright (C) 2018 Intel Corporation
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
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
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
#ifndef __SWIM_INTERNAL_H__
#define __SWIM_INTERNAL_H__

#ifdef _USE_ABT_SYNC_
#include <abt.h>
#else
#include <pthread.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/queue.h>
#include <errno.h>
#include <time.h>

#include <cart/swim.h>
#include <gurt/common.h>

/* Use debug capability from CaRT */
#define SWIM_INFO(fmt, ...)	D_DEBUG(DLOG_INFO, fmt, ##__VA_ARGS__)
#define SWIM_ERROR(fmt, ...)	D_DEBUG(DLOG_ERR,  fmt, ##__VA_ARGS__)

#ifdef _USE_ABT_SYNC_
#define SWIM_MUTEX_T		ABT_mutex
#define SWIM_MUTEX_CREATE(x, y)	ABT_mutex_create(&(x))
#define SWIM_MUTEX_DESTROY(x)	ABT_mutex_destroy(&(x))
#define SWIM_MUTEX_LOCK(x)	ABT_mutex_lock(x)
#define SWIM_MUTEX_UNLOCK(x)	ABT_mutex_unlock(x)
#else  /* _USE_ABT_SYNC_ */
#define SWIM_MUTEX_T		pthread_mutex_t
#define SWIM_MUTEX_CREATE(x, y)	pthread_mutex_init(&(x), (y))
#define SWIM_MUTEX_DESTROY(x)	pthread_mutex_destroy(&(x))
#define SWIM_MUTEX_LOCK(x)	pthread_mutex_lock(&(x))
#define SWIM_MUTEX_UNLOCK(x)	pthread_mutex_unlock(&(x))
#endif /* _USE_ABT_SYNC_ */

#ifdef __cplusplus
extern "C" {
#endif

/** SWIM protocol parameter defaults */
#define SWIM_PROTOCOL_PERIOD_LEN 2000	/* milliseconds, should NOT be less
					 * than 3 * SWIM_PING_TIMEOUT
					 */
#define SWIM_SUSPECT_TIMEOUT	3 * SWIM_PROTOCOL_PERIOD_LEN
#define SWIM_PING_TIMEOUT	800	/* milliseconds */
#define SWIM_SUBGROUP_SIZE	2
#define SWIM_PIGGYBACK_ENTRIES	8	/**< count of piggybacked entries */
#define SWIM_PIGGYBACK_TX_COUNT	50	/**< count of transfers each entry
					 * until it be removed from the list of
					 * updates.
					 */

enum swim_context_state {
	SCS_BEGIN = 0,		/**< initial state when next target was already
				 * selected.
				 */
	SCS_DPINGED,		/**< the state after dping was sent and we are
				 * waiting for response.
				 */
	SCS_IPINGED,		/**< the state after ipings were sent and we are
				 * waiting for any response.
				 */
	SCS_TIMEDOUT,		/**< the state when no dping response was
				 * received and we should select iping targets.
				 */
	SCS_ACKED,		/**< the state when dping or iping response was
				 * successfully received
				 */
	SCS_DEAD,		/**< the state to select next target */
};

struct swim_item {
	TAILQ_ENTRY(swim_item)	 si_link;
	swim_id_t		 si_id;
	swim_id_t		 si_from;
	union {
		uint64_t	 si_deadline; /**< for sc_suspects/sc_ipings */
		uint64_t	 si_count;    /**< for sc_updates */
	} u;
};

/** internal swim context implementation */
struct swim_context {
	SWIM_MUTEX_T		 sc_mutex;	/**< mutex for modifying */

	void			*sc_data;	/**< private data */
	struct swim_ops		*sc_ops;

	TAILQ_HEAD(, swim_item)	 sc_subgroup;
	TAILQ_HEAD(, swim_item)	 sc_suspects;
	TAILQ_HEAD(, swim_item)	 sc_updates;
	TAILQ_HEAD(, swim_item)	 sc_ipings;

	enum swim_context_state	 sc_state;
	swim_id_t		 sc_target;
	swim_id_t		 sc_self;

	uint64_t		 sc_next_tick_time;
	uint64_t		 sc_dping_deadline;
	uint64_t		 sc_iping_deadline;

	uint64_t		 sc_piggyback_tx_max;
};

static inline int
swim_ctx_lock(struct swim_context *ctx)
{
	int rc;

	rc = SWIM_MUTEX_LOCK(ctx->sc_mutex);
	if (rc != 0)
		SWIM_ERROR("SWIM_MUTEX_LOCK() failed rc=%d\n", rc);

	return rc;
}

static inline int
swim_ctx_unlock(struct swim_context *ctx)
{
	int rc;

	rc = SWIM_MUTEX_UNLOCK(ctx->sc_mutex);
	if (rc != 0)
		SWIM_ERROR("SWIM_MUTEX_UNLOCK() failed rc=%d\n", rc);

	return rc;
}

static inline uint64_t
swim_now_ms(void)
{
	struct timespec now;
	int rc;

	rc = clock_gettime(CLOCK_MONOTONIC, &now);

	return rc ? 0 : now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static inline enum swim_context_state
swim_state_get(struct swim_context *ctx)
{
	return ctx->sc_state;
}

static inline void
swim_state_set(struct swim_context *ctx, enum swim_context_state state)
{
	if (ctx->sc_state != state)
		ctx->sc_state = state;
}

#ifdef __cplusplus
}
#endif

#endif /* __SWIM_INTERNAL_H__ */
