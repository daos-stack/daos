/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This program provides a testing tool for the middleware consistency work to allow introducing
 * leaks in the container by unlinking objects from the DFS or PyDAOS namespace without punching the
 * objects themselves.
 * For POSIX container, this supports three types of operations:
 * 1) Punch the SB object (punch_sb)
 * 2) Punch an entry of an object leaving a leaked object (punch_entry path)
 * 3) Punch an object leaving a dangling entry (punch_obj path)
 * Note that the path specified must be relative to the root of the container.
 * A path with dfuse mountpoint is not yet supported.
 *
 * For Python container, only punch_entry is valid to punch the entry of a dictionary
 * (punch_entry dict_name).
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos/common.h>

/** Copied from dfs.c (TODO: create an internal header file) */
#define INODE_AKEYS	12
#define INODE_AKEY_NAME	"DFS_INODE"
#define SLINK_AKEY_NAME	"DFS_SLINK"
#define MODE_IDX	0
#define OID_IDX		(sizeof(mode_t))
#define MTIME_IDX	(OID_IDX + sizeof(daos_obj_id_t))
#define CTIME_IDX	(MTIME_IDX + sizeof(uint64_t))
#define CSIZE_IDX	(CTIME_IDX + sizeof(uint64_t))
#define OCLASS_IDX	(CSIZE_IDX + sizeof(daos_size_t))
#define MTIME_NSEC_IDX	(OCLASS_IDX + sizeof(daos_oclass_id_t))
#define CTIME_NSEC_IDX	(MTIME_NSEC_IDX + sizeof(uint64_t))
#define UID_IDX		(CTIME_NSEC_IDX + sizeof(uint64_t))
#define GID_IDX		(UID_IDX + sizeof(uid_t))
#define SIZE_IDX	(GID_IDX + sizeof(gid_t))
#define HLC_IDX		(SIZE_IDX + sizeof(daos_size_t))
#define END_IDX		(HLC_IDX + sizeof(uint64_t))

enum {
	PUNCH_SB,
	PUNCH_ENTRY,
	PUNCH_OBJ,
	CORRUPT_ENTRY
};

static int
action_obj(daos_handle_t coh, daos_obj_id_t oid, int op, const char *name)
{
	daos_handle_t	oh;
	daos_key_t	dkey;
	int		rc, rc2;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		fprintf(stderr, "daos_obj_open() failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (name == NULL) {
		D_ASSERT(op == PUNCH_OBJ || op == PUNCH_SB);
		rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
		if (rc)
			fprintf(stderr, "daos_obj_punch() failed: "DF_RC"\n", DP_RC(rc));
		goto close;
	}

	d_iov_set(&dkey, (void *)name, strlen(name));

	if (op == PUNCH_ENTRY) {
		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		if (rc)
			fprintf(stderr, "daos_obj_punch_dkeys() failed: "DF_RC"\n", DP_RC(rc));
		goto close;
	}

	mode_t		bad_mode = 0xDEADBEAF;
	daos_size_t	bad_csize = 13;
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[2];
	daos_iod_t	iod;
	daos_recx_t	recxs[2];

	D_ASSERT(op == CORRUPT_ENTRY);
	/** corrupt the mode type bits and chunk size */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs	= recxs;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	iod.iod_nr	= 2;
	recxs[0].rx_idx	= MODE_IDX;
	recxs[0].rx_nr	= sizeof(mode_t);
	recxs[1].rx_idx	= CSIZE_IDX;
	recxs[1].rx_nr	= sizeof(daos_size_t);
	sgl.sg_nr	= 2;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iovs[0];
	d_iov_set(&sg_iovs[0], &bad_mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[1], &bad_csize, sizeof(daos_size_t));

	rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc)
		fprintf(stderr, "Failed to corrupt entry %s: "DF_RC"\n", name, DP_RC(rc));

close:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = rc2;
	return rc;
}

static int
fi_dfs(daos_handle_t poh, daos_handle_t coh, int op, const char *path, daos_prop_t *prop)
{
	dfs_t		*dfs = NULL;
	dfs_obj_t	*obj;
	daos_obj_id_t	oid;
	char		*dir = NULL;
	const char	*dirp;
	char		*file = NULL, *fname = NULL;
	int		rc;

	if (op == PUNCH_SB) {
		struct daos_prop_entry		*entry;
		struct daos_prop_co_roots	*roots;

		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
		D_ASSERT(entry != NULL);
		roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
		if (daos_obj_id_is_nil(roots->cr_oids[0]) ||
		    daos_obj_id_is_nil(roots->cr_oids[1])) {
			fprintf(stderr, "Failed: Invalid superblock or root object ID\n");
			return -DER_INVAL;
		}
		return action_obj(coh, roots->cr_oids[0], op, NULL);
	}

	if (path[0] != '/') {
		fprintf(stderr, "Failed: Path must be absolute from the container root\n");
		return -DER_INVAL;
	}

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if (rc) {
		fprintf(stderr, "dfs_mount() failed: (%d)\n", rc);
		return rc;
	}

	if (op == PUNCH_ENTRY || op == CORRUPT_ENTRY) {
		D_STRNDUP(dir, path, PATH_MAX);
		if (dir == NULL)
			D_GOTO(out_dfs, rc = -DER_NOMEM);
		D_STRNDUP(file, path, PATH_MAX);
		if (file == NULL)
			D_GOTO(out_path, rc = -DER_NOMEM);

		fname = basename(file);
		dirp = dirname(dir);
		if (op == PUNCH_ENTRY)
			printf("punching %s from %s\n", fname, dirp);
		else
			printf("corrupting %s in %s\n", fname, dirp);
	} else if (op == PUNCH_OBJ) {
		dirp = path;
		printf("punching object %s\n", path);
	} else {
		D_ASSERT(0);
	}

	rc = dfs_lookup(dfs, dirp, O_RDWR, &obj, NULL, NULL);
	if (rc) {
		fprintf(stderr, "dfs_lookup() failed: (%d)\n", rc);
		D_GOTO(out_path, rc);
	}
	rc = dfs_obj2id(obj, &oid);
	dfs_release(obj);
	if (rc) {
		fprintf(stderr, "dfs_obj2id() failed: (%d)\n", rc);
		D_GOTO(out_path, rc);
	}

	rc = action_obj(coh, oid, op, fname);
out_path:
	D_FREE(file);
	D_FREE(dir);
out_dfs:
	dfs_umount(dfs);
	return rc;
}

static int
fi_pydaos(daos_handle_t poh, daos_handle_t coh, const char *name, daos_prop_t *prop)
{
	daos_handle_t			oh;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	daos_key_t			dkey;
	int				rc;

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0])) {
		fprintf(stderr, "Failed: Invalid PyDAOS root object ID\n");
		return -DER_INVAL;
	}
	roots->cr_oids[0].hi |= (uint64_t)DAOS_OT_KV_HASHED << OID_FMT_TYPE_SHIFT;

	/** punch the dkey entry leaving the object it corresponds to. */
	rc = daos_obj_open(coh, roots->cr_oids[0], DAOS_OO_RW, &oh, NULL);
	if (rc) {
		fprintf(stderr, "daos_obj_open() failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	d_iov_set(&dkey, (void *)name, strlen(name));

	rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
	if (rc) {
		daos_obj_close(oh, NULL);
		fprintf(stderr, "daos_obj_punch_dkeys() failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	rc = daos_obj_close(oh, NULL);
	if (rc)
		fprintf(stderr, "daos_obj_close() failed: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static inline void
print_usage()
{
	fprintf(stderr, "usage: ./daos_mw_fi pool_label container_label action target\n");
	fprintf(stderr, "\t action: punch_entry; punch_obj; punch_sb; corrupt_entry\n");
	fprintf(stderr, "\t target: DFS path; Dictionary name\n");
}

int
main(int argc, char **argv)
{
	daos_handle_t			poh, coh;
	uint32_t			props[] = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int			num_props = ARRAY_SIZE(props);
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry = NULL;
	int				i;
	int				op;
	int				rc, rc2;

	if (argc != 5 && argc != 4) {
		print_usage();
		exit(1);
	}

	if (strcmp(argv[3], "punch_entry") == 0 || strcmp(argv[3], "punch_obj") == 0 ||
	    strcmp(argv[3], "corrupt_entry") == 0) {
		if (argc != 5) {
			print_usage();
			exit(1);
		}

		if (strcmp(argv[3], "punch_entry") == 0)
			op = PUNCH_ENTRY;
		else if (strcmp(argv[3], "punch_obj") == 0)
			op = PUNCH_OBJ;
		else
			op = CORRUPT_ENTRY;
	} else if (strcmp(argv[3], "punch_sb") == 0) {
		if (argc != 4) {
			print_usage();
			exit(1);
		}
		op = PUNCH_SB;
	} else {
		fprintf(stderr, "Invalid Operation: %s\n", argv[3]);
		print_usage();
		exit(1);
	}

	rc = daos_init();
	if (rc) {
		fprintf(stderr, "daos_init() failed "DF_RC"\n", DP_RC(rc));
		return 1;
	}

	rc = daos_pool_connect(argv[1], NULL, DAOS_PC_RW, &poh, NULL, NULL);
	if (rc) {
		fprintf(stderr, "daos_pool_connect() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_init, rc);
	}

	rc = daos_cont_open(poh, argv[2], DAOS_COO_RW, &coh, NULL, NULL);
	if (rc) {
		fprintf(stderr, "daos_cont_open() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_pool, rc);
	}

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		D_GOTO(out_cont, rc = -DER_NOMEM);

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		fprintf(stderr, "daos_cont_query() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_prop, rc);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || (entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX &&
			      entry->dpe_val != DAOS_PROP_CO_LAYOUT_PYTHON)) {
		fprintf(stderr, "Failed: container is not of type POSIX or PYTHON\n");
		D_GOTO(out_prop, rc = -DER_INVAL);
	}

	if (entry->dpe_val == DAOS_PROP_CO_LAYOUT_POSIX) {
		rc = fi_dfs(poh, coh, op, argv[4], prop);
	} else if (entry->dpe_val == DAOS_PROP_CO_LAYOUT_PYTHON) {
		if (strcmp(argv[3], "punch_entry") != 0) {
			fprintf(stderr, "Failed: Invalid op on PyDAOS container: %s\n", argv[3]);
			D_GOTO(out_prop, rc = -DER_INVAL);
		}
		rc = fi_pydaos(poh, coh, argv[4], prop);
	} else {
		D_ASSERT(0);
	}

out_prop:
	daos_prop_free(prop);
out_cont:
	rc2 = daos_cont_close(coh, NULL);
	if (rc == 0)
		rc = rc2;
out_pool:
	rc2 = daos_pool_disconnect(poh, NULL);
	if (rc == 0)
		rc = rc2;
out_init:
	daos_fini();
	return rc;
}
