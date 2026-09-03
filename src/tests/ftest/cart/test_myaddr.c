#include <cart/api.h>
#include <cart/iv.h>
#include <daos_errno.h>
#include <cart/types.h>

/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Minimal CaRT test: initialize, create a context, query its URI and
 * cleanly exit.
 */

#include <stdio.h>
#include <stdlib.h>

#include <cart/api.h>
#include <cart/types.h>
#include <gurt/common.h>
#include "crt_utils.h"

static void
usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-p provider] [-i interface] [-d domain]\n"
		"  -p provider  CaRT provider (default: %s)\n"
		"  -i interface CaRT interface (default: %s)\n"
		"  -d domain    Optional CaRT domain\n",
		prog, getenv("D_PROVIDER") ? getenv("D_PROVIDER") : "ofi+tcp",
		getenv("D_INTERFACE") ? getenv("D_INTERFACE") : "lo");
}

int
main(int argc, char **argv)
{
	crt_context_t      crt_ctx;
	char              *uri           = NULL;
	crt_init_options_t init_opts     = {0};
	const char        *provider      = NULL;
	const char        *interface     = NULL;
	const char        *domain        = NULL;
	bool               provider_set  = false;
	bool               interface_set = false;
	int                opt;
	int                rc;

	provider = getenv("D_PROVIDER");
	if (provider == NULL) {
		provider = "ofi+tcp";
	} else {
		provider_set = true;
		printf("using D_PROVIDER setting of %s\n", provider);
	}

	interface = getenv("D_INTERFACE");
	if (interface == NULL) {
		interface = "lo";
	} else {
		interface_set = true;
		printf("Using D_INTERFACE setting of %s\n", interface);
	}

	domain = getenv("D_DOMAIN");

	while ((opt = getopt(argc, argv, "p:i:d:h")) != -1) {
		switch (opt) {
		case 'p':
			provider     = optarg;
			provider_set = true;
			break;
		case 'i':
			interface     = optarg;
			interface_set = true;
			break;
		case 'd':
			domain = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (provider_set == false)
		printf("Warning: provider (-p option) was not set, assuming 'ofi+tcp'\n");
	if (interface_set == false)
		printf("Warning: interface (-i option) was not set, assuming 'lo'\n");

	init_opts.cio_provider  = (char *)provider;
	init_opts.cio_interface = (char *)interface;
	init_opts.cio_domain    = (char *)domain;

	rc = d_log_init();
	if (rc != 0) {
		fprintf(stderr, "d_log_init() failed: rc=%d\n", rc);
		return EXIT_FAILURE;
	}

	rc = crt_init_opt(NULL, 0, &init_opts);
	if (rc != 0) {
		fprintf(stderr, "crt_init_opt() failed: rc=%d\n", rc);
		d_log_fini();
		return EXIT_FAILURE;
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		fprintf(stderr, "crt_context_create() failed: rc=%d\n", rc);
		crt_finalize();
		d_log_fini();
		return EXIT_FAILURE;
	}

	rc = crt_context_uri_get(crt_ctx, &uri);
	if (rc != 0) {
		fprintf(stderr, "crt_context_uri_get() failed: rc=%d\n", rc);
		crt_context_destroy(crt_ctx, 0);
		crt_finalize();
		d_log_fini();
		return EXIT_FAILURE;
	}

	printf("my address: %s\n", uri);
	D_FREE(uri);

	rc = crt_context_destroy(crt_ctx, 0);
	if (rc != 0) {
		fprintf(stderr, "crt_context_destroy() failed: rc=%d\n", rc);
		crt_finalize();
		d_log_fini();
		return EXIT_FAILURE;
	}

	rc = crt_finalize();
	if (rc != 0) {
		fprintf(stderr, "crt_finalize() failed: rc=%d\n", rc);
		d_log_fini();
		return EXIT_FAILURE;
	}

	d_log_fini();
	return EXIT_SUCCESS;
}
