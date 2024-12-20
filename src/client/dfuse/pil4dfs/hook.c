/**
 * (C) Copyright 2018-2021 Lei Huang.
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * This is a mini framework to intercept the functions in shared libraries under Linux.
 * It only works on x86_64 at this time. It will be extend it to support ARM64 in future.
 * libcapstone was adopted to disasseble binary code on x86_64.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <execinfo.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <elf.h>
#include <termios.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <link.h>
#include <fcntl.h>
#include <execinfo.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <capstone/capstone.h>
#include <gurt/common.h>
#include <gnu/libc-version.h>

#include "hook.h"
#include "hook_int.h"

#define MAX_LEN_DISASSEMBLE (28)

static int                        num_hook;
static int                        num_module;
static int                        num_patch_blk, is_uninstalled;
static int                        get_module_maps_inited;

static size_t                     page_size, mask;

static struct module_patch_info_t *module_list;

/* The flag whethere libc.so is found or not. */
static int                        found_libc = 1;

/* The instruction to jump to new function.
 * 00:  ff 25 00 00 00 00       jmp    QWORD PTR [rip+0x0]        # 6 <_main+0x6>
 * The long int +6 (0x1234567812345678) needs be replaced by the address of new function.
 */
static unsigned char              instruction_bounce[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00, 0x78,
							  0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};

/* The list of memory blocks allocated to hold patches for hook */
static struct patch_block_t       patch_blk_list[MAX_MODULE];

/* start to compile list of memory blocks in /proc/pid/maps */

/* The max number of libraries loaded                       */
#define MAX_NUM_LIB (256)

/* The max number of segments in /proc/pid/maps */
#define MAX_NUM_SEG (2048)

static int      num_seg, num_lib_in_map;

/* List of min and max addresses of segments in /proc/pid/maps */
static uint64_t addr_min[MAX_NUM_SEG], addr_max[MAX_NUM_SEG];

/* List of base addresses of loaded libraries */
static uint64_t lib_base_addr[MAX_NUM_LIB];

/* List of names of loaded libraries */
static char   **lib_name_list;

/* libc version number in current process. e.g., 2.28 */
static float    libc_version;
static char    *libc_version_str;

/* end   to compile list of memory blocks in /proc/pid/maps */

static char    *path_ld;
static char    *path_libc;
static char    *path_libdl;
static char    *path_libpthread;
/* This holds the path of libpil4dfs.so. It is needed when we want to
 * force child processes append libpil4dfs.so to env LD_PRELOAD. */
static char     path_libpil4dfs[PATH_MAX];

#define MAX_MAP_SIZE	(512*1024)
#define MAP_SIZE_LIMIT	(16*1024*1024)

static void
quit_hook_init(void)
{
	/* print to stdout instead of stderr to avoid fault injection errors */
	printf("pil4dfs failed to initialize, aborting.\n");
	exit(1);
}

/*
 * get_path_pos - Determine the start and end positions of a path in /proc/self/maps.
 */
static void
get_path_pos(char *buf, char **start, char **end, int path_offset, char *buf_min, char *buf_max)
{
	int i;

	*start = NULL;
	*end = NULL;

	/* look backward for a '\n', the end of last line */
	for (i = 0; i < PATH_MAX + path_offset; i++) {
		if ((buf - i) < buf_min) {
			break;
		}
		if (*(buf - i) == '\n') {
			/* the beginning of lib path */
			*start = buf - i + path_offset;
			break;
		}
	}

	/* look forward for a '\n', the end of current line */
	for (i = 0; i < PATH_MAX; i++) {
		if ((buf + i) > buf_max) {
			break;
		}
		if (*(buf + i) == '\n') {
			/* the beginning of lib path */
			*end = buf + i;
			break;
		}
	}
}

/*
 * read_map_file - Read the whole file /proc/self/maps. Adaptively allocate memory when needed.
 */
static int
read_map_file(char **buf)
{
	bool buffer_full;
	int max_read_size, complete = 0, read_size;
	FILE *fIn;

	max_read_size = MAX_MAP_SIZE;

	/* There is NO way to know the size of /proc/self/maps without reading the full file */
	/* Keep reading the file until finish the whole file. Increase the buffer size if needed. */
	while (complete == 0) {
		buffer_full = false;
		D_ALLOC(*buf, max_read_size + 1);
		if (*buf == NULL)
			quit_hook_init();

		/* non-seekable file. fread is needed!!! */
		fIn = fopen("/proc/self/maps", "r");
		if (fIn == NULL) {
			DS_ERROR(errno, "Fail to open /proc/self/maps");
			D_FREE(*buf);
			quit_hook_init();
		}

		/* fgets seems not working. */
		/* fread can read complete file. read() does not most of time!!! */
		read_size = fread(*buf, 1, max_read_size, fIn);
		fclose(fIn);

		if (read_size < 0) {
			DS_ERROR(errno, "Error in reading file /proc/self/maps");
			D_FREE(*buf);
			quit_hook_init();
		} else if (read_size == max_read_size) {
			/* need to increase the buffer and try again */
			max_read_size *= 3;
			buffer_full = true;
		} else {
			/* reached the end of the file */
			complete = 1;
		}
		(*buf)[read_size] = '\0';
		if (buffer_full)
			D_FREE(*buf);
		if (max_read_size >= MAP_SIZE_LIMIT) {
			/* not likely to be here */
			DS_ERROR(EFBIG, "/proc/self/maps is TOO large");
			D_FREE(*buf);
			quit_hook_init();
		}
	}

	return read_size;
}

/*
 * determine_lib_path - Determine the full paths of three libraries, ld.so, libc.so
 * and libpthread.so.
 */
static void
determine_lib_path(void)
{
	int   path_offset   = 0, read_size, i;
	char *read_buff_map = NULL;
	char *pos, *start, *end, *lib_dir_str = NULL;
	bool  ver_in_lib_name = false;

	read_size = read_map_file(&read_buff_map);

	/* need to find the offset of lib path in the line */
	pos = strstr(read_buff_map, "[stack]");
	if (pos == NULL) {
		D_ERROR("Failed to find section stack.\n");
		goto err;
	}
	/* look back for the first '\n', the end of last line */
	for(i = 0; i < 128; i++) {
		if (*(pos - i) == '\n') {
			/* path_offset is the offset from the end of previous line to the beginning
			 * of the lib path string in current line
			 */
			path_offset = i;
			break;
		}
		if ((pos - i) < read_buff_map) {
			break;
		}
	}
	if (path_offset == 0) {
		D_ERROR("Fail to determine path_offset in /proc/self/maps.\n");
		quit_hook_init();
	}

	pos = strstr(read_buff_map, "ld-linux");
	if (pos == NULL)
		/* try a different format */
		pos = strstr(read_buff_map, "ld-2.");
	if (pos == NULL) {
		D_ERROR("Failed to find ld.so.\n");
		goto err;
	}
	get_path_pos(pos, &start, &end, path_offset, read_buff_map, read_buff_map + read_size);
	if (start == NULL || end == NULL) {
		D_ERROR("get_path_pos() failed to determine the path for ld.so.\n");
		goto err;
	}
	if ((end - start + 1) >= PATH_MAX) {
		DS_ERROR(ENAMETOOLONG, "path_ld is too long");
		goto err;
	}
	D_STRNDUP(path_ld, start, end - start + 1);
	if (path_ld == NULL)
		goto err;
	path_ld[end - start] = 0;

	pos = strstr(read_buff_map, "libc.so");
	if (pos == NULL)
		/* try a different format */
		pos = strstr(read_buff_map, "libc-2.");
	if (pos == NULL) {
		D_ERROR("Failed to find the path of libc.so.\n");
		goto err;
	}
	get_path_pos(pos, &start, &end, path_offset, read_buff_map, read_buff_map + read_size);
	if (start == NULL || end == NULL) {
		D_ERROR("get_path_pos() failed to determine the path for libc.so.\n");
		goto err;
	}
	if ((end - start + 1) >= PATH_MAX) {
		DS_ERROR(ENAMETOOLONG, "path_libc is too long");
		goto err;
	}
	/* extract the directory where libc.so is located in. */
	D_STRNDUP(lib_dir_str, start, pos - start);
	if (lib_dir_str == NULL)
		goto err;
	lib_dir_str[pos - start - 1] = 0;
	D_STRNDUP(path_libc, start, end - start + 1);
	if (path_libc == NULL)
		goto err;
	path_libc[end - start] = 0;

	if (libc_version_str == NULL) {
		libc_version_str = (char *)gnu_get_libc_version();
		if (libc_version_str == NULL) {
			DS_ERROR(errno, "Failed to determine libc version");
			goto err;
		}
		libc_version = atof(libc_version_str);
	}

	/* check whether libc name contains version. EL9 libs do not have version info! */
	pos = strstr(path_libc, "libc-2.");
	if (pos)
		ver_in_lib_name = true;

	/* with version in name */
	if (ver_in_lib_name)
		D_ASPRINTF(path_libpthread, "%s/libpthread-%s.so", lib_dir_str, libc_version_str);
	else
		D_ASPRINTF(path_libpthread, "%s/libpthread.so.0", lib_dir_str);
	if (path_libpthread == NULL)
		goto err;
	if (strnlen(path_libpthread, PATH_MAX) >= PATH_MAX) {
		D_FREE(path_libpthread);
		DS_ERROR(ENAMETOOLONG, "path_libpthread is too long");
		goto err;
	}
	if (ver_in_lib_name)
		D_ASPRINTF(path_libdl, "%s/libdl-%s.so", lib_dir_str, libc_version_str);
	else
		D_ASPRINTF(path_libdl, "%s/libdl.so.2", lib_dir_str);
	if (path_libdl == NULL)
		goto err;
	D_FREE(lib_dir_str);

	if (strstr(read_buff_map, "libioil.so")) {
		D_FREE(read_buff_map);
		return;
	}

	pos = strstr(read_buff_map, "libpil4dfs.so");
	if (pos == NULL) {
		D_ERROR("Failed to find the path of libpil4dfs.so.\n");
		goto err;
	}
	get_path_pos(pos, &start, &end, path_offset, read_buff_map, read_buff_map + read_size);
	if (start == NULL || end == NULL) {
		D_ERROR("get_path_pos() failed to determine the path for libpil4dfs.so.\n");
		goto err;
	}
	if ((end - start + 1) >= PATH_MAX) {
		DS_ERROR(ENAMETOOLONG, "path_libpil4dfs is too long");
		goto err;
	}
	memcpy(path_libpil4dfs, start, pos - start + sizeof("libpil4dfs.so"));
	path_libpil4dfs[pos - start - 1 + sizeof("libpil4dfs.so")] = 0;
	D_FREE(read_buff_map);

	return;

err:
	D_FREE(read_buff_map);
	D_FREE(lib_dir_str);
	found_libc = 0;
	quit_hook_init();
}

char *
query_pil4dfs_path(void)
{
	return path_libpil4dfs;
}

float
query_libc_version(void)
{
	return libc_version;
}

/*
 * query_func_addr - Determine the addresses and code sizes of functions in func_name_list[].
 *   @lib_path: The full path of the shared object file
 *   @func_name_list: A list of function names in this lib to be intercepted
 *   @func_addr_list: A list to hold the addresses of functions
 *   @func_len_list:  A list of hold the size (number of bytes) of functions
 *   @img_base_addr:  The base address of this loaded module
 *   @num_func:       The number of functions in the list of func_name_list[]
 * Returns:
 *   void
 */
static void
query_func_addr(const char lib_path[], const char func_name_list[][MAX_LEN_FUNC_NAME],
		void *func_addr_list[], long int func_len_list[], const long int img_base_addr,
		const int num_func)
{
	int         fd, i, j, k, rc;
	struct stat file_stat;
	void       *map_start;
	Elf64_Ehdr *header;
	Elf64_Shdr *sections;
	int         strtab_offset  = 0;
	void       *symb_base_addr = NULL;
	int         num_sym = 0, sym_rec_size = 0, sym_offset, rec_addr;
	char       *sym_name;

	rc = stat(lib_path, &file_stat);
	if (rc == -1) {
		DS_ERROR(errno, "Fail to query stat of file %s", lib_path);
		quit_hook_init();
	}

	fd = open(lib_path, O_RDONLY);
	if (fd == -1) {
		DS_ERROR(errno, "Fail to open file %s", lib_path);
		quit_hook_init();
	}

	map_start = mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if ((long int)map_start == -1) {
		close(fd);
		DS_ERROR(errno, "Fail to mmap file %s", lib_path);
		quit_hook_init();
	}
	header = (Elf64_Ehdr *)map_start;

	sections = (Elf64_Shdr *)((char *)map_start + header->e_shoff);

	for (i = 0; i < header->e_shnum; i++) {
		if ((sections[i].sh_type == SHT_DYNSYM) || (sections[i].sh_type == SHT_SYMTAB)) {
			symb_base_addr = (void *)(sections[i].sh_offset + map_start);
			sym_rec_size   = sections[i].sh_entsize;
			if (sections[i].sh_entsize == 0) {
				munmap(map_start, file_stat.st_size);
				close(fd);
				D_ERROR("Unexpected entry size in ELF file.\n");
				quit_hook_init();
			}
			num_sym        = sections[i].sh_size / sections[i].sh_entsize;

			/* tricky here!!! */
			for (j = i - 1; j < i + 2; j++) {
				if (sections[j].sh_type == SHT_STRTAB)
					strtab_offset = (int)(sections[j].sh_offset);
			}

			/* Hash table would be more efficient. */
			for (j = 0; j < num_sym; j++) {
				rec_addr   = sym_rec_size * j;
				sym_offset = *((int *)(symb_base_addr + rec_addr)) & 0xFFFFFFFF;
				sym_name   = (char *)(map_start + strtab_offset + sym_offset);
				for (k = 0; k < num_func; k++) {
					if (strcmp(sym_name, func_name_list[k]) == 0) {
						func_addr_list[k] =
						    (void *)(((long int)(*((int *)(symb_base_addr +
										   rec_addr + 8))) &
							      0xFFFFFFFF) +
							     img_base_addr);
						func_len_list[k] =
						    (*((int *)(symb_base_addr + rec_addr + 16))) &
						    0xFFFFFFFF;
					}
				}
			}
		}
	}
	munmap(map_start, file_stat.st_size);
	close(fd);
}

/*
 * uninstall_hook - Uninstall hooks by cleaning up trampolines.
 * Returns:
 *   void
 */
void
uninstall_hook(void)
{
	int                  i, iBlk, iFunc;
	void                *pbaseOrg;
	size_t               MemSize_Modify;
	struct trampoline_t *tramp_list;

	if (found_libc == 0 || is_uninstalled == 1)
		return;

	for (iBlk = 0; iBlk < num_patch_blk; iBlk++) {
		tramp_list = (struct trampoline_t *)(patch_blk_list[iBlk].patch_addr);

		for (iFunc = 0; iFunc < patch_blk_list[iBlk].num_trampoline; iFunc++) {
			/* fast mod */
			pbaseOrg = (void *)((long int)(tramp_list[iFunc].addr_org_func) & mask);
			MemSize_Modify = determine_mem_block_size(
			    (const void *)(tramp_list[iFunc].addr_org_func));

			if (pbaseOrg == NULL)
				continue;
			if (mprotect(pbaseOrg, MemSize_Modify,
				     PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
				DS_WARN(errno, "mprotect() failed");
			/* save original code for uninstall */
			memcpy(tramp_list[iFunc].addr_org_func, tramp_list[iFunc].org_code, 5);
			if (mprotect(pbaseOrg, MemSize_Modify, PROT_READ | PROT_EXEC) != 0)
				DS_WARN(errno, "mprotect() failed");
		}
	}

	for (i = 0; i < num_patch_blk; i++) {
		if (patch_blk_list[i].patch_addr) {
			if (munmap(patch_blk_list[i].patch_addr, MIN_MEM_SIZE))
				DS_WARN(errno, "munmap() failed");
			patch_blk_list[i].patch_addr = 0;
		}
	}

	is_uninstalled = 1;
}

/*
 * query_lib_name_in_list - Query the index of the name of a library in lib_name_list[].
 *   @lib_name_str: The library name
 * Returns:
 *   The index in lib_name_list[]. (-1) means not found in the list.
 */
static int
query_lib_name_in_list(const char *lib_name_str)
{
	int i;

	/* Try exact match first */
	for (i = 0; i < num_lib_in_map; i++) {
		if (strcmp(lib_name_str, lib_name_list[i]) == 0)
			return i;
	}
	/* Try partial match */
	for (i = 0; i < num_lib_in_map; i++) {
		if (strstr(lib_name_list[i], lib_name_str))
			return i;
	}
	return (-1);
}

/*
 * query_registered_module - Query the index of the name of a library in module_list[].
 *   @lib_name_str: The library name
 * Returns:
 *   The index in module_list[]. (-1) means not found in the list.
 */
static int
query_registered_module(const char *lib_name_str)
{
	int i;

	for (i = 0; i < num_module; i++) {
		if (strcmp(lib_name_str, module_list[i].module_name) == 0)
			return i;
	}
	return (-1);
}

/*
 * get_position_of_next_line - Determine the offset of the next new line in a string buffer.
 *   @buff: The string buffer
 *   @pos_start: The starting offset to search
 *   @max_buff_size: The max length of buff[]
 * Returns:
 *   The offset of the next new line. (-1) means reaching the end of buffer.
 */
static int
get_position_of_next_line(const char buff[], const int pos_start, const int max_buff_size)
{
	int i = pos_start;

	while (i < max_buff_size) {
		/* A new line */
		if (buff[i] == 0xA) {
			i++;
			return ((i >= max_buff_size) ? (-1) : (i));
		}
		i++;
	}
	return (-1);
}

/*
 * get_module_maps - Read "/proc/self/maps" and extract the names of modules.
 */
static void
get_module_maps(void)
{
	char    *buf = NULL, *lib_name;
	int      iPos, iPos_Save, ReadItem, read_size;
	uint64_t addr_B, addr_E;

	read_size = read_map_file(&buf);

	num_seg          = 0;

	D_ALLOC(lib_name, PATH_MAX);
	if (lib_name == NULL) {
		D_FREE(buf);
		quit_hook_init();
	}
	/* start from the beginging */
	iPos = 0;
	while (iPos >= 0) {
		ReadItem = sscanf(buf + iPos, "%lx-%lx", &addr_B, &addr_E);
		if (ReadItem == 2) {
			addr_min[num_seg] = addr_B;
			addr_max[num_seg] = addr_E;
			if (num_seg >= 1) {
				/* merge contacted blocks */
				if (addr_min[num_seg] == addr_max[num_seg - 1]) {
					addr_max[num_seg - 1] = addr_max[num_seg];
					num_seg--;
				}
			}
			num_seg++;
		}
		iPos_Save = iPos;
		/* find the next line */
		iPos = get_position_of_next_line(buf, iPos + 38, read_size);
		if ((iPos - iPos_Save) > 73) {
			/* with a lib name */
			ReadItem = sscanf(buf + iPos_Save + 73, "%s", lib_name);
			if (ReadItem == 1) {
				if (strncmp(lib_name, "[stack]", 7) == 0) {
					num_seg--;
					break;
				}
				if (query_lib_name_in_list(lib_name) == -1) {
					/* a new name not in list */
					D_STRNDUP(lib_name_list[num_lib_in_map], lib_name,
						  PATH_MAX);
					if (lib_name_list[num_lib_in_map] == NULL) {
						D_FREE(buf);
						D_FREE(lib_name);
						quit_hook_init();
					}
					lib_base_addr[num_lib_in_map] = addr_B;
					num_lib_in_map++;
					if (num_lib_in_map >= MAX_NUM_LIB) {
						D_WARN("lib_base_addr is FULL. "
						       "You may need to increase MAX_NUM_LIB.\n");
						break;
					}
				}
			}
		}
		if (num_seg >= MAX_NUM_SEG) {
			D_WARN("num_seg >= MAX_NUM_LIB. You may want to increase MAX_NUM_LIB.\n");
			break;
		}
	}
	D_FREE(buf);
	D_FREE(lib_name);
}

/*
 * find_usable_block - Try to find an allocated memory block that is close enough to
 * a give module/shared object.
 *   @idx_mod: The name of shared library. Both short name ("ld") and full name ("ld-2.17.so")
 *             are accepted.
 * Returns:
 *   The index of memory block for patches. (-1) means not found usable block.
 */
static int
find_usable_block(int idx_mod)
{
	int      i;
	long int p_Min, p_Max, p_MemBlk;

	p_Min = (long int)(module_list[idx_mod].old_func_addr_min);
	p_Max = (long int)(module_list[idx_mod].old_func_addr_max);

	for (i = 0; i < num_patch_blk; i++) {
		p_MemBlk = (((long int)(patch_blk_list[i].patch_addr) +
			     (long int)(patch_blk_list[i].patch_addr_end)) /
			    2);
		if ((labs(p_Min - p_MemBlk) < NULL_RIP_VAR_OFFSET) &&
		    (labs(p_Max - p_MemBlk) < NULL_RIP_VAR_OFFSET)) {
			return i;
		}
	}

	return (-1);
}

/*
 * allocate_memory_block_for_patches - Allocated memory blocks to hold the patches for hook.
 */
static void
allocate_memory_block_for_patches(void)
{
	int      i, idx_seg, idx_mod, idx_blk;
	void    *pt_alloc;
	uint64_t pt_check;

	num_patch_blk = 0;

	for (idx_mod = 0; idx_mod < num_module; idx_mod++) {
		if ((module_list[idx_mod].old_func_addr_min == 0) &&
		    (module_list[idx_mod].old_func_addr_max == 0)) {
			continue;
		}
		idx_blk = find_usable_block(idx_mod);
		if (idx_blk >= 0) {
			module_list[idx_mod].idx_patch_blk = idx_blk;
		} else {
			/* does not exist */
			pt_check = ((uint64_t)(module_list[idx_mod].old_func_addr_min) +
				  (uint64_t)(module_list[idx_mod].old_func_addr_max)) /
				 2;

			idx_seg = -1;
			for (i = 0; i < num_seg; i++) {
				if ((pt_check >= addr_min[i]) && (pt_check <= addr_max[i])) {
					idx_seg = i;
					break;
				}
			}
			D_ASSERT(idx_seg >= 0);
			pt_alloc = (void *)(addr_max[idx_seg]);

			if (idx_seg < (num_seg - 1)) {
				if ((addr_min[idx_seg + 1] - addr_max[idx_seg]) < MIN_MEM_SIZE) {
					D_ERROR("Only %" PRIu64 " bytes available. No enough "
						"space to hold the trampoline for patches.\n",
						addr_min[idx_seg + 1] - addr_max[idx_seg]);
					quit_hook_init();
				}
			}

			patch_blk_list[num_patch_blk].patch_addr =
			    mmap(pt_alloc, MIN_MEM_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
				 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (patch_blk_list[num_patch_blk].patch_addr == MAP_FAILED) {
				DS_ERROR(errno, "mmap() failed");
				quit_hook_init();
			} else if (patch_blk_list[num_patch_blk].patch_addr != pt_alloc) {
				D_ERROR("mmap failed to allocate memory at desired address\n");
				quit_hook_init();
			}

			patch_blk_list[num_patch_blk].num_trampoline = 0;
			patch_blk_list[num_patch_blk].patch_addr_end =
			    patch_blk_list[num_patch_blk].patch_addr + MIN_MEM_SIZE;
			num_patch_blk++;
		}
	}
}

/*
 * determine_mem_block_size - Determine we need to change the permission of one or two pages
 * for a given address.
 *   @addr: The address of the entry of original function. We will change it to a jmp instruction.
 * Returns:
 *   The number of bytes we need to change permission with mprotect().
 */
static size_t
determine_mem_block_size(const void *addr)
{
	unsigned long int res, addr_code;

	addr_code = (unsigned long int)addr;
	res       = addr_code % page_size;
	if ((res + 5) > page_size) {
		/* close to the boundary of two memory pages */
		return (size_t)(page_size * 2);
	} else {
		return (size_t)(page_size);
	}
}

void
free_memory_in_hook(void)
{
	int i;

	for (i = 0; i < num_module; i++)
		D_FREE(module_list[i].module_name);

	D_FREE(path_ld);
	D_FREE(path_libc);
	D_FREE(module_list);
	D_FREE(path_libdl);
	D_FREE(path_libpthread);

	if (lib_name_list) {
		for (i = 0; i < num_lib_in_map; i++) {
			D_FREE(lib_name_list[i]);
		}
		D_FREE(lib_name_list);
	}
}

/* The max number of instruments of the entry code in original function to analyze */
#define MAX_INSTUMENTS (24)

/*
 * install_hook - Install hooks by setting up trampolines for all functions registered.
 * Returns:
 *   The number of hooks actually installed.
 */
int
install_hook(void)
{
	int                  j, idx_mod, iFunc, iFunc2, jMax, ReadItem, *p_int;
	int                  WithJmp[MAX_PATCH];
	int                  RIP_Offset, Jmp_Offset, nFunc_InBlk, num_hook_installed = 0;
	int                  OffsetList[MAX_INSTUMENTS];
	char                *pSubStr = NULL, *pOpOrgEntry;
	void                *pbaseOrg;
	size_t               MemSize_Modify;
	csh                  handle;
	cs_insn             *insn = NULL;
	size_t               num_inst, idx_inst;
	struct trampoline_t *tramp_list;
	long                 rc;

	if (found_libc == 0)
		return 0;

	rc = sysconf(_SC_PAGESIZE);
	if (rc == -1) {
		DS_ERROR(errno, "sysconf() failed to query page size");
		quit_hook_init();
	}
	page_size = (size_t)rc;
	mask      = ~(page_size - 1);

	query_all_org_func_addr();
	allocate_memory_block_for_patches();

	if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle)) {
		D_ERROR("cs_open() failed to initialize capstone engine.\n");
		quit_hook_init();
	}
	cs_opt_skipdata skipdata = {
		.mnemonic = "db",
	};
	cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
	cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t)&skipdata);

	for (idx_mod = 0; idx_mod < num_module; idx_mod++) {
		tramp_list =
		    (struct trampoline_t *)(patch_blk_list[module_list[idx_mod].idx_patch_blk]
						.patch_addr);
		nFunc_InBlk = patch_blk_list[module_list[idx_mod].idx_patch_blk].num_trampoline;

		for (iFunc = 0; iFunc < module_list[idx_mod].num_hook; iFunc++) {
			if (module_list[idx_mod].is_patch_disabled[iFunc])
				continue;
			for (iFunc2 = 0; iFunc2 < iFunc; iFunc2++) {
				if (module_list[idx_mod].is_patch_disabled[iFunc2] == 0) {
					/* a valid patch */
					if (module_list[idx_mod].old_func_addr_list[iFunc] ==
					    module_list[idx_mod].old_func_addr_list[iFunc2]) {
						module_list[idx_mod].is_patch_disabled[iFunc] = 1;
						/* disable duplicated patch */
						module_list[idx_mod].old_func_addr_list[iFunc] = 0;
						break;
					}
				}
			}
			/* recheck */
			if (module_list[idx_mod].is_patch_disabled[iFunc])
				continue;

			WithJmp[nFunc_InBlk] = -1;

			tramp_list[nFunc_InBlk].addr_org_func =
			    (void *)(module_list[idx_mod].old_func_addr_list[iFunc]);
			tramp_list[nFunc_InBlk].offset_rIP_var = NULL_RIP_VAR_OFFSET;
			tramp_list[nFunc_InBlk].saved_code_len = 0;

			insn     = NULL;
			num_inst = cs_disasm(handle,
					     (unsigned char *)tramp_list[nFunc_InBlk].addr_org_func,
					     MAX_LEN_DISASSEMBLE, 0, 0, &insn);
			if (num_inst <= 0) {
				D_ERROR("cs_disasm() failed to disassemble code.\n");
				goto err;
			}

			for (idx_inst = 0; idx_inst < num_inst; idx_inst++) {
				OffsetList[idx_inst] = insn[idx_inst].address;
				if (OffsetList[idx_inst] >= JMP_INSTRCTION_LEN) {
					tramp_list[nFunc_InBlk].saved_code_len =
					    OffsetList[idx_inst];
					if (idx_inst >= 2 && insn[idx_inst - 1].bytes[0] == 0xe9 &&
					    insn[idx_inst - 1].size == 5) {
						/* found a jmp instruction here!!! */
						WithJmp[nFunc_InBlk] = insn[idx_inst - 2].size + 1;
					}
					break;
				}

				pSubStr = strstr(insn[idx_inst].op_str, "[rip + ");
				if (pSubStr) {
					ReadItem = sscanf(pSubStr + 6, "%x]", &RIP_Offset);
					if (ReadItem == 1)
						tramp_list[nFunc_InBlk].offset_rIP_var = RIP_Offset;
				}
			}

			memcpy(tramp_list[nFunc_InBlk].bounce, instruction_bounce, BOUNCE_CODE_LEN);
			/* the address of new function */
			*((unsigned long int *)(tramp_list[nFunc_InBlk].bounce +
						OFFSET_NEW_FUNC_ADDR)) =
			    (unsigned long int)(module_list[idx_mod].new_func_addr_list[iFunc]);

			memcpy(tramp_list[nFunc_InBlk].trampoline,
			       tramp_list[nFunc_InBlk].addr_org_func,
			       tramp_list[nFunc_InBlk].saved_code_len);
			/* E9 is a jmp instruction */
			tramp_list[nFunc_InBlk].trampoline[tramp_list[nFunc_InBlk].saved_code_len] =
			    0xE9;
			Jmp_Offset = (int)(((long int)(tramp_list[nFunc_InBlk].addr_org_func) -
					    ((long int)(tramp_list[nFunc_InBlk].trampoline) + 5)) &
					   0xFFFFFFFF);
			*((int *)(tramp_list[nFunc_InBlk].trampoline +
				  tramp_list[nFunc_InBlk].saved_code_len + 1)) = Jmp_Offset;

			if (tramp_list[nFunc_InBlk].offset_rIP_var != NULL_RIP_VAR_OFFSET) {
				jMax = tramp_list[nFunc_InBlk].saved_code_len - 4;
				for (j = jMax - 2; j <= jMax; j++) {
					p_int = (int *)(tramp_list[nFunc_InBlk].trampoline + j);
					if (*p_int == tramp_list[nFunc_InBlk].offset_rIP_var) {
						/* correct relative offset of PIC var */
						*p_int += ((int)(((long int)(tramp_list[nFunc_InBlk]
										 .addr_org_func) -
								  (long int)(tramp_list[nFunc_InBlk]
										 .trampoline))));
					}
				}
			}
			if (WithJmp[nFunc_InBlk] > 0) {
				p_int = (int *)(tramp_list[nFunc_InBlk].trampoline +
						WithJmp[nFunc_InBlk]);
				*p_int +=
				    ((int)(((long int)(tramp_list[nFunc_InBlk].addr_org_func) -
					    (long int)(tramp_list[nFunc_InBlk].trampoline))));
			}
			if (tramp_list[nFunc_InBlk].trampoline[0] == 0xE9) {
				/* First instruction is JMP xxxx. Needs address correction */
				p_int = (int *)(tramp_list[nFunc_InBlk].trampoline + 1);
				/* the next four bytes are supposed to be the relative offset */
				*p_int +=
				    ((int)(((long int)(tramp_list[nFunc_InBlk].addr_org_func) -
					    (long int)(tramp_list[nFunc_InBlk].trampoline))));
			}

			/* set up function pointers for original functions */
			/* tramp_list[].trampoline holds the entry address */
			/* to call original function                        */
			*module_list[idx_mod].ptr_old_func_add_list[iFunc] =
			    (long int)(tramp_list[nFunc_InBlk].trampoline);

			/* fast mod */
			pbaseOrg =
			    (void *)((long int)(tramp_list[nFunc_InBlk].addr_org_func) & mask);

			MemSize_Modify = determine_mem_block_size(
			    (void *)(tramp_list[nFunc_InBlk].addr_org_func));
			if (mprotect(pbaseOrg, MemSize_Modify,
				     PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
				DS_ERROR(errno, "mprotect() failed");
				goto err;
			}

			/* save original code for uninstall */
			memcpy(tramp_list[nFunc_InBlk].org_code,
			       tramp_list[nFunc_InBlk].addr_org_func, 5);

			pOpOrgEntry    = (char *)(tramp_list[nFunc_InBlk].addr_org_func);
			pOpOrgEntry[0] = 0xE9;
			*((int *)(pOpOrgEntry + 1)) =
			    (int)((long int)(tramp_list[nFunc_InBlk].bounce) -
				  (long int)(tramp_list[nFunc_InBlk].addr_org_func) - 5);

			if (mprotect(pbaseOrg, MemSize_Modify, PROT_READ | PROT_EXEC) != 0) {
				DS_ERROR(errno, "mprotect() failed");
				goto err;
			}

			nFunc_InBlk++;
			num_hook_installed++;

			if (insn) {
				cs_free(insn, num_inst);
				insn = NULL;
			}
		}
		patch_blk_list[module_list[idx_mod].idx_patch_blk].num_trampoline +=
		    module_list[idx_mod].num_hook;
	}

	cs_close(&handle);

	free_memory_in_hook();

	return num_hook_installed;

err:
	if (insn) {
		cs_free(insn, num_inst);
		insn = NULL;
	}
	quit_hook_init();
	return 0;
}

/*
 * register_a_hook - Add one target function into the list of the functions to intercept.
 *   @module_name: The name of shared library. Both short name ("ld") and full name ("ld-2.17.so")
 *                 are accepted.
 *   @func_Name:   The function name.
 *   @new_func_addr: The address of our new implementation.
 *   @ptr_org_func: *ptr_org_func will hold the address of original function implemented in
 *                  lib module_name.
 * Returns:
 *   0: success; otherwise fail.
 */
int
register_a_hook(const char *module_name, const char *func_name, const void *new_func_addr,
		const long int *ptr_org_func)
{
	void *module;
	int   idx, idx_mod;
	char *module_name_local;

	/* make sure module_name[] and func_name[] are not too long. */
	if (strnlen(module_name, MAX_LEN_PATH_NAME + 1) >= MAX_LEN_PATH_NAME)
		return REGISTER_MODULE_NAME_TOO_LONG;
	if (strnlen(func_name, MAX_LEN_PATH_NAME + 1) >= MAX_LEN_FUNC_NAME)
		return REGISTER_FUNC_NAME_TOO_LONG;

	/* Do initialization work at the very first time. */
	if (!num_hook) {
		D_ALLOC_ARRAY(module_list, MAX_MODULE);
		if (module_list == NULL)
			quit_hook_init();
		memset(module_list, 0, sizeof(struct module_patch_info_t) * MAX_MODULE);

		D_ALLOC_ARRAY(lib_name_list, MAX_NUM_LIB);
		if (lib_name_list == NULL)
			quit_hook_init();
		memset(lib_name_list, 0, sizeof(char *) * MAX_NUM_LIB);

		memset(patch_blk_list, 0, sizeof(struct patch_block_t) * MAX_MODULE);
		determine_lib_path();
	}

	if (found_libc == 0)
		return REGISTER_NOT_FOUND_LIBC;

	if (get_module_maps_inited == 0) {
		get_module_maps();
		get_module_maps_inited = 1;
	}

	if (strncmp(module_name, "ld", 3) == 0)
		module_name_local = path_ld;
	else if (strncmp(module_name, "libc", 5) == 0)
		module_name_local = path_libc;
	else if (strncmp(module_name, "libdl", 6) == 0)
		module_name_local = path_libdl;
	else if (strncmp(module_name, "libpthread", 11) == 0)
		module_name_local = path_libpthread;
	else
		module_name_local = (char *)module_name;

	if (module_name_local[0] == '/') {
		/* absolute path */
		if (query_lib_name_in_list(module_name_local) == -1) {
			/* not loaded yet, then load the library */
			module = dlopen(module_name_local, RTLD_LAZY);
			if (module == NULL) {
				DS_ERROR(errno, "dlopen() failed");
				return REGISTER_DLOPEN_FAILED;
			}
			get_module_maps();
		}
	}

	idx = query_lib_name_in_list(module_name_local);
	if (idx == -1) {
		D_ERROR("Failed to find %s in /proc/pid/maps\n", module_name_local);
		quit_hook_init();
	}

	idx_mod = query_registered_module(module_name_local);
	if (idx_mod == -1) {
		/* not registered module name. Register it. */
		D_STRNDUP(module_list[num_module].module_name, module_name_local, PATH_MAX);
		if (module_list[num_module].module_name == NULL)
			quit_hook_init();
		module_list[num_module].module_base_addr = lib_base_addr[idx];
		strcpy(module_list[num_module].func_name_list[module_list[num_module].num_hook],
		       func_name);
		module_list[num_module].new_func_addr_list[module_list[num_module].num_hook] =
		    (void *)new_func_addr;
		module_list[num_module].ptr_old_func_add_list[module_list[num_module].num_hook] =
		    (long int *)ptr_org_func;
		module_list[num_module].num_hook = 1;
		num_module++;
	} else {
		D_ASSERT(module_list[idx_mod].module_base_addr == lib_base_addr[idx]);

		strcpy(module_list[idx_mod].func_name_list[module_list[idx_mod].num_hook],
		       func_name);
		module_list[idx_mod].new_func_addr_list[module_list[idx_mod].num_hook] =
		    (void *)new_func_addr;
		module_list[idx_mod].ptr_old_func_add_list[module_list[idx_mod].num_hook] =
		    (long int *)ptr_org_func;
		module_list[idx_mod].num_hook++;
	}

	num_hook++;

	if (num_hook > MAX_PATCH) {
		D_ERROR("num_hook > MAX_PATCH. MAX_PATCH needs to be increased.\n");
		quit_hook_init();
	}

	return REGISTER_SUCCESS;
}

/*
 * query_all_org_func_addr - Queries the addresses of all original functions to hook.
 * Returns:
 *   void
 */
static void
query_all_org_func_addr(void)
{
	int idx, idx_mod, iFunc;

	/* update module map */
	get_module_maps();

	for (idx_mod = 0; idx_mod < num_module; idx_mod++) {
		/* update module base address */
		idx = query_lib_name_in_list(module_list[idx_mod].module_name);

		if (idx == -1) {
			/* a new name not in list */
			D_ERROR("Fail to find library %s in maps.\n",
			       module_list[idx_mod].module_name);
			quit_hook_init();
		} else {
			module_list[idx_mod].module_base_addr = lib_base_addr[idx];
		}
	}

	for (idx_mod = 0; idx_mod < num_module; idx_mod++) {
		query_func_addr(
		    module_list[idx_mod].module_name, module_list[idx_mod].func_name_list,
		    module_list[idx_mod].old_func_addr_list, module_list[idx_mod].old_func_len_list,
		    module_list[idx_mod].module_base_addr, module_list[idx_mod].num_hook);

		for (iFunc = 0; iFunc < module_list[idx_mod].num_hook; iFunc++) {
			if (module_list[idx_mod].old_func_addr_list[iFunc]) {
				module_list[idx_mod].old_func_addr_min =
				    module_list[idx_mod].old_func_addr_list[iFunc];
				module_list[idx_mod].old_func_addr_max =
				    module_list[idx_mod].old_func_addr_list[iFunc];
				break;
			}
		}

		for (iFunc = 0; iFunc < module_list[idx_mod].num_hook; iFunc++) {
			if (module_list[idx_mod].old_func_addr_list[iFunc] == 0) {
				/* Ignore if fail to find the entry address for function */
				module_list[idx_mod].is_patch_disabled[iFunc] = 1;
				continue;
			}
			if (module_list[idx_mod].old_func_addr_min >
			    module_list[idx_mod].old_func_addr_list[iFunc]) {
				module_list[idx_mod].old_func_addr_min =
				    module_list[idx_mod].old_func_addr_list[iFunc];
			}
			if (module_list[idx_mod].old_func_addr_max <
			    module_list[idx_mod].old_func_addr_list[iFunc]) {
				module_list[idx_mod].old_func_addr_max =
				    module_list[idx_mod].old_func_addr_list[iFunc];
			}
		}
	}
}
