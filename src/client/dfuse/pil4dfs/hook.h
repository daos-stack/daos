/**
 * (C) Copyright 2018-2021 Lei Huang.
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __HOOK
#define __HOOK

enum ERROR_CODE_REGISTER_HOOK {
	REGISTER_SUCCESS,
	REGISTER_NOT_FOUND_LIBC,
	REGISTER_DLOPEN_FAILED,
	REGISTER_MODULE_NAME_TOO_LONG,
	REGISTER_FUNC_NAME_TOO_LONG,
	REGISTER_TOO_MANY_HOOKS
};

/**
 * Add one target function into the list of the functions to intercept.
 *
 * \param[in]	module_name	The name of shared library. Both short name ("ld") and
 *				full name ("ld-2.17.so") are accepted.
 * \param[in]	func_Name	The function name string
 * \param[in]	new_func_addr	The address of our new implementation.
 * \param[out]	ptr_org_func	*ptr_org_func will hold the address of original function
 *				implemented in lib module_name.
 *
 * \return			0		success
 *				otherwise	fail.
 */
int
register_a_hook(const char *module_name, const char *func_Name, const void *new_func_addr,
		const long int *ptr_org_func);

/**
 * Install hooks by setting up trampolines for all functions registered.
 *
 * return			The number of hooks actually installed.
 */
int
install_hook(void);

/**
 * uninstall_hook - Uninstall hooks by cleaning up trampolines.
 */
void
uninstall_hook(void);

/**
 * free_memory_in_hook - Free memory dynamically allocated.
 */
void
free_memory_in_hook(void);

/**
 * return the full path of libpil4dfs.so.
 */
char *
query_pil4dfs_path(void);

/**
 * return glibc version in current process
 */
float
query_libc_version(void);

#endif
