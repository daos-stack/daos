/*
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rsvc: Replicated Service Client
 */

#ifndef DAOS_RSVC_H
#define DAOS_RSVC_H

#include <daos_types.h>

/** Flags in rsvc_hint::sh_flags (opaque) */
enum rsvc_hint_flag {
	RSVC_HINT_VALID = 1 /* sh_term and sh_rank contain valid info */
};

/** Leadership information (opaque) */
struct rsvc_hint {
	uint32_t sh_flags; /* enum rsvc_hint_flag */
	d_rank_t sh_rank;  /* leader rank (must match sh_term) */
	uint64_t sh_term;  /* leader term (must match sh_rank) */
};

/** Replicated service client (opaque) */
struct rsvc_client {
	d_rank_list_t *sc_ranks;            /* of rsvc replicas */
	bool           sc_leader_known;     /* cache nonempty */
	unsigned int   sc_leader_aliveness; /* 0 means dead */
	uint64_t       sc_leader_term;
	int            sc_leader_index; /* in sc_ranks */
	int            sc_next;         /* in sc_ranks */
};

/** Return code of rsvc_client_complete_rpc() */
enum rsvc_client_complete_rpc_rc {
	RSVC_CLIENT_PROCEED = 0, /**< proceed to process the reply */
	RSVC_CLIENT_RECHOOSE     /**< rechoose and send a new RPC */
};

int
rsvc_client_init(struct rsvc_client *client, const d_rank_list_t *ranks);
void
rsvc_client_fini(struct rsvc_client *client);
int
rsvc_client_choose(struct rsvc_client *client, crt_endpoint_t *ep);
int
rsvc_client_complete_rpc(struct rsvc_client *client, const crt_endpoint_t *ep, int rc_crt,
			 int rc_svc, const struct rsvc_hint *hint);
size_t
rsvc_client_encode(const struct rsvc_client *client, void *buf);
ssize_t
rsvc_client_decode(void *buf, size_t len, struct rsvc_client *client);

#endif /* DAOS_RSVC_H */
