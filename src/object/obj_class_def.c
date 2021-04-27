/**
 * (C) Copyright 2021 Intel Corporation.
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
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= 1,			\
	}						\
}

/* single replica, groups = (g * 1024) */
#define OC_SS_DEF_GK(g)					\
{							\
	.oc_name	= "S"#g"K",			\
	.oc_id		= OC_S##g##K,			\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= (g) << 10,		\
		.ca_rp_nr	= 1,			\
	}						\
}

/* single replica, maximum groups */
#define OC_SS_DEF_GX()					\
{							\
	.oc_name	= "SX",				\
	.oc_id		= OC_SX,			\
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
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= r,			\
	}						\
}

/* replicas=r, groups = (g * 1024) */
#define OC_RP_DEF_GK(r, g)				\
{							\
	.oc_name	= "RP_"#r"G"#g"K",		\
	.oc_id		= OC_RP_##r##G##g##K,		\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= (g) << 10,		\
		.ca_rp_nr	= r,			\
	}						\
}

/* replicas=r, maximum groups */
#define OC_RP_DEF_GX(r)					\
{							\
	.oc_name	= "RP_"#r"GX",			\
	.oc_id		= OC_RP_##r##GX,		\
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
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= g,			\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
		.ca_ec_cell	= (1 << 20),		\
	}						\
}

/* EC(k+p), groups = (g * 1024) */
#define OC_EC_DEF_GK(k, p, g)				\
{							\
	.oc_name	= "EC_"#k"P"#p"G"#g"K",		\
	.oc_id		= OC_EC_##k##P##p##G##g##K,	\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= (g) << 10,		\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
		.ca_ec_cell	= (1 << 20),		\
	}						\
}

/* EC(k+p), maximum groups */
#define OC_EC_DEF_GX(k, p)				\
{							\
	.oc_name	= "EC_"#k"P"#p"GX",		\
	.oc_id		= OC_EC_##k##P##p##GX,		\
	{						\
		.ca_schema	= DAOS_OS_STRIPED,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= DAOS_OBJ_GRP_MAX,	\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
		.ca_ec_cell	= (1 << 20),		\
	}						\
}

/* Internal classes */

/* OI table class, redundancy_factor=rf.
 * XXX use 1 group for simplicity, it should be more scalable.
 */
#define OC_RP_OIT_DEF(rf)					\
{							\
	.oc_name	= "OBJ_ID_TABLE_RF"#rf,		\
	.oc_id		= DAOS_OC_OIT_RF##rf,		\
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= 1,			\
		.ca_rp_nr	= (rf + 1),		\
	},						\
	.oc_private	= true,				\
}

/* echo object class */
#define OC_RP_ECHO_DEF(r)				\
{							\
	.oc_name	= "RP_"#r"G1_ECHO",		\
	.oc_id		= DAOS_OC_ECHO_R##r##S_RW,	\
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
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_REPL,	\
		.ca_grp_nr	= g,			\
		.ca_rp_nr	= r,			\
	},						\
	.oc_private	= true,				\
}

/* EC(k+p), 32K cell size, this is really for debugging, because cell size
 * should be part of the pool/container properties
 */
#define OC_EC_32K_DEF(k, p, g)				\
{							\
	.oc_name	= "EC_K"#k"P"#p"_L32K",		\
	.oc_id		= DAOS_OC_EC_K##k##P##p##_L32K,	\
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= g,			\
		.ca_ec_k	= k,			\
		.ca_ec_p	= p,			\
		.ca_ec_cell	= (1 << 15),		\
	}						\
}

/* EC(k+p), 32K cell size and specified rank, this is for debugging */
#define OC_EC_SRANK_32K_DEF(k, p, g)			\
{							\
	.oc_name	= "EC_SRANK_K"#k"P"#p"_L32K",	\
	.oc_id		=				\
		DAOS_OC_EC_K##k##P##p##_SPEC_RANK_L32K,	\
	{						\
		.ca_schema	= DAOS_OS_SINGLE,	\
		.ca_resil	= DAOS_RES_EC,		\
		.ca_grp_nr	= 1,			\
		.ca_ec_k	= 2,			\
		.ca_ec_p	= 1,			\
		.ca_ec_cell	= 1 << 15,		\
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
	OC_SS_DEF(24),
	OC_SS_DEF(32),
	OC_SS_DEF(48),
	OC_SS_DEF(64),
	OC_SS_DEF(96),
	OC_SS_DEF(128),
	OC_SS_DEF(192),
	OC_SS_DEF(256),
	OC_SS_DEF(384),
	OC_SS_DEF(512),
	OC_SS_DEF(768),
	OC_SS_DEF_GK(1),
	OC_SS_DEF_GK(2),
	OC_SS_DEF_GK(4),
	OC_SS_DEF_GK(6),
	OC_SS_DEF_GK(8),
	OC_SS_DEF_GX(),

	/* 2-replica classes */
	OC_RP_DEF(2, 1),
	OC_RP_DEF(2, 2),
	OC_RP_DEF(2, 3),
	OC_RP_DEF(2, 4),
	OC_RP_DEF(2, 6),
	OC_RP_DEF(2, 8),
	OC_RP_DEF(2, 12),
	OC_RP_DEF(2, 16),
	OC_RP_DEF(2, 24),
	OC_RP_DEF(2, 32),
	OC_RP_DEF(2, 48),
	OC_RP_DEF(2, 64),
	OC_RP_DEF(2, 96),
	OC_RP_DEF(2, 128),
	OC_RP_DEF(2, 192),
	OC_RP_DEF(2, 256),
	OC_RP_DEF(2, 384),
	OC_RP_DEF(2, 512),
	OC_RP_DEF(2, 768),
	OC_RP_DEF_GK(2, 1),
	OC_RP_DEF_GK(2, 2),
	OC_RP_DEF_GK(2, 4),
	OC_RP_DEF_GK(2, 6),
	OC_RP_DEF_GK(2, 8),
	OC_RP_DEF_GX(2),

	/* 3-replica classes */
	OC_RP_DEF(3, 1),
	OC_RP_DEF(3, 2),
	OC_RP_DEF(3, 4),
	OC_RP_DEF(3, 6),
	OC_RP_DEF(3, 8),
	OC_RP_DEF(3, 12),
	OC_RP_DEF(3, 16),
	OC_RP_DEF(3, 24),
	OC_RP_DEF(3, 32),
	OC_RP_DEF(3, 48),
	OC_RP_DEF(3, 64),
	OC_RP_DEF(3, 96),
	OC_RP_DEF(3, 128),
	OC_RP_DEF(3, 192),
	OC_RP_DEF(3, 256),
	OC_RP_DEF(3, 384),
	OC_RP_DEF(3, 512),
	OC_RP_DEF(3, 768),
	OC_RP_DEF_GK(3, 1),
	OC_RP_DEF_GK(3, 2),
	OC_RP_DEF_GK(3, 4),
	OC_RP_DEF_GK(3, 6),
	OC_RP_DEF_GK(3, 8),
	OC_RP_DEF_GX(3),

	/* 4-replica classes */
	OC_RP_DEF(4, 1),
	OC_RP_DEF(4, 2),
	OC_RP_DEF(4, 4),
	OC_RP_DEF(4, 6),
	OC_RP_DEF(4, 8),
	OC_RP_DEF(4, 12),
	OC_RP_DEF(4, 16),
	OC_RP_DEF(4, 24),
	OC_RP_DEF(4, 32),
	OC_RP_DEF(4, 48),
	OC_RP_DEF(4, 64),
	OC_RP_DEF(4, 96),
	OC_RP_DEF(4, 128),
	OC_RP_DEF(4, 192),
	OC_RP_DEF(4, 256),
	OC_RP_DEF(4, 384),
	OC_RP_DEF(4, 512),
	OC_RP_DEF(4, 768),
	OC_RP_DEF_GK(4, 1),
	OC_RP_DEF_GK(4, 2),
	OC_RP_DEF_GK(4, 4),
	OC_RP_DEF_GK(4, 6),
	OC_RP_DEF_GK(4, 8),
	OC_RP_DEF_GX(4),

	/* 6-replica classes */
	OC_RP_DEF(6, 1),
	OC_RP_DEF(6, 2),
	OC_RP_DEF(6, 4),
	OC_RP_DEF(6, 6),
	OC_RP_DEF(6, 8),
	OC_RP_DEF(6, 12),
	OC_RP_DEF(6, 16),
	OC_RP_DEF(6, 24),
	OC_RP_DEF(6, 32),
	OC_RP_DEF(6, 48),
	OC_RP_DEF(6, 64),
	OC_RP_DEF(6, 96),
	OC_RP_DEF(6, 128),
	OC_RP_DEF(6, 192),
	OC_RP_DEF(6, 256),
	OC_RP_DEF(6, 384),
	OC_RP_DEF(6, 512),
	OC_RP_DEF(6, 768),
	OC_RP_DEF_GK(6, 1),
	OC_RP_DEF_GK(6, 2),
	OC_RP_DEF_GK(6, 4),
	OC_RP_DEF_GK(6, 6),
	OC_RP_DEF_GK(6, 8),
	OC_RP_DEF_GX(6),

	/* 8-replica classes */
	OC_RP_DEF(8, 1),
	OC_RP_DEF(8, 2),
	OC_RP_DEF(8, 4),
	OC_RP_DEF(8, 6),
	OC_RP_DEF(8, 8),
	OC_RP_DEF(8, 12),
	OC_RP_DEF(8, 16),
	OC_RP_DEF(8, 24),
	OC_RP_DEF(8, 32),
	OC_RP_DEF(8, 48),
	OC_RP_DEF(8, 64),
	OC_RP_DEF(8, 96),
	OC_RP_DEF(8, 128),
	OC_RP_DEF(8, 192),
	OC_RP_DEF(8, 256),
	OC_RP_DEF(8, 384),
	OC_RP_DEF(8, 512),
	OC_RP_DEF(8, 768),
	OC_RP_DEF_GK(8, 1),
	OC_RP_DEF_GK(8, 2),
	OC_RP_DEF_GK(8, 4),
	OC_RP_DEF_GK(8, 6),
	OC_RP_DEF_GK(8, 8),
	OC_RP_DEF_GX(8),

	/* [12, 16, 24, 32, 48, 64]-replica classes, these are mostly used by
	 * special objects like superblock
	 */
	OC_RP_DEF(12, 1),
	OC_RP_DEF(16, 1),
	OC_RP_DEF(24, 1),
	OC_RP_DEF(32, 1),
	OC_RP_DEF(48, 1),
	OC_RP_DEF(64, 1),

	/* extremely scalable fetch */
	OC_RP_XSF(),

	/* EC(2+1) classes */
	OC_EC_DEF(2, 1, 1),
	OC_EC_DEF(2, 1, 2),
	OC_EC_DEF(2, 1, 4),
	OC_EC_DEF(2, 1, 8),
	OC_EC_DEF(2, 1, 12),
	OC_EC_DEF(2, 1, 16),
	OC_EC_DEF(2, 1, 24),
	OC_EC_DEF(2, 1, 32),
	OC_EC_DEF(2, 1, 48),
	OC_EC_DEF(2, 1, 64),
	OC_EC_DEF(2, 1, 96),
	OC_EC_DEF(2, 1, 128),
	OC_EC_DEF(2, 1, 192),
	OC_EC_DEF(2, 1, 256),
	OC_EC_DEF(2, 1, 384),
	OC_EC_DEF(2, 1, 512),
	OC_EC_DEF(2, 1, 768),
	OC_EC_DEF_GK(2, 1, 1),
	OC_EC_DEF_GK(2, 1, 2),
	OC_EC_DEF_GK(2, 1, 4),
	OC_EC_DEF_GK(2, 1, 6),
	OC_EC_DEF_GK(2, 1, 8),
	OC_EC_DEF_GX(2, 1),

	/* EC(2+2) classes */
	OC_EC_DEF(2, 2, 1),
	OC_EC_DEF(2, 2, 2),
	OC_EC_DEF(2, 2, 4),
	OC_EC_DEF(2, 2, 8),
	OC_EC_DEF(2, 2, 12),
	OC_EC_DEF(2, 2, 16),
	OC_EC_DEF(2, 2, 24),
	OC_EC_DEF(2, 2, 32),
	OC_EC_DEF(2, 2, 48),
	OC_EC_DEF(2, 2, 64),
	OC_EC_DEF(2, 2, 96),
	OC_EC_DEF(2, 2, 128),
	OC_EC_DEF(2, 2, 192),
	OC_EC_DEF(2, 2, 256),
	OC_EC_DEF(2, 2, 384),
	OC_EC_DEF(2, 2, 512),
	OC_EC_DEF(2, 2, 768),
	OC_EC_DEF_GK(2, 2, 1),
	OC_EC_DEF_GK(2, 2, 2),
	OC_EC_DEF_GK(2, 2, 4),
	OC_EC_DEF_GK(2, 2, 6),
	OC_EC_DEF_GK(2, 2, 8),
	OC_EC_DEF_GX(2, 2),

	/* EC(4+1) classes */
	OC_EC_DEF(4, 1, 1),
	OC_EC_DEF(4, 1, 2),
	OC_EC_DEF(4, 1, 4),
	OC_EC_DEF(4, 1, 8),
	OC_EC_DEF(4, 1, 12),
	OC_EC_DEF(4, 1, 16),
	OC_EC_DEF(4, 1, 24),
	OC_EC_DEF(4, 1, 32),
	OC_EC_DEF(4, 1, 48),
	OC_EC_DEF(4, 1, 64),
	OC_EC_DEF(4, 1, 96),
	OC_EC_DEF(4, 1, 128),
	OC_EC_DEF(4, 1, 192),
	OC_EC_DEF(4, 1, 256),
	OC_EC_DEF(4, 1, 384),
	OC_EC_DEF(4, 1, 512),
	OC_EC_DEF(4, 1, 768),
	OC_EC_DEF_GK(4, 1, 1),
	OC_EC_DEF_GK(4, 1, 2),
	OC_EC_DEF_GK(4, 1, 4),
	OC_EC_DEF_GK(4, 1, 6),
	OC_EC_DEF_GK(4, 1, 8),
	OC_EC_DEF_GX(4, 1),

	/* EC(4+2) classes */
	OC_EC_DEF(4, 2, 1),
	OC_EC_DEF(4, 2, 2),
	OC_EC_DEF(4, 2, 4),
	OC_EC_DEF(4, 2, 8),
	OC_EC_DEF(4, 2, 12),
	OC_EC_DEF(4, 2, 16),
	OC_EC_DEF(4, 2, 24),
	OC_EC_DEF(4, 2, 32),
	OC_EC_DEF(4, 2, 48),
	OC_EC_DEF(4, 2, 64),
	OC_EC_DEF(4, 2, 96),
	OC_EC_DEF(4, 2, 128),
	OC_EC_DEF(4, 2, 192),
	OC_EC_DEF(4, 2, 256),
	OC_EC_DEF(4, 2, 384),
	OC_EC_DEF(4, 2, 512),
	OC_EC_DEF(4, 2, 768),
	OC_EC_DEF_GK(4, 2, 1),
	OC_EC_DEF_GK(4, 2, 2),
	OC_EC_DEF_GK(4, 2, 4),
	OC_EC_DEF_GK(4, 2, 6),
	OC_EC_DEF_GK(4, 2, 8),
	OC_EC_DEF_GX(4, 2),

	/* EC(8+1) classes */
	OC_EC_DEF(8, 1, 1),
	OC_EC_DEF(8, 1, 2),
	OC_EC_DEF(8, 1, 4),
	OC_EC_DEF(8, 1, 8),
	OC_EC_DEF(8, 1, 12),
	OC_EC_DEF(8, 1, 16),
	OC_EC_DEF(8, 1, 24),
	OC_EC_DEF(8, 1, 32),
	OC_EC_DEF(8, 1, 48),
	OC_EC_DEF(8, 1, 64),
	OC_EC_DEF(8, 1, 96),
	OC_EC_DEF(8, 1, 128),
	OC_EC_DEF(8, 1, 192),
	OC_EC_DEF(8, 1, 256),
	OC_EC_DEF(8, 1, 384),
	OC_EC_DEF(8, 1, 512),
	OC_EC_DEF(8, 1, 768),
	OC_EC_DEF_GK(8, 1, 1),
	OC_EC_DEF_GK(8, 1, 2),
	OC_EC_DEF_GK(8, 1, 4),
	OC_EC_DEF_GK(8, 1, 6),
	OC_EC_DEF_GK(8, 1, 8),
	OC_EC_DEF_GX(8, 1),

	/* EC(8+2) classes */
	OC_EC_DEF(8, 2, 1),
	OC_EC_DEF(8, 2, 2),
	OC_EC_DEF(8, 2, 4),
	OC_EC_DEF(8, 2, 8),
	OC_EC_DEF(8, 2, 12),
	OC_EC_DEF(8, 2, 16),
	OC_EC_DEF(8, 2, 24),
	OC_EC_DEF(8, 2, 32),
	OC_EC_DEF(8, 2, 48),
	OC_EC_DEF(8, 2, 64),
	OC_EC_DEF(8, 2, 96),
	OC_EC_DEF(8, 2, 128),
	OC_EC_DEF(8, 2, 192),
	OC_EC_DEF(8, 2, 256),
	OC_EC_DEF(8, 2, 384),
	OC_EC_DEF(8, 2, 512),
	OC_EC_DEF(8, 2, 768),
	OC_EC_DEF_GK(8, 2, 1),
	OC_EC_DEF_GK(8, 2, 2),
	OC_EC_DEF_GK(8, 2, 4),
	OC_EC_DEF_GK(8, 2, 6),
	OC_EC_DEF_GK(8, 2, 8),
	OC_EC_DEF_GX(8, 2),

	/* EC(16+1) classes */
	OC_EC_DEF(16, 1, 1),
	OC_EC_DEF(16, 1, 2),
	OC_EC_DEF(16, 1, 4),
	OC_EC_DEF(16, 1, 8),
	OC_EC_DEF(16, 1, 12),
	OC_EC_DEF(16, 1, 16),
	OC_EC_DEF(16, 1, 24),
	OC_EC_DEF(16, 1, 32),
	OC_EC_DEF(16, 1, 48),
	OC_EC_DEF(16, 1, 64),
	OC_EC_DEF(16, 1, 96),
	OC_EC_DEF(16, 1, 128),
	OC_EC_DEF(16, 1, 192),
	OC_EC_DEF(16, 1, 256),
	OC_EC_DEF(16, 1, 384),
	OC_EC_DEF(16, 1, 512),
	OC_EC_DEF(16, 1, 768),
	OC_EC_DEF_GK(16, 1, 1),
	OC_EC_DEF_GK(16, 1, 2),
	OC_EC_DEF_GK(16, 1, 4),
	OC_EC_DEF_GK(16, 1, 6),
	OC_EC_DEF_GK(16, 1, 8),
	OC_EC_DEF_GX(16, 1),

	/* EC(16+2) classes */
	OC_EC_DEF(16, 2, 1),
	OC_EC_DEF(16, 2, 2),
	OC_EC_DEF(16, 2, 4),
	OC_EC_DEF(16, 2, 8),
	OC_EC_DEF(16, 2, 12),
	OC_EC_DEF(16, 2, 16),
	OC_EC_DEF(16, 2, 24),
	OC_EC_DEF(16, 2, 32),
	OC_EC_DEF(16, 2, 48),
	OC_EC_DEF(16, 2, 64),
	OC_EC_DEF(16, 2, 96),
	OC_EC_DEF(16, 2, 128),
	OC_EC_DEF(16, 2, 192),
	OC_EC_DEF(16, 2, 256),
	OC_EC_DEF(16, 2, 384),
	OC_EC_DEF(16, 2, 512),
	OC_EC_DEF(16, 2, 768),
	OC_EC_DEF_GK(16, 2, 1),
	OC_EC_DEF_GK(16, 2, 2),
	OC_EC_DEF_GK(16, 2, 4),
	OC_EC_DEF_GK(16, 2, 6),
	OC_EC_DEF_GK(16, 2, 8),
	OC_EC_DEF_GX(16, 2),

	/* Internal classes: OI table */
	OC_RP_OIT_DEF(0),
	OC_RP_OIT_DEF(1),
	OC_RP_OIT_DEF(2),
	OC_RP_OIT_DEF(3),
	OC_RP_OIT_DEF(4),

	/* Internal classes: echo objects */
	OC_RP_ECHO_DEF(1),
	OC_RP_ECHO_DEF(2),
	OC_RP_ECHO_DEF(3),
	OC_RP_ECHO_DEF(4),

	/* Internal classes: specified rank */
	OC_RP_SRANK_DEF(1, 1),
	OC_RP_SRANK_DEF(2, 1),
	OC_RP_SRANK_DEF(3, 1),

	/* Internal classes: XXX for debug and development */
	OC_EC_32K_DEF(2, 1, 1),
	OC_EC_32K_DEF(2, 2, 1),
	OC_EC_32K_DEF(4, 1, 1),
	OC_EC_32K_DEF(4, 2, 1),

	OC_EC_SRANK_32K_DEF(2, 1, 1),
	OC_EC_SRANK_32K_DEF(4, 1, 1),

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
