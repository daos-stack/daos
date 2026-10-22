/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __TFIFO__
#define __TFIFO__

/* Opaque TFIFO queue */
struct tfifo_queue;

/* Destroy a TFIFO queue */
void
tfifo_destroy(struct tfifo_queue *tq);

/* Create a TFIFO queue */
struct tfifo_queue *
tfifo_create(unsigned int tiers);

/* Wakeup next waiter in TFIFO queue */
void
tfifo_wakeup_next(struct tfifo_queue *tq);

/* Enter TFIFO queue */
void
tfifo_enter(struct tfifo_queue *tq, unsigned int tier);

/* Exit from TFIFO queue */
void
tfifo_exit(struct tfifo_queue *tq, unsigned int tier);

/* First waiter blocks until the shared resource becomes available */
void
tfifo_wait(struct tfifo_queue *tq, unsigned int tier);

#endif /* __TFIFO__ */
