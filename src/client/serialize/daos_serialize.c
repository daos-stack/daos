#include <stdio.h>
#include <gurt/common.h>
#include <hdf5.h>
#include <daos.h>
#include <daos_cont.h>

#define ATTR_DSET "User Attributes"

/* for user attr dataset */
struct usr_attr {
	char	*attr_name;
	hvl_t	attr_val;
};

static int
serialize_acl(hid_t file_id, struct daos_prop_entry *entry,
	      const char *prop_str)
{

	int		rc = 0;
	int		i = 0;
	hid_t		status = 0;
	struct daos_acl	*acl = NULL;
	char		**acl_strs = NULL;
	size_t		len_acl = 0;
	hsize_t		attr_dims[1];
	hid_t		attr_dtype = 0;
	hid_t		attr_dspace = 0;
	hid_t		usr_attr = 0;


	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		D_GOTO(out, rc);
	}

	/* convert acl to list of strings */
	acl = (struct daos_acl *)entry->dpe_val_ptr;
	rc = daos_acl_to_strs(acl, &acl_strs, &len_acl);
	if (rc != 0) {
		D_ERROR("failed to convert acl to strs "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dims[0] = len_acl;
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create acl type "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to set acle dtype size "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create version attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create attribute "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Awrite(usr_attr, attr_dtype, acl_strs);
	if (status < 0) {
		rc = -DER_IO;
		D_ERROR("failed to write attributes "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	for (i = 0; i < len_acl; i++) {
		D_FREE(acl_strs[i]);
	}
	D_FREE(acl_strs);

	/* close hdf5 objects */
	if (usr_attr > 0)
		H5Aclose(usr_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	if (attr_dspace > 0)
		H5Sclose(attr_dspace);
	return rc;
}

static int
serialize_str(hid_t file_id, struct daos_prop_entry *entry,
	      const char *prop_str)
{

	int	rc = 0;
	hid_t	status = 0;
	hsize_t	attr_dims[1];
	hid_t	attr_dtype = 0;
	hid_t	attr_dspace = 0;
	hid_t	usr_attr = 0;

	if (entry == NULL || entry->dpe_str == NULL) {
		rc = -DER_MISC;
		D_ERROR("Propertty %s not found "DF_RC"\n", prop_str,
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	attr_dims[0] = 1;
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create user attr type "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Tset_size(attr_dtype, strlen(entry->dpe_str) + 1);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to set dtype size "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Tset_strpad(attr_dtype, H5T_STR_NULLTERM);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to set null terminator "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create version attribute dataspace "
			""DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create attribute "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Awrite(usr_attr, attr_dtype, entry->dpe_str);
	if (status < 0) {
		rc = -DER_IO;
		D_ERROR("failed to write attribute "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	/* close hdf5 objects */
	if (usr_attr > 0)
		H5Aclose(usr_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	if (attr_dspace > 0)
		H5Sclose(attr_dspace);
	return rc;
}

static int
serialize_uint(hid_t file_id,
	       struct daos_prop_entry *entry,
	       const char *prop_str)
{
	int	rc = 0;
	hid_t	status = 0;
	hsize_t	attr_dims[1];
	hid_t	attr_dtype = 0;
	hid_t	attr_dspace = 0;
	hid_t	usr_attr = 0;


	attr_dims[0] = 1;
	attr_dtype = H5Tcopy(H5T_NATIVE_UINT64);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create usr attr type "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create version attr dspace "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype,
			      attr_dspace, H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create attr "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Awrite(usr_attr, attr_dtype, &entry->dpe_val);
	if (status < 0) {
		rc = -DER_IO;
		D_ERROR("failed to write attr "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	/* close hdf5 objects */
	if (usr_attr > 0)
		H5Aclose(usr_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	if (attr_dspace > 0)
		H5Sclose(attr_dspace);
	return rc;
}

int
daos_cont_serialize_props(hid_t file_id, daos_prop_t *prop_query,
			  daos_handle_t cont)
{
	int			rc = 0;
	struct daos_prop_entry	*entry;

	entry = &prop_query->dpp_entries[0];
	rc = serialize_str(file_id, entry, "DAOS_PROP_CO_LABEL");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[1];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_LAYOUT_TYPE");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[2];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_LAYOUT_VER");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[3];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_CSUM");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[4];
	rc = serialize_uint(file_id, entry,
			    "DAOS_PROP_CO_CSUM_CHUNK_SIZE");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[5];
	rc = serialize_uint(file_id, entry,
			    "DAOS_PROP_CO_CSUM_SERVER_VERIFY");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[6];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_REDUN_FAC");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[7];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_REDUN_LVL");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[8];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_SNAPSHOT_MAX");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[9];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_COMPRESS");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[10];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_ENCRYPT");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[11];
	rc = serialize_str(file_id, entry, "DAOS_PROP_CO_OWNER");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[12];
	rc = serialize_str(file_id, entry, "DAOS_PROP_CO_OWNER_GROUP");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[13];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_DEDUP");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[14];
	rc = serialize_uint(file_id, entry,
				      "DAOS_PROP_CO_DEDUP_THRESHOLD");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	entry = &prop_query->dpp_entries[15];
	rc = serialize_uint(file_id, entry, "DAOS_PROP_CO_ALLOCED_OID");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	/* serialize ACL */
	if (prop_query->dpp_nr > 16) {
		entry = &prop_query->dpp_entries[16];
		rc = serialize_acl(file_id, entry, "DAOS_PROP_CO_ACL");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
	}

out:
	return rc;
}

int
daos_cont_serialize_attrs(hid_t file_id, hid_t *usr_attr_memtype,
			  int num_attrs, char **names, char **buffers,
			  size_t *sizes, daos_handle_t coh)
{
	int		rc = 0;
	hid_t		status = 0;
	hid_t		dset = 0;
	hid_t		dspace = 0;
	hsize_t		dims[1];
	struct usr_attr	*attr_data = NULL;
	int		i;

	if (num_attrs == 0) {
		D_GOTO(out_no_attrs, rc);
	}

	/* Create the user attribute data space */
	dims[0] = num_attrs;
	dspace = H5Screate_simple(1, dims, NULL);
	if (dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create user attr dspace "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Create the user attribute dataset */
	dset = H5Dcreate(file_id, ATTR_DSET, *usr_attr_memtype, dspace,
			 H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dset < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create user attribute dset "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Allocate space for all attributes */
	D_ALLOC(attr_data, num_attrs * sizeof(struct usr_attr));
	if (attr_data == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate user attributes "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Set the data for all attributes */
	for (i = 0; i < num_attrs; i++) {
		attr_data[i].attr_name = names[i];
		attr_data[i].attr_val.p = buffers[i];
		attr_data[i].attr_val.len = sizes[i];
	}

	status = H5Dwrite(dset, *usr_attr_memtype, H5S_ALL,
			  H5S_ALL, H5P_DEFAULT, attr_data);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to write user attr dset "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	D_FREE(attr_data);
	if (dset > 0)
		H5Dclose(dset);
	if (*usr_attr_memtype > 0)
		H5Tclose(*usr_attr_memtype);
	if (dspace > 0)
		H5Sclose(dspace);
out_no_attrs:
	return rc;
}

int
daos_cont_serialize_md(char *filename, daos_prop_t *props, daos_handle_t coh,
		       int num_attrs, char **names, char **buffers,
		       size_t *sizes)
{
	int	rc = 0;
	hid_t	status;
	hid_t	file_id = 0;
	hid_t	usr_attr_memtype = 0;
	hid_t	usr_attr_name_vtype = 0;
	hid_t	usr_attr_val_vtype = 0;

	fprintf(stdout, "Writing metadata file: %s\n", filename);

	file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file_id < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create hdf5 file "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = daos_cont_serialize_props(file_id, props, coh);
	if (rc != 0) {
		D_ERROR("failed to serialize cont layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* serialize usr_attrs if there are any */
	if (num_attrs > 0) {
		/* create User Attributes Dataset in daos_metadata file */
		usr_attr_memtype = H5Tcreate(H5T_COMPOUND,
					     sizeof(struct usr_attr));
		if (usr_attr_memtype < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to create user attr memtype "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}

		usr_attr_name_vtype = H5Tcopy(H5T_C_S1);
		if (usr_attr_name_vtype < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to create user attr vtype "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
		status = H5Tset_size(usr_attr_name_vtype, H5T_VARIABLE);
		if (status < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to create user attr name "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
		usr_attr_val_vtype = H5Tvlen_create(H5T_NATIVE_OPAQUE);
		if (usr_attr_val_vtype < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to create user attr val vtype "
				""DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Name",
				   HOFFSET(struct usr_attr, attr_name),
				   usr_attr_name_vtype);
		if (status < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to insert user attr name "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Value",
				   HOFFSET(struct usr_attr, attr_val),
				   usr_attr_val_vtype);
		if (status < 0) {
			rc = -DER_MISC;
			D_ERROR("failed to insert user attr val "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
		rc = daos_cont_serialize_attrs(file_id, &usr_attr_memtype,
					       num_attrs, names, buffers,
					       sizes, coh);
		if (rc != 0) {
			D_ERROR("failed to serialize usr attributes "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}
out:
	if (usr_attr_name_vtype > 0)
		H5Tclose(usr_attr_name_vtype);
	if (usr_attr_val_vtype > 0)
		H5Tclose(usr_attr_val_vtype);
	if (file_id > 0)
		H5Fclose(file_id);
	return rc;
}

static int
deserialize_str(hid_t file_id, struct daos_prop_entry *entry,
		const char *prop_str)
{
	hid_t	status = 0;
	int	rc = 0;
	hid_t	attr_dtype = 0;
	hid_t	cont_attr = 0;
	size_t	buf_size;

	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute type "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	buf_size = H5Tget_size(attr_dtype);
	if (buf_size <= 0) {
		rc = -DER_MISC;
		D_ERROR("failed to get size for property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ALLOC(entry->dpe_str, buf_size);
	if (entry->dpe_str == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Aread(cont_attr, attr_dtype, entry->dpe_str);
	if (status < 0) {
		rc = -DER_IO;
		D_ERROR("failed to read property attribute %s "DF_RC"\n",
			entry->dpe_str, DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	if (cont_attr > 0)
		H5Aclose(cont_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	return rc;
}

static int
deserialize_uint(hid_t file_id, struct daos_prop_entry *entry,
		 const char *prop_str)
{
	hid_t   status = 0;
	int     rc = 0;
	hid_t   cont_attr = 0;
	hid_t   attr_dtype = 0;

	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute type "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Aread(cont_attr, attr_dtype, &entry->dpe_val);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to read property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	if (cont_attr > 0)
		H5Aclose(cont_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	return rc;
}

static int
deserialize_acl(hid_t file_id, struct daos_prop_entry *entry,
		const char *prop_str)
{
	hid_t		status = 0;
	int		rc = 0;
	int		ndims = 0;
	const char      **rdata = NULL;
	struct daos_acl	*acl;
	htri_t		acl_exist;
	hid_t		cont_attr = 0;
	hid_t		attr_dtype = 0;
	hid_t		attr_dspace = 0;
	hsize_t		attr_dims[1];

	/* First check if the ACL * attribute exists. */
	acl_exist = H5Aexists(file_id, prop_str);
	if (acl_exist < 0) {
		/* Actual error  */
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute type "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	} else if (acl_exist == 0) {
		/* Does not exist, but that's okay. */
		rc = 0;
		D_GOTO(out, rc);
	}

	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		/* Could not open, but that's okay. */
		rc = 0;
		D_GOTO(out, rc);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open property attribute type %s "DF_RC"\n",
			prop_str, DP_RC(rc));
		D_GOTO(out, rc);
	}
	attr_dspace = H5Aget_space(cont_attr);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to read acl dspace "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	ndims = H5Sget_simple_extent_dims(attr_dspace, attr_dims, NULL);
	if (ndims < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to get dimensions of dspace "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ALLOC(rdata, attr_dims[0] * sizeof(char *));
	if (rdata == NULL) {
		rc = -DER_NOMEM;
		D_GOTO(out, rc);
	}
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to create dtype "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to set acl dtype size "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Aread(cont_attr, attr_dtype, rdata);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to read property attribute "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	/* convert acl strings back to struct acl, then store in entry */
	rc = daos_acl_from_strs(rdata, (size_t)attr_dims[0], &acl);
	if (rc != 0) {
		D_ERROR("failed to convert acl strs "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	entry->dpe_val_ptr = (void *)acl;
out:
	D_FREE(rdata);

	/* close hdf5 objects */
	if (cont_attr > 0)
		H5Aclose(cont_attr);
	if (attr_dtype > 0)
		H5Tclose(attr_dtype);
	if (attr_dspace > 0)
		H5Sclose(attr_dspace);
	return rc;
}

static int
deserialize_props(daos_handle_t poh, hid_t file_id, daos_prop_t **_prop,
		  uint64_t *cont_type)
{

	int			rc = 0;
	bool			deserialize_label = false;
	bool			close_cont = true;
	uint32_t		num_props = 16;
	daos_prop_t		*label = NULL;
	daos_prop_t		*prop = NULL;
	struct daos_prop_entry	*entry;
	struct daos_prop_entry	*label_entry;
	daos_handle_t		coh;
	daos_cont_info_t	cont_info = {0};

	label = daos_prop_alloc(1);
	if (label == NULL) {
		return ENOMEM;
	}
	label->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;

	/* read the container label entry to decide if it should be added
	 * to property list. The container label is required to be unique,
	 * which is why it is handled differently than the other container
	 * properties. If the label already exists in the pool then this
	 * property will be skipped for deserialization
	 */
	label_entry = &label->dpp_entries[0];
	rc = deserialize_str(file_id, label_entry, "DAOS_PROP_CO_LABEL");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	rc = daos_cont_open_by_label(poh, label_entry->dpe_str, DAOS_COO_RW,
				     &coh, &cont_info, NULL);
	if (rc == -DER_NONEXIST) {
		/* doesn't exist so ok to deserialize this container label */
		deserialize_label = true;
		close_cont = false;
	} else if (rc != 0) {
		D_GOTO(out, rc);
	}  else {
		fprintf(stdout, "Container label cannot be set, "
			"the label already exists in pool\n");
	}

	if (deserialize_label) {
		num_props++;
	}

	prop = daos_prop_alloc(num_props);
	if (prop == NULL) {
		return -DER_NOMEM;
	}

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_LAYOUT_VER;
	prop->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM;
	prop->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	prop->dpp_entries[4].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	prop->dpp_entries[5].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[6].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	prop->dpp_entries[7].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop->dpp_entries[8].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop->dpp_entries[9].dpe_type = DAOS_PROP_CO_ENCRYPT;
	prop->dpp_entries[10].dpe_type = DAOS_PROP_CO_OWNER;
	prop->dpp_entries[11].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	prop->dpp_entries[12].dpe_type = DAOS_PROP_CO_DEDUP;
	prop->dpp_entries[13].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
	prop->dpp_entries[14].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
	prop->dpp_entries[15].dpe_type = DAOS_PROP_CO_ACL;
	if (deserialize_label)
		prop->dpp_entries[16].dpe_type = DAOS_PROP_CO_LABEL;

	entry = &prop->dpp_entries[0];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_LAYOUT_TYPE");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[1];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_LAYOUT_VER");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[2];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_CSUM");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[3];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_CSUM_CHUNK_SIZE");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[4];
	rc = deserialize_uint(file_id, entry,
			      "DAOS_PROP_CO_CSUM_SERVER_VERIFY");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[5];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_REDUN_FAC");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[6];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_REDUN_LVL");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[7];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_SNAPSHOT_MAX");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[8];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_COMPRESS");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[9];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_ENCRYPT");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[10];
	rc = deserialize_str(file_id, entry, "DAOS_PROP_CO_OWNER");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[11];
	rc = deserialize_str(file_id, entry,
				       "DAOS_PROP_CO_OWNER_GROUP");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[12];
	rc = deserialize_uint(file_id, entry, "DAOS_PROP_CO_DEDUP");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[13];
	rc = deserialize_uint(file_id, entry,
					"DAOS_PROP_CO_DEDUP_THRESHOLD");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[14];
	rc = deserialize_uint(file_id, entry,
					"DAOS_PROP_CO_ALLOCED_OID");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	entry = &prop->dpp_entries[15];
	/* read acl as a list of strings in deserialize, then convert back to
	 * acl for property entry
	 */
	rc = deserialize_acl(file_id, entry, "DAOS_PROP_CO_ACL");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	if (deserialize_label)
		prop->dpp_entries[16].dpe_str = strdup(label_entry->dpe_str);
	*cont_type = prop->dpp_entries[0].dpe_val;
	*_prop = prop;
out:
	/* close container after checking if label exists in pool */
	if (close_cont)
		daos_cont_close(coh, NULL);
	daos_prop_free(label);
	return rc;
}

int
daos_cont_deserialize_props(daos_handle_t poh, char *filename,
			    daos_prop_t **props, uint64_t *cont_type)
{
	int	rc = 0;
	hid_t	file_id = 0;

	fprintf(stdout, "Reading metadata file: %s\n", filename);

	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open hdf5 file "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = deserialize_props(poh, file_id, props, cont_type);
	if (rc != 0) {
		fprintf(stdout, "failed to deserialize_props\n");
		D_ERROR("failed to deserialize cont props "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	if (file_id > 0)
		H5Fclose(file_id);
	return rc;
}

static int
deserialize_attrs(hid_t file_id, uint64_t *_num_attrs,
		  char ***_names, void ***_buffers, size_t **_sizes)
{
	hid_t		status = 0;
	int		rc = 0;
	int		num_attrs = 0;
	int		num_dims = 0;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	hid_t		dset = 0;
	hid_t		dspace = 0;
	hid_t		vtype = 0;
	hsize_t		dims[1];
	struct usr_attr	*attr_data = NULL;
	int		i;

	/* Read the user attributes */
	dset = H5Dopen(file_id, "User Attributes", H5P_DEFAULT);
	if (dset < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open user attributes dset "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	dspace = H5Dget_space(dset);
	if (dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to get user attributes dspace "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	vtype = H5Dget_type(dset);
	if (vtype < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to get user attributes vtype "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	num_dims = H5Sget_simple_extent_dims(dspace, dims, NULL);
	if (num_dims < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to get user attributes dimensions "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	num_attrs = dims[0];
	D_ALLOC(attr_data, dims[0] * sizeof(struct usr_attr));
	if (attr_data == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate user attributes "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	status = H5Dread(dset, vtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, attr_data);
	if (status < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to read user attributes data "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ALLOC(names, num_attrs * sizeof(char *));
	if (names == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate user attributes "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ALLOC(buffers, num_attrs * sizeof(void *));
	if (buffers == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate user attributes "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ALLOC(sizes, num_attrs * sizeof(size_t));
	if (sizes == NULL) {
		rc = -DER_MISC;
		D_ERROR("failed to allocate user attribute sizes "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	/* Set the user attribute buffers */
	for (i = 0; i < num_attrs; i++) {
		names[i] = attr_data[i].attr_name;
		buffers[i] = attr_data[i].attr_val.p;
		sizes[i] = attr_data[i].attr_val.len;
	}
	*_num_attrs = num_attrs;
	*_names = names;
	*_buffers = buffers;
	*_sizes = sizes;
out:
	D_FREE(attr_data);
	if (dset > 0)
		H5Dclose(dset);
	if (dspace > 0)
		H5Sclose(dspace);
	if (vtype > 0)
		H5Tclose(vtype);
	return rc;
}


int
daos_cont_deserialize_attrs(char *filename, uint64_t *num_attrs,
			    char ***names, void ***buffers, size_t **sizes)
{
	int			rc = 0;
	hid_t			file_id = 0;
	htri_t			usr_attrs_exist;


	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		rc = -DER_MISC;
		D_ERROR("failed to open hdf5 file "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	usr_attrs_exist = H5Lexists(file_id, "User Attributes", H5P_DEFAULT);
	if (usr_attrs_exist > 0) {
		rc = deserialize_attrs(file_id, num_attrs, names,
				       buffers, sizes);
		if (rc != 0) {
			D_ERROR("failed to deserialize user attrs "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}
out:
	if (file_id > 0)
		H5Fclose(file_id);
	return rc;
}

