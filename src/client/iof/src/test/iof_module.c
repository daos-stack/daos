/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*** Python/C Shim used for testing ***/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#ifdef IOF_PYTHON_35
#include <python3.5m/Python.h>
#else
#include <python3.4m/Python.h>
#endif
#include <dirent.h>

#include <sys/stat.h>
#include <sys/types.h>

#define BUF_SIZE 4096

struct module_state {
	PyObject	*error;
	int		open_fd;
};

/* Test low-level POSIX APIs */
static PyObject *open_test_file(PyObject *self, PyObject *args)
{
	struct module_state	*st = PyModule_GetState(self);
	const char		*mount_dir;
	char			*template = NULL;
	int fd;
	int rc;

	if (!PyArg_ParseTuple(args, "s", &mount_dir))
		return NULL;

	rc = asprintf(&template, "%s/posix_test_file_XXXXXX", mount_dir);
	if (rc == -1 || !template) {
		PyErr_SetString(st->error, "Unable to open file");
		return NULL;
	}

	errno = 0;
	fd = mkstemp(template);
	if (fd == -1) {
		printf("mkstemp = %s\n", strerror(errno));
		free(template);
		PyErr_SetString(st->error, "Unable to open file");
		return NULL;
	}

	printf("\nOpened %s, fd = %d\n", template, fd);
	free(template);
	st->open_fd = fd;
	return PyLong_FromLong(fd);
}

static PyObject *test_write_file(PyObject *self, PyObject *args)
{
	struct module_state *st = PyModule_GetState(self);
	int	fd;
	char	write_buf[BUF_SIZE];
	ssize_t	bytes;
	size_t	len;

	if (!PyArg_ParseTuple(args, "i", &fd))
		return NULL;

	if (st->open_fd == 0 || st->open_fd != fd) {
		PyErr_SetString(st->error, "Invalid fd");
		return NULL;
	}

	snprintf(write_buf, BUF_SIZE, "Writing to a test file\n");
	len = strlen(write_buf);

	printf("Writing: '%s' to fd = %d\n", write_buf, fd);
	errno = 0;
	bytes = write(fd, write_buf, len);

	if (bytes != len) {
		printf("Wrote %zd bytes, expected %zu\n", bytes, len);
		Py_RETURN_NONE;
	}

	if (errno == 0) {
		printf("Wrote %zd bytes, expected %zu %d %s\n", bytes, len,
		       errno, strerror(errno));
		return PyLong_FromLong(fd);
	}

	printf("Write file errno = %s\n", strerror(errno));
	Py_RETURN_NONE;
}

static PyObject *test_read_file(PyObject *self, PyObject *args)
{
	struct module_state *st = PyModule_GetState(self);
	int	fd;
	char	read_buf[BUF_SIZE] = {0};
	ssize_t	bytes;

	if (!PyArg_ParseTuple(args, "i", &fd))
		return NULL;

	if (st->open_fd == 0 || st->open_fd != fd) {
		PyErr_SetString(st->error, "Invalid fd");
		return NULL;
	}

	if (!test_write_file(self, args))
		Py_RETURN_NONE;

	if (lseek(fd, 0, SEEK_SET) < 0) {
		printf("lseek error %s after read\n", strerror(errno));
		Py_RETURN_NONE;
	}

	printf("Reading from fd = %d\n", fd);
	errno = 0;
	bytes = read(fd, read_buf, BUF_SIZE - 1);

	if (errno == 0) {
		printf("Read %zd bytes\n", bytes);
		printf("Read: '%s'\n", read_buf);
		return PyLong_FromLong(fd);
	}

	printf("Read file errno = %s\n", strerror(errno));
	Py_RETURN_NONE;
}

static PyObject *close_test_file(PyObject *self, PyObject *args)
{
	struct module_state *st = PyModule_GetState(self);
	int fd, rc;

	if (!PyArg_ParseTuple(args, "i", &fd))
		return NULL;

	if (st->open_fd == 0 || st->open_fd != fd) {
		PyErr_SetString(st->error, "Invalid fd");
		return NULL;
	}

	errno = 0;
	rc = close(fd);

	if (rc == 0) {
		st->open_fd = 0;
		printf("Closed fd = %d\n", fd);
		Py_RETURN_NONE;
	}

	printf("Close file errno = %s\n", strerror(errno));

	PyErr_SetString(st->error, "Unable to close file");
	return NULL;
}

static PyObject *test_unlink(PyObject *self, PyObject *args)
{
	struct module_state	*st = PyModule_GetState(self);
	char			*filename = NULL;
	const char		*path;
	int rc;
	int err;

	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;

	rc = asprintf(&filename, "%s/no_file", path);
	if (rc == -1 || !filename) {
		PyErr_SetString(st->error, "Unable to create filename");
		return NULL;
	}

	errno = 0;
	rc = unlink(filename);
	err = errno;
	free(filename);

	printf("unlink returned %d errno = %s\n", rc, strerror(err));

	if (rc != -1 || err != ENOENT) {
		PyErr_SetString(st->error, "Incorrect return values");
		return NULL;
	}

	Py_RETURN_NONE;
}

static PyObject *test_dir_mode(PyObject *self, PyObject *args)
{
	struct module_state	*st = PyModule_GetState(self);
	char			*filename = NULL;
	const char		*path;
	int rc;
	int err;

	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;

	rc = asprintf(&filename, "%s/t_dir", path);
	if (rc == -1 || !filename) {
		PyErr_SetString(st->error, "Unable to create filename");
		return NULL;
	}

	errno = 0;
	rc = mkdir(filename, 0100);
	err = errno;

	printf("mkdir returned %d errno = %s\n", rc, strerror(err));

	if (rc != 0) {
		PyErr_SetString(st->error, "Incorrect mkdir return values");
		return NULL;
	}

	errno = 0;
	rc = chmod(filename, 0500);
	err = errno;
	printf("chmod returned %d errno = %s\n", rc, strerror(err));

	if (rc != 0) {
		PyErr_SetString(st->error, "Incorrect chmod return values");
		return NULL;
	}

	errno = 0;
	rc = rmdir(filename);
	err = errno;

	printf("rmdir returned %d errno = %s\n", rc, strerror(err));

	if (rc != 0) {
		PyErr_SetString(st->error, "Incorrect rmdir return values");
		return NULL;
	}

	free(filename);
	Py_RETURN_NONE;
}

/* Directory handle testing functions.
 *
 * Python does not appear to have complete calls for accessing directories but
 * replies on os.listdir() which does not hold open a handle, so implement core
 * functionality here, which is then driven from iof_test_local.py.
 *
 * Allow the opening, reading and closing of a single directory handle, instead
 * of creating an object, or returning a pointer to python simply use a global
 * variable here and test for NULL before use.
 *
 */

static DIR *dirp;

/* Open a directory by name and save the handle, return None on success or an
 * error number on failure
 */
static PyObject *do_opendir(PyObject *self, PyObject *args)
{
	const char *path;
	int rc;

	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;

	errno = 0;
	dirp = opendir(path);
	rc = errno;
	if (!dirp)
		return Py_BuildValue("i", rc);

	Py_RETURN_NONE;
}

/* Read a single filename from the open directory handle, return either a string
 * on success, None on if no remaining file or an error number on failure.
 */
static PyObject *do_readdir(PyObject *self, PyObject *args)
{
	struct dirent *entry;
	int rc;

	if (!PyArg_ParseTuple(args, ""))
		return NULL;

	if (!dirp)
		return Py_BuildValue("i", EINVAL);

	errno = 0;
	entry = readdir(dirp);
	if (!entry) {
		rc = errno;
		if (rc == 0)
			Py_RETURN_NONE;
		else
			return Py_BuildValue("i", rc);
	}
	return Py_BuildValue("s", entry->d_name);
}

/* Rewind the open directory handle.  Return None on success or errno on
 * failure, but note that rewinddir() call itself does not return a error
 * code
 */
static PyObject *do_rewinddir(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;

	if (!dirp)
		return Py_BuildValue("i", EINVAL);

	rewinddir(dirp);

	Py_RETURN_NONE;
}

/* Close the open directory handle.
 *
 * Return None on success or an errno on failure.
 */
static PyObject *do_closedir(PyObject *self, PyObject *args)
{
	int rc;

	if (!PyArg_ParseTuple(args, ""))
		return NULL;

	if (!dirp)
		return Py_BuildValue("i", EINVAL);

	errno = 0;
	rc = closedir(dirp);
	if (rc == -1) {
		rc = errno;
		return Py_BuildValue("i", rc);
	}
	dirp = NULL;

	Py_RETURN_NONE;
}

static PyMethodDef iofMethods[] = {
	{ "opendir", do_opendir, METH_VARARGS, "Open a directory"},
	{ "readdir", do_readdir, METH_VARARGS, "Read a filename from a directory"},
	{ "rewinddir", do_rewinddir, METH_VARARGS, "Rewind a directory"},
	{ "closedir", do_closedir, METH_VARARGS, "Close a directory"},
	{ "open_test_file", open_test_file, METH_VARARGS, NULL },
	{ "test_write_file", test_write_file, METH_VARARGS, NULL },
	{ "test_read_file", test_read_file, METH_VARARGS, NULL },
	{ "close_test_file", close_test_file, METH_VARARGS, NULL },
	{ "test_unlink", test_unlink, METH_VARARGS, NULL},
	{ "test_dir_mode", test_dir_mode, METH_VARARGS, NULL},
	{ NULL, NULL, 0, NULL},
};

static int iofmod_traverse(PyObject *m, visitproc visit, void *arg)
{
	struct module_state *st = PyModule_GetState(m);

	Py_VISIT(st->error);
	return 0;
}

static int iofmod_clear(PyObject *m)
{
	struct module_state *st = PyModule_GetState(m);

	Py_CLEAR(st->error);
	return 0;
}

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"iofmod",
	NULL,
	sizeof(struct module_state),
	iofMethods,
	NULL,
	iofmod_traverse,
	iofmod_clear,
	NULL
};

PyMODINIT_FUNC PyInit_iofmod(void)
{
	PyObject *module;
	struct module_state *st;

	module = PyModule_Create(&moduledef);

	st = PyModule_GetState(module);
	st->error = PyErr_NewException("iofmod.failure", NULL, NULL);
	if (!st->error) {
		Py_DECREF(module);
			return NULL;
	}
	Py_INCREF(st->error);

	return module;
}
