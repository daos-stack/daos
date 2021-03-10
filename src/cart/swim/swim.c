/*
 * Copyright (c) 2016 UChicago Argonne, LLC
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(swim)

#include "swim_internal.h"
#include <assert.h>

static uint64_t swim_prot_period_len;
static uint64_t swim_suspect_timeout;
static uint64_t swim_ping_timeout;

static inline uint64_t
swim_prot_period_len_default(void)
{
	unsigned int val = SWIM_PROTOCOL_PERIOD_LEN;

	d_getenv_int("SWIM_PROTOCOL_PERIOD_LEN", &val);
	return val;
}

static inline uint64_t
swim_suspect_timeout_default(void)
{
	unsigned int val = SWIM_SUSPECT_TIMEOUT;

	d_getenv_int("SWIM_SUSPECT_TIMEOUT", &val);
	return val;
}

static inline uint64_t
swim_ping_timeout_default(void)
{
	unsigned int val = SWIM_PING_TIMEOUT;

	d_getenv_int("SWIM_PING_TIMEOUT", &val);
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
swim_dump_updates(swim_id_t self_id, swim_id_t from, swim_id_t to,
		  struct swim_member_update *upds, size_t nupds)
{
	FILE *fp;
	char *msg;
	size_t msg_size, i;
	int rc;

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
			SWIM_INFO("%lu %s %lu:%s\n", self_id,
				  self_id == from ? "=>" : "<=",
				  self_id == from ? to   : from, msg);
		free(msg); /* allocated by open_memstream() */
	}
}

static int
swim_updates_send(struct swim_context *ctx, swim_id_t id, swim_id_t to)
{
	struct swim_member_update *upds;
	struct swim_item *next, *item;
	swim_id_t self_id = swim_self_get(ctx);
	size_t nupds, i = 0;
	int rc = 0;

	if (id == SWIM_ID_INVALID || to == SWIM_ID_INVALID) {
		SWIM_ERROR("member id is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	nupds = SWIM_PIGGYBACK_ENTRIES + (id != self_id ? 2 : 1);
	D_ALLOC_ARRAY(upds, nupds);
	if (upds == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	swim_ctx_lock(ctx);

	rc = ctx->sc_ops->get_member_state(ctx, id, &upds[i].smu_state);
	if (rc) {
		SWIM_ERROR("get_member_state(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_unlock, rc);
	}
	upds[i++].smu_id = id;

	if (id != self_id) {
		/* update self status on target */
		rc = ctx->sc_ops->get_member_state(ctx, self_id,
						   &upds[i].smu_state);
		if (rc) {
			SWIM_ERROR("get_member_state(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_unlock, rc);
		}
		upds[i++].smu_id = self_id;
	}

	item = TAILQ_FIRST(&ctx->sc_updates);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);

		/* delete entries that are too many */
		if (i >= nupds) {
			TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
			D_FREE(item);
			item = next;
			continue;
		}

		/* update with recent updates */
		if (item->si_id != id && item->si_id != self_id) {
			rc = ctx->sc_ops->get_member_state(ctx, item->si_id,
							   &upds[i].smu_state);
			if (rc) {
				if (rc == -DER_NONEXIST) {
					/* this member was removed already */
					TAILQ_REMOVE(&ctx->sc_updates, item,
						     si_link);
					D_FREE(item);
					item = next;
					continue;
				}
				SWIM_ERROR("get_member_state(): "DF_RC"\n",
					   DP_RC(rc));
				D_GOTO(out_unlock, rc);
			}
			upds[i++].smu_id = item->si_id;
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

	if (rc == 0) {
		swim_dump_updates(self_id, self_id, to, upds, i);
		rc = ctx->sc_ops->send_message(ctx, to, upds, i);
	}

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
swim_member_alive(struct swim_context *ctx, swim_id_t from,
		  swim_id_t id, uint64_t nr)
{
	struct swim_member_state id_state;
	struct swim_item *item;
	uint64_t count = 0;
	int rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		SWIM_ERROR("get_member_state(): "DF_RC"\n", DP_RC(rc));
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

	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_ALIVE;
	rc = swim_updates_notify(ctx, from, id, &id_state, count);
out:
	return rc;
}

static int
swim_member_dead(struct swim_context *ctx, swim_id_t from,
		 swim_id_t id, uint64_t nr)
{
	struct swim_member_state id_state;
	struct swim_item *item;
	int rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		SWIM_ERROR("get_member_state(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
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

	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_DEAD;
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);
out:
	return rc;
}

static int
swim_member_suspect(struct swim_context *ctx, swim_id_t from,
		    swim_id_t id, uint64_t nr)
{
	struct swim_member_state id_state;
	struct swim_item *item;
	int rc;

	/* if there is no suspicion timeout, just kill the member */
	if (swim_suspect_timeout_get() == 0)
		return swim_member_dead(ctx, from, id, nr);

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);
	if (rc) {
		SWIM_ERROR("get_member_state(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

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
		if (item->si_id == id)
			goto update;
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
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_SUSPECT;
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);
out:
	return rc;
}

static int
swim_member_update_suspected(struct swim_context *ctx, uint64_t now,
			     uint64_t net_glitch_delay)
{
	TAILQ_HEAD(, swim_item)  targets;
	struct swim_member_state id_state;
	struct swim_item *next, *item;
	swim_id_t self_id = swim_self_get(ctx);
	swim_id_t id, from;
	int rc = 0;

	TAILQ_INIT(&targets);

	/* update status of suspected members */
	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_suspects);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		item->u.si_deadline += net_glitch_delay;
		if (now > item->u.si_deadline) {
			SWIM_INFO("%lu: suspect timeout %lu\n",
				  self_id, item->si_id);

			if (item->si_from != self_id) {
				/* let's try to confirm from gossip origin */
				id   = item->si_id;
				from = item->si_from;

				item->si_from = self_id;
				item->u.si_deadline += swim_ping_timeout_get();

				D_ALLOC_PTR(item);
				if (item == NULL)
					D_GOTO(next_item, rc = -DER_NOMEM);
				item->si_id   = id;
				item->si_from = from;
				TAILQ_INSERT_TAIL(&targets, item, si_link);
			} else {
				rc = ctx->sc_ops->get_member_state(ctx,
								   item->si_id,
								   &id_state);
				if (!rc) {
					/* if this member has exceeded
					 * its allowable suspicion timeout,
					 * we mark it as dead
					 */
					swim_member_dead(ctx, item->si_from,
							item->si_id,
						      id_state.sms_incarnation);
				} else {
					TAILQ_REMOVE(&ctx->sc_suspects, item,
						     si_link);
					D_FREE(item);
				}
			}
		}
next_item:
		item = next;
	}
	swim_ctx_unlock(ctx);

	/* send confirmations to selected members */
	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		SWIM_INFO("%lu: try to confirm %lu <= %lu\n", self_id,
			  item->si_id, item->si_from);

		rc = swim_updates_send(ctx, item->si_id, item->si_from);
		if (rc)
			SWIM_ERROR("swim_updates_send(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

static int
swim_ipings_update(struct swim_context *ctx, uint64_t now,
		   uint64_t net_glitch_delay)
{
	struct swim_item *next, *item;
	int rc = 0;

	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		item->u.si_deadline += net_glitch_delay;
		if (now > item->u.si_deadline) {
			TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
			D_FREE(item);
		}
		item = next;
	}
	swim_ctx_unlock(ctx);

	return rc;
}

static int
swim_subgroup_init(struct swim_context *ctx)
{
	struct swim_item *item;
	swim_id_t id;
	int i, rc = 0;

	for (i = 0; i < SWIM_SUBGROUP_SIZE; i++) {
		id = ctx->sc_ops->get_iping_target(ctx);
		if (id == SWIM_ID_INVALID)
			D_GOTO(out, rc = 0);

		D_ALLOC_PTR(item);
		if (item == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		item->si_id = id;
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
	if (ctx != NULL)
		ctx->sc_self = self_id;
}

struct swim_context *
swim_init(swim_id_t self_id, struct swim_ops *swim_ops, void *data)
{
	struct swim_context *ctx;
	int rc;

	if (swim_ops == NULL ||
	    swim_ops->send_message == NULL ||
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

	/* delay the first ping until all things will be initialized */
	ctx->sc_next_tick_time = swim_now_ms() + 10 * SWIM_PROTOCOL_PERIOD_LEN;

	/* set global tunable defaults */
	swim_prot_period_len = swim_prot_period_len_default();
	swim_suspect_timeout = swim_suspect_timeout_default();
	swim_ping_timeout    = swim_ping_timeout_default();

out:
	return ctx;
}

void
swim_fini(struct swim_context *ctx)
{
	struct swim_item *next, *item;
	int rc;

	if (ctx == NULL)
		return;

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
swim_net_glitch_update(struct swim_context *ctx, swim_id_t id,
		       uint64_t delay)
{
	swim_id_t self_id = swim_self_get(ctx);
	struct swim_item *item;
	int rc = 0;

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
		switch (swim_state_get(ctx)) {
		case SCS_DPINGED:
			ctx->sc_dping_deadline += delay;
			break;
		case SCS_IPINGED:
			ctx->sc_iping_deadline += delay;
			break;
		default:
			break;
		}
	}

	swim_ctx_unlock(ctx);

	SWIM_ERROR("%lu: A network glitch of %lu with %lu msec delay"
		   " is detected.\n", self_id, id, delay);
	return rc;
}

int
swim_progress(struct swim_context *ctx, int64_t timeout)
{
	enum swim_context_state ctx_state = SCS_TIMEDOUT;
	struct swim_member_state target_state;
	struct swim_item *item;
	uint64_t now, end = 0;
	uint64_t net_glitch_delay = 0UL;
	swim_id_t id_target, id_sendto;
	bool send_updates = false;
	int rc;

	/* validate input parameters */
	if (ctx == NULL) {
		SWIM_ERROR("invalid parameter (ctx is NULL)\n");
		D_GOTO(out_err, rc = -DER_INVAL);
	}

	if (ctx->sc_self == SWIM_ID_INVALID) /* not initialized yet */
		D_GOTO(out_err, rc = 0); /* Ignore this update */

	now = swim_now_ms();
	if (timeout > 0)
		end = now + timeout;

	if (now > ctx->sc_expect_progress_time &&
	    0  != ctx->sc_expect_progress_time) {
		net_glitch_delay = now - ctx->sc_expect_progress_time;
		SWIM_ERROR("The progress callback was not called for too long: "
			   "%lu ms after expected.\n", net_glitch_delay);
	}

	for (; now <= end || ctx_state == SCS_TIMEDOUT; now = swim_now_ms()) {
		rc = swim_member_update_suspected(ctx, now, net_glitch_delay);
		if (rc) {
			SWIM_ERROR("swim_member_update_suspected(): "DF_RC"\n",
				   DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = swim_ipings_update(ctx, now, net_glitch_delay);
		if (rc) {
			SWIM_ERROR("swim_ipings_update(): "DF_RC"\n",
				   DP_RC(rc));
			D_GOTO(out, rc);
		}

		swim_ctx_lock(ctx);
		ctx_state = SCS_DEAD;
		if (ctx->sc_target != SWIM_ID_INVALID) {
			rc = ctx->sc_ops->get_member_state(ctx,
							   ctx->sc_target,
							   &target_state);
			if (rc) {
				ctx->sc_target = SWIM_ID_INVALID;
				if (rc != -DER_NONEXIST) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
			} else {
				ctx_state = swim_state_get(ctx);
			}
		}

		switch (ctx_state) {
		case SCS_BEGIN:
			if (now > ctx->sc_next_tick_time) {
				ctx->sc_next_tick_time = now
							 + swim_period_get();

				id_target = ctx->sc_target;
				id_sendto = ctx->sc_target;
				send_updates = true;
				SWIM_INFO("%lu: dping %lu => {%lu %c %lu}\n",
					  ctx->sc_self, ctx->sc_self, id_sendto,
					  SWIM_STATUS_CHARS[
						       target_state.sms_status],
					  target_state.sms_incarnation);

				ctx->sc_dping_deadline = now
						      + swim_ping_timeout_get();
				ctx_state = SCS_DPINGED;
			}
			break;
		case SCS_DPINGED:
			/* check whether the ping target from the previous
			 * protocol tick ever successfully acked a direct
			 * ping request
			 */
			ctx->sc_dping_deadline += net_glitch_delay;
			if (now > ctx->sc_dping_deadline) {
				/* no response from direct ping */
				if (target_state.sms_status != SWIM_MEMBER_INACTIVE) {
					/* suspect this member */
					swim_member_suspect(ctx, ctx->sc_self,
							    ctx->sc_target,
						  target_state.sms_incarnation);
					ctx_state = SCS_TIMEDOUT;
				} else {
					/* just goto next member,
					 * this is not ready yet.
					 */
					ctx_state = SCS_DEAD;
				}
			}
			break;
		case SCS_IPINGED:
			/* check whether the ping target from the previous
			 * protocol tick ever successfully acked a indirect
			 * ping request
			 */
			ctx->sc_iping_deadline += net_glitch_delay;
			if (now > ctx->sc_iping_deadline) {
				/* no response from indirect pings,
				 * will be dead by suspicion timeout
				 * if there are no gossips about it.
				 */
				SWIM_ERROR("%lu: iping timeout {%lu %c %lu}\n",
					   ctx->sc_self, ctx->sc_target,
					   SWIM_STATUS_CHARS[
						       target_state.sms_status],
					   target_state.sms_incarnation);
				/* So, just goto next member. */
				ctx_state = SCS_DEAD;
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
				id_target = ctx->sc_target;
				id_sendto = item->si_id;
				send_updates = true;

				SWIM_INFO("%lu: ireq  %lu => {%lu %c %lu}\n",
					  ctx->sc_self, id_sendto, id_target,
					  SWIM_STATUS_CHARS[
						       target_state.sms_status],
					  target_state.sms_incarnation);

				TAILQ_REMOVE(&ctx->sc_subgroup,
					     item, si_link);
				D_FREE(item);

				item = TAILQ_FIRST(&ctx->sc_subgroup);
				if (item == NULL) {
					ctx->sc_iping_deadline = now
						  + 2 * swim_ping_timeout_get();
					ctx_state = SCS_IPINGED;
				}
				break;
			}
			/* fall through to select a next target */
		case SCS_ACKED:
		case SCS_DEAD:
			ctx->sc_target = ctx->sc_ops->get_dping_target(ctx);
			if (ctx->sc_target == SWIM_ID_INVALID) {
				swim_ctx_unlock(ctx);
				D_GOTO(out, rc = -DER_SHUTDOWN);
			}

			ctx_state = SCS_BEGIN;
			break;
		}

		net_glitch_delay = 0UL;
		swim_state_set(ctx, ctx_state);
		swim_ctx_unlock(ctx);

		if (send_updates) {
			rc = swim_updates_send(ctx, id_target, id_sendto);
			if (rc) {
				SWIM_ERROR("swim_updates_send(): "
					   DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			send_updates = false;
		}
	}
	rc = (now > end) ? -DER_TIMEDOUT : -DER_CANCELED;
out:
	ctx->sc_expect_progress_time = now + swim_period_get() / 2;
out_err:
	return rc;
}

int
swim_parse_message(struct swim_context *ctx, swim_id_t from,
		   struct swim_member_update *upds, size_t nupds)
{
	struct swim_item *item;
	enum swim_context_state ctx_state;
	struct swim_member_state self_state;
	swim_id_t self_id = swim_self_get(ctx);
	swim_id_t id_target, id_sendto, to, id;
	bool send_updates = false;
	size_t i;
	int rc = 0;

	swim_dump_updates(self_id, from, self_id, upds, nupds);

	if (self_id == SWIM_ID_INVALID || nupds == 0) /* not initialized yet */
		return 0; /* Ignore this update */

	swim_ctx_lock(ctx);
	ctx_state = swim_state_get(ctx);

	if (from == ctx->sc_target &&
	    (ctx_state == SCS_BEGIN || ctx_state == SCS_DPINGED))
		ctx_state = SCS_ACKED;

	to = upds[0].smu_id; /* save first index from update */
	for (i = 0; i < nupds; i++) {
		id = upds[i].smu_id;

		switch (upds[i].smu_state.sms_status) {
		case SWIM_MEMBER_INACTIVE:
			/* ignore inactive updates.
			 * inactive status is only for bootstrapping now,
			 * so it should not be spread across others.
			 */
			break;
		case SWIM_MEMBER_ALIVE:
			if (id == self_id)
				break; /* ignore alive updates for self */

			if (id == ctx->sc_target && ctx_state == SCS_IPINGED)
				ctx_state = SCS_ACKED;

			swim_member_alive(ctx, from, id,
					  upds[i].smu_state.sms_incarnation);
			break;
		case SWIM_MEMBER_SUSPECT:
			if (id == self_id) {
				/* increment our incarnation number if we are
				 * suspected in the current incarnation
				 */
				rc = ctx->sc_ops->get_member_state(ctx, self_id,
								   &self_state);
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}

				if (self_state.sms_incarnation >
				    upds[i].smu_state.sms_incarnation)
					break; /* already incremented */

				SWIM_ERROR("{%lu %c %lu} self SUSPECT received "
					   "{%lu %c %lu} from %lu\n", self_id,
					   SWIM_STATUS_CHARS[
							 self_state.sms_status],
					   self_state.sms_incarnation, self_id,
					   SWIM_STATUS_CHARS[
						  upds[i].smu_state.sms_status],
					   upds[i].smu_state.sms_incarnation,
					   from);

				self_state.sms_incarnation++;
				rc = swim_updates_notify(ctx, self_id, self_id,
							 &self_state, 0);
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("swim_updates_notify(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				break;
			}

			swim_member_suspect(ctx, from, id,
					    upds[i].smu_state.sms_incarnation);
			break;
		case SWIM_MEMBER_DEAD:
			/* if we get an update that we are dead,
			 * just shut down
			 */
			if (id == self_id) {
				swim_ctx_unlock(ctx);
				SWIM_ERROR("%lu: self confirmed DEAD received "
					   "{%lu %c %lu} from %lu\n", self_id,
					   self_id, SWIM_STATUS_CHARS[
						  upds[i].smu_state.sms_status],
					   upds[i].smu_state.sms_incarnation,
					   from);
				D_GOTO(out, rc = -DER_SHUTDOWN);
			}

			SWIM_ERROR("%lu: DEAD received {%lu %c %lu} from %lu\n",
				   self_id, id, SWIM_STATUS_CHARS[
						  upds[i].smu_state.sms_status],
				   upds[i].smu_state.sms_incarnation, from);

			swim_member_dead(ctx, from, id,
					 upds[i].smu_state.sms_incarnation);
			break;
		}
	}

	if (to == self_id) { /* dping request */
		/* send dping response */
		id_target = self_id;
		id_sendto = from;
		send_updates = true;
		SWIM_INFO("%lu: dresp %lu => %lu\n",
			  self_id, id_target, id_sendto);
	} else if (to == from) { /* dping response */
		/* forward this dping response to appropriate target */
		TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
			if (item->si_id == from) {
				id_target = to;
				id_sendto = item->si_from;
				send_updates = true;
				SWIM_INFO("%lu: iresp %lu => %lu\n",
					  self_id, id_target, id_sendto);

				TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
				D_FREE(item);
				break;
			}
		}
	} else { /* iping request or response */
		if (to != ctx->sc_target &&
		    upds[0].smu_state.sms_status == SWIM_MEMBER_SUSPECT) {
			/* send dping request to iping target */
			id_target = to;
			id_sendto = to;
			send_updates = true;

			/* looking if sent already */
			TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
				if (item->si_id == to) {
					/* don't send a second time */
					send_updates = false;
					break;
				}
			}

			D_ALLOC_PTR(item);
			if (item != NULL) {
				item->si_id   = to;
				item->si_from = from;
				item->u.si_deadline = swim_now_ms()
						      + swim_ping_timeout_get();
				TAILQ_INSERT_TAIL(&ctx->sc_ipings, item,
						  si_link);
				SWIM_INFO("%lu: iping %lu => %lu\n",
					  self_id, from, to);
			} else {
				send_updates = false;
				rc = -DER_NOMEM;
			}
		}
	}

	swim_state_set(ctx, ctx_state);
	swim_ctx_unlock(ctx);

	while (send_updates) {
		rc = swim_updates_send(ctx, id_target, id_sendto);
		if (rc)
			SWIM_ERROR("swim_updates_send(): "DF_RC"\n", DP_RC(rc));

		send_updates = false;
		if (to != self_id && to == from) { /* dping response */
			/* forward this dping response to appropriate target */
			swim_ctx_lock(ctx);
			TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
				if (item->si_id == from) {
					id_target = to;
					id_sendto = item->si_from;
					send_updates = true;
					SWIM_INFO("%lu: iresp %lu => %lu\n",
						  self_id, id_target,
						  id_sendto);

					TAILQ_REMOVE(&ctx->sc_ipings, item,
						     si_link);
					D_FREE(item);
					break;
				}
			}
			swim_ctx_unlock(ctx);
		}
	}
out:
	return rc;
}
