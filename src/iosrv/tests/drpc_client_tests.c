/*
 * (C) Copyright 2019-2021 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../drpc_internal.h"
#include "../srv.pb-c.h"
#include "../event.pb-c.h"
#include "../srv_internal.h"
#include <daos/test_mocks.h>
#include <daos/test_utils.h>
#include <daos/drpc_modules.h>

/*
 * Mocks of DAOS internals
 */

/*
 * Globals for socket locations - arbitrary, these tests don't create a real one
 */
const char	*dss_socket_dir = "/my/fake/path";
char		*drpc_listener_socket_path = "/fake/listener.sock";
char		 dss_hostname[DSS_HOSTNAME_MAX_LEN] = "foo-host";

/* DAOS internal globals - arbitrary values okay */
uint32_t	dss_tgt_offload_xs_nr = 3;
uint32_t	dss_tgt_nr = 4;
uint32_t	dss_sys_xs_nr = 2;
uint32_t	dss_instance_idx = 5;

static int		 crt_self_uri_get_return;
static const char	*crt_self_uri_get_uri = "/cart/test/uri";
int
crt_self_uri_get(int tag, char **uri)
{
	if (crt_self_uri_get_return == 0)
		D_STRNDUP(*uri, crt_self_uri_get_uri,
			  strlen(crt_self_uri_get_uri));
	return crt_self_uri_get_return;
}

static uint32_t	mock_self_rank = 1;
d_rank_t
dss_self_rank(void)
{
	return (d_rank_t)mock_self_rank;
}

/* satisfy symbol definitions for srv.c and tls.c not included in build */
struct dss_module_info	*
get_module_info(void)
{
	return NULL;
}

/*
static void
setup_mock_dss_drpc_ctx(void)
{
	drpc_connect("non-existent", NULL);
}
static void
teardown_mock_dss_drpc_ctx(void)
{
	drpc_close(dss_drpc_ctx);
	dss_drpc_ctx = NULL;
}
*/

/*
 * Test setup and teardown
 */
static int
drpc_client_test_setup(void **state)
{
	mock_socket_setup();
	mock_connect_setup();
	mock_sendmsg_setup();
	mock_recvmsg_setup();
	mock_close_setup();

	crt_self_uri_get_return = 0;

	return 0;
}

static int
drpc_client_test_teardown(void **state)
{
	return 0;
}

/*
 * Unit tests
 */
static void
test_drpc_init_connect_fails(void **state)
{
	skip(); /* DAOS-6436 */
	connect_return = -1;

	assert_int_equal(drpc_init(), -DER_NOMEM);
}

static void
test_drpc_init_crt_get_uri_fails(void **state)
{
	crt_self_uri_get_return = -DER_BUSY;
	assert_int_equal(drpc_init(), crt_self_uri_get_return);

	/* make sure socket was closed */
	assert_int_equal(close_call_count, 1);
}

static void
test_drpc_init_sendmsg_fails(void **state)
{
	skip(); /* DAOS-6436 */
	sendmsg_return = -1;
	errno = EPERM;

	assert_int_equal(drpc_init(), -DER_NO_PERM);

	/* make sure socket was closed */
	assert_int_equal(close_call_count, 1);
}

static void
verify_notify_ready_message(void)
{
	Drpc__Call		*call;
	Srv__NotifyReadyReq	*req;

	call = drpc__call__unpack(NULL, sendmsg_msg_iov_len,
				  sendmsg_msg_content);
	assert_non_null(call);
	assert_int_equal(call->module, DRPC_MODULE_SRV);
	assert_int_equal(call->method, DRPC_METHOD_SRV_NOTIFY_READY);

	/* Verify payload contents */
	req = srv__notify_ready_req__unpack(NULL, call->body.len,
					    call->body.data);
	assert_non_null(req);
	assert_string_equal(req->uri, crt_self_uri_get_uri);
	assert_int_equal(req->nctxs, DSS_CTX_NR_TOTAL);
	assert_string_equal(req->drpclistenersock, drpc_listener_socket_path);
	assert_int_equal(req->instanceidx, dss_instance_idx);
	assert_int_equal(req->ntgts, dss_tgt_nr);

	/* Cleanup */
	srv__notify_ready_req__free_unpacked(req, NULL);
	drpc__call__free_unpacked(call, NULL);
}

static void
test_drpc_init_fini(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	/* drpc connection created */
	assert_int_equal(connect_sockfd, socket_return);

	/* socket was left open */
	assert_int_equal(close_call_count, 0);

	/* Message was sent */
	assert_non_null(sendmsg_msg_ptr);
	verify_notify_ready_message();

	/* Now let's shut things down... */
	drpc_fini();

	/* socket was closed */
	assert_int_equal(close_call_count, 1);
}

static void
test_drpc_init_bad_response(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__FAILURE);
	assert_int_equal(drpc_init(), -DER_IO);

	/* make sure socket was closed */
	assert_int_equal(close_call_count, 1);
}

static void
verify_notify_bio_error(void)
{
	Drpc__Call		*call;
	Srv__BioErrorReq	*req;

	call = drpc__call__unpack(NULL, sendmsg_msg_iov_len,
				  sendmsg_msg_content);
	assert_non_null(call);
	assert_int_equal(call->module, DRPC_MODULE_SRV);
	assert_int_equal(call->method, DRPC_METHOD_SRV_BIO_ERR);

	/* Verify payload contents */
	req = srv__bio_error_req__unpack(NULL, call->body.len,
					    call->body.data);
	assert_non_null(req);
	assert_string_equal(req->uri, crt_self_uri_get_uri);
	assert_string_equal(req->drpclistenersock, drpc_listener_socket_path);
	assert_int_equal(req->instanceidx, dss_instance_idx);
	assert_false(req->unmaperr);
	assert_true(req->writeerr);
	assert_false(req->readerr);
	assert_int_equal(req->tgtid, 0);

	/* Cleanup */
	srv__bio_error_req__free_unpacked(req, NULL);
	drpc__call__free_unpacked(call, NULL);
}

static void
test_drpc_verify_notify_bio_error(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	assert_int_equal(ds_notify_bio_error(MET_WRITE, 0), 0);
	verify_notify_bio_error();

	/* Now let's shut things down... */
	drpc_fini();

	/* socket was closed */
	assert_int_equal(close_call_count, 1);
}

static void
verify_notify_pool_svc_update(uuid_t *pool_uuid, d_rank_list_t *svc_reps)
{
	Drpc__Call		*call;
	Shared__ClusterEventReq	*req;
	d_rank_list_t		*reps;
	uuid_t			 pool;
	int			 reps_match;

	call = drpc__call__unpack(NULL, sendmsg_msg_iov_len,
				  sendmsg_msg_content);
	assert_non_null(call);
	assert_int_equal(call->module, DRPC_MODULE_SRV);
	assert_int_equal(call->method, DRPC_METHOD_SRV_CLUSTER_EVENT);

	/* Verify payload contents */
	req = shared__cluster_event_req__unpack(NULL, call->body.len,
						call->body.data);
	assert_non_null(req);

	assert_string_equal(req->event->hostname, dss_hostname);
	/* populated by mock dss_self_rank */
	assert_int_equal(req->event->rank, mock_self_rank);
	assert_int_equal(uuid_parse(req->event->pool_uuid, pool), 0);
	assert_int_equal(uuid_compare(pool, *pool_uuid), 0);
	assert_int_equal(req->event->pool_svc_info->n_svc_reps,
			 svc_reps->rl_nr);
	reps = uint32_array_to_rank_list(req->event->pool_svc_info->svc_reps,
					 svc_reps->rl_nr);
	reps_match = daos_rank_list_identical(svc_reps, reps);
	d_rank_list_free(reps);
	assert_true(reps_match);

	/* Cleanup */
	shared__cluster_event_req__free_unpacked(req, NULL);
	drpc__call__free_unpacked(call, NULL);
}

static void
test_drpc_verify_notify_pool_svc_update(void **state)
{
	uuid_t		 pool_uuid;
	uint32_t	 svc_reps[4] = {0, 1, 2, 3};
	d_rank_list_t	*svc_ranks;

	/* setup_mock_dss_drpc_ctx(); */
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	assert_int_equal(uuid_parse("11111111-1111-1111-1111-111111111111",
				    pool_uuid), 0);

	svc_ranks = uint32_array_to_rank_list(svc_reps, 4);
	assert_non_null(svc_ranks);

	assert_int_equal(ds_notify_pool_svc_update(&pool_uuid, svc_ranks), 0);
	verify_notify_pool_svc_update(&pool_uuid, svc_ranks);

	d_rank_list_free(svc_ranks);

	/* teardown_mock_dss_drpc_ctx(); */
	drpc_fini();
}

static void
verify_cluster_event_not_sent()
{
	Drpc__Call		*call;
	Shared__ClusterEventReq	*req;

	call = drpc__call__unpack(NULL, sendmsg_msg_iov_len,
				  sendmsg_msg_content);
	assert_non_null(call);
	assert_int_equal(call->module, DRPC_MODULE_SRV);

	/* Verify NULL payload content */
	req = shared__cluster_event_req__unpack(NULL, call->body.len,
						call->body.data);
	assert_true(req == NULL);

	/* Cleanup */
	drpc__call__free_unpacked(call, NULL);
}

static void
test_drpc_verify_notify_pool_svc_update_noreps(void **state)
{
	uuid_t	pool_uuid;

	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	assert_int_equal(uuid_parse("11111111-1111-1111-1111-111111111111",
				    pool_uuid), 0);

	assert_int_equal(ds_notify_pool_svc_update(&pool_uuid, NULL),
			 -DER_INVAL);
	verify_cluster_event_not_sent();

	drpc_fini();
}

static void
test_drpc_verify_notify_pool_svc_update_nopool(void **state)
{
	uint32_t	 svc_reps[4] = {0, 1, 2, 3};
	d_rank_list_t	*svc_ranks;

	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	svc_ranks = uint32_array_to_rank_list(svc_reps, 4);
	assert_non_null(svc_ranks);

	assert_int_equal(ds_notify_pool_svc_update(NULL, svc_ranks),
			 -DER_INVAL);
	verify_cluster_event_not_sent();

	d_rank_list_free(svc_ranks);

	drpc_fini();
}

static void
verify_cluster_event(uint32_t id, char *msg, uint32_t type, uint32_t sev,
		     char *hwid, uint32_t rank, char *jobid, char *pool,
		     char *cont, char *objid, char *ctlop, char *data)
{
	Drpc__Call		*call;
	Shared__ClusterEventReq	*req;

	call = drpc__call__unpack(NULL, sendmsg_msg_iov_len,
				  sendmsg_msg_content);
	assert_non_null(call);
	assert_int_equal(call->module, DRPC_MODULE_SRV);
	assert_int_equal(call->method, DRPC_METHOD_SRV_CLUSTER_EVENT);

	/* Verify payload contents */
	req = shared__cluster_event_req__unpack(NULL, call->body.len,
						call->body.data);
	assert_non_null(req);

	assert_string_equal(req->event->hostname, dss_hostname);
	assert_int_equal(req->event->rank, rank);
	assert_int_equal(req->event->id, id);
	assert_string_equal(req->event->msg, msg);
	assert_int_equal(req->event->type, type);
	assert_int_equal(req->event->severity, sev);
	assert_string_equal(req->event->hw_id, hwid);
	assert_string_equal(req->event->job_id, jobid);
	assert_string_equal(req->event->pool_uuid, pool);
	assert_string_equal(req->event->cont_uuid, cont);
	assert_string_equal(req->event->obj_id, objid);
	assert_string_equal(req->event->ctl_op, ctlop);
	assert_string_equal(req->event->str_info, data);

	/* Cleanup */
	shared__cluster_event_req__free_unpacked(req, NULL);
	drpc__call__free_unpacked(call, NULL);
}

static void
test_drpc_verify_cluster_event(void **state)
{
	uuid_t		 pool, cont;
	char		*pool_str = "11111111-1111-1111-1111-111111111111";
	char		*cont_str = "22222222-2222-2222-2222-222222222222";
	d_rank_t	 rank = 1;
	daos_obj_id_t	 objid = { .hi = 1, .lo = 1 };

	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	assert_int_equal(uuid_parse(pool_str, pool), 0);
	assert_int_equal(uuid_parse(cont_str, cont), 0);

	ds_notify_ras_event(RAS_RANK_NO_RESPONSE, "no response", RAS_TYPE_INFO,
			    RAS_SEV_WARN, "exhwid", &rank, "exjobid", &pool,
			    &cont, &objid, "exctlop",
			    "{\"people\":[\"bill\",\"steve\",\"bob\"]}");
	verify_cluster_event((uint32_t)RAS_RANK_NO_RESPONSE, "no response",
			     (uint32_t)RAS_TYPE_INFO, (uint32_t)RAS_SEV_WARN,
			     "exhwid", 1, "exjobid", pool_str, cont_str, "1.1",
			     "exctlop",
			     "{\"people\":[\"bill\",\"steve\",\"bob\"]}");

	drpc_fini();
}

static void
test_drpc_verify_cluster_event_min_viable(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	ds_notify_ras_event(RAS_RANK_DOWN, "rank down", RAS_TYPE_STATE_CHANGE,
			    RAS_SEV_ERROR, NULL, NULL, NULL, NULL, NULL, NULL,
			    NULL, NULL);
	verify_cluster_event((uint32_t)RAS_RANK_DOWN, "rank down",
			     (uint32_t)RAS_TYPE_STATE_CHANGE,
			     (uint32_t)RAS_SEV_ERROR, "", mock_self_rank, "",
			     "", "", "", "", "");

	drpc_fini();
}

static void
test_drpc_verify_cluster_event_emptymsg(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	ds_notify_ras_event(RAS_RANK_DOWN, "", RAS_TYPE_STATE_CHANGE,
			    RAS_SEV_ERROR, NULL, NULL, NULL, NULL, NULL, NULL,
			    NULL, NULL);
	verify_cluster_event_not_sent();

	drpc_fini();
}

static void
test_drpc_verify_cluster_event_nomsg(void **state)
{
	mock_valid_drpc_resp_in_recvmsg(DRPC__STATUS__SUCCESS);
	assert_int_equal(drpc_init(), 0);

	ds_notify_ras_event(RAS_RANK_DOWN, NULL, RAS_TYPE_STATE_CHANGE,
			    RAS_SEV_ERROR, NULL, NULL, NULL, NULL, NULL, NULL,
			    NULL, NULL);
	verify_cluster_event_not_sent();

	drpc_fini();
}

/* Convenience macros for unit tests */
#define UTEST(x)	cmocka_unit_test_setup_teardown(x,	\
				drpc_client_test_setup,	\
				drpc_client_test_teardown)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		UTEST(test_drpc_init_connect_fails),
		UTEST(test_drpc_init_crt_get_uri_fails),
		UTEST(test_drpc_init_sendmsg_fails),
		UTEST(test_drpc_init_fini),
		UTEST(test_drpc_init_bad_response),
		UTEST(test_drpc_verify_notify_bio_error),
		UTEST(test_drpc_verify_notify_pool_svc_update),
		UTEST(test_drpc_verify_notify_pool_svc_update_noreps),
		UTEST(test_drpc_verify_notify_pool_svc_update_nopool),
		UTEST(test_drpc_verify_cluster_event),
		UTEST(test_drpc_verify_cluster_event_min_viable),
		UTEST(test_drpc_verify_cluster_event_emptymsg),
		UTEST(test_drpc_verify_cluster_event_nomsg),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef UTEST
