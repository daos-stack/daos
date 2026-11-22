/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of GURT.
 */
#include <stdio.h>
#include <daos_errno.h>
#include <gurt/list.h>
#include <gurt/common.h>

static D_LIST_HEAD(g_error_reg_list);

struct d_error_reg {
	d_list_t           er_link;
	int                er_base;
	int                er_limit;
	const char *const *er_strings;
	const char *const *er_strerror;
};

#define D_DEFINE_COMP_ERRSTR(name, base)                                                           \
	static const char *const g_##name##_errstr[] = {D_FOREACH_##name##_ERR(D_DEFINE_ERRSTR)};  \
	static const char *const g_##name##_errstr_desc[] = {                                      \
	    D_FOREACH_##name##_ERR(D_DEFINE_ERRDESC)};                                             \
	static struct d_error_reg g_##name##_errreg = {                                            \
	    .er_base     = DER_ERR_##name##_BASE,                                                  \
	    .er_limit    = ARRAY_SIZE(g_##name##_errstr) + DER_ERR_##name##_BASE,                  \
	    .er_strings  = g_##name##_errstr,                                                      \
	    .er_strerror = g_##name##_errstr_desc,                                                 \
	};

D_FOREACH_ERR_RANGE(D_DEFINE_COMP_ERRSTR)

#define D_ADD_LIST(name, base) d_list_add_tail(&g_##name##_errreg.er_link, &g_error_reg_list);

#define D_ERR_BUF_SIZE         32

static __attribute__((constructor)) void
d_err_init(void)
{
	D_FOREACH_ERR_RANGE(D_ADD_LIST)
}

const char *
d_errstr(int errnum)
{
	struct d_error_reg *entry;

	if (errnum == 0)
		return "DER_SUCCESS";

	if (errnum > 0)
		goto out;

	errnum = -errnum;

	d_list_for_each_entry(entry, &g_error_reg_list, er_link) {
		if (errnum <= entry->er_base || errnum > entry->er_limit)
			continue;
		return entry->er_strings[errnum - entry->er_base - 1];
	}

out:
	return "DER_UNKNOWN";
}

const char *
d_errdesc(int errnum)
{
	struct d_error_reg *entry;
	static char         buf[D_ERR_BUF_SIZE];

	if (errnum == DER_SUCCESS)
		return "Success";

	if (errnum == -DER_UNKNOWN)
		return "Unknown error";

	snprintf(buf, D_ERR_BUF_SIZE, "Unknown error code %d", errnum);

	if (errnum > 0)
		goto out;

	errnum = -errnum;

	d_list_for_each_entry(entry, &g_error_reg_list, er_link) {
		if (errnum <= entry->er_base || errnum > entry->er_limit)
			continue;
		return entry->er_strerror[errnum - entry->er_base - 1];
	}

out:
	return buf;
}

int
d_der2errno(int err)
{
	if (err > 0)
		return EINVAL;

	switch (err) {
	case -DER_SUCCESS:
		return 0;
	case -DER_NO_PERM:
	case -DER_EP_RO:
	case -DER_EP_OLD:
	case -DER_CONT_DESTROYING:
		return EPERM;
	case -DER_ENOENT:
	case -DER_NONEXIST:
		return ENOENT;
	case -DER_INVAL:
	case -DER_NOTYPE:
	case -DER_NOSCHEMA:
	case -DER_NOLOCAL:
	case -DER_IO_INVAL:
		return EINVAL;
	case -DER_NO_HDL:
		return EBADF;
	case -DER_KEY2BIG:
	case -DER_REC2BIG:
		return E2BIG;
	case -DER_EXIST:
		return EEXIST;
	case -DER_UNREACH:
		return EHOSTUNREACH;
	case -DER_NOSPACE:
		return ENOSPC;
	case -DER_ALREADY:
		return EALREADY;
	case -DER_DOS:
	case -DER_NOMEM:
		return ENOMEM;
	case -DER_TIMEDOUT:
		return ETIMEDOUT;
	case -DER_BUSY:
	case -DER_EQ_BUSY:
		return EBUSY;
	case -DER_AGAIN:
		return EAGAIN;
	case -DER_PROTO:
		return EPROTO;
	case -DER_IO:
		return EIO;
	case -DER_CANCELED:
		return ECANCELED;
	case -DER_OVERFLOW:
		return EOVERFLOW;
	case -DER_BADPATH:
	case -DER_NOTDIR:
		return ENOTDIR;
	case -DER_STALE:
		return ESTALE;
	case -DER_TX_RESTART:
		return ERESTART;
	case -DER_INPROGRESS:
		return EINPROGRESS;
	case -DER_NOTSUPPORTED:
		return ENOTSUP;
	case -DER_NOSYS:
		return ENOSYS;
	default:
		return EIO;
	}
}
