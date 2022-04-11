/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DATAMOVER_H__
#define __DAOS_DATAMOVER_H__

#include "daos_fs_sys.h"

/* datamover functions and structs */

struct dm_args {
	char		*src;
	char		*dst;
	char		src_pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		src_cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		dst_pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		dst_cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	daos_handle_t	src_poh;
	daos_handle_t	src_coh;
	daos_handle_t	dst_poh;
	daos_handle_t	dst_coh;
	uint32_t	cont_prop_oid;
	uint32_t	cont_prop_layout;
	uint64_t	cont_layout;
	uint64_t	cont_oid;
};

struct file_dfs {
	enum {POSIX, DAOS} type;
	int fd;
	daos_off_t offset;
	dfs_obj_t *obj;
	dfs_sys_t *dfs_sys;
};

struct dm_stats {
	int total_oids;
	int total_dkeys;
	int total_akeys;
	uint64_t bytes_read;
	uint64_t bytes_written;
};

struct fs_copy_stats {
	uint64_t		num_dirs;
	uint64_t		num_files;
	uint64_t		num_links;
};

/* general datamover operations */
int dm_parse_path(struct file_dfs *file, char *path, size_t path_len, char (*pool_str)[],
		  char (*cont_str)[]);
void dm_cont_free_usr_attrs(int n, char ***_names, void ***_buffers, size_t **_sizes);
int dm_cont_get_usr_attrs(struct cmd_args_s *ap, daos_handle_t coh, int *_n, char ***_names,
			  void ***_buffers, size_t **_sizes);
int dm_cont_get_all_props(struct cmd_args_s *ap, daos_handle_t coh, daos_prop_t **_props,
			  bool get_oid, bool get_label, bool get_roots);
int dm_copy_usr_attrs(struct cmd_args_s *ap, daos_handle_t src_coh, daos_handle_t dst_coh);
int dm_deserialize_cont_md(struct cmd_args_s *ap, struct dm_args *ca, char *preserve_props,
			   daos_prop_t **props);
int dm_deserialize_cont_attrs(struct cmd_args_s *ap, struct dm_args *ca, char *filename);

/* serialization operations */
int cont_serialize_hdlr(struct cmd_args_s *ap);
int cont_deserialize_hdlr(struct cmd_args_s *ap);

#endif /* __DAOS_DATAMOVER_H__ */
