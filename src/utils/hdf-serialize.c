#include <stdio.h>
#include <hdf5.h>
#include <daos.h>
#include <daos_cont.h>

/* for user attr dataset */
typedef struct {
    char *attr_name;
    hvl_t attr_val;
} usr_attr_t;

static int
cont_serialize_prop_acl(hid_t *file_id, struct daos_prop_entry *entry,
			const char *prop_str)
{

	int		rc = 0;
	hid_t		status = 0;
	struct daos_acl	*acl = NULL;
	char		**acl_strs = NULL;
	size_t		len_acl = 0;
	hsize_t		attr_dims[1];
	hid_t		attr_dtype;
	hid_t		attr_dspace;
	hid_t		usr_attr;


	if (!entry || !entry->dpe_val_ptr) {
		goto out;
	}

	/* convert acl to list of strings */
	acl = (struct daos_acl *)entry->dpe_val_ptr;
	rc = daos_acl_to_strs(acl, &acl_strs, &len_acl);
	if (rc != 0) {
		fprintf(stderr, "failed to convert acl to strs");
		goto out;
	}
	attr_dims[0] = len_acl;
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to create acl type");
		rc = 1;
		goto out;
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		fprintf(stderr, "failed to set acl dtype size");
		rc = 1;
		goto out;
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		fprintf(stderr, "failed to create version attribute");
		rc = 1;
		goto out;
	}
	usr_attr = H5Acreate2(*file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		fprintf(stderr, "failed to create attribute");
		rc = 1;
		goto out;
	}
	status = H5Awrite(usr_attr, attr_dtype, acl_strs);
	if (status < 0) {
		fprintf(stderr, "failed to write attribute");
		rc = 1;
		goto out;
	}
	status = H5Aclose(usr_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute");
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close dtype");
		rc = 1;
		goto out;
	}
	status = H5Sclose(attr_dspace);
	if (status < 0) {
		fprintf(stderr, "failed to close dspace");
		rc = 1;
		goto out;
	}
out:
	return rc;
}

static int
cont_serialize_prop_str(hid_t *file_id, struct daos_prop_entry *entry,
			const char *prop_str)
{

	int	rc = 0;
	hid_t	status = 0;
	hsize_t	attr_dims[1];
	hid_t	attr_dtype;
	hid_t	attr_dspace;
	hid_t	usr_attr;

	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "Property %s not found", prop_str);
		rc = 1;
		goto out;
	}

	attr_dims[0] = 1;
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to create usr attr type");
		rc = 1;
		goto out;
	}
	status = H5Tset_size(attr_dtype, strlen(entry->dpe_str) + 1);
	if (status < 0) {
		fprintf(stderr, "failed to set dtype size");
		rc = 1;
		goto out;
	}
	status = H5Tset_strpad(attr_dtype, H5T_STR_NULLTERM);
	if (status < 0) {
		fprintf(stderr, "failed to set null terminator");
		rc = 1;
		goto out;
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		fprintf(stderr, "failed to create version attribute dataspace");
		rc = 1;
		goto out;
	}

	usr_attr = H5Acreate2(*file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		fprintf(stderr, "failed to create attribute");
		rc = 1;
		goto out;
	}
	status = H5Awrite(usr_attr, attr_dtype, entry->dpe_str);
	if (status < 0) {
		fprintf(stderr, "failed to write attribute");
		rc = 1;
		goto out;
	}
	status = H5Aclose(usr_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute");
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close dtype");
		rc = 1;
		goto out;
	}
	status = H5Sclose(attr_dspace);
	if (status < 0) {
		fprintf(stderr, "failed to close dspace");
		rc = 1;
		goto out;
	}
out:
	return rc;
}

static int
cont_serialize_prop_uint(hid_t *file_id,
			 struct daos_prop_entry *entry,
			 const char *prop_str)
{
	int	rc = 0;
	hid_t	status = 0;
	hsize_t	attr_dims[1];
	hid_t	attr_dtype;
	hid_t	attr_dspace;
	hid_t	usr_attr;


	attr_dims[0] = 1;
	attr_dtype = H5Tcopy(H5T_NATIVE_UINT64);
	status = H5Tset_size(attr_dtype, 8);
	if (status < 0) {
		fprintf(stderr, "failed to create version dtype");
		rc = 1;
		goto out;
	}
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to create usr attr type");
		rc = 1;
		goto out;
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		fprintf(stderr, "failed to create version attr dspace");
		rc = 1;
		goto out;
	}
	usr_attr = H5Acreate2(*file_id, prop_str, attr_dtype,
			      attr_dspace, H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		fprintf(stderr, "failed to create attr");
		rc = 1;
		goto out;
	}
	status = H5Awrite(usr_attr, attr_dtype, &entry->dpe_val);
	if (status < 0) {
		fprintf(stderr, "failed to write attr");
		rc = 1;
		goto out;
	}
	status = H5Aclose(usr_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close attr");
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close dtype");
		rc = 1;
		goto out;
	}
	status = H5Sclose(attr_dspace);
	if (status < 0) {
		fprintf(stderr, "failed to close dspace");
		rc = 1;
		goto out;
	}
out:
	return rc;
}

static int
cont_serialize_props(hid_t *file_id, daos_prop_t *prop_query,
		     daos_handle_t cont)
{
	int			rc = 0;
	struct daos_prop_entry	*entry;

	entry = &prop_query->dpp_entries[0];
	rc = cont_serialize_prop_str(file_id, entry, "DAOS_PROP_CO_LABEL");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[1];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_LAYOUT_TYPE");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[2];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_LAYOUT_VER");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[3];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_CSUM");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[4];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_CSUM_CHUNK_SIZE");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[5];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_CSUM_SERVER_VERIFY");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[6];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_REDUN_FAC");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[7];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_REDUN_LVL");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[8];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_SNAPSHOT_MAX");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[9];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_COMPRESS");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[10];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_ENCRYPT");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[11];
	rc = cont_serialize_prop_str(file_id, entry, "DAOS_PROP_CO_OWNER");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[12];
	rc = cont_serialize_prop_str(file_id, entry,
				     "DAOS_PROP_CO_OWNER_GROUP");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[13];
	rc = cont_serialize_prop_uint(file_id, entry, "DAOS_PROP_CO_DEDUP");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[14];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_DEDUP_THRESHOLD");
	if (rc != 0) {
		goto out;
	}

	entry = &prop_query->dpp_entries[15];
	rc = cont_serialize_prop_uint(file_id, entry,
				      "DAOS_PROP_CO_ALLOCED_OID");
	if (rc != 0) {
		goto out;
	}

	/* serialize ACL */
	if (prop_query->dpp_nr > 16) {
		entry = &prop_query->dpp_entries[16];
		rc = cont_serialize_prop_acl(file_id, entry,
					     "DAOS_PROP_CO_ACL");
		if (rc != 0) {
			goto out;
		}
	}

out:
	return rc;
}

int
cont_serialize_usr_attrs(hid_t *file_id, hid_t *usr_attr_memtype, int num_attrs,
			 char **names, char **buffers, size_t *sizes,
			 daos_handle_t coh)
{
	int		rc = 0;
	hid_t		status = 0;
	hid_t		dset = 0;
	hid_t		dspace = 0;
	hsize_t		dims[1];
	usr_attr_t	*attr_data = NULL;
	int		i;

	if (num_attrs == 0) {
		goto out_no_attrs;
	}

	/* Create the user attribute data space */
	dims[0] = num_attrs;
	dspace = H5Screate_simple(1, dims, NULL);
	if (dspace < 0) {
		fprintf(stderr, "failed to create user attr dspace");
		rc = 1;
		goto out;
	}

	/* Create the user attribute dataset */
	dset = H5Dcreate(*file_id, "User Attributes",
			 *usr_attr_memtype, dspace,
			 H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dset < 0) {
		fprintf(stderr, "failed to create user attribute dset");
		rc = 1;
		goto out;
	}

	/* Allocate space for all attributes */
	attr_data = calloc(num_attrs, sizeof(usr_attr_t));
	if (attr_data == NULL) {
		fprintf(stderr, "failed to allocate user attributes");
		rc = 1;
		goto out;
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
		fprintf(stderr, "failed to write user attr dset");
		rc = 1;
		goto out;
	}

out:
	free(attr_data);
	H5Dclose(dset);
	H5Tclose(*usr_attr_memtype);
	H5Sclose(dspace);
out_no_attrs:
	return rc;
}

int
serialize_daos_metadata(char *filename, daos_prop_t *props,
			daos_handle_t coh, int num_attrs,
			char **names, char **buffers, size_t *sizes)
{
	int	rc = 0;
	hid_t	status;
	hid_t	file_id;
	hid_t	usr_attr_memtype;
	hid_t	usr_attr_name_vtype;
	hid_t	usr_attr_val_vtype;

	fprintf(stderr, "Writing metadata file: %s\n", filename);

	file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file_id < 0) {
		rc = 1;
		fprintf(stderr, "failed to create hdf5 file");
		goto out;
	}
	rc = cont_serialize_props(&file_id, props, coh);
	if (rc != 0) {
		fprintf(stderr, "failed to serialize cont layout: "DF_RC,
			DP_RC(rc));
		goto out;
	}

	/* serialize usr_attrs if there are any */
	if (num_attrs > 0) {
		/* create User Attributes Dataset in daos_metadata file */
		usr_attr_memtype = H5Tcreate(H5T_COMPOUND, sizeof(usr_attr_t));
		if (usr_attr_memtype < 0) {
			rc = 1;
			fprintf(stderr, "failed to create user attr memtype");
			goto out;
		}

		usr_attr_name_vtype = H5Tcopy(H5T_C_S1);
		if (usr_attr_name_vtype < 0) {
			rc = 1;
			fprintf(stderr, "failed to create user attr name "
				"vtype");
			goto out;
		}
		status = H5Tset_size(usr_attr_name_vtype, H5T_VARIABLE);
		if (status < 0) {
			rc = 1;
			fprintf(stderr, "failed to create user attr name "
				"vtype");
			goto out;
		}
		usr_attr_val_vtype = H5Tvlen_create(H5T_NATIVE_OPAQUE);
		if (usr_attr_val_vtype < 0) {
			rc = 1;
			fprintf(stderr, "failed to create user attr val vtype");
			goto out;
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Name",
				   HOFFSET(usr_attr_t, attr_name),
				   usr_attr_name_vtype);
		if (status < 0) {
			rc = 1;
			fprintf(stderr, "failed to insert user attr name");
			goto out;
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Value",
				   HOFFSET(usr_attr_t, attr_val),
				   usr_attr_val_vtype);
		if (status < 0) {
			rc = 1;
			fprintf(stderr, "failed to insert user attr val");
			goto out;
		}
		rc = cont_serialize_usr_attrs(&file_id, &usr_attr_memtype,
					      num_attrs, names, buffers,
					      sizes, coh);
		if (rc != 0) {
			fprintf(stderr, "failed to serialize usr attributes: "
				""DF_RC, DP_RC(rc));
			goto out;
		}

		status = H5Tclose(usr_attr_name_vtype);
		if (status < 0) {
			rc = 1;
			fprintf(stderr, "failed to close user attr name "
				"datatype");
		}
		status = H5Tclose(usr_attr_val_vtype);
		if (status < 0) {
			rc = 1;
			fprintf(stderr, "failed to close user attr value "
				"datatype");
		}
	}
out:
	H5Fclose(file_id);
	return rc;
}

static int
cont_deserialize_prop_str(hid_t *file_id, struct daos_prop_entry *entry,
			  const char *prop_str)
{
	hid_t	status = 0;
	int	rc = 0;
	hid_t	attr_dtype;
	hid_t	cont_attr;
	size_t	buf_size;

	cont_attr = H5Aopen(*file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		fprintf(stderr, "failed to open property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to open property attribute type %s",
			prop_str);
		rc = 1;
		goto out;
	}
	buf_size = H5Tget_size(attr_dtype);
	if (buf_size <= 0) {
		fprintf(stderr, "failed to get size for property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	entry->dpe_str = calloc(1, buf_size);
	if (entry->dpe_str == NULL) {
		fprintf(stderr, "failed to allocate property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Aread(cont_attr, attr_dtype, entry->dpe_str);
	if (status < 0) {
		fprintf(stderr, "failed to read property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Aclose(cont_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute datatype");
		rc = 1;
		goto out;
	}
out:
	return rc;
}

static int
cont_deserialize_prop_uint(hid_t *file_id, struct daos_prop_entry *entry,
			   const char *prop_str)
{
	hid_t   status = 0;
	int     rc = 0;
	hid_t   cont_attr;
	hid_t   attr_dtype;

	cont_attr = H5Aopen(*file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		fprintf(stderr, "failed to open property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to open property attribute type %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Aread(cont_attr, attr_dtype, &entry->dpe_val);
	if (status < 0) {
		fprintf(stderr, "failed to read property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Aclose(cont_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute datatype: %s",
			prop_str);
		rc = 1;
		goto out;
	}
out:
	return rc;
}

static int
cont_deserialize_prop_acl(hid_t *file_id, struct daos_prop_entry *entry,
			  const char *prop_str)
{
	hid_t		status = 0;
	int		rc = 0;
	int		ndims = 0;
	const char      **rdata = NULL;
	struct daos_acl	*acl;
	htri_t		acl_exist;
	hid_t		cont_attr;
	hid_t		attr_dtype;
	hid_t		attr_dspace;
	hsize_t		attr_dims[1];

	/* First check if the ACL * attribute exists. */
	acl_exist = H5Aexists(*file_id, prop_str);
	if (acl_exist < 0) {
		/* Actual error  */
		fprintf(stderr, "failed to open property attribute type %s",
			prop_str);
		rc = 1;
		goto out;
	} else if (acl_exist == 0) {
		/* Does not exist, but that's okay. */
		rc = 0;
		goto out;
	}

	cont_attr = H5Aopen(*file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		/* Could not open, but that's okay. */
		rc = 0;
		goto out;
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		fprintf(stderr, "failed to open property attribute type %s",
			prop_str);
		rc = 1;
		goto out;
	}
	attr_dspace = H5Aget_space(cont_attr);
	if (status < 0) {
		fprintf(stderr, "failed to read acl dspace");
		rc = 1;
		goto out;
	}
	ndims = H5Sget_simple_extent_dims(attr_dspace, attr_dims, NULL);
	if (ndims < 0) {
		fprintf(stderr, "failed to get dimensions of dspace");
		rc = 1;
		goto out;
	}
	rdata = calloc(attr_dims[0], sizeof(char *));
	if (rdata == NULL) {
		rc = ENOMEM;
		goto out;
	}
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (status < 0) {
		fprintf(stderr, "failed to create dtype");
		rc = 1;
		goto out;
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		fprintf(stderr, "failed to set acl dtype size");
		rc = 1;
		goto out;
	}
	status = H5Aread(cont_attr, attr_dtype, rdata);
	if (status < 0) {
		fprintf(stderr, "failed to read property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	/* convert acl strings back to struct acl, then store in entry */
	rc = daos_acl_from_strs(rdata, (size_t)attr_dims[0], &acl);
	if (rc != 0) {
		fprintf(stderr, "failed to convert acl strs");
		goto out;
	}
	entry->dpe_val_ptr = (void *)acl;
	status = H5Aclose(cont_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close property attribute %s",
			prop_str);
		rc = 1;
		goto out;
	}
	status = H5Tclose(attr_dtype);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute datatype");
		rc = 1;
		goto out;
	}
	status = H5Sclose(attr_dspace);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute dataspace");
		rc = 1;
		goto out;
	}
out:
	free(rdata);
	return rc;
}

static int
cont_deserialize_all_props(hid_t *file_id, daos_prop_t *prop,
			   uint64_t *cont_type)
{

	int			rc = 0;
	struct daos_prop_entry	*entry;

	prop = daos_prop_alloc(17);
	if (prop == NULL) {
		return ENOMEM;
	}

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	prop->dpp_entries[2].dpe_type = DAOS_PROP_CO_LAYOUT_VER;
	prop->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM;
	prop->dpp_entries[4].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	prop->dpp_entries[5].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	prop->dpp_entries[6].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[7].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	prop->dpp_entries[8].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop->dpp_entries[9].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop->dpp_entries[10].dpe_type = DAOS_PROP_CO_ENCRYPT;
	prop->dpp_entries[11].dpe_type = DAOS_PROP_CO_OWNER;
	prop->dpp_entries[12].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	prop->dpp_entries[13].dpe_type = DAOS_PROP_CO_DEDUP;
	prop->dpp_entries[14].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
	prop->dpp_entries[15].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
	prop->dpp_entries[16].dpe_type = DAOS_PROP_CO_ACL;

	entry = &prop->dpp_entries[0];
	rc = cont_deserialize_prop_str(file_id, entry, "DAOS_PROP_CO_LABEL");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[1];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_LAYOUT_TYPE");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[2];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_LAYOUT_VER");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[3];
	rc = cont_deserialize_prop_uint(file_id, entry, "DAOS_PROP_CO_CSUM");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[4];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_CSUM_CHUNK_SIZE");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[5];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_CSUM_SERVER_VERIFY");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[6];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_REDUN_FAC");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[7];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_REDUN_LVL");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[8];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_SNAPSHOT_MAX");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[9];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_COMPRESS");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[10];
	rc = cont_deserialize_prop_uint(file_id, entry, "DAOS_PROP_CO_ENCRYPT");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[11];
	rc = cont_deserialize_prop_str(file_id, entry, "DAOS_PROP_CO_OWNER");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[12];
	rc = cont_deserialize_prop_str(file_id, entry,
				       "DAOS_PROP_CO_OWNER_GROUP");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[13];
	rc = cont_deserialize_prop_uint(file_id, entry, "DAOS_PROP_CO_DEDUP");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[14];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_DEDUP_THRESHOLD");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[15];
	rc = cont_deserialize_prop_uint(file_id, entry,
					"DAOS_PROP_CO_ALLOCED_OID");
	if (rc != 0) {
		goto out;
	}
	entry = &prop->dpp_entries[16];
	/* read acl as a list of strings in deserialize, then convert back to
	 * acl for property entry
	 */
	rc = cont_deserialize_prop_acl(file_id, entry, "DAOS_PROP_CO_ACL");
	if (rc != 0) {
		goto out;
	}
	*cont_type = prop->dpp_entries[1].dpe_val;
out:
	return rc;
}

int
deserialize_daos_cont_prop_metadata(char *filename, daos_prop_t *props,
				    uint64_t *cont_type)
{
	int			rc = 0;
	hid_t			file_id;

	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		fprintf(stderr, "failed to open hdf5 file");
		rc = 1;
		goto out;
	}
	rc = cont_deserialize_all_props(&file_id, props, cont_type);
	if (rc != 0) {
		fprintf(stderr, "failed to deserialize cont props: "
			""DF_RC, DP_RC(rc));
		goto out;
	}
out:
	H5Fclose(file_id);
	return rc;
}

static int
cont_deserialize_usr_attrs(hid_t *file_id, uint64_t *_num_attrs,
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
	usr_attr_t	*attr_data = NULL;
	int		i;

	/* Read the user attributes */
	dset = H5Dopen(*file_id, "User Attributes", H5P_DEFAULT);
	if (dset < 0) {
		fprintf(stderr, "failed to open user attributes dset");
		rc = 1;
		goto out;
	}
	dspace = H5Dget_space(dset);
	if (dspace < 0) {
		fprintf(stderr, "failed to get user attributes dspace");
		rc = 1;
		goto out;
	}
	vtype = H5Dget_type(dset);
	if (vtype < 0) {
		fprintf(stderr, "failed to get user attributes vtype");
		rc = 1;
		goto out;
	}
	num_dims = H5Sget_simple_extent_dims(dspace, dims, NULL);
	if (num_dims < 0) {
		fprintf(stderr, "failed to get user attributes dimensions");
		rc = 1;
		goto out;
	}
	num_attrs = dims[0];
	attr_data = calloc(dims[0], sizeof(usr_attr_t));
	if (attr_data == NULL) {
		fprintf(stderr, "failed to allocate user attributes");
		rc = 1;
		goto out;
	}
	status = H5Dread(dset, vtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, attr_data);
	if (status < 0) {
		fprintf(stderr, "failed to read user attributes data");
		rc = 1;
		goto out;
	}
	names = calloc(num_attrs, sizeof(char *));
	if (!names) {
		fprintf(stderr, "failed to allocate user attributes");
		rc = 1;
		goto out;
	}
	buffers = calloc(num_attrs, sizeof(void *));
	if (!buffers) {
		fprintf(stderr, "failed to allocate user attributes");
		rc = 1;
		goto out;
	}
	sizes = calloc(num_attrs, sizeof(size_t));
	if (!sizes) {
		fprintf(stderr, "failed to allocate user attributes");
		rc = 1;
		goto out;
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
	H5Dclose(dset);
	H5Sclose(dspace);
	H5Tclose(vtype);
	free(attr_data);
	return rc;
}


int
deserialize_daos_cont_attrs_metadata(char *filename, uint64_t *num_attrs,
				     char ***names, void ***buffers,
				     size_t **sizes)
{
	int			rc = 0;
	hid_t			file_id;
	htri_t			usr_attrs_exist;


	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		fprintf(stderr, "failed to open hdf5 file");
		rc = 1;
		goto out;
	}
	usr_attrs_exist = H5Lexists(file_id, "User Attributes", H5P_DEFAULT);
	if (usr_attrs_exist > 0) {
		rc = cont_deserialize_usr_attrs(&file_id, num_attrs, names,
						buffers, sizes);
		if (rc != 0) {
			fprintf(stderr, "failed to deserialize usr attrs: "
				""DF_RC, DP_RC(rc));
			goto out;
		}
	}
out:
	H5Fclose(file_id);
	return rc;
}

