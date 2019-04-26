/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(object)

#include "obj_internal.h"
#include <daos_api.h>

/** DAOS object class */
struct daos_obj_class {
	/** class name */
	char				*oc_name;
	/** unique class ID */
	daos_oclass_id_t		 oc_id;
	struct daos_oclass_attr		 oc_attr;
};

/** predefined object classes */
static struct daos_obj_class daos_obj_classes[] = {
	{
		.oc_name	= "tiny_rw",
		.oc_id		= DAOS_OC_TINY_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 1,
			},
		},
	},
	{
		.oc_name	= "small_rw",
		.oc_id		= DAOS_OC_SMALL_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 4,
			.u.repl			= {
				.r_num		= 1,
			},
		},
	},
	{
		.oc_name	= "large_rw",
		.oc_id		= DAOS_OC_LARGE_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.u.repl			= {
				.r_num		= 1,
			},
		},
	},
	{
		.oc_name	= "repl_2_small_rw",
		.oc_id		= DAOS_OC_R2S_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 2,
			},
		},
	},
	{
		.oc_name	= "repl_2_rw",
		.oc_id		= DAOS_OC_R2_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.u.repl			= {
				.r_num		= 2,
			},
		},
	},
	{
		.oc_name	= "repl_3_small_rw",
		.oc_id		= DAOS_OC_R3S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 3,
			},
		},
	},
	{
		.oc_name	= "repl_3_rw",
		.oc_id		= DAOS_OC_R3_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.u.repl			= {
				.r_num		= 3,
			},
		},
	},
	{
		.oc_name	= "repl_4_small_rw",
		.oc_id		= DAOS_OC_R4S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 4,
			},
		},
	},
	{
		.oc_name	= "repl_4_rw",
		.oc_id		= DAOS_OC_R4_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.u.repl			= {
				.r_num		= 4,
			},
		},
	},
	{
		.oc_name	= "repl_max_rw",
		.oc_id		= DAOS_OC_REPL_MAX_RW,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= DAOS_OBJ_REPL_MAX,
			},
		},
	}, {
		.oc_name	= "echo_rw",
		.oc_id		= DAOS_OC_ECHO_TINY_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 1,
			},
		},
	}, {
		.oc_name	= "echo_rw",
		.oc_id		= DAOS_OC_ECHO_R2S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 2,
			},
		},
	}, {
		.oc_name	= "echo_rw",
		.oc_id		= DAOS_OC_ECHO_R3S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 3,
			},
		},
	}, {
		.oc_name	= "echo_rw",
		.oc_id		= DAOS_OC_ECHO_R4S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 4,
			},
		},
	}, {
		.oc_name	= "repl_3_small_rw_spec_rank",
		.oc_id		= DAOS_OC_R3S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 3,
			},
		},
	},
	{
		.oc_name	= "repl_2_small_rw_spec_rank",
		.oc_id		= DAOS_OC_R2S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 2,
			},
		},
	},
	{
		.oc_name	= "repl_1_small_rw_spec_rank",
		.oc_id		= DAOS_OC_R1S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.u.repl			= {
				.r_num		= 1,
			},
		},
	},
	{
		.oc_name	= "ec_k2p2_len32k",
		.oc_id		= DAOS_OC_EC_K2P2_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.u.ec			= {
				.e_k		= 2,
				.e_p		= 2,
				.e_len		= 1 << 15,
			},
		},
	},
	{
		.oc_name	= "ec_k8p2_len1m",
		.oc_id		= DAOS_OC_EC_K8P2_L1M,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.u.ec			= {
				.e_k		= 8,
				.e_p		= 2,
				.e_len		= 1 << 20,
			},
		},
	},
	{
		.oc_name	= NULL,
		.oc_id		= DAOS_OC_UNKNOWN,
	},
};

/** find the object class attributes for the provided @oid */
struct daos_oclass_attr *
daos_oclass_attr_find(daos_obj_id_t oid)
{
	struct daos_obj_class	*oc;
	daos_oclass_id_t	 ocid;

	/* see daos_objid_generate */
	ocid = daos_obj_id2class(oid);
	for (oc = &daos_obj_classes[0]; oc->oc_id != DAOS_OC_UNKNOWN; oc++) {
		if (oc->oc_id == ocid)
			break;
	}

	if (oc->oc_id == DAOS_OC_UNKNOWN) {
		D_DEBUG(DB_PL, "Unknown object class %d for "DF_OID"\n",
			ocid, DP_OID(oid));
		return NULL;
	}

	D_DEBUG(DB_PL, "Find class %s for oid "DF_OID"\n",
		oc->oc_name, DP_OID(oid));
	return &oc->oc_attr;
}

int
daos_oclass_name2id(const char *name)
{
	struct daos_obj_class	*oc;

	for (oc = &daos_obj_classes[0]; oc->oc_id != DAOS_OC_UNKNOWN; oc++) {
		if (strncmp(oc->oc_name, name, strlen(name)) == 0)
			break;
	}
	if (oc->oc_id == DAOS_OC_UNKNOWN)
		return -1;

	return oc->oc_id;
}

/** Return the redundancy group size of @oc_attr */
unsigned int
daos_oclass_grp_size(struct daos_oclass_attr *oc_attr)
{
	switch (oc_attr->ca_resil) {
	default:
		return -DER_INVAL;

	case DAOS_RES_REPL:
		return oc_attr->u.repl.r_num;

	case DAOS_RES_EC:
		return oc_attr->u.ec.e_k + oc_attr->u.ec.e_p;
	}
}

int
dc_oclass_register(daos_handle_t coh, daos_oclass_id_t cid,
		   daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
dc_oclass_query(daos_handle_t coh, daos_oclass_id_t cid,
		daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
dc_oclass_list(daos_handle_t coh, daos_oclass_list_t *clist,
	       daos_anchor_t *anchor, daos_event_t *ev)
{
	return -DER_NOSYS;
}

/**
 * Return the number of redundancy groups for the object class @oc_attr with
 * the provided metadata @md
 */
unsigned int
daos_oclass_grp_nr(struct daos_oclass_attr *oc_attr, struct daos_obj_md *md)
{
	/* NB: @md is unsupported for now */
	return oc_attr->ca_grp_nr;
}

/** a structure to map EC object class to EC codec structure */
struct daos_oc_ec_codec {
	/** object class id */
	daos_oclass_id_t	 ec_oc_id;
	/** pointer to EC codec */
	struct obj_ec_codec	 ec_codec;
};

static struct daos_oc_ec_codec	*oc_ec_codecs;
static int			 oc_ec_codec_nr;

void
obj_ec_codec_fini(void)
{
	struct obj_ec_codec	*ec_codec;
	struct daos_obj_class	*oc;
	int			 ocnr = 0;
	int			 i;

	if (oc_ec_codecs == NULL)
		return;

	for (oc = &daos_obj_classes[0]; oc->oc_id != DAOS_OC_UNKNOWN; oc++) {
		if (oc->oc_attr.ca_resil == DAOS_RES_EC)
			ocnr++;
	}
	D_ASSERTF(oc_ec_codec_nr == ocnr,
		  "oc_ec_codec_nr %d mismatch with ocnr %d.\n",
		  oc_ec_codec_nr, ocnr);

	for (i = 0; i < ocnr; i++) {
		ec_codec = &oc_ec_codecs[i++].ec_codec;
		if (ec_codec->ec_en_matrix != NULL)
			D_FREE(ec_codec->ec_en_matrix);
		if (ec_codec->ec_gftbls != NULL)
			D_FREE(ec_codec->ec_gftbls);
	}

	D_FREE(oc_ec_codecs);
	oc_ec_codecs = NULL;
	oc_ec_codec_nr = 0;
}

int
obj_ec_codec_init()
{
	struct obj_ec_codec	*ec_codec;
	struct daos_obj_class	*oc;
	unsigned char		*encode_matrix = NULL;
	int			 ocnr;
	int			 i;
	int			 k, p, m;
	int			 rc;

	if (oc_ec_codecs != NULL)
		return 0;

	ocnr = 0;
	for (oc = &daos_obj_classes[0]; oc->oc_id != DAOS_OC_UNKNOWN; oc++) {
		if (oc->oc_attr.ca_resil == DAOS_RES_EC)
			ocnr++;
	}
	if (ocnr == 0)
		return 0;

	D_ALLOC_ARRAY(oc_ec_codecs, ocnr);
	if (oc_ec_codecs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);
	oc_ec_codec_nr = ocnr;

	i = 0;
	for (oc = &daos_obj_classes[0]; oc->oc_id != DAOS_OC_UNKNOWN; oc++) {
		if (oc->oc_attr.ca_resil != DAOS_RES_EC)
			continue;

		oc_ec_codecs[i].ec_oc_id = oc->oc_id;
		ec_codec = &oc_ec_codecs[i++].ec_codec;
		k = oc->oc_attr.u.ec.e_k;
		p = oc->oc_attr.u.ec.e_p;
		m = k + p;
		/* 32B needed for data generated for each input coefficient */
		D_ALLOC(ec_codec->ec_gftbls, k * p * 32);
		if (ec_codec->ec_gftbls == NULL)
			D_GOTO(failed, rc = -DER_NOMEM);
		D_ALLOC(encode_matrix, m * k);
		if (encode_matrix == NULL)
			D_GOTO(failed, rc = -DER_NOMEM);
		ec_codec->ec_en_matrix = encode_matrix;
		/* A Cauchy matrix is always invertible, the recovery rule is
		 * simpler than gf_gen_rs_matrix (vandermonde matrix).
		 */
		gf_gen_cauchy1_matrix(encode_matrix, m, k);
		/* Initialize gf tables from encode matrix */
		ec_init_tables(k, p, &encode_matrix[k * k],
			       ec_codec->ec_gftbls);
	}

	D_ASSERT(i == ocnr);
	return 0;

failed:
	obj_ec_codec_fini();
	return rc;
}

struct obj_ec_codec *
obj_ec_codec_get(daos_oclass_id_t oc_id)
{
	struct daos_oc_ec_codec *oc_ec_codec;
	int			 i;

	if (oc_ec_codecs == NULL)
		return NULL;

	D_ASSERT(oc_ec_codec_nr >= 1);
	for (i = 0; i < oc_ec_codec_nr; i++) {
		oc_ec_codec = &oc_ec_codecs[i];
		D_ASSERT(oc_ec_codec->ec_codec.ec_en_matrix != NULL);
		D_ASSERT(oc_ec_codec->ec_codec.ec_gftbls != NULL);
		if (oc_ec_codec->ec_oc_id == oc_id)
			return &oc_ec_codec->ec_codec;
	}

	return NULL;
}
