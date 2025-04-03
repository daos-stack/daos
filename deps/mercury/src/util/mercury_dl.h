/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_DL_H
#define MERCURY_DL_H

#include "mercury_util_config.h"

#if defined(_WIN32)
#    define _WINSOCKAPI_
#    include <windows.h>
#    define HG_DL_HANDLE HMODULE
#else
#    include <dlfcn.h>
#    define HG_DL_HANDLE void *
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the last error.
 *
 * \return Most recent error or NULL if no error
 */
static HG_UTIL_INLINE const char *
hg_dl_error(void);

/**
 * Open a shared library object referenced by \file.
 *
 * \param file [IN]             library name
 *
 * \return Shared object handle on success or NULL on failure
 */
static HG_UTIL_INLINE HG_DL_HANDLE
hg_dl_open(const char *file);

/**
 * Close the shared library object.
 *
 * \param handle [IN]           shared object handle
 *
 * \return Non-negative on success or negative on failure
 */
static HG_UTIL_INLINE int
hg_dl_close(HG_DL_HANDLE handle);

/**
 * Obtain address of a symbol in a shared library object.
 *
 * \param handle [IN]           shared object handle
 * \param name [IN]             symbol name
 *
 * \return Address of the symbol on success or NULL on failure
 */
static HG_UTIL_INLINE void *
hg_dl_sym(HG_DL_HANDLE handle, const char *name);

/*---------------------------------------------------------------------------*/
static HG_UTIL_INLINE const char *
hg_dl_error(void)
{
#ifdef _WIN32
    return "no last error known";
#else
    return dlerror();
#endif
}

/*---------------------------------------------------------------------------*/
static HG_UTIL_INLINE HG_DL_HANDLE
hg_dl_open(const char *file)
{
#ifdef _WIN32
    return LoadLibraryEx(file, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(file, RTLD_LAZY);
#endif
}

/*---------------------------------------------------------------------------*/
static HG_UTIL_INLINE int
hg_dl_close(HG_DL_HANDLE handle)
{
#ifdef _WIN32
    return !(FreeLibrary(handle));
#else
    return dlclose(handle);
#endif
}

/*---------------------------------------------------------------------------*/
static HG_UTIL_INLINE void *
hg_dl_sym(HG_DL_HANDLE handle, const char *name)
{
#ifdef _WIN32
    return (void *) GetProcAddress(handle, name);
#else
    return dlsym(handle, name);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_DL_H */
