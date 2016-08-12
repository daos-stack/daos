/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of CaRT. It gives out the internal data structure of group.
 */

#ifndef __CRT_GROUP_H__
#define __CRT_GROUP_H__

#include <crt_pmix.h>

/* TODO export the group name to user? and multiple client groups? */
#define CRT_GLOBAL_SRV_GROUP_NAME	"crt_global_srv_group"
#define CRT_CLI_GROUP_NAME		"crt_cli_group"

enum crt_grp_status {
	CRT_GRP_CREATING = 0x66,
	CRT_GRP_NORMAL,
	CRT_GRP_DESTROYING,
};

struct crt_grp_priv {
	crt_list_t		 gp_link; /* link to crt_grp_list */
	crt_group_t		 gp_pub; /* public grp handle */
	crt_rank_list_t		*gp_membs; /* member ranks in global group */
	/* the priv pointer user passed in for crt_group_create */
	void			*gp_priv;
	/* CaRT context only for sending sub-grp create/destroy RPCs */
	crt_context_t		 gp_ctx;
	enum crt_grp_status	 gp_status; /* group status */

	uint32_t		 gp_size; /* size (number of membs) of group */
	crt_rank_t		 gp_self; /* self rank in this group */
	crt_rank_t		 gp_psr; /* PSR rank in attached group */
	uint32_t		 gp_primary:1, /* flag of primary group */
				 gp_local:1, /* flag of local group, false means
					      * attached remote group */
				 gp_service:1; /* flag of service group */

	/* TODO: reuse crt_corpc_info here */
	/*
	 * Some temporary info used for group creating/destroying, valid when
	 * gp_status is CRT_GRP_CREATING or CRT_GRP_DESTROYING.
	 */
	struct crt_rpc_priv	*gp_parent_rpc; /* parent RPC, NULL on root */
	crt_list_t		 gp_child_rpcs; /* child RPCs list */
	uint32_t		 gp_child_num;
	uint32_t		 gp_child_ack_num;
	int			 gp_rc; /* temporary recoded return code */
	crt_rank_list_t		*gp_failed_ranks; /* failed ranks */

	crt_grp_create_cb_t	 gp_create_cb; /* grp create completion cb */
	crt_grp_destroy_cb_t	 gp_destroy_cb; /* grp destroy completion cb */
	void			*gp_destroy_cb_arg;

	pthread_mutex_t		 gp_mutex; /* protect all fields above */
};

/* structure of global group data */
struct crt_grp_gdata {
	/* PMIx related global data */
	struct crt_pmix_gdata	*gg_pmix;

	/* client-side primary group, only meaningful for client */
	struct crt_grp_priv	*gg_cli_pri_grp;
	/* server-side primary group */
	struct crt_grp_priv	*gg_srv_pri_grp;

	/* client side group list attached by, only meaningful for server */
	crt_list_t		 gg_cli_grps_attached;
	/* server side group list attached to */
	crt_list_t		 gg_srv_grps_attached;

	/* TODO: move crt_grp_list here */
	/* sub-grp list, only meaningful for server */
	crt_list_t		 gg_sub_grps;
	/* some flags */
	uint32_t		 gg_inited:1, /* all initialized */
				 gg_pmix_inited:1; /* PMIx initialized */
	/* rwlock to protect above fields */
	pthread_rwlock_t	 gg_rwlock;
};

crt_group_id_t crt_global_grp_id(void);
int crt_hdlr_grp_create(crt_rpc_t *rpc_req);
int crt_hdlr_grp_destroy(crt_rpc_t *rpc_req);
/* TODO refine and export crt_group_attach */
int crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);
int crt_grp_lookup(struct crt_grp_priv *grp_priv, crt_rank_t rank, char **uri);
int crt_grp_init(void);
int crt_grp_fini(void);

static inline bool
crt_ep_identical(crt_endpoint_t *ep1, crt_endpoint_t *ep2)
{
	C_ASSERT(ep1 != NULL);
	C_ASSERT(ep2 != NULL);
	/* TODO: check group */
	if (ep1->ep_rank == ep2->ep_rank)
		return true;
	else
		return false;
}

static inline void
crt_ep_copy(crt_endpoint_t *dst_ep, crt_endpoint_t *src_ep)
{
	C_ASSERT(dst_ep != NULL);
	C_ASSERT(src_ep != NULL);
	/* TODO: copy grp id */
	dst_ep->ep_rank = src_ep->ep_rank;
	dst_ep->ep_tag = src_ep->ep_tag;
}

#endif /* __CRT_GROUP_H__ */
