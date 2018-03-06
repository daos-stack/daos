#define DDSUBSYS	DDFAC(tier)

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
	WARMER,
	THIS
};

/*TODO move declaration to a header?
 * Decide on size definition location?
 */
#define				MAX_RANKS 8
static d_rank_t		colder_ranks[MAX_RANKS];
static d_rank_list_t		colder_svc;
static daos_pool_info_t		colder_pool_info;

static d_rank_t		warmer_ranks[MAX_RANKS];
static d_rank_list_t		warmer_svc;
static daos_pool_info_t		warmer_pool_info;

static d_rank_t		this_ranks[MAX_RANKS];
static d_rank_list_t		this_svc;
static daos_pool_info_t		this_pool_info;

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

char *this_grp = NULL;
uuid_t this_id;
daos_handle_t this_poh;
bool this_conn_flg = false;

struct upstream_arg {
	crt_rpc_t	*rpc;
};

static void
ds_tier_init_group(d_rank_list_t *prl, d_rank_t *pr, uint32_t nr)
{
	int j;

	for (j = 0; j < nr; j++)
		pr[j] = j;

	prl->rl_ranks = pr;
	prl->rl_nr = nr;
}

void
ds_tier_init_vars(void)
{
	ds_tier_init_group(&warmer_svc, warmer_ranks, MAX_RANKS);
	ds_tier_init_group(&colder_svc, colder_ranks, MAX_RANKS);
}


/*Broadcast specified handle to all members of current pool*/
static int
poh_bcast(crt_context_t *ctx, const uuid_t pool_id, int hdl_type,
	  daos_handle_t poh)
{
	int rc = 0;
	struct tier_hdl_bcast_in	*b_in;
	struct tier_hdl_bcast_out	*b_out;
	crt_rpc_t			*rpc;
	daos_iov_t			global_hdl;
	void				*glob_buf;

	rc = ds_tier_bcast_create(ctx, pool_id, TIER_BCAST_HDL, &rpc);
	if (rc) {
		D__ERROR("ds_tier_bcast_create returned %d\n", rc);
		D__GOTO(out_nofree, rc);
	}

	/*Get global token handle, set it to null so we get the handle size*/
	global_hdl.iov_buf = NULL;

	/*First get size, then allocate buffer and try again to get handle*/
	daos_pool_local2global(poh, &global_hdl);
	D__ALLOC(glob_buf, global_hdl.iov_buf_len);
	global_hdl.iov_len = global_hdl.iov_buf_len;
	global_hdl.iov_buf = glob_buf;

	daos_pool_local2global(poh, &global_hdl);

	b_in = crt_req_get(rpc);
	b_in->hbi_pool_hdl = global_hdl;
	b_in->hbi_type = hdl_type;

	/*Send BCAST rpc using server side util...*/
	rc = dss_rpc_send(rpc);

	if (rc)
		D__GOTO(out, rc);

	b_out = crt_reply_get(rpc);
	rc = b_out->hbo_ret;
	D_DEBUG(DF_TIERS, "Pool handle broadcast resp: %d", b_out->hbo_ret);

out:
	D__FREE(glob_buf, global_hdl.iov_buf_len);
out_nofree:
	return rc;
}

static int
tier_upstream_cb(tse_task_t *task, void *data)
{
	struct upstream_arg		*arg = (struct upstream_arg *) data;
	int				rc = 0;

	D_DEBUG(DF_TIERS, "Upstream Connection Complete!\n");

	crt_req_decref(arg->rpc);
	return rc;
}


static int
tier_upstream(uuid_t warm_id, char *warm_grp, uuid_t cold_id,
		 char *cold_grp, tse_task_t *upstream_task)
{
	int				rc;
	crt_endpoint_t			cold_tgt;
	crt_rpc_t			*rpc_req = NULL;
	struct tier_upstream_in		*ui_in = NULL;
	static crt_group_t		*tgt_grp;
	struct upstream_arg		*cb_arg;

	/* NOTE freed by callback infrastructure*/
	D__ALLOC_PTR(cb_arg);
	if (cb_arg == NULL) {
		rc = DER_NOMEM;
		D__GOTO(no_cleanup_err, rc);
	}


	D__ALLOC(tgt_grp, sizeof(crt_group_t));
	if (tgt_grp == NULL) {
		rc = DER_NOMEM;
		D__GOTO(no_cleanup_err, rc);
	}


	/* Binding group ID to groupd data structure for use in target
	* as we dont "know" cold for looking things up conveniently
	**/
	rc = daos_group_attach(cold_grp, &tgt_grp);

	if (rc != 0) {
		D__ERROR("Error attaching group: %d\n", rc);
		D__GOTO(no_cleanup_err, rc);
	}

	cold_tgt.ep_grp = tgt_grp;
	cold_tgt.ep_rank = 0;
	cold_tgt.ep_tag = 0;

	rc = tier_req_create(daos_task2ctx(upstream_task), &cold_tgt,
			    TIER_UPSTREAM_CONN, &rpc_req);

	if (rc != 0) {
		D__ERROR("crt_req_create(TIER_UPSTREAM_CONN) failed, rc: %d.\n",
			rc);
		D__GOTO(no_cleanup_err, rc);
	}

	/*Verifying Request is	there.*/
	D__ASSERT(rpc_req != NULL);
	ui_in = crt_req_get(rpc_req);
	D__ASSERT(ui_in != NULL);

	/*Load up the RPC inputs*/
	uuid_copy(ui_in->ui_warm_id, warm_id);
	uuid_copy(ui_in->ui_cold_id, cold_id);
	ui_in->ui_warm_grp = (d_string_t)warm_grp;
	ui_in->ui_cold_grp = (d_string_t)cold_grp;

	crt_req_addref(rpc_req); /*Added for the arg cb*/
	cb_arg->rpc = rpc_req;

	/*Register CB*/
	rc = tse_task_register_comp_cb(upstream_task, tier_upstream_cb, cb_arg,
				       sizeof(struct upstream_arg));
	if (rc) {
		D__ERROR("Callback registration failed: %d", rc);
		D__GOTO(out, rc);
	}

	/*Send the RPC*/
	rc = daos_rpc_send(rpc_req, upstream_task);
	return rc;
out:
	/*Decrement ref count since callback never triggers if we got here*/
	crt_req_decref(cb_arg->rpc);
	/*Free CB arg since it will not be freed via task completions*/
	D__FREE_PTR(cb_arg);
	return rc;
no_cleanup_err:
	return rc;

}

void
ds_tier_cross_conn_handler(crt_rpc_t *rpc)
{
	struct tier_cross_conn_in	*in = crt_req_get(rpc);
	struct tier_cross_conn_out	*out = crt_reply_get(rpc);
	uuid_t				self_pool_id;
	char				*self_srv_grp = NULL;
	int				buf_len;
	daos_event_t			upstream_ev;
	daos_event_t			downstream_ev;
	daos_event_t			this_ev;
	int				rc = 0;
	daos_handle_t			cross_conn_eqh;
	bool				ev_flag;
	tse_task_t			*downstream_task;
	tse_task_t			*upstream_task;
	tse_task_t			*this_task;
	daos_pool_connect_t		*pc_args;

	colder_svc.rl_ranks = colder_ranks;
	colder_svc.rl_nr = MAX_RANKS;

	this_svc.rl_ranks = this_ranks;
	this_svc.rl_nr = MAX_RANKS;

	/* Check if we've actually got a colder group to connect to*/
	/* If not, we're done and time to move on.*/
	if (colder_grp == NULL) {
		D__INFO("No Tier Beneath Current %s:"DF_UUIDF"\n",
		       in->cci_warm_grp, DP_UUID(in->cci_warm_id));

		out->cco_ret = -NO_COLDER;
		D__GOTO(no_conn_out, rc);
	}

	/*Note: this naively assumes all servers are or are not connected*/
	if (colder_conn_flg == true) {
		D__WARN("Downstream (colder) tier connection already made!\n");
		out->cco_ret = -ALREADY_CONN_COLD;
		D__GOTO(no_conn_out, rc);
	}

	/*Initialize the event queue*/
	rc = daos_eq_create(&cross_conn_eqh);
	if (rc) {
		D__ERROR("Failed to Create Event Queue:%d\n", rc);
		D__GOTO(out, rc);
	}

	/* Copying uuid over, in this case the warm ID actually the pool
	* we're currently on, as this is handler does warm->cold before
	* triggering the upstream connection. Note null server group is valid
	* and refers to default grp id
	*/
	uuid_copy(self_pool_id, in->cci_warm_id);
	if (in->cci_warm_grp != NULL) {
		buf_len = strlen((char *)in->cci_warm_grp) + 1;
		D__ALLOC(self_srv_grp, buf_len);
		strcpy(self_srv_grp, in->cci_warm_grp);
	} else {
		self_srv_grp = NULL;
	}


	/*Issue non-blocking connect to the colder tier*/
	rc = daos_event_init(&downstream_ev, cross_conn_eqh, NULL);
	if (rc) {
		D__ERROR("Downstream event init failure: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = daos_event_init(&upstream_ev, cross_conn_eqh, NULL);
	if (rc) {
		D__ERROR("Upstream event init failure: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = daos_event_init(&this_ev, cross_conn_eqh, NULL);
	if (rc) {
		D__ERROR("Upstream event init failure: %d\n", rc);
		D__GOTO(out, rc);
	}

	/*Initialize tasks affiliated with downstream event*/
	rc = dc_task_create(dc_pool_connect, NULL, &downstream_ev,
			    &downstream_task);
	if (rc) {
		D__ERROR("Client Task prep failure: %d\n", rc);
		D__GOTO(out, rc);
	}

	pc_args = dc_task_get_args(downstream_task);
	uuid_copy((unsigned char *)pc_args->uuid, colder_id);
	pc_args->grp	= colder_grp;
	pc_args->svc	= &colder_svc;
	pc_args->flags	= DAOS_PC_RW;
	pc_args->poh	= &colder_poh;
	pc_args->info	= &colder_pool_info;

	dc_task_schedule(downstream_task, true);

	/*Currently a blocking wait, in future may need to change*/
	rc = daos_event_test(&downstream_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D__ERROR("Error waiting for downstream event complete:%d", rc);
		D__GOTO(out, rc);
	}
	daos_event_fini(&downstream_ev);

	/*broadcast colder (downstream) pool handle*/
	rc = poh_bcast(rpc->cr_ctx, self_pool_id, COLDER, colder_poh);
	if (rc) {
		D__ERROR("Cold Handle Broadcast Error: %d\n", rc);
		D__GOTO(out, -HANDLE_BCAST_ERR);
	}

	D_DEBUG(DF_TIERS, "Connect to Colder Tier Group: %s, ID:"DF_UUIDF"\n",
		colder_grp, DP_UUID(colder_id));

	/*Now we do the work for the local connection*/
	 rc = dc_task_create(dc_pool_connect, NULL, &this_ev, &this_task);
	if (rc) {
		D__ERROR("Client Task prep failure: %d\n", rc);
		D__GOTO(out, rc);
	}

	pc_args = dc_task_get_args(this_task);
	uuid_copy((unsigned char *)pc_args->uuid, self_pool_id);
	pc_args->grp	= self_srv_grp;
	pc_args->svc	= &this_svc;
	pc_args->flags	= DAOS_PC_RW;
	pc_args->poh	= &this_poh;
	pc_args->info	= &this_pool_info;
	dc_task_schedule(this_task, true);

	/*Currently a blocking wait, in future may need to change*/
	rc = daos_event_test(&this_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D__ERROR("Error waiting for local event complete:%d", rc);
		D__GOTO(out, rc);
	}
	daos_event_fini(&this_ev);

	/*broadcast colder (downstream) pool handle*/
	rc = poh_bcast(rpc->cr_ctx, self_pool_id, THIS, this_poh);
	if (rc) {
		D__ERROR("Local Handle Broadcast Error: %d\n", rc);
		D__GOTO(out, HANDLE_BCAST_ERR);
	}

	D_DEBUG(DF_TIERS, "Connect to Local Tier Group: %s, ID:"DF_UUIDF"\n",
		this_grp, DP_UUID(this_id));

	/*End of local connection*/

	/*Now we do the task for the upstream (cold to warm) connections*/
	rc = dc_task_create(NULL, NULL, &upstream_ev, &upstream_task);
	if (rc) {
		D__ERROR("Client Task Prep Error for Upstream Task: %d\n", rc);
		D__GOTO(out, rc);
	}

	/* XXX this is a hack and can't work anymore */
	rc = tier_upstream(self_pool_id, self_srv_grp, colder_id,
			 colder_grp, upstream_task);
	/*Note, this may change as we might want more informative RC in future
	 * e.g. tier beneath self identified as coldest, etc.
	*/
	if (rc) {
		D__ERROR("Error from dc_tier_upstream call: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = daos_event_test(&upstream_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D__ERROR("Error waiting for upstream conn event: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = upstream_ev.ev_error;
	if (rc)
		D__ERROR("Upstream Connection Error: %d\n", rc);
	else
		D__INFO("Upstream connection (cold tier to local) complete!\n");



out:
	out->cco_ret = rc;
	rc = crt_reply_send(rpc);
	daos_event_fini(&upstream_ev);
	daos_eq_destroy(cross_conn_eqh, 0);
	D_DEBUG(DF_TIERS, "Leaving ds_ct_hdlr_cross_conn...\n");
	if (self_srv_grp != NULL)
		D__FREE(self_srv_grp, buf_len);
	return;
/* Used for when no connection is being set up
 * e.g. no colder tier, or already exists, return code already set in RPC
 */
no_conn_out:
	crt_reply_send(rpc);
}

void
ds_tier_upstream_handler(crt_rpc_t *rpc)
{
	struct tier_upstream_in		*in = crt_req_get(rpc);
	struct tier_upstream_out	*out = crt_reply_get(rpc);
	int				rc;
	daos_event_t			conn_ev;
	daos_handle_t			upstream_eqh;
	bool				ev_flag;
	struct dc_pool			*pool;
	tse_task_t			*upstream_task;
	daos_pool_connect_t		*pc_args;

	uuid_copy(warmer_id, in->ui_warm_id);
	if (warmer_grp == NULL) {
		crt_group_t *grp;
		uint32_t     grpsz;

		D__ALLOC(warmer_grp, 32);
		strcpy(warmer_grp, in->ui_warm_grp);
		grp = crt_group_lookup(warmer_grp);
		if (grp) {
			rc = crt_group_size(grp, &grpsz);
			if (rc != 0)
				D__ERROR("crt_group_size returned %d\n", rc);
			else {
				D__INFO("warmer_svc has %u ranks\n", grpsz);
				warmer_svc.rl_nr     = grpsz;
			}
		} else
			D_DEBUG(DF_TIERS, "failed to lookup warmer group\n");

	}

	/*Initialize the event queue*/
	rc = daos_eq_create(&upstream_eqh);
	if (rc) {
		D__ERROR("Failed to create event queue:%d\n", rc);
		D__GOTO(out, rc);
	}


	/*Initialize Event*/
	rc = daos_event_init(&conn_ev, upstream_eqh, NULL);
	if (rc) {
		D__ERROR("Event init failure:%d\n", rc);
		D__GOTO(out, rc);
	}

	rc = dc_task_create(dc_pool_connect, NULL, &conn_ev, &upstream_task);
	if (rc) {
		D__ERROR("Client Task Prep Error: %d\n", rc);
		D__GOTO(out, rc);
	}

	pc_args = dc_task_get_args(upstream_task);
	uuid_copy((unsigned char *)pc_args->uuid, in->ui_warm_id);
	pc_args->grp	= in->ui_warm_grp;
	pc_args->svc	= &warmer_svc;
	pc_args->flags	= DAOS_PC_RW;
	pc_args->poh	= &warmer_poh;
	pc_args->info	= &warmer_pool_info;
	/*Connect warmer*/
	dc_task_schedule(upstream_task, true);

	rc = daos_event_test(&conn_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D__ERROR("Error waiting for upstream conn event: %d\n", rc);
		D__GOTO(out, rc);
	}

	D__INFO("Tier: %s upstream connect to pool: "DF_UUIDF"\n",
	       in->ui_cold_grp, DP_UUID(in->ui_warm_id));

	/*Eventually replace with dss_async if/when that lands*/
	rc = daos_event_test(&conn_ev, DAOS_EQ_WAIT, &ev_flag);
	if (rc) {
		D__ERROR("Daos Event Test Error: %d\n", rc);
		D__GOTO(out, rc);
	}

	pool = dc_hdl2pool(warmer_poh);

	D_DEBUG(DF_TIERS, "UUID of Warmer POH:"DF_UUIDF"\n",
		DP_UUID(pool->dp_pool));
	D_DEBUG(DF_TIERS, "Tier/Group of Warmer: %s\n",
		pool->dp_group->cg_grpid);

	/*Note, in this case the cold ID is local, as this is upstream hdlr*/
	rc = poh_bcast(rpc->cr_ctx, in->ui_cold_id, WARMER, warmer_poh);
	if (rc)
		D__ERROR("Cold Handle Broadcast Error: %d\n", rc);

out:
	out->uo_ret = rc;
	crt_reply_send(rpc);
}

void
ds_tier_register_cold_handler(crt_rpc_t *rpc)
{
	struct tier_register_cold_in	*in = crt_req_get(rpc);
	struct tier_register_cold_out	*out = crt_reply_get(rpc);
	int rc;

	/*Note assumes non-default name of colder group*/
	if (colder_grp == NULL) {
		crt_group_t *grp;
		uint32_t     grpsz;

		uuid_copy(colder_id, in->rci_colder_id);
		D__ALLOC(colder_grp, 32);
		strcpy(colder_grp, in->rci_colder_grp);
		out->rco_ret = 0;
		grp = crt_group_lookup(colder_grp);
		if (grp) {
			rc = crt_group_size(grp, &grpsz);
			if (rc != 0)
				D__ERROR("crt_group_size returned %d\n", rc);
			else {
				D__INFO("colder_svc has %u ranks\n", grpsz);
				colder_svc.rl_nr     = grpsz;
			}
		} else
			D_DEBUG(DF_TIERS, "fail to lookup colder group\n");
	} else {
		D__WARN("Colder Group already set to: %s\n", colder_grp);
		D__WARN("Ignoring Colder Tier Set Request\n");
		out->rco_ret = -COLD_ALREADY_SET;
	}

	D__INFO("Registered Colder Handle!\n");
	crt_reply_send(rpc);
}

void
ds_tier_hdl_bcast_handler(crt_rpc_t *rpc) {

	struct tier_hdl_bcast_out *out;
	struct tier_hdl_bcast_in  *in;

	in = crt_req_get(rpc);
	out = crt_reply_get(rpc);

	/*Set the appropriate handle, or return error if hbi_type is wrong*/
	if (in->hbi_type == WARMER) {
		D__INFO("Setting Inter-Tier Warmer Pool Handle\n");
		daos_pool_global2local(in->hbi_pool_hdl, &warmer_poh);
		warmer_conn_flg = true;
		out->hbo_ret = 0;

	} else if (in->hbi_type == COLDER) {
		D__INFO("Setting Inter-Tier Colder Pool Handle\n");
		daos_pool_global2local(in->hbi_pool_hdl, &colder_poh);
		colder_conn_flg = true;
		out->hbo_ret = 0;
	} else if (in->hbi_type == THIS) {
		D__INFO("Setting Local-Tier  Pool Handle\n");
		daos_pool_global2local(in->hbi_pool_hdl, &this_poh);
		this_conn_flg = true;
		out->hbo_ret = 0;
	} else {
		out->hbo_ret = HANDLE_BCAST_ERR;
	}

	crt_reply_send(rpc);
}
