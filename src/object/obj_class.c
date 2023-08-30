/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(object)

#include "obj_class.h"
#include "obj_internal.h"
#include <isa-l.h>

/** indirect indices for binary search by ID */
static struct daos_obj_class **oc_ident_array;
/** indirect indices for binary search by number of replicas */
static struct daos_obj_class **oc_resil_array;

static int oc_ident_array_sz;
static int oc_resil_array_sz;

static struct daos_obj_class  *oclass_ident2cl(daos_oclass_id_t oc_id,
					       uint32_t *nr_grps);
static struct daos_obj_class  *oclass_resil2cl(struct daos_oclass_attr *ca);

int
daos_oclass_cid2allowedfailures(daos_oclass_id_t oc_id, uint32_t *tf)
{
	struct daos_obj_class *oc;

	oc = oclass_ident2cl(oc_id, NULL);
	if (oc == NULL)
		return -DER_INVAL;
	*tf = oc->oc_resil_degree;
	return 0;
}

/**
 * Find the object class attributes for the provided @oid.
 */
struct daos_oclass_attr *
daos_oclass_attr_find(daos_obj_id_t oid, uint32_t *nr_grps)
{
	struct daos_obj_class	*oc;

	/* see daos_objid_generate */
	oc = oclass_ident2cl(daos_obj_id2class(oid), nr_grps);
	if (!oc) {
		D_DEBUG(DB_PL, "Unknown object class %u for "DF_OID"\n",
			(unsigned int)daos_obj_id2class(oid), DP_OID(oid));
		return NULL;
	}
	D_DEBUG(DB_PL, "Find class %s for oid "DF_OID"\n",
		oc->oc_name, DP_OID(oid));

	return &oc->oc_attr;
}

int daos_obj2oc_attr(daos_handle_t oh, struct daos_oclass_attr *oca)
{
	struct dc_object *dc_object;
	struct daos_oclass_attr *tmp;

	dc_object = obj_hdl2ptr(oh);
	if (dc_object == NULL)
		return -DER_NO_HDL;

	tmp = obj_get_oca(dc_object);
	D_ASSERT(tmp != NULL);
	*oca = *tmp;
	obj_decref(dc_object);

	return 0;
}

int
daos_obj_set_oid_by_class(daos_obj_id_t *oid, enum daos_otype_t type,
			  daos_oclass_id_t cid, uint32_t args)
{
	struct daos_obj_class	*oc;
	uint32_t nr_grps;

	oc = oclass_ident2cl(cid, &nr_grps);
	if (!oc) {
		D_DEBUG(DB_PL, "Unknown object class %u\n", (unsigned int)cid);
		return -DER_INVAL;
	}

	if (cid < (OR_RP_1 << OC_REDUN_SHIFT))
		daos_obj_set_oid(oid, type, 0, cid, args);
	else
		daos_obj_set_oid(oid, type, oc->oc_redun, nr_grps, args);

	return 0;
}

int
daos_oclass_id2name(daos_oclass_id_t oc_id, char *str)
{
	struct daos_obj_class   *oc;
	uint32_t nr_grps;

	oc = oclass_ident2cl(oc_id, &nr_grps);
	if (!oc) {
		strcpy(str, "UNKNOWN");
		return -1;
	}
	if (nr_grps == oc->oc_grp_nr) {
		strcpy(str, oc->oc_name);
	} else {
		/** update oclass name with group number */
		char *p = oc->oc_name;
		int i = 0;

		if (p[i] == 'S') {
			str[0] = 'S';
			i = snprintf(&str[1], MAX_OBJ_CLASS_NAME_LEN - 1, "%u", nr_grps);
			if (i < 0) {
				D_ERROR("Failed to encode object class name\n");
				strcpy(str, "UNKNOWN");
				return -1;
			}
			return 0;
		}

		while (p[i] != 'G') {
			str[i] = p[i];
			i++;
		}
		str[i++] = 'G';
		p = &str[i];
		str[i++] = 0;
		i = snprintf(p, MAX_OBJ_CLASS_NAME_LEN - strlen(str), "%u", nr_grps);
		if (i < 0) {
			D_ERROR("Failed to encode object class name\n");
			strcpy(str, "UNKNOWN");
			return -1;
		}
	}
	return 0;
}

int
daos_oclass_name2id(const char *name)
{
	struct daos_obj_class	*oc;

	if (strnlen(name, MAX_OBJ_CLASS_NAME_LEN + 1) > MAX_OBJ_CLASS_NAME_LEN)
		return OC_UNKNOWN;

	/* slow search path, it's for tool and not performance sensitive. */
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		if (strlen(oc->oc_name) == strnlen(name, MAX_OBJ_CLASS_NAME_LEN) &&
		    strcmp(oc->oc_name, name) == 0)
			return oc->oc_id;
	}

	D_ASSERT(oc->oc_id == OC_UNKNOWN);
	return OC_UNKNOWN;
}

/** Return the list of registered oclass names */
ssize_t
daos_oclass_names_list(size_t size, char *str)
{
	struct daos_obj_class   *oc;
	ssize_t len = 0;

	if (size <= 0 || str == NULL)
		return -DER_INVAL;

	*str = '\0';
	for (oc = &daos_obj_classes[0]; oc->oc_id != OC_UNKNOWN; oc++) {
		len += strlen(oc->oc_name) + 2;
		if (len < size) {
			strcat(str, oc->oc_name);
			strcat(str, ", ");
		} else {
			return -DER_OVERFLOW;
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
	return (oclass_ident2cl(oc_id, NULL) != NULL) ? true : false;
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

/**
 * To honor RF setting during failure cases, let's reserve RF
 * groups, so if some targets fail, there will be enough replacement
 * targets to rebuild, so to avoid putting multiple shards in the same
 * domain, which may break the RF setting.
 *
 * Though let's keep reserve targets to be less than 30% of the total
 * targets.
 */
static uint32_t
reserve_grp_by_rf(uint32_t target_nr, uint32_t grp_size, uint32_t rf)
{
	return min(((target_nr * 3) / 10) / grp_size, rf);
}

int
daos_oclass_fit_max(daos_oclass_id_t oc_id, int domain_nr, int target_nr, enum daos_obj_redun *ord,
		    uint32_t *nr, uint32_t rf_factor)
{
	struct daos_obj_class	*oc;
	struct daos_oclass_attr	 ca;
	uint32_t grp_size;
	uint32_t nr_grps;
	int rc;

	D_ASSERT(target_nr > 0);
	D_ASSERT(domain_nr > 0);

	oc = oclass_ident2cl(oc_id, &nr_grps);
	if (!oc) {
		rc = -DER_INVAL;
		D_ERROR("oclass_ident2cl(oc_id %d), failed "DF_RC"\n", oc_id, DP_RC(rc));
		return rc;
	}

	memcpy(&ca, &oc->oc_attr, sizeof(ca));
	ca.ca_grp_nr = nr_grps;
	if (oc_id == OC_RP_XSF) {
		D_ASSERT(ca.ca_resil_degree == DAOS_OBJ_RESIL_MAX);
		D_ASSERT(ca.ca_rp_nr == DAOS_OBJ_REPL_MAX);

		/* search for the highest possible resilience level */
		ca.ca_resil_degree = domain_nr - 1;
		ca.ca_rp_nr = domain_nr;
		oc = oclass_resil2cl(&ca);
		*ord = oc->oc_redun;
		*nr = oc->oc_attr.ca_grp_nr;
		goto out;
	}

	grp_size = daos_oclass_grp_size(&ca);
	if (ca.ca_grp_nr == DAOS_OBJ_GRP_MAX) {
		uint32_t reserve_grp = reserve_grp_by_rf(target_nr, grp_size, rf_factor);

		ca.ca_grp_nr = max(1, (target_nr / grp_size));

		if (ca.ca_grp_nr > reserve_grp)
			ca.ca_grp_nr -= reserve_grp;
	}
	if (grp_size > domain_nr) {
		D_ERROR("grp size (%u) (%u) is larger than domain nr (%u)\n",
			grp_size, DAOS_OBJ_REPL_MAX, domain_nr);
		return -DER_INVAL;
	}

	if (ca.ca_grp_nr * grp_size > target_nr) {
		D_ERROR("grp_size (%u) x grp_nr (%u) is larger than targets (%u)\n",
			grp_size, ca.ca_grp_nr, target_nr);
		return -DER_INVAL;
	}

	*ord = oc->oc_redun;
	*nr = ca.ca_grp_nr;
out:
	/* ord = 0 means object class less than OC_S1 */
	if (oc_id < (OR_RP_1 << OC_REDUN_SHIFT)) {
		*ord = 0;
		*nr = oc_id;
	}

	D_DEBUG(DB_PL, "found object class, group_nr: %d, redun: %d\n",
		*nr, *ord);
	return 0;
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

static inline enum daos_obj_redun
daos_oclass_id2redun(daos_oclass_id_t oc_id)
{
	return (oc_id >> OC_REDUN_SHIFT);
}

static int
ecc_sop_redun_cmp(void *array, int a, int b)
{
	struct daos_oc_ec_codec **ecc = (struct daos_oc_ec_codec **)array;

	if (daos_oclass_id2redun(ecc[a]->ec_oc_id) >
	    daos_oclass_id2redun(ecc[b]->ec_oc_id))
		return 1;
	if (daos_oclass_id2redun(ecc[a]->ec_oc_id) <
	    daos_oclass_id2redun(ecc[b]->ec_oc_id))
		return -1;
	return 0;
}

static int
ecc_sop_redun_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_oc_ec_codec **ecc = (struct daos_oc_ec_codec **)array;
	daos_oclass_id_t id = (daos_oclass_id_t)key;

	if (daos_oclass_id2redun(ecc[i]->ec_oc_id) >
	    daos_oclass_id2redun(id))
		return 1;
	if (daos_oclass_id2redun(ecc[i]->ec_oc_id) <
	    daos_oclass_id2redun(id))
		return -1;
	return 0;
}

static daos_sort_ops_t	ecc_redun_sort_ops = {
	.so_swap	= ecc_sop_swap,
	.so_cmp		= ecc_sop_redun_cmp,
	.so_cmp_key	= ecc_sop_redun_cmp_key,
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
	D_ALLOC_ARRAY(oc_ec_codecs, ocnr);
	if (oc_ec_codecs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(ecc_array, ocnr);
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
	if (oc_id < (OR_RP_1 << OC_REDUN_SHIFT))
		idx = daos_array_find(ecc_array, oc_ec_codec_nr,
				      oc_id, &ecc_sort_ops);
	else
		idx = daos_array_find(ecc_array, oc_ec_codec_nr,
				      oc_id, &ecc_redun_sort_ops);
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
oc_sop_redun_cmp(void *array, int a, int b)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;

	if (ocs[a]->oc_redun > ocs[b]->oc_redun)
		return 1;
	if (ocs[a]->oc_redun < ocs[b]->oc_redun)
		return -1;
	return 0;
}

static int
oc_sop_redun_cmp_key(void *array, int i, uint64_t key)
{
	struct daos_obj_class **ocs = (struct daos_obj_class **)array;
	daos_oclass_id_t id  = (daos_oclass_id_t)key;

	if (daos_oclass_id2redun(ocs[i]->oc_id) > daos_oclass_id2redun(id))
		return 1;
	if (daos_oclass_id2redun(ocs[i]->oc_id) < daos_oclass_id2redun(id))
		return -1;
	return 0;
}

static daos_sort_ops_t	oc_redun_sort_ops = {
	.so_swap	= oc_sop_swap,
	.so_cmp		= oc_sop_redun_cmp,
	.so_cmp_key	= oc_sop_redun_cmp_key,
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

/* NB: ignore the last one which is UNKNOWN */
#define OC_NR	daos_oclass_nr(0)

/*
 * find object class by ID.
 *
 * firstly try to search predefined object class by unique ID, if
 * not try to match oc_redun if ID is not less than OC_S1, number
 * of groups will be parsed from oc_id rather than array for this case.
 */
static struct daos_obj_class *
oclass_ident2cl(daos_oclass_id_t oc_id, uint32_t *nr_grps)
{
	int	idx;

	if (oc_id == OC_UNKNOWN)
		return NULL;

	idx = daos_array_find(oc_ident_array, oc_ident_array_sz, oc_id,
			      &oc_ident_sort_ops);
	if (idx >= 0) {
		if (nr_grps)
			*nr_grps = oc_ident_array[idx]->oc_grp_nr;

		return oc_ident_array[idx];
	}

	if (oc_id < (OR_RP_1 << OC_REDUN_SHIFT))
		return NULL;

	idx = daos_array_find(oc_ident_array, oc_ident_array_sz, oc_id,
			      &oc_redun_sort_ops);
	if (idx < 0)
		return NULL;

	if (nr_grps) {
		if ((oc_ident_array[idx]->oc_redun << OC_REDUN_SHIFT) >= oc_id)
			return NULL;
		*nr_grps = oc_id -
			(oc_ident_array[idx]->oc_redun << OC_REDUN_SHIFT);
	}

	return oc_ident_array[idx];
}

int
dc_set_oclass(uint32_t rf, int domain_nr, int target_nr, enum daos_otype_t otype,
	      daos_oclass_hints_t hints, enum daos_obj_redun *ord, uint32_t *nr)
{
	uint16_t shd;
	uint16_t rdd;
	uint32_t grp_size;
	uint32_t grp_nr;
	int      rc;

	rdd = hints & DAOS_OCH_RDD_MASK;
	shd = hints & DAOS_OCH_SHD_MASK;

	/** first set a reasonable default based on RF & RDD hint (if set) */
	switch (rf) {
	default:
	case DAOS_PROP_CO_REDUN_RF0:
		if (rdd == DAOS_OCH_RDD_RP && domain_nr >= 2) {
			*ord =  OR_RP_2;
			grp_size = 2;
		} else if (rdd == DAOS_OCH_RDD_EC) {
			if (domain_nr >= 18) {
				*ord = OR_RS_16P1;
				grp_size = 17;
			} else if (domain_nr >= 10) {
				*ord = OR_RS_8P1;
				grp_size = 9;
			} else if (domain_nr >= 6) {
				*ord = OR_RS_4P1;
				grp_size = 5;
			} else {
				*ord = OR_RS_2P1;
				grp_size = 3;
			}
		} else {
			*ord = OR_RP_1;
			grp_size = 1;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF1:
		if ((rdd == DAOS_OCH_RDD_EC || (rdd == 0 && daos_is_array_type(otype))) &&
		    domain_nr >= 3) {
			if (domain_nr >= 18) {
				*ord = OR_RS_16P1;
				grp_size = 17;
			} else if (domain_nr >= 10) {
				*ord = OR_RS_8P1;
				grp_size = 9;
			} else if (domain_nr >= 6) {
				*ord = OR_RS_4P1;
				grp_size = 5;
			} else {
				*ord = OR_RS_2P1;
				grp_size = 3;
			}
		} else {
			*ord = OR_RP_2;
			grp_size = 2;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF2:
		if ((rdd == DAOS_OCH_RDD_EC || (rdd == 0 && daos_is_array_type(otype))) &&
		    domain_nr >= 4) {
			if (domain_nr >= 20) {
				*ord = OR_RS_16P2;
				grp_size = 18;
			} else if (domain_nr >= 12) {
				*ord = OR_RS_8P2;
				grp_size = 10;
			} else if (domain_nr >= 8) {
				*ord = OR_RS_4P2;
				grp_size = 6;
			} else {
				*ord = OR_RS_2P2;
				grp_size = 4;
			}
		} else {
			*ord = OR_RP_3;
			grp_size = 3;
		}
		break;
	case DAOS_PROP_CO_REDUN_RF3:
		/** EC not supported here */
		*ord = OR_RP_4;
		grp_size = 4;
		break;
	case DAOS_PROP_CO_REDUN_RF4:
		/** EC not supported here */
		*ord = OR_RP_5;
		grp_size = 5;
		break;
	}

	if (unlikely(grp_size > domain_nr)) {
		/** cannot meet redundancy requirement */
		rc = -DER_INVAL;
		D_ERROR("grp_size %d > domain_nr %d, "DF_RC"\n", grp_size, domain_nr, DP_RC(rc));
		return rc;
	}

	/** adjust the group size based on the sharding hint */
	switch (shd) {
	case 0:
	case DAOS_OCH_SHD_DEF:
		if (daos_is_array_type(otype) || daos_is_kv_type(otype))
			grp_nr = DAOS_OBJ_GRP_MAX;
		else
			grp_nr = 1;
		break;
	case DAOS_OCH_SHD_MAX:
		grp_nr = DAOS_OBJ_GRP_MAX;
		break;
	case DAOS_OCH_SHD_TINY:
		grp_nr = 1;
		break;
	case DAOS_OCH_SHD_REG:
		grp_nr = max(128, target_nr * 25 / 100);
		break;
	case DAOS_OCH_SHD_HI:
		grp_nr = max(256, target_nr * 50 / 100);
		break;
	case DAOS_OCH_SHD_EXT:
		grp_nr = max(1024, target_nr * 80 / 100);
		break;
	default:
		D_ERROR("Invalid sharding hint\n");
		return -DER_INVAL;
	}

	if (grp_nr == DAOS_OBJ_GRP_MAX || grp_nr * grp_size > target_nr) {
		uint32_t max_grp     = target_nr / grp_size;
		uint32_t reserve_grp = reserve_grp_by_rf(target_nr, grp_size, rf);

		/* search for the highest scalability in the allowed range */
		if (max_grp > reserve_grp)
			max_grp = max_grp - reserve_grp;
		*nr = max(1, max_grp);
	} else {
		*nr = grp_nr;
	}

	return 0;
}


/**
 * XXX: Function below should not look into the pre-registered list of
 * class and now just generate it on the fly based on daos_obj_redun
 * and customer nr_grp now encoded in the objid.
 */

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

static char *
oclass_resil_str(struct daos_obj_class *oc)
{
	return oc->oc_resil == DAOS_RES_REPL ? "RP" : "EC";
}

static inline void
oclass_debug(struct daos_obj_class *oc)
{
	D_DEBUG(DB_PL,
		"ID: %u, name: %s, resil: %s, resil_degree: %d, "
		"grp_size: %d, grp_nr: %d\n",
		(unsigned int)oc->oc_id, oc->oc_name, oclass_resil_str(oc),
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

	D_ALLOC_ARRAY(oc_ident_array, OC_NR);
	if (!oc_ident_array)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(oc_resil_array, OC_NR);
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
	}

	rc = daos_array_sort(oc_ident_array, oc_ident_array_sz, true,
			     &oc_ident_sort_ops);
	if (rc) {
		D_ERROR("object class ID should be unique\n");
		goto failed;
	}
	oclass_array_debug("ident", oc_ident_array, oc_ident_array_sz);

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

	if (oc_ident_array) {
		D_FREE(oc_ident_array);
		oc_ident_array = NULL;
		oc_ident_array_sz = 0;
	}
}
