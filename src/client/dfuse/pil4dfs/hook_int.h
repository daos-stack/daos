/**
 * (C) Copyright 2018-2021 Lei Huang.
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __HOOK_INT
#define __HOOK_INT

/* The max number of shared objects we intercept. */
#define MAX_MODULE             (16)

/* The max number of hooks we intercept with trampoline technique. */
#define MAX_PATCH              (48)

/* The max length of shared objects' path */
#define MAX_LEN_PATH_NAME      (512)

/* The max length of names of the functions to intercept */
#define MAX_LEN_FUNC_NAME      (128)

/* The minimal memory size we need to allocate to hold trampolines. */
#define MIN_MEM_SIZE           (0x1000)

/**
 * The max range of a signed integer can represent is 0x7FFFFFFF. We decrease it
 * a little bit to be safer.
 */
#define NULL_RIP_VAR_OFFSET    (0x7FF00000)

/**
 * The length of instructions to jmp to a new function is 14 bytes.
 * ff 25 00 00 00 00       jmp    QWORD PTR [rip+0x0]     6 bytes
 * 8 bytes for the address of new function.
 */
#define BOUNCE_CODE_LEN        (14)

/**
 * The relative offset of new function address in bouncing code. It is after
 * jmp    QWORD PTR [rip+0x0]
 */
#define OFFSET_NEW_FUNC_ADDR   (6)

/* The max number of bytes to disassemble for the entry code of original functions */
#define MAX_LEN_TO_DISASSEMBLE (24)

/* The max length of an instruction */
#define MAX_INSN_LEN           (15)

/* The length of jmp instruction we use. */
#define JMP_INSTRCTION_LEN     (5)

/**
 * The max length of bytes to hold the instruments to call original function.
 * 1) saved instruction
 * 2) jump to resuming address
 */
#define MAX_TRAMPOLINE_LEN     ((MAX_INSN_LEN) + (JMP_INSTRCTION_LEN))

struct module_patch_info_t {
	char			*module_name;
	unsigned long int	module_base_addr;
	char			func_name_list[MAX_PATCH][MAX_LEN_FUNC_NAME];
	int			is_patch_disabled[MAX_PATCH];
	void			*old_func_addr_list[MAX_PATCH];
	void			*old_func_addr_min, *old_func_addr_max;
	long int		*ptr_old_func_add_list[MAX_PATCH];
	long int		old_func_len_list[MAX_PATCH];
	void			*new_func_addr_list[MAX_PATCH];
	int			num_hook;
	int			idx_patch_blk; /* which patch memory block */
};

struct trampoline_t {
	/* save the original function entry code and jump instrument */
	unsigned char	trampoline[MAX_TRAMPOLINE_LEN];
	/* the code can jmp to my hook function. +3 for padding */
	unsigned char	bounce[BOUNCE_CODE_LEN + 2];
	/* to save 5 bytes of the entry instruction of original function */
	char		org_code[12];
	/* the address of original function */
	void		*addr_org_func;
	/* the number of bytes copied of the entry instructions of the original function. */
	/* Needed when removing hooks. */
	int		saved_code_len;
	/* the offset of rip addressed variable. Relative address has to be corrected */
	/* when copied into trampoline from original address.                         */
	int		offset_rIP_var;
};

struct patch_block_t {
	void	*patch_addr;
	void	*patch_addr_end;
	int	num_trampoline;
};

/**
 * Queries the addresses of all original functions to hook.
 */
static void
query_all_org_func_addr(void);

/**
 * Queries the index of a given library name in registered libs.
 * \param[in]	module_name	The name of shared library. Both short name ("ld") and
 *				full name ("ld-2.17.so") are accepted.
 *
 * \return			The index in registered libs.
 *				-1	not found.
 *				>=0	valid
 */
static int
query_registered_module(const char *module_name);

/**
 * Determine we need to change the permission of one or two pages
 * for a given address.
 *
 * \param[in]	addr		The address of the entry of original function.
 *				We will change it to a jmp instruction.
 *
 * \return			The number of bytes we need to change permission with mprotect().
 */
static size_t
determine_mem_block_size(const void *addr);

/**
 * Determine the full paths of libraries, ld.so, libc.so, and libpthread.so, etc.
 */
static void
determine_lib_path(void);

/**
 * Read "/proc/%pid/maps" and extract the names of modules.
 */
static void
get_module_maps(void);

/**
 * Allocated memory blocks to hold the patches for hook.
 */
static void
allocate_memory_block_for_patches(void);

/**
 * Query the index of the name of a library in lib_name_list[].
 *
 * \param[in]	lib_name_str	The library name
 *
 * \return			The index in lib_name_list[].
 *				-1	Not found in the list
 */
static int
query_lib_name_in_list(const char *lib_name_str);

#endif
