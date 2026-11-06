/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_TEST_HELPER_ARGS__
#define __DLCK_TEST_HELPER_ARGS__

#include "../dlck_args.h"

struct dlck_helper_workload_args {
	unsigned int oids_num;
	unsigned int keys_num;
	unsigned int updates_num;

	bool         verbose;
	bool         create_pools;
};

struct dlck_helper_args {
	struct dlck_args_files           files;
	struct dlck_args_engine          engine;
	struct dlck_helper_workload_args workload;
};

void
dlck_helper_args_parse(int argc, char **argv, struct dlck_helper_args *args);

#endif /* __DLCK_TEST_HELPER_ARGS__ */
