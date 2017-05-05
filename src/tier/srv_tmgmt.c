#define DD_SUBSYS	DD_FAC(tier)

#include <daos.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos/pool.h>
#include <daos/rpc.h>
#include <daos/tier.h>
#include "rpc.h"
#include "../client/client_internal.h"
#include "../client/task_internal.h"
#include "srv_internal.h"
#include <daos_srv/daos_ct_srv.h>

/*Used for identify pool handle type*/
enum hdl_type {
	COLDER,
	WARMER
};

/*TODO move declaration to a header?
 * Decide on size definition location?
 */
static daos_rank_t		colder_ranks[8];
static daos_rank_list_t		colder_svc;
static daos_pool_info_t		colder_pool_info;

static daos_rank_t		warmer_ranks[8];
static daos_rank_list_t		warmer_svc;
static daos_pool_info_t		warmer_pool_info;

/* These are extern declared in  srv_internal
 * consider definition move elsewhere as this is a little odd
 * though having build issues defining/initializing elsewhere.
 */
char *colder_grp = NULL;
uuid_t colder_id;
daos_handle_t colder_poh;
bool colder_conn_flg = false;

char *warmer_grp = NULL;
uuid_t warmer_id;
daos_handle_t warmer_poh;
bool warmer_conn_flg = false;

struct upstream_arg {
	crt_rpc_t	*rpc;
};


/*Broadcast specified handle to all members of current pool*/
static int
poh_bcast(crt_context_t *ctx, const uuid_t pool_id, int hdl_type,
	  daos_handle_t poh)
{
	int rc = 0;
	struct ds_pool			*srv_pool;
	struct tier_hdl_bcast_in	*b_in;
	struct tier_hdl_bcast_out	*b_out;
	crt_rpc_t			*rpc;
	daos_iov_t			global_hdl;
	void				*glob_buf;

	srv_pool = ds_pool_lookup(pool_id);
	if (srv_pool != NULL) {
		D_DEBUG(DF_TIERS, "UUID from ds_pool struct:"DF_UUIDF"\n",
			DP_UUID(srv_pool->sp_uuid));
	} else {
		D_ERROR("Return of ds_pool lookup is NULL\n");
		return -1;

	}

	/*Borrowing the pool bcast create call*/
	ds_pool_bcast_create(ctx, srv_pool, DAOS_TIER_MODULE, TIER_BCAST_HDL,
			     &rpc, NULL, NULL);

	/*Get global token handle, set it to null so we get the handle size*/
	global_hdl.iov_buf = NULL;

	/*First get size, then allocate buffer and try again to get handle*/
	daos_pool_local2global(poh, &global_hdl);
	D_ALLOC(glob_buf, global_hdl.iov_buf_len);
	global_hdl.iov_len = global_hdl.iov_buf_len;
	global_hdl.iov_buf = glob_buf;

	daos_pool_local2global(poh, &global_hdl);

	b_in = crt_req_get(rpc);
	b_in->hbi_pool_hdl = global_hdl;
	b_in->hbi_type =	hdl_type;

	/*Send BCAST rpc using server side util...*/
	rc = dss_rpc_send(rpc);

	if (rc)
		D_GOTO(out, rc);

	b_out = crt_reply_get(rpc);
	rc = b_out->hbo_ret;
	D_DEBUG(DF_TIERS, "Pool handle broadcast resp: %d", b_out->hbo_ret);

out:
	D_FREE(glob_buf, global_hdl.iov_buf_len);
	return rc;
}

static int
tier_upstream_cb(struct daos_task *task, void *data)
{
	struct upstream_arg		*arg = (struct upstream_arg *) data;
	int				rc = 0;

	D_DEBUG(DF_TIERS, "Upstream Connection Complete!\n");

	crt_req_decref(arg->rpc);
	return rc;
}


static int
tier_upstream(uuid_t warm_id, char *warm_grp, uuid_t cold_id,
		 char *cold_grp, struct daos_task *upstream_task)
{
	D_ERROR("Entered tier_upstream\n");
	int				rc;
	crt_endpoint_t			cold_tgt;
	crt_rpc_t			*rpc_req = NULL;
	struct tier_upstream_in		*ui_in = NULL;
	static crt_group_t		*tgt_grp;
	struct upstream_arg		*cb_arg;

	/* TODO where does this get freed?*/
	D_ALLOC_PTR(cb_arg);
	if (cb_arg == NULL) {
		rc = DER_NOMEM;
		D_GOTO(no_cleanup_err, rc);
	}


	D_ALLOC(tgt_grp, sizeof(crt_group_t));
	if (tgt_grp == NULL) {
		rc = DER_NOMEM;
		D_GOTO(no_cleanup_err, rc);
	}


	/* Binding group ID to groupd data structure for use in target
	* as we dont "know" cold for looking things up conveniently
	**/
	rc = daos_group_attach(cold_grp, &tgt_grp);

	if (rc != 0) {
		D_ERROR("Error attaching group: %d\n", rc);
		D_GOTO(no_cleanup_err, rc);

	}

	cold_tgt.ep_grp = tgt_grp;
	cold_tgt.ep_rank = 0;
	cold_tgt.ep_tag = 0;

	rc = tier_req_create(daos_task2ctx(upstream_task), cold_tgt,
			    TIER_UPSTREAM_CONN, &rpc_req);

	if (rc != 0) {
		D_ERROR("crt_req_create(TIER_UPSTREAM_CONN) failed, rc: %d.\n",
			rc);
		D_GOTO(no_cleanup_err, rc);
	}

	/*Verifying Request is	there.*/
	D_ASSERT(rpc_req != NULL);
	ui_in = crt_req_get(rpc_req);
	D_ASSERT(ui_in != NULL);

	/*Load up the RPC inputs*/
	uuid_copy(ui_in->ui_warm_id, warm_id);
	uuid_copy(ui_in->ui_cold_id, cold_id);
	ui_in->ui_warm_grp = (crt_string_t)warm_grp;
	ui_in->ui_cold_grp = (crt_string_t)cold_grp;

	crt_req_addref(rpc_req); /*Added for the arg cb*/
	cb_arg->rpc = rpc_req;

	/*Register CB*/
	rc = daos_task_register_comp_cb(upstream_task, tier_upstream_cb, cb_arg,
					sizeof(struct upstream_arg));
	if (rc) {
		D_ERROR("Callback registration failed: %d", rc);
		D_GOTO(out, rc);
	}

	/*Send the RPC*/
	rc = daos_rpc_send(rpc_req, upstream_task);
	D_INFO("Leaving dc_tier_upstream...\n");
	return rc;
out:
	/*Decrement ref count since callback never triggers if we got here*/
	crt_req_decref(cb_arg->rpc);
	return rc;
no_cleanup_err:
	return rc;

}


int
ds_tier_hdlr_cross_conn(crt_rpc_t *rpc)
{
	struct tier_cross_conn_in	*in = crt_req_get(rpc);
	struct tier_cross_conn_out	*out = crt_reply_get(rpc);
	uuid_t				self_pool_id;
	char				*self_srv_grp = NULL;
	int				buf_len;
	daos_event_t			upstream_ev;
	daos_event_t			downstream_ev;
	daos_event_t			*upstream_evp = &upstream_ev;
	daos_event_t			*downstream_evp = &downstream_ev;
	int				rc = 0;
	daos_handle_t			cross_conn_eqh;
	bool				ev_flag;
	struct daos_task		*downstream_task;
	struct daos_task		*upstream_task;
	struct daos_task_args   *dta;

	colder_svc.rl_ranks = colder_ranks;

	/* Check if we've actually got a colder group to connect to*/
	/* If not, we're done and time to move on.*/
	if (colder_grp == NULL) {
		D_INFO("No Tier Beneath Current %s:"DF_UUIDF"\n",
		       in->cci_warm_grp, DP_UUID(in->cci_warm_id));

		out->cco_ret = NO_COLDER;
		D_GOTO(no_conn_out, rc);
	}

	/*Note: this naively assumes all servers are or are not connected*/
	if (colder_conn_flg == true) {
		D_WARN("Downstream (colder) tier connection already made!\n");
		out->cco_ret = ALREADY_CONN_COLD;
		D_GOTO(no_conn_out, rc);
	}

	/*Initialize the event queue*/
	rc = daos_eq_create(&cross_conn_eqh);
	if (rc) {
		D_ERROR("Failed to Create Event Queue:%d\n", rc);
		D_GOTO(out, rc);
	}

	/* Copying uuid over, in this case the warm ID actually the pool
	* we're currently on, as this is handler does warm->cold before
	* triggering the upstream connection. Note null server group is valid
	* and refers to default grp id
	*/
	uuid_copy(self_pool_id, in->cci_warm_id);
	if (in->cci_warm_grp != NULL) {
		buf_len = strlen((char *)in->cci_warm_grp) + 1;
		D_ALLOC(self_srv_grp, buf_len);
		strcpy(self_srv_grp, in->cci_warm_grp);
	} else {
		self_srv_grp = NULL;
	}


	/*Issue non-blocking connect to the colder tier*/
	rc = daos_event_init(&downstream_ev, cross_conn_eqh, NULL);
	if (rc) {
		D_ERROR("Downstream event init failure: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_event_init(&upstream_ev, cross_conn_eqh, NULL);
	if (rc) {
		D_ERROR("Upstream event init failure: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*Initialize tasks affiliated with event*/
	rc = daos_client_task_prep(NULL, 0, &downstream_task, &downstream_evp);

	if (rc) {
		D_ERROR("Client Task prep failure: %d\n", rc);
		D_GOTO(out, rc);
	}

	dta = daos_task_buf_get(downstream_task, sizeof(*dta));
	dta->opc = DAOS_OPC_POOL_CONNECT;
	uuid_copy((unsigned char *)dta->op_args.pool_connect.uuid, colder_id);
	dta->op_args.pool_connect.grp = colder_grp;
	dta->op_args.pool_connect.svc = &colder_svc;
	dta->op_args.pool_connect.flags = DAOS_PC_RW;
	dta->op_args.pool_connect.poh = &colder_poh;
	dta->op_args.pool_connect.info = &colder_pool_info;

	rc = dc_pool_connect(downstream_task);

	if (rc) {
		D_WARN("Downstream Tier Connection DC Call Failed: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*Currently a blocking wait for a temp hack, in future needs to change*/
	rc = daos_event_test(&downstream_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D_ERROR("Error waiting for downstream event complete:%d", rc);
		D_GOTO(out, rc);
	}
	daos_event_fini(&downstream_ev);

	/*broadcast colder (downstream) pool handle*/
	rc = poh_bcast(rpc->cr_ctx, self_pool_id, COLDER, colder_poh);
	if (rc)
		D_WARN("Cold Handle Broadcast Error: %d\n", rc);

	D_DEBUG(DF_TIERS, "Connect to Colder Tier Group: %s, ID:"DF_UUIDF"\n",
		colder_grp, DP_UUID(colder_id));

	rc = daos_client_task_prep(NULL, 0, &upstream_task, &upstream_evp);

	rc = tier_upstream(self_pool_id, self_srv_grp, colder_id,
			 colder_grp, upstream_task);
	/*Note, this may change as we might want more informative RC in future
	 * e.g. tier beneath self identified as coldest, etc.
	*/
	if (rc) {
		D_ERROR("Error from dc_tier_upstream call: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_event_test(&upstream_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D_ERROR("Error waiting for upstream conn event: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = upstream_ev.ev_error;

	if (rc)
		D_ERROR("Upstream Connection Error: %d\n", rc);

	else
		D_INFO("Upstream connection (cold tier to local) complete!\n");

out:
	out->cco_ret = rc;
	rc = crt_reply_send(rpc);
	daos_event_fini(&upstream_ev);
	daos_eq_destroy(cross_conn_eqh, 0);
	D_DEBUG(DF_TIERS, "Leaving ds_ct_hdlr_cross_conn...\n");
	if (self_srv_grp != NULL)
		D_FREE(self_srv_grp, buf_len);
	return rc;
/* Used for when no connection is being set up
 * e.g. no colder tier, or already exists, return code already set in RPC
 */
no_conn_out:
	rc = crt_reply_send(rpc);
	return rc;
}

int
ds_tier_hdlr_upstream(crt_rpc_t *rpc)
{
	struct tier_upstream_in		*in = crt_req_get(rpc);
	struct tier_upstream_out	*out = crt_reply_get(rpc);
	int				rc;
	daos_event_t			conn_ev;
	daos_event_t			*conn_evp = &conn_ev;
	daos_handle_t			upstream_eqh;
	bool				ev_flag;
	struct dc_pool			*pool;
	struct daos_task		*upstream_task;
	struct daos_task_args		*dta;

	/*Temp hack*/
	warmer_svc.rl_ranks = warmer_ranks;

	uuid_copy(warmer_id, in->ui_warm_id);

	/*Initialize the event queue*/
	rc = daos_eq_create(&upstream_eqh);
	if (rc) {
		D_ERROR("Failed to create event queue:%d\n", rc);
		D_GOTO(out, rc);
	}


	/*Initialize Event*/
	rc = daos_event_init(&conn_ev, upstream_eqh, NULL);
	if (rc) {
		D_ERROR("Event init failure:%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_client_task_prep(NULL, 0, &upstream_task, &conn_evp);

	dta = daos_task_buf_get(upstream_task, sizeof(*dta));
	dta->opc = DAOS_OPC_POOL_CONNECT;
	uuid_copy((unsigned char *)dta->op_args.pool_connect.uuid,
		  in->ui_warm_id);
	dta->op_args.pool_connect.grp = in->ui_warm_grp;
	dta->op_args.pool_connect.svc = &warmer_svc;
	dta->op_args.pool_connect.flags = DAOS_PC_RW;
	dta->op_args.pool_connect.poh = &warmer_poh;
	dta->op_args.pool_connect.info = &warmer_pool_info;

	/*Connect warmer*/
	rc = dc_pool_connect(upstream_task);
	if (rc) {
		D_ERROR("Error in dc_pool_connect: %d\n", rc);
		D_GOTO(out, rc);
	}


	rc = daos_event_test(&conn_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D_ERROR("Error waiting for upstream conn event: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_INFO("Tier: %s upstream connect to pool: "DF_UUIDF"\n",
	       in->ui_cold_grp, DP_UUID(in->ui_warm_id));

	/*Eventually replace with dss_async if/when that lands*/
	rc = daos_event_test(&conn_ev, DAOS_EQ_WAIT, &ev_flag);


	pool = dc_hdl2pool(warmer_poh);

	D_DEBUG(DF_TIERS, "UUID of Warmer POH:"DF_UUIDF"\n",
		DP_UUID(pool->dp_pool));
	D_DEBUG(DF_TIERS, "Tier/Group of Warmer: %s\n",
		pool->dp_group->cg_grpid);

	/*Note, in this case the cold ID is local, as this is upstream hdlr*/
	poh_bcast(rpc->cr_ctx, in->ui_cold_id, WARMER, warmer_poh);

out:
	out->uo_ret = rc;
	rc = crt_reply_send(rpc);
	return rc;
}

int
ds_tier_hdlr_register_cold(crt_rpc_t *rpc)
{
	struct tier_register_cold_in	*in = crt_req_get(rpc);
	struct tier_register_cold_out	*out = crt_reply_get(rpc);
	int rc = 0;

	/*Note assumes non-default name of colder group*/
	if (colder_grp == NULL) {
		uuid_copy(colder_id, in->rci_colder_id);
		D_ALLOC(colder_grp, 32);
		strcpy(colder_grp, in->rci_colder_grp);
	} else {
		D_DEBUG(DF_TIERS, "Colder Group already set: %s\n", colder_grp);

	}

	out->rco_ret = 0;

	D_INFO("Registered Colder Handle!\n");
	rc = crt_reply_send(rpc);

	return rc;
}

int
ds_tier_hdlr_hdl_bcast(crt_rpc_t *rpc) {

	struct tier_hdl_bcast_out *out;
	struct tier_hdl_bcast_in  *in;

	in = crt_req_get(rpc);

	/*Set the appropriate handle*/
	if (in->hbi_type == WARMER) {
		D_INFO("Setting Inter-Tier Warmer Pool Handle\n");
		daos_pool_global2local(in->hbi_pool_hdl, &warmer_poh);
		warmer_conn_flg = true;

	} else {
		D_INFO("Setting Inter-Tier Colder Pool Handle\n");
		daos_pool_global2local(in->hbi_pool_hdl, &colder_poh);
		colder_conn_flg = true;
	}


	out = crt_reply_get(rpc);
	out->hbo_ret = 0;
	crt_reply_send(rpc);

	return 0;
}
