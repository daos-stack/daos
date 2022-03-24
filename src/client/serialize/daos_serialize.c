#include <stdio.h>
#include <gurt/common.h>
#include <hdf5.h>
#include <daos.h>
#include <daos_cont.h>

#define SERIALIZE_ATTR_DSET "User Attributes"

/* TODO: need to fix HDF5 dependency issue */

/* TODO: determine reasonable sizes */
#define DSR_OID_BATCH_SIZE 8	/**< Number of OIDs fetched per batch */
#define DSR_DKEY_BATCH_SIZE 8	/**< Number of dkeys fetched per batch */
#define DSR_AKEY_BATCH_SIZE 8	/**< Number of akey fetched per batch */

#define DSR_KEY_BUF_LEN 512	/**< Default key buf length */

/* TODO: mostly needs to be removed with dynamic key buffers */
#define ENUM_KEY_BUF		128	/**< size of each dkey/akey */
#define ATTR_NAME_LEN		128	/**< max HDF5 attribute length */

/* TODO: define a major INT and a minor INT */
#define SERIALIZE_VERSION	0.0	/**< version of the serialization layout */

/* TODO: treat public (non-static) functions as API, respecting ABI breakage rules */

#if defined(__cplusplus)
extern "C" {
#endif

/* for user attr dataset */
struct dsr_h5_usr_attr {
	char	*attr_name;
	hvl_t	attr_val;
};

/* for oid dataset */
struct dsr_h5_oid {
	uint64_t oid_hi;
	uint64_t oid_low;
	uint64_t dkey_offset;
};

/* for dkey dataset */
struct dsr_h5_dkey {
	/* array of vlen structure */
	hvl_t dkey_val;
	uint64_t akey_offset;
	/* for for kv values that can be
	 * written with daos_kv.h
	 */
	hvl_t rec_kv_val;
};

/* for akey dataset */
struct dsr_h5_akey {
	/* array of vlen structure */
	hvl_t akey_val;
	uint64_t rec_dset_id;
	hvl_t rec_single_val;
};

/* TODO: remove after deserialization is refactored */
struct dsr_h5_args {
	hid_t file;
	/* data for keys */
	struct dsr_h5_dkey *dkey_data;
	struct dsr_h5_akey *akey_data;
};

static int
serialize_roots(hid_t file_id, struct daos_prop_entry *entry, const char *prop_str)
{
	int				rc = 0;
	hid_t				status = 0;
	struct daos_prop_co_roots	*roots;
	hsize_t				attr_dims[1];
	/* HDF5 returns non-negative identifiers if successfully opened/created */
	hid_t				attr_dtype = -1;
	hid_t				attr_dspace = -1;
	hid_t				usr_attr = -1;

	if (entry == NULL || entry->dpe_val_ptr == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	roots = entry->dpe_val_ptr;
	attr_dims[0] = 4;

	attr_dtype = H5Tcreate(H5T_COMPOUND, sizeof(daos_obj_id_t));
	if (attr_dtype < 0) {
		D_ERROR("failed to create attribute datatype");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tinsert(attr_dtype, "lo", HOFFSET(daos_obj_id_t, lo), H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("failed to insert oid low");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Tinsert(attr_dtype, "hi", HOFFSET(daos_obj_id_t, hi), H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("failed to insert oid high");
		D_GOTO(out, rc = -DER_MISC);
	}

	attr_dspace = H5Screate_simple(1, attr_dims, NULL);
	if (attr_dspace < 0) {
		D_ERROR("failed to create attribute dataspace");
		D_GOTO(out, rc = -DER_MISC);
	}
	usr_attr = H5Acreate2(file_id, prop_str, attr_dtype, attr_dspace, H5P_DEFAULT, H5P_DEFAULT);
	if (usr_attr < 0) {
		D_ERROR("failed to create attribute");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Awrite(usr_attr, attr_dtype, roots->cr_oids);
	if (status < 0) {
		D_ERROR("failed to write attribute");
		D_GOTO(out, rc = -DER_MISC);
	}
out:
	if (usr_attr >= 0)
		H5Aclose(usr_attr);
	if (attr_dtype >= 0)
		H5Tclose(attr_dtype);
	if (attr_dspace >= 0)
		H5Sclose(attr_dspace);
	return rc;
}

static int
serialize_acl(hid_t file_id, struct daos_prop_entry *entry, const char *prop_str)
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
serialize_str(hid_t file_id, struct daos_prop_entry *entry, const char *prop_str)
{

	int	rc = 0;
	hid_t	status = 0;
	hid_t	attr_dtype = 0;
	hid_t	attr_dspace = 0;
	hid_t	usr_attr = 0;

	if (entry == NULL || entry->dpe_str == NULL) {
		D_GOTO(out, rc = -DER_INVAL);
	}

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
	attr_dspace = H5Screate(H5S_SCALAR);
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
	hid_t	attr_dtype = H5T_NATIVE_UINT64;
	hid_t	attr_dspace = 0;
	hid_t	usr_attr = 0;

	attr_dspace = H5Screate(H5S_SCALAR);
	if (attr_dspace < 0) {
		D_ERROR("failed to create attribute dataspace\n");
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
		D_ERROR("failed to write attribute "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	/* close hdf5 objects */
	if (usr_attr > 0)
		H5Aclose(usr_attr);
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
	case DAOS_PROP_CO_EC_CELL_SZ:
		return "DAOS_PROP_CO_EC_CELL_SZ";
	case DAOS_PROP_CO_EC_PDA:
		return "DAOS_PROP_CO_EC_PDA";
	case DAOS_PROP_CO_RP_PDA:
		return "DAOS_PROP_CO_RP_PDA";
	case DAOS_PROP_CO_ROOTS:
		return "DAOS_PROP_CO_ROOTS";
	default:
		return "PROPERTY NOT SUPPORTED";
	}
}

static int
simple_extend_write(hid_t file, char *dset_name, void *data, uint64_t data_len)
{
	int			rc = 0;
	hid_t			status = 0;
	hsize_t			oid_dims_old[1];
	hsize_t			oid_dims_new[1];
	int			oid_ndims;
	hid_t			oid_dspace = -1;
	hid_t			oid_dtype = -1;
	hid_t			oid_dset = -1;
	hid_t			oid_mspace = -1;
	hsize_t			write_offset;
	hsize_t			write_len;

	oid_dset = H5Dopen(file, dset_name, H5P_DEFAULT);
	if (oid_dset < 0) {
		D_ERROR("Failed to open '%s' Dataset\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_dspace = H5Dget_space(oid_dset);
	if (oid_dspace < 0) {
		D_ERROR("Failed to get '%s' Dataspace\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_ndims = H5Sget_simple_extent_dims(oid_dspace, oid_dims_old, NULL);
	if (oid_ndims < 0) {
		D_ERROR("Failed to get '%s' Dimensions\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	/* extend dataset */
	oid_dims_new[0] = oid_dims_old[0] + data_len;
	status = H5Dset_extent(oid_dset, oid_dims_new);
	if (status < 0) {
		D_ERROR("Failed to extend '%s'\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	/* Must close and reopen space after extending */
	status = H5Sclose(oid_dspace);
	if (status < 0) {
		D_ERROR("Failed to close '%s' Dataspace\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}
	oid_dspace = H5Dget_space(oid_dset);
	if (oid_dspace < 0) {
		D_ERROR("Failed to get '%s'' Dataspace\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	write_offset = oid_dims_old[0];
	write_len = data_len;
	status = H5Sselect_hyperslab(oid_dspace, H5S_SELECT_SET, &write_offset, NULL, &write_len, NULL);
	if (status < 0) {
		D_ERROR("Failed to select '%s' Dataset Hyperslab\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_mspace = H5Screate_simple(1, &write_len, &write_len);
	if (oid_mspace < 0) {
		D_ERROR("Failed to create '%s' Memspace\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_dtype = H5Dget_type(oid_dset);
	if (oid_dtype < 0) {
		D_ERROR("Failed to get '%s' Datatype\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Dwrite(oid_dset, oid_dtype, oid_mspace, oid_dspace, H5P_DEFAULT, data);
	if (status < 0) {
		D_ERROR("Failed to write '%s' Dataset\n", dset_name);
		D_GOTO(out, rc = -DER_MISC);
	}

out:
	if (oid_mspace >= 0)
		H5Sclose(oid_mspace);
	if (oid_dspace >= 0)
		H5Sclose(oid_dspace);
	if (oid_dtype >= 0)
		H5Tclose(oid_dtype);
	if (oid_dset >= 0)
		H5Dclose(oid_dset);
	return rc;
}

static int
write_oids(hid_t file, struct dsr_h5_oid *oid_data, uint64_t oid_nr)
{
	int rc;

	rc = simple_extend_write(file, "Oid Data", (void *)oid_data, oid_nr);
	if (rc)
		D_ERROR("Failed to write OID Data "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
write_dkeys(hid_t file, struct dsr_h5_dkey *dkey_data, uint64_t dkey_nr)
{
	int rc;

	rc = simple_extend_write(file, "Dkey Data", (void *)dkey_data, dkey_nr);
	if (rc)
		D_ERROR("Failed to write Dkey Data "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
write_akeys(hid_t file, struct dsr_h5_akey *akey_data, uint64_t akey_nr)
{
	int rc;

	rc = simple_extend_write(file, "Akey Data", (void *)akey_data, akey_nr);
	if (rc)
		D_ERROR("Failed to write Akey Data "DF_RC"\n", DP_RC(rc));

	return rc;
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
		} else if (type == DAOS_PROP_CO_ROOTS) {
			entry = &prop_query->dpp_entries[i];
			rc = serialize_roots(file_id, entry, prop_str);
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
			   type == DAOS_PROP_CO_EC_CELL_SZ ||
			   type == DAOS_PROP_CO_EC_PDA ||
			   type == DAOS_PROP_CO_RP_PDA ||
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

static int
daos_cont_serialize_attrs(hid_t file_id, hid_t *usr_attr_memtype,
			  int num_attrs, char **names, char **buffers,
			  size_t *sizes)
{
	int		rc = 0;
	hid_t		status = 0;
	hid_t		dset = 0;
	hid_t		dspace = 0;
	hsize_t		dims[1];
	struct dsr_h5_usr_attr	*attr_data = NULL;
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
	D_ALLOC(attr_data, num_attrs * sizeof(struct dsr_h5_usr_attr));
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

	D_PRINT("Writing metadata to: %s\n", filename);

	file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file_id < 0) {
		D_ERROR("failed to create metadata file: %s\n", filename);
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
					     sizeof(struct dsr_h5_usr_attr));
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
			D_ERROR("failed to create variable length type\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Name",
				   HOFFSET(struct dsr_h5_usr_attr, attr_name),
				   usr_attr_name_vtype);
		if (status < 0) {
			D_ERROR("failed to insert into compound datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Value",
				   HOFFSET(struct dsr_h5_usr_attr, attr_val),
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
deserialize_str(hid_t file_id, char **str, const char *prop_str)
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

	D_ALLOC(*str, buf_size);
	if (*str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	status = H5Aread(cont_attr, attr_dtype, *str);
	if (status < 0) {
		rc = -DER_IO;
		D_ERROR("failed to read property attribute %s "DF_RC"\n", *str, DP_RC(rc));
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
deserialize_roots(hid_t file_id, struct daos_prop_entry *entry, const char *prop_str)
{
	hid_t				status = 0;
	int				rc = 0;
	int				ndims = 0;
	htri_t				roots_exist;
	hid_t				cont_attr = -1;
	hid_t				attr_dtype = -1;
	hid_t				attr_dspace = -1;
	hsize_t				attr_dims[1];
	size_t				attr_dtype_size;
	struct daos_prop_co_roots	*roots = NULL;

	/* First check if the roots attribute exists. */
	roots_exist = H5Aexists(file_id, prop_str);
	if (roots_exist < 0) {
		/* Actual error  */
		D_ERROR("failed to check if attribute exists\n");
		D_GOTO(out, rc = -DER_MISC);
	} else if (roots_exist == 0) {
		/* Does not exist, but that's okay. */
		D_GOTO(out, rc = 0);
	}
	cont_attr = H5Aopen(file_id, prop_str, H5P_DEFAULT);
	if (cont_attr < 0) {
		/* Should be able to open attribute if it exists */
		D_ERROR("failed to open attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dtype = H5Aget_type(cont_attr);
	if (attr_dtype < 0) {
		D_ERROR("failed to get attribute type\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dtype_size = H5Tget_size(attr_dtype);
	if (attr_dtype_size < 0) {
		D_ERROR("failed to get attribute type size\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	attr_dspace = H5Aget_space(cont_attr);
	if (attr_dspace < 0) {
		D_ERROR("failed to get attribute dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	ndims = H5Sget_simple_extent_dims(attr_dspace, attr_dims, NULL);
	if (ndims < 0) {
		D_ERROR("failed to get dimensions of dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	D_ALLOC_PTR(roots);
	if (roots == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	status = H5Aread(cont_attr, attr_dtype, roots->cr_oids);
	if (status < 0) {
		D_ERROR("failed to read property attribute %s\n", prop_str);
		D_GOTO(out, rc = -DER_MISC);
	}
	entry->dpe_val_ptr = roots;
out:
	if (rc != 0)
		D_FREE(roots);
	if (cont_attr >= 0)
		status = H5Aclose(cont_attr);
	if (attr_dtype >= 0)
		status = H5Tclose(attr_dtype);
	if (attr_dspace >= 0)
		status = H5Sclose(attr_dspace);
	return rc;
}

static int
deserialize_acl(hid_t file_id, struct daos_prop_entry *entry, const char *prop_str)
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
deserialize_props(daos_handle_t poh, hid_t file_id, daos_prop_t **_prop, uint64_t *cont_type)
{
	int			rc = 0;
	bool			deserialize_label = false;
	bool			close_cont = true;
	uint64_t		total_props = 0;
	char			*label = NULL;
	daos_prop_t		*prop = NULL;
	struct daos_prop_entry	*entry;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	daos_cont_info_t	cont_info = {0};
	int			prop_num = 0;
	uint32_t		type;

	/* try to read the 17 (including label) properties that are supported
	 * for serialization. If property is not found in file it is skipped.
	 */

	if (H5Aexists(file_id, "DAOS_PROP_CO_LABEL") > 0) {
		/* read the container label entry to decide if it should be added to property
		 * list. The container label is required to be unique, which is why it is handled
		 * differently than the other container properties. If the label already exists in
		 * the pool then this property will be skipped for deserialization
		 */
		rc = deserialize_str(file_id, &label, "DAOS_PROP_CO_LABEL");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(poh, label, DAOS_COO_RW, &coh, &cont_info, NULL);
		if (rc == -DER_NONEXIST) {
			/* label doesn't already exist so deserialize */
			deserialize_label = true;
			close_cont = false;
			/* reset rc */
			rc = 0;
		} else if (rc != 0) {
			D_GOTO(out, rc);
		}  else {
			D_PRINT("Container label already exists in pool and cannot be set\n");
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
		char *owner = NULL;

		rc = deserialize_str(file_id, &owner, "DAOS_PROP_CO_OWNER");
		if (rc != 0) {
			D_GOTO(out, rc);
		}

		prop->dpp_entries[prop_num].dpe_type = DAOS_PROP_CO_OWNER;
		rc = daos_prop_entry_set_str(&prop->dpp_entries[prop_num], owner, strlen(owner));
		if (rc)
			D_GOTO(out, rc);
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_OWNER_GROUP") > 0) {
		char *group = NULL;

		rc = deserialize_str(file_id, &group, "DAOS_PROP_CO_OWNER_GROUP");
		if (rc != 0) {
			D_GOTO(out, rc);
		}

		prop->dpp_entries[prop_num].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		rc = daos_prop_entry_set_str(&prop->dpp_entries[prop_num], group, strlen(group));
		if (rc)
			D_GOTO(out, rc);
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
	if (H5Aexists(file_id, "DAOS_PROP_CO_ROOTS") > 0) {
		type = DAOS_PROP_CO_ROOTS;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_roots(file_id, entry, "DAOS_PROP_CO_ROOTS");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_EC_CELL_SZ") > 0) {
		type = DAOS_PROP_CO_EC_CELL_SZ;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val, "DAOS_PROP_CO_EC_CELL_SZ");
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_EC_PDA") > 0) {
		type = DAOS_PROP_CO_EC_PDA;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_EC_PDA");
		if (rc != 0)
			D_GOTO(out, rc);
		prop_num++;
	}
	if (H5Aexists(file_id, "DAOS_PROP_CO_RP_PDA") > 0) {
		type = DAOS_PROP_CO_RP_PDA;
		prop->dpp_entries[prop_num].dpe_type = type;
		entry = &prop->dpp_entries[prop_num];
		rc = deserialize_uint(file_id, &entry->dpe_val,
				      "DAOS_PROP_CO_RP_PDA");
		if (rc != 0)
			D_GOTO(out, rc);
		prop_num++;
	}
	/* deserialize_label stays false if property doesn't exist above */
	if (deserialize_label) {
		prop->dpp_entries[prop_num].dpe_type = DAOS_PROP_CO_LABEL;
		rc = daos_prop_entry_set_str(&prop->dpp_entries[prop_num], label, strlen(label));
		if (rc)
			D_GOTO(out, rc);
	}
	*_prop = prop;
out:
	/* close container after checking if label exists in pool */
	if (close_cont)
		daos_cont_close(coh, NULL);
	if (rc != 0 && prop != NULL)
		daos_prop_free(prop);
	D_FREE(label);
	return rc;
}

int
daos_cont_deserialize_props(daos_handle_t poh, char *filename,
			    daos_prop_t **props, uint64_t *cont_type)
{
	int	rc = 0;
	hid_t	file_id = -1;

	if (filename == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_PRINT("Reading metadata file from: %s\n", filename);

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
	if (file_id >= 0)
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
	struct dsr_h5_usr_attr	*attr_data = NULL;
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
	D_ALLOC(attr_data, dims[0] * sizeof(struct dsr_h5_usr_attr));
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
	if (rc != 0) {
		D_FREE(names);
		D_FREE(buffers);
		D_FREE(sizes);
	}
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

static int
fetch_recx_single(hvl_t *single_val, daos_key_t *dkey, daos_handle_t *oh, daos_iod_t *iod,
		  uint64_t *bytes_read)
{
	/* if iod_type is single value just fetch iod size from source
	 * and update in destination object
	 */
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		rc;

	D_ALLOC(single_val->p, iod->iod_size);
	if (single_val->p == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	single_val->len = iod->iod_size;

	/* set sgl values */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	d_iov_set(&iov, single_val->p, single_val->len);
	rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL, NULL);
	if (rc != 0) {
		D_ERROR("failed to fetch object "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Sanity check */
	if (sgl.sg_nr_out != 1) {
		D_ERROR("failed to fetch single recx "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	(*bytes_read) += single_val->len;

out:
	if (rc)
		D_FREE(single_val->p);
	return rc;
}

static int
serialize_recx_array(hid_t file, daos_key_t *dkey, daos_key_t *akey, char *rec_name,
		     int *akey_index, daos_handle_t *oh, daos_iod_t *iod, uint64_t *bytes_read)
{
	int			rc = 0;
	hid_t			status = 0;
	int			i = 0;
	int			attr_num = 0;
	int			buf_len = 0;
	int			path_len = 0;
	uint32_t		number = 5;
	size_t			nalloc = 0;
	daos_anchor_t		recx_anchor = {0};
	daos_anchor_t		fetch_anchor = {0};
	daos_epoch_range_t	eprs[5] = {0};
	daos_recx_t		recxs[5] = {0};
	daos_size_t		size = 0;
	char			attr_name[ATTR_NAME_LEN] = {0};
	char			attr_num_str[ATTR_NAME_LEN] = {0};
	unsigned char		*encode_buf = NULL;
	d_sg_list_t		sgl = {0};
	d_iov_t			iov = {0};
	char			*buf = NULL;
	herr_t			err = 0;
	hid_t			rx_dset = -1;
	hid_t			selection_attr = -1;
	hid_t			rx_dtype = -1;
	hid_t			rx_dspace = -1;
	hid_t			rx_memspace = -1;
	hid_t			attr_dspace = -1;
	hid_t			attr_dtype = -1;
	hid_t			plist = -1;
	hsize_t			rx_dims[1];
	hsize_t			rx_max_dims[1];
	hsize_t			rx_chunk_dims[1];
	hsize_t			mem_dims[1];
	hsize_t			attr_dims[1];
	hsize_t			start;
	hsize_t			count;

	rx_dims[0] = 0;
	rx_max_dims[0] = H5S_UNLIMITED;

	/* TODO: consider other chunk sizes or possibly use different
	 * chunk sizes for different dkeys/akeys.
	 * TODO: use a #define for chunk size
	 */
	rx_chunk_dims[0] = 1024;

	plist = H5Pcreate(H5P_DATASET_CREATE);
	if (plist < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create property list "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rx_dspace = H5Screate_simple(1, rx_dims, rx_max_dims);
	if (rx_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create rx_dspace "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_layout(plist, H5D_CHUNKED);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set rx_dspace layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_chunk(plist, 1, rx_chunk_dims);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set rx_dspace chunk "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* need to do a fetch for size, so that we can
	 * create the dataset with the correct datatype size
	 */
	number = 1;
	rc = daos_obj_list_recx(*oh, DAOS_TX_NONE, dkey, akey, &size, &number, NULL, eprs,
				&fetch_anchor, true, NULL);
	if (rc != 0) {
		D_ERROR("Failed to list recx "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	if (number == 0) {
		rc = 0;
		D_GOTO(out, rc);
	}

	if (size > 2000) {
		D_ERROR("recx size is too large: %d, "DF_RC"\n", (int)size, DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* create the dataset with the correct type size */
	rx_dtype = H5Tcreate(H5T_OPAQUE, size);
	if (rx_dtype < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed create rx_dtype "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rx_dset = H5Dcreate(file, rec_name, rx_dtype, rx_dspace, H5P_DEFAULT,
			    plist, H5P_DEFAULT);
	if (rx_dset < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed create rx_dset "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	size = 0;
	while (!daos_anchor_is_eof(&recx_anchor)) {
		memset(recxs, 0, sizeof(recxs));
		memset(eprs, 0, sizeof(eprs));

		/* list all recx for this dkey/akey */
		number = 5;
		rc = daos_obj_list_recx(*oh, DAOS_TX_NONE, dkey, akey, &size, &number, recxs,
					eprs, &recx_anchor, true, NULL);
		if (rc != 0) {
			D_ERROR("Failed to list record extent "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		/* if no recx is returned for this dkey/akey move on */
		if (number == 0)
			continue;
		for (i = 0; i < number; i++) {
			buf_len = recxs[i].rx_nr * size;
			D_ALLOC(buf, buf_len);
			if (buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			memset(&sgl, 0, sizeof(sgl));
			memset(&iov, 0, sizeof(iov));

			/* set iod values */
			(*iod).iod_type  = DAOS_IOD_ARRAY;
			(*iod).iod_size  = size;
			(*iod).iod_nr    = 1;
			(*iod).iod_recxs = &recxs[i];

			/* set sgl values */
			sgl.sg_nr     = 1;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs   = &iov;

			d_iov_set(&iov, buf, buf_len);
			/* fetch recx values from source */
			rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL, NULL);
			if (rc != 0) {
				D_ERROR("Failed to fetch object "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* Sanity check */
			if (sgl.sg_nr_out != 1) {
				D_ERROR("Failed to fetch array recxs "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			(*bytes_read) += buf_len;

			/* write data to record dset */
			mem_dims[0] = recxs[i].rx_nr;
			rx_memspace = H5Screate_simple(1, mem_dims, mem_dims);
			if (rx_memspace < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed create rx_memspace "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* extend dataset */
			rx_dims[0] += recxs[i].rx_nr;
			status = H5Dset_extent(rx_dset, rx_dims);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to extend rx dataset "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* retrieve extended dataspace */
			rx_dspace = H5Dget_space(rx_dset);
			if (rx_dspace < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to get rx dataspace "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* select hyperslab to write based on rx index and number */
			start = (hsize_t)recxs[i].rx_idx;
			count = (hsize_t)recxs[i].rx_nr;
			status = H5Sselect_hyperslab(rx_dspace, H5S_SELECT_AND, &start,
						     NULL, &count, NULL);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to select hyperslab "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* write the hyperslab selection to dataset */
			status = H5Dwrite(rx_dset, rx_dtype, rx_memspace, rx_dspace, H5P_DEFAULT,
					  sgl.sg_iovs[0].iov_buf);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to write rx_dset "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* get size of buffer needed from nalloc */
			status = H5Sencode1(rx_dspace, NULL, &nalloc);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to get size of buffer needed "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* encode dataspace description in buffer then store in attribute
			 * on dataset
			 */
			D_ALLOC(encode_buf, nalloc * sizeof(unsigned char));
			if (encode_buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			status = H5Sencode1(rx_dspace, encode_buf, &nalloc);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to encode dataspace "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			/* created attribute in HDF5 file with encoded
			 * dataspace for this record extent
			 */
			path_len = snprintf(attr_num_str, ATTR_NAME_LEN, "-%d", attr_num);
			if (path_len >= ATTR_NAME_LEN) {
				D_ERROR("attribute number string is too long\n");
				D_GOTO(out, rc);
			}
			path_len = snprintf(attr_name, ATTR_NAME_LEN, "%s%d%s", "A-",
					    *akey_index, attr_num_str);
			if (path_len >= ATTR_NAME_LEN) {
				D_ERROR("attribute name is too long\n");
				D_GOTO(out, rc);
			}

			attr_dims[0] = 1;
			attr_dspace = H5Screate_simple(1, attr_dims, NULL);
			if (attr_dspace < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to create attr "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			attr_dtype = H5Tcreate(H5T_OPAQUE, nalloc);
			if (attr_dtype < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to create attr dtype "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			selection_attr = H5Acreate2(rx_dset, attr_name, attr_dtype, attr_dspace,
						    H5P_DEFAULT, H5P_DEFAULT);
			if (selection_attr < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to create selection attr "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			status = H5Awrite(selection_attr, attr_dtype, encode_buf);
			if (status < 0) {
				rc = -DER_MISC;
				D_ERROR("Failed to write attr "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			H5Aclose(selection_attr);
			H5Sclose(rx_memspace);
			H5Tclose(attr_dtype);
			D_FREE(encode_buf);
			D_FREE(buf);
			attr_num++;
		}
	}
out:
	if (plist >= 0)
		H5Pclose(plist);
	if (rx_dtype >= 0)
		H5Tclose(rx_dtype);
	if (rx_dspace >= 0)
		H5Sclose(rx_dspace);
	if (rx_dset >= 0)
		H5Dclose(rx_dset);
	if (rc != 0) {
		H5Tclose(attr_dtype);
		H5Sclose(rx_memspace);
		H5Aclose(selection_attr);
		D_FREE(encode_buf);
		D_FREE(buf);
	}
	return rc;
}

/**
 * Realloc a buffer by a multiple of 2 with a minimum length.
 *
 * \param[in,out]
 *		buf	[in]: Pointer to current buffer.
 *			[out]: Pointer to new buffer. Unmodified on failure.
 *
 * \param[in,out]
 *		buf_len	[in]: Current length of \a buf. If 0, DSR_KEY_BUF_LEN will be used.
 *			[out]: New length of \a buf.
 *
 * \param[in]	min_len	Minimum buffer length needed.
 *
 * \return		0		Success
 *			-DER_NOMEM	Out of memory
 */
static int
realloc_buf(char **buf, daos_size_t *buf_len, daos_size_t min_len)
{
	daos_size_t	new_len = *buf_len;
	char		*new_buf = NULL;

	if (new_len == 0)
		new_len = DSR_KEY_BUF_LEN;

	while (new_len < min_len)
		new_len *= 2;

	D_REALLOC(new_buf, *buf, *buf_len, new_len);
	if (new_buf == NULL)
		return -DER_NOMEM;

	*buf = new_buf;
	*buf_len = new_len;

	return 0;
}

static int
serialize_akeys(struct dsr_h5_args *args, daos_key_t diov, int *akey_index,
		daos_handle_t *oh, int *total_akeys, uint64_t *bytes_read)
{
	int			rc = 0;
	int			i = 0;
	daos_anchor_t		akey_anchor = {0};
	d_sg_list_t		akey_sgl;
	d_iov_t			akey_iov;
	daos_key_desc_t		akey_kds[DSR_AKEY_BATCH_SIZE] = {0};
	uint32_t		akey_number = DSR_AKEY_BATCH_SIZE;
	struct dsr_h5_akey	*akey_data = NULL;
	daos_size_t		akey_data_len = DSR_AKEY_BATCH_SIZE;
	char			*akey_ptr = NULL;
	daos_key_t		aiov = {0};
	daos_iod_t		iod = {0};
	size_t			rec_name_len = 32;
	char			rec_name[rec_name_len];
	int			path_len = 0;
	hvl_t			*akey_val;
	char			*key_buf = NULL;
	daos_size_t		key_buf_len = DSR_KEY_BUF_LEN;
	hvl_t			*single_val;

	D_ALLOC(key_buf, key_buf_len);
	if (key_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(akey_data, akey_data_len);
	if (akey_data == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	while (!daos_anchor_is_eof(&akey_anchor)) {
		memset(akey_kds, 0, sizeof(akey_kds));
		memset(key_buf, 0, key_buf_len);
		memset(akey_data, 0, sizeof(struct dsr_h5_dkey) * akey_data_len);
		akey_number = akey_data_len;

		akey_sgl.sg_nr     = 1;
		akey_sgl.sg_nr_out = 0;
		akey_sgl.sg_iovs   = &akey_iov;

retry_list_akey:
		d_iov_set(&akey_iov, key_buf, key_buf_len);
		/* get akeys */
		rc = daos_obj_list_akey(*oh, DAOS_TX_NONE, &diov, &akey_number, akey_kds,
					&akey_sgl, &akey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* allocate a larger buffer and try again */
			rc = realloc_buf(&key_buf, &key_buf_len, akey_kds[0].kd_key_len);
			if (rc)
				D_GOTO(out, rc);
			goto retry_list_akey;
		} else if (rc != 0) {
			D_ERROR("failed to list akeys "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		/* if no akeys returned move on */
		if (akey_number == 0)
			continue;

		/* parse out individual akeys based on key length and number of dkeys returned */
		for (akey_ptr = key_buf, i = 0; i < akey_number; i++) {
			akey_val = &(akey_data)[i].akey_val;
			D_ALLOC(akey_val->p, (uint64_t)akey_kds[i].kd_key_len * sizeof(char));
			if (akey_val->p == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(akey_val->p, (void *)akey_ptr, (uint64_t)akey_kds[i].kd_key_len);
			akey_val->len = (uint64_t)akey_kds[i].kd_key_len;
			akey_data[i].rec_dset_id = *akey_index;
			memset(&aiov, 0, sizeof(aiov));
			d_iov_set(&aiov, akey_val->p, akey_val->len);

			/* set iod values */
			iod.iod_nr   = 1;
			iod.iod_type = DAOS_IOD_SINGLE;
			iod.iod_size = DAOS_REC_ANY;
			iod.iod_recxs = NULL;
			iod.iod_name  = aiov;

			/* do a fetch (with NULL sgl) of single value type, and if that returns
			 * iod_size == 0, then a single value does not exist.
			 */
			rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, &diov, 1, &iod, NULL, NULL, NULL);
			if (rc != 0) {
				D_ERROR("Failed to fetch object "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}

			single_val = &(akey_data)[i].rec_single_val;

			/* if iod_size == 0 then this is a DAOS_IOD_ARRAY type */
			if ((int)iod.iod_size == 0) {
				/* set single value field to NULL, 0 for array types */
				single_val->p = NULL;
				single_val->len = 0;

				/* create a record dset only for array types */
				memset(&rec_name, 0, rec_name_len);
				path_len = snprintf(rec_name, rec_name_len, "%d", *akey_index);
				if (path_len > rec_name_len) {
					D_ERROR("Record name too long "DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				rc = serialize_recx_array(args->file, &diov, &aiov, rec_name,
							  akey_index, oh, &iod, bytes_read);
				if (rc != 0) {
					D_ERROR("Failed to serialize recx array "DF_RC"\n",
						DP_RC(rc));
					D_GOTO(out, rc);
				}
			} else {
				rc = fetch_recx_single(single_val, &diov, oh, &iod, bytes_read);
				if (rc != 0) {
					D_ERROR("Failed to serialize recx single "DF_RC"\n",
						DP_RC(rc));
					D_GOTO(out, rc);
				}
			}
			/* advance to next akey returned */
			akey_ptr += akey_kds[i].kd_key_len;
			(*akey_index)++;
		}

		/* serialize this batch of akeys */
		rc = write_akeys(args->file, akey_data, akey_number);
		if (rc != 0)
			D_GOTO(out, rc);

		/* increment total akeys successfully serialized */
		*total_akeys += akey_number;

		/* free all rec values */
		for (i = 0; i < akey_number; i++) {
			D_FREE(akey_data[i].akey_val.p);
			D_FREE(akey_data[i].rec_single_val.p);
		}
	}

	
	
out:
	if (rc && akey_data != NULL) {
		for (i = 0; i < akey_data_len; i++) {
			D_FREE(akey_data[i].akey_val.p);
			D_FREE(akey_data[i].rec_single_val.p);
		}
	}
	D_FREE(akey_data);
	D_FREE(key_buf);
	return rc;
}

static int
fetch_kv_rec(hvl_t *kv_val, daos_key_t dkey, daos_handle_t *oh,
	   char *dkey_val, uint64_t *bytes_read)
{
	int		rc;
	daos_size_t	size = 0;

	/* Get the size of the value */
	rc = daos_kv_get(*oh, DAOS_TX_NONE, 0, dkey_val, &size, kv_val->p, NULL);
	if (rc != 0) {
		D_ERROR("Failed to fetch KV object "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Allocate buffer to hold value. Caller is responsible for calling D_FREE. */
	D_ALLOC(kv_val->p, (uint64_t)size);
	if (kv_val->p == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	kv_val->len = (uint64_t)size;

	/* Fetch the value */
	rc = daos_kv_get(*oh, DAOS_TX_NONE, 0, dkey_val, &size, kv_val->p, NULL);
	if (rc != 0) {
		D_ERROR("Failed to fetch KV object "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	(*bytes_read) += size;
out:
	if (rc)
		D_FREE(kv_val->p);
	return rc;
}

static int
serialize_dkeys(struct dsr_h5_args *args, daos_obj_id_t oid, daos_handle_t coh,
		int *dkey_index, int *akey_index, daos_handle_t *oh, bool is_kv,
		int *total_dkeys, int *total_akeys, uint64_t *bytes_read)
{
	int			rc = 0;
	int			i = 0;
	daos_anchor_t		dkey_anchor = {0};
	d_sg_list_t		dkey_sgl;
	d_iov_t			dkey_iov;
	daos_key_desc_t		dkey_kds[DSR_DKEY_BATCH_SIZE] = {0};
	uint32_t		dkey_number = DSR_DKEY_BATCH_SIZE;
	struct dsr_h5_dkey	*dkey_data = NULL;
	daos_size_t		dkey_data_len = DSR_DKEY_BATCH_SIZE;
	char			*dkey_ptr;
	daos_key_t		diov;
	hvl_t			*dkey_val;
	char			*key_buf = NULL;
	daos_size_t		key_buf_len = DSR_KEY_BUF_LEN;

	D_ALLOC(key_buf, key_buf_len);
	if (key_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(dkey_data, dkey_number);
		if (dkey_data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

	while (!daos_anchor_is_eof(&dkey_anchor)) {
		memset(dkey_kds, 0, sizeof(dkey_kds));
		memset(key_buf, 0, key_buf_len);
		memset(dkey_data, 0, sizeof(struct dsr_h5_dkey) * dkey_data_len);

		dkey_number		= dkey_data_len;
		dkey_sgl.sg_nr		= 1;
		dkey_sgl.sg_nr_out	= 0;
		dkey_sgl.sg_iovs	= &dkey_iov;

retry_list_dkey:
		d_iov_set(&dkey_iov, key_buf, key_buf_len);
		if (is_kv)
			rc = daos_kv_list(*oh, DAOS_TX_NONE, &dkey_number, dkey_kds,
					  &dkey_sgl, &dkey_anchor, NULL);
		else
			rc = daos_obj_list_dkey(*oh, DAOS_TX_NONE, &dkey_number,
						dkey_kds, &dkey_sgl, &dkey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* allocate a larger buffer and try again */
			rc = realloc_buf(&key_buf, &key_buf_len, dkey_kds[0].kd_key_len);
			if (rc)
				D_GOTO(out, rc);
			goto retry_list_dkey;
		} else if (rc != 0) {
			D_ERROR("failed to list dkeys "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		/* if no dkeys were returned move on */
		if (dkey_number == 0)
			continue;

		/* parse out individual dkeys based on key length and
		 * number of dkeys returned
		 */
		for (dkey_ptr = key_buf, i = 0; i < dkey_number; i++) {
			dkey_val = &(dkey_data)[i].dkey_val;
			D_ALLOC(dkey_val->p, (uint64_t)dkey_kds[i].kd_key_len * sizeof(char));
			if (dkey_val->p == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(dkey_val->p, (void *)dkey_ptr, (uint64_t)dkey_kds[i].kd_key_len);
			dkey_val->len = (uint64_t)dkey_kds[i].kd_key_len;
			dkey_data[i].rec_kv_val.p = NULL;
			dkey_data[i].rec_kv_val.len = 0;
			memset(&diov, 0, sizeof(diov));
			d_iov_set(&diov, dkey_val->p, dkey_val->len);
			if (is_kv) {
				dkey_data[i].akey_offset = 0;

				/* fetch KV record into dkey buffer */
				rc = fetch_kv_rec(&dkey_data[i].rec_kv_val, diov, oh,
						  (char *)dkey_val->p, bytes_read);
				if (rc != 0) {
					D_ERROR("Failed to fetch KV record "DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
			} else {
				dkey_data[i].akey_offset = *akey_index;

				rc = serialize_akeys(args, diov, akey_index, oh,
						     total_akeys, bytes_read);
				if (rc != 0) {
					D_ERROR("Failed to serialize akeys "DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
			}
			dkey_ptr += dkey_kds[i].kd_key_len;
			(*dkey_index)++;
		}

		/* serialize this batch of dkeys. */
		rc = write_dkeys(args->file, dkey_data, dkey_number);
		if (rc != 0)
			D_GOTO(out, rc);

		/* increment total dkeys successfully serialized */
		(*total_dkeys) += dkey_number;

		/* free all KV rec values */
		for (i = 0; i < dkey_number; i++)
			D_FREE(dkey_data[i].dkey_val.p);
	}

out:
	if (rc && dkey_data != NULL) {
		/* free all KV rec values */
		for (i = 0; i < dkey_data_len; i++)
			D_FREE(dkey_data[i].dkey_val.p);
	}

	D_FREE(key_buf);
	D_FREE(dkey_data);
	return rc;
}

static int
create_oid_dataset(hid_t file)
{
	int			rc = 0;
	hid_t			status = 0;
	hsize_t			oid_dims[1];
	hsize_t			oid_max_dims[1];
	hsize_t			oid_chunk_dims[1];
	hid_t			oid_dspace = -1;
	hid_t			oid_memtype = -1;
	hid_t			oid_dset = -1;
	hid_t			oid_plist = -1;
	herr_t			err = 0;

	/* inital size of 0, chunked to handle extension */
	oid_dims[0] = 0;
	oid_max_dims[0] = H5S_UNLIMITED;
	oid_chunk_dims[0] = 128;

	oid_plist = H5Pcreate(H5P_DATASET_CREATE);
	if (oid_plist < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create OID Property List "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	oid_dspace = H5Screate_simple(1, oid_dims, oid_max_dims);
	if (oid_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create OID Dataspace "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_layout(oid_plist, H5D_CHUNKED);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set OID Dataspace Layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_chunk(oid_plist, 1, oid_chunk_dims);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set OID Dataspace Chunk "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	oid_memtype = H5Tcreate(H5T_COMPOUND, sizeof(struct dsr_h5_oid));
	if (oid_memtype < 0) {
		D_ERROR("Failed to create OID memtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(oid_memtype, "OID Hi", HOFFSET(struct dsr_h5_oid, oid_hi),
			   H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("Failed to insert OID Hi\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(oid_memtype, "OID Low", HOFFSET(struct dsr_h5_oid, oid_low),
			   H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("Failed to insert OID Low\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(oid_memtype, "Dkey Offset", HOFFSET(struct dsr_h5_oid, dkey_offset),
			   H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("Failed to insert Dkey Offset\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_dset = H5Dcreate(file, "Oid Data", oid_memtype, oid_dspace, H5P_DEFAULT,
			     oid_plist, H5P_DEFAULT);
	if (oid_dset < 0) {
		D_ERROR("Failed to create OID Data\n");
		D_GOTO(out, rc = -DER_MISC);
	}

out:
	if (oid_dspace >= 0)
		H5Sclose(oid_dspace);
	if (oid_memtype >= 0)
		H5Tclose(oid_memtype);
	if (oid_dset >= 0)
		H5Dclose(oid_dset);
	if (oid_plist >= 0)
		H5Pclose(oid_plist);
	return rc;
}

static int
create_dkey_dataset(hid_t file)
{
	int			rc = 0;
	hid_t			status = 0;
	hsize_t			dkey_dims[1];
	hsize_t			dkey_max_dims[1];
	hsize_t			dkey_chunk_dims[1];
	hid_t			dkey_dspace = -1;
	hid_t			dkey_memtype = -1;
	hid_t			dkey_dset = -1;
	hid_t			dkey_plist = -1;
	hid_t			dkey_vtype = -1;
	herr_t			err = 0;

	/* inital size of 0, chunked to handle extension */
	dkey_dims[0] = 0;
	dkey_max_dims[0] = H5S_UNLIMITED;
	dkey_chunk_dims[0] = 128;

	dkey_plist = H5Pcreate(H5P_DATASET_CREATE);
	if (dkey_plist < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create Dkey Property List "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	dkey_dspace = H5Screate_simple(1, dkey_dims, dkey_max_dims);
	if (dkey_dspace < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to create Dkey Dataspace "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_layout(dkey_plist, H5D_CHUNKED);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set Dkey Dataspace Layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	err = H5Pset_chunk(dkey_plist, 1, dkey_chunk_dims);
	if (err < 0) {
		rc = -DER_MISC;
		D_ERROR("Failed to set Dkey Dataspace Chunk "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	dkey_memtype = H5Tcreate(H5T_COMPOUND, sizeof(struct dsr_h5_dkey));
	if (dkey_memtype < 0) {
		D_ERROR("Failed to create Dkey memtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(dkey_memtype, "Akey Offset", HOFFSET(struct dsr_h5_dkey, akey_offset),
			   H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("Failed to insert Akey Offset\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	dkey_vtype = H5Tvlen_create(H5T_NATIVE_OPAQUE);
	if (dkey_vtype < 0) {
		D_ERROR("Failed to create Dkey vtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(dkey_memtype, "Dkey Value", HOFFSET(struct dsr_h5_dkey, dkey_val),
			   dkey_vtype);
	if (status < 0) {
		D_ERROR("Failed to insert Dkey Value\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(dkey_memtype, "Record KV Value", HOFFSET(struct dsr_h5_dkey, rec_kv_val),
			   dkey_vtype);
	if (status < 0) {
		D_ERROR("Failed to insert Record KV Value\n");
		D_GOTO(out, rc = -DER_MISC);
	}


	dkey_dset = H5Dcreate(file, "Dkey Data", dkey_memtype, dkey_dspace, H5P_DEFAULT,
			     dkey_plist, H5P_DEFAULT);
	if (dkey_dset < 0) {
		D_ERROR("Failed to create Dkey Data\n");
		D_GOTO(out, rc = -DER_MISC);
	}

out:
	if (dkey_vtype >= 0)
		H5Tclose(dkey_vtype);
	if (dkey_dspace >= 0)
		H5Sclose(dkey_dspace);
	if (dkey_memtype >= 0)
		H5Tclose(dkey_memtype);
	if (dkey_dset >= 0)
		H5Dclose(dkey_dset);
	if (dkey_plist >= 0)
		H5Pclose(dkey_plist);
	return rc;
}

static int
create_akey_dataset(hid_t file)
{
	int			rc = 0;
	hid_t			status = 0;
	hsize_t			akey_dims[1];
	hsize_t			akey_max_dims[1];
	hsize_t			akey_chunk_dims[1];
	hid_t			akey_dspace = -1;
	hid_t			akey_memtype = -1;
	hid_t			akey_dset = -1;
	hid_t			akey_plist = -1;
	hid_t			akey_vtype = -1;
	herr_t			err = 0;

	/* inital size of 0, chunked to handle extension */
	akey_dims[0] = 0;
	akey_max_dims[0] = H5S_UNLIMITED;
	akey_chunk_dims[0] = 128;

	akey_plist = H5Pcreate(H5P_DATASET_CREATE);
	if (akey_plist < 0) {
		D_ERROR("Failed to create Akey Property List\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	akey_dspace = H5Screate_simple(1, akey_dims, akey_max_dims);
	if (akey_dspace < 0) {
		D_ERROR("Failed to create Akey Dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	err = H5Pset_layout(akey_plist, H5D_CHUNKED);
	if (err < 0) {
		D_ERROR("Failed to set Akey Dataspace Layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = -DER_MISC);
	}

	err = H5Pset_chunk(akey_plist, 1, akey_chunk_dims);
	if (err < 0) {
		D_ERROR("Failed to set Akey Dataspace Chunk\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	akey_memtype = H5Tcreate(H5T_COMPOUND, sizeof(struct dsr_h5_akey));
	if (akey_memtype < 0) {
		D_ERROR("Failed to create Akey memtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(akey_memtype, "Dataset ID", HOFFSET(struct dsr_h5_akey, rec_dset_id),
			   H5T_NATIVE_UINT64);
	if (status < 0) {
		D_ERROR("Failed to insert Dataset ID\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	akey_vtype = H5Tvlen_create(H5T_NATIVE_OPAQUE);
	if (akey_vtype < 0) {
		D_ERROR("Failed to create Akey vtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(akey_memtype, "Record Single Value",
			   HOFFSET(struct dsr_h5_akey, rec_single_val), akey_vtype);
	if (status < 0) {
		D_ERROR("Failed to insert Record Single Value\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	status = H5Tinsert(akey_memtype, "Akey Value", HOFFSET(struct dsr_h5_akey, akey_val),
			   akey_vtype);
	if (status < 0) {
		D_ERROR("Failed to insert Akey Value\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	akey_dset = H5Dcreate(file, "Akey Data", akey_memtype, akey_dspace, H5P_DEFAULT,
			     akey_plist, H5P_DEFAULT);
	if (akey_dset < 0) {
		D_ERROR("Failed to create Akey Data\n");
		D_GOTO(out, rc = -DER_MISC);
	}

out:
	if (akey_vtype >= 0)
		H5Tclose(akey_vtype);
	if (akey_dspace >= 0)
		H5Sclose(akey_dspace);
	if (akey_memtype >= 0)
		H5Tclose(akey_memtype);
	if (akey_dset >= 0)
		H5Dclose(akey_dset);
	if (akey_plist >= 0)
		H5Pclose(akey_plist);
	return rc;
}

static int
serialize_version(struct dsr_h5_args *args, float version)
{
	int	rc = 0;
	hid_t	status = 0;
	char	*version_name = "Version";
	hsize_t	version_attr_dims[1];
	hid_t	version_attr_type = H5T_NATIVE_FLOAT;
	hid_t	version_attr_dspace = -1;
	hid_t	version_attr = -1;

	version_attr_dims[0] = 1;
	version_attr_dspace = H5Screate_simple(1, version_attr_dims, NULL);
	if (version_attr_dspace < 0) {
		D_ERROR("Failed to create version attribute dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	version_attr = H5Acreate2(args->file, version_name, version_attr_type, version_attr_dspace,
				  H5P_DEFAULT, H5P_DEFAULT);
	if (version_attr < 0) {
		D_ERROR("Failed to create version attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Awrite(version_attr, version_attr_type, &version);
	if (status < 0) {
		D_ERROR("Failed to write attribute\n");
		D_GOTO(out, rc = -DER_MISC);
	}
out:
	if (version_attr_dspace >= 0)
		H5Sclose(version_attr_dspace);
	if (version_attr >= 0)
		H5Aclose(version_attr);
	return rc;
}

/* TODO: put stats into a stat struct instead of passing several variables */
/* TODO: signature should be something like:
 *	 daos_cont_serialize(coh, filename, optional snapshot, stats)
 *	 Similar for other daos_* functions defined in this file.
 */
/* TODO: daos_cont_serialize_obj so mpifileutils, etc. can share the API */
int
daos_cont_serialize(daos_prop_t *props, int num_attrs, char **names, char **buffers, size_t *sizes,
		    int *total_oids, int *total_dkeys, int *total_akeys, uint64_t *bytes_read,
		    daos_handle_t coh, char *filename)
{
	int			rc = 0;
	int			rc2 = 0;
	hid_t			status = 0;
	int			i = 0;
	int			dkey_index = 0;
	int			akey_index = 0;
	struct dsr_h5_args	args = {0};
	daos_anchor_t		anchor;
	daos_epoch_t		epoch;
	daos_epoch_range_t	epr;
	daos_handle_t		toh;
	daos_obj_id_t		oids[DSR_OID_BATCH_SIZE];
	struct dsr_h5_oid	*oid_data = NULL;
	daos_handle_t		oh;
	uint32_t		oids_nr;
	bool			is_kv = false;
	float			version = 0.0;
	hid_t			usr_attr_memtype = -1;
	hid_t			usr_attr_name_vtype = -1;
	hid_t			usr_attr_val_vtype = -1;

	if (filename == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	/* Create HDF5 file for serialization */
	args.file = H5Fcreate(filename, H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT);
	if (args.file < 0) {
		D_ERROR("Failed to create HDF5 file: %s\n", filename);
		D_GOTO(out, rc = -DER_IO);
	}

	D_PRINT("Serializing Container to: %s\n", filename);

	/* Create datasets upfront */
	rc = create_oid_dataset(args.file);
	if (rc) {
		D_ERROR("Failed to create OID Dataset "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = create_dkey_dataset(args.file);
	if (rc) {
		D_ERROR("Failed to create Dkey Dataset "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = create_akey_dataset(args.file);
	if (rc) {
		D_ERROR("Failed to create Akey Dataset "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* serialize cont version */
	rc = serialize_version(&args, version);
	if (rc != 0) {
		D_ERROR("Failed to serialize version\n");
		D_GOTO(out, rc);
	}

	/* serialize cont props */
	rc = daos_cont_serialize_props(args.file, props);
	if (rc != 0) {
		D_ERROR("failed to serialize cont layout "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* serialize usr_attrs if there are any */
	if (num_attrs > 0) {
		/* create User Attributes Dataset for file */
		usr_attr_memtype = H5Tcreate(H5T_COMPOUND, sizeof(struct dsr_h5_usr_attr));
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
				   HOFFSET(struct dsr_h5_usr_attr, attr_name),
				   usr_attr_name_vtype);
		if (status < 0) {
			D_ERROR("failed to insert into compound datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Tinsert(usr_attr_memtype, "Attribute Value",
				   HOFFSET(struct dsr_h5_usr_attr, attr_val),
				   usr_attr_val_vtype);
		if (status < 0) {
			D_ERROR("failed to insert into compound datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rc = daos_cont_serialize_attrs(args.file, &usr_attr_memtype,
					       num_attrs, names, buffers,
					       sizes);
		if (rc != 0) {
			D_ERROR("failed to serialize usr attributes "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	/* create oid, dkey, akey buffers for container data */
	D_ALLOC_ARRAY(oid_data, DSR_OID_BATCH_SIZE);
	if (oid_data == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* create snapshot, open oit, then starting iterating over oids */
	rc = daos_cont_create_snap_opt(coh, &epoch, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create snapshot: "DF_RC, DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = daos_oit_open(coh, epoch, &toh, NULL);
	if (rc != 0) {
		D_ERROR("Failed to open object iterator: "DF_RC, DP_RC(rc));
		D_GOTO(out_snap, rc);
	}
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		memset(oid_data, 0, sizeof(struct dsr_h5_oid) * DSR_OID_BATCH_SIZE);

		oids_nr = DSR_OID_BATCH_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			D_ERROR("Failed to list objects: "DF_RC, DP_RC(rc));
			D_GOTO(out_oit, rc);
		}

		/* list object ID's */
		for (i = 0; i < oids_nr; i++) {
			oid_data[i].oid_hi = oids[i].hi;
			oid_data[i].oid_low = oids[i].lo;
			oid_data[i].dkey_offset = dkey_index;

			is_kv = daos_is_kv(oids[i]);
			if (is_kv) {
				rc = daos_kv_open(coh, oids[i], DAOS_OO_RW, &oh, NULL);
				if (rc != 0) {
					D_ERROR("Failed to open kv object: "DF_RC, DP_RC(rc));
					D_GOTO(out_oit, rc);
				}

				/* serialize dkeys */
				rc = serialize_dkeys(&args, oids[i], coh, &dkey_index,
						     &akey_index, &oh, is_kv, total_dkeys,
						     total_akeys, bytes_read);
				if (rc != 0) {
					D_ERROR("Failed to serialize dkeys: "DF_RC, DP_RC(rc));
					D_GOTO(err_kv_obj, rc);
				}

				rc = daos_kv_close(oh, NULL);
				if (rc != 0) {
					D_ERROR("Failed to close kv object: "DF_RC, DP_RC(rc));
					D_GOTO(out_oit, rc);
				}
			} else {
				rc = daos_obj_open(coh, oids[i], 0, &oh, NULL);
				if (rc != 0) {
					D_ERROR("Failed to open object: "DF_RC, DP_RC(rc));
					D_GOTO(out_oit, rc);
				}

				/* serialize dkeys */
				rc = serialize_dkeys(&args, oids[i], coh, &dkey_index,
						     &akey_index, &oh, is_kv, total_dkeys,
						     total_akeys, bytes_read);

				if (rc != 0) {
					D_ERROR("Failed to serialize dkeys: "DF_RC, DP_RC(rc));
					D_GOTO(err_obj, rc);
				}

				rc = daos_obj_close(oh, NULL);
				if (rc != 0) {
					D_ERROR("Failed to close object: "DF_RC, DP_RC(rc));
					D_GOTO(out_oit, rc);
				}
			}
			(*total_oids)++;
		}

		/* serialize this batch of oids. */
		rc = write_oids(args.file, oid_data, oids_nr);
		if (rc != 0)
			D_GOTO(out_oit, rc);
	}

	D_GOTO(out_oit, rc);
err_kv_obj:
	rc2 = daos_kv_close(oh, NULL);
	if (rc2 != 0)
		D_ERROR("Failed to close kv object\n");
err_obj:
	rc2 = daos_obj_close(oh, NULL);
	if (rc2 != 0)
		D_ERROR("Failed to close object\n");
out_oit:
	rc2 = daos_oit_close(toh, NULL);
	if (rc2 != 0) {
		D_ERROR("Failed to close object iterator\n");
		D_GOTO(out, rc2);
	}
out_snap:
	epr.epr_lo = epoch;
	epr.epr_hi = epoch;
	rc2 = daos_cont_destroy_snap(coh, epr, NULL);
	if (rc2 != 0)
		D_ERROR("Failed to destroy snapshot "DF_RC"\n", DP_RC(rc));
out:
	if (args.file >= 0)
		H5Fclose(args.file);
	D_FREE(oid_data);
	if (usr_attr_name_vtype >= 0)
		H5Tclose(usr_attr_name_vtype);
	if (usr_attr_val_vtype >= 0)
		H5Tclose(usr_attr_val_vtype);
	return rc;
}

static int
cont_deserialize_recx(hvl_t *akey_val, daos_handle_t *oh, daos_key_t diov, int num_attrs,
		      hid_t *rx_dtype, hid_t *rx_dspace,
		      hid_t *rx_dset, hid_t *rx_memspace, uint64_t *bytes_written)
{
	int		rc = 0;
	hid_t		status = 0;
	int		i = 0;
	ssize_t		attr_len = 0;
	char		attr_name_buf[ATTR_NAME_LEN];
	hsize_t		attr_space;
	hid_t		attr_type = -1;
	size_t		type_size;
	size_t		rx_dtype_size;
	unsigned char	*decode_buf  = NULL;
	hid_t		rx_range_id = -1;
	hsize_t		*rx_range = NULL;
	uint64_t	recx_len = 0;
	void		*recx_data = NULL;
	hssize_t	nblocks = 0;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_iod_t	iod;
	daos_recx_t	recxs;
	hid_t		aid = -1;
	hsize_t		mem_dims[1];
	hsize_t		start;
	hsize_t		count;
	uint64_t	buf_size;

	for (i = 0; i < num_attrs; i++) {
		memset(attr_name_buf, 0, sizeof(attr_name_buf));
		aid = H5Aopen_idx(*rx_dset, (unsigned int)i);
		if (aid < 0) {
			D_ERROR("Failed to open attribute\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		attr_len = H5Aget_name(aid, ATTR_NAME_LEN, attr_name_buf);
		if (attr_len < 0) {
			D_ERROR("Failed to get attribute name\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		attr_space = H5Aget_storage_size(aid);
		if (attr_space < 0) {
			D_ERROR("Failed to get attribute space\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		attr_type = H5Aget_type(aid);
		if (attr_type < 0) {
			D_ERROR("Failed to get attribute type\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		type_size = H5Tget_size(attr_type);
		if (type_size < 0) {
			D_ERROR("Failed to get type size\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rx_dtype_size = H5Tget_size(*rx_dtype);
		if (rx_dtype_size < 0) {
			D_ERROR("Failed to get rx dtype size\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		D_ALLOC(decode_buf, type_size * attr_space);
		if (decode_buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		D_ALLOC(rx_range, type_size * attr_space);
		if (rx_range == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		status = H5Aread(aid, attr_type, decode_buf);
		if (status < 0) {
			D_ERROR("Failed to read attribute\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rx_range_id = H5Sdecode(decode_buf);
		if (rx_range_id < 0) {
			D_ERROR("Failed to decode attribute buffer\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		nblocks = H5Sget_select_hyper_nblocks(rx_range_id);
		if (nblocks < 0) {
			D_ERROR("Failed to get hyperslab blocks\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		status = H5Sget_select_hyper_blocklist(rx_range_id, 0, nblocks, rx_range);
		if (status < 0) {
			D_ERROR("Failed to get blocklist\n");
			D_GOTO(out, rc = -DER_MISC);
		}

		start = rx_range[0];
		count = (rx_range[1] - rx_range[0]) + 1;
		status = H5Sselect_hyperslab(*rx_dspace, H5S_SELECT_AND, &start, NULL,
					     &count, NULL);
		if (status < 0) {
			D_ERROR("Failed to select hyperslab\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		recx_len = count;
		D_ALLOC(recx_data, recx_len * rx_dtype_size);
		if (recx_data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		mem_dims[0] = count;
		*rx_memspace = H5Screate_simple(1, mem_dims, mem_dims);
		status = H5Dread(*rx_dset, *rx_dtype, *rx_memspace, *rx_dspace,
				 H5P_DEFAULT, recx_data);
		if (status < 0) {
			D_ERROR("Failed to read record extent\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		memset(&sgl, 0, sizeof(sgl));
		memset(&iov, 0, sizeof(iov));
		memset(&iod, 0, sizeof(iod));
		memset(&recxs, 0, sizeof(recxs));
		d_iov_set(&iod.iod_name, akey_val->p, akey_val->len);
		/* set iod values */
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = rx_dtype_size;
		iod.iod_nr    = 1;

		recxs.rx_nr = recx_len;
		recxs.rx_idx = start;
		iod.iod_recxs = &recxs;

		/* set sgl values */
		sgl.sg_nr     = 1;
		sgl.sg_iovs   = &iov;

		buf_size = recx_len * rx_dtype_size;
		d_iov_set(&iov, recx_data, buf_size);

		/* update fetched recx values and place in destination object */
		rc = daos_obj_update(*oh, DAOS_TX_NONE, 0, &diov, 1, &iod, &sgl, NULL);
		if (rc != 0) {
			D_ERROR("Failed to update object: "DF_RC, DP_RC(rc));
			D_GOTO(out, rc);
		}
		(*bytes_written) += buf_size;
	}
out:
	/* TODO: leak: need to close for every loop iteration */
	if (attr_type >= 0)
		H5Tclose(attr_type);
	if (aid >= 0)
		H5Aclose(aid);
	/* TODO: leak: need to free for every loop iteration */
	D_FREE(rx_range);
	D_FREE(recx_data);
	D_FREE(decode_buf);
	return rc;
}

static int
cont_deserialize_akeys(struct dsr_h5_args *args, daos_key_t diov, uint64_t *ak_off, int k,
		       daos_handle_t *oh, int *total_akeys, uint64_t *bytes_written)
{
	int		rc = 0;
	daos_key_t	aiov;
	int		rx_ndims;
	uint64_t	index = 0;
	int		len = 0;
	int		num_attrs;
	size_t		single_tsize;
	void		*single_data = NULL;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_iod_t	iod;
	char		*dset_name = NULL;
	hvl_t		*akey_val;
	hvl_t		*rec_single_val;
	hid_t		rx_dset = -1;
	hid_t		rx_dspace = -1;
	hid_t		rx_memspace = -1;
	hid_t		rx_dtype = -1;
	hid_t		plist = -1;
	hsize_t		rx_dims[1];

	memset(&aiov, 0, sizeof(aiov));
	akey_val = &(args->akey_data)[*ak_off + k].akey_val;
	rec_single_val = &(args->akey_data)[*ak_off + k].rec_single_val;
	d_iov_set(&aiov, (void *)akey_val->p, akey_val->len);

	/* if the len of the single value is set to zero,
	 * then this akey points to an array record dataset
	 */
	if (rec_single_val->len == 0) {
		index = *ak_off + k;
		len = snprintf(NULL, 0, "%lu", index);
		D_ALLOC(dset_name, len + 1);
		if (dset_name == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		snprintf(dset_name, len + 1, "%lu", index);
		rx_dset = H5Dopen(args->file, dset_name, H5P_DEFAULT);
		if (rx_dset < 0) {
			D_ERROR("Failed to read rx_dset\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rx_dspace = H5Dget_space(rx_dset);
		if (rx_dspace < 0) {
			D_ERROR("Failed to get rx_dsapce\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rx_dtype = H5Dget_type(rx_dset);
		if (rx_dtype < 0) {
			D_ERROR("Failed to read rx_dtype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		plist = H5Dget_create_plist(rx_dset);
		if (plist < 0) {
			D_ERROR("Failed to get plist\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rx_ndims = H5Sget_simple_extent_dims(rx_dspace, rx_dims, NULL);
		if (rx_ndims < 0) {
			D_ERROR("Failed to get rx ndims\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		num_attrs = H5Aget_num_attrs(rx_dset);
		if (num_attrs < 0) {
			D_ERROR("Failed to get num attrs\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		rc = cont_deserialize_recx(&args->akey_data[*ak_off + k].akey_val, oh, diov, num_attrs,
					   &rx_dtype, &rx_dspace, &rx_dset, &rx_memspace,
					   bytes_written);
		if (rc != 0) {
			D_ERROR("Failed to deserialize recx "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	} else {
		memset(&sgl, 0, sizeof(sgl));
		memset(&iov, 0, sizeof(iov));
		memset(&iod, 0, sizeof(iod));
		single_tsize = rec_single_val->len;
		if (single_tsize == 0) {
			D_ERROR("Failed to get size of type in single record datatype\n");
			D_GOTO(out, rc = -DER_MISC);
		}
		D_ALLOC(single_data, single_tsize);
		if (single_data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		memcpy(single_data, rec_single_val->p, rec_single_val->len);

		/* set iod values */
		iod.iod_type  = DAOS_IOD_SINGLE;
		iod.iod_size  = single_tsize;
		iod.iod_nr    = 1;
		iod.iod_recxs = NULL;
		iod.iod_name  = aiov;

		/* set sgl values */
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, single_data, single_tsize);

		/* update fetched recx values and place in destination object */
		rc = daos_obj_update(*oh, DAOS_TX_NONE, 0, &diov, 1, &iod, &sgl, NULL);
		if (rc != 0) {
			D_ERROR("Failed to update object: "DF_RC, DP_RC(rc));
			D_GOTO(out, rc);
		}
		(*bytes_written) += single_tsize;
		D_FREE(single_data);
	}
	(*total_akeys)++;
out:
	if (rx_dset >= 0)
		H5Dclose(rx_dset);
	if (rx_dspace >= 0)
		H5Sclose(rx_dspace);
	if (rx_dtype >= 0)
		H5Tclose(rx_dtype);
	if (plist >= 0)
		H5Pclose(plist);
	D_FREE(dset_name);
	D_FREE(single_data);
	return rc;
}

static int
cont_deserialize_keys(struct dsr_h5_args *args, uint64_t *total_dkeys_this_oid, uint64_t *dk_off,
		      daos_handle_t *oh, hsize_t dkey_dims[], hsize_t akey_dims[],
		      int *total_dkeys, int *total_akeys, uint64_t *bytes_written)
{
	int		rc = 0;
	int		j = 0;
	daos_key_t	diov;
	uint64_t	ak_off = 0;
	uint64_t	ak_next = 0;
	uint64_t	total_akeys_this_dkey = 0;
	int		k = 0;
	hvl_t		*dkey_val;
	hvl_t		*rec_kv_val;
	daos_size_t	kv_single_size = 0;

	for (j = 0; j < *total_dkeys_this_oid; j++) {
		memset(&diov, 0, sizeof(diov));
		dkey_val = &(args->dkey_data)[*dk_off + j].dkey_val;
		rec_kv_val = &(args->dkey_data)[*dk_off + j].rec_kv_val;
		d_iov_set(&diov, (void *)dkey_val->p, dkey_val->len);
		ak_off = args->dkey_data[*dk_off + j].akey_offset;
		ak_next = 0;
		total_akeys_this_dkey = 0;
		if (*dk_off + j + 1 < (int)dkey_dims[0]) {
			ak_next = args->dkey_data[(*dk_off + j) + 1].akey_offset;
			total_akeys_this_dkey = ak_next - ak_off;
		} else if (*dk_off + j == ((int)dkey_dims[0] - 1)) {
			total_akeys_this_dkey = ((int)akey_dims[0]) - ak_off;
		}

		/* if rec_kv_val.len != 0 then skip akey iteration, we can write data back
		 * into DAOS using the daos_kv.h API using just oid, dkey, and key value
		 * (stored in dkey dataset)
		 */

		/* run daos_kv_put on rec_kv_val (dkey val) and key (dkey) */
		/* skip akey iteration for DAOS_OF_KV_FLAT objects
		 */
		if (rec_kv_val->len > 0) {
			kv_single_size = rec_kv_val->len;
			rc = daos_kv_put(*oh, DAOS_TX_NONE, 0, dkey_val->p, kv_single_size,
					 rec_kv_val->p, NULL);
			if (rc != 0) {
				D_ERROR("failed to write kv object "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			(*bytes_written) += kv_single_size;
		} else {
			for (k = 0; k < total_akeys_this_dkey; k++) {
				rc = cont_deserialize_akeys(args, diov, &ak_off, k, oh,
							    total_akeys, bytes_written);
				if (rc != 0) {
					D_ERROR("failed to deserialize akeys "DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
			}
		}
		(*total_dkeys)++;
	}
out:
	return rc;
}

static int
deserialize_oids(struct dsr_h5_args *args, int *total_oids, int *total_dkeys, int *total_akeys, uint64_t *bytes_written,
		 daos_handle_t coh, char *filename)
{
	int			rc = 0;
	int			i = 0;
	hid_t			status = 0;
	int			oid_ndims = 0;
	int			dkey_ndims = 0;
	int			akey_ndims = 0;
	hsize_t			oid_dims[1];
	hid_t			oid_dset = -1;
	hid_t			oid_dspace = -1;
	hid_t			oid_dtype = -1;
	hsize_t			dkey_dims[1];
	hid_t			dkey_dset = -1;
	hid_t			dkey_dspace = -1;
	hid_t			dkey_vtype = -1;
	hsize_t			akey_dims[1];
	hid_t			akey_dset = -1;
	hid_t			akey_dspace = -1;
	hid_t			akey_vtype = -1;
	struct dsr_h5_oid	*oid_data = NULL;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	bool			is_kv = false;
	uint64_t                total_dkeys_this_oid = 0;
	uint64_t		dk_off = 0;
	uint64_t		dk_next = 0;

	/* read oid data */
	/* TODO: read oids in batches instead of storing all in memory */
	oid_dset = H5Dopen(args->file, "Oid Data", H5P_DEFAULT);
	if (oid_dset < 0) {
		D_ERROR("Failed to open Oid Dataset\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	oid_dspace = H5Dget_space(oid_dset);
	if (oid_dspace < 0) {
		D_ERROR("Failed to get oid dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	oid_dtype = H5Dget_type(oid_dset);
	if (oid_dtype < 0) {
		D_ERROR("Failed to get oid datatype\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	oid_ndims = H5Sget_simple_extent_dims(oid_dspace, oid_dims, NULL);
	if (oid_ndims < 0) {
		D_ERROR("Failed to get oid dimensions\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	if (oid_dims[0] > 0) {
		D_ALLOC_ARRAY(oid_data, oid_dims[0]);
		if (oid_data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		status = H5Dread(oid_dset, oid_dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, oid_data);
		if (status < 0) {
			D_ERROR("Failed to get oid data\n");
			D_GOTO(out, rc = -DER_MISC);
		}
	}

	/* read dkey data */
	/* TODO: read dkeys in batches instead of storing all in memory */
	dkey_dset = H5Dopen(args->file, "Dkey Data", H5P_DEFAULT);
	if (dkey_dset < 0) {
		D_ERROR("Failed to open dkey data\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	dkey_dspace = H5Dget_space(dkey_dset);
	if (dkey_dspace < 0) {
		D_ERROR("Failed to get dkey dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	dkey_vtype = H5Dget_type(dkey_dset);
	if (dkey_vtype < 0) {
		D_ERROR("Failed to get dkey vtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	dkey_ndims = H5Sget_simple_extent_dims(dkey_dspace, dkey_dims, NULL);
	if (dkey_ndims < 0) {
		D_ERROR("Failed to get dkey dimensions\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	if (dkey_dims[0] > 0) {
		D_ALLOC_ARRAY(args->dkey_data, dkey_dims[0]);
		if (args->dkey_data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		status = H5Dread(dkey_dset, dkey_vtype, H5S_ALL, H5S_ALL, H5P_DEFAULT,
				 args->dkey_data);
		if (status < 0) {
			D_ERROR("Failed to read dkey dataset\n");
			D_GOTO(out, rc = -DER_MISC);
		}
	}

	/* read akey data */
	/* TODO: read akeys in batches instead of storing all in memory */
	akey_dset = H5Dopen(args->file, "Akey Data", H5P_DEFAULT);
	if (akey_dset < 0) {
		D_ERROR("Failed to open akey dataset\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	akey_dspace = H5Dget_space(akey_dset);
	if (akey_dspace < 0) {
		D_ERROR("Failed to get akey dataspace\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	akey_vtype = H5Dget_type(akey_dset);
	if (akey_vtype < 0) {
		D_ERROR("Failed to get akey vtype\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	akey_ndims = H5Sget_simple_extent_dims(akey_dspace, akey_dims, NULL);
	if (akey_ndims < 0) {
		D_ERROR("Failed to get akey dimensions\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	if (akey_dims[0] > 0) {
		D_ALLOC_ARRAY(args->akey_data, akey_dims[0]);
		if (args->akey_data == NULL)
			D_GOTO(out, rc =  -DER_NOMEM);
		status = H5Dread(akey_dset, akey_vtype, H5S_ALL, H5S_ALL, H5P_DEFAULT,
				 args->akey_data);
		if (status < 0) {
			D_ERROR("Failed to read akey dataset\n");
			D_GOTO(out, rc = -DER_MISC);
		}
	}

	/* iterate over read key data from file and write it to a new DAOS container */
	for (i = 0; i < (int)oid_dims[0]; i++) {
		oid.lo = oid_data[i].oid_low;
		oid.hi = oid_data[i].oid_hi;
		is_kv = daos_is_kv(oid);
		if (is_kv) {
			rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
			if (rc != 0) {
				D_ERROR("failed to open kv object "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
		} else {
			rc = daos_obj_open(coh, oid, 0, &oh, NULL);
			if (rc != 0) {
				D_ERROR("failed to open object "DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
		}

		dk_off = oid_data[i].dkey_offset;
		dk_next = 0;
		total_dkeys_this_oid = 0;
		if (i + 1 < (int)oid_dims[0]) {
			dk_next = oid_data[i + 1].dkey_offset;
			total_dkeys_this_oid = dk_next - dk_off;
		} else if (i == ((int)oid_dims[0] - 1)) {
			total_dkeys_this_oid = (int)dkey_dims[0] - (dk_off);
		}

		rc = cont_deserialize_keys(args, &total_dkeys_this_oid, &dk_off, &oh,
					   dkey_dims, akey_dims, total_dkeys, total_akeys,
					   bytes_written);
		if (rc != 0) {
			D_ERROR("failed to deserialize keys "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		if (is_kv) {
			rc = daos_kv_close(oh, NULL);
			if (rc != 0) {
				D_ERROR("failed to close kv object: "DF_RC, DP_RC(rc));
				D_GOTO(out, rc);
			}
		} else {
			rc = daos_obj_close(oh, NULL);
			if (rc != 0) {
				D_ERROR("failed to close object: "DF_RC, DP_RC(rc));
				D_GOTO(out, rc);
			}
		}
		(*total_oids)++;
	}
out:
	if (oid_dset >= 0)
		H5Dclose(oid_dset);
	if (oid_dspace >= 0)
		H5Sclose(oid_dspace);
	if (oid_dtype >= 0)
		H5Tclose(oid_dtype);
	if (dkey_dset >= 0)
		H5Dclose(dkey_dset);
	if (dkey_dspace >= 0)
		H5Sclose(dkey_dspace);
	if (dkey_vtype >= 0)
		H5Tclose(dkey_vtype);
	if (akey_dset >= 0)
		H5Dclose(akey_dset);
	if (akey_dspace >= 0)
		H5Sclose(akey_dspace);
	if (akey_vtype >= 0)
		H5Tclose(akey_vtype);
	D_FREE(oid_data);
	D_FREE(args->dkey_data);
	D_FREE(args->akey_data);
	return rc;
}

static int
read_layout_version(hid_t file, float *version)
{
	int	rc = 0;
	hid_t	status = 0;
	hid_t	version_attr = -1;
	hid_t	version_attr_dtype = -1;

	version_attr = H5Aopen(file, "Version", H5P_DEFAULT);
	if (version_attr < 0) {
		D_ERROR("Failed to open version attr\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	version_attr_dtype = H5Aget_type(version_attr);
	if (version_attr_dtype < 0) {
		D_ERROR("Failed to get attr type\n");
		D_GOTO(out, rc = -DER_MISC);
	}
	status = H5Aread(version_attr, version_attr_dtype, version);
	if (status < 0) {
		D_ERROR("Failed to read version\n");
		D_GOTO(out, rc = -DER_MISC);
	}

out:
	if (version_attr >= 0)
		H5Aclose(version_attr);
	if (version_attr_dtype >= 0)
		H5Tclose(version_attr_dtype);
	return rc;
}

int
daos_cont_deserialize(int *total_oids, int *total_dkeys, int *total_akeys, uint64_t *bytes_written,
		      daos_handle_t coh, char *filename)
{
	int			rc = 0;
	struct dsr_h5_args	args = {0};
	float			version;

	/* Open HDF5 file */
	args.file = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (args.file < 0) {
		D_ERROR("Failed to open HDF5 file\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	/* Verify layout version in compatible */
	rc = read_layout_version(args.file, &version);
	if (rc)
		D_GOTO(out, rc);

	if (version > SERIALIZE_VERSION) {
		rc = -DER_INVAL;
		D_ERROR("deserialize version not compatible with serialization version "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Deserialize objects */
	rc = deserialize_oids(&args, total_oids, total_dkeys, total_akeys, bytes_written, coh, filename);
	if (rc) {
		D_ERROR("Failed to deserialize OIDs: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (args.file >= 0)
		H5Fclose(args.file);
	return rc;
}
#if defined(__cplusplus)
}
#endif
