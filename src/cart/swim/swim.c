/*
 * Copyright (c) 2016 UChicago Argonne, LLC
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(swim)

#include "swim_internal.h"
#include <assert.h>

static const char *SWIM_STATUS_STR[] = {
	[SWIM_MEMBER_ALIVE]	= "ALIVE",
	[SWIM_MEMBER_SUSPECT]	= "SUSPECT",
	[SWIM_MEMBER_DEAD]	= "DEAD",
	[SWIM_MEMBER_INACTIVE]	= "INACTIVE",
};

static uint64_t swim_prot_period_len;
static uint64_t swim_suspect_timeout;
static uint64_t swim_ping_timeout;
static int      swim_subgroup_size;

static inline uint64_t
swim_prot_period_len_default(void)
{
	unsigned int val = SWIM_PROTOCOL_PERIOD_LEN;

	d_getenv_uint("SWIM_PROTOCOL_PERIOD_LEN", &val);
	return val;
}

static inline uint64_t
swim_suspect_timeout_default(void)
{
	unsigned int val = SWIM_SUSPECT_TIMEOUT;

	d_getenv_uint("SWIM_SUSPECT_TIMEOUT", &val);
	return val;
}

static inline uint64_t
swim_ping_timeout_default(void)
{
	unsigned int val = SWIM_PING_TIMEOUT;

	d_getenv_uint("SWIM_PING_TIMEOUT", &val);
	return val;
}

static inline int
swim_subgroup_size_default(void)
{
	unsigned int val = SWIM_SUBGROUP_SIZE;

	d_getenv_uint("SWIM_SUBGROUP_SIZE", &val);
	return val;
}

void
swim_period_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_prot_period_len set as %lu\n", val);
	swim_prot_period_len = val;
}

uint64_t
swim_period_get(void)
{
	return swim_prot_period_len;
}

void
swim_suspect_timeout_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_suspect_timeout set as %lu\n", val);
	swim_suspect_timeout = val;
}

uint64_t
swim_suspect_timeout_get(void)
{
	return swim_suspect_timeout;
}

void
swim_ping_timeout_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_ping_timeout set as %lu\n", val);
	swim_ping_timeout = val;
}

uint64_t
swim_ping_timeout_get(void)
{
	return swim_ping_timeout;
}

static inline void
swim_dump_updates(swim_id_t self_id, swim_id_t from_id, swim_id_t to_id,
		  struct swim_member_update *upds, size_t nupds)
{
	FILE	*fp;
	char	*msg;
	size_t	 msg_size, i;
	int	 rc;

	if (!D_LOG_ENABLED(DLOG_DBG))
		return;

	fp = open_memstream(&msg, &msg_size);
	if (fp != NULL) {
		for (i = 0; i < nupds; i++) {
			rc = fprintf(fp, " {%lu %c %lu}", upds[i].smu_id,
				     SWIM_STATUS_CHARS[upds[i].smu_state.sms_status],
				     upds[i].smu_state.sms_incarnation);
			if (rc < 0)
				break;
		}

		fclose(fp);
		/* msg and msg_size will be set after fclose(fp) only */
		if (msg_size > 0)
			SWIM_DEBUG("%lu %s %lu:%s\n", self_id, self_id == from_id ? "=>" : "<=",
				   self_id == from_id ? to_id : from_id, msg);
		free(msg); /* allocated by open_memstream() */
	}
}

int
swim_updates_prepare(struct swim_context *ctx, swim_id_t id, swim_id_t to,
		     struct swim_member_update **pupds, size_t *pnupds)
{
	struct swim_member_update	*upds;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	size_t				 nupds, n = 0;
	int				 rc = 0;

	if (id == SWIM_ID_INVALID || to == SWIM_ID_INVALID) {
		SWIM_ERROR("member id is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	nupds = SWIM_PIGGYBACK_ENTRIES + 1 /* id */;
	if (id != self_id)
		nupds++; /* self_id */
	if (id != to)
		nupds++; /* to */

	D_ALLOC_ARRAY(upds, nupds);
	if (upds == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	swim_ctx_lock(ctx);

	rc = ctx->sc_ops->get_member_state(ctx, id, &upds[n].smu_state);
	if (rc) {
		if (rc == -DER_NONEXIST)
			SWIM_DEBUG("%lu: not bootstrapped yet with %lu\n", self_id, id);
		else
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out_unlock, rc);
	}
	upds[n++].smu_id = id;

	if (id != self_id) {
		/* update self status on target */
		rc = ctx->sc_ops->get_member_state(ctx, self_id,
						   &upds[n].smu_state);
		if (rc) {
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", self_id, DP_RC(rc));
			D_GOTO(out_unlock, rc);
		}
		upds[n++].smu_id = self_id;
	}

	if (id != to) {
		rc = ctx->sc_ops->get_member_state(ctx, to, &upds[n].smu_state);
		if (rc) {
			if (rc == -DER_NONEXIST)
				SWIM_DEBUG("%lu: not bootstrapped yet with %lu\n", self_id, to);
			else
				SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", to, DP_RC(rc));
			D_GOTO(out_unlock, rc);
		}
		upds[n++].smu_id = to;
	}

	item = TAILQ_FIRST(&ctx->sc_updates);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);

		/* delete entries that are too many */
		if (n >= nupds) {
			TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
			D_FREE(item);
			item = next;
			continue;
		}

		/* update with recent updates */
		if (item->si_id != id &&
		    item->si_id != self_id &&
		    item->si_id != to) {
			rc = ctx->sc_ops->get_member_state(ctx, item->si_id,
							   &upds[n].smu_state);
			if (rc) {
				if (rc == -DER_NONEXIST) {
					/* this member was removed already */
					TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
					D_FREE(item);
					item = next;
					continue;
				}
				SWIM_ERROR("get_member_state(%lu): "DF_RC"\n",
					   item->si_id, DP_RC(rc));
				D_GOTO(out_unlock, rc);
			}
			upds[n++].smu_id = item->si_id;
		}

		if (++item->u.si_count > ctx->sc_piggyback_tx_max) {
			TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
			D_FREE(item);
		}

		item = next;
	}
	rc = 0;

out_unlock:
	swim_ctx_unlock(ctx);

	if (rc) {
		D_FREE(upds);
	} else {
		swim_dump_updates(self_id, self_id, to, upds, n);
		*pupds  = upds;
		*pnupds = n;
	}
out:
	return rc;
}

int
swim_updates_send(struct swim_context *ctx, swim_id_t id, swim_id_t to)
{
	struct swim_member_update	*upds;
	size_t				 nupds;
	int				 rc;

	rc = swim_updates_prepare(ctx, id, to, &upds, &nupds);
	if (rc)
		goto out;

	rc = ctx->sc_ops->send_request(ctx, id, to, upds, nupds);
	if (rc)
		D_FREE(upds);
out:
	return rc;
}

static int
swim_updates_notify(struct swim_context *ctx, swim_id_t from, swim_id_t id,
		    struct swim_member_state *id_state, uint64_t count)
{
	struct swim_item *item;

	/* determine if this member already have an update */
	TAILQ_FOREACH(item, &ctx->sc_updates, si_link) {
		if (item->si_id == id) {
			item->si_from = from;
			item->u.si_count = count;
			D_GOTO(update, 0);
		}
	}

	/* add this update to recent update list so it will be
	 * piggybacked on future protocol messages
	 */
	D_ALLOC_PTR(item);
	if (item != NULL) {
		item->si_id   = id;
		item->si_from = from;
		item->u.si_count = count;
		TAILQ_INSERT_HEAD(&ctx->sc_updates, item, si_link);
	}
update:
	return ctx->sc_ops->set_member_state(ctx, id, id_state);
}

static int
swim_member_alive(struct swim_context *ctx, swim_id_t from, swim_id_t id, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	uint64_t			 count = 0;
	int				 rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		swim_id_t self_id = swim_self_get(ctx);

		if (rc == -DER_NONEXIST)
			SWIM_DEBUG("%lu: not bootstrapped yet with %lu\n", self_id, id);
		else
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Do not widely spread the information about bootstrap complete */
	if (id_state.sms_status == SWIM_MEMBER_INACTIVE) {
		count = ctx->sc_piggyback_tx_max;
		D_GOTO(update, rc = 0);
	}

	if (nr > id_state.sms_incarnation)
		D_GOTO(update, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||
	    id_state.sms_status == SWIM_MEMBER_ALIVE ||
	    id_state.sms_incarnation >= nr)
		D_GOTO(out, rc = -DER_ALREADY);

update:
	/* if member is suspected, remove from suspect list */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (item->si_id == id) {
			/* remove this member from suspect list */
			TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
			D_FREE(item);
			break;
		}
	}

	SWIM_INFO("%lu: member %lu %lu is ALIVE from %lu\n", ctx->sc_self, id, nr, from);
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_ALIVE;
	rc = swim_updates_notify(ctx, from, id, &id_state, count);
out:
	return rc;
}

static int
swim_member_dead(struct swim_context *ctx, swim_id_t from, swim_id_t id, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	int				 rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (id_state.sms_status == SWIM_MEMBER_INACTIVE) {
		if (ctx->sc_glitch)
			D_GOTO(update, rc = 0);
		else
			D_GOTO(out, rc = 0);
	}

	if (nr > id_state.sms_incarnation)
		D_GOTO(update, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||
	    id_state.sms_incarnation > nr)
		D_GOTO(out, rc = -DER_ALREADY);

update:
	/* if member is suspected, remove it from suspect list */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (item->si_id == id) {
			/* remove this member from suspect list */
			TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
			D_FREE(item);
			break;
		}
	}

	SWIM_ERROR("%lu: member %lu %lu is DEAD from %lu%s\n", ctx->sc_self, id, nr, from,
		   from == ctx->sc_self ? " (self)" : "");
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_DEAD;
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);
out:
	return rc;
}

static int
swim_member_suspect(struct swim_context *ctx, swim_id_t from, swim_id_t id, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	int				 rc;

	/* if there is no suspicion timeout, just kill the member */
	if (swim_suspect_timeout_get() == 0)
		return swim_member_dead(ctx, from, id, nr);

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (id_state.sms_status == SWIM_MEMBER_INACTIVE)
		D_GOTO(out, rc = 0);

	if (nr > id_state.sms_incarnation)
		D_GOTO(search, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||
	    id_state.sms_status == SWIM_MEMBER_SUSPECT ||
	    id_state.sms_incarnation > nr)
		D_GOTO(out, rc = -DER_ALREADY);

search:
	/* determine if this member is already suspected */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (item->si_id == id) {
			/*
			 * if the new suspicion is of a newer incarnation,
			 * reset the existing one
			 */
			if (nr > id_state.sms_incarnation) {
				item->si_from = from;
				item->u.si_deadline = swim_now_ms() + swim_suspect_timeout_get();
			}
			goto update;
		}
	}

	/* add to end of suspect list */
	D_ALLOC_PTR(item);
	if (item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	item->si_id   = id;
	item->si_from = from;
	item->u.si_deadline = swim_now_ms() + swim_suspect_timeout_get();
	TAILQ_INSERT_TAIL(&ctx->sc_suspects, item, si_link);

update:
	SWIM_INFO("%lu: member %lu %lu is SUSPECT from %lu%s\n", ctx->sc_self, id, nr, from,
		  from == ctx->sc_self ? " (self)" : "");
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_SUSPECT;
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);
out:
	return rc;
}

static int
swim_member_update_suspected(struct swim_context *ctx, uint64_t now, uint64_t net_glitch_delay)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_member_state	 id_state;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	swim_id_t			 from_id, id;
	int				 rc = 0;

	TAILQ_INIT(&targets);

	/* update status of suspected members */
	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_suspects);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		item->u.si_deadline += net_glitch_delay;
		if (now > item->u.si_deadline) {
			rc = ctx->sc_ops->get_member_state(ctx, item->si_id, &id_state);
			if (rc || (id_state.sms_status != SWIM_MEMBER_SUSPECT)) {
				/* this member was removed or updated already */
				TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
				D_FREE(item);
				D_GOTO(next_item, rc = 0);
			}

			SWIM_DEBUG("%lu: suspect timeout %lu\n", self_id, item->si_id);
			if (item->si_from != self_id) {
				/* let's try to confirm from gossip origin */
				id      = item->si_id;
				from_id = item->si_from;

				item->si_from = self_id;
				item->u.si_deadline += swim_ping_timeout_get();

				D_ALLOC_PTR(item);
				if (item == NULL)
					D_GOTO(next_item, rc = -DER_NOMEM);
				item->si_id   = id;
				item->si_from = from_id;
				TAILQ_INSERT_TAIL(&targets, item, si_link);
			} else {
				TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
				/* if this member has exceeded
				 * its allowable suspicion timeout,
				 * we mark it as dead
				 */
				swim_member_dead(ctx, item->si_from, item->si_id,
						 id_state.sms_incarnation);
				D_FREE(item);
			}
		} else {
			if (item->u.si_deadline < ctx->sc_next_event)
				ctx->sc_next_event = item->u.si_deadline;
		}
next_item:
		item = next;
	}
	swim_ctx_unlock(ctx);

	/* send confirmations to selected members */
	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);

		SWIM_DEBUG("try to confirm from source. %lu: %lu <= %lu\n", self_id, item->si_id,
			   item->si_from);

		rc = swim_updates_send(ctx, item->si_id, item->si_from);
		if (rc)
			SWIM_ERROR("swim_updates_send(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

static int
swim_ipings_update(struct swim_context *ctx, uint64_t now, uint64_t net_glitch_delay)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	int				 rc = 0;

	TAILQ_INIT(&targets);

	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		item->u.si_deadline += net_glitch_delay;
		if (now > item->u.si_deadline) {
			TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
			TAILQ_INSERT_TAIL(&targets, item, si_link);
		} else {
			if (item->u.si_deadline < ctx->sc_next_event)
				ctx->sc_next_event = item->u.si_deadline;
		}
		item = next;
	}
	swim_ctx_unlock(ctx);

	/* send notification to expired members */
	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		SWIM_DEBUG("reply IREQ expired. %lu: %lu => %lu\n", self_id, item->si_from,
			   item->si_id);

		rc = ctx->sc_ops->send_reply(ctx, item->si_id, item->si_from,
					     -DER_TIMEDOUT, item->si_args);
		if (rc)
			SWIM_ERROR("send_reply(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

int
swim_ipings_reply(struct swim_context *ctx, swim_id_t to_id, int ret_rc)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	int				 rc = 0;

	TAILQ_INIT(&targets);

	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		if (item->si_id == to_id) {
			TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
			TAILQ_INSERT_TAIL(&targets, item, si_link);
		}
		item = next;
	}
	swim_ctx_unlock(ctx);

	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		SWIM_DEBUG("reply IREQ. %lu: %lu <= %lu\n", self_id, item->si_id, item->si_from);

		rc = ctx->sc_ops->send_reply(ctx, item->si_id, item->si_from,
					     ret_rc, item->si_args);
		if (rc)
			SWIM_ERROR("send_reply(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

int
swim_ipings_suspend(struct swim_context *ctx, swim_id_t from_id, swim_id_t to_id, void *args)
{
	struct swim_item	*item;
	int			 rc = 0;

	swim_ctx_lock(ctx);
	/* looking if sent already */
	TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
		if (item->si_id == to_id) {
			/* don't ping a second time */
			rc = -DER_ALREADY;
			break;
		}
	}

	D_ALLOC_PTR(item);
	if (item != NULL) {
		item->si_id   = to_id;
		item->si_from = from_id;
		item->si_args = args;
		item->u.si_deadline = swim_now_ms() + swim_ping_timeout_get();
		TAILQ_INSERT_TAIL(&ctx->sc_ipings, item, si_link);
	} else {
		rc = -DER_NOMEM;
	}
	swim_ctx_unlock(ctx);

	return rc;
}

static int
swim_subgroup_init(struct swim_context *ctx)
{
	struct swim_item	*item;
	swim_id_t		 id;
	int			 i, rc = 0;

	for (i = 0; i < swim_subgroup_size; i++) {
		id = ctx->sc_ops->get_iping_target(ctx);
		if (id == SWIM_ID_INVALID)
			D_GOTO(out, rc = 0);

		D_ALLOC_PTR(item);
		if (item == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		item->si_from = ctx->sc_target;
		item->si_id   = id;
		TAILQ_INSERT_TAIL(&ctx->sc_subgroup, item, si_link);
	}
out:
	return rc;
}

void *
swim_data(struct swim_context *ctx)
{
	return ctx ? ctx->sc_data : NULL;
}

swim_id_t
swim_self_get(struct swim_context *ctx)
{
	return ctx ? ctx->sc_self : SWIM_ID_INVALID;
}

void
swim_self_set(struct swim_context *ctx, swim_id_t self_id)
{
	if (ctx == NULL)
		return;

	ctx->sc_self = self_id;

	/* Reset it when disabled to avoid false error report about stopping progress */
	if (self_id == SWIM_ID_INVALID)
		ctx->sc_expect_progress_time = 0;
}

struct swim_context *
swim_init(swim_id_t self_id, struct swim_ops *swim_ops, void *data)
{
	struct swim_context	*ctx;
	int			 rc;

	if (swim_ops == NULL ||
	    swim_ops->send_request == NULL ||
	    swim_ops->get_dping_target == NULL ||
	    swim_ops->get_iping_target == NULL ||
	    swim_ops->get_member_state == NULL ||
	    swim_ops->set_member_state == NULL) {
		SWIM_ERROR("there are no proper callbacks specified\n");
		D_GOTO(out, ctx = NULL);
	}

	/* allocate structure for storing swim context */
	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		D_GOTO(out, ctx = NULL);

	rc = SWIM_MUTEX_CREATE(ctx->sc_mutex, NULL);
	if (rc != 0) {
		D_FREE(ctx);
		D_DEBUG(DB_TRACE, "SWIM_MUTEX_CREATE(): %s\n", strerror(rc));
		D_GOTO(out, ctx = NULL);
	}

	ctx->sc_self = self_id;
	ctx->sc_data = data;
	ctx->sc_ops  = swim_ops;

	TAILQ_INIT(&ctx->sc_subgroup);
	TAILQ_INIT(&ctx->sc_suspects);
	TAILQ_INIT(&ctx->sc_updates);
	TAILQ_INIT(&ctx->sc_ipings);

	/* this can be tuned according members count */
	ctx->sc_piggyback_tx_max = SWIM_PIGGYBACK_TX_COUNT;
	/* force to choose next target first */
	ctx->sc_target = SWIM_ID_INVALID;

	/* set global tunable defaults */
	swim_prot_period_len = swim_prot_period_len_default();
	swim_suspect_timeout = swim_suspect_timeout_default();
	swim_ping_timeout    = swim_ping_timeout_default();
	swim_subgroup_size   = swim_subgroup_size_default();

	ctx->sc_default_ping_timeout = swim_ping_timeout;

	/* delay the first ping until all things will be initialized */
	ctx->sc_next_tick_time = swim_now_ms() + 3 * swim_prot_period_len;
out:
	return ctx;
}

void
swim_fini(struct swim_context *ctx)
{
	struct swim_item	*next, *item;
	int			 rc;

	if (ctx == NULL)
		return;

	swim_ipings_update(ctx, UINT64_MAX, 0);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_updates);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_suspects);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_subgroup);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_subgroup, item, si_link);
		D_FREE(item);
		item = next;
	}

	rc = SWIM_MUTEX_DESTROY(ctx->sc_mutex);
	if (rc != 0)
		D_DEBUG(DB_TRACE, "SWIM_MUTEX_DESTROY(): %s\n", strerror(rc));

	D_FREE(ctx);
}

int
swim_net_glitch_update(struct swim_context *ctx, swim_id_t id, uint64_t delay)
{
	struct swim_item	*item;
	swim_id_t		 self_id = swim_self_get(ctx);
	int			 rc = 0;

	swim_ctx_lock(ctx);

	/* update expire time of suspected members */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (id == self_id || id == item->si_id)
			item->u.si_deadline += delay;
	}
	/* update expire time of ipinged members */
	TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
		if (id == self_id || id == item->si_id)
			item->u.si_deadline += delay;
	}

	if (id == self_id || id == ctx->sc_target) {
		if (swim_state_get(ctx) == SCS_PINGED || swim_state_get(ctx) == SCS_IPINGED)
			ctx->sc_deadline += delay;
	}

	swim_ctx_unlock(ctx);

	if (id != self_id)
		SWIM_ERROR("%lu: A network glitch of %lu with %lu ms delay"
			   " is detected.\n", self_id, id, delay);
	return rc;
}

static uint64_t
swim_ping_delay(uint64_t state_delay)
{
	uint64_t	delay = state_delay * 2;
	uint64_t	ping_timeout = swim_ping_timeout_get();

	if (delay < ping_timeout || delay > 3 * ping_timeout)
		delay = ping_timeout;
	return delay;
}

int
swim_progress(struct swim_context *ctx, int64_t timeout_us)
{
	enum swim_context_state	 ctx_state = SCS_TIMEDOUT;
	struct swim_member_state target_state;
	struct swim_item	*item;
	uint64_t		 now, end = 0;
	uint64_t		 net_glitch_delay = 0UL;
	swim_id_t		 target_id, sendto_id;
	bool			 send_updates = false;
	int			 rc;

	/* validate input parameters */
	if (ctx == NULL) {
		SWIM_ERROR("invalid parameter (ctx is NULL)\n");
		D_GOTO(out_err, rc = -DER_INVAL);
	}

	if (ctx->sc_self == SWIM_ID_INVALID) /* not initialized yet */
		D_GOTO(out_err, rc = 0); /* Ignore this update */

	now = swim_now_ms();
	if (timeout_us > 0)
		end = now + timeout_us / 1000; /* timeout in us */
	ctx->sc_next_event = now + swim_period_get();

	if (now > ctx->sc_expect_progress_time &&
	    0  != ctx->sc_expect_progress_time) {
		net_glitch_delay = now - ctx->sc_expect_progress_time;
		SWIM_ERROR("The progress callback was not called for too long: "
			   "%lu ms after expected.\n", net_glitch_delay);
	}

	for (; now <= end || ctx_state == SCS_TIMEDOUT; now = swim_now_ms()) {
		rc = swim_member_update_suspected(ctx, now, net_glitch_delay);
		if (rc) {
			SWIM_ERROR("swim_member_update_suspected(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = swim_ipings_update(ctx, now, net_glitch_delay);
		if (rc) {
			SWIM_ERROR("swim_ipings_update(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		swim_ctx_lock(ctx);
		ctx_state = SCS_SELECT;
		if (ctx->sc_target != SWIM_ID_INVALID) {
			rc = ctx->sc_ops->get_member_state(ctx, ctx->sc_target,
							   &target_state);
			if (rc) {
				ctx->sc_target = SWIM_ID_INVALID;
				if (rc != -DER_NONEXIST) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(%lu): "
						   DF_RC"\n", ctx->sc_target,
						   DP_RC(rc));
					D_GOTO(out, rc);
				}
			} else {
				ctx_state = swim_state_get(ctx);
			}
		}

		switch (ctx_state) {
		case SCS_BEGIN:
			if (now > ctx->sc_next_tick_time) {
				uint64_t delay = swim_ping_delay(target_state.sms_delay);

				target_id = ctx->sc_target;
				sendto_id = ctx->sc_target;
				send_updates = true;
				SWIM_DEBUG("%lu: dping %lu => {%lu %c %lu} "
					   "delay: %u ms, timeout: %lu ms\n",
					   ctx->sc_self, ctx->sc_self, sendto_id,
					   SWIM_STATUS_CHARS[target_state.sms_status],
					   target_state.sms_incarnation, target_state.sms_delay,
					   delay);

				ctx->sc_next_tick_time = now + swim_period_get();
				ctx->sc_deadline = now + delay;
				if (ctx->sc_deadline < ctx->sc_next_event)
					ctx->sc_next_event = ctx->sc_deadline;
				ctx_state = SCS_PINGED;
			} else {
				ctx->sc_next_event =
				    MIN(ctx->sc_next_event, ctx->sc_next_tick_time);
			}
			break;
		case SCS_PINGED:
			/* check whether the ping target from the previous
			 * protocol tick ever successfully acked a direct
			 * ping request
			 */
			ctx->sc_deadline += net_glitch_delay;
			if (now > ctx->sc_deadline) {
				/* no response from direct ping */
				if (target_state.sms_status != SWIM_MEMBER_INACTIVE) {
					ctx_state = SCS_TIMEDOUT;
				} else {
					/* just goto next member,
					 * this is not ready yet.
					 */
					ctx_state = SCS_SELECT;
				}
				ctx->sc_next_event = now;
			} else {
				if (ctx->sc_deadline < ctx->sc_next_event)
					ctx->sc_next_event = ctx->sc_deadline;
			}
			break;
		case SCS_TIMEDOUT:
			/* if we don't hear back from the target after an RTT,
			 * kick off a set of indirect pings to a subgroup of
			 * group members
			 */
			item = TAILQ_FIRST(&ctx->sc_subgroup);
			if (item == NULL) {
				rc = swim_subgroup_init(ctx);
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("swim_subgroup_init(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				item = TAILQ_FIRST(&ctx->sc_subgroup);
			}

			if (item != NULL) {
				struct swim_member_state	state;
				uint64_t			delay;
				uint64_t			deadline;

				target_id = item->si_from;
				sendto_id = item->si_id;
				TAILQ_REMOVE(&ctx->sc_subgroup, item, si_link);
				D_FREE(item);

				rc = ctx->sc_ops->get_member_state(ctx, sendto_id, &state);
				if (rc) {
					SWIM_ERROR("get_member_state(%lu): "DF_RC"\n",
						   sendto_id, DP_RC(rc));
					goto done_item;
				}

				delay = swim_ping_delay(target_state.sms_delay);

				if (target_id != sendto_id) {
					/* Send indirect ping request to ALIVE member only */
					if (state.sms_status != SWIM_MEMBER_ALIVE)
						goto done_item;

					delay *= 2;
					SWIM_DEBUG("%lu: ireq  %lu => {%lu %c %lu} "
						   "delay: %u ms, timeout: %lu ms\n",
						   ctx->sc_self, sendto_id, target_id,
						   SWIM_STATUS_CHARS[target_state.sms_status],
						   target_state.sms_incarnation,
						   target_state.sms_delay, delay);
				} else {
					/* Send ping only if this member is not respond yet */
					if (state.sms_status != SWIM_MEMBER_INACTIVE)
						goto done_item;

					SWIM_DEBUG("%lu: dping  %lu => {%lu %c %lu} "
						   "delay: %u ms, timeout: %lu ms\n",
						   ctx->sc_self, ctx->sc_self, sendto_id,
						   SWIM_STATUS_CHARS[state.sms_status],
						   state.sms_incarnation, state.sms_delay, delay);
				}

				send_updates = true;

				deadline = now + delay;
				if (deadline > ctx->sc_deadline)
					ctx->sc_deadline = deadline;
				if (ctx->sc_deadline < ctx->sc_next_event)
					ctx->sc_next_event = ctx->sc_deadline;
			}

done_item:
			if (TAILQ_EMPTY(&ctx->sc_subgroup))
				ctx_state = SCS_IPINGED;
			break;
		case SCS_IPINGED:
			ctx->sc_deadline += net_glitch_delay;
			if (now > ctx->sc_deadline) {
				/* no response from indirect pings */
				if (target_state.sms_status != SWIM_MEMBER_INACTIVE) {
					/* suspect this member */
					swim_member_suspect(ctx, ctx->sc_self, ctx->sc_target,
							    target_state.sms_incarnation);
				}
				ctx->sc_next_event = now;
				ctx_state = SCS_SELECT;
			} else {
				ctx->sc_next_event =
				    MIN(ctx->sc_next_event, ctx->sc_next_tick_time);
			}
			break;
		case SCS_SELECT:
			ctx->sc_target = ctx->sc_ops->get_dping_target(ctx);
			if (ctx->sc_target == SWIM_ID_INVALID) {
				ctx->sc_next_event = now + swim_period_get();
			} else {
				ctx->sc_next_event =
				    MIN(ctx->sc_next_event, ctx->sc_next_tick_time);
				ctx_state = SCS_BEGIN;
			}
			break;
		}

		net_glitch_delay = 0UL;
		swim_state_set(ctx, ctx_state);
		swim_ctx_unlock(ctx);

		if (send_updates) {
			rc = swim_updates_send(ctx, target_id, sendto_id);
			if (rc) {
				SWIM_ERROR("swim_updates_send(): "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			send_updates = false;
		} else if (now + 100 < ctx->sc_next_event) {
			break; /* break loop if need to wait more than 100 ms. */
		}
	}
	rc = (now > end) ? -DER_TIMEDOUT : -DER_CANCELED;
out:
	ctx->sc_expect_progress_time = now + swim_period_get();
out_err:
	return rc;
}

int
swim_updates_parse(struct swim_context *ctx, swim_id_t from_id, swim_id_t id,
		   struct swim_member_update *upds, size_t nupds)
{
	enum swim_context_state ctx_state;
	struct swim_member_state self_state;
	struct swim_member_state id_state;
	swim_id_t self_id = swim_self_get(ctx);
	swim_id_t upd_id;
	size_t i;
	int rc = 0;

	swim_dump_updates(self_id, from_id, self_id, upds, nupds);

	if (self_id == SWIM_ID_INVALID || nupds == 0) /* not initialized yet */
		return 0; /* Ignore this update */

	swim_ctx_lock(ctx);
	ctx_state = swim_state_get(ctx);

	rc = ctx->sc_ops->get_member_state(ctx, from_id, &id_state);
	if (rc == -DER_NONEXIST || id_state.sms_status == SWIM_MEMBER_DEAD) {
		swim_ctx_unlock(ctx);
		SWIM_DEBUG("%lu: skip untrustable update from %lu, rc = %d\n", self_id, from_id,
			   rc);
		D_GOTO(out, rc = -DER_NONEXIST);
	} else if (rc != 0) {
		swim_ctx_unlock(ctx);
		SWIM_ERROR("get_member_state(%lu): " DF_RC "\n", from_id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if ((from_id == ctx->sc_target || id == ctx->sc_target) &&
	    (ctx_state == SCS_BEGIN || ctx_state == SCS_PINGED || ctx_state == SCS_IPINGED)) {
		ctx_state = SCS_SELECT;
		SWIM_DEBUG("target %lu %s okay\n", ctx->sc_target,
			   from_id == id ? "dping" : "iping");
	}

	for (i = 0; i < nupds; i++) {
		upd_id = upds[i].smu_id;

		switch (upds[i].smu_state.sms_status) {
		case SWIM_MEMBER_INACTIVE:
			/* ignore inactive updates.
			 * inactive status is only for bootstrapping now,
			 * so it should not be spread across others.
			 */
			break;
		case SWIM_MEMBER_ALIVE:
			if (upd_id == self_id)
				break; /* ignore alive updates for self */

			swim_member_alive(ctx, from_id, upd_id, upds[i].smu_state.sms_incarnation);
			break;
		case SWIM_MEMBER_SUSPECT:
		case SWIM_MEMBER_DEAD:
			if (upd_id == self_id) {
				/* increment our incarnation number if we are
				 * suspected/confirmed in the current
				 * incarnation
				 */
				rc = ctx->sc_ops->get_member_state(ctx, self_id,
								   &self_state);
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(%lu): "
						   DF_RC"\n", self_id,
						   DP_RC(rc));
					D_GOTO(out, rc);
				}

				if (self_state.sms_incarnation >
				    upds[i].smu_state.sms_incarnation)
					break; /* already incremented */

				SWIM_ERROR("{%lu %c %lu} self %s received "
					   "{%lu %c %lu} from %lu\n",
					   self_id,
					   SWIM_STATUS_CHARS[self_state.sms_status],
					   self_state.sms_incarnation,
					   SWIM_STATUS_STR[upds[i].smu_state.sms_status],
					   self_id,
					   SWIM_STATUS_CHARS[upds[i].smu_state.sms_status],
					   upds[i].smu_state.sms_incarnation,
					   from_id);

				ctx->sc_ops->new_incarnation(ctx, self_id, &self_state);
				rc = swim_updates_notify(ctx, self_id, self_id, &self_state, 0);
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("swim_updates_notify(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				break;
			}

			if (upds[i].smu_state.sms_status == SWIM_MEMBER_SUSPECT)
				swim_member_suspect(ctx, from_id, upd_id,
						    upds[i].smu_state.sms_incarnation);
			else
				swim_member_dead(ctx, from_id, upd_id,
						 upds[i].smu_state.sms_incarnation);
			break;
		}
	}
	swim_state_set(ctx, ctx_state);
	swim_ctx_unlock(ctx);
out:
	return rc;
}

int
swim_updates_short(struct swim_context *ctx, swim_id_t self_id, uint64_t self_incarnation,
		   swim_id_t from_id, swim_id_t id, struct swim_member_update *upds_in,
		   size_t nupds_in, struct swim_member_update **upds_out, size_t *nupds_out)
{
	struct swim_member_state	 self_state = {
		.sms_incarnation	= self_incarnation,
		.sms_status		= SWIM_MEMBER_ALIVE,
		.sms_delay		= 0
	};
	struct swim_member_update	*id_upd = NULL;
	struct swim_member_update	*upds;
	size_t				 nupds;
	size_t				 i;

	swim_dump_updates(self_id, from_id, self_id, upds_in, nupds_in);

	swim_ctx_lock(ctx);
	for (i = 0; i < nupds_in; i++) {
		if (upds_in[i].smu_id == self_id) {
			if (upds_in[i].smu_state.sms_incarnation < self_incarnation ||
			    (upds_in[i].smu_state.sms_status != SWIM_MEMBER_SUSPECT &&
			     upds_in[i].smu_state.sms_status != SWIM_MEMBER_DEAD))
				continue;

			SWIM_ERROR("{%lu %c %lu} self %s received {%lu %c %lu} from %lu\n",
				   self_id, 'A', self_incarnation,
				   SWIM_STATUS_STR[upds_in[i].smu_state.sms_status],
				   upds_in[i].smu_id,
				   SWIM_STATUS_CHARS[upds_in[i].smu_state.sms_status],
				   upds_in[i].smu_state.sms_incarnation, from_id);

			ctx->sc_ops->new_incarnation(ctx, self_id, &self_state);
		} else if (upds_in[i].smu_id == id) {
			id_upd = &upds_in[i];
		}
	}
	swim_ctx_unlock(ctx);

	nupds = 1 /* self_id */;
	if (id != self_id && id_upd != NULL)
		nupds++; /* id */

	D_ALLOC_ARRAY(upds, nupds);
	if (upds == NULL)
		return -DER_NOMEM;

	i = 0;

	upds[i].smu_state.sms_incarnation = self_state.sms_incarnation;
	upds[i].smu_state.sms_status = SWIM_MEMBER_ALIVE;
	upds[i].smu_state.sms_delay = 0;
	upds[i++].smu_id = self_id;

	if (id != self_id && id_upd != NULL) {
		upds[i].smu_state.sms_incarnation = id_upd->smu_state.sms_incarnation;
		upds[i].smu_state.sms_status = SWIM_MEMBER_ALIVE;
		upds[i].smu_state.sms_delay = 0;
		upds[i++].smu_id = id;
	}

	D_ASSERTF(i == nupds, "%zu == %zu\n", i, nupds);

	swim_dump_updates(self_id, self_id, from_id, upds, nupds);

	*upds_out = upds;
	*nupds_out = nupds;
	return 0;
}

void
swim_member_del(struct swim_context *ctx, swim_id_t id)
{
	struct swim_item *item;

	swim_ctx_lock(ctx);
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (item->si_id == id) {
			TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
			D_FREE(item);
			break;
		}
	}
	swim_ctx_unlock(ctx);
}
