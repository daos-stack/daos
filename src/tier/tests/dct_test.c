/**
 * (C) Copyright 2016 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of dct
 *
 * dct/tests/dct_test
 */
#include <dct_rpc.h>
#include <daos.h>
#include <daos/event.h>
#include <daos/rpc.h>

#include "dct_test.h"


#define DEFAULT_TIMEOUT	20


int
main(int argc, char **argv)
{
	int	rc = 0;

	rc = daos_init();
	if (rc != 0) {
		D__ERROR("daos init fails: rc = %d\n", rc);
		return rc;
	}

	/*Actually run the tests*/
	run_dct_ping_test();

	daos_fini();
	return rc;
}
