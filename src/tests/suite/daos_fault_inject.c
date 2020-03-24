#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <daos/checksum.h>
#include <gurt/types.h>
#include <daos_prop.h>

/** fault injection helpers */
static void
update_csum_fi()
{
	sleep(10);
	daos_fail_loc_set(dt_inject_fault | DAOS_FAIL_ALWAYS);
	sleep(dt_fi_sleep);
}

/*
static void
update_csum_fi()
{
	daos_fail_loc_set(dt_inject_fault | DAOS_FAIL_ALWAYS);
}
*/

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static const struct CMUnitTest tests[] = {
	{ "DAOS_FAULT: Set Fault Injection",
		update_csum_fi, async_disable, test_case_teardown}
};

int
run_daos_fault_injection(int rank, int size)
{
	int rc = 0;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS Set fault injection",
			tests, setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;

}