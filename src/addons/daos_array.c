/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos_m
 *
 * src/addons/daos_array.c
 */

#include <daos/common.h>
#include <daos_types.h>
#include <daos_event.h>
#include <daos_api.h>
#include <daos_array.h>

#define DD_SUBSYS	DD_FAC(client)

/* #define ARRAY_DEBUG */

/** MSC - Those need to be configurable later through hints */
/** Array cell size - curently a byte array i.e. 1 byte */
#define D_ARRAY_CELL_SIZE 1
/** Bytes to store in a dkey before moving to the next one in the group */
#define D_ARRAY_DKEY_BLOCK_SIZE		1048576
/** Num blocks to store in each dkey before creating the next group */
#define D_ARRAY_DKEY_NUM_BLOCKS		3
/** Number of dkeys in a group */
#define D_ARRAY_DKEY_NUM		4
#define D_ARRAY_DKEY_GRP_CHUNK		(D_ARRAY_DKEY_BLOCK_SIZE * \
					 D_ARRAY_DKEY_NUM)
#define D_ARRAY_DKEY_GRP_SIZE		(D_ARRAY_DKEY_BLOCK_SIZE * \
					 D_ARRAY_DKEY_NUM_BLOCKS * \
					 D_ARRAY_DKEY_NUM)

enum array_op_t {
	D_ARRAY_OP_WRITE,
	D_ARRAY_OP_READ
};

struct io_params {
	daos_key_t		dkey;
	char			*dkey_str;
	char			*akey_str;
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	daos_event_t		event;
	struct io_params	*next;
};

static bool
io_extent_same(daos_array_ranges_t *ranges, daos_sg_list_t *sgl);

static int
compute_dkey(daos_off_t array_i, daos_size_t *num_records,
	     daos_off_t *record_i, char **obj_dkey);

static int
create_sgl(daos_sg_list_t *user_sgl, daos_size_t num_records,
	   daos_off_t *sgl_off, daos_size_t *sgl_i, daos_sg_list_t *sgl);

static int
array_access_kv(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev,
		enum array_op_t op_type);

static int
get_highest_dkey(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev,
		 uint32_t *max_hi, uint32_t *max_lo);

#if 0
static int
daos_array_parse_env_vars(void)
{
	char *cell_size_val = NULL;
	char *dkey_block_len_val = NULL;
	char *dkey_num_blocks_val = NULL;
	char *num_dkeys_val = NULL;

	cell_size_val = getenv("D_ARRAY_ARRAY_CELL_SIZE");
	if (cell_size_val) {
		/* sscanf(cell_size_val, "%zu", &cell_size_g); */

		printf("D_ARRAY_ARRAY_CELL_SIZE = %s %zu\n",
		       cell_size_val, cell_size_g);

		if (cell_size_g != 1) {
			D_ERROR("Only a 1 byte cell size is supported.\n");
			return -1;
		}
	} else {
		cell_size_g = D_ARRAY_CELL_SIZE;
	}

	dkey_block_len_val = getenv("D_ARRAY_ARRAY_DKEY_BLOCK_LEN");
	if (dkey_block_len_val) {
		/* sscanf(dkey_block_len_val, "%zu", &dkey_block_len_g); */

		printf("D_ARRAY_ARRAY_DKEY_BLOCK_LEN = %s %zu\n",
		       dkey_block_len_val, dkey_block_len_g);
	} else {
		dkey_block_len_g = D_ARRAY_DKEY_BLOCK_SIZE;
	}

	dkey_num_blocks_val = getenv("D_ARRAY_ARRAY_DKEY_NUM_BLOCKS");
	if (dkey_num_blocks_val) {
		/* sscanf(dkey_num_blocks_val, "%zu", &dkey_num_blocks_g); */

		printf("D_ARRAY_ARRAY_DKEY_NUM_BLOCKS = %s %zu\n",
		       dkey_num_blocks_val, dkey_num_blocks_g);
	} else {
		dkey_num_blocks_g = D_ARRAY_DKEY_NUM_BLOCKS;
	}

	num_dkeys_val = getenv("D_ARRAY_ARRAY_NUM_DKEYS");
	if (num_dkeys_val) {
		/* sscanf(num_dkeys_val, "%zu", &num_dkeys_g); */

		printf("D_ARRAY_ARRAY_NUM_DKEYS = %s %zu\n",
		       num_dkeys_val, num_dkeys_g);
	} else {
		num_dkeys_g = D_ARRAY_DKEY_NUM;
	}
}
#endif

static bool
io_extent_same(daos_array_ranges_t *ranges, daos_sg_list_t *sgl)
{
	daos_size_t ranges_len;
	daos_size_t sgl_len;
	daos_size_t u;

	ranges_len = 0;
#ifdef ARRAY_DEBUG
	printf("USER ARRAY RANGE -----------------------\n");
	printf("ranges_nr = %zu\n", ranges->ranges_nr);
#endif
	for (u = 0 ; u < ranges->ranges_nr ; u++) {
		ranges_len += ranges->ranges[u].len;
#ifdef ARRAY_DEBUG
		printf("%zu: length %zu, index %d\n",
			u, ranges->ranges[u].len, (int)ranges->ranges[u].index);
#endif
	}
#ifdef ARRAY_DEBUG
	printf("------------------------------------\n");
	printf("USER SGL -----------------------\n");
	printf("sg_nr = %u\n", sgl->sg_nr.num);
#endif
	sgl_len = 0;
	for (u = 0 ; u < sgl->sg_nr.num; u++) {
		sgl_len += sgl->sg_iovs[u].iov_len;
#ifdef ARRAY_DEBUG
		printf("%zu: length %zu, Buf %p\n",
			u, sgl->sg_iovs[u].iov_len, sgl->sg_iovs[u].iov_buf);
#endif
	}

	return (ranges_len == sgl_len);
}

static int
compute_dkey(daos_off_t array_i, daos_size_t *num_records, daos_off_t *record_i,
	     char **dkey_str)
{
	daos_off_t	byte_a;		/* Byte address of I/O */
	daos_size_t	dkey_grp;	/* Which grp of dkeys to look into */
	daos_off_t	dkey_grp_a;	/* Byte address of dkey_grp */
	daos_off_t	rel_byte_a;	/* offset relative to grp */
	daos_size_t	dkey_num;	/* The dkey number for access */
	daos_size_t	grp_iter;	/* round robin iteration number */
	daos_off_t	dkey_byte_a;	/* address of dkey relative to group */

	byte_a = array_i * D_ARRAY_CELL_SIZE;

	/* Compute dkey group number and address */
	dkey_grp = byte_a / D_ARRAY_DKEY_GRP_SIZE;
	dkey_grp_a = dkey_grp * D_ARRAY_DKEY_GRP_SIZE;

	/* Compute dkey number within dkey group */
	rel_byte_a = byte_a - dkey_grp_a;
	dkey_num = (size_t)(rel_byte_a / D_ARRAY_DKEY_BLOCK_SIZE) %
		D_ARRAY_DKEY_NUM;

	/* Compute relative offset/index in dkey */
	grp_iter = rel_byte_a / D_ARRAY_DKEY_GRP_CHUNK;
	dkey_byte_a = (grp_iter * D_ARRAY_DKEY_GRP_CHUNK) +
		(dkey_num * D_ARRAY_DKEY_BLOCK_SIZE);
	*record_i = (D_ARRAY_DKEY_BLOCK_SIZE * grp_iter) +
		(rel_byte_a - dkey_byte_a);

	/* Number of records to access in current dkey */
	*num_records = ((grp_iter + 1) * D_ARRAY_DKEY_BLOCK_SIZE) - *record_i;

	if (dkey_str) {
		asprintf(dkey_str, "%zu_%zu", dkey_grp, dkey_num);
		if (*dkey_str == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -DER_NOMEM;
		}
	}

	return 0;
}

static int
create_sgl(daos_sg_list_t *user_sgl, daos_size_t num_records,
	   daos_off_t *sgl_off, daos_size_t *sgl_i, daos_sg_list_t *sgl)
{
	daos_size_t	k;
	daos_size_t	rem_records;
	daos_size_t	cur_i;
	daos_off_t	cur_off;

	cur_i = *sgl_i;
	cur_off = *sgl_off;
	sgl->sg_nr.num = k = 0;
	sgl->sg_iovs = NULL;
	rem_records = num_records;

	/**
	 * Keep iterating through the user sgl till we populate our sgl to
	 * satisfy the number of records to read/write from the KV object
	 */
	do {
		D_ASSERT(user_sgl->sg_nr.num > cur_i);

		sgl->sg_nr.num++;
		sgl->sg_iovs = (daos_iov_t *)realloc
			(sgl->sg_iovs, sizeof(daos_iov_t) * sgl->sg_nr.num);
		if (sgl->sg_iovs == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -DER_NOMEM;
		}

		sgl->sg_iovs[k].iov_buf = user_sgl->sg_iovs[cur_i].iov_buf +
			cur_off;

		if (rem_records >=
		    (user_sgl->sg_iovs[cur_i].iov_len - cur_off)) {
			sgl->sg_iovs[k].iov_len =
				user_sgl->sg_iovs[cur_i].iov_len - cur_off;
			cur_i++;
			cur_off = 0;
		} else {
			sgl->sg_iovs[k].iov_len = rem_records;
			cur_off += rem_records;
		}

		sgl->sg_iovs[k].iov_buf_len = sgl->sg_iovs[k].iov_len;
		rem_records -= sgl->sg_iovs[k].iov_len;

		k++;
	} while (rem_records && user_sgl->sg_nr.num > cur_i);

	sgl->sg_nr.num_out = 0;

	*sgl_i = cur_i;
	*sgl_off = cur_off;

	return 0;
}

static int
array_access_kv(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_ranges_t *ranges, daos_sg_list_t *user_sgl,
		daos_csum_buf_t *csums, daos_event_t *ev,
		enum array_op_t op_type)
{
	daos_off_t	cur_off;/* offset into user buf to track current pos */
	daos_size_t	cur_i;	/* index into user sgl to track current pos */
	daos_size_t	records; /* Number of records to access in cur range */
	daos_off_t	array_i; /* object array index of current range */
	daos_size_t	u;
	daos_size_t	num_records;
	daos_off_t	record_i;
	daos_csum_buf_t	null_csum;
	char		*akey = strdup("akey_not_used");
	struct io_params *head, *current;
	daos_size_t	num_ios;
	int		rc;

	if (ranges == NULL) {
		D_ERROR("NULL ranges passed\n");
		return -DER_INVAL;
	}
	if (user_sgl == NULL) {
		D_ERROR("NULL scatter-gather list passed\n");
		return -DER_INVAL;
	}

	if (!io_extent_same(ranges, user_sgl)) {
		D_ERROR("Unequal extents of memory and array descriptors\n");
		return -DER_INVAL;
	}

#if 0
	rc = daos_array_parse_env_vars();
	if (rc != 0) {
		D_ERROR("Array read failed (%d)\n", rc);
		return rc;
	}
#endif

	cur_off = 0;
	cur_i = 0;
	u = 0;
	num_ios = 0;
	records = ranges->ranges[0].len;
	array_i = ranges->ranges[0].index;
	daos_csum_set(&null_csum, NULL, 0);

	/**
	 * Loop over every range, but at the same time combine consecutive
	 * ranges that belong to the same dkey. If the user gives ranges that
	 * are not increasing in offset, they probably won't be combined unless
	 * the separating ranges also belong to the same dkey.
	 */
	while (u < ranges->ranges_nr) {
		daos_iod_t	*iod, local_iod;
		daos_sg_list_t	*sgl, local_sgl;
		char		*dkey_str;
		daos_key_t	*dkey, local_dkey;
		bool		user_sgl_used = false;
		daos_size_t	dkey_records;
		daos_event_t	*io_event;
		daos_size_t	i;

		if (ranges->ranges[u].len == 0) {
			u++;
			if (u < ranges->ranges_nr) {
				records = ranges->ranges[u].len;
				array_i = ranges->ranges[u].index;
			}
			continue;
		}

		if (ev != NULL) {
			struct io_params *params = NULL;

			params = (struct io_params *)malloc
				(sizeof(struct io_params));
			if (params == NULL) {
				D_ERROR("Failed memory allocation\n");
				return -DER_NOMEM;
			}

			if (num_ios == 0) {
				head = params;
				current = head;
			} else {
				current->next = params;
				current = params;
			}

			iod = &params->iod;
			sgl = &params->sgl;
			io_event = &params->event;
			dkey_str = params->dkey_str;
			dkey = &params->dkey;
			params->akey_str = akey;
			params->next = NULL;

			num_ios++;
		} else {
			iod = &local_iod;
			sgl = &local_sgl;
			dkey_str = NULL;
			dkey = &local_dkey;
			io_event = NULL;
		}

		/**
		 * Compute the dkey given the array index for this range. Also
		 * compute: - the number of records that the dkey can hold
		 * starting at the index where we start writing. - the record
		 * index relative to the dkey.
		 */
		rc = compute_dkey(array_i, &num_records, &record_i, &dkey_str);
		if (rc != 0) {
			D_ERROR("Failed to compute dkey\n");
			return rc;
		}
#ifdef ARRAY_DEBUG
		printf("DKEY IOD %s ---------------------------\n", dkey_str);
		printf("array_i = %d\t num_records = %zu\t record_i = %d\n",
		       (int)array_i, num_records, (int)record_i);
#endif
		daos_iov_set(dkey, (void *)dkey_str, strlen(dkey_str));

		/* set descriptor for KV object */
		daos_iov_set(&iod->iod_name, (void *)akey, strlen(akey));
		iod->iod_kcsum = null_csum;
		iod->iod_nr = 0;
		iod->iod_csums = NULL;
		iod->iod_eprs = NULL;
		iod->iod_recxs = NULL;
		i = 0;
		dkey_records = 0;

		/**
		 * Create the IO descriptor for this dkey. If the entire range
		 * fits in the dkey, continue to the next range to see if we can
		 * combine it fully or partially in the current dkey IOD/
		 */
		do {
			daos_off_t	old_array_i;

			iod->iod_nr++;

			/** add another element to recxs */
			iod->iod_recxs = (daos_recx_t *)realloc
				(iod->iod_recxs, sizeof(daos_recx_t) *
				 iod->iod_nr);
			if (iod->iod_recxs == NULL) {
				D_ERROR("Failed memory allocation\n");
				return -DER_NOMEM;
			}

			/** set the record access for this range */
			iod->iod_recxs[i].rx_rsize = 1;
			iod->iod_recxs[i].rx_idx = record_i;
			iod->iod_recxs[i].rx_nr = (num_records > records) ?
				records : num_records;
#ifdef ARRAY_DEBUG
			printf("Add %zu to ARRAY IOD (size = %zu index = %d)\n",
			       u, iod->iod_recxs[i].rx_nr,
			       (int)iod->iod_recxs[i].rx_idx);
#endif
			/**
			 * if the current range is bigger than what the dkey can
			 * hold, update the array index and number of records in
			 * the current range and break to issue the I/O on the
			 * current KV.
			 */
			if (records > num_records) {
				array_i += num_records;
				records -= num_records;
				dkey_records += num_records;
				break;
			}

			u++;
			i++;
			dkey_records += records;

			/** if there are no more ranges to write, then break */
			if (ranges->ranges_nr <= u)
				break;

			old_array_i = array_i;
			records = ranges->ranges[u].len;
			array_i = ranges->ranges[u].index;

			/**
			 * Boundary case where number of records align with the
			 * end boundary of the KV. break after advancing to the
			 * next range
			 */
			if (records == num_records)
				break;

			/** cont processing the next range in the cur dkey */
			if (array_i < old_array_i + num_records &&
			   array_i >= ((old_array_i + num_records) -
				       D_ARRAY_DKEY_BLOCK_SIZE)) {
				char	*dkey_str_tmp = NULL;

				/**
				 * verify that the dkey is the same as the one
				 * we are working on given the array index, and
				 * also compute the number of records left in
				 * the dkey and the record indexin the dkey.
				 */
				rc = compute_dkey(array_i, &num_records,
						  &record_i, &dkey_str_tmp);
				if (rc != 0) {
					D_ERROR("Failed to compute dkey\n");
					return rc;
				}

				D_ASSERT(strcmp(dkey_str_tmp, dkey_str) == 0);

				free(dkey_str_tmp);
				dkey_str_tmp = NULL;
			} else {
				break;
			}
		} while (1);
#ifdef ARRAY_DEBUG
		printf("END DKEY IOD %s ---------------------------\n",
		       dkey_str);
#endif
		/**
		 * if the user sgl maps directly to the array range, no need to
		 * partition it.
		 */
		if (1 == ranges->ranges_nr && 1 == user_sgl->sg_nr.num &&
		    dkey_records == ranges->ranges[0].len && ev == NULL) {
			sgl = user_sgl;
			user_sgl_used = true;
		}
		/** create an sgl from the user sgl for the current IOD */
		else {
			/* set sgl for current dkey */
			rc = create_sgl(user_sgl, dkey_records, &cur_off,
					&cur_i, sgl);
			if (rc != 0) {
				D_ERROR("Failed to create sgl\n");
				return rc;
			}
#ifdef ARRAY_DEBUG
			daos_size_t s;

			printf("DKEY SGL -----------------------\n");
			printf("sg_nr = %u\n", sgl->sg_nr.num);
			for (s = 0; s < sgl->sg_nr.num; s++) {
				printf("%zu: length %zu, Buf %p\n",
				       s, sgl->sg_iovs[s].iov_len,
				       sgl->sg_iovs[s].iov_buf);
			}
			printf("------------------------------------\n");
#endif
		}

		/**
		 * If this is an asynchronous call, add the I/Os generated here
		 * as children of the event passed from the user. The user polls
		 * on the completion of their event which polls on all the
		 * events here.
		 */
		if (ev != NULL) {
			rc = daos_event_init(io_event, DAOS_HDL_INVAL, ev);
			if (rc != 0) {
				D_ERROR("Failed to init child event (%d)\n",
					rc);
				return rc;
			}
		}

		/* issue KV IO to DAOS */
		if (op_type == D_ARRAY_OP_READ) {
			rc = daos_obj_fetch(oh, epoch, dkey, 1, iod, sgl, NULL,
					    io_event);
			if (rc != 0) {
				D_ERROR("KV Fetch of dkey %s failed (%d)\n",
					dkey_str, rc);
				return rc;
			}
		} else if (op_type == D_ARRAY_OP_WRITE) {
			rc = daos_obj_update(oh, epoch, dkey, 1, iod, sgl,
					     io_event);
			if (rc != 0) {
				D_ERROR("KV Update of dkey %s failed (%d)\n",
					dkey_str, rc);
				return rc;
			}
		} else {
			D_ASSERTF(0, "Invalid array operation.\n");
		}

		if (ev == NULL) {
			if (dkey_str) {
				free(dkey_str);
				dkey_str = NULL;
			}

			if (!user_sgl_used && sgl->sg_iovs) {
				free(sgl->sg_iovs);
				sgl->sg_iovs = NULL;
			}

			if (iod->iod_recxs) {
				free(iod->iod_recxs);
				iod->iod_recxs = NULL;
			}
		}
	} /* end while */

	if (ev && num_ios) {
		rc = daos_event_parent_barrier(ev);
		if (rc != 0) {
			D_ERROR("daos_event_launch Failed (%d)\n", rc);
			return rc;
		}
	}

	if (ev == NULL) {
		free(akey);
		akey = NULL;
	}

	return 0;
}

int
daos_array_read(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev)
{
	int rc;

	rc = array_access_kv(oh, epoch, ranges, sgl, csums, ev,
			     D_ARRAY_OP_READ);
	if (rc != 0) {
		D_ERROR("Array read failed (%d)\n", rc);
		return rc;
	}
	return rc;
}

int
daos_array_write(daos_handle_t oh, daos_epoch_t epoch,
		 daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		 daos_csum_buf_t *csums, daos_event_t *ev)
{
	int rc;

	rc = array_access_kv(oh, epoch, ranges, sgl, csums, ev,
			     D_ARRAY_OP_WRITE);
	if (rc != 0) {
		D_ERROR("Array write failed (%d)\n", rc);
		return rc;
	}
	return rc;
}

#define ENUM_KEY_BUF	32
#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

static int
get_highest_dkey(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev,
		 uint32_t *max_hi, uint32_t *max_lo)
{
	uint32_t	key_nr, i, j;
	daos_sg_list_t  sgl;
	daos_hash_out_t hash_out;
	char		key[ENUM_KEY_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_size_t	len = ENUM_DESC_BUF;
	char		*ptr;
	char		*buf;
	int             rc = 0;

	memset(&hash_out, 0, sizeof(hash_out));
	buf = malloc(ENUM_DESC_BUF);
	if (buf == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	*max_hi = 0;
	*max_lo = 0;

	/** enumerate records */
	for (i = ENUM_DESC_NR, key_nr = 0; !daos_hash_is_eof(&hash_out);
	     i = ENUM_DESC_NR) {
		daos_iov_t	iov;

		memset(buf, 0, ENUM_DESC_BUF);

		sgl.sg_nr.num = 1;
		daos_iov_set(&iov, buf, len);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_dkey(oh, epoch, &i, kds, &sgl, &hash_out,
					ev);
		if (rc != 0) {
			D_ERROR("DKey list failed (%d)\n", rc);
			return rc;
		}

		if (i == 0)
			continue; /* loop should break for EOF */

		key_nr += i;
		for (ptr = buf, j = 0; j < i; j++) {
			uint32_t hi, lo;
			int ret;

			snprintf(key, kds[j].kd_key_len + 1, ptr);
#ifdef ARRAY_DEBUG
			printf("%d: key %s len %d\n", j, key,
				      (int)kds[j].kd_key_len);
#endif
			ret = sscanf(key, "%u_%u", &hi, &lo);
			D_ASSERT(ret == 2);

			if (hi >= *max_hi) {
				*max_hi = hi;
				if (lo > *max_lo)
					*max_lo = lo;
			}
			ptr += kds[j].kd_key_len;
		}
	}

	free(buf);
	buf = NULL;

	return rc;
}

int
daos_array_get_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t *size,
		    daos_event_t *ev)
{
	uint32_t	i;
	uint32_t	max_hi, max_lo;
	daos_off_t	max_offset;
	daos_size_t	max_iter;
	int		rc;

	rc = get_highest_dkey(oh, epoch, NULL, &max_hi, &max_lo);
	if (rc != 0) {
		D_ERROR("Failed to retrieve max dkey (%d)\n", rc);
		return rc;
	}

	/*
	 * Go through all the dkeys in the current group (maxhi_x) and get the
	 * highest index to determine which dkey in the group has the highest
	 * bit.
	 */
	max_iter = 0;
	max_offset = 0;

	for (i = 0 ; i <= max_lo; i++) {
		daos_off_t	offset, index_hi = 0;
		daos_size_t	iter;
		char		key[ENUM_KEY_BUF];

		sprintf(key, "%u_%u", max_hi, i);
		/** retrieve the highest index */
		/** MSC - need new functionality from DAOS to retrieve that. */

		/** Compute the iteration where the highest record is stored */
		iter = index_hi / D_ARRAY_DKEY_BLOCK_SIZE;

		offset = iter * D_ARRAY_DKEY_GRP_CHUNK +
			(index_hi - iter * D_ARRAY_DKEY_BLOCK_SIZE);

		if (iter == max_iter || max_iter == 0) {
			/** D_ASSERT(offset > max_offset); */
			max_offset = offset;
			max_iter = iter;
		} else {
			if (i < max_lo)
				break;
		}
	}

	*size = max_hi * D_ARRAY_DKEY_GRP_SIZE + max_offset;

	return rc;
} /* end daos_array_get_size */

int
daos_array_set_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t size,
		    daos_event_t *ev)
{
	char            *dkey_str = NULL;
	daos_size_t	num_records;
	daos_off_t	record_i;
	uint32_t	new_hi, new_lo;
	uint32_t	key_nr, i, j;
	daos_sg_list_t  sgl;
	daos_hash_out_t hash_out;
	char		key[ENUM_KEY_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_size_t	len = ENUM_DESC_BUF;
	char		*ptr;
	char		*buf;
	bool		shrinking;
	int		rc, ret;

	rc = compute_dkey(size, &num_records, &record_i, &dkey_str);
	if (rc != 0) {
		D_ERROR("Failed to compute dkey\n");
		return rc;
	}

	ret = sscanf(dkey_str, "%u_%u", &new_hi, &new_lo);
	D_ASSERT(ret == 2);

	memset(&hash_out, 0, sizeof(hash_out));
	buf = malloc(ENUM_DESC_BUF);
	if (buf == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	shrinking = false;

	for (i = ENUM_DESC_NR, key_nr = 0; !daos_hash_is_eof(&hash_out);
	     i = ENUM_DESC_NR) {
		daos_iov_t	iov;

		memset(buf, 0, ENUM_DESC_BUF);

		sgl.sg_nr.num = 1;
		daos_iov_set(&iov, buf, len);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_dkey(oh, epoch, &i, kds, &sgl, &hash_out,
					ev);
		if (rc != 0) {
			D_ERROR("DKey list failed (%d)\n", rc);
			return rc;
		}

		if (i == 0)
			continue; /* loop should break for EOF */

		key_nr += i;
		for (ptr = buf, j = 0; j < i; j++) {
			uint32_t hi, lo;

			snprintf(key, kds[j].kd_key_len + 1, ptr);
#ifdef ARRAY_DEBUG
			printf("%d: key %s len %d\n", j, key,
				      (int)kds[j].kd_key_len);
#endif
			/** Keep a record of the highest dkey */
			ret = sscanf(key, "%u_%u", &hi, &lo);
			D_ASSERT(ret == 2);

			if (hi >= new_hi) {
				/** Punch this entire dkey */
				if (lo > new_lo)
					shrinking = true;
				/**
				 * Punch only records that are at higher index
				 * than size.
				 */
				else if (lo == new_lo)
					shrinking = true;
			}
			ptr += kds[j].kd_key_len;
		}
	}

	/** if array is smaller, write a record at the new size */
	if (!shrinking) {
		daos_array_ranges_t	ranges;
		daos_range_t		rg;
		daos_sg_list_t		sgl;
		daos_iov_t		iov;
		uint8_t			val = 0;

		/** set array location */
		ranges.ranges_nr = 1;
		rg.len = D_ARRAY_CELL_SIZE;
		rg.index = size - D_ARRAY_CELL_SIZE;
		ranges.ranges = &rg;

		/** set memory location */
		sgl.sg_nr.num = 1;
		daos_iov_set(&iov, &val, 1);
		sgl.sg_iovs = &iov;

		/** Write */
		rc = daos_array_write(oh, 0, &ranges, &sgl, NULL, NULL);
		if (rc != 0) {
			D_ERROR("Failed to write array (%d)\n", rc);
			return rc;
		}
	}

	free(buf);
	buf = NULL;

	return rc;
} /* end daos_array_set_size */
