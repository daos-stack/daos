/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * daos_serialize_hdlr.c - handler function for daos serialization
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)
#define LIBSERIALIZE	"libdaos_serialize.so"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/debug.h>

#include "daos_types.h"
#include "daos_fs.h"
#include "daos_uns.h"
#include "daos_hdlr.h"
#include "daos_datamover.h"

/* calls daos_cont_serialize using dlsym inside of daos_serialize library */
static int
serialize_cont(struct cmd_args_s *ap, daos_prop_t *props, struct dm_stats *stats,
	       struct dm_args *ca, char *filename)
{
	int		rc = 0;
	void		*handle = NULL;
	int		num_attrs = 0;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	int (*daos_cont_serialize)(daos_prop_t *, int, char **, char **, size_t *, int *,
				   int *, int *, uint64_t *, daos_handle_t, char *);

	/* Get all user attributes if any exist */
	rc = dm_cont_get_usr_attrs(ap, ca->src_coh, &num_attrs, &names, &buffers, &sizes);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attributes");
		D_GOTO(out, rc);
	}
	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to open "LIBSERIALIZE": %s", dlerror());
		D_GOTO(out, rc);
	}
	daos_cont_serialize = dlsym(handle, "daos_cont_serialize");
	if (daos_cont_serialize == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_serialize: %s", dlerror());
		D_GOTO(out, rc);
	}
	rc = (*daos_cont_serialize)(props, num_attrs, names, (char **)buffers, sizes,
				    &stats->total_oids, &stats->total_dkeys, &stats->total_akeys,
				    &stats->bytes_read, ca->src_coh, filename);
	if (rc != 0)
		DH_PERROR_DER(ap, rc, "Failed to serialize container");
out:
	if (num_attrs > 0)
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	if (handle != NULL)
		dlclose(handle);
	return rc;
}

int
cont_serialize_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			rc2 = 0;
	char			*src_str = NULL;
	size_t			src_str_len = 0;
	daos_cont_info_t	src_cont_info = {0};
	struct dm_args		ca = {0};
	struct dm_stats		stats = {0};
	size_t			output_path_len = 0;
	char			*filename = NULL;
	daos_prop_t		*props = NULL;

	/* Default output path to current working dir */
	if (ap->output_path == NULL) {
		ap->output_path = getcwd(ap->output_path, output_path_len);
		if (ap->output_path == NULL) {
			rc = daos_errno2der(errno);
			DH_PERROR_DER(ap, rc, "Failed to get current working directory");
			D_GOTO(out, rc);
		}
	} else {
		rc = mkdir(ap->output_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (rc && errno != EEXIST) {
			rc = daos_errno2der(errno);
			DH_PERROR_DER(ap, rc, "Failed to create output directory");
			D_GOTO(out, rc);
		}
	}
	src_str_len = strlen(ap->src);
	if (src_str_len == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Source path required");
		D_GOTO(out, rc);
	}
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(NULL, src_str, src_str_len, &ca.src_pool, &ca.src_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to parse source path");
		D_GOTO(out, rc);
	}

	/* connect to pool/cont to be serialized */
	rc = daos_pool_connect(ca.src_pool, ap->sysname, DAOS_PC_RW, &ca.src_poh,
			       NULL, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to connect to pool");
		D_GOTO(out, rc);
	}
	rc = daos_cont_open(ca.src_poh, ca.src_cont, DAOS_COO_RW,
			    &ca.src_coh, &src_cont_info, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to open container");
		D_GOTO(out_pool, rc);
	}

	D_ASPRINTF(filename, "%s/%s%s", ap->output_path, ca.src_cont, ".h5");
	if (filename == NULL)
		D_GOTO(out_cont, rc = -DER_NOMEM);

	/* get all container props */
	rc = dm_cont_get_all_props(ap, ca.src_coh, &props, true,  true, true);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get container properties");
		D_GOTO(out, rc);
	}

	/* serialize the container */
	rc = serialize_cont(ap, props, &stats, &ca, filename);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to serialize container");
		D_GOTO(out_cont, rc);
	}

out_cont:
	rc2 = daos_cont_close(ca.src_coh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc, "Failed to close container");
	}
out_pool:
	rc2 = daos_pool_disconnect(ca.src_poh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc, "failed to disconnect from pool %s", ca.src_pool);
	}
	if (rc == 0) {
		/* return with error code if cleanup fails*/
		rc = rc2;
		D_PRINT("Objects: %d\n", stats.total_oids);
		D_PRINT("\tDkeys: %d\n", stats.total_dkeys);
		D_PRINT("\tAkeys: %d\n", stats.total_akeys);
		D_PRINT("Bytes Read: %lu\n", stats.bytes_read);
	}
out:
	D_FREE(filename);
	D_FREE(src_str);
	return rc;
}

/* calls daos_cont_deserialize using dlsym inside of daos_serialize library */
static int
deserialize_cont(struct cmd_args_s *ap, struct dm_stats *stats, struct dm_args *ca, char *filename)
{
	int		rc = 0;
	void		*handle = NULL;
	int (*daos_cont_deserialize)(int *, int *, int *, uint64_t *, daos_handle_t, char *);

	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to open "LIBSERIALIZE": %s", dlerror());
		D_GOTO(out, rc);
	}
	daos_cont_deserialize = dlsym(handle, "daos_cont_deserialize");
	if (daos_cont_deserialize == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_deserialize: %s", dlerror());
		D_GOTO(out, rc);
	}
	rc = (*daos_cont_deserialize)(&stats->total_oids, &stats->total_dkeys, &stats->total_akeys,
				      &stats->bytes_written, ca->dst_coh, filename);
	if (rc != 0)
		DH_PERROR_DER(ap, rc, "Failed to deserialize container");
out:
	if (handle != NULL)
		dlclose(handle);
	return rc;
}

int
cont_deserialize_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			rc2 = 0;
	daos_cont_info_t	src_cont_info = {0};
	struct dm_args		ca = {0};
	uuid_t			cont_uuid;
	char			cont_str[130];
	bool			label_passed = false;
	struct dm_stats		stats = {0};
	daos_prop_t		*props = NULL;

	/* check if label was passed in using --cont-label */
	if (strlen(ap->cont_str) > 0)
		label_passed = true;

	/* connect to pool/cont to be serialized */
	rc = daos_pool_connect(ap->pool_str, ap->sysname, DAOS_PC_RW, &ca.dst_poh, NULL, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to connect to pool");
		D_GOTO(out, rc);
	}

	/* deserialize cont props to pass to cont create */
	rc = dm_deserialize_cont_md(ap, &ca, ap->path, &props);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to deserialize cont properties");
		D_GOTO(out_pool, rc);
	}

	/* need to pass deserialized props new container here */
	if (label_passed)
		rc = daos_cont_create_with_label(ca.dst_poh, ap->cont_str, props, &cont_uuid, NULL);
	else
		rc = daos_cont_create(ca.dst_poh, &cont_uuid, props, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to create container");
		D_GOTO(out_pool, rc);
	}
	uuid_unparse(cont_uuid, cont_str);

	D_PRINT("Successfully created container "DF_UUIDF"\n", DP_UUID(cont_uuid));

	rc = daos_cont_open(ca.dst_poh, cont_str, DAOS_COO_RW, &ca.dst_coh, &src_cont_info, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to open container %s", cont_str);
		D_GOTO(out_pool, rc);
	}

	D_PRINT("Deserializing file %s\n", ap->path);

	rc = deserialize_cont(ap, &stats, &ca, ap->path);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to deserialize container");
		D_GOTO(out_cont, rc);
	}

	/* deserialize user attributes if there are any */
	rc = dm_deserialize_cont_attrs(ap, &ca, ap->path);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to deserialize user attributes");
		D_GOTO(out_cont, rc);
	}

out_cont:
	rc2 = daos_cont_close(ca.dst_coh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc, "Failed to close container");
	}
out_pool:
	rc2 = daos_pool_disconnect(ca.dst_poh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc, "failed to disconnect from pool %s", ca.dst_pool);
	}
out:
	if (rc == 0) {
		/* return with error code if cleanup fails*/
		rc = rc2;
		D_PRINT("Objects: %d\n", stats.total_oids);
		D_PRINT("\tD-keys: %d\n", stats.total_dkeys);
		D_PRINT("\tA-keys: %d\n", stats.total_akeys);
		D_PRINT("Bytes Written: %lu\n", stats.bytes_written);
	}
	return rc;
}
