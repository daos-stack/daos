/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_MAIN_H
#define DAOS_DDB_MAIN_H

#include <daos_types.h>
#include "ddb_common.h"

int ddb_init(void);
void ddb_fini(void);

int ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[]);

#endif /* DAOS_DDB_MAIN_H */
