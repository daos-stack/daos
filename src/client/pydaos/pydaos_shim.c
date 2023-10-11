/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* Those are gone from python3, replaced with new functions */
#define PyInt_FromLong		PyLong_FromLong
#define PyString_FromString	PyUnicode_FromString
#define PyString_FromStringAndSize PyUnicode_FromStringAndSize
#define PyString_AsString	PyBytes_AsString

#include <Python.h>

#include <daos_errno.h>
#include <gurt/debug.h>
#include <gurt/list.h>

#include <daos_types.h>
#include <daos.h>
#include <daos_prop.h>
#include <daos_obj_class.h>
#include <gurt/common.h>
#include <daos_kv.h>
#include <daos_uns.h>

#define PY_SHIM_MAGIC_NUMBER 0x7A8A
#define MAX_OID_HI ((1UL << 32) - 1)

/** Durable format of entries in the root kv */
struct pydaos_df {
	daos_obj_id_t	oid;
	uint32_t	otype;
	uint32_t	res1;
	uint64_t	res2[5];
};

/** Object type, stored in pydaos_df::otype */
enum pydaos_otype {
	PYDAOS_DICT,
	PYDAOS_ARRAY,
};

/** in-memory tracking of handles */
struct open_handle {
	daos_handle_t	poh;   /** pool handle */
	daos_handle_t	coh;   /** container handle */
	daos_handle_t	oh;    /** root object handle */
	daos_obj_id_t	alloc; /** last allocated objid */
};

static int
__is_magic_valid(int input)
{
	if (input != PY_SHIM_MAGIC_NUMBER) {
		D_ERROR("MAGIC number does not match, expected %d got %d\n",
			PY_SHIM_MAGIC_NUMBER, input);
		return 0;
	}

	return 1;
}

/* Macro that parses out magic value and verifies it */
#define RETURN_NULL_IF_BAD_MAGIC(args)					\
do {									\
	int magic;							\
	if (!PyArg_ParseTuple(args, "i", &magic)) {			\
		DEBUG_PRINT("Bad arguments passed to %s", __func__);	\
		return NULL;						\
	}								\
									\
	if (!__is_magic_valid(magic)) {					\
		return NULL;						\
	}								\
} while (0)


/* Parse arguments and magic number out*/
#define RETURN_NULL_IF_FAILED_TO_PARSE(args, format, x...)		\
do {									\
	int magic;							\
	if (!PyArg_ParseTuple(args, "i"format, &magic, x)) {		\
		D_DEBUG(DB_ANY, "Bad args passed to %s", __func__);	\
		return NULL;						\
	}								\
									\
	if (!__is_magic_valid(magic)) {					\
		return NULL;						\
	}								\
} while (0)

static daos_handle_t	glob_eq;
static int		use_glob_eq;

/**
 * Implementations of baseline shim functions
 */

static PyObject *
__shim_handle__daos_init(PyObject *self, PyObject *args)
{
	int rc;
	int ret;
	char *override;

	rc = daos_init();
	if ((rc == 0) && (use_glob_eq == 0)) {
		override = getenv("PYDAOS_GLOB_EQ");
		if ((override == NULL) || strcmp(override, "0")) {
			use_glob_eq = 1;
			ret = daos_eq_create(&glob_eq);
			if (ret) {
				D_ERROR("Failed to create global eq, "DF_RC"\n", DP_RC(ret));
				use_glob_eq = 0;
			}
		}
	}

	return PyInt_FromLong(rc);
}

static PyObject *
__shim_handle__daos_fini(PyObject *self, PyObject *args)
{
	int rc;

	if (use_glob_eq) {
		rc =  daos_eq_destroy(glob_eq, DAOS_EQ_DESTROY_FORCE);
		if (rc)
			D_ERROR("Failed to destroy global eq, "DF_RC"\n", DP_RC(rc));
		use_glob_eq = 0;
	}

	rc = daos_fini();

	return PyInt_FromLong(rc);
}

static PyObject *
__shim_handle__err_to_str(PyObject *self, PyObject *args)
{
	const char	*str;
	int		 val;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "i", &val);
	/* Call C function */
	str = d_errstr(val);
	if (str == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	return PyString_FromString(str);
}

/**
 * Implementation of container functions
 */

static PyObject *
cont_open(int ret, char *pool, char *cont, int flags)
{
	PyObject			*return_list;
	struct open_handle		*hdl = NULL;
	daos_handle_t			coh = {0};
	daos_handle_t			poh = {0};
	daos_handle_t			oh = {0};
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	int				rc;

	if (ret != DER_SUCCESS) {
		rc = ret;
		goto out;
	}

	/** Connect to pool */
	rc = daos_pool_connect(pool, NULL, DAOS_PC_RW, &poh, NULL, NULL);
	if (rc)
		goto out;

	/** Open container */
	rc = daos_cont_open(poh, cont, DAOS_COO_RW, &coh, NULL, NULL);
	if (rc)
		goto out;

	/** Retrieve container properties via cont_query() */
	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc)
		goto out;

	/** Verify that this is a python container */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_PYTHON) {
		rc = -DER_INVAL;
		D_ERROR("Container is not a python container: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/** Fetch root object ID */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	if (entry == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Invalid entry in properties for root object ID: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (roots->cr_oids[0].hi == 0 && roots->cr_oids[0].lo == 0) {
		rc = -DER_INVAL;
		D_ERROR("Invalid root object ID in properties: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/** Use KV type for root kv */
	roots->cr_oids[0].hi |= (uint64_t)DAOS_OT_KV_HASHED << OID_FMT_TYPE_SHIFT;

	/** Open root object */
	rc = daos_kv_open(coh, roots->cr_oids[0], DAOS_OO_RW, &oh, NULL);
	if (rc)
		goto out;

	/** Track all handles */
	D_ALLOC_PTR(hdl);
	if (hdl == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	hdl->poh	= poh;
	hdl->coh	= coh;
	hdl->oh		= oh;
	hdl->alloc.lo	= 0;
	hdl->alloc.hi	= MAX_OID_HI;
out:
	if (prop)
		daos_prop_free(prop);
	if (rc) {
		int	rc2;

		if (daos_handle_is_valid(oh)) {
			rc2 = daos_kv_close(oh, NULL);
			if (rc2)
				D_ERROR("daos_kv_close() Failed "DF_RC"\n", DP_RC(rc2));
		}
		if (daos_handle_is_valid(coh)) {
			rc2 = daos_cont_close(coh, NULL);
			if (rc2)
				D_ERROR("daos_cont_close() Failed "DF_RC"\n", DP_RC(rc2));
		}
		if (daos_handle_is_valid(poh)) {
			rc2 = daos_pool_disconnect(poh, NULL);
			if (rc2)
				D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc2));
		}
	}

	/* Populate return list */
	return_list = PyList_New(2);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyLong_FromVoidPtr(hdl));

	return return_list;
}

static PyObject *
__shim_handle__cont_open(PyObject *self, PyObject *args)
{
	char	*pool;
	char	*cont;
	int	 flags;

	/** Parse arguments, flags not used for now */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "ssi", &pool, &cont, &flags);

	return cont_open(0, pool, cont, flags);
}

static PyObject *
__shim_handle__cont_open_by_path(PyObject *self, PyObject *args)
{
	const char		*path;
	PyObject		*obj;
	int			 flags;
	struct duns_attr_t	 attr = {0};
	int			 rc;

	/** Parse arguments, flags not used for now */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "si", &path, &flags);

	rc = duns_resolve_path(path, &attr);
	if (rc)
		goto out;

out:
	obj = cont_open(rc, attr.da_pool, attr.da_cont, flags);
	duns_destroy_attr(&attr);
	return obj;
}

static PyObject *
__shim_handle__cont_get(PyObject *self, PyObject *args)
{
	PyObject		*return_list;
	struct open_handle	*hdl;
	char			*name;
	struct pydaos_df	entry;
	size_t			size = sizeof(entry);
	daos_obj_id_t		oid = {0, };
	unsigned int		otype = 0;
	int			rc;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "Ks", &hdl, &name);

	/** Lookup name in root kv */
	rc = daos_kv_get(hdl->oh, DAOS_TX_NONE, 0, name, &size, &entry, NULL);
	if (rc != -DER_SUCCESS)
		goto out;

	/** Check if entry actually exists */
	if (size == 0) {
		rc = -DER_NONEXIST;
		goto out;
	}

	/** If we fetched a value which isn't an entry ... we have a problem */
	if (size != sizeof(entry)) {
		rc = -DER_INVAL;
		goto out;
	}

	oid	= entry.oid;
	otype	= entry.otype;
out:
	/* Populate return list */
	return_list = PyList_New(4);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyLong_FromLong(oid.hi));
	PyList_SetItem(return_list, 2, PyLong_FromLong(oid.lo));
	PyList_SetItem(return_list, 3, PyInt_FromLong(otype));

	return return_list;
}

static PyObject *
__shim_handle__cont_newobj(PyObject *self, PyObject *args)
{
	PyObject		*return_list;
	struct open_handle	*hdl;
	char			*name;
	unsigned int		otype;
	daos_oclass_id_t	cid;
	struct pydaos_df	entry;
	daos_obj_id_t		oid = {0, };
	enum daos_otype_t	type;
	int			rc;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "Ksii", &hdl, &name, &cid, &otype);

	/** Allocate OID for new object */
	if (hdl->alloc.hi >= MAX_OID_HI) {
		rc = daos_cont_alloc_oids(hdl->coh, 1, &hdl->alloc.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
		if (hdl->alloc.lo == 0)
			/** reserve the first 100 object IDs */
			hdl->alloc.hi = 100;
		else
			hdl->alloc.hi = 0;
	}

	/** set oid lo and bump the current hi value */
	oid.lo = hdl->alloc.lo;
	oid.hi = hdl->alloc.hi++;

	/** generate the actual object ID */
	if (otype == PYDAOS_DICT)
		type = DAOS_OT_KV_HASHED;
	else /** PYDAOS_ARRAY */
		type = DAOS_OT_ARRAY;
	rc = daos_obj_generate_oid(hdl->coh, &oid, type, cid, 0, 0);
	if (rc)
		goto out;

	/**
	 * Insert name in root kv, use conditional insert to fail if already
	 * exist
	 */
	entry.oid	= oid;
	entry.otype	= otype;
	rc = daos_kv_put(hdl->oh, DAOS_TX_NONE, DAOS_COND_KEY_INSERT, name,
			 sizeof(entry), &entry, NULL);
	if (rc != -DER_SUCCESS)
		goto out;

out:
	/* Populate return list */
	return_list = PyList_New(3);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyLong_FromLong(oid.hi));
	PyList_SetItem(return_list, 2, PyLong_FromLong(oid.lo));

	return return_list;
}

static PyObject *
__shim_handle__cont_close(PyObject *self, PyObject *args)
{
	struct open_handle	*hdl;
	int			rc = 0;
	int			ret;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "K", &hdl);

	/** Close root object */
	rc = daos_kv_close(hdl->oh, NULL);

	/** Close container */
	ret = daos_cont_close(hdl->coh, NULL);
	if (rc == 0)
		rc = ret;

	/** Disconnect from pool */
	ret = daos_pool_disconnect(hdl->poh, NULL);
	if (rc == 0)
		rc = ret;

	/** if everything went well, free up the handle */
	if (rc == 0)
		D_FREE(hdl);

	return PyInt_FromLong(rc);
}

#define ITER_NR		96

static int
oit_mark(daos_handle_t oh, daos_handle_t oit)
{
	char			*buf = NULL;
	daos_anchor_t		anchor = {0};
	daos_key_desc_t		kds[ITER_NR];
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_size_t		buf_size = ITER_NR * 256;
	struct pydaos_df	entry;
	size_t			size = sizeof(entry);
	d_iov_t			marker;
	bool			mark_data = true;
	int			rc = 0;

	D_ALLOC(buf, buf_size);
	if (buf == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	d_iov_set(&sg_iov, buf, buf_size);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	nr = ITER_NR, i;
		void		*ptr;

		memset(buf, 0, buf_size);
		rc = daos_kv_list(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc)
			goto out;
		if (nr == 0)
			continue;

		for (ptr = buf, i = 0; i < nr; i++) {
			char *key;

			D_ALLOC(key, kds[i].kd_key_len + 1);
			if (key == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}

			memcpy(key, ptr, kds[i].kd_key_len);
			key[kds[i].kd_key_len + 1] = '\0';
			ptr += kds[i].kd_key_len;

			rc = daos_kv_get(oh, DAOS_TX_NONE, DAOS_COND_KEY_GET, key, &size, &entry,
					 NULL);
			D_FREE(key);
			if (rc)
				goto out;

			rc = daos_oit_mark(oit, entry.oid, &marker, NULL);
			if (rc) {
				D_ERROR("daos_oit_mark() failed: "DF_RC"\n", DP_RC(rc));
				goto out;
			}
		}
	}

out:
	D_FREE(buf);
	return rc;
}

static PyObject *
cont_check(int ret, char *pool, char *cont, int flags)
{
	daos_handle_t			coh = {0};
	daos_handle_t			poh = {0};
	daos_handle_t			oh = {0};
	daos_handle_t			oit = {0};
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	daos_epoch_t			snap_epoch = 0;
	daos_anchor_t			anchor = {0};
	uint32_t			nr_entries = ITER_NR, i;
	daos_obj_id_t			oids[ITER_NR] = {0};
	daos_epoch_range_t		epr;
	d_iov_t				marker;
	bool				mark_data = true;
	int				rc, rc2;

	if (ret != DER_SUCCESS) {
		rc = ret;
		goto out;
	}

	/** Connect to pool */
	rc = daos_pool_connect(pool, NULL, DAOS_PC_RW, &poh, NULL, NULL);
	if (rc)
		goto out;

	/** Open container. */
	rc = daos_cont_open(poh, cont, DAOS_COO_EX, &coh, NULL, NULL);
	if (rc)
		goto out;

	/** create snapshot for OIT */
	rc = daos_cont_create_snap_opt(coh, &snap_epoch, NULL, DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc)
		goto out;

	/** Open OIT table */
	rc = daos_oit_open(coh, snap_epoch, &oit, NULL);
	if (rc)
		goto out;

	/** Retrieve container properties via cont_query() */
	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc)
		goto out;

	/** Verify that this is a python container */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_PYTHON) {
		rc = -DER_INVAL;
		D_ERROR("Container is not a python container: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/** Fetch root object ID */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	if (entry == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Invalid entry in properties for root object ID: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (roots->cr_oids[0].hi == 0 && roots->cr_oids[0].lo == 0) {
		rc = -DER_INVAL;
		D_ERROR("Invalid root object ID in properties: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	roots->cr_oids[0].hi |= (uint64_t)DAOS_OT_KV_HASHED << OID_FMT_TYPE_SHIFT;
	/** Open root object */
	rc = daos_kv_open(coh, roots->cr_oids[0], DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_kv_open() failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/** Mark the root */
	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	rc = daos_oit_mark(oit, roots->cr_oids[0], &marker, NULL);
	if (rc) {
		D_ERROR("daos_oit_mark() failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/** iterate through the root KV and mark oids seen in the pydaos namespace */
	rc = oit_mark(oh, oit);
	if (rc)
		goto out;

	/** list all unmarked oids and relink them in the root KV */
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = ITER_NR;
		rc = daos_oit_list_unmarked(oit, oids, &nr_entries, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_oit_list_unmarked() failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		for (i = 0; i < nr_entries; i++) {
			struct pydaos_df	dentry = {0};
			char			oid_name[42];
			size_t			len;
			enum daos_otype_t	type = daos_obj_id2type(oids[i]);

			/** Insert leaked oid back in root with oid as the name */
			len = sprintf(oid_name, "%"PRIu64".%"PRIu64"", oids[i].hi, oids[i].lo);
			D_ASSERT(len < 42);
			dentry.oid = oids[i];

			if (type == DAOS_OT_KV_HASHED) {
				printf("Adding leaked Dictionary back as: %s\n", oid_name);
				dentry.otype = PYDAOS_DICT;
			} else {
				printf("Adding leaked Array back as: %s\n", oid_name);
				dentry.otype = PYDAOS_ARRAY;
			}

			rc = daos_kv_put(oh, DAOS_TX_NONE, DAOS_COND_KEY_INSERT, oid_name,
					 sizeof(dentry), &dentry, NULL);
			if (rc) {
				D_ERROR("daos_kv_put() failed: "DF_RC"\n", DP_RC(rc));
				goto out;
			}
		}
	}

out:
	if (prop)
		daos_prop_free(prop);
	if (daos_handle_is_valid(oh)) {
		rc2 = daos_kv_close(oh, NULL);
		if (rc == 0)
			rc = rc2;
	}
	if (daos_handle_is_valid(oit)) {
		rc2 = daos_oit_close(oit, NULL);
		if (rc == 0)
			rc = rc2;
	}
	if (snap_epoch) {
		epr.epr_hi = epr.epr_lo = snap_epoch;
		rc2 = daos_cont_destroy_snap(coh, epr, NULL);
		if (rc == 0)
			rc = rc2;
	}
	if (daos_handle_is_valid(coh)) {
		rc2 = daos_cont_close(coh, NULL);
		if (rc == 0)
			rc = rc2;
	}
	if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc == 0)
			rc = rc2;
	}

	return PyInt_FromLong(rc);
}

static PyObject *
__shim_handle__cont_check(PyObject *self, PyObject *args)
{
	char	*pool;
	char	*cont;
	int	 flags;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "ssi", &pool, &cont, &flags);

	return cont_check(0, pool, cont, flags);
}

static PyObject *
__shim_handle__cont_check_by_path(PyObject *self, PyObject *args)
{
	const char		*path;
	PyObject		*obj;
	int			 flags;
	struct duns_attr_t	 attr = {0};
	int			 rc;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "si", &path, &flags);

	rc = duns_resolve_path(path, &attr);
	if (rc)
		goto out;

out:
	obj = cont_check(rc, attr.da_pool, attr.da_cont, flags);
	duns_destroy_attr(&attr);
	return obj;
}

/**
 * Implementation of baseline object functions
 */

/** Object Class Definition */
static void
oc_define(PyObject *module)
{
#define DEFINE_OC(pref, suf) \
	PyModule_AddIntConstant(module, "OC_" #pref #suf, OC_##pref##suf)

	DEFINE_OC(RP_, XSF);		/** OC_RP_XSF */

#define DEFINE_OC_PROT(prot)	\
do {				\
	DEFINE_OC(prot, TINY);	\
	DEFINE_OC(prot, SMALL);	\
	DEFINE_OC(prot, LARGE);	\
	DEFINE_OC(prot, MAX);	\
} while (0)

	DEFINE_OC_PROT();		/** OC_TINY, OC_SMALL, ... */
	DEFINE_OC_PROT(RP_);		/** OC_RP_TINY, OC_RP_SMALL, ... */
	DEFINE_OC_PROT(RP_SF_);		/** OC_RP_SF_TINY, ... */
	DEFINE_OC_PROT(EC_);		/** OC_EC_TINY, OC_EC_SMALL, ... */

#define DEFINE_OC_EXPL(name)	\
do {				\
	DEFINE_OC(name, 1);	\
	DEFINE_OC(name, 2);	\
	DEFINE_OC(name, 4);	\
	DEFINE_OC(name, 8);	\
	DEFINE_OC(name, 16);	\
	DEFINE_OC(name, 32);	\
	DEFINE_OC(name, X);	\
} while (0)

	DEFINE_OC_EXPL(S);		/** OC_S1, OC_S2, ... */
	DEFINE_OC_EXPL(RP_2G);		/** OC_RP_2G1, OC_RP_2G2, ... */
	DEFINE_OC_EXPL(RP_3G);		/** OC_RP_3G1, OC_RP_3G2, ... */
	DEFINE_OC_EXPL(RP_4G);		/** OC_RP_4G1, OC_RP_4G2, ... */
	DEFINE_OC_EXPL(RP_5G);		/** OC_RP_5G1, OC_RP_5G2, ... */
	DEFINE_OC_EXPL(RP_6G);		/** OC_RP_6G1, OC_RP_5G2, ... */
	DEFINE_OC_EXPL(EC_2P1G);	/** OC_EC_2P1G1, OC_EC_2P1G2, ... */
	DEFINE_OC_EXPL(EC_2P2G);	/** OC_EC_2P2G1, OC_EC_2P2G2, ... */
	DEFINE_OC_EXPL(EC_4P1G);	/** OC_EC_4P1G1, OC_EC_4P1G2, ... */
	DEFINE_OC_EXPL(EC_4P2G);	/** OC_EC_4P2G1, OC_EC_4P2G2, ... */
	DEFINE_OC_EXPL(EC_8P1G);	/** OC_EC_8P1G1, OC_EC_8P1G2, ... */
	DEFINE_OC_EXPL(EC_8P2G);	/** OC_EC_8P2G1, OC_EC_8P2G2, ... */
	DEFINE_OC_EXPL(EC_16P1G);	/** OC_EC_16P1G1, OC_EC_16P1G2, ... */
	DEFINE_OC_EXPL(EC_16P2G);	/** OC_EC_16P2G1, OC_EC_16P2G2, ... */

#define DEFINE_OC_INTERNAL(name)\
do {				\
	DEFINE_OC(name, 1);	\
	DEFINE_OC(name, 2);	\
	DEFINE_OC(name, 4);	\
	DEFINE_OC(name, X);	\
} while (0)

	DEFINE_OC_INTERNAL(RP_4G);          /** OC_RP_4G1, OC_RP_4G2, ... */
}

static void
cont_prop_define(PyObject *module)
{
#define DEFINE_CONT(value) \
	PyModule_AddIntConstant(module, "DAOS_PROP_" #value, DAOS_PROP_##value)
	DEFINE_CONT(CO_MIN);
	DEFINE_CONT(CO_LABEL);
	DEFINE_CONT(CO_LAYOUT_VER);
	DEFINE_CONT(CO_LAYOUT_TYPE);
	DEFINE_CONT(CO_LAYOUT_VER);
	DEFINE_CONT(CO_CSUM);
	DEFINE_CONT(CO_CSUM_CHUNK_SIZE);
	DEFINE_CONT(CO_CSUM_SERVER_VERIFY);
	DEFINE_CONT(CO_REDUN_FAC);
	DEFINE_CONT(CO_REDUN_LVL);
	DEFINE_CONT(CO_SNAPSHOT_MAX);
	DEFINE_CONT(CO_ACL);
	DEFINE_CONT(CO_COMPRESS);
	DEFINE_CONT(CO_ENCRYPT);
	DEFINE_CONT(CO_OWNER);
	DEFINE_CONT(CO_OWNER_GROUP);
	DEFINE_CONT(CO_MAX);
	DEFINE_CONT(CO_LAYOUT_UNKOWN);
	DEFINE_CONT(CO_LAYOUT_POSIX);
	DEFINE_CONT(CO_LAYOUT_HDF5);
}

/**
 * Anchor management
 * The anchor is a 128-byte structure which isn't straightforward to serialize
 * between the shim and pydaoos modules. We thus use the PyCapsule abstraction
 * that provides us with the ability to return an anchor pointer as an opaq
 * void * with its own destructor to free up the data structure when not
 * referenced any longer. That's useful when implementing python iterator
 * since pydaos needs to store the anchor returned by one iteration and
 * pass it back on the subsequent call.
 */
static inline daos_anchor_t *
capsule2anchor(PyObject *obj)
{
	daos_anchor_t	 *anchor;

	anchor = (daos_anchor_t *)PyCapsule_GetPointer(obj, "daos_anchor");

	return anchor;
}

static void
anchor_destructor(PyObject *obj)
{
	daos_anchor_t	 *anchor;

	anchor = capsule2anchor(obj);
	if (anchor == NULL)
		return;

	daos_anchor_fini(anchor);
	D_FREE(anchor);
}

static PyObject *
anchor2capsule(daos_anchor_t *anchor)
{
	PyObject *obj;

	obj = PyCapsule_New(anchor, "daos_anchor", anchor_destructor);

	return obj;
}

static PyObject *
__shim_handle__kv_open(PyObject *self, PyObject *args)
{
	PyObject		*return_list;
	struct open_handle	*hdl;
	daos_handle_t		oh;
	daos_obj_id_t		oid;
	int			flags;
	int			rc;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "KLLi", &hdl, &oid.hi,
				       &oid.lo, &flags);

	/** Open object */
	rc = daos_kv_open(hdl->coh, oid, DAOS_OO_RW, &oh, NULL);

	/* Populate return list */
	return_list = PyList_New(2);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyLong_FromLong(oh.cookie));

	return return_list;
}

static PyObject *
__shim_handle__kv_close(PyObject *self, PyObject *args)
{
	daos_handle_t	 oh;
	int		 rc;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "L", &oh.cookie);

	/** Close object */
	rc = daos_kv_close(oh, NULL);

	return PyInt_FromLong(rc);
}

/**
 * Implementation of kv functions
 */

/** max number of concurrent put/get requests */
#define MAX_INFLIGHT 16

struct kv_op {
	daos_event_t	 ev;
	PyObject	*key_obj;
	char		*key;
	char		*buf;
	daos_size_t	 size;
	daos_size_t	buf_size;
};

static inline int
kv_get_comp(struct kv_op *op, PyObject *daos_dict)
{
	PyObject	*val;
	int		 rc;

	/** insert value in python dict */
	if (op->size == 0) {
		Py_INCREF(Py_None);
		val = Py_None;
	} else {
		val = PyBytes_FromStringAndSize(op->buf, op->size);
	}

	if (val == NULL)
		return -DER_IO;

	rc = PyDict_SetItem(daos_dict, op->key_obj, val);
	if (rc < 0)
		rc = -DER_IO;
	else
		rc = DER_SUCCESS;

	Py_DECREF(val);

	return rc;
}

static PyObject *
__shim_handle__kv_get(PyObject *self, PyObject *args)
{
	PyObject	*daos_dict;
	daos_handle_t	 oh;
	PyObject	*key;
	Py_ssize_t	 pos = 0;
	daos_handle_t	 eq;
	struct kv_op	*kv_array = NULL;
	struct kv_op	*op;
	daos_event_t	*evp;
	int		 i = 0;
	int		 rc = 0;
	int		 ret;
	size_t		 v_size;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LO!l", &oh.cookie, &PyDict_Type,
				       &daos_dict, &v_size);

	if (!use_glob_eq) {
		rc = daos_eq_create(&eq);
		if (rc)
			return PyInt_FromLong(rc);
	} else {
		eq = glob_eq;
	}

	D_ALLOC_ARRAY(kv_array, MAX_INFLIGHT);
	if (kv_array == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	while (PyDict_Next(daos_dict, &pos, &key, NULL)) {
		if (i < MAX_INFLIGHT) {
			/** haven't reached max request in flight yet */
			op = &kv_array[i];
			evp = &op->ev;
			rc = daos_event_init(evp, eq, NULL);
			if (rc)
				break;
			op->buf_size = v_size;
			op->size = op->buf_size;
			D_ALLOC(op->buf, op->buf_size);
			if (op->buf == NULL) {
				rc = -DER_NOMEM;
				break;
			}

			i++;
		} else {
			/**
			 * max request request in flight reached, wait
			 * for one i/o to complete to reuse the slot
			 */
rewait:
			rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
			if (rc < 0)
				break;
			if (rc == 0) {
				rc = -DER_IO;
				break;
			}

			op = container_of(evp, struct kv_op, ev);

			/** check result of completed operation */
			if (evp->ev_error == DER_SUCCESS) {
				rc = kv_get_comp(op, daos_dict);
				if (rc != DER_SUCCESS)
					D_GOTO(err, rc);
				/* Reset the size of the request */
				op->size = op->buf_size;
				evp->ev_error = 0;
			} else if (evp->ev_error == -DER_REC2BIG) {
				char *new_buff;

				D_REALLOC_NZ(new_buff, op->buf, op->size);
				if (new_buff == NULL) {
					rc = -DER_NOMEM;
					break;
				}
				op->buf_size = op->size;
				op->buf = new_buff;

				daos_event_fini(evp);
				rc = daos_event_init(evp, eq, NULL);
				if (rc != -DER_SUCCESS)
					break;

				rc = daos_kv_get(oh, DAOS_TX_NONE, 0, op->key,
						&op->size, op->buf, evp);
				if (rc != -DER_SUCCESS)
					break;
				D_GOTO(rewait, rc);
			} else {
				rc = evp->ev_error;
				break;
			}
		}

		/** submit get request */
		op->key_obj = key;

		if (PyUnicode_Check(key)) {
			op->key = (char *)PyUnicode_AsUTF8(key);
		} else {
			op->key = PyString_AsString(key);
		}
		if (!op->key)
			D_GOTO(err, rc = 0);
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, op->key, &op->size,
				 op->buf, evp);
		if (rc) {
			break;
		}
	}

	/** wait for completion of all in-flight requests */
	do {
		ret = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
		if (ret == 1) {
			int rc2;

			op = container_of(evp, struct kv_op, ev);

			/** check result of completed operation */
			if (evp->ev_error == DER_SUCCESS) {
				rc2 = kv_get_comp(op, daos_dict);
				if (rc == DER_SUCCESS && rc2 != DER_SUCCESS)
					D_GOTO(err, rc = rc2);
				continue;
			} else if (evp->ev_error == -DER_REC2BIG) {
				char *new_buff;

				daos_event_fini(evp);
				rc2 = daos_event_init(evp, eq, NULL);

				D_REALLOC_NZ(new_buff, op->buf, op->size);
				if (new_buff == NULL)
					D_GOTO(out, rc = -DER_NOMEM);

				op->buf_size = op->size;
				op->buf = new_buff;

				rc2 = daos_kv_get(oh, DAOS_TX_NONE, 0, op->key,
						&op->size, op->buf, evp);
				if (rc2 != -DER_SUCCESS)
					D_GOTO(out, rc = rc2);
			} else {
				if (rc == DER_SUCCESS)
					rc = evp->ev_error;
			}
		}
		if (rc == DER_SUCCESS && ret == 1)
			rc = evp->ev_error;
	} while (ret == 1);

	if (rc == DER_SUCCESS && ret < 0)
		rc = ret;

	/** free up all buffers */
	for (i = 0; i < MAX_INFLIGHT; i++) {
		op = &kv_array[i];
		D_FREE(op->buf);
	}

out:
	D_FREE(kv_array);

	/** destroy event queue */
	if (!use_glob_eq) {
		ret = daos_eq_destroy(eq, DAOS_EQ_DESTROY_FORCE);
		if (rc == DER_SUCCESS && ret < 0)
			rc = ret;
	}

	/* Populate return list */
	return PyInt_FromLong(rc);

err:
	if (!use_glob_eq)
		daos_eq_destroy(eq, DAOS_EQ_DESTROY_FORCE);
	D_FREE(kv_array);

	return NULL;
}

static PyObject *
__shim_handle__kv_put(PyObject *self, PyObject *args)
{
	PyObject	*daos_dict;
	daos_handle_t	 oh;
	PyObject	*key;
	PyObject	*value;
	Py_ssize_t	 pos = 0;
	daos_handle_t	 eq;
	daos_event_t	 ev_array[MAX_INFLIGHT];
	daos_event_t	*evp;
	int		 i = 0;
	int		 rc = 0;
	int		 ret;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LO!", &oh.cookie,
				       &PyDict_Type, &daos_dict);

	if (!use_glob_eq) {
		rc = daos_eq_create(&eq);
		if (rc)
			return PyInt_FromLong(rc);
	} else {
		eq = glob_eq;
	}

	while (PyDict_Next(daos_dict, &pos, &key, &value)) {
		char		*buf;
		daos_size_t	 size;
		char		*key_str;

		if (i < MAX_INFLIGHT) {
			/** haven't reached max request in flight yet */
			evp = &ev_array[i];
			rc = daos_event_init(evp, eq, NULL);
			if (rc)
				break;
			i++;
		} else {
			/**
			 * max request request in flight reached, wait
			 * for one i/o to complete to reuse the slot
			 */
			rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
			if (rc < 0)
				break;
			if (rc == 0) {
				rc = -DER_IO;
				break;
			}

			/** check if completed operation failed */
			if (evp->ev_error != DER_SUCCESS) {
				rc = evp->ev_error;
				break;
			}
			evp->ev_error = 0;
		}

		/** XXX: Interpret all values as strings for now */
		if (value == Py_None) {
			size = 0;
		} else if (PyUnicode_Check(value)) {
			Py_ssize_t pysize = 0;

			buf = (char *)PyUnicode_AsUTF8AndSize(value, &pysize);
			size = pysize;
		} else {
			Py_ssize_t pysize = 0;

			rc = PyBytes_AsStringAndSize(value, &buf, &pysize);
			if (buf == NULL || rc != 0)
				D_GOTO(err, rc);

			size = pysize;
		}

		if (PyUnicode_Check(key)) {
			key_str = (char *)PyUnicode_AsUTF8(key);
		} else {
			key_str = PyString_AsString(key);
		}
		if (!key_str)
			D_GOTO(err, rc = 0);

		/** insert or delete kv pair */
		if (size == 0)
			rc = daos_kv_remove(oh, DAOS_TX_NONE, 0, key_str, evp);
		else
			rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key_str, size,
					 buf, evp);
		if (rc)
			break;
	}

	/** wait for completion of all in-flight requests */
	do {
		ret = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
		if (rc == DER_SUCCESS && ret == 1)
			rc = evp->ev_error;
	} while (ret == 1);

	if (rc == DER_SUCCESS && ret < 0)
		rc = ret;

	/** destroy event queue */
	if (!use_glob_eq) {
		ret = daos_eq_destroy(eq, 0);
		if (rc == DER_SUCCESS && ret < 0)
			rc = ret;
	}

	return PyInt_FromLong(rc);
err:
	if (!use_glob_eq)
		daos_eq_destroy(eq, 0);
	return NULL;
}

static PyObject *
__shim_handle__kv_iter(PyObject *self, PyObject *args)
{
	PyObject	*return_list;
	PyObject	*entries;
	uint32_t	 nr_req;
	uint32_t	 nr;
	daos_handle_t	 oh;
	daos_key_desc_t *kds = NULL;
	d_iov_t		 iov;
	d_sg_list_t	 sgl;
	daos_anchor_t	*anchor;
	PyObject	*anchor_cap;
	char		*enum_buf = NULL;
	daos_size_t	 size;
	daos_size_t	 oldsize;
	char		*ptr;
	uint32_t	 i;
	int		 rc = 0;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LOiLO", &oh.cookie, &entries,
				       &nr_req, &size, &anchor_cap);
	if (nr_req == 0 || size < 16) {
		rc = -DER_INVAL;
		goto out;
	}

	oldsize = size;

	/** Allocate an anchor for the first iteration */
	if (anchor_cap == Py_None) {
		D_ALLOC_PTR(anchor);
		if (anchor == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		daos_anchor_init(anchor, 0);
		anchor_cap = anchor2capsule(anchor);
		if (anchor_cap == NULL) {
			D_FREE(anchor);
			rc = -DER_NOMEM;
			goto out;
		}
	} else {
		anchor = capsule2anchor(anchor_cap);
		if (anchor == NULL) {
			rc = -DER_INVAL;
			goto out;
		}
		/** extra ref eventually passed to return list */
		Py_INCREF(anchor_cap);
	}

	/** Allocate & populate DAOS data structures */
	D_ALLOC_ARRAY(kds, nr_req);
	if (kds == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	D_ALLOC(enum_buf, size);
	if (enum_buf == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)enum_buf, size);
	sgl.sg_iovs = &iov;

	/**
	 * Finally enumerate entries
	 * While we want to issue a single call to daos_kv_list(), the original
	 * buffer might not be big enough for one key. We thus increase the
	 * buffer until we get at least one key back.
	 */
	do {
		sgl.sg_nr_out = 0;
		nr = nr_req;
		rc = daos_kv_list(oh, DAOS_TX_NONE, &nr, kds, &sgl, anchor,
				  NULL);

		if (rc == -DER_KEY2BIG) {
			char *new_buf;

			/** buffer too small for the key */
			size = kds[0].kd_key_len;

			/** realloc buffer twice as big */
			D_REALLOC(new_buf, enum_buf, oldsize, size);
			if (new_buf == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}
			oldsize = size;

			/** refresh daos structures to point at new buffer */
			d_iov_set(&iov, (void *)new_buf, size);
			enum_buf = new_buf;
			nr = 0;
			continue;
		}

		if (rc)
			goto out;
	} while (!daos_anchor_is_eof(anchor) && nr == 0);

	/** Populate python list with entries */
	for (ptr = enum_buf, i = 0; i < nr; i++) {
		Py_ssize_t len = kds[i].kd_key_len;

		rc = PyList_Append(entries,
				   PyString_FromStringAndSize(ptr, len));
		if (rc  < 0) {
			rc = -DER_IO;
			break;
		}
		ptr += kds[i].kd_key_len;
	}

	/** Adjust nr entries and buffer size for next iteration */
	if (nr_req == nr) {
		/**
		 * we filled all the slot, bump the number of slots for the
		 * next time
		 */
		nr_req *= 2;
	} else if (size < 1024 * 1024 && nr > 0 &&
		   enum_buf + size - ptr < (ptr - enum_buf) / nr) {
		/**
		 * there might not have been enough room in the buffer to fit
		 * another entry, bump buffer size ...
		 * still set upper limit at 1MB as a safeguard
		 */
		if (size < 512 * 1024)
			size *= 2;
		else
			size = 1024 * 1024;
	}

out:
	D_FREE(kds);
	D_FREE(enum_buf);

	/* Populate return list */
	return_list = PyList_New(4);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyInt_FromLong(nr_req));
	PyList_SetItem(return_list, 2, PyInt_FromLong(size));
	if (rc || daos_anchor_is_eof(anchor)) {
		if (anchor_cap != NULL)
			Py_DECREF(anchor_cap);
		Py_INCREF(Py_None);
		PyList_SetItem(return_list, 3, Py_None);
	} else {
		PyList_SetItem(return_list, 3, anchor_cap);
	}

	return return_list;
}

/**
 * Python shim module
 */
#define EXPORT_PYTHON_METHOD(name)		\
{						\
	#name,					\
	__shim_handle__##name,			\
	METH_VARARGS | METH_KEYWORDS,		\
	"text"					\
}

static PyMethodDef daosMethods[] = {
	/** Generic methods */
	EXPORT_PYTHON_METHOD(daos_init),
	EXPORT_PYTHON_METHOD(daos_fini),
	EXPORT_PYTHON_METHOD(err_to_str),

	/** Container operations */
	EXPORT_PYTHON_METHOD(cont_open),
	EXPORT_PYTHON_METHOD(cont_open_by_path),
	EXPORT_PYTHON_METHOD(cont_get),
	EXPORT_PYTHON_METHOD(cont_newobj),
	EXPORT_PYTHON_METHOD(cont_close),
	EXPORT_PYTHON_METHOD(cont_check),
	EXPORT_PYTHON_METHOD(cont_check_by_path),

	/** KV operations */
	EXPORT_PYTHON_METHOD(kv_open),
	EXPORT_PYTHON_METHOD(kv_close),
	EXPORT_PYTHON_METHOD(kv_get),
	EXPORT_PYTHON_METHOD(kv_put),
	EXPORT_PYTHON_METHOD(kv_iter),

	/** Array operations */

	{NULL, NULL}
};

struct module_struct {
	PyObject *error;
};
#define GETSTATE(m) ((struct module_struct *)PyModule_GetState(m))

static int
__daosbase_traverse(PyObject *m, visitproc visit, void *arg)
{
	Py_VISIT(GETSTATE(m)->error);
	return 0;
}

static int
__daosbase_clear(PyObject *m)
{
	Py_CLEAR(GETSTATE(m)->error);
	return 0;
}

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"pydaos_shim",
	NULL,
	sizeof(struct module_struct),
	daosMethods,
	NULL,
	__daosbase_traverse,
	__daosbase_clear,
	NULL
};

PyMODINIT_FUNC PyInit_pydaos_shim(void)

{
	PyObject *module;

	module = PyModule_Create(&moduledef);

#define DEFINE_PY_RETURN_CODE(name, errstr) PyModule_AddIntConstant(module, "" #name, name);

	/** export return codes */
	D_FOREACH_GURT_ERR(DEFINE_PY_RETURN_CODE);
	D_FOREACH_DAOS_ERR(DEFINE_PY_RETURN_CODE);
	PyModule_AddIntConstant(module, "DER_SUCCESS", DER_SUCCESS);
	PyModule_AddIntConstant(module, "DER_UNKNOWN", DER_UNKNOWN);

	/** export object type */
	PyModule_AddIntConstant(module, "PYDAOS_DICT", PYDAOS_DICT);
	PyModule_AddIntConstant(module, "PYDAOS_ARRAY", PYDAOS_ARRAY);

	/** export object class */
	oc_define(module);

	/** export container properties */
	cont_prop_define(module);

	return module;
}
