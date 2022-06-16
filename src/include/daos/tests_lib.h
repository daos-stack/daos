/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_TESTS_LIB_H__
#define __DAOS_TESTS_LIB_H__

#include <getopt.h>
#include <daos_types.h>
#include <daos/common.h>
#include <daos_mgmt.h>
#include <daos/object.h>
#include <daos/credit.h>

#define assert_success(r)						\
	do {								\
		int __rc = (r);						\
		if (__rc != 0)						\
			fail_msg("Not successful!! Error code: "	\
				 DF_RC, DP_RC(__rc));			\
	} while (0)

#define assert_rc_equal(rc, expected_rc)				\
	do {								\
		if ((rc) == (expected_rc))				\
			break;						\
		print_message("Failure assert_rc_equal %s:%d "		\
			      "%s(%d) != %s(%d)\n", __FILE__, __LINE__, \
			      d_errstr(rc), rc,				\
			      d_errstr(expected_rc), expected_rc);	\
		assert_string_equal(d_errstr(rc), d_errstr(expected_rc)); \
		assert_int_equal(rc, expected_rc);			\
	} while (0)

#define DTS_OCLASS_DEF OC_RP_XSF

/** Fill in readable random bytes into the buffer */
void dts_buf_render(char *buf, unsigned int buf_len);

/** Fill in random uppercase chars into the buffer */
void dts_buf_render_uppercase(char *buf, unsigned int buf_len);

/** generate a unique key */
void dts_key_gen(char *key, unsigned int key_len, const char *prefix);

/** generate a random and unique object ID */
daos_obj_id_t dts_oid_gen(unsigned seed);

/** generate a random and unique baseline object ID */
daos_unit_oid_t dts_unit_oid_gen(enum daos_otype_t type, uint32_t shard);

/** Set rank into the oid */
#define dts_oid_set_rank(oid, rank)	daos_oclass_sr_set_rank(oid, rank)
/** Set target offset into oid */
#define dts_oid_set_tgt(oid, tgt)	daos_oclass_st_set_tgt(oid, tgt)

/**
 * Create a random (optionally) ordered integer array with \a nr elements, value
 * of this array starts from \a base.
 */
uint64_t *dts_rand_iarr_alloc_set(int nr, int base, bool shuffle);
uint64_t *dts_rand_iarr_alloc(int nr);
void dts_rand_iarr_set(uint64_t *array, int nr, int base, bool shuffle);

static inline double
dts_time_now(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

void dts_reset_key(void);

static inline bool
tsc_create_pool(struct credit_context *tsc)
{
	return !tsc->tsc_skip_pool_create;
}

static inline bool
tsc_create_cont(struct credit_context *tsc)
{
	/* Can't skip container if pool isn't also skipped */
	return tsc_create_pool(tsc) || !tsc->tsc_skip_cont_create;
}

/* match BIO_XS_CNT_MAX, which is the max VOS xstreams mapped to a device */
#define MAX_TEST_TARGETS_PER_DEVICE 48
#define DSS_HOSTNAME_MAX_LEN	255

typedef struct {
	uuid_t		device_id;
	char		state[10];
	int		rank;
	char		host[DSS_HOSTNAME_MAX_LEN];
	int		tgtidx[MAX_TEST_TARGETS_PER_DEVICE];
	int		n_tgtidx;
}  device_list;

/** Initialize an SGL with a variable number of IOVs and set the IOV buffers
 *  to the value of the strings passed. This will allocate memory for the iov
 *  structures as well as the iov buffers, so d_sgl_fini(sgl, true) must be
 *  called when sgl is no longer needed.
 *
 * @param sgl		Scatter gather list to initialize
 * @param count		Number of IO Vectors that will be created in the SGL
 * @param d		First string that will be used
 * @param ...		Rest of strings, up to count
 */
void
dts_sgl_init_with_strings(d_sg_list_t *sgl, uint32_t count, char *d, ...);

/** Initialize and SGL with a variable number of IOVs and set the IOV buffers
 *  to the value of the strings passed, repeating the string. This is an
 *  easy way to get larger data in the sgl. This will allocate memory for the
 *  iov structures as well as the iov buffers, so d_sgl_fini(sgl, true) must be
 *  called when sgl is no longer needed.
 *
 * @param sgl		Scatter gather list to initialize
 * @param count		Number of IO Vectors that will be created in the SGL
 * @param repeat	Number of tiems to repeat the string
 * @param d		First string that will be used
 * @param ...		Rest of strings, up to count
 */
void
dts_sgl_init_with_strings_repeat(d_sg_list_t *sgl, uint32_t repeat,
				 uint32_t count, char *d, ...);

#define DTS_CFG_MAX 256
__attribute__ ((__format__(__printf__, 2, 3)))
static inline void
dts_create_config(char buf[DTS_CFG_MAX], const char *format, ...)
{
	va_list	ap;
	int	count;

	va_start(ap, format);
	count = vsnprintf(buf, DTS_CFG_MAX, format, ap);
	va_end(ap);

	if (count >= DTS_CFG_MAX)
		buf[DTS_CFG_MAX - 1] = 0;
}

static inline void
dts_append_config(char buf[DTS_CFG_MAX], const char *format, ...)
{
	va_list	ap;
	int	count = strnlen(buf, DTS_CFG_MAX);

	va_start(ap, format);
	vsnprintf(buf + count, DTS_CFG_MAX - count, format, ap);
	va_end(ap);

	if (strlen(buf) >= DTS_CFG_MAX)
		buf[DTS_CFG_MAX - 1] = 0;
}

/**
 * List all pools created in the specified DAOS system.
 *
 * \param dmg_config_file
 *			[IN]	DMG config file
 * \param group		[IN]	Name of DAOS system managing the service.
 * \param npools	[IN,OUT]
 *				[in] \a pools length in items.
 *				[out] Number of pools in the DAOS system.
 * \param pools		[OUT]	Array of pool mgmt information structures.
 *				NULL is permitted in which case only the
 *				number of pools will be returned in \a npools.
 *				When non-NULL and on successful return, a
 *				service replica rank list (mgpi_svc) is
 *				allocated for each item in \pools.
 *				The rank lists must be freed by the caller.
 *
 * \return			0		Success
 *				-DER_TRUNC	\a pools cannot hold \a npools
 *						items
 */
int dmg_pool_list(const char *dmg_config_file, const char *group,
		  daos_size_t *npools, daos_mgmt_pool_info_t *pools);

/**
 * Create a pool spanning \a tgts in \a grp. Upon successful completion, report
 * back the pool UUID in \a uuid and the pool service rank(s) in \a svc.
 *
 * Targets are assumed to share the same \a size.
 *
 * \param dmg_config_file
 *		[IN]	DMG config file
 * \param uid	[IN]	User owning the pool
 * \param gid	[IN]	Group owning the pool
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param tgts	[IN]	Optional, allocate targets on this list of ranks
 *			If set to NULL, create the pool over all the ranks
 *			available in the service group.
 * \param scm_size
 *		[IN]	Target SCM (Storage Class Memory) size in bytes (i.e.,
 *			maximum amounts of SCM storage space targets can
 *			consume) in bytes. Passing 0 will use the minimal
 *			supported target size.
 * \param nvme_size
 *		[IN]	Target NVMe (Non-Volatile Memory express) size in bytes.
 * \param prop	[IN]	Optional, pool properties.
 * \param svc	[IN]	Number of desired pool service replicas. Callers must
 *			specify svc->rl_nr and allocate a matching
 *			svc->rl_ranks; svc->rl_nr and svc->rl_ranks
 *			content are ignored.
 *		[OUT]	List of actual pool service replicas. svc->rl_nr
 *			is the number of actual pool service replicas, which
 *			shall be equal to or smaller than the desired number.
 *			The first svc->rl_nr elements of svc->rl_ranks
 *			shall be the list of pool service ranks.
 * \param uuid	[OUT]	UUID of the pool created
 */
int dmg_pool_create(const char *dmg_config_file,
		    uid_t uid, gid_t gid, const char *grp,
		    const d_rank_list_t *tgts,
		    daos_size_t scm_size, daos_size_t nvme_size,
		    daos_prop_t *prop,
		    d_rank_list_t *svc, uuid_t uuid);

/**
 * Destroy a pool with \a uuid. If there is at least one connection to this
 * pool, and \a force is zero, then this operation completes with DER_BUSY.
 * Otherwise, the pool is destroyed when the operation completes.
 *
 * \param dmg_config_file
 *		[IN]	DMG config file
 * \param uuid	[IN]	UUID of the pool to destroy
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param force	[IN]	Force destruction even if there are active connections
 */
int dmg_pool_destroy(const char *dmg_config_file,
		     const uuid_t uuid, const char *grp, int force);

/**
 * Set property of the pool with \a pool_uuid.
 *
 * \param dmg_config_file	[IN] DMG config file.
 * \param pool_uuid		[IN] UUID of the pool.
 * \param prop_name		[IN] the name of the property.
 * \param prop_value		[IN] the value of the property.
 */
int
dmg_pool_set_prop(const char *dmg_config_file,
		  const char *prop_name, const char *prop_value,
		  const uuid_t pool_uuid);

/**
 * List all disks in the specified DAOS system.
 *
 * \param dmg_config_file
 *				[IN]	DMG config file
 * \param ndisks	[OUT]
  *				[OUT] Number of drives  in the DAOS system.
 * \param devices	[OUT]	Array of NVMe device information structures.
 *				NULL is permitted in which case only the
 *				number of disks will be returned in \a ndisks.
 */
int dmg_storage_device_list(const char *dmg_config_file, int *ndisks,
			    device_list *devices);

/**
 * Set NVMe device to faulty. Which will trigger the rebuild and all the
 * target attached to the disk will be excluded.
 *
 * \param dmg_config_file
 *		[IN]	DMG config file
 * \param host	[IN]	Nvme set to faulty on host name provided. Only single
					disk can be set to fault for now.
 * \param uuid	[IN]	UUID of the device.
 * \param force	[IN]	Do not require confirmation
 */
int dmg_storage_set_nvme_fault(const char *dmg_config_file,
			       char *host, const uuid_t uuid, int force);
/**
 * Get NVMe Device health stats.
 *
 * \param dmg_config_file
 *		[IN]	DMG config file
 * \param host	[IN]	Get device-health from the given host.
 * \param uuid	[IN]	UUID of the device.
 * \param stats	[IN/OUT]
 *			[in] Health stats for which to get counter value.
 *			[out] Stats counter value.
 */
int dmg_storage_query_device_health(const char *dmg_config_file, char *host,
				    char *stats, const uuid_t uuid);

/**
 * Verify the assumed blobstore device state with the actual enum definition
 * defined in bio.h.
 *
 * \param state	    [IN]    Blobstore state return from daos_mgmt_ger_bs_state()
 * \param state_str [IN]    Assumed blobstore state (ie normal, out, faulty,
 *				teardown, setup)
 *
 * \return		0 on success
 *			1 on failure, meaning the enum definition differs from
 *					expected state
 */
int verify_blobstore_state(int state, const char *state_str);

const char *daos_target_state_enum_to_str(int state);

#endif /* __DAOS_TESTS_LIB_H__ */
