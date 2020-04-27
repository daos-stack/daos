/*
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * \file
 *
 * rsvc: Replicated Service Client
 */

#ifndef DAOS_RSVC_H
#define DAOS_RSVC_H

#include <daos_types.h>

/** Flags in rsvc_hint::sh_flags (opaque) */
enum rsvc_hint_flag {
	RSVC_HINT_VALID	= 1	/* sh_term and sh_rank contain valid info */
};

/** Leadership information (opaque) */
struct rsvc_hint {
	uint32_t	sh_flags;	/* enum rsvc_hint_flag */
	d_rank_t	sh_rank;	/* leader rank (must match sh_term) */
	uint64_t	sh_term;	/* leader term (must match sh_rank) */
};

/** Replicated service client (opaque) */
struct rsvc_client {
	d_rank_list_t  *sc_ranks;		/* of rsvc replicas */
	bool		sc_leader_known;	/* cache nonempty */
	unsigned int	sc_leader_aliveness;	/* 0 means dead */
	uint64_t	sc_leader_term;
	int		sc_leader_index;	/* in sc_ranks */
	int		sc_next;		/* in sc_ranks */
};

/** Return code of rsvc_client_complete_rpc() */
enum rsvc_client_complete_rpc_rc {
	RSVC_CLIENT_PROCEED	= 0,	/**< proceed to process the reply */
	RSVC_CLIENT_RECHOOSE		/**< rechoose and send a new RPC */
};

int rsvc_client_init(struct rsvc_client *client, const d_rank_list_t *ranks);
void rsvc_client_fini(struct rsvc_client *client);
int rsvc_client_choose(struct rsvc_client *client, crt_endpoint_t *ep);
int rsvc_client_complete_rpc(struct rsvc_client *client,
			     const crt_endpoint_t *ep, int rc_crt, int rc_svc,
			     const struct rsvc_hint *hint);
size_t rsvc_client_encode(const struct rsvc_client *client, void *buf);
ssize_t rsvc_client_decode(void *buf, size_t len, struct rsvc_client *client);

#endif /* DAOS_RSVC_H */
