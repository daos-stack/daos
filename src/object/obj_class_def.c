/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(object)

#include "obj_class.h"

/* single replica, "g" is number of groups */
#define OC_SS_DEF(g)					\
{							\
	.oc_name	= "S"#g,			\
	.oc_id		= OC_S##g,			\
	.oc_redun	= OR_RP_1,				\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= 1,			\
	}						\
}

/* single replica, maximum groups */
#define OC_SS_DEF_GX()					\
{							\
	.oc_name	= "SX",				\
	.oc_id		= OC_SX,			\
	.oc_redun	= OR_RP_1,			\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= DAOS_OBJ_GRP_MAX,	\
		.ca_rp_nr	= 1,			\
	}						\
}

/* replicas=r, groups=g */
#define OC_RP_DEF(r, g)					\
{							\
	.oc_name	= "RP_"#r"G"#g,			\
	.oc_id		= OC_RP_##r##G##g,		\
	.oc_redun	= OR_RP_##r,			\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= r,			\
	}						\
}

/* replicas=r, maximum groups */
#define OC_RP_DEF_GX(r)					\
{							\
	.oc_name	= "RP_"#r"GX",			\
	.oc_id		= OC_RP_##r##GX,		\
	.oc_redun	= OR_RP_##r,			\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= DAOS_OBJ_GRP_MAX,	\
		.ca_rp_nr	= r,			\
	}						\
}

/* extremely scalable fetch */
#define OC_RP_XSF()					\
{							\
	.oc_name	= "RP_XSF",			\
	.oc_id		= OC_RP_XSF,			\
	.oc_redun	= DAOS_OBJ_REPL_MAX,		\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= 1,			\
		.ca_rp_nr	= DAOS_OBJ_REPL_MAX,	\
	},						\
}

/* EC(k+p), groups=g */
#define OC_EC_DEF(k, p, g)				\
{							\
	.oc_name	= "EC_"#k"P"#p"G"#g,		\
	.oc_id		= OC_EC_##k##P##p##G##g,	\
	.oc_redun	= OR_RS_##k##P##p,		\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= g,			\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
	}						\
}

/* EC(k+p), maximum groups */
#define OC_EC_DEF_GX(k, p)				\
{							\
	.oc_name	= "EC_"#k"P"#p"GX",		\
	.oc_id		= OC_EC_##k##P##p##GX,		\
	.oc_redun	= OR_RS_##k##P##p,		\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= DAOS_OBJ_GRP_MAX,	\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
	}						\
}

/* Internal classes */

/* echo object class */
#define OC_RP_ECHO_DEF(r)				\
{							\
	.oc_name	= "RP_"#r"G1_ECHO",		\
	.oc_id		= DAOS_OC_ECHO_R##r##S_RW,	\
	.oc_redun	= OR_RP_##r,			\
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= 1,			\
		.ca_rp_nr	= r,			\
	},						\
	.oc_private	= true,				\
}

/* Replicas=r, specified rank object class for debugging */
#define OC_RP_SRANK_DEF(r, g)				\
{							\
	.oc_name	= "RP_"#r"G"#g"_SR",		\
	.oc_id		= DAOS_OC_R##r##S_SPEC_RANK,	\
	.oc_redun	= OR_RP_##r,			\
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= r,			\
	},						\
	.oc_private	= true,				\
}

struct daos_obj_class daos_obj_classes[] = {
	/* single replica classes */
	OC_SS_DEF(1),
	OC_SS_DEF(2),
	OC_SS_DEF(4),
	OC_SS_DEF(6),
	OC_SS_DEF(8),
	OC_SS_DEF(12),
	OC_SS_DEF(16),
	OC_SS_DEF(32),
	OC_SS_DEF_GX(),

	/* 2-replica classes */
	OC_RP_DEF(2, 1),
	OC_RP_DEF(2, 2),
	OC_RP_DEF(2, 4),
	OC_RP_DEF(2, 6),
	OC_RP_DEF(2, 8),
	OC_RP_DEF(2, 12),
	OC_RP_DEF(2, 16),
	OC_RP_DEF(2, 32),
	OC_RP_DEF_GX(2),

	/* 3-replica classes */
	OC_RP_DEF(3, 1),
	OC_RP_DEF(3, 2),
	OC_RP_DEF(3, 4),
	OC_RP_DEF(3, 6),
	OC_RP_DEF(3, 8),
	OC_RP_DEF(3, 12),
	OC_RP_DEF(3, 16),
	OC_RP_DEF(3, 32),
	OC_RP_DEF_GX(3),

	/* 4-replica classes */
	OC_RP_DEF(4, 1),
	OC_RP_DEF(4, 2),
	OC_RP_DEF(4, 4),
	OC_RP_DEF(4, 6),
	OC_RP_DEF(4, 8),
	OC_RP_DEF(4, 12),
	OC_RP_DEF(4, 16),
	OC_RP_DEF(4, 32),
	OC_RP_DEF_GX(4),

	/* 5-replica classes */
	OC_RP_DEF(5, 1),
	OC_RP_DEF(5, 2),
	OC_RP_DEF(5, 4),
	OC_RP_DEF(5, 6),
	OC_RP_DEF(5, 8),
	OC_RP_DEF(5, 12),
	OC_RP_DEF(5, 16),
	OC_RP_DEF(5, 32),
	OC_RP_DEF_GX(5),

	/* 6-replica classes */
	OC_RP_DEF(6, 1),
	OC_RP_DEF(6, 2),
	OC_RP_DEF(6, 4),
	OC_RP_DEF(6, 6),
	OC_RP_DEF(6, 8),
	OC_RP_DEF(6, 12),
	OC_RP_DEF(6, 16),
	OC_RP_DEF(6, 32),
	OC_RP_DEF_GX(6),

	/* extremely scalable fetch */
	OC_RP_XSF(),

	/* EC(2+1) classes */
	OC_EC_DEF(2, 1, 1),
	OC_EC_DEF(2, 1, 2),
	OC_EC_DEF(2, 1, 4),
	OC_EC_DEF(2, 1, 8),
	OC_EC_DEF(2, 1, 12),
	OC_EC_DEF(2, 1, 16),
	OC_EC_DEF(2, 1, 32),
	OC_EC_DEF_GX(2, 1),

	/* EC(2+2) classes */
	OC_EC_DEF(2, 2, 1),
	OC_EC_DEF(2, 2, 2),
	OC_EC_DEF(2, 2, 4),
	OC_EC_DEF(2, 2, 8),
	OC_EC_DEF(2, 2, 12),
	OC_EC_DEF(2, 2, 16),
	OC_EC_DEF(2, 2, 32),
	OC_EC_DEF_GX(2, 2),

	/* EC(4+1) classes */
	OC_EC_DEF(4, 1, 1),
	OC_EC_DEF(4, 1, 2),
	OC_EC_DEF(4, 1, 4),
	OC_EC_DEF(4, 1, 8),
	OC_EC_DEF(4, 1, 12),
	OC_EC_DEF(4, 1, 16),
	OC_EC_DEF(4, 1, 32),
	OC_EC_DEF_GX(4, 1),

	/* EC(4+2) classes */
	OC_EC_DEF(4, 2, 1),
	OC_EC_DEF(4, 2, 2),
	OC_EC_DEF(4, 2, 4),
	OC_EC_DEF(4, 2, 8),
	OC_EC_DEF(4, 2, 12),
	OC_EC_DEF(4, 2, 16),
	OC_EC_DEF(4, 2, 32),
	OC_EC_DEF_GX(4, 2),

	/* EC(8+1) classes */
	OC_EC_DEF(8, 1, 1),
	OC_EC_DEF(8, 1, 2),
	OC_EC_DEF(8, 1, 4),
	OC_EC_DEF(8, 1, 8),
	OC_EC_DEF(8, 1, 12),
	OC_EC_DEF(8, 1, 16),
	OC_EC_DEF(8, 1, 32),
	OC_EC_DEF_GX(8, 1),

	/* EC(8+2) classes */
	OC_EC_DEF(8, 2, 1),
	OC_EC_DEF(8, 2, 2),
	OC_EC_DEF(8, 2, 4),
	OC_EC_DEF(8, 2, 8),
	OC_EC_DEF(8, 2, 12),
	OC_EC_DEF(8, 2, 16),
	OC_EC_DEF(8, 2, 32),
	OC_EC_DEF_GX(8, 2),

	/* EC(16+1) classes */
	OC_EC_DEF(16, 1, 1),
	OC_EC_DEF(16, 1, 2),
	OC_EC_DEF(16, 1, 4),
	OC_EC_DEF(16, 1, 8),
	OC_EC_DEF(16, 1, 12),
	OC_EC_DEF(16, 1, 16),
	OC_EC_DEF(16, 1, 32),
	OC_EC_DEF_GX(16, 1),

	/* EC(16+2) classes */
	OC_EC_DEF(16, 2, 1),
	OC_EC_DEF(16, 2, 2),
	OC_EC_DEF(16, 2, 4),
	OC_EC_DEF(16, 2, 8),
	OC_EC_DEF(16, 2, 12),
	OC_EC_DEF(16, 2, 16),
	OC_EC_DEF(16, 2, 32),
	OC_EC_DEF_GX(16, 2),

	/* Internal classes: echo objects */
	OC_RP_ECHO_DEF(1),
	OC_RP_ECHO_DEF(2),
	OC_RP_ECHO_DEF(3),
	OC_RP_ECHO_DEF(4),

	/* Internal classes: specified rank */
	OC_RP_SRANK_DEF(1, 1),
	OC_RP_SRANK_DEF(2, 1),
	OC_RP_SRANK_DEF(3, 1),

	/* The last one */
	{
		.oc_name	= NULL,
		.oc_id		= OC_UNKNOWN,
		.oc_private	= true,
	},
};

int
daos_oclass_nr(int opts)
{
	return ARRAY_SIZE(daos_obj_classes);
}
