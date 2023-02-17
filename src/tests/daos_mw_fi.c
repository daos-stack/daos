/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This program provides a testing tool for the middleware consistency work to allow introducing
 * leaks in the container by unlinking objects from the DFS or PyDAOS namespace without punching the
 * objects themselves.
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

static int
punch_obj(daos_handle_t coh, daos_obj_id_t oid, char *name)
{
	daos_handle_t	oh;
	daos_key_t	dkey;
	int		rc;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		fprintf(stderr, "daos_obj_open() failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (name == NULL) {
		rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
		if (rc) {
			daos_obj_close(oh, NULL);
			fprintf(stderr, "daos_obj_punch() failed: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	} else {
		d_iov_set(&dkey, (void *)name, strlen(name));

		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		if (rc) {
			daos_obj_close(oh, NULL);
			fprintf(stderr, "daos_obj_punch_dkeys() failed: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	rc = daos_obj_close(oh, NULL);
	if (rc)
		fprintf(stderr, "daos_obj_close() failed: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static int
fi_dfs(daos_handle_t poh, daos_handle_t coh, const char *op, const char *path, daos_prop_t *prop)
{
	dfs_t		*dfs = NULL;
	dfs_obj_t	*obj;
	daos_obj_id_t	oid;
	char		*dir = NULL, *dirp;
	char		*file = NULL, *fname;
	int		rc;

	if (strcmp(op, "punch_sb") == 0) {
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
		return punch_obj(coh, roots->cr_oids[0], NULL);
	}

	if (path[0] != '/') {
		fprintf(stderr, "Failed: Path must be absolute from the container root\n");
		return -DER_INVAL;
	}

	D_STRNDUP(dir, path, PATH_MAX);
	if (dir == NULL)
		return -DER_NOMEM;
	D_STRNDUP(file, path, PATH_MAX);
	if (file == NULL)
		D_GOTO(out_path, rc = -DER_NOMEM);

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if (rc) {
		fprintf(stderr, "dfs_mount() failed: (%d)\n", rc);
		D_GOTO(out_path, rc);
	}

	fname = basename(file);
	dirp = dirname(dir);
	printf("punching %s from %s\n", fname, dirp);
	rc = dfs_lookup(dfs, dirp, O_RDWR, &obj, NULL, NULL);
	if (rc) {
		fprintf(stderr, "dfs_lookup() failed: (%d)\n", rc);
		D_GOTO(out_dfs, rc);
	}
	rc = dfs_obj2id(obj, &oid);
	dfs_release(obj);
	if (rc) {
		fprintf(stderr, "dfs_obj2id() failed: (%d)\n", rc);
		D_GOTO(out_dfs, rc);
	}

	rc = punch_obj(coh, oid, fname);
out_dfs:
	dfs_umount(dfs);
out_path:
	D_FREE(file);
	D_FREE(dir);
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

int
main(int argc, char **argv)
{
	daos_handle_t			poh, coh;
	uint32_t			props[] = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int			num_props = ARRAY_SIZE(props);
	int				i;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry = NULL;
	int				rc;

	if (argc != 5 && argc != 4) {
		fprintf(stderr,
			"usage: ./daos_mw_fi pool cont [punch_entry; punch_sb] [name or path]\n");
		exit(1);
	}

	if (strcmp(argv[3], "punch_entry") == 0) {
		if (argc != 5) {
			fprintf(stderr,
				"usage: ./daos_mw_fi pool cont punch_entry [name or path]\n");
			exit(1);
		}
	} else if (strcmp(argv[3], "punch_sb") == 0) {
		if (argc != 4) {
			fprintf(stderr, "usage: ./daos_mw_fi pool cont punch_sb\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "Invalid Operation: %s\n", argv[3]);
		fprintf(stderr,
			"usage: ./daos_mw_fi pool cont [punch_entry; punch_sb] [name or path]\n");
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
		D_ASSERT(strcmp(argv[3], "punch_entry") == 0 || strcmp(argv[3], "punch_sb") == 0);
		rc = fi_dfs(poh, coh, argv[3], argv[4], prop);
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
	daos_cont_close(coh, NULL);
out_pool:
	daos_pool_disconnect(poh, NULL);
out_init:
	dfs_fini();
	return rc;
}
