/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DEDUP_H
#define __DAOS_DEDUP_H

#include <daos/cont_props.h>

int
dedup_get_csum_algo(struct cont_props *cont_props);

void
dedup_configure_csummer(struct daos_csummer *csummer,
			struct cont_props *cont_props);


#endif /** __DAOS_DEDUP_H */
