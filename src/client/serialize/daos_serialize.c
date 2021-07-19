#include <stdio.h>
#include <gurt/common.h>
#include <hdf5.h>
#include <daos.h>
#include <daos_cont.h>

#define SERIALIZE_ATTR_DSET "User Attributes"

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
		D_GOTO(out, rc = -DER_INVAL);
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
		D_ERROR("failed to create attribute datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		D_ERROR("failed to set attribute datatype size\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		D_ERROR("failed to create dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		D_ERROR("failed to create attribute\n");
		D_GOTO(out, rc = -DER_MISC);
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
		D_GOTO(out, rc = -DER_INVAL);
	}

	attr_dims[0] = 1;
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (attr_dtype < 0) {
		D_ERROR("failed to create datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tset_size(attr_dtype, strlen(entry->dpe_str) + 1);
	if (status < 0) {
		D_ERROR("failed to set datatype size\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tset_strpad(attr_dtype, H5T_STR_NULLTERM);
	if (status < 0) {
		D_ERROR("failed to set string pad on datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		D_ERROR("failed to create dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype, attr_dspace,
			      H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		D_ERROR("failed to create attribute\n");
		D_GOTO(out, rc = -DER_MISC);
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
serialize_uint(hid_t file_id, uint64_t val, const char *prop_str)
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
		D_ERROR("failed to attribute datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		D_ERROR("failed to create dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype,
			      attr_dspace, H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		D_ERROR("failed to create attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Awrite(usr_attr, attr_dtype, &val);
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

static char*
prop_to_str(uint32_t type)
{
	switch (type) {
	case DAOS_PROP_CO_LABEL:
		return "DAOS_PROP_CO_LABEL";
	case DAOS_PROP_CO_OWNER:
		return "DAOS_PROP_CO_OWNER";
	case DAOS_PROP_CO_OWNER_GROUP:
		return "DAOS_PROP_CO_OWNER_GROUP";
	case DAOS_PROP_CO_ACL:
		return "DAOS_PROP_CO_ACL";
	case DAOS_PROP_CO_LAYOUT_TYPE:
		return "DAOS_PROP_CO_LAYOUT_TYPE";
	case DAOS_PROP_CO_LAYOUT_VER:
		return "DAOS_PROP_CO_LAYOUT_VER";
	case DAOS_PROP_CO_CSUM:
		return "DAOS_PROP_CO_CSUM";
	case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
		return "DAOS_PROP_CO_CSUM_CHUNK_SIZE";
	case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
		return "DAOS_PROP_CO_CSUM_SERVER_VERIFY";
	case DAOS_PROP_CO_REDUN_FAC:
		return "DAOS_PROP_CO_REDUN_FAC";
	case DAOS_PROP_CO_REDUN_LVL:
		return "DAOS_PROP_CO_REDUN_LVL";
	case DAOS_PROP_CO_SNAPSHOT_MAX:
		return "DAOS_PROP_CO_SNAPSHOT_MAX";
	case DAOS_PROP_CO_COMPRESS:
		return "DAOS_PROP_CO_COMPRESS";
	case DAOS_PROP_CO_ENCRYPT:
		return "DAOS_PROP_CO_ENCRYPT";
	case DAOS_PROP_CO_DEDUP:
		return "DAOS_PROP_CO_DEDUP";
	case DAOS_PROP_CO_DEDUP_THRESHOLD:
		return "DAOS_PROP_CO_DEDUP_THRESHOLD";
	case DAOS_PROP_CO_ALLOCED_OID:
		return "DAOS_PROP_CO_ALLOCED_OID";
	default:
		return "PROPERTY NOT SUPPORTED";
	}
}

int
daos_cont_serialize_props(hid_t file_id, daos_prop_t *prop_query)
{
	int			rc = 0;
	struct daos_prop_entry	*entry;
	int			i = 0;
	uint32_t		type;
	char			*prop_str;

	if (prop_query == NULL) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* serialize number of props written */
	rc = serialize_uint(file_id, prop_query->dpp_nr, "NUM_PROPS");
	if (rc != 0) {
		D_GOTO(out, rc);
	}
	for (i = 0; i < prop_query->dpp_nr; i++) {
		type =	prop_query->dpp_entries[i].dpe_type;
		prop_str = prop_to_str(type);
		if (type == DAOS_PROP_CO_LABEL ||
		    type == DAOS_PROP_CO_OWNER ||
		    type == DAOS_PROP_CO_OWNER_GROUP) {
			entry = &prop_query->dpp_entries[i];
			rc = serialize_str(file_id, entry, prop_str);
			if (rc != 0) {
				D_GOTO(out, rc);
			}
		} else if (type == DAOS_PROP_CO_ACL) {
			entry = &prop_query->dpp_entries[i];
			rc = serialize_acl(file_id, entry, prop_str);
			if (rc != 0) {
				D_GOTO(out, rc);
			}
		} else if (type == DAOS_PROP_CO_LAYOUT_TYPE ||
			   type == DAOS_PROP_CO_LAYOUT_VER ||
			   type == DAOS_PROP_CO_CSUM ||
			   type == DAOS_PROP_CO_CSUM_CHUNK_SIZE ||
			   type == DAOS_PROP_CO_CSUM_SERVER_VERIFY ||
			   type == DAOS_PROP_CO_REDUN_FAC ||
			   type == DAOS_PROP_CO_REDUN_LVL ||
			   type == DAOS_PROP_CO_SNAPSHOT_MAX ||
			   type == DAOS_PROP_CO_COMPRESS ||
			   type == DAOS_PROP_CO_ENCRYPT ||
			   type == DAOS_PROP_CO_DEDUP ||
			   type == DAOS_PROP_CO_DEDUP_THRESHOLD ||
			   type == DAOS_PROP_CO_ALLOCED_OID) {
			entry = &prop_query->dpp_entries[i];
			rc = serialize_uint(file_id, entry->dpe_val,
					    prop_str);
			if (rc != 0) {
				D_GOTO(out, rc);
			}
		} else {
			rc = -DER_INVAL;
			D_ERROR("Serialization of this container property "
				"%s type is not supported "DF_RC"\n",
				prop_str, DP_RC(rc));
		}
	}
out:
	return rc;
}

int
daos_cont_serialize_attrs(hid_t file_id, hid_t *usr_attr_memtype,
			  int num_attrs, char **names, char **buffers,
			  size_t *sizes)
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

	if (names == NULL || buffers == NULL || sizes == NULL) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Create the user attribute data space */
	dims[0] = num_attrs;
	dspace = H5Screate_simple(1, dims, NULL);
	if (dspace < 0) {
		D_ERROR("failed to create dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	/* Create the user attribute dataset */
	dset = H5Dcreate(file_id, SERIALIZE_ATTR_DSET, *usr_attr_memtype,
			 dspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dset < 0) {
		D_ERROR("failed to create dataset\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	/* Allocate space for all attributes */
	D_ALLOC(attr_data, num_attrs * sizeof(struct usr_attr));
	if (attr_data == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
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
		D_ERROR("failed to write to dataset\n");
		D_GOTO(out, rc = -DER_MISC);
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
daos_cont_serialize_md(char *filename, daos_prop_t *props, int num_attrs,
		       char **names, char **buffers, size_t *sizes)
{
	int	rc = 0;
	hid_t	status;
	hid_t	file_id = 0;
	hid_t	usr_attr_memtype = 0;
	hid_t	usr_attr_name_vtype = 0;
	hid_t	usr_attr_val_vtype = 0;

	if (filename == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_PRINT("Writing metadata file: %s\n", filename);

	file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file_id < 0) {
		D_ERROR("failed to write to metadata file: %s\n", filename);
		D_GOTO(out, rc = -DER_MISC);
	}
	rc = daos_cont_serialize_props(file_id, props);
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
			D_ERROR("failed to create memory datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}

		usr_attr_name_vtype = H5Tcopy(H5T_C_S1);
		if (usr_attr_name_vtype < 0) {
			D_ERROR("failed to create variable datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tset_size(usr_attr_name_vtype, H5T_VARIABLE);
		if (status < 0) {
			D_ERROR("failed to set datatype size\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		usr_attr_val_vtype = H5Tvlen_create(H5T_NATIVE_OPAQUE);
		if (usr_attr_val_vtype < 0) {
			D_ERROR("failed to variable length type\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Name",
				   HOFFSET(struct usr_attr, attr_name),
				   usr_attr_name_vtype);
		if (status < 0) {
			D_ERROR("failed to insert into compound datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Value",
				   HOFFSET(struct usr_attr, attr_val),
				   usr_attr_val_vtype);
		if (status < 0) {
			D_ERROR("failed to insert into compound datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rc = daos_cont_serialize_attrs(file_id, &usr_attr_memtype,
					       num_attrs, names, buffers,
					       sizes);
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
		D_ERROR("failed to open attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		D_ERROR("failed to get attribute datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	buf_size = H5Tget_size(attr_dtype);
	if (buf_size <= 0) {
		D_ERROR("failed to get size of datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	D_ALLOC(entry->dpe_str, buf_size);
	if (entry->dpe_str == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
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
deserialize_uint(hid_t file_id, uint64_t *val, const char *prop_str)
{
	hid_t   status = 0;
	int     rc = 0;
	hid_t   cont_attr = 0;
	hid_t   attr_dtype = 0;

	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		D_ERROR("failed to open attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		D_ERROR("failed to get attribute datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Aread(cont_attr, attr_dtype, val);
	if (status < 0) {
		D_ERROR("failed to read attribute\n");
		D_GOTO(out, rc = -DER_MISC);
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
		D_ERROR("failed to check if attribute exists\n");
		D_GOTO(out, rc = -DER_MISC);
	} else if (acl_exist == 0) {
		/* Does not exist, but that's okay. */
		D_GOTO(out, rc = 0);
	}

	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		/* Could not open, but that's okay. */
		D_GOTO(out, rc = 0);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		D_ERROR("failed to get attribute datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dspace = H5Aget_space(cont_attr);
	if (status < 0) {
		D_ERROR("failed to get dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	ndims = H5Sget_simple_extent_dims(attr_dspace, attr_dims, NULL);
	if (ndims < 0) {
		D_ERROR("failed to get number of dimensions\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	D_ALLOC(rdata, attr_dims[0] * sizeof(char *));
	if (rdata == NULL) {
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dtype = H5Tcopy(H5T_C_S1);
	if (status < 0) {
		D_ERROR("failed to copy attribute datatype size\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tset_size(attr_dtype, H5T_VARIABLE);
	if (status < 0) {
		D_ERROR("failed to set attribute datatype size\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Aread(cont_attr, attr_dtype, rdata);
	if (status < 0) {
		D_ERROR("failed to read attribute\n");
		D_GOTO(out, rc = -DER_MISC);
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
	uint64_t		total_props = 0;
	daos_prop_t		*label = NULL;
	daos_prop_t		*prop = NULL;
	struct daos_prop_entry	*entry;
	struct daos_prop_entry	*label_entry = NULL;
	daos_handle_t		coh;
	daos_cont_info_t	cont_info = {0};
	int			prop_num = 0;
	uint32_t		type;

	/* try to read the 17 (including label) properties that are supported
	 * for serialization. If property is not found in file it is skipped.
	 */

	if (H5Aexists(file_id, "DAOS_PROP_CO_LABEL") > 0) {
		label = daos_prop_alloc(1);
		if (label == NULL) {
			return ENOMEM;
		}
		label->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;

		/* read the container label entry to decide if it should be
		 * added to property list. The container label is required to
		 * be unique, which is why it is handled differently than the
		 * other container properties. If the label already exists in
		 * the pool then this property will be skipped for
		 * deserialization
		 */
		label_entry = &label->dpp_entries[0];
		rc = deserialize_str(file_id, label_entry,
				     "DAOS_PROP_CO_LABEL");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(poh, label_entry->dpe_str, DAOS_COO_RW,
				    &coh, &cont_info, NULL);
		if (rc == -DER_NONEXIST) {
			/* label doesn't already exist so deserialize */
			deserialize_label = true;
			close_cont = false;
		} else if (rc != 0) {
			D_GOTO(out, rc);
		}  else {
			D_PRINT("Container label cannot be set, "
				"the label already exists in pool\n");
		}
	}

	/* this is always written/read */
	rc = deserialize_uint(file_id, &total_props, "NUM_PROPS");
	if (rc != 0) {
		D_GOTO(out, rc);
	}

	prop = daos_prop_alloc(total_props);
	if (prop == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

	if (H5Aexists(file_id, "DAOS_PROP_CO_LAYOUT_TYPE") > 0) {
		type = DAOS_PROP_CO_LAYOUT_TYPE;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_LAYOUT_TYPE");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		*cont_type = prop->dpp_entries[prop_num].dpe_val;
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_LAYOUT_VER") > 0) {
		type = DAOS_PROP_CO_LAYOUT_VER;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_LAYOUT_VER");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_CSUM") > 0) {
		type = DAOS_PROP_CO_CSUM;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_CSUM");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_CSUM_CHUNK_SIZE") > 0) {
		type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_CSUM_CHUNK_SIZE");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_CSUM_SERVER_VERIFY") > 0) {
		type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_CSUM_SERVER_VERIFY");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_REDUN_FAC") > 0) {
		type = DAOS_PROP_CO_REDUN_FAC;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_REDUN_FAC");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_REDUN_LVL") > 0) {
		type = DAOS_PROP_CO_REDUN_LVL;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_REDUN_LVL");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_SNAPSHOT_MAX") > 0) {
		type = DAOS_PROP_CO_SNAPSHOT_MAX;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_SNAPSHOT_MAX");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_COMPRESS") > 0) {
		type = DAOS_PROP_CO_COMPRESS;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_COMPRESS");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_ENCRYPT") > 0) {
		type = DAOS_PROP_CO_ENCRYPT;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_ENCRYPT");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_OWNER") > 0) {
		type = DAOS_PROP_CO_OWNER;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_str(file_id, entry, "DAOS_PROP_CO_OWNER");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_OWNER_GROUP") > 0) {
		type = DAOS_PROP_CO_OWNER_GROUP;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_str(file_id, entry,
				     "DAOS_PROP_CO_OWNER_GROUP");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_DEDUP") > 0) {
		type = DAOS_PROP_CO_DEDUP;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_DEDUP");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_DEDUP_THRESHOLD") > 0) {
		type = DAOS_PROP_CO_DEDUP_THRESHOLD;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_DEDUP_THRESHOLD");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_ALLOCED_OID") > 0) {
		type = DAOS_PROP_CO_ALLOCED_OID;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_ALLOCED_OID");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_ACL") > 0) {
		type = DAOS_PROP_CO_ACL;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		/* read acl as a list of strings in deserialize, then
		 * convert back to acl for property entry
		 */
		rc = deserialize_acl(file_id, entry, "DAOS_PROP_CO_ACL");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	/* deserialize_label stays false if property doesn't exist above */
	if (deserialize_label) {
		type = DAOS_PROP_CO_LABEL;
		prop->dpp_entries[prop_num].dpe_type = type;
		prop->dpp_entries[prop_num].dpe_str =
						strdup(label_entry->dpe_str);
	}
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

	if (filename == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_PRINT("Reading metadata file: %s\n", filename);

	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		D_ERROR("failed to open metadata file: %s\n", filename);
		D_GOTO(out, rc = -DER_MISC);
	}
	rc = deserialize_props(poh, file_id, props, cont_type);
	if (rc != 0) {
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
		D_ERROR("failed to open User Attributes Dataset\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	dspace = H5Dget_space(dset);
	if (dspace < 0) {
		D_ERROR("failed to get dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	vtype = H5Dget_type(dset);
	if (vtype < 0) {
		D_ERROR("failed to get datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	num_dims = H5Sget_simple_extent_dims(dspace, dims, NULL);
	if (num_dims < 0) {
		D_ERROR("failed to get number of dimensions\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	num_attrs = dims[0];
	D_ALLOC(attr_data, dims[0] * sizeof(struct usr_attr));
	if (attr_data == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	status = H5Dread(dset, vtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, attr_data);
	if (status < 0) {
		D_ERROR("failed to read attribute dataset\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	D_ALLOC(names, num_attrs * sizeof(char *));
	if (names == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	D_ALLOC(buffers, num_attrs * sizeof(void *));
	if (buffers == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	D_ALLOC(sizes, num_attrs * sizeof(size_t));
	if (sizes == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
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


	if (filename == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		D_ERROR("failed to open metadata file: %s\n", filename);
		D_GOTO(out, rc = -DER_MISC);
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

