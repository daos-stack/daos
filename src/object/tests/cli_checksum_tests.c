/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/checksum.h>
#include <daos/tests_lib.h>
#include <daos/test_perf.h>
#include "../cli_csum.h"

struct cli_checksum_test_state {
	struct daos_csummer *csummer;
	struct test_data     td;
};

static void
timing_obj_csum_update(void **state)
{
	struct cli_checksum_test_state	*st = *state;
	struct dcs_csum_info		*dkey_csum = NULL;
	struct dcs_iod_csums		*iod_csums = NULL;
	daos_obj_id_t			 oid = {0};
	struct cont_props		 props = {.dcp_csum_enabled = true};

	MEASURE_TIME(dc_obj_csum_update(st->csummer, props, oid, &st->td.dkey, st->td.td_iods,
					st->td.td_sgls, 1, NULL, &dkey_csum,
					&iod_csums),
		     noop(),
		     daos_csummer_free_ci(st->csummer, &dkey_csum);
		     daos_csummer_free_ic(st->csummer, &iod_csums););
	MEASURE_TIME(dc_obj_csum_update(st->csummer, props, oid, &st->td.dkey, st->td.td_iods,
					st->td.td_sgls, st->td.td_iods_nr, NULL, &dkey_csum,
					&iod_csums),
		     noop(),
		     daos_csummer_free_ci(st->csummer, &dkey_csum);
		     daos_csummer_free_ic(st->csummer, &iod_csums););
}

static void
timing_obj_csum_fetch(void **state)
{
	struct cli_checksum_test_state	*st = *state;
	struct dcs_csum_info		*dkey_csum = NULL;
	struct dcs_iod_csums		*iod_csums = NULL;

	MEASURE_TIME(dc_obj_csum_fetch(st->csummer, &st->td.dkey, st->td.td_iods, st->td.td_sgls,
				       1, NULL, &dkey_csum, &iod_csums),
		     noop(),
		     daos_csummer_free_ci(st->csummer, &dkey_csum);
		     daos_csummer_free_ic(st->csummer, &iod_csums);
	);
	MEASURE_TIME(dc_obj_csum_fetch(st->csummer, &st->td.dkey, st->td.td_iods, st->td.td_sgls,
				       st->td.td_iods_nr, NULL, &dkey_csum, &iod_csums),
		     noop(),
		     daos_csummer_free_ci(st->csummer, &dkey_csum);
		     daos_csummer_free_ic(st->csummer, &iod_csums);
	);
}

static void
timiing_obj_csums_verify(void **state)
{
	struct cli_checksum_test_state	*st = *state;
	struct dc_csum_veriry_args	 args = {
		 .csummer    = st->csummer,
		 .iods       = st->td.td_iods,
		 .iod_nr     = 1,
		 .sgls       = st->td.td_sgls,
		 .dkey       = &st->td.dkey,
		 .maps       = st->td.td_maps,
		 .maps_nr    = 1,
		 .iods_csums = NULL,
		 .dkey_hash  = 1,
		 .sizes      = st->td.td_sizes,
	};

	/* Calculate the checksums that will be verified. In production, these would
	 * come from the server
	 */
	assert_success(
	    daos_csummer_calc_iods(st->csummer, st->td.td_sgls, st->td.td_iods, NULL,
				   st->td.td_iods_nr,
				   false, NULL, 0, &args.iods_csums));

	/* Time it */
	MEASURE_TIME(dc_rw_cb_csum_verify(&args), noop(), noop());

	args.maps_nr = args.iod_nr = st->td.td_iods_nr;
	MEASURE_TIME(dc_rw_cb_csum_verify(&args), noop(), noop());

	daos_csummer_free_ic(st->csummer, &args.iods_csums);
}

static int
cct_setup(void **state)
{
	struct cli_checksum_test_state *st;

	D_ALLOC_PTR(st);
	assert_non_null(st);

	/* Using the noop algorithm so measurements are all overhead */
	daos_csummer_init_with_type(&st->csummer, HASH_TYPE_NOOP, 1024, false);

	td_init(&st->td, 10,
		(struct td_init_args){.ca_iod_types = {DAOS_IOD_ARRAY, DAOS_IOD_SINGLE,
						       DAOS_IOD_ARRAY, DAOS_IOD_SINGLE,
						       DAOS_IOD_ARRAY, DAOS_IOD_SINGLE,
						       DAOS_IOD_ARRAY, DAOS_IOD_SINGLE,
						       DAOS_IOD_ARRAY, DAOS_IOD_SINGLE},
				      .ca_recx_nr   = {10, 1, 10, 1, 10, 1, 10, 1, 10, 1} });

	*state = st;
	return 0;
}

static int
cct_teardown(void **state)
{
	struct cli_checksum_test_state *st = *state;

	td_destroy(&st->td);
	daos_csummer_destroy(&st->csummer);
	D_FREE(st);

	return 0;
}

/* Convenience macro for unit tests */
#define	TA(fn) \
	{ #fn, fn, cct_setup, cct_teardown }

static const struct CMUnitTest srv_csum_tests[] = {
	TA(timing_obj_csum_update),
	TA(timing_obj_csum_fetch),
	TA(timiing_obj_csums_verify),
};

int
main(int argc, char **argv)
{
	int	rc = 0;
#if CMOCKA_FILTER_SUPPORTED == 1 /** for cmocka filter(requires cmocka 1.1.5) */
	char	 filter[1024];

	if (argc > 1) {
		snprintf(filter, 1024, "*%s*", argv[1]);
		cmocka_set_test_filter(filter);
	}
#endif

	rc += cmocka_run_group_tests_name(
		"Storage and retrieval of checksums for Array Type",
		srv_csum_tests, NULL, NULL);

	return rc;
}
