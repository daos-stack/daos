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
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>
#include <daos_m.h>

int run_pool_test(void);
int run_co_test(void);
int run_io_test(void);

int
main(int argc, char **argv)
{
	int	nr_failed = 0;
	int	rc;

	rc = dmg_init();
	if (rc) {
		print_message("dmg_init() failed with %d\n", rc);
		return -1;
	}

	rc = dsm_init();
	if (rc) {
		print_message("dmg_init() failed with %d\n", rc);
		return -1;
	}

	nr_failed = run_pool_test();
	nr_failed += run_co_test();
	nr_failed += run_io_test();

	rc = dsm_fini();
	if (rc)
		print_message("dsm_fini() failed with %d\n", rc);

	rc = dmg_fini();
	if (rc)
		print_message("dmg_fini() failed with %d\n", rc);

	print_message("\n============ Summary %s\n", __FILE__);
	if (nr_failed == 0)
		print_message("OK - NO TEST FAILURES\n");
	else
		print_message("ERROR, %i TEST(S) FAILED\n", nr_failed);

	return nr_failed;
}
