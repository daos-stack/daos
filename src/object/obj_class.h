/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(object)

#include "obj_internal.h"
#include <daos_api.h>

/** DAOS object class */
struct daos_obj_class {
	/** class name */
	char				*oc_name;
	/** unique class ID */
	daos_oclass_id_t		 oc_id;
	/** redundancy schema */
	enum daos_obj_redun		 oc_redun;
	struct daos_oclass_attr		 oc_attr;
	/** for internal usage, unit/functional test etc. */
	bool				 oc_private;
};

extern struct daos_obj_class daos_obj_classes[];

#define ca_rp_nr	u.rp.r_num
#define ca_ec_k		u.ec.e_k
#define ca_ec_p		u.ec.e_p

#define oc_rp_nr	oc_attr.ca_rp_nr
#define oc_ec_k		oc_attr.ca_ec_k
#define oc_ec_p		oc_attr.ca_ec_p
#define oc_grp_nr	oc_attr.ca_grp_nr
#define oc_resil	oc_attr.ca_resil
#define oc_resil_degree	oc_attr.ca_resil_degree
