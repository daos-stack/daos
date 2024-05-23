/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2017-2021, Intel Corporation */

/*
 * stats.c -- implementation of statistics
 */

#include <errno.h>

#include "dav_internal.h"
#include "obj.h"
#include "stats.h"

/*
 * stats_new -- allocates and initializes statistics instance
 */
struct stats *
stats_new(dav_obj_t *pop)
{
	struct stats *s;

	D_ALLOC_PTR_NZ(s);
	if (s == NULL) {
		D_CRIT("Malloc\n");
		return NULL;
	}

	s->persistent = &pop->do_phdr->dp_stats_persistent;
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(s->persistent, sizeof(*s->persistent));
	D_ALLOC_PTR(s->transient);
	if (s->transient == NULL)
		goto error_transient_alloc;

	return s;

error_transient_alloc:
	D_FREE(s);
	return NULL;
}

/*
 * stats_delete -- deletes statistics instance
 */
void
stats_delete(dav_obj_t *pop, struct stats *s)
{
	D_FREE(s->transient);
	D_FREE(s);
}

/*
 * stats_persist -- save the persistent statistics to wal
 */
void
stats_persist(dav_obj_t *pop, struct stats *s)
{
	if (s->transient->heap_prev_pval !=
	    s->persistent->heap_curr_allocated) {
		mo_wal_persist(&pop->p_ops, s->persistent,
			       sizeof(struct stats_persistent));
		s->transient->heap_prev_pval =
		    s->persistent->heap_curr_allocated;
	}
}

int
dav_get_heap_stats(dav_obj_t *pop, struct dav_heap_stats *st)
{
	if ((pop == NULL) || (st == NULL)) {
		errno = EINVAL;
		return -1;
	}

	st->curr_allocated = pop->do_stats->persistent->heap_curr_allocated;
	st->run_allocated = pop->do_stats->transient->heap_run_allocated;
	st->run_active = pop->do_stats->transient->heap_run_active;
	return 0;
}
