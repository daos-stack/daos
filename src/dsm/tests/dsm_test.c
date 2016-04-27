/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of dsm
 *
 * dsm/tests/dsm_test
 *
 * Author: Wang Di <di.wang@intel.com>
 */
#include <getopt.h>
#include <dsm_rpc.h>
#include <daos_m.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/transport.h>

static struct option opts[] = {
	{ "ping",	0,	NULL,   'p'},
	{  NULL,	0,	NULL,	 0 }
};

#define DEFAULT_TIMEOUT	20
static int test_ping_rpc(dtp_context_t *ctx)
{
	dtp_endpoint_t		tgt_ep;
	daos_handle_t		eqh;
	struct daos_event	events[10];
	struct daos_event	parent_event;
	dtp_rpc_t		*req;
	int			i;
	int			completed = 0;
	int			rc;

	tgt_ep.ep_rank = 0;
	tgt_ep.ep_tag = 0;
	rc = dsm_req_create(ctx, tgt_ep, DSM_PING, &req);
	if (rc != 0)
		return rc;

	/* send ping synchronously */
	rc = dtp_sync_req(req, 0);
	if (rc != 0)
		return rc;

	/* Test async ping req */
	rc = daos_eq_create(&eqh);
	if (rc != 0)
		return rc;

	for (i = 0; i < 5; i++) {
		dtp_rpc_t *async_req;

		rc = daos_event_init(&events[i], eqh, NULL);
		if (rc != 0)
			break;

		rc = dsm_req_create(ctx, tgt_ep, DSM_PING, &async_req);
		if (rc != 0)
			break;
		/* Create events and send it asynchronously */
		rc = dsm_client_async_rpc(async_req, &events[i]);
		if (rc != 0)
			break;
	}

	rc = daos_event_init(&parent_event, eqh, NULL);
	if (rc != 0)
		goto out_destroy;

	for (i = 5; i < 10; i++) {
		rc = daos_event_init(&events[i], eqh, &parent_event);
		if (rc != 0)
			break;
	}

	for (i = 5; i < 10; i++) {
		dtp_rpc_t *async_req;

		rc = dsm_req_create(ctx, tgt_ep, DSM_PING, &async_req);
		if (rc != 0)
			break;
		/* Create events and send it asynchronously */
		rc = dsm_client_async_rpc(async_req, &events[i]);
		if (rc != 0)
			break;
	}

	/* Wait all of async request finished */
	while (completed < 6) {
		rc = daos_eq_poll(eqh, 1, DEFAULT_TIMEOUT * 1000 * 1000 * i,
				  i, NULL);
		if (rc < 0)
			break;
		completed += rc;
	}

out_destroy:
	daos_eq_destroy(eqh, 0);
	if (rc == 0)
		fprintf(stdout, "test ping succeeds!\n");
	return rc;
}

int
main(int argc, char **argv)
{
	dtp_context_t	ctx;
	int		rc = 0;
	int		option;

	/* use full debug dy default for now */
	rc = setenv("DAOS_DEBUG", "-1", false);
	if (rc)
		D_ERROR("failed to enable full debug, %d\n", rc);

	rc = dtp_init(false);
	if (rc != 0) {
		D_ERROR("dtp init failure: rc =%d\n", rc);
		return rc;
	}

	rc = dtp_context_create(NULL, &ctx);
	if (rc != 0) {
		D_ERROR("dtp context create failure: rc = %d\n", rc);
		goto out_dtp;
	}

	rc = daos_eq_lib_init(ctx);
	if (rc != 0) {
		D_ERROR("Failed to initailiz DAOS/event library: %d\n", rc);
		goto out_ctx;
	}

	rc = dsm_init();
	if (rc != 0) {
		D_ERROR("dsm init fails: rc = %d\n", rc);
		goto out_eq;
	}

	while ((option = getopt_long(argc, argv, "p", opts, NULL)) != -1) {
		switch (option) {
		default:
			return -EINVAL;
		case 'p':
			rc = test_ping_rpc(ctx);
			break;
		}
		if (rc < 0) {
			D_ERROR("fails on %d: rc = %d\n", option, rc);
			break;
		}
	}

	dsm_fini();
out_eq:
	daos_eq_lib_fini();
out_ctx:
	dtp_context_destroy(ctx, 1);
out_dtp:
	dtp_finalize();
	return rc;
}
