/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* Tiered FIFO Wait Queue (TFIFO)
 *
 * A synchronization primitive consisting of multiple vertically stacked FIFO wait queues
 * organized into distinct precedence layers. It preserves strict fairness within each individual
 * tier while enforcing a bottom-to-top directional wakeup order, ensuring lower-layer
 * dependencies or priority bands are fully drained before higher layers are signaled.
 */

/* TFIFO workflow:
 *
 * tq = tfifo_create(tiers);	// Create TFIFO queue
 *
 * each_job(tq, tier) {
 * 	tfifo_enter(tq, tier);		 // Enter TFIFO queue
 *
 * 	while (resource isn't available) // Wait for resource available
 *		tfifo_wait(tq, tier);
 *
 *	do_the_job();			 // Do the job
 *
 *	tfifo_exit(tq, tier);		 // Exit from TFIFO queue
 * }
 *
 * tfifo_destroy(tq);		// Destroy TFIFO queue
 *
 * Resource holder must invoke tfifo_wakeup_next() after releasing the resource.
 */

#define D_LOGFAC DD_FAC(vos)
#include <abt.h>
#include <daos/common.h>
#include "tfifo.h"

struct fifo_wait_queue {
	ABT_mutex fwq_mutex;
	ABT_cond  fwq_cond_first;
	ABT_cond  fwq_cond_fifo;
	uint32_t  fwq_waiters;
	uint32_t  fwq_acquired : 1;
};

struct tfifo_queue {
	uint32_t               tq_tiers;
	uint32_t               tq_waiters;
	struct fifo_wait_queue tq_wait_queue[0];
};

static void
tfifo_fini(struct tfifo_queue *tq, unsigned int tiers)
{
	struct fifo_wait_queue *wq;
	int                     i;

	D_ASSERT(tq->tq_waiters == 0);
	for (i = 0; i < tiers; i++) {
		wq = &tq->tq_wait_queue[i];

		D_ASSERT(wq->fwq_waiters == 0);
		D_ASSERT(!wq->fwq_acquired);
		ABT_mutex_free(&wq->fwq_mutex);
		ABT_cond_free(&wq->fwq_cond_first);
		ABT_cond_free(&wq->fwq_cond_fifo);
	}
}

void
tfifo_destroy(struct tfifo_queue *tq)
{
	tfifo_fini(tq, tq->tq_tiers);
	D_FREE(tq);
}

static int
tfifo_init(struct tfifo_queue *tq, unsigned int tiers)
{
	struct fifo_wait_queue *wq;
	int                     i, rc = 0;

	tq->tq_tiers = tiers;
	for (i = 0; i < tiers; i++) {
		wq = &tq->tq_wait_queue[i];

		rc = ABT_mutex_create(&wq->fwq_mutex);
		if (rc != ABT_SUCCESS)
			break;

		rc = ABT_cond_create(&wq->fwq_cond_first);
		if (rc != ABT_SUCCESS) {
			ABT_mutex_free(&wq->fwq_mutex);
			break;
		}

		rc = ABT_cond_create(&wq->fwq_cond_fifo);
		if (rc != ABT_SUCCESS) {
			ABT_cond_free(&wq->fwq_cond_first);
			ABT_mutex_free(&wq->fwq_mutex);
			break;
		}
	}

	if (rc) {
		tfifo_fini(tq, i);
		return -DER_NOMEM;
	}
	return rc;
}

#define TFIFO_MAX_TIERS 5

struct tfifo_queue *
tfifo_create(unsigned int tiers)
{
	struct tfifo_queue *tq;

	if (tiers == 0 || tiers > TFIFO_MAX_TIERS) {
		D_ERROR("%u tiers isn't supported. (max: %u)\n", tiers, TFIFO_MAX_TIERS);
		return NULL;
	}

	D_ALLOC(tq, offsetof(struct tfifo_queue, tq_wait_queue[tiers]));
	if (tq == NULL)
		return NULL;

	if (tfifo_init(tq, tiers)) {
		D_FREE(tq);
		return NULL;
	}

	return tq;
}

void
tfifo_wakeup_next(struct tfifo_queue *tq)
{
	struct fifo_wait_queue *wq;
	int                     i;

	if (tq->tq_waiters == 0)
		return;

	for (i = 0; i < tq->tq_tiers; i++) {
		wq = &tq->tq_wait_queue[i];

		if (wq->fwq_waiters > 0) {
			ABT_mutex_lock(wq->fwq_mutex);
			if (wq->fwq_acquired)
				ABT_cond_broadcast(wq->fwq_cond_first);
			else
				ABT_cond_signal(wq->fwq_cond_fifo);
			ABT_mutex_unlock(wq->fwq_mutex);
			break;
		}
	}
}

static inline struct fifo_wait_queue *
tfifo2queue(struct tfifo_queue *tq, unsigned int tier)
{
	D_ASSERT(tier < tq->tq_tiers);
	return &tq->tq_wait_queue[tier];
}

void
tfifo_enter(struct tfifo_queue *tq, unsigned int tier)
{
	struct fifo_wait_queue *wq = tfifo2queue(tq, tier);

	wq->fwq_waiters++;
	tq->tq_waiters++;
	if (wq->fwq_waiters > 1) {
		ABT_mutex_lock(wq->fwq_mutex);
		ABT_cond_wait(wq->fwq_cond_fifo, wq->fwq_mutex);
		ABT_mutex_unlock(wq->fwq_mutex);
	}
	wq->fwq_acquired = 1;
}

void
tfifo_exit(struct tfifo_queue *tq, unsigned int tier)
{
	struct fifo_wait_queue *wq = tfifo2queue(tq, tier);

	D_ASSERT(wq->fwq_waiters > 0);
	wq->fwq_waiters--;
	D_ASSERT(tq->tq_waiters > 0);
	tq->tq_waiters--;

	D_ASSERT(wq->fwq_acquired);
	wq->fwq_acquired = 0;

	tfifo_wakeup_next(tq);
}

void
tfifo_wait(struct tfifo_queue *tq, unsigned int tier)
{
	struct fifo_wait_queue *wq = tfifo2queue(tq, tier);

	D_ASSERT(wq->fwq_waiters > 0);
	D_ASSERT(wq->fwq_acquired);
	ABT_mutex_lock(wq->fwq_mutex);
	ABT_cond_wait(wq->fwq_cond_first, wq->fwq_mutex);
	ABT_mutex_unlock(wq->fwq_mutex);
}
