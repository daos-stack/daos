/**
 * (C) Copyright 2016-2021 Intel Corporation.
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
#include <isa-l.h>

/** DAOS object class */
struct daos_obj_class {
	/** class name */
	char				*oc_name;
	/** unique class ID */
	daos_oclass_id_t		 oc_id;
	struct daos_oclass_attr		 oc_attr;
};

#define ca_rp_nr	u.rp.r_num
#define ca_ec_k		u.ec.e_k
#define ca_ec_p		u.ec.e_p
#define ca_ec_cell	u.ec.e_len

/** predefined object classes */
static struct daos_obj_class daos_obj_classes[] = {
	/**
	 * Object classes with no data protection.
	 */
	{
		.oc_name	= "S1",
		.oc_id		= OC_S1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 1,
		},
	},
	{
		.oc_name	= "S2",
		.oc_id		= OC_S2,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.ca_rp_nr		= 1,
		},
	},
	{
		.oc_name	= "S4",
		.oc_id		= OC_S4,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 4,
			.ca_rp_nr		= 1,
		},
	},
	/* TODO: add more */
	{
		.oc_name	= "SX",
		.oc_id		= OC_SX,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.ca_rp_nr		= 1,
		},
	},
	/**
	 * Object classes protected by 2-way replication
	 */
	{
		.oc_name	= "RP_2G1",
		.oc_id		= OC_RP_2G1,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 2,
		},
	},
	{
		.oc_name	= "RP_2G2",
		.oc_id		= OC_RP_2G2,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.ca_rp_nr		= 2,
		},
	},
	/* TODO: add more */
	{
		.oc_name	= "RP_2GX",
		.oc_id		= OC_RP_2GX,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.ca_rp_nr		= 2,
		},
	},
	/**
	 * Object classes protected by 3-way replication
	 */
	{
		.oc_name	= "RP_3G1",
		.oc_id		= OC_RP_3G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 3,
		},
	},
	{
		.oc_name	= "RP_3G2",
		.oc_id		= OC_RP_3G2,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.ca_rp_nr		= 3,
		},
	},
	/* TODO: add more */
	{
		.oc_name	= "RP_3GX",
		.oc_id		= OC_RP_3GX,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.ca_rp_nr		= 3,
		},
	},
	/**
	 * Object classes protected by 4-way replication
	 */
	{
		.oc_name	= "RP_4G1",
		.oc_id		= OC_RP_4G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 4,
		},
	},
	{
		.oc_name	= "RP_4G2",
		.oc_id		= OC_RP_4G2,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 2,
			.ca_rp_nr		= 4,
		},
	},
	/* TODO: add more */
	{
		.oc_name	= "RP_4GX",
		.oc_id		= OC_RP_4GX,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= DAOS_OBJ_GRP_MAX,
			.ca_rp_nr		= 4,
		},
	},
	/*
	 * Object class to support extremely scalable fetch
	 * It is replicated to everywhere
	 */
	{
		.oc_name	= "RP_XSF",
		.oc_id		= OC_RP_XSF,
		{
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= DAOS_OBJ_REPL_MAX,
		},
	},
	/*
	 * Internal classes
	 * XXX: needs further cleanup
	 */
	{
		.oc_name	= "S1_ECHO",
		.oc_id		= DAOS_OC_ECHO_TINY_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 1,
		},
	},
	{
		.oc_name	= "RP_2G1_ECHO",
		.oc_id		= DAOS_OC_ECHO_R2S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 2,
		},
	},
	{
		.oc_name	= "RP_3G1_ECHO",
		.oc_id		= DAOS_OC_ECHO_R3S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 3,
		},
	},
	{
		.oc_name	= "RP_4G1_ECHO",
		.oc_id		= DAOS_OC_ECHO_R4S_RW,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 4,
		},
	},
	{
		.oc_name	= "RP_3G1_SR",
		.oc_id		= DAOS_OC_R3S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 3,
		},
	},
	{
		.oc_name	= "RP_2G1_SR",
		.oc_id		= DAOS_OC_R2S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 2,
		},
	},
	{
		.oc_name	= "S1_SR",
		.oc_id		= DAOS_OC_R1S_SPEC_RANK,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 1,
		},
	},
	{
		.oc_name	= "OBJ_ID_TABLE",
		.oc_id		= DAOS_OC_OIT,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_REPL,
			/* XXX use 1 replica and 1 groop for simplicity,
			 * it should be more scalable
			 */
			.ca_grp_nr		= 1,
			.ca_rp_nr		= 1,
		},
	},
	{
		.oc_name	= "EC_2P1G1",
		.oc_id		= OC_EC_2P1G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 2,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "DAOS_OC_EC_K2P1_L32K",
		.oc_id		= DAOS_OC_EC_K2P1_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 2,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= "EC_2P2G1",
		.oc_id		= OC_EC_2P2G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 2,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "DAOS_OC_EC_K2P2_L32K",
		.oc_id		= DAOS_OC_EC_K2P2_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 2,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= "EC_4P1G1",
		.oc_id		= OC_EC_4P1G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 4,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "EC_4P2G1",
		.oc_id		= OC_EC_4P2G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 4,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "DAOS_OC_EC_K4P1_L32K",
		.oc_id		= DAOS_OC_EC_K4P1_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 4,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= "DAOS_OC_EC_K4P2_L32K",
		.oc_id		= DAOS_OC_EC_K4P2_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 4,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= "EC_8P2G1",
		.oc_id		= OC_EC_8P2G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 8,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "EC_16P2G1",
		.oc_id		= OC_EC_16P2G1,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 16,
			.ca_ec_p		= 2,
			.ca_ec_cell		= 1 << 20,
		},
	},
	{
		.oc_name	= "EC_2P1G1_SPEC",
		.oc_id		= DAOS_OC_EC_K2P1_SPEC_RANK_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 2,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= "EC_4P1G1_SPEC",
		.oc_id		= DAOS_OC_EC_K4P1_SPEC_RANK_L32K,
		{
			.ca_schema		= DAOS_OS_SINGLE,
			.ca_resil		= DAOS_RES_EC,
			.ca_grp_nr		= 1,
			.ca_ec_k		= 4,
			.ca_ec_p		= 1,
			.ca_ec_cell		= 1 << 15,
		},
	},
	{
		.oc_name	= NULL,
		.oc_id		= OC_UNKNOWN,
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
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (oc->oc_id == ocid)
			break;
	}

	if (oc->oc_id == OC_UNKNOWN) {
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

	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (strncmp(oc->oc_name, name, strlen(name)) == 0)
			return oc->oc_id;
	}

	D_ASSERT(oc->oc_id == OC_UNKNOWN);
	return OC_UNKNOWN;
}

int
daos_oclass_id2name(daos_oclass_id_t oc_id, char *str)
{
	struct daos_obj_class   *oc;

	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (oc->oc_id == oc_id) {
			strcpy(str, oc->oc_name);
			return 0;
		}
	}

	D_ASSERT(oc->oc_id == OC_UNKNOWN);
	strcpy(str, "UNKNOWN");
	return -1;
}

/** Return the list of registered oclass names */
size_t
daos_oclass_names_list(size_t size, char *str)
{
	struct daos_obj_class   *oc;
	size_t len = 0;

	if (size <= 0 || str == NULL)
		return -1;

	*str = '\0';
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		len += strlen(oc->oc_name) + 2;
		if (len < size) {
			strcat(str, oc->oc_name);
			strcat(str, ", ");
		}
	}
	return len;
}

/** Return the redundancy group size of @oc_attr */
unsigned int
daos_oclass_grp_size(struct daos_oclass_attr *oc_attr)
{
	switch (oc_attr->ca_resil) {
	default:
		return -DER_INVAL;

	case DAOS_RES_REPL:
		return oc_attr->ca_rp_nr;

	case DAOS_RES_EC:
		return oc_attr->ca_ec_k + oc_attr->ca_ec_p;
	}
}

int
dc_oclass_register(daos_handle_t coh, daos_oclass_id_t cid,
		   struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
dc_oclass_query(daos_handle_t coh, daos_oclass_id_t cid,
		struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
dc_oclass_list(daos_handle_t coh, struct daos_oclass_list *clist,
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

	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (DAOS_OC_IS_EC(&oc->oc_attr))
			ocnr++;
	}
	D_ASSERTF(oc_ec_codec_nr == ocnr,
		  "oc_ec_codec_nr %d mismatch with ocnr %d.\n",
		  oc_ec_codec_nr, ocnr);

	for (i = 0; i < ocnr; i++) {
		ec_codec = &oc_ec_codecs[i].ec_codec;
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
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (DAOS_OC_IS_EC(&oc->oc_attr))
			ocnr++;
	}
	if (ocnr == 0)
		return 0;

	D_ALLOC_ARRAY(oc_ec_codecs, ocnr);
	if (oc_ec_codecs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);
	oc_ec_codec_nr = ocnr;

	i = 0;
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (!DAOS_OC_IS_EC(&oc->oc_attr))
			continue;

		oc_ec_codecs[i].ec_oc_id = oc->oc_id;
		ec_codec = &oc_ec_codecs[i++].ec_codec;
		k = oc->oc_attr.ca_ec_k;
		p = oc->oc_attr.ca_ec_p;
		if (k > OBJ_EC_MAX_K || p > OBJ_EC_MAX_P) {
			D_ERROR("invalid k %d p %d (max k %d, max p %d)\n",
				k, p, OBJ_EC_MAX_K, OBJ_EC_MAX_P);
			D_GOTO(failed, rc = -DER_INVAL);
		}
		if (k < 2 || p < 1) {
			D_ERROR("invalid k %d / p %d (min k 2, min p 1).\n",
				k, p);
			D_GOTO(failed, rc = -DER_INVAL);
		}
		if (p > k) {
			D_ERROR("invalid k %d p %d (parity target number cannot"
				" exceed data target number).\n", k, p);
			D_GOTO(failed, rc = -DER_INVAL);
		}
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

/**
 * Encode (using ISA-L) a full stripe from the submitted scatter-gather list.
 *
 * oid		[IN]		The object id of the object undergoing encode.
 * sgl		[IN]		The SGL containing the user data.
 * sg_idx	[IN|OUT]	Index of sg_iov entry in array.
 * sg_off	[IN|OUT]	Offset into sg_iovs' io_buf.
 * parity	[IN|OUT]	Struct containing parity buffers.
 * p_idx	[IN]		Index into parity p_bufs array.
 */
int
obj_encode_full_stripe(daos_obj_id_t oid, d_sg_list_t *sgl, uint32_t *sg_idx,
		       size_t *sg_off, struct obj_ec_parity *parity,
		       uint32_t p_idx)
{
	struct obj_ec_codec		*codec = obj_ec_codec_get(
							daos_obj_id2class(oid));
	struct daos_oclass_attr		*oca = daos_oclass_attr_find(oid);
	unsigned int			 len = oca->ca_ec_cell;
	unsigned int			 k = oca->ca_ec_k;
	unsigned int			 p = oca->ca_ec_p;
	unsigned char			*data[k];
	unsigned char			*ldata[k];
	int				 i, lcnt = 0;
	int				 rc = 0;

	for (i = 0; i < k; i++) {
		if (sgl->sg_iovs[*sg_idx].iov_len - *sg_off >= len) {
			unsigned char *from =
				(unsigned char *)sgl->sg_iovs[*sg_idx].iov_buf;

			data[i] = &from[*sg_off];
			*sg_off += len;
			if (*sg_off == sgl->sg_iovs[*sg_idx].iov_len) {
				*sg_off = 0;
				(*sg_idx)++;
			}
		} else {
			int cp_cnt = 0;

			D_ALLOC(ldata[lcnt], len);
			if (ldata[lcnt] == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			while (cp_cnt < len) {
				int cp_amt =
					sgl->sg_iovs[*sg_idx].iov_len-*sg_off <
					len - cp_cnt ?
					sgl->sg_iovs[*sg_idx].iov_len-*sg_off :
					len - cp_cnt;
				unsigned char *from =
					sgl->sg_iovs[*sg_idx].iov_buf;

				memcpy(&ldata[lcnt][cp_cnt], &from[*sg_off],
				       cp_amt);
				if (sgl->sg_iovs[*sg_idx].iov_len - *sg_off <=
					len - cp_cnt) {
					*sg_off = 0;
					(*sg_idx)++;
				} else
					*sg_off += cp_amt;
				cp_cnt += cp_amt;
				if (cp_cnt < len && *sg_idx >= sgl->sg_nr)
					D_GOTO(out, rc = -DER_INVAL);
			}
			data[i] = ldata[lcnt++];
		}
	}

	ec_encode_data(len, k, p, codec->ec_gftbls, data,
		       &parity->p_bufs[p_idx]);
out:
	for (i = 0; i < lcnt; i++)
		D_FREE(ldata[i]);
	return rc;
}
