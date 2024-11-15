/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2024 Google LLC
 * (C) Copyright 2024 Enakta Labs Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Python.h>

#include <fcntl.h>  /* S_ISDIR */
#include <libgen.h> /* dirname() */

#include <daos.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_types.h>
#include <daos_fs.h>

#include <daos/common.h>
#include <gurt/debug.h>
#include <gurt/common.h>

#define PY_SHIM_MAGIC_NUMBER (0x7A8B)
#define EQ_POLL_BATCH_SIZE   (64)

struct dfs_handle {
	int           flags;
	dfs_t        *dfs;
	d_iov_t       global;

	daos_handle_t eq;
	pid_t         eq_owner_pid;
};

/* Parse arguments and magic number.
 * As well as returning NULL this sets the Python exception state as required
 */
#define RETURN_NULL_IF_FAILED_TO_PARSE(args, format, x...)                                         \
	do {                                                                                       \
		int magic;                                                                         \
		if (!PyArg_ParseTuple(args, "i" format, &magic, x)) {                              \
			D_DEBUG(DB_ANY, "Bad args passed to %s", __func__);                        \
			return NULL;                                                               \
		}                                                                                  \
		if (magic != PY_SHIM_MAGIC_NUMBER) {                                               \
			D_ERROR("MAGIC number does not match, expected %d got %d\n",               \
				PY_SHIM_MAGIC_NUMBER, magic);                                      \
			PyErr_Format(PyExc_TypeError,                                              \
				     "Bad magic value in torch(%s), expected %d got %d", __func__, \
				     PY_SHIM_MAGIC_NUMBER, magic);                                 \
			return NULL;                                                               \
		}                                                                                  \
	} while (0)

static void
atfork_handler(void)
{
	int rc = daos_reinit();
	if (rc) {
		D_ERROR("daos_reinit() failed in child process %s (rc=%d)", d_errstr(rc), rc);
	}
}

static PyObject *
__shim_handle__module_init(PyObject *self, PyObject *args)
{
	int rc = daos_init();
	if (rc) {
		PyErr_Format(PyExc_TypeError, "Could not initialize DAOS module %s (rc=%d)",
			     d_errstr(rc), rc);
		return NULL;
	}

	rc = pthread_atfork(NULL, NULL, &atfork_handler);
	if (rc) {
		PyErr_Format(PyExc_TypeError, "Could not set atfork handler %s (rc=%d)",
			     strerror(rc), rc);
		return NULL;
	}

	return PyLong_FromLong(rc);
}

static PyObject *
__shim_handle__module_fini(PyObject *self, PyObject *args)
{
	int rc = daos_fini();
	if (rc) {
		rc = daos_der2errno(rc);
	}

	return PyLong_FromLong(rc);
}

static PyObject *
__shim_handle__torch_connect(PyObject *self, PyObject *args)
{
	int                rc      = 0;
	char              *pool    = NULL;
	char              *cont    = NULL;
	int                rd_only = 1;
	struct dfs_handle *hdl     = NULL;

	PyObject          *result = PyList_New(2);
	if (result == NULL) {
		return PyErr_NoMemory();
	}

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "ssp", &pool, &cont, &rd_only);

	D_ALLOC_PTR(hdl);
	if (hdl == NULL) {
		rc = ENOMEM;
		goto out;
	}
	hdl->flags = rd_only ? O_RDONLY : O_RDWR;

	rc = dfs_init();
	if (rc) {
		D_ERROR("Could not initialize DFS: %s (rc=%d)", strerror(rc), rc);
		goto out;
	}

	rc = dfs_connect(pool, NULL, cont, hdl->flags, NULL, &hdl->dfs);
	if (rc) {
		D_ERROR("Could not connect to %s:%s: %s (rc=%d)", pool, cont, strerror(rc), rc);
		goto out;
	}

	hdl->global.iov_buf     = NULL;
	hdl->global.iov_buf_len = 0;

	rc = dfs_local2global_all(hdl->dfs, &hdl->global);
	if (rc) {
		D_ERROR("Could not get global handler size for dfs: %s (rc=%d)", strerror(rc), rc);
		goto out;
	}

	D_ALLOC(hdl->global.iov_buf, hdl->global.iov_buf_len);
	if (hdl->global.iov_buf == NULL) {
		rc = ENOMEM;
		goto out;
	}

	rc = dfs_local2global_all(hdl->dfs, &hdl->global);
	if (rc) {
		D_ERROR("Could not create global handler for dfs: %s (rc=%d)", strerror(rc), rc);
		goto out;
	}

	rc = daos_eq_create(&hdl->eq);
	if (rc) {
		D_ERROR("Could not create event queue: %s (rc=%d)", d_errstr(rc), rc);
		rc = daos_der2errno(rc);
		goto out;
	}
	hdl->eq_owner_pid = getpid();

out:
	if (rc) {
		dfs_disconnect(hdl->dfs);

		D_FREE(hdl->global.iov_buf);
		D_FREE(hdl);
		hdl = NULL;
	}

	PyList_SetItem(result, 0, PyLong_FromLong(rc));
	PyList_SetItem(result, 1, PyLong_FromVoidPtr(hdl));

	return result;
}

static PyObject *
__shim_handle__torch_disconnect(PyObject *self, PyObject *args)
{
	struct dfs_handle *hdl = NULL;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "K", &hdl);

	assert(hdl->dfs != NULL);

	if (hdl->eq_owner_pid != getpid()) {
		return PyLong_FromLong(EACCES);
	}

	int rc = dfs_disconnect(hdl->dfs);
	if (rc) {
		D_ERROR("Could not disconnect DFS: %s (rc=%d)", strerror(rc), rc);
		goto out;
	}

	rc = daos_eq_destroy(hdl->eq, DAOS_EQ_DESTROY_FORCE);
	if (rc) {
		D_ERROR("Could not destroy event queue: %s (rc=%d)", d_errstr(rc), rc);
		rc = daos_der2errno(rc);
		goto out;
	}

	rc = dfs_fini();
	if (rc) {
		D_ERROR("Could not finalize DFS: %s (rc=%d)", strerror(rc), rc);
		goto out;
	}

out:
	D_FREE(hdl->global.iov_buf);
	D_FREE(hdl);

	return PyLong_FromLong(rc);
}

/*
  DataLoader uses python multiprocessing with fork method to create its workers.
  This function is the part of initialization routine for such workers, it reuses global connection
  to the container and create an even queue for the worker process.
 */
static PyObject *
__shim_handle__torch_worker_init(PyObject *self, PyObject *args)
{
	struct dfs_handle *hdl = NULL;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "K", &hdl);

	if (hdl->eq_owner_pid == getpid()) {
		return PyLong_FromLong(0);
	}

	int rc = dfs_init();
	if (rc) {
		return PyLong_FromLong(rc);
	}

	hdl->dfs          = NULL;
	hdl->eq           = DAOS_HDL_INVAL;
	hdl->eq_owner_pid = getpid();

	rc = dfs_global2local_all(hdl->flags, hdl->global, &hdl->dfs);
	if (rc) {
		D_ERROR("Could not create local handler from global one: %s (rc=%d)", strerror(rc),
			rc);
		return PyLong_FromLong(rc);
	}

	rc = daos_eq_create(&hdl->eq);
	if (rc) {
		D_ERROR("Could not create event queue: %s (rc=%d)", d_errstr(rc), rc);
		rc = daos_der2errno(rc);
	}

	return PyLong_FromLong(rc);
}

static PyObject *
__shim_handle__torch_recommended_dir_split(PyObject *self, PyObject *args)
{
	struct dfs_handle *hdl  = NULL;
	char              *path = NULL;
	dfs_obj_t         *obj  = NULL;
	uint32_t           nr   = 0;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "Ls", &hdl, &path);

	assert(hdl->dfs != NULL);

	int rc = dfs_lookup(hdl->dfs, path, O_RDONLY, &obj, NULL, NULL);
	if (rc) {
		return PyLong_FromLong(-rc);
	}

	rc = dfs_obj_anchor_split(obj, &nr, NULL);
	if (rc) {
		return PyLong_FromLong(-rc);
	}

	rc = dfs_release(obj);
	if (rc) {
		return PyLong_FromLong(-rc);
	}

	return PyLong_FromLong(nr);
}

static PyObject *
__shim_handle__torch_list_with_anchor(PyObject *self, PyObject *args)
{
	struct dfs_handle *hdl           = NULL;
	char              *path          = NULL;
	PyObject          *files         = NULL;
	PyObject          *dirs          = NULL;
	uint32_t           readdir_chunk = 0;
	uint32_t           anchor_index  = 0;
	dfs_obj_t         *obj           = NULL;
	daos_anchor_t      anchor;
	int                rc       = 0;
	uint32_t           nr       = 0;
	struct stat       *stats    = NULL;
	struct dirent     *dentries = NULL;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LsIOOI", &hdl, &path, &anchor_index, &files, &dirs,
				       &readdir_chunk);

	assert(hdl->dfs != NULL);

	if (readdir_chunk == 0) {
		return PyLong_FromLong(EINVAL);
	}

	D_ALLOC_ARRAY(dentries, readdir_chunk);
	if (dentries == NULL) {
		rc = ENOMEM;
		goto out;
	}

	D_ALLOC_ARRAY(stats, readdir_chunk);
	if (stats == NULL) {
		rc = ENOMEM;
		goto out;
	}

	rc = dfs_lookup(hdl->dfs, path, O_RDONLY, &obj, NULL, NULL);
	if (rc) {
		D_ERROR("Could not lookup object at '%s': %s (rc=%d)", path, strerror(rc), rc);
		goto out;
	}

	daos_anchor_init(&anchor, 0);
	rc = dfs_obj_anchor_set(obj, anchor_index, &anchor);
	if (rc) {
		D_ERROR("Could not set anchor '%u' for object at '%s': %s (rc=%d)", anchor_index,
			path, strerror(rc), rc);
		goto out;
	}

	do {
		nr = readdir_chunk;
		rc = dfs_readdirplus(hdl->dfs, obj, &anchor, &nr, dentries, stats);
		if (rc) {
			D_ERROR("Readdirplus of '%s' failed: %s (rc=%d)", path, strerror(rc), rc);
			goto out;
		}

		for (uint32_t i = 0; i < nr; ++i) {
			const struct stat *st    = &stats[i];
			PyObject          *dname = PyUnicode_FromString(dentries[i].d_name);
			if (dname == NULL) {
				rc = ENOMEM;
				goto out;
			}

			if (S_ISDIR(st->st_mode)) {
				rc = PyList_Append(dirs, dname);
				Py_DECREF(dname); /* PyList_Append does not steal the reference */
				if (rc) {
					goto out;
				}
				continue;
			}

			PyObject *item = PyTuple_New(2);
			if (item == NULL) {
				rc = ENOMEM;
				goto out;
			}

			PyTuple_SetItem(item, 0, dname); /* steals the reference */
			PyTuple_SetItem(item, 1, PyLong_FromLong(st->st_size));

			rc = PyList_Append(files, item);
			Py_DECREF(item); /* PyList_Append does not steal the reference */
			if (rc < 0) {
				rc = EIO;
				goto out;
			}
		}
	} while (nr > 0);

out:
	D_FREE(dentries);
	D_FREE(stats);

	if (obj) {
		rc = dfs_release(obj);
	}

	return PyLong_FromLong(rc);
}

static PyObject *
__shim_handle__torch_read(PyObject *self, PyObject *args)
{
	ssize_t            rc     = 0;
	struct dfs_handle *hdl    = NULL;
	char              *path   = NULL;
	dfs_obj_t         *obj    = NULL;
	PyObject          *buffer = NULL;
	Py_buffer          bview;
	d_iov_t            iov;
	daos_size_t        read = 0;

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LsO", &hdl, &path, &buffer);
	assert(hdl->dfs != NULL);

	if (!PyObject_CheckBuffer(buffer)) {
		PyErr_SetString(PyExc_TypeError,
				"Expected an object that supports the buffer protocol");
		return NULL;
	}

	if (PyObject_GetBuffer(buffer, &bview, PyBUF_WRITE) == -1) {
		PyErr_SetString(PyExc_BufferError, "Buffer is not writable");
		return NULL;
	}

	/*
	  Since python can use buffer like objects that might not have contiguous memory layout,
	  let's put a guardrail accepting only buffers with contiguous memory region
	*/
	if (!PyBuffer_IsContiguous(&bview, 'C')) {
		PyErr_SetString(PyExc_BufferError, "Buffer is not contiguous");
		PyBuffer_Release(&bview);
		return NULL;
	}

	read = bview.len;
	d_iov_set(&iov, bview.buf, read);

	d_sg_list_t sgl = {
	    .sg_nr     = 1,
	    .sg_nr_out = 0,
	    .sg_iovs   = &iov,
	};

	rc = dfs_lookup(hdl->dfs, path, O_RDONLY, &obj, NULL, NULL);
	if (rc) {
		D_ERROR("Could not lookup '%s': %s (rc=%ld)", path, strerror(rc), rc);
		goto out;
	}

	rc = dfs_read(hdl->dfs, obj, &sgl, 0 /* offset */, &read, NULL);
	if (rc) {
		goto out;
	}
	if (read != bview.len) {
		rc = EIO;
	}

out:
	PyBuffer_Release(&bview);

	if (obj) {
		int rc2 = dfs_release(obj);
		if (rc2) {
			D_ERROR("Could not release object '%s': %s (rc=%d)", path, strerror(rc2),
				rc2);
			if (rc == 0)
				rc = rc2;
		}
	}

	return PyLong_FromLong(rc);
}

/* describes the IO operation */
struct io_op {
	daos_event_t ev;

	dfs_obj_t   *obj;
	PyObject    *item;

	/* Purely for debug purpose: should not be freed as it's not the owner of the data */
	const char  *path;

	daos_size_t  size;

	d_iov_t      iov;
	d_sg_list_t  sgl;

	/* Buffer view that implements Python Buffer Protocol, has to be release after use */
	Py_buffer    buf_view;
};

static int
start_read_op(struct dfs_handle *hdl, PyObject *item, struct io_op *op)
{
	assert(op != NULL);

	int           rc  = 0;
	int           rc2 = 0;
	daos_event_t *evp = &op->ev;

	PyObject     *py_path = PyTuple_GetItem(item, 0);
	PyObject     *py_buff = PyTuple_GetItem(item, 1);

	if (py_path == NULL || py_buff == NULL) {
		D_ERROR("Each tuple must contain exactly two elements: path and bytearray");
		return EINVAL;
	}

	const char *path = PyUnicode_AsUTF8(py_path);
	if (path == NULL) {
		D_ERROR("First element of a tuple does not look like a path");
		return EINVAL;
	}

	if (PyObject_GetBuffer(py_buff, &op->buf_view, PyBUF_WRITE) == -1) {
		D_ERROR("Buffer is not writable");
		return EINVAL;
	}

	if (!PyBuffer_IsContiguous(&op->buf_view, 'C')) {
		D_ERROR("Buffer for '%s' is not contiguous", path);
		rc = EINVAL;
		goto out;
	}

	rc = daos_event_init(evp, hdl->eq, NULL);
	if (rc) {
		D_ERROR("Could not init event: %s (rc=%d)", d_errstr(rc), rc);
		rc = daos_der2errno(rc);
		goto out;
	}

	rc = dfs_lookup(hdl->dfs, path, O_RDONLY, &op->obj, NULL, NULL);
	if (rc) {
		D_ERROR("Could not lookup path '%s': %s (rc=%d)", op->path, strerror(rc), rc);
		goto out;
	}

	op->path = path;
	op->size = op->buf_view.len;
	d_iov_set(&op->iov, op->buf_view.buf, op->size);

	op->sgl.sg_nr     = 1;
	op->sgl.sg_nr_out = 0;
	op->sgl.sg_iovs   = &op->iov;

	rc = dfs_read(hdl->dfs, op->obj, &op->sgl, 0 /* offset */, &op->size, &op->ev);
	if (rc) {
		D_ERROR("Could not start async read on '%s': %s (rc=%d)", op->path, strerror(rc),
			rc);
		goto out;
	}

	return 0;

out:
	PyBuffer_Release(&op->buf_view);

	rc2 = daos_event_fini(&op->ev);
	if (rc2) {
		D_ERROR("Could not finalize event: %s (rc=%d)", d_errstr(rc2), rc2);
	}

	if (op->obj != NULL) {
		rc2 = dfs_release(op->obj);
		if (rc2) {
			D_ERROR("Could not release obj handler of '%s': %s (rc=%d)", op->path,
				strerror(rc2), rc2);
		}
		op->obj = NULL;
	}

	return rc;
}

static int
complete_read_op(struct dfs_handle *hdl, struct io_op *op)
{
	int rc  = 0;
	int rc2 = 0;

	assert(op != NULL);

	D_DEBUG(DB_ANY, "READ of %zu bytes from '%s' completed with  status: %s (rc = %d)",
		op->size, op->path, d_errstr(op->ev.ev_error), op->ev.ev_error);

	rc = op->ev.ev_error;
	if (rc == 0 && op->size != op->buf_view.len) {
		rc = EIO;
	}

	rc2 = daos_event_fini(&op->ev);
	if (rc2) {
		D_ERROR("Could not finalize event handler of '%s': %s (rc=%d)", op->path,
			d_errstr(rc2), rc2);
		if (rc == 0)
			rc = rc2;
	}

	rc2 = dfs_release(op->obj);
	if (rc2) {
		D_ERROR("Could not release object handler %s: %s (rc=%d)", op->path, strerror(rc2),
			rc2);
		if (rc == 0)
			rc = rc2;
	}
	op->obj = NULL;

	PyBuffer_Release(&op->buf_view);
	return rc;
}

/*

 */
static PyObject *
__shim_handle__torch_batch_read(PyObject *self, PyObject *args)
{
	int                rc        = 0;
	int                completed = 0;
	PyObject          *items     = NULL;
	Py_ssize_t         nr        = 0;
	struct io_op      *ops       = NULL;
	struct dfs_handle *hdl       = NULL;
	daos_event_t      *evp[EQ_POLL_BATCH_SIZE];

	RETURN_NULL_IF_FAILED_TO_PARSE(args, "LO", &hdl, &items);
	assert(hdl->dfs != NULL);

	nr = PyList_Size(items);
	if (nr <= 0) {
		return PyLong_FromLong(EINVAL);
	}

	D_ALLOC_ARRAY(ops, nr);
	if (ops == NULL) {
		return PyLong_FromLong(ENOMEM);
	}

	/* For optimal usage of transport layer, we try to enqueue all items in the bath
	   leaving the throttling up to transport level.
	   It can be manually adjusted by D_QUOTA_RPC environment variable.
	 */
	for (Py_ssize_t i = 0; i < nr; ++i) {
		PyObject *item = PyList_GetItem(items, i);
		if (item == NULL) {
			D_ERROR("Unexpected NULL entry in the batch read list");
			rc = EINVAL;

			nr = i;
			break;
		}

		rc = start_read_op(hdl, item, &ops[i]);
		if (rc) {
			nr = i;
			break;
		}
	}

	do {
		int eq_rc = daos_eq_poll(hdl->eq, 0, DAOS_EQ_NOWAIT, nr, evp);
		if (eq_rc < 0) {
			D_ERROR("Could not poll event queue: %s (rc=%d)", d_errstr(eq_rc), eq_rc);
			rc = daos_der2errno(eq_rc);
			break;
		}

		for (int i = 0; i < eq_rc; ++i) {
			struct io_op *op    = container_of(evp[i], struct io_op, ev);
			int           op_rc = complete_read_op(hdl, op);

			if (rc == 0 && op_rc != 0) {
				rc = op_rc;
			}

			if (op_rc) {
				D_ERROR("ERROR in fetching the results: %s (rc=%d)",
					strerror(op_rc), op_rc);
			}
		}
		completed += eq_rc;

	} while (completed < nr);

	D_FREE(ops);

	return PyLong_FromLong(rc);
}

/**
 * Python shim module
 */
#define EXPORT_PYTHON_METHOD(name)                                                                 \
	{                                                                                          \
		#name, __shim_handle__##name, METH_VARARGS | METH_KEYWORDS, "text"                 \
	}

static PyMethodDef torchMethods[] = {
    /** Torch operations */
    EXPORT_PYTHON_METHOD(torch_connect),
    EXPORT_PYTHON_METHOD(torch_disconnect),
    EXPORT_PYTHON_METHOD(torch_worker_init),
    EXPORT_PYTHON_METHOD(torch_read),
    EXPORT_PYTHON_METHOD(torch_batch_read),
    EXPORT_PYTHON_METHOD(torch_recommended_dir_split),
    EXPORT_PYTHON_METHOD(torch_list_with_anchor),

    EXPORT_PYTHON_METHOD(module_init),
    EXPORT_PYTHON_METHOD(module_fini),

    {NULL, NULL},
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
    PyModuleDef_HEAD_INIT, "torch_shim",     NULL, sizeof(struct module_struct), torchMethods, NULL,
    __daosbase_traverse,   __daosbase_clear, NULL,
};

PyMODINIT_FUNC
PyInit_torch_shim(void)
{
	PyObject *module;

	module = PyModule_Create(&moduledef);

#define DEFINE_PY_RETURN_CODE(name, errstr) PyModule_AddIntConstant(module, "" #name, name);

	/** export return codes */
	D_FOREACH_GURT_ERR(DEFINE_PY_RETURN_CODE);
	D_FOREACH_DAOS_ERR(DEFINE_PY_RETURN_CODE);
	PyModule_AddIntConstant(module, "DER_SUCCESS", DER_SUCCESS);
	PyModule_AddIntConstant(module, "DER_UNKNOWN", DER_UNKNOWN);

	return module;
}
