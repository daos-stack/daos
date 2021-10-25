/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(object)

#include "obj_class.h"
#include <isa-l.h>

/** indirect indices for binary search by ID */
static struct daos_obj_class **oc_ident_array;
/** indirect indices for binary search by number of groups */
static struct daos_obj_class **oc_scale_array;
/** indirect indices for binary search by number of replicas */
static struct daos_obj_class **oc_resil_array;

static int oc_ident_array_sz;
static int oc_scale_array_sz;
static int oc_resil_array_sz;

static struct daos_obj_class  *oclass_ident2cl(daos_oclass_id_t oc_id);
static struct daos_obj_class  *oclass_scale2cl(struct daos_oclass_attr *ca);
static struct daos_obj_class  *oclass_resil2cl(struct daos_oclass_attr *ca);

/**
 * Find the object class attributes for the provided @oid.
 * NB: Because ec.e_len can be overwritten by pool/container property,
 * please don't directly use ec.e_len.
 */
struct daos_oclass_attr *
daos_oclass_attr_find(daos_obj_id_t oid, bool *is_priv)
{
	struct daos_obj_class	*oc;

	/* see daos_objid_generate */
	oc = oclass_ident2cl(daos_obj_id2class(oid));
	if (!oc) {
		D_DEBUG(DB_PL, "Unknown object class %d for "DF_OID"\n",
			daos_obj_id2class(oid), DP_OID(oid));
		return NULL;
	}
	D_DEBUG(DB_PL, "Find class %s for oid "DF_OID"\n",
		oc->oc_name, DP_OID(oid));
	if (is_priv)
		*is_priv = oc->oc_private;
	return &oc->oc_attr;
}

int
daos_oclass_id2name(daos_oclass_id_t oc_id, char *str)
{
	struct daos_obj_class   *oc;

	oc = oclass_ident2cl(oc_id);
	if (!oc) {
		strcpy(str, "UNKNOWN");
		return -1;
	}
	strcpy(str, oc->oc_name);
	return 0;
}

int
daos_oclass_name2id(const char *name)
{
	struct daos_obj_class	*oc;

	/* slow search path, it's for tool and not performance sensitive. */
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (strncmp(oc->oc_name, name, strlen(name)) == 0)
			return oc->oc_id;
	}

	D_ASSERT(oc->oc_id == OC_UNKNOWN);
	return OC_UNKNOWN;
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

bool
daos_oclass_is_valid(daos_oclass_id_t oc_id)
{
	return (oclass_ident2cl(oc_id) != NULL) ? true : false;
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

static struct daos_obj_class *
oclass_fit_max(daos_oclass_id_t oc_id, int domain_nr, int target_nr)
{
	struct daos_obj_class	*oc;
	struct daos_oclass_attr	 ca;
	int grp_size;

	oc = oclass_ident2cl(oc_id);
	if (!oc)
		return NULL;

	memcpy(&ca, &oc->oc_attr, sizeof(ca));
	if (oc_id == OC_RP_XSF) {
		D_ASSERT(ca.ca_resil_degree == DAOS_OBJ_RESIL_MAX);
		D_ASSERT(ca.ca_rp_nr == DAOS_OBJ_REPL_MAX);

		/* search for the highest possible resilience level */
		ca.ca_resil_degree = domain_nr - 1;
		ca.ca_rp_nr = domain_nr;
		oc = oclass_resil2cl(&ca);
		goto out;
	}

	grp_size = daos_oclass_grp_size(&ca);
	if (ca.ca_grp_nr == DAOS_OBJ_GRP_MAX ||
	    ca.ca_grp_nr * grp_size > target_nr) {
		/* search for the highest scalability in the allowed range */
		ca.ca_grp_nr = max(1, (target_nr / grp_size));
		oc = oclass_scale2cl(&ca);
		goto out;
	}
out:
	D_DEBUG(DB_PL, "matched object class: %s, group: %d, group_nr: %d\n",
		oc->oc_name, daos_oclass_grp_size(&ca), ca.ca_grp_nr);
	return oc;
}

int
daos_oclass_fit_max(daos_oclass_id_t oc_id, int domain_nr, int target_nr,
		    daos_oclass_id_t *oc_id_p)
{
	struct daos_obj_class	*oc;

	D_ASSERT(target_nr > 0);
	D_ASSERT(domain_nr > 0);

	oc = oclass_fit_max(oc_id, domain_nr, target_nr);
	if (oc)
		*oc_id_p = oc->oc_id;

	return oc ? 0 : -DER_NONEXIST;
}

int
dc_set_oclass(uint64_t rf_factor, int domain_nr, int target_nr,
	      daos_ofeat_t ofeats, daos_oclass_hints_t hints,
	      daos_oclass_id_t *oc_id_p)
{
	daos_oclass_id_t	cid = 0;
	struct daos_obj_class	*oc;
	struct daos_oclass_attr	ca;
	uint16_t		shd, rdd;
	int			grp_size;

	rdd = hints & DAOS_OCH_RDD_MASK;
	shd = hints & DAOS_OCH_SHD_MASK;

	/** first set a reasonable default based on RF & RDD hint (if set) */
	switch (rf_factor) {
	case DAOS_PROP_CO_REDUN_RF0:
		if (rdd == DAOS_OCH_RDD_RP) {
			cid = OC_RP_2GX;
		} else if (rdd == DAOS_OCH_RDD_EC) {
			if (domain_nr >= 10)
				cid = OC_EC_8P1GX;
			else if (domain_nr >= 6)
				cid = OC_EC_4P1GX;
			else
				cid = OC_EC_2P1GX;
		} else {
			cid = OC_SX;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF1:
		if (rdd == DAOS_OCH_RDD_EC || ofeats & DAOS_OF_ARRAY ||
		    ofeats & DAOS_OF_ARRAY_BYTE) {
			if (domain_nr >= 10)
				cid = OC_EC_8P1GX;
			else if (domain_nr >= 6)
				cid = OC_EC_4P1GX;
			else
				cid = OC_EC_2P1GX;
		} else {
			cid = OC_RP_2GX;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF2:
		if (rdd == DAOS_OCH_RDD_EC || ofeats & DAOS_OF_ARRAY ||
		    ofeats & DAOS_OF_ARRAY_BYTE) {
			if (domain_nr >= 10)
				cid = OC_EC_8P2GX;
			else if (domain_nr >= 6)
				cid = OC_EC_4P2GX;
			else
				cid = OC_EC_2P2GX;
		} else {
			cid = OC_RP_3GX;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF3:
		/** EC not supported here */
		cid = OC_RP_4GX;
		break;
	case DAOS_PROP_CO_REDUN_RF4:
		/** EC not supported here */
		cid = OC_RP_6GX;
		break;
	}

	/** we have determined the resilience part, now set the grp size */

	oc = oclass_ident2cl(cid);
	if (!oc)
		return -DER_INVAL;

	memcpy(&ca, &oc->oc_attr, sizeof(ca));
	grp_size = daos_oclass_grp_size(&ca);

	/** adjust the group size based on the sharding hint */
	switch (shd) {
	case 0:
	case DAOS_OCH_SHD_DEF:
		if (ofeats & DAOS_OF_ARRAY || ofeats & DAOS_OF_ARRAY_BYTE ||
		    ofeats & DAOS_OF_KV_FLAT)
			ca.ca_grp_nr = DAOS_OBJ_GRP_MAX;
		else
			ca.ca_grp_nr = 1;
		break;
	case DAOS_OCH_SHD_MAX:
		ca.ca_grp_nr = DAOS_OBJ_GRP_MAX;
		break;
	case DAOS_OCH_SHD_TINY:
		ca.ca_grp_nr = 4;
		break;
	case DAOS_OCH_SHD_REG:
		ca.ca_grp_nr = max(128, target_nr * 25 / 100);
		break;
	case DAOS_OCH_SHD_HI:
		ca.ca_grp_nr = max(256, target_nr * 50 / 100);
		break;
	case DAOS_OCH_SHD_EXT:
		ca.ca_grp_nr = max(1024, target_nr * 80 / 100);
		break;
	default:
		D_ERROR("Invalid sharding hint\n");
		return -DER_INVAL;
	}

	if (ca.ca_grp_nr == DAOS_OBJ_GRP_MAX ||
	    ca.ca_grp_nr * grp_size > target_nr) {
		/* search for the highest scalability in the allowed range */
		ca.ca_grp_nr = max(1, (target_nr / grp_size));
	}
	oc = oclass_scale2cl(&ca);
	if (oc)
		*oc_id_p = oc->oc_id;

	return oc ? 0 : -DER_NONEXIST;
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
/* for binary search */
static struct daos_oc_ec_codec **ecc_array;

static void
ecc_sop_swap(void *array, int a, int b)
{
	struct daos_oc_ec_codec **ecc = (struct daos_oc_ec_codec **)array;
	struct daos_oc_ec_codec  *tmp;

	tmp = ecc[a];
	ecc[a] = ecc[b];
	ecc[b] = tmp;
}

static int
ecc_sop_cmp(void *array, int a, int b)
{
	struct daos_oc_ec_codec **ecc = (struct daos_oc_ec_codec **)array;

	if (ecc[a]->ec_oc_id > ecc[b]->ec_oc_id)
		return 1;
	if (ecc[a]->ec_oc_id < ecc[b]->ec_oc_id)
		return -1;
	return 0;
}

static int
ecc_sop_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_oc_ec_codec **ecc = (struct daos_oc_ec_codec **)array;
	unsigned int		  id  = (unsigned int)key;

	if (ecc[i]->ec_oc_id > id)
		return 1;
	if (ecc[i]->ec_oc_id < id)
		return -1;
	return 0;
}

static daos_sort_ops_t	ecc_sort_ops = {
	.so_swap	= ecc_sop_swap,
	.so_cmp		= ecc_sop_cmp,
	.so_cmp_key	= ecc_sop_cmp_key,
};

void
obj_ec_codec_fini(void)
{
	struct obj_ec_codec	*ec_codec;
	struct daos_obj_class	*oc;
	int			 ocnr = 0;
	int			 i;

	if (ecc_array) {
		D_FREE(ecc_array);
		ecc_array = NULL;
	}

	if (oc_ec_codecs == NULL)
		return;

	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (daos_oclass_is_ec(&oc->oc_attr))
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
		if (daos_oclass_is_ec(&oc->oc_attr))
			ocnr++;
	}
	if (ocnr == 0)
		return 0;

	oc_ec_codec_nr = ocnr;
	DM_ALLOC_ARRAY(M_EC, oc_ec_codecs, ocnr);
	if (oc_ec_codecs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	DM_ALLOC_ARRAY(M_EC, ecc_array, ocnr);
	if (ecc_array == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	for (i = 0, oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (!daos_oclass_is_ec(&oc->oc_attr))
			continue;

		oc_ec_codecs[i].ec_oc_id = oc->oc_id;
		ec_codec = &oc_ec_codecs[i].ec_codec;
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
		DM_ALLOC(M_EC, ec_codec->ec_gftbls, k * p * 32);
		if (ec_codec->ec_gftbls == NULL)
			D_GOTO(failed, rc = -DER_NOMEM);
		DM_ALLOC(M_EC, encode_matrix, m * k);
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

		ecc_array[i] = &oc_ec_codecs[i];
		i++;
	}
	D_ASSERT(i == ocnr);

	rc = daos_array_sort(ecc_array, oc_ec_codec_nr, true, &ecc_sort_ops);
	D_ASSERT(rc == 0);
	return 0;

failed:
	obj_ec_codec_fini();
	return rc;
}

struct obj_ec_codec *
obj_ec_codec_get(daos_oclass_id_t oc_id)
{
	int	idx;

	D_ASSERT(ecc_array);
	idx = daos_array_find(ecc_array, oc_ec_codec_nr, oc_id, &ecc_sort_ops);
	if (idx < 0)
		return NULL;

	return &ecc_array[idx]->ec_codec;
}

static void
oc_sop_swap(void *array, int a, int b)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;
	struct daos_obj_class  *tmp;

	tmp = ocs[a];
	ocs[a] = ocs[b];
	ocs[b] = tmp;
}

static int
oc_sop_ident_cmp(void *array, int a, int b)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;

	if (ocs[a]->oc_id > ocs[b]->oc_id)
		return 1;
	if (ocs[a]->oc_id < ocs[b]->oc_id)
		return -1;
	return 0;
}

static int
oc_sop_ident_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;
	unsigned int		id  = (unsigned int)key;

	if (ocs[i]->oc_id > id)
		return 1;
	if (ocs[i]->oc_id < id)
		return -1;
	return 0;
}

static daos_sort_ops_t	oc_ident_sort_ops = {
	.so_swap	= oc_sop_swap,
	.so_cmp		= oc_sop_ident_cmp,
	.so_cmp_key	= oc_sop_ident_cmp_key,
};

static int
oc_resil_cmp(struct daos_oclass_attr *ca1, struct daos_oclass_attr *ca2)
{
	D_ASSERT(ca1->ca_grp_nr == ca2->ca_grp_nr && ca1->ca_grp_nr == 1);
	D_ASSERT(ca1->ca_resil == ca2->ca_resil &&
		 ca1->ca_resil == DAOS_RES_REPL);

	if (ca1->ca_resil_degree > ca2->ca_resil_degree)
		return 1;
	if (ca1->ca_resil_degree < ca2->ca_resil_degree)
		return -1;

	return 0;
}

static int
oc_sop_resil_cmp(void *array, int a, int b)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;

	return oc_resil_cmp(&ocs[a]->oc_attr, &ocs[b]->oc_attr);
}

static int
oc_sop_resil_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_obj_class   **ocs = (struct daos_obj_class **)array;
	struct daos_oclass_attr	 *ca;

	ca = (struct daos_oclass_attr *)(unsigned long)key;
	return oc_resil_cmp(&ocs[i]->oc_attr, ca);
}

static daos_sort_ops_t	oc_resil_sort_ops = {
	.so_swap	= oc_sop_swap,
	.so_cmp		= oc_sop_resil_cmp,
	.so_cmp_key	= oc_sop_resil_cmp_key,
};

static int
oc_scale_cmp(struct daos_oclass_attr *ca1, struct daos_oclass_attr *ca2)
{
	if (ca1->ca_resil > ca2->ca_resil)
		return 1;
	if (ca1->ca_resil < ca2->ca_resil)
		return -1;

	if (ca1->ca_resil == DAOS_RES_EC) {
		if (ca1->ca_ec_cell > ca2->ca_ec_cell)
			return 1;
		if (ca1->ca_ec_cell < ca2->ca_ec_cell)
			return -1;

		if (ca1->ca_ec_k + ca1->ca_ec_p > ca2->ca_ec_k + ca2->ca_ec_p)
			return 1;
		if (ca1->ca_ec_k + ca1->ca_ec_p < ca2->ca_ec_k + ca2->ca_ec_p)
			return -1;
	} else {
		if (ca1->ca_rp_nr > ca2->ca_rp_nr)
			return 1;
		if (ca1->ca_rp_nr < ca2->ca_rp_nr)
			return -1;
	}

	if (ca1->ca_resil_degree > ca2->ca_resil_degree)
		return 1;
	if (ca1->ca_resil_degree < ca2->ca_resil_degree)
		return -1;

	/* all the same, real comparison is here */
	if (ca1->ca_grp_nr > ca2->ca_grp_nr)
		return 1;
	if (ca1->ca_grp_nr < ca2->ca_grp_nr)
		return -1;

	return 0;
}

static int
oc_sop_scale_cmp(void *array, int a, int b)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;

	return oc_scale_cmp(&ocs[a]->oc_attr, &ocs[b]->oc_attr);
}

static int
oc_sop_scale_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_obj_class   **ocs = (struct daos_obj_class **)array;
	struct daos_oclass_attr	 *ca;

	ca = (struct daos_oclass_attr *)(unsigned long)key;
	return oc_scale_cmp(&ocs[i]->oc_attr, ca);
}

static daos_sort_ops_t	oc_scale_sort_ops = {
	.so_swap	= oc_sop_swap,
	.so_cmp		= oc_sop_scale_cmp,
	.so_cmp_key	= oc_sop_scale_cmp_key,
};

/* NB: ignore the last one which is UNKNOWN */
#define OC_NR	daos_oclass_nr(0)

/* find object class by ID */
static struct daos_obj_class *
oclass_ident2cl(daos_oclass_id_t oc_id)
{
	int	idx;

	if (oc_id == OC_UNKNOWN)
		return NULL;

	idx = daos_array_find(oc_ident_array, oc_ident_array_sz, oc_id,
			      &oc_ident_sort_ops);
	if (idx < 0)
		return NULL;

	return oc_ident_array[idx];
}

/* find object class by number of replicas (single group), the returned
 * class should have the same of less number of replicas than ca::ca_rp_nr
 */
static struct daos_obj_class *
oclass_resil2cl(struct daos_oclass_attr *ca)
{
	int	idx;

	idx = daos_array_find_le(oc_resil_array, oc_resil_array_sz,
				 (uint64_t)(unsigned long)ca,
				 &oc_resil_sort_ops);
	if (idx < 0)
		return NULL;

	return oc_resil_array[idx];
}

/* find object class by number of redundancy groups
 * the returned class should have the same protection method (EC/replication),
 * the same group size, the same redundancy level, equal to or less than
 * redundancy groups than ca::ca_grp_nr.
 */
static struct daos_obj_class *
oclass_scale2cl(struct daos_oclass_attr *ca)
{
	struct daos_obj_class *oc;
	int		       idx;

	idx = daos_array_find_le(oc_scale_array, oc_scale_array_sz,
				 (uint64_t)(unsigned long)ca,
				 &oc_scale_sort_ops);
	if (idx < 0)
		return NULL;

	oc = oc_scale_array[idx];
	if (ca->ca_resil != oc->oc_resil ||
	    ca->ca_resil_degree != oc->oc_resil_degree ||
	    daos_oclass_grp_size(ca) != daos_oclass_grp_size(&oc->oc_attr))
		return NULL;

	return oc;
}

static char *
oclass_resil_str(struct daos_obj_class *oc)
{
	return oc->oc_resil == DAOS_RES_REPL ? "RP" : "EC";
}

static inline void
oclass_debug(struct daos_obj_class *oc)
{
	D_DEBUG(DB_PL,
		"ID: %d, name: %s, resil: %s, resil_degree: %d, "
		"grp_size: %d, grp_nr: %d\n",
		oc->oc_id, oc->oc_name, oclass_resil_str(oc),
		oc->oc_resil_degree, daos_oclass_grp_size(&oc->oc_attr),
		oc->oc_grp_nr);
}

static void
oclass_array_debug(char *array_name, struct daos_obj_class **oc_array,
		   int array_size)
{
	int	i;

	D_DEBUG(DB_PL, "Object class %s array[%d]:\n", array_name, array_size);
	for (i = 0; i < array_size; i++)
		oclass_debug(oc_array[i]);
}

int
obj_class_init(void)
{
	int	i;
	int	rc;

	if (oc_ident_array)
		return 0;

	DM_ALLOC_ARRAY(M_OBJ, oc_ident_array, OC_NR);
	if (!oc_ident_array)
		return -DER_NOMEM;

	DM_ALLOC_ARRAY(M_OBJ, oc_scale_array, OC_NR);
	if (!oc_scale_array) {
		rc = -DER_NOMEM;
		goto failed;
	}

	DM_ALLOC_ARRAY(M_OBJ, oc_resil_array, OC_NR);
	if (!oc_resil_array) {
		rc = -DER_NOMEM;
		goto failed;
	}

	for (i = 0; i < OC_NR; i++) {
		struct daos_obj_class *oc = &daos_obj_classes[i];

		if (oc->oc_resil == DAOS_RES_REPL) {
			D_ASSERT(oc->oc_rp_nr >= 1);
			if (oc->oc_rp_nr == DAOS_OBJ_REPL_MAX)
				oc->oc_resil_degree = DAOS_OBJ_RESIL_MAX;
			else
				oc->oc_resil_degree = oc->oc_rp_nr - 1;

			if (!oc->oc_private && /* ignore private classes */
			    oc->oc_grp_nr == 1)
				oc_resil_array[oc_resil_array_sz++] = oc;
		} else {
			D_ASSERT(oc->oc_resil == DAOS_RES_EC);
			oc->oc_resil_degree = oc->oc_ec_p;
		}
		oc_ident_array[oc_ident_array_sz++] = oc;

		if (!oc->oc_private) /* ignore private classes */
			oc_scale_array[oc_scale_array_sz++] = oc;
	}

	rc = daos_array_sort(oc_ident_array, oc_ident_array_sz, true,
			     &oc_ident_sort_ops);
	if (rc) {
		D_ERROR("object class ID should be unique\n");
		goto failed;
	}
	oclass_array_debug("ident", oc_ident_array, oc_ident_array_sz);

	rc = daos_array_sort(oc_scale_array, oc_scale_array_sz, true,
			     &oc_scale_sort_ops);
	if (rc) {
		D_ERROR("object class scale attribute should be unique\n");
		goto failed;
	}
	oclass_array_debug("scale", oc_scale_array, oc_scale_array_sz);

	rc = daos_array_sort(oc_resil_array, oc_resil_array_sz, true,
			     &oc_resil_sort_ops);
	if (rc) {
		D_ERROR("object class resilience attribute should be unique\n");
		goto failed;
	}
	oclass_array_debug("resilience", oc_resil_array, oc_resil_array_sz);
	return 0;
failed:
	obj_class_fini();
	return rc;
}

void
obj_class_fini(void)
{
	if (oc_resil_array) {
		D_FREE(oc_resil_array);
		oc_resil_array = NULL;
		oc_resil_array_sz = 0;
	}

	if (oc_scale_array) {
		D_FREE(oc_scale_array);
		oc_scale_array = NULL;
		oc_scale_array_sz = 0;
	}

	if (oc_ident_array) {
		D_FREE(oc_ident_array);
		oc_ident_array = NULL;
		oc_ident_array_sz = 0;
	}
}
