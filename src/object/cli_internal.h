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
 * This file is part of daos_sr
 *
 * src/dsr/cli_internal.h
 */
#ifndef __DSR_CLI_INTENRAL_H__
#define __DSR_CLI_INTENRAL_H__

#include <daos_m.h>
#include "dsr_internal.h"

/** Client stack DSR object */
struct dsr_cli_obj {
	/**
	 * Object metadata stored in the OI table. For those object classes
	 * and have no metadata in OI table, DSR only stores OID and pool map
	 * version in it.
	 */
	struct dsr_obj_md	 cob_md;
	/** container open handle */
	daos_handle_t		 cob_coh;
	/** object open mode */
	unsigned int		 cob_mode;
	/** refcount on this object */
	unsigned int		 cob_ref;
	/** algorithmically generated object layout */
	struct pl_obj_layout	*cob_layout;
	/** object handles of underlying DSM objects */
	daos_handle_t		*cob_mohs;
};

#endif /* __DSR_CLI_INTENRAL_H__ */
