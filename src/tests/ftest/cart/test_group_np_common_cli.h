
static void
send_rpc_shutdown(crt_endpoint_t server_ep, crt_rpc_t *rpc_req)
{
	struct test_shutdown_in	*rpc_req_input;

	int rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
				TEST_OPC_SHUTDOWN, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL,
		  "crt_req_create() failed. "
		  "rc: %d, rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);

	rpc_req_input->rank = 123;
	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	crtu_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
}
