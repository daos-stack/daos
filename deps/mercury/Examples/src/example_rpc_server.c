/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_rpc.h"
#include "example_rpc_engine.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

/* example server program.  Starts HG engine, registers the example RPC type,
 * and then executes indefinitely.
 */

int
main(void)
{
    hg_engine_init(HG_TRUE, "tcp://:12345");

    hg_engine_print_self_addr();

    /* register RPC */
    my_rpc_register();

    /* this would really be something waiting for shutdown notification */
    while (1)
        sleep(1);

    hg_engine_finalize();

    return (0);
}
