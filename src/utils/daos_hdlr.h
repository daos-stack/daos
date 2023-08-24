/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_HDLR_H__
#define __DAOS_HDLR_H__

#include <daos_fs.h>

#include <dfuse_ioctl.h>

#define OID_ARR_SIZE 8

enum fs_op {
	FS_COPY,
	FS_SET_ATTR,
	FS_GET_ATTR,
	FS_RESET_ATTR,
	FS_RESET_CHUNK_SIZE,
	FS_RESET_OCLASS,
	FS_CHECK,
};

enum cont_op {
	CONT_CREATE,
	CONT_DESTROY,
	CONT_CLONE,
	CONT_LIST_OBJS,
	CONT_QUERY,
	CONT_STAT,
	CONT_CHECK,
	CONT_GET_PROP,
	CONT_SET_PROP,
	CONT_LIST_ATTRS,
	CONT_DEL_ATTR,
	CONT_GET_ATTR,
	CONT_SET_ATTR,
	CONT_CREATE_SNAP,
	CONT_LIST_SNAPS,
	CONT_DESTROY_SNAP,
	CONT_ROLLBACK,
	CONT_GET_ACL,
	CONT_OVERWRITE_ACL,
	CONT_UPDATE_ACL,
	CONT_DELETE_ACL,
	CONT_SET_OWNER,
};

enum pool_op {
	POOL_LIST_CONTAINERS,
	POOL_QUERY,
	POOL_STAT,
	POOL_GET_PROP,
	POOL_SET_ATTR,
	POOL_GET_ATTR,
	POOL_LIST_ATTRS,
	POOL_DEL_ATTR,
	POOL_AUTOTEST,
};

enum obj_op {
	OBJ_QUERY,
	OBJ_LIST_KEYS,
	OBJ_DUMP
};

enum sh_op {
	SH_DAOS,
	SH_VOS
};

struct fs_copy_stats {
	uint64_t		num_dirs;
	uint64_t		num_files;
	uint64_t		num_links;
};

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

/* cmd_args_s: consolidated result of parsing command-line arguments
 * for pool, cont, obj commands, much of which is common.
 */

struct cmd_args_s {
	enum pool_op		p_op;		/* pool sub-command */
	enum cont_op		c_op;		/* cont sub-command */
	enum obj_op		o_op;		/* obj sub-command */
	enum fs_op		fs_op;		/* filesystem sub-command */
	enum sh_op		sh_op;		/* DAOS shell sub-command */
	char			*sysname;	/* --sys-name or --sys */
	uuid_t			p_uuid;		/* --pool */
	char			pool_str[DAOS_PROP_LABEL_MAX_LEN + 1]; /* pool label or uuid */
	daos_handle_t		pool;
	uuid_t			c_uuid;		/* --cont */
	char			cont_str[DAOS_PROP_LABEL_MAX_LEN + 1]; /* container label or uuid */
	daos_handle_t		cont;
	int			force;		/* --force */
	char			*attrname_str;	/* --attr attribute name */
	char			*value_str;	/* --value attribute value */

	/* Container unified namespace (path) related */
	char			*path;		/* --path cont namespace */
	char			*src;		/* --src path for fs copy */
	char			*dst;		/* --dst path for fs copy */
	char			*preserve_props; /* --path to metadata file */
	daos_cont_layout_t	type;		/* --type cont type */
	daos_oclass_id_t	oclass;		/* --oclass object class */
	daos_oclass_id_t	dir_oclass;	/* --dir_oclass object class */
	daos_oclass_id_t	file_oclass;	/* --file_oclass object class */
	uint32_t		mode;		/* --posix consistency mode */
	char			*hints;		/* --posix hints */
	daos_size_t		chunk_size;	/* --chunk_size of cont objs */

	/* Container snapshot/rollback related */
	char			*snapname_str;	/* --snap cont snapshot name */
	daos_epoch_t		epc;		/* --epc cont epoch */
	char			*epcrange_str;	/* --epcrange cont epochs */
	daos_epoch_t		epcrange_begin;
	daos_epoch_t		epcrange_end;
	daos_obj_id_t		oid;
	daos_prop_t		*props;		/* --properties cont create */

	/* Container datamover related */
	struct dm_args		*dm_args;	/* datamover arguments */
	struct fs_copy_stats	*fs_copy_stats;	/* fs copy stats */
	bool			 fs_copy_posix; /* fs copy to POSIX */

	FILE			*outstream;	/* normal output stream */
	FILE			*errstream;	/* errors stream */

	/* DFS related */
	char			*dfs_prefix;	/* --dfs-prefix name */
	char			*dfs_path;	/* --dfs-path file/dir */

	/* autotest related */
	bool			skip_big;	/* skip big tests */
	int			deadline_limit;	/* deadline limit for tests */

	FILE			*ostream;	/* help_hdlr() stream */
	char			*outfile;	/* --outfile path */
	char			*aclfile;	/* --acl-file path */
	char			*user;		/* --user name */
	char			*group;		/* --group name */
	bool			verbose;	/* --verbose mode */
	char			*entry;		/* --entry for ACL */
	char                    *principal;     /* --principal for ACL */

	/* DFuse related */
	struct dfuse_mem_query   dfuse_mem; /* --memquery */
};

int pool_autotest_hdlr(struct cmd_args_s *ap);
/* TODO: implement these pool op functions
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 */

/* general datamover operations */
void
dm_cont_free_usr_attrs(int n, char ***_names, void ***_buffers, size_t **_sizes);
int
dm_cont_get_usr_attrs(struct cmd_args_s *ap, daos_handle_t coh, int *_n, char ***_names,
		      void ***_buffers, size_t **_sizes);
int
dm_cont_get_all_props(struct cmd_args_s *ap, daos_handle_t coh, daos_prop_t **_props, bool get_oid,
		      bool get_label, bool get_roots);
int
dm_copy_usr_attrs(struct cmd_args_s *ap, daos_handle_t src_coh, daos_handle_t dst_coh);

/* DAOS filesystem operations */
int
fs_copy_hdlr(struct cmd_args_s *ap);
int
fs_dfs_hdlr(struct cmd_args_s *ap);
int
fs_dfs_get_attr_hdlr(struct cmd_args_s *ap, dfs_obj_info_t *attrs);
int
parse_filename_dfs(const char *path, char **_obj_name, char **_cont_name);
int
fs_fix_entry_hdlr(struct cmd_args_s *ap, bool fix_entry);
int
fs_recreate_sb_hdlr(struct cmd_args_s *ap);
int
fs_relink_root_hdlr(struct cmd_args_s *ap);

/* Container operations */
int
cont_check_hdlr(struct cmd_args_s *ap);
int
cont_clone_hdlr(struct cmd_args_s *ap);

/* Dfuse operations */
int
dfuse_evict(struct cmd_args_s *ap);

/* TODO implement the following container op functions
 * all with signatures similar to this:
 * int cont_FN_hdlr(struct cmd_args_s *ap)
 *
 * int cont_stat_hdlr()
 * int cont_rollback_hdlr()
 */

/* Dfuse operations, mostly handled through ioctls */
int
dfuse_count_query(struct cmd_args_s *ap);

#endif /* __DAOS_HDLR_H__ */
