/**
 * (C) Copyright 2019 Intel Corporation.
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
#ifdef __USE_PYTHON3__
/* Those are gone from python3, replaced with new functions */
#define PyInt_FromLong		PyLong_FromLong
#define PyInt_AsLong		PyLong_AsLong
#define PyString_FromString	PyUnicode_FromString
#define PyString_AsString	PyBytes_AsString
#endif

#include <Python.h>
#include "daos_types.h"
#include "daos.h"
#include "daos_obj_class.h"
#include <daos/object.h>

#define PY_SHIM_MAGIC_NUMBER 0x7A89

static int
__is_magic_valid(int input)
{
	if (input != PY_SHIM_MAGIC_NUMBER) {
		D_ERROR("MAGIC number doesnt match, expected %d got %d\n",
			PY_SHIM_MAGIC_NUMBER, input);
		return 0;
	}

	return 1;
}

/**
 * Implementations of shim functions
 */

static PyObject *
__shim_handle__test(PyObject *self, PyObject *args)
{
	long ret;
	int magic;

	if (!PyArg_ParseTuple(args, "i", &magic)) {
		D_ERROR("Bad arguments passed to %s\n", __func__);
		return NULL;
	}
	if (!__is_magic_valid(magic)) {
		return NULL;
	}

	/* Call C function */
	ret = 1;

	return PyInt_FromLong(ret);
}

/**
 * Exports of enumerations/macros
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
	EXPORT_PYTHON_METHOD(test),
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
	"pydaos_shim_36",
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

	oc_define(module);

#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}
