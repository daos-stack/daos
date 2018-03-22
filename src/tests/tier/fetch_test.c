#define DDSUBSYS	DDFAC(tests)

#include <getopt.h>
#include "daos_test.h"
#include <stdio.h>
#include "tier_test.h"
#include <sys/types.h>
#include <unistd.h>
#include <daos/debug.h>
#include "daos_iotest.h"


char USAGE[] = "ds_fetch_test <warm_tier_group> <cold_tier_group>";
char TIER_1_ID[] = "TIER_1_ID";
char TIER_2_ID[] = "TIER_2_ID";

/*Small helpder func*/
static int
pool_create(const char *grp_id, uuid_t pool_id, d_rank_list_t *svc)
{
	int rc = 0;

	rc = daos_pool_create(0731, geteuid(), getegid(), grp_id,
				      NULL, "pmem", 256 << 22, svc,
				      pool_id, NULL);
	return rc;
}

void
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type, test_arg_t *arg)
{
	int rc;
	int i;
	bool ev_flag;

	memset(req, 0, sizeof(*req));

	req->iod_type = iod_type;
	req->arg = arg;
	if (arg->async) {
		rc = daos_event_init(&req->ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	arg->expect_result = 0;
	daos_fail_loc_set(arg->fail_loc);
	daos_fail_value_set(arg->fail_value);

	/* init sgl */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		req->sgl[i].sg_nr = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init csum */
	daos_csum_set(&req->csum, &req->csum_buf[0], UPDATE_CSUM_SIZE);

	/* init record extent */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		int j;

		for (j = 0; j < IOREQ_IOD_NR; j++) {
			req->rex[i][j].rx_nr = 1;
			req->rex[i][j].rx_idx = 0;

			/** epoch range: required by the wire format */
			req->erange[i][j].epr_lo = 0;
			req->erange[i][j].epr_hi = DAOS_EPOCH_MAX;
		}

		/* I/O descriptor */
		req->iod[i].iod_recxs = req->rex[i];
		req->iod[i].iod_nr = IOREQ_IOD_NR;

		/* epoch descriptor */
		req->iod[i].iod_eprs = req->erange[i];

		req->iod[i].iod_kcsum.cs_csum = NULL;
		req->iod[i].iod_kcsum.cs_buf_len = 0;
		req->iod[i].iod_kcsum.cs_len = 0;
		req->iod[i].iod_type = iod_type;

	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(coh, oid, 0, 0, &req->oh,
			   req->arg->async ? &req->ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
	}
}

void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = daos_obj_close(req->oh, NULL);
	assert_int_equal(rc, 0);

	req->arg->fail_loc = 0;
	req->arg->fail_value = 0;
	daos_fail_loc_set(0);
	if (req->arg->async) {
		rc = daos_event_fini(&req->ev);
		assert_int_equal(rc, 0);
	}
}

void
debug(char *msg)
{
	char ch;
	pid_t pid = getpid();

	fflush(stdin);
	print_message("(%d): %s press CR to continue", pid, msg);
	ch = fgetc(stdin);
	if (ch != '\n')
		fflush(stdin);

}

daos_epoch_t g_epoch;

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	/** akey */
	for (i = 0; i < nr; i++)
		daos_iov_set(&req->iod[i].iod_name, (void *)akey[i],
			     strlen(akey[i]));
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value,
		     daos_size_t *size, int nr)
{
	daos_sg_list_t *sgl = req->sgl;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		daos_iov_set(&sgl[i].sg_iovs[0], value[i], size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *size,
		     uint64_t *idx, daos_epoch_t *epoch, int nr)
{
	daos_iod_t *iod = req->iod;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		/** record extent */
		iod[i].iod_type = req->iod_type;
		iod[i].iod_size = size[i];
		if (req->iod_type == DAOS_IOD_ARRAY) {
			iod[i].iod_recxs[0].rx_idx = idx[i] + i * SEGMENT_SIZE;
			iod[i].iod_recxs[0].rx_nr = 1;
		}

		/** XXX: to be fixed */
		iod[i].iod_eprs[0].epr_lo = *epoch;
		iod[i].iod_nr = 1;

		D_DEBUG(DF_TIERS,
			"%d: typ:%d sz:%lu idx:"DF_U64" nr:"DF_U64"\n",
			i, iod[i].iod_type, iod[i].iod_size,
			iod[i].iod_recxs[0].rx_idx,
			iod[i].iod_recxs[0].rx_nr);
	}
}

static void
lookup_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_iod_t *iods, daos_epoch_t epoch, struct ioreq *req)
{
	bool ev_flag;
	int rc;

	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, epoch, dkey, nr, iods, sgls,
			    NULL, req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		assert_int_equal(rc, req->arg->expect_result);
		return;
	}

	/** wait for fetch completion */
	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_int_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	assert_int_equal(req->ev.ev_error, req->arg->expect_result);
	/* Only single iov for each sgls during the test */
	assert_int_equal(sgls->sg_nr_out, 1);
}

void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
	daos_size_t *read_size, void **val, daos_size_t *size,
	daos_epoch_t *epoch, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_akey_set(req, akey, nr);

	/* set sgl */
	ioreq_sgl_simple_set(req, val, size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, read_size, idx, epoch, nr);

	lookup_internal(&req->dkey, nr, req->sgl, req->iod, *epoch, req);
}

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY;

	lookup(dkey, 1, &akey, &idx, &read_size, &val, &size, &epoch, req);
}


int
main(int argc, char **argv)
{
	int rank;
	int size;
	int rc;

	int warm_id_len;
	int cold_id_len;

	char *warm_grp;
	char *cold_grp;

	daos_event_t	ev;
	daos_event_t	*evp;
	test_arg_t	arg;




	/*IDs and daos (not MPI) rank info*/
	uuid_t warm_uuid;
	d_rank_t warm_ranks[1];
	d_rank_list_t warm_svc;

	uuid_t cold_uuid;

	warm_ranks[0] = 0;
	warm_svc.rl_nr = 1;
	warm_svc.rl_ranks = warm_ranks;

	/*Pool handles and info*/
	daos_handle_t warm_poh;
	daos_pool_info_t warm_pool_info;
	struct ioreq req;
	char         *buf;


	struct tier_info tinfo;
	daos_epoch_t ep = 9;

	g_epoch = ep;

	if (argc != 3) {
		print_message("Incorrect number of args. %s\n", USAGE);
		return -1;
	}
	debug("Fetch whole container: ready to begin");
	tinfo_init(&tinfo, NULL, 0);

	print_message("Getting cold tier pool UUID and container UUID\n");
	rc = parse_info_file("cold_tier.info", &tinfo);
	if (rc) {
		print_message("failed to read cold_tier.info\n");
		return -1;
	}
	warm_id_len = strlen(argv[1]);
	cold_id_len = strlen(argv[2]);

	warm_grp = malloc(warm_id_len + 1);
	cold_grp = malloc(cold_id_len + 1);

	strcpy(warm_grp, argv[1]);
	strcpy(cold_grp, argv[2]);

	print_message("Warm-Tier Group: %s\n", warm_grp);
	print_message("Cold-Tier Group: %s\n", cold_grp);
	memset(&arg, 0, sizeof(arg));

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	arg.myrank = rank;
	arg.rank_size = size;
	arg.svc.rl_nr = 1;
	arg.svc.rl_ranks = arg.ranks;
	arg.multi_rank = 0;
	arg.mode = 0731;
	arg.uid = geteuid();
	arg.gid = getegid();

	arg.group = warm_grp;
	uuid_clear(arg.pool_uuid);
	uuid_clear(arg.co_uuid);

	arg.hdl_share = false;
	arg.poh = DAOS_HDL_INVAL;
	arg.coh = DAOS_HDL_INVAL;

	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_init();

	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	/*Initialze event and queue*/
	rc = daos_eq_create(&arg.eq);
	if (rc) {
		print_message("EQ Create Failed");
		D__GOTO(out, rc);
	}

	rc = daos_event_init(&ev, arg.eq, NULL);

	if (rank == 0) {
		print_message("Creating WARM tier pool\n");
		/* create pools with minimal size */
		rc = pool_create(warm_grp, warm_uuid, &warm_svc);

		if (rc) {
			print_message("Warm Pool Create Failed: %d\n", rc);
			goto out;
		} else {
			print_message("Warm Pool Created\n");
		}
		/* cold tier pool is already created */
		uuid_copy(cold_uuid, tinfo.pool_uuid);

	}

	print_message("Warm Pool UUID: "DF_UUIDF"\n", DP_UUID(warm_uuid));
	print_message("Cold Pool UUID: "DF_UUIDF"\n", DP_UUID(cold_uuid));

	daos_tier_setup_client_ctx(cold_uuid, cold_grp, NULL, warm_uuid,
				   warm_grp, NULL);

	/*Register the colder tier ID and group server side*/
	print_message("Registering Cold Tier...\n");
	daos_tier_register_cold(cold_uuid, cold_grp, warm_uuid, warm_grp, NULL);

	print_message("Initiating Tier Cross-Connect\n");
	/*Connect to the pool*/

	rc =  daos_tier_pool_connect(warm_uuid, warm_grp, &warm_svc,
				     DAOS_PC_RW, &warm_poh, &warm_pool_info,
				     &ev);

	print_message("Polling for event completion\n");
	daos_eq_poll(arg.eq, 1, DAOS_EQ_WAIT, 1, &evp);
	D_INFO("event says done!\n");

	rc = evp->ev_error;

	if (rc)
		print_message("Pool Connect Failed with code: %d\n", rc);
	else
		print_message("Connected to pool\n");

	arg.poh = warm_poh;
	uuid_copy(arg.co_uuid, tinfo.cont_uuid);

	print_message("Initiating Container Fetch...");

	rc = daos_tier_fetch_cont(warm_poh, tinfo.cont_uuid, ep, NULL, &ev);
	daos_eq_poll(arg.eq, 1, DAOS_EQ_WAIT, 1, &evp);

	D_INFO("event says done!\n");

	rc = evp->ev_error;

	if (rc) {
		print_message("Failed with code: %d\n", rc);
		goto discon;
	} else
		print_message("Success\n\n");

	print_message("Opening fetched container.....");
	rc = daos_cont_open(arg.poh, arg.co_uuid, DAOS_COO_RW,
			    &arg.coh, &arg.co_info, NULL);
	if (rc) {
		print_message("Failed: %d\n", rc);
		goto discon;
	} else
		print_message("Success\n\n");

	print_message("container info:\n");
	print_message("  hce: "DF_U64"\n", arg.co_info.ci_epoch_state.es_hce);
	print_message("  lre: "DF_U64"\n", arg.co_info.ci_epoch_state.es_lre);
	print_message("  lhe: "DF_U64"\n", arg.co_info.ci_epoch_state.es_lhe);
	print_message("  ghce: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_ghce);
	print_message("  glre: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_glre);
	print_message("  ghpce: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_ghpce);

	ioreq_init(&req, arg.coh, tinfo.tgt, DAOS_IOD_ARRAY, &arg);

	/** Lookup */
	print_message("Checking a fetched object\n");
	print_message("OID:"DF_OID" D:dkey-bob A:akey-bob\n",
		      DP_OID(tinfo.tgt));
	buf = calloc(64, 1);
	assert_non_null(buf);
	lookup_single("dkey-bob", "akey-bob", 0, buf, 64, g_epoch, &req);

	/** Verify data consistency */
	print_message("size = %lu\n", req.iod[0].iod_size);
	print_message("value:%s\n", buf);
	if (strcmp(buf, "yabba-dabba-dooooo") == 0)
		print_message("CORRECT\n");
	else
		print_message("WRONG value\n");
	free(buf);
	ioreq_fini(&req);

	print_message("Closing containter\n");
	rc = daos_cont_close(arg.coh, NULL);
	if (rc)
		print_message("Container Close: %d\n", rc);

discon:
	print_message("Disconnecting from Warm Pool...\n");
	rc = daos_pool_disconnect(warm_poh, NULL);
	if (rc)
		print_message("Failed: %d\n", rc);
	else
		print_message("Success\n");

out:
	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);

	MPI_Finalize();

	free(warm_grp);
	free(cold_grp);

	return 0;
}
