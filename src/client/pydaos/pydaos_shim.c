/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifdef __USE_PYTHON3__
/* Those are gone from python3, replaced with new functions */
#define PyInt_FromLong		PyLong_FromLong
#define PyString_FromString	PyUnicode_FromString
#define PyString_FromStringAndSize PyUnicode_FromStringAndSize
#define PyString_AsString	PyBytes_AsString
#endif

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

#define PY_SHIM_MAGIC_NUMBER 0x7A89

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

/**
 * Implementations of baseline shim functions
 */

static PyObject *
__shim_handle__daos_init(PyObject *self, PyObject *args)
{
	int rc;

	rc = daos_init();

	return PyInt_FromLong(rc);
}

static PyObject *
__shim_handle__daos_fini(PyObject *self, PyObject *args)
{
	int rc;

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
cont_open(int ret, uuid_t puuid, uuid_t cuuid, int flags)
{
	PyObject	*return_list;
	daos_handle_t	 coh = {0};
	daos_handle_t	 poh = {0};
	int		 rc;

	if (ret != DER_SUCCESS) {
		rc = ret;
		goto out;
	}

	/** Connect to pool */
	rc = daos_pool_connect(puuid, "daos_server", DAOS_PC_RW, &poh,
			       NULL, NULL);
	if (rc)
		goto out;

	/** Open container */
	rc = daos_cont_open(poh, cuuid, DAOS_COO_RW, &coh, NULL, NULL);
	if (rc)
		daos_pool_disconnect(poh, NULL);
out:
	/* Populate return list */
	return_list = PyList_New(3);
	PyList_SetItem(return_list, 0, PyInt_FromLong(rc));
	PyList_SetItem(return_list, 1, PyLong_FromLong(poh.cookie));
	PyList_SetItem(return_list, 2, PyLong_FromLong(coh.cookie));

	return return_list;
}

static PyObject *
__shim_handle__cont_open(PyObject *self, PyObject *args)
{
	const char	*puuid_str;
	const char	*cuuid_str;
	uuid_t		 puuid;
	uuid_t		 cuuid;
	int		 flags;
	int		 rc;

	/** Parse arguments, flags not used for now */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "ssi", &puuid_str, &cuuid_str,
				       &flags);
	rc = uuid_parse(puuid_str, puuid);
	if (rc)
		goto out;

	rc = uuid_parse(cuuid_str, cuuid);
out:
	return cont_open(rc, puuid, cuuid, flags);
}

static PyObject *
__shim_handle__cont_open_by_path(PyObject *self, PyObject *args)
{
	const char		*path;
	int			 flags;
	struct duns_attr_t	 attr = {0};
	int			 rc;

	/** Parse arguments, flags not used for now */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "si", &path, &flags);

	rc = duns_resolve_path(path, &attr);

	return cont_open(rc, attr.da_puuid, attr.da_cuuid, flags);
}

static PyObject *
__shim_handle__cont_close(PyObject *self, PyObject *args)
{
	daos_handle_t	 poh;
	daos_handle_t	 coh;
	int		 rc;
	int		 ret;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LL", &poh.cookie, &coh.cookie);

	/** Close container */
	rc = daos_cont_close(coh, NULL);

	/** Disconnect from pool */
	ret = daos_pool_disconnect(poh, NULL);

	if (rc == 0)
		rc = ret;

	return PyInt_FromLong(rc);
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
	DEFINE_OC(name, 64);	\
	DEFINE_OC(name, 128);	\
	DEFINE_OC(name, 256);	\
	DEFINE_OC(name, 512);	\
	DEFINE_OC(name, 1K);	\
	DEFINE_OC(name, 2K);	\
	DEFINE_OC(name, 4K);	\
	DEFINE_OC(name, 8K);	\
	DEFINE_OC(name, X);	\
} while (0)

	DEFINE_OC_EXPL(S);		/** OC_S1, OC_S2, ... */
	DEFINE_OC_EXPL(RP_2G);		/** OC_RP_2G1, OC_RP_2G2, ... */
	DEFINE_OC_EXPL(RP_3G);		/** OC_RP_3G1, OC_RP_3G2, ... */
	DEFINE_OC_EXPL(RP_8G);		/** OC_RP_8G1, OC_RP_8G2, ... */
	DEFINE_OC_EXPL(EC_2P1G);	/** OC_EC_2P1G1, OC_EC_2P1G2, ... */
	DEFINE_OC_EXPL(EC_2P2G);	/** OC_EC_2P2G1, OC_EC_2P2G2, ... */
	DEFINE_OC_EXPL(EC_4P1G);	/** OC_EC_4P1G1, OC_EC_4P1G2, ... */
	DEFINE_OC_EXPL(EC_4P2G);	/** OC_EC_4P2G1, OC_EC_4P2G2, ... */
	DEFINE_OC_EXPL(EC_8P1G);	/** OC_EC_8P1G1, OC_EC_8P1G2, ... */
	DEFINE_OC_EXPL(EC_8P2G);	/** OC_EC_8P2G1, OC_EC_8P2G2, ... */
	DEFINE_OC_EXPL(EC_16P1G);	/** OC_EC_16P1G1, OC_EC_16P1G2, ... */
	DEFINE_OC_EXPL(EC_16P2G);	/** OC_EC_16P2G1, OC_EC_16P2G2, ... */
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
 * void * with its own destuctor to free up the data structure when not
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
__shim_handle__obj_idroot(PyObject *self, PyObject *args)
{
	PyObject		*return_list;
	daos_oclass_id_t	 cid;
	int			 cid_in;
	daos_obj_id_t		 oid;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "i", &cid_in);
	cid = (uint16_t) cid_in;
	oid.hi = 0;
	oid.lo = 0;

	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, cid, 0);

	return_list = PyList_New(3);
	PyList_SetItem(return_list, 0, PyInt_FromLong(DER_SUCCESS));
	PyList_SetItem(return_list, 1, PyLong_FromLong(oid.hi));
	PyList_SetItem(return_list, 2, PyLong_FromLong(oid.lo));

	return return_list;
}

static PyObject *
__shim_handle__obj_idgen(PyObject *self, PyObject *args)
{
	PyObject		*return_list;
	daos_handle_t		 coh;
	daos_oclass_id_t	 cid;
	int			 cid_in;
	daos_obj_id_t		 oid;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "Li", &coh.cookie, &cid_in);
	cid = (uint16_t) cid_in;

	/** XXX: OID should be generated via daos_cont_alloc_oids() */
	srand(time(0));
	oid.lo = rand();
	oid.hi = 0;

	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, cid, 0);

	return_list = PyList_New(3);
	PyList_SetItem(return_list, 0, PyInt_FromLong(DER_SUCCESS));
	PyList_SetItem(return_list, 1, PyLong_FromLong(oid.hi));
	PyList_SetItem(return_list, 2, PyLong_FromLong(oid.lo));

	return return_list;
}

static PyObject *
__shim_handle__kv_open(PyObject *self, PyObject *args)
{
	PyObject	*return_list;
	daos_handle_t	 coh;
	daos_handle_t	 oh;
	daos_obj_id_t	 oid;
	int		 flags;
	int		 rc;

	/** Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LLLi", &coh.cookie, &oid.hi,
				       &oid.lo, &flags);

	/** Open object */
	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);

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
	int		 rc;
	int		 ret;
	size_t		 v_size;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LO!l", &oh.cookie, &PyDict_Type,
				       &daos_dict, &v_size);

	rc = daos_eq_create(&eq);
	if (rc)
		return PyInt_FromLong(rc);

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
					D_GOTO(err, 0);
				/* Reset the size of the request */
				op->size = op->buf_size;
				evp->ev_error = 0;
			} else if (evp->ev_error == -DER_REC2BIG) {
				char *new_buff;

				D_REALLOC(new_buff, op->buf, op->size);
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
				D_GOTO(rewait, 0);
			} else {
				rc = evp->ev_error;
				break;
			}
		}

		/** submit get request */
		op->key_obj = key;
#ifdef __USE_PYTHON3__
		if (PyUnicode_Check(key)) {
			op->key = (char *)PyUnicode_AsUTF8(key);
		} else
#endif
		{
			op->key = PyString_AsString(key);
		}
		if (!op->key)
			D_GOTO(err, 0);
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

				D_REALLOC(new_buff, op->buf, op->size);
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
	ret = daos_eq_destroy(eq, DAOS_EQ_DESTROY_FORCE);
	if (rc == DER_SUCCESS && ret < 0)
		rc = ret;

	/* Populate return list */
	return PyInt_FromLong(rc);

err:
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
	int		 rc;
	int		 ret;

	/* Parse arguments */
	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LO!", &oh.cookie,
				&PyDict_Type, &daos_dict);

	rc = daos_eq_create(&eq);
	if (rc)
		return PyInt_FromLong(rc);

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
#ifdef __USE_PYTHON3__
		} else if (PyUnicode_Check(value)) {
			Py_ssize_t pysize = 0;

			buf = (char *)PyUnicode_AsUTF8AndSize(value, &pysize);
			size = pysize;
#endif
		} else {
			Py_ssize_t pysize = 0;

			rc = PyBytes_AsStringAndSize(value, &buf, &pysize);
			if (buf == NULL || rc != 0)
				D_GOTO(err, 0);

			size = pysize;
		}

#ifdef __USE_PYTHON3__
		if (PyUnicode_Check(key)) {
			key_str = (char *)PyUnicode_AsUTF8(key);
		} else
#endif
		{
			key_str = PyString_AsString(key);
		}
		if (!key_str)
			D_GOTO(err, 0);

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
	ret = daos_eq_destroy(eq, 0);
	if (rc == DER_SUCCESS && ret < 0)
		rc = ret;

	return PyInt_FromLong(rc);
err:
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

	/** Allocate an anchor for the first iteration */
	if (anchor_cap == Py_None) {
		D_ALLOC_PTR(anchor);
		if (anchor == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		daos_anchor_set_zero(anchor);
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
			D_REALLOC(new_buf, enum_buf, size);
			if (new_buf == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}

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
	if (kds)
		D_FREE(kds);
	if (enum_buf)
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
	EXPORT_PYTHON_METHOD(cont_close),

	/** Object operations */
	EXPORT_PYTHON_METHOD(obj_idgen),
	EXPORT_PYTHON_METHOD(obj_idroot),

	/** KV operations */
	EXPORT_PYTHON_METHOD(kv_open),
	EXPORT_PYTHON_METHOD(kv_close),
	EXPORT_PYTHON_METHOD(kv_get),
	EXPORT_PYTHON_METHOD(kv_put),
	EXPORT_PYTHON_METHOD(kv_iter),

	{NULL, NULL}
};

#if PY_MAJOR_VERSION >= 3
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
	"pydaos_shim_3",
	NULL,
	sizeof(struct module_struct),
	daosMethods,
	NULL,
	__daosbase_traverse,
	__daosbase_clear,
	NULL
};

PyMODINIT_FUNC PyInit_pydaos_shim_3(void)
#else
void
initpydaos_shim_27(void)
#endif
{
	PyObject *module;

#if PY_MAJOR_VERSION >= 3
	module = PyModule_Create(&moduledef);
#else
	module = Py_InitModule("pydaos_shim_27", daosMethods);
#endif

#define DEFINE_PY_RETURN_CODE(name, desc, errstr) \
	PyModule_AddIntConstant(module, ""#name, desc);

	/** export return codes */
	D_FOREACH_GURT_ERR(DEFINE_PY_RETURN_CODE);
	D_FOREACH_DAOS_ERR(DEFINE_PY_RETURN_CODE);
	PyModule_AddIntConstant(module, "DER_SUCCESS", DER_SUCCESS);

	/** export object class */
	oc_define(module);

	/** export container properties */
	cont_prop_define(module);

#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}
