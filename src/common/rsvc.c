/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rsvc: Replicated Service Utilities
 *
 * These are utilities help us deal with replicated services. Currently, it is
 * mainly about client state and client leader searching.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/rsvc.h>

#define DF_CLI		"n=%u known=%d alive=%u term="DF_U64" index=%d next=%d"
#define DP_CLI(c)	(c)->sc_ranks->rl_nr, (c)->sc_leader_known,	\
			(c)->sc_leader_aliveness, (c)->sc_leader_term,	\
			(c)->sc_leader_index, (c)->sc_next

/* Maximum value for rsvc_client.sc_leader_aliveness */
#define LEADER_ALIVENESS_MAX 1

static inline void
rsvc_client_reset_leader(struct rsvc_client *client)
{
	client->sc_leader_known = false;
	client->sc_leader_aliveness = 0;
	client->sc_leader_term = -1;
	client->sc_leader_index = -1;
}

/**
 * Initialize \a client.
 *
 * \param[out]	client	client state
 * \param[in]	ranks	(optional) ranks of (potential) service replicas
 */
int
rsvc_client_init(struct rsvc_client *client, const d_rank_list_t *ranks)
{
	if (ranks) {
		int	rc;

		rc = daos_rank_list_dup_sort_uniq(&client->sc_ranks, ranks);
		if (rc != 0)
			return rc;
	} else {
		client->sc_ranks = d_rank_list_alloc(0);

		if (client->sc_ranks == NULL)
			return -DER_NOMEM;
	}
	rsvc_client_reset_leader(client);
	client->sc_next = 0;
	return 0;
}

/**
 * Finalize \a client.
 *
 * \param[in,out]	client	client state
 */
void
rsvc_client_fini(struct rsvc_client *client)
{
	d_rank_list_free(client->sc_ranks);
	client->sc_ranks = NULL;
}

/**
 * Choose an \a ep for an RPC of \a client. Does not change \a ep->ep_group.
 *
 * \param[in,out]	client	client state
 * \param[out]		ep	crt_endpoint_t for the RPC
 */
int
rsvc_client_choose(struct rsvc_client *client, crt_endpoint_t *ep)
{
	int chosen = -1;

	D_DEBUG(DB_MD, DF_CLI"\n", DP_CLI(client));
	if (client->sc_leader_known && client->sc_leader_aliveness > 0) {
		chosen = client->sc_leader_index;
	} else if (client->sc_ranks->rl_nr > 0) {
		chosen = client->sc_next;
		/* The hintless search is a round robin of all replicas. */
		client->sc_next++;
		client->sc_next %= client->sc_ranks->rl_nr;
	}

	if (chosen == -1) {
		D_DEBUG(DB_MD, "replica list empty\n");
		return -DER_NOTREPLICA;
	} else {
		D_ASSERTF(chosen >= 0 && chosen < client->sc_ranks->rl_nr,
			  "%d\n", chosen);
		ep->ep_rank = client->sc_ranks->rl_ranks[chosen];
	}
	ep->ep_tag = 0;
	return 0;
}

/* Process an error without leadership hint. */
static void
rsvc_client_process_error(struct rsvc_client *client, int rc,
			  const crt_endpoint_t *ep)
{
	int leader_index = client->sc_leader_index;

	if (rc == -DER_OOG || rc == -DER_NOTREPLICA) {
		int pos;
		bool found;
		d_rank_list_t *rl = client->sc_ranks;

		rsvc_client_reset_leader(client);
		found = daos_rank_list_find(rl, ep->ep_rank, &pos);
		if (!found) {
			D_DEBUG(DB_MD, "rank %u not found in list of replicas",
				ep->ep_rank);
			return;
		}
		rl->rl_nr--;
		if (pos < rl->rl_nr) {
			memmove(&rl->rl_ranks[pos], &rl->rl_ranks[pos + 1],
				(rl->rl_nr - pos) * sizeof(*rl->rl_ranks));
			client->sc_next = pos;
		} else {
			client->sc_next = 0;
		}
		D_ERROR("removed rank %u from replica list due to "DF_RC"\n",
			ep->ep_rank, DP_RC(rc));
	} else if (client->sc_leader_known && client->sc_leader_aliveness > 0 &&
		   ep->ep_rank == client->sc_ranks->rl_ranks[leader_index]) {
		/* A leader stepping up may briefly reply NOTLEADER with hint.
		 * "Give up" but "bump aliveness" in rsvc_client_process_hint().
		 */
		if (rc == -DER_NOTLEADER)
			client->sc_leader_aliveness = 0;
		else
			client->sc_leader_aliveness--;
		if (client->sc_leader_aliveness == 0) {
			/*
			 * Gave up this leader. Start the hintless
			 * search.
			 */
			D_DEBUG(DB_MD, "give up leader rank %u\n",
				ep->ep_rank);
			client->sc_next = client->sc_leader_index + 1;
			client->sc_next %= client->sc_ranks->rl_nr;
		}
	}
}

/* Process a result with a leadership hint. */
static void
rsvc_client_process_hint(struct rsvc_client *client,
			 const struct rsvc_hint *hint, bool from_leader,
			 const crt_endpoint_t *ep)
{
	bool found;
	bool becoming_leader;

	D_ASSERT(hint->sh_flags & RSVC_HINT_VALID);

	if (from_leader && hint->sh_rank != ep->ep_rank) {
		D_ERROR("empty or invalid hint from leader rank %u: hint.term="
			DF_U64" hint.rank=%u\n", ep->ep_rank, hint->sh_term,
			hint->sh_rank);
		return;
	}

	if (client->sc_leader_known) {
		if (hint->sh_term < client->sc_leader_term) {
			D_DEBUG(DB_MD, "stale hint from rank %u: hint.term="
				DF_U64" hint.rank=%u\n", ep->ep_rank,
				hint->sh_term, hint->sh_rank);
			return;
		} else if (hint->sh_term == client->sc_leader_term) {
			if (ep->ep_rank == hint->sh_rank) {
				if (client->sc_leader_aliveness < LEADER_ALIVENESS_MAX) {
					D_DEBUG(DB_MD, "leader rank %u bump aliveness %u -> %u\n",
						hint->sh_rank, client->sc_leader_aliveness,
						LEADER_ALIVENESS_MAX);
					client->sc_leader_aliveness = LEADER_ALIVENESS_MAX;
				}
			}
			return;
		}
	}

	/* Got new leadership info. Cache it. */
	found = daos_rank_list_find(client->sc_ranks, hint->sh_rank,
				    &client->sc_leader_index);
	if (!found) {
		int rc;

		D_DEBUG(DB_MD, "unknown replica from rank %u: hint.term="DF_U64
			" hint.rank=%u\n", ep->ep_rank, hint->sh_term,
			hint->sh_rank);
		/* Append the unknown rank to tolerate user mistakes. */
		rc = daos_rank_list_append(client->sc_ranks, hint->sh_rank);
		if (rc != 0) {
			D_DEBUG(DB_MD, "failed to append new rank: "DF_RC"\n",
				DP_RC(rc));
			return;
		}
		client->sc_leader_index = client->sc_ranks->rl_nr - 1;
	}
	client->sc_leader_term = hint->sh_term;
	client->sc_leader_known = true;
	/*
	 * If from_leader, set the aliveness to MAX so that (if MAX > 1) upon a
	 * crt error we'll give the leader more tries before turning to others.
	 * (If node failures were more frequent than message losses, then a
	 * smaller MAX should be used instead.). A new leader may briefly reply
	 * NOTLEADER while stepping up, in which case "from_leader=false" and
	 * inspect further.
	 */
	becoming_leader = (ep->ep_rank == hint->sh_rank);
	client->sc_leader_aliveness = (from_leader || becoming_leader) ? LEADER_ALIVENESS_MAX : 1;
	D_DEBUG(DB_MD, "new hint from rank %u: hint.term="DF_U64
		" hint.rank=%u\n", ep->ep_rank, hint->sh_term, hint->sh_rank);
}

/**
 * Complete an RPC of \a client. Callers shall call this right after the RPC
 * completes (e.g., at the beginning of the RPC completion callback or an early
 * task completion callback).
 *
 * \param[in,out]	client	client state
 * \param[in]		ep	crt_endpoint_t of the RPC
 * \param[in]		rc_crt	rc from crt
 * \param[in]		rc_svc	rc from service (only read if rc_crt is zero)
 * \param[in]		hint	leadership hint (only read if rc_crt is zero)
 *
 * \retval	RSVC_CLIENT_PROCEED	see definition
 * \retval	RSVC_CLIENT_RECHOOSE	see definition
 */
int
rsvc_client_complete_rpc(struct rsvc_client *client, const crt_endpoint_t *ep,
			 int rc_crt, int rc_svc, const struct rsvc_hint *hint)
{
	D_DEBUG(DB_MD, DF_CLI"\n", DP_CLI(client));
	/*
	 * Enumerate all cases of <rc_crt, rc_svc, hint>. Keep them at the same
	 * indentation level, please.
	 */
	if (rc_crt == -DER_INVAL) {
		D_DEBUG(DB_MD, "group-id %s does not exist for rank %u: rc_crt=%d\n",
			ep->ep_grp->cg_grpid, ep->ep_rank, rc_crt);
		rsvc_client_process_error(client, rc_crt, ep);
		return RSVC_CLIENT_PROCEED;
	} else if (rc_crt == -DER_OOG) {
		D_DEBUG(DB_MD, "rank %u out of group: rc_crt=%d\n",
			ep->ep_rank, rc_crt);
		rsvc_client_process_error(client, rc_crt, ep);
		return RSVC_CLIENT_RECHOOSE;
	} else if (rc_crt != 0) {
		D_DEBUG(DB_MD, "no reply from rank %u: rc_crt=%d\n",
			ep->ep_rank, rc_crt);
		rsvc_client_process_error(client, rc_crt, ep);
		return RSVC_CLIENT_RECHOOSE;
	} else if (rc_svc == -DER_NOTLEADER &&
		   (hint == NULL || !(hint->sh_flags & RSVC_HINT_VALID))) {
		D_DEBUG(DB_MD, "non-leader reply without hint from rank %u\n",
			ep->ep_rank);
		rsvc_client_process_error(client, rc_svc, ep);
		return RSVC_CLIENT_RECHOOSE;
	} else if (rc_svc == -DER_NOTLEADER) {
		D_DEBUG(DB_MD, "non-leader reply with hint from rank %u: "
			"hint.term="DF_U64" hint.rank=%u\n", ep->ep_rank,
			hint->sh_term, hint->sh_rank);
		rsvc_client_process_error(client, rc_svc, ep);
		rsvc_client_process_hint(client, hint, false /* !from_leader */,
					 ep);
		return RSVC_CLIENT_RECHOOSE;
	} else if (rc_svc == -DER_NOTREPLICA) {
		/* This may happen when a service replica was destroyed. */
		D_DEBUG(DB_MD, "service not found reply from rank %u: ",
			ep->ep_rank);
		rsvc_client_process_error(client, rc_svc, ep);
		return RSVC_CLIENT_RECHOOSE;
	} else if (hint == NULL || !(hint->sh_flags & RSVC_HINT_VALID)) {
		/* This may happen if the service wasn't found. */
		D_DEBUG(DB_MD, "\"leader\" reply without hint from rank %u: "
			"rc_svc=%d\n", ep->ep_rank, rc_svc);
		return RSVC_CLIENT_PROCEED;
	} else {
		D_DEBUG(DB_MD, "leader reply with hint from rank %u: hint.term="
			DF_U64" hint.rank=%u rc_svc=%d\n", ep->ep_rank,
			hint->sh_term, hint->sh_rank, rc_svc);
		rsvc_client_process_hint(client, hint, true /* from_leader */,
					 ep);
		return RSVC_CLIENT_PROCEED;
	}
}

static const uint32_t rsvc_client_buf_magic = 0x23947e2f;

struct rsvc_client_buf {
	uint32_t	scb_magic;
	uint32_t	scb_nranks;
	uint32_t	scb_leader_known;
	uint32_t	scb_leader_aliveness;
	uint64_t	scb_leader_term;
	uint32_t	scb_leader_index;
	uint32_t	scb_next;
	d_rank_t	scb_ranks[0];
};

size_t
rsvc_client_encode(const struct rsvc_client *client, void *buf)
{
	struct rsvc_client_buf *p = buf;
	size_t			len;

	len = sizeof(*p) + sizeof(*p->scb_ranks) * client->sc_ranks->rl_nr;
	if (p != NULL) {
		p->scb_magic = rsvc_client_buf_magic;
		p->scb_nranks = client->sc_ranks->rl_nr;
		p->scb_leader_known = client->sc_leader_known ? 1 : 0;
		p->scb_leader_aliveness = client->sc_leader_aliveness;
		p->scb_leader_term = client->sc_leader_term;
		p->scb_leader_index = client->sc_leader_index;
		p->scb_next = client->sc_next;
		memcpy(p->scb_ranks, client->sc_ranks->rl_ranks,
		       sizeof(*p->scb_ranks) * client->sc_ranks->rl_nr);
	}
	return len;
}

static void
rsvc_client_buf_swap(struct rsvc_client_buf *buf)
{
	D_SWAP32S(&buf->scb_magic);
	D_SWAP32S(&buf->scb_nranks);
	D_SWAP32S(&buf->scb_leader_known);
	D_SWAP32S(&buf->scb_leader_aliveness);
	D_SWAP64S(&buf->scb_leader_term);
	D_SWAP32S(&buf->scb_leader_index);
	D_SWAP32S(&buf->scb_next);
}

static void
rsvc_client_buf_swap_ranks(struct rsvc_client_buf *buf)
{
	int i;

	D_ASSERT(buf->scb_magic == rsvc_client_buf_magic);
	for (i = 0; i < buf->scb_nranks; i++)
		D_SWAP32S(&buf->scb_ranks[i]);
}

ssize_t
rsvc_client_decode(void *buf, size_t len, struct rsvc_client *client)
{
	struct rsvc_client_buf *p = buf;
	bool			swap = false;

	/* OK to access the struct? */
	if (len < sizeof(*p)) {
		D_ERROR("truncated buffer: %zu < %zu\n", len, sizeof(*p));
		return -DER_IO;
	}
	/* Magic matches? */
	if (p->scb_magic != rsvc_client_buf_magic) {
		if (p->scb_magic == D_SWAP32(rsvc_client_buf_magic)) {
			swap = true;
		} else {
			D_ERROR("bad buffer magic: %x\n", p->scb_magic);
			return -DER_IO;
		}
	}
	/* Swap the struct if needed. */
	if (swap)
		rsvc_client_buf_swap(p);
	/* OK to access the ranks? */
	if (p->scb_nranks == 0) {
		D_ERROR("zero nranks\n");
		return -DER_IO;
	}
	if (len < sizeof(*p) + sizeof(*p->scb_ranks) * p->scb_nranks) {
		D_ERROR("truncated buffer: %zu < %zu\n", len,
			sizeof(*p) + sizeof(*p->scb_ranks) * p->scb_nranks);
		return -DER_IO;
	}
	/* Swap the ranks if needed. */
	if (swap)
		rsvc_client_buf_swap_ranks(p);
	/* Copy the data. */
	client->sc_ranks = daos_rank_list_alloc(p->scb_nranks);
	if (client->sc_ranks == NULL)
		return -DER_NOMEM;
	memcpy(client->sc_ranks->rl_ranks, p->scb_ranks,
	       sizeof(*p->scb_ranks) * p->scb_nranks);
	client->sc_leader_known = p->scb_leader_known == 0 ? false : true;
	client->sc_leader_aliveness = p->scb_leader_aliveness;
	client->sc_leader_term = p->scb_leader_term;
	client->sc_leader_index = p->scb_leader_index;
	client->sc_next = p->scb_next;
	return sizeof(*p) + sizeof(*p->scb_ranks) * p->scb_nranks;
}
